/*
 * dos.c - INT 21h: files, memory, vectors, clock, exit.
 *
 * Follows DinoPark's runtime16 int21 (MIT, sp00nz) for the calls both games
 * make. Files are looked up by base name, case-insensitively, in the game
 * directory, so the original data files work unrenamed on any host.
 */
#include "rt.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#endif

uint16_t g_vec_seg[256], g_vec_off[256];
char g_game_dir[1024] = ".";

#define MAXFH 32
static FILE *g_fh[MAXFH];
static uint16_t g_dta_seg, g_dta_off;
static uint16_t g_high_next = RT_HIGH_LO;

static void read_name(CPU *cpu, uint16_t seg, uint16_t off, char *out, int n)
{
    int i = 0;
    for (; i < n - 1; i++) {
        uint8_t c = mem_read8(cpu, seg, (uint16_t)(off + i));
        if (!c) break;
        out[i] = (char)c;
    }
    out[i] = 0;
}

static const char *base_name(const char *p)
{
    const char *b = p;
    if (b[0] && b[1] == ':') b += 2;
    for (const char *q = b; *q; q++)
        if (*q == '\\' || *q == '/') b = q + 1;
    return b;
}

/* Find `name` in the game directory regardless of case. */
static int resolve_path(const char *dos_name, char *out, size_t n)
{
    const char *b = base_name(dos_name);
    snprintf(out, n, "%s/%s", g_game_dir, b);
#ifdef _WIN32
    return 1;                       /* the filesystem is case-insensitive already */
#else
    DIR *d = opendir(g_game_dir);
    if (!d) return 1;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!strcasecmp(e->d_name, b)) {
            snprintf(out, n, "%s/%s", g_game_dir, e->d_name);
            break;
        }
    }
    closedir(d);
    return 1;
#endif
}

FILE *rt_open_game_file(const char *dos_name, const char *mode)
{
    char path[1200];
    resolve_path(dos_name, path, sizeof path);
    return fopen(path, mode);
}

static int new_handle(FILE *f)
{
    for (int i = 5; i < MAXFH; i++)
        if (!g_fh[i]) { g_fh[i] = f; return i; }
    return -1;
}

static void ok(CPU *cpu)  { cpu->flags &= ~FLAG_CF; }
static void fail(CPU *cpu, uint16_t err) { cpu->ax = err; cpu->flags |= FLAG_CF; }

static void open_file(CPU *cpu, const char *mode, int create_ok)
{
    char name[256];
    read_name(cpu, cpu->ds, cpu->dx, name, sizeof name);
    FILE *f = rt_open_game_file(name, mode);
    if (!f && create_ok) f = rt_open_game_file(name, "w+b");
    int h = f ? new_handle(f) : -1;
    TRACE("[dos] open '%s' %s -> %d\n", name, mode, h);
    if (h < 0) { if (f) fclose(f); fail(cpu, f ? 4 : 2); return; }
    cpu->ax = (uint16_t)h; ok(cpu);
}

/* Directory listing of the game folder, one name at a time. */
#ifdef _WIN32
static HANDLE g_find = INVALID_HANDLE_VALUE;
static WIN32_FIND_DATAA g_fd;
static int g_fd_pending;
static void dir_open(void)
{
    char pat[1100];
    if (g_find != INVALID_HANDLE_VALUE) FindClose(g_find);
    snprintf(pat, sizeof pat, "%s\\*", g_game_dir);
    g_find = FindFirstFileA(pat, &g_fd);
    g_fd_pending = g_find != INVALID_HANDLE_VALUE;
}
static const char *dir_next(void)
{
    if (g_find == INVALID_HANDLE_VALUE) return NULL;
    if (!g_fd_pending && !FindNextFileA(g_find, &g_fd)) { FindClose(g_find); g_find = INVALID_HANDLE_VALUE; return NULL; }
    g_fd_pending = 0;
    return g_fd.cFileName;
}
#else
static DIR *g_find;
static void dir_open(void)
{
    if (g_find) closedir(g_find);
    g_find = opendir(g_game_dir);
}
static const char *dir_next(void)
{
    struct dirent *e = g_find ? readdir(g_find) : NULL;
    if (!e && g_find) { closedir(g_find); g_find = NULL; }
    return e ? e->d_name : NULL;
}
#endif
static char g_find_pat[64];

/* DOS wildcard match, case-insensitive, 8.3 semantics are close enough. */
static int wild(const char *p, const char *s)
{
    for (; *p; p++, s++) {
        if (*p == '*') {
            if (!p[1]) return 1;
            for (; *s; s++) if (wild(p + 1, s)) return 1;
            return wild(p + 1, s);
        }
        if (!*s) return 0;
        if (*p != '?' && toupper((unsigned char)*p) != toupper((unsigned char)*s)) return 0;
    }
    return !*s;
}

static void dta_fill(CPU *cpu, const char *name, uint32_t size)
{
    uint32_t a = seg_off(g_dta_seg, g_dta_off);
    memset(&cpu->mem[a], 0, 0x2B);
    cpu->mem[a + 0x15] = 0x20;
    cpu->mem[a + 0x18] = 0x21; cpu->mem[a + 0x19] = 0x1A;
    for (int i = 0; i < 4; i++) cpu->mem[a + 0x1A + i] = (uint8_t)(size >> (8 * i));
    for (int i = 0; i < 12 && name[i]; i++)
        cpu->mem[a + 0x1E + i] = (uint8_t)toupper((unsigned char)name[i]);
}

static int find_step(CPU *cpu)
{
    const char *name;
    while ((name = dir_next())) {
        if (!wild(g_find_pat, name) || strlen(name) > 12) continue;
        char path[1200];
        snprintf(path, sizeof path, "%s/%s", g_game_dir, name);
        FILE *f = fopen(path, "rb");
        long sz = 0;
        if (f) { fseek(f, 0, SEEK_END); sz = ftell(f); fclose(f); }
        dta_fill(cpu, name, (uint32_t)sz);
        return 1;
    }
    return 0;
}

void dos_int21_impl(CPU *cpu)
{
    uint8_t ah = cpu->ah;
#ifdef GG_SPCHECK
    if (ah == 0x02 || ah == 0x09 || ah == 0x06 || ah == 0x40) {
        static int shown;
        if (!shown++) { extern unsigned g_stk[]; extern int g_stk_depth;
            fprintf(stderr, "[dos] first output (AH=%02X); call chain (%d deep):", ah, g_stk_depth);
            for (int i = 0; i < g_stk_depth && i < 64; i++) fprintf(stderr, " %05X", g_stk[i]);
            fprintf(stderr, "\n"); }
    }
#endif
    switch (ah) {
    case 0x02: fputc(cpu->dl, stdout); break;
    case 0x09: {
        uint32_t a = seg_off(cpu->ds, cpu->dx);
        for (int i = 0; i < 4096 && cpu->mem[a + i] != '$'; i++) fputc(cpu->mem[a + i], stdout);
        fflush(stdout);
        break;
    }
    case 0x0E: cpu->al = 26; break;                       /* select drive: 26 drives */
    case 0x19: cpu->al = 2; break;                        /* current drive C: */
    case 0x1A: g_dta_seg = cpu->ds; g_dta_off = cpu->dx; break;
    case 0x2F: cpu->es = g_dta_seg; cpu->bx = g_dta_off; break;
    case 0x25:
        TRACE("[dos] set vector %02X -> %04X:%04X\n", cpu->al, cpu->ds, cpu->dx);
        g_vec_seg[cpu->al] = cpu->ds; g_vec_off[cpu->al] = cpu->dx;
        break;
    case 0x35:
        cpu->es = g_vec_seg[cpu->al]; cpu->bx = g_vec_off[cpu->al];
        break;
    case 0x2A: {                                          /* date */
        time_t t = time(NULL); struct tm *tm = localtime(&t);
        cpu->cx = (uint16_t)(tm->tm_year + 1900); cpu->dh = (uint8_t)(tm->tm_mon + 1);
        cpu->dl = (uint8_t)tm->tm_mday; cpu->al = (uint8_t)tm->tm_wday;
        break;
    }
    case 0x2C: {                                          /* time */
        time_t t = time(NULL); struct tm *tm = localtime(&t);
        unsigned long ms = rt_ms();
        cpu->ch = (uint8_t)tm->tm_hour; cpu->cl = (uint8_t)tm->tm_min;
        cpu->dh = (uint8_t)tm->tm_sec;  cpu->dl = (uint8_t)((ms / 10) % 100);
        break;
    }
    case 0x30: cpu->ax = 0x0005; cpu->bx = 0; cpu->cx = 0; break;   /* DOS 5.0 */
    case 0x33: cpu->dl = 0; break;                        /* ctrl-break state */
    case 0x3B: ok(cpu); break;                            /* chdir */
    case 0x47:                                            /* cwd: root */
        mem_write8(cpu, cpu->ds, cpu->si, 0); ok(cpu); break;
    case 0x3C: open_file(cpu, "w+b", 0); break;           /* create */
    case 0x5B: open_file(cpu, "w+b", 0); break;           /* create new */
    case 0x3D: {                                          /* open */
        static const char *modes[] = { "rb", "r+b", "r+b" };
        int access = cpu->al & 3;
        open_file(cpu, modes[access > 2 ? 2 : access], 0);
        break;
    }
    case 0x3E:
        if (cpu->bx < MAXFH && g_fh[cpu->bx]) { fclose(g_fh[cpu->bx]); g_fh[cpu->bx] = NULL; }
        ok(cpu); break;
    case 0x3F: {                                          /* read */
        int h = cpu->bx; size_t got = 0;
        uint32_t a = seg_off(cpu->ds, cpu->dx);
        if (h < MAXFH && g_fh[h]) got = fread(&cpu->mem[a], 1, cpu->cx, g_fh[h]);
        else if (h == 0) got = 0;
        else { fail(cpu, 6); break; }
        cpu->ax = (uint16_t)got; ok(cpu); break;
    }
    case 0x40: {                                          /* write */
        int h = cpu->bx; uint32_t a = seg_off(cpu->ds, cpu->dx);
        if (h == 1 || h == 2) {
            fwrite(&cpu->mem[a], 1, cpu->cx, h == 2 ? stderr : stdout);
#ifdef GG_SPCHECK
            { extern unsigned g_stk[]; extern int g_stk_depth;
              fprintf(stderr, "\n[dos] console write; call chain (%d deep):", g_stk_depth);
              for (int i = 0; i < g_stk_depth && i < 64; i++) fprintf(stderr, " %05X", g_stk[i]);
              fprintf(stderr, "\n"); }
#endif
        }
        else if (h < MAXFH && g_fh[h]) {
            if (cpu->cx == 0) { /* DOS: zero-length write truncates; rarely matters */ }
            else fwrite(&cpu->mem[a], 1, cpu->cx, g_fh[h]);
            fflush(g_fh[h]);
        } else { fail(cpu, 6); break; }
        cpu->ax = cpu->cx; ok(cpu); break;
    }
    case 0x41: {                                          /* delete */
        char name[256], path[1200];
        read_name(cpu, cpu->ds, cpu->dx, name, sizeof name);
        resolve_path(name, path, sizeof path);
        if (remove(path)) fail(cpu, 2); else ok(cpu);
        break;
    }
    case 0x42: {                                          /* lseek */
        int h = cpu->bx;
        if (!(h < MAXFH && g_fh[h])) { fail(cpu, 6); break; }
        long off = (long)(int32_t)(((uint32_t)cpu->cx << 16) | cpu->dx);
        int wh = cpu->al == 1 ? SEEK_CUR : cpu->al == 2 ? SEEK_END : SEEK_SET;
        fseek(g_fh[h], off, wh);
        long p = ftell(g_fh[h]);
        cpu->ax = (uint16_t)p; cpu->dx = (uint16_t)(p >> 16); ok(cpu); break;
    }
    case 0x43: {                                          /* attributes */
        char name[256];
        read_name(cpu, cpu->ds, cpu->dx, name, sizeof name);
        FILE *f = rt_open_game_file(name, "rb");
        if (!f) { fail(cpu, 2); break; }
        fclose(f);
        if (cpu->al == 0) cpu->cx = 0x20;
        ok(cpu); break;
    }
    case 0x44:                                            /* IOCTL */
        if (cpu->al == 0x00) { cpu->dx = cpu->bx <= 2 ? 0x80D3 : 0x0002; cpu->ax = cpu->dx; }
        else if (cpu->al == 0x08) cpu->ax = 1;            /* not removable */
        else if (cpu->al == 0x09) cpu->dx = 0;            /* local */
        ok(cpu); break;
    case 0x48: {                                          /* allocate (rare with Borland) */
        uint16_t avail = (uint16_t)(RT_HIGH_HI - g_high_next);
        TRACE("[dos] alloc %04X paras (avail %04X)\n", cpu->bx, avail);
        if (cpu->bx > avail) { cpu->bx = avail; fail(cpu, 8); break; }
        cpu->ax = g_high_next; g_high_next = (uint16_t)(g_high_next + cpu->bx); ok(cpu);
        break;
    }
    case 0x49: ok(cpu); break;
    case 0x4A: {                                          /* resize the program block */
        /* Borland's brk grows the far heap by resizing the PSP's block to
         * (heap top - PSP). The PSP sits above the image here, so that size
         * wraps around 64K and the end it names is (PSP + BX) mod 64K. Grant
         * it up to RT_MEM_TOP and refuse beyond: above lie the PSP itself and
         * the overlay code. */
        uint16_t end = (uint16_t)(cpu->es + cpu->bx);
        TRACE("[dos] resize %04X to %04X paras (ends at %04X)\n", cpu->es, cpu->bx, end);
        if (cpu->es == RT_PSP_SEG && end > RT_MEM_TOP && end < RT_PSP_SEG + 0x10) { ok(cpu); break; }
        if (cpu->es == RT_PSP_SEG && end > RT_MEM_TOP) {
            cpu->bx = (uint16_t)(RT_MEM_TOP - RT_PSP_SEG);
            fail(cpu, 8);
            break;
        }
        ok(cpu); break;
    }
    case 0x4C:
        fprintf(stderr, "[dos] program exit, code %u\n", cpu->al);
        fflush(stdout); fflush(stderr);
        host_shutdown();
        exit(cpu->al);
    case 0x4E: {                                          /* find first */
        char pat[256];
        read_name(cpu, cpu->ds, cpu->dx, pat, sizeof pat);
        dir_open();
        snprintf(g_find_pat, sizeof g_find_pat, "%s", base_name(pat));
        if (find_step(cpu)) { cpu->ax = 0; ok(cpu); } else fail(cpu, 18);
        break;
    }
    case 0x4F: if (find_step(cpu)) { cpu->ax = 0; ok(cpu); } else fail(cpu, 18); break;
    case 0x56: {                                          /* rename */
        char a[256], b[256], pa[1200], pb[1200];
        read_name(cpu, cpu->ds, cpu->dx, a, sizeof a);
        read_name(cpu, cpu->es, cpu->di, b, sizeof b);
        resolve_path(a, pa, sizeof pa); resolve_path(b, pb, sizeof pb);
        if (rename(pa, pb)) fail(cpu, 5); else ok(cpu);
        break;
    }
    case 0x57:
        if (cpu->al == 0) { cpu->cx = 0x6000; cpu->dx = 0x1A61; }
        ok(cpu); break;
    case 0x62: cpu->bx = RT_PSP_SEG; break;
    default:
        TRACE("[dos] unhandled AH=%02X AL=%02X\n", ah, cpu->al);
        ok(cpu);
        break;
    }
}
