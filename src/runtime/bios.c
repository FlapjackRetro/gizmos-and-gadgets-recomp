/*
 * bios.c - INT dispatch, BIOS services (10h video, 16h keyboard, 1Ah clock),
 * the INT 33h mouse driver, and the service loop that stands in for the
 * hardware interrupting the guest: timer ticks, key presses, mouse events,
 * and presenting frames.
 *
 * Calling a guest interrupt handler follows DinoPark's run_guest_isr (MIT,
 * sp00nz): push an IRET frame, dispatch, then restore the whole register
 * file, since lifted `iret` returns without popping it.
 */
#include "rt.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

int g_trace = 0;

/* ---- time ---------------------------------------------------------------- */
unsigned long rt_ms(void)
{
    static struct timespec t0;
    static int init;
    struct timespec t;
    timespec_get(&t, TIME_UTC);
    if (!init) { t0 = t; init = 1; }
    return (unsigned long)((t.tv_sec - t0.tv_sec) * 1000L + (t.tv_nsec - t0.tv_nsec) / 1000000L);
}

unsigned long rt_bios_ticks(void) { return (unsigned long)(rt_ms() * 18.2065 / 1000.0); }

void recomp_div0(const char *kind)
{
    fprintf(stderr, "[cpu] divide error (%s)\n", kind);
}

/* ---- calling into the guest ---------------------------------------------- */
static int g_in_isr;

void rt_call_isr(CPU *cpu, int vec)
{
    uint16_t seg = g_vec_seg[vec], off = g_vec_off[vec];
    if ((!seg && !off) || seg == RT_BIOS_SEG || g_in_isr) return;
    g_in_isr++;
    CPU save = *cpu;
    push16(cpu, cpu->flags);
    push16(cpu, cpu->cs);
    push16(cpu, 0xFFFF);
    cpu->flags &= ~FLAG_IF;
    if (!recomp_dispatch(cpu, seg, off))
        TRACE("[isr] INT %02X handler %04X:%04X not lifted\n", vec, seg, off);
    *cpu = save;
    g_in_isr--;
}

void rt_call_far(CPU *cpu, uint16_t seg, uint16_t off)
{
    CPU save = *cpu;
    push16(cpu, cpu->cs);
    push16(cpu, 0xFFFF);
    if (!recomp_dispatch(cpu, seg, off))
        TRACE("[guest] far call %04X:%04X not lifted\n", seg, off);
    *cpu = save;
}

/* ---- mouse (INT 33h) ----------------------------------------------------- */
static int m_x = 160, m_y = 100, m_btn, m_shown = -1;
static int m_minx = 0, m_maxx = 639, m_miny = 0, m_maxy = 199;
static uint16_t m_handler_seg, m_handler_off, m_handler_mask;
static int m_events;                    /* event bits pending for the handler */
static int m_press_count[3], m_release_count[3];
static int m_press_x[3], m_press_y[3], m_release_x[3], m_release_y[3];

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

void mouse_host_update(int x320, int y200, int buttons)
{
    int nx = clampi(x320 * 2, m_minx, m_maxx), ny = clampi(y200, m_miny, m_maxy);
    if (nx != m_x || ny != m_y) m_events |= 0x01;
    for (int b = 0; b < 3; b++) {
        int was = (m_btn >> b) & 1, now = (buttons >> b) & 1;
        if (now && !was) { m_events |= 0x02 << (b * 2); m_press_count[b]++; m_press_x[b] = nx; m_press_y[b] = ny; }
        if (!now && was) { m_events |= 0x04 << (b * 2); m_release_count[b]++; m_release_x[b] = nx; m_release_y[b] = ny; }
    }
    m_x = nx; m_y = ny; m_btn = buttons & 7;
}

int mouse_cursor_visible(int *x, int *y)
{
    *x = m_x / 2; *y = m_y;
    return m_shown >= 0;
}

void mouse_service(CPU *cpu)
{
    int ev = m_events & m_handler_mask;
    m_events = 0;
    if (!ev || !(m_handler_seg || m_handler_off)) return;
    CPU save = *cpu;                    /* the driver preserves the interrupted state */
    cpu->ax = (uint16_t)ev; cpu->bx = (uint16_t)m_btn;
    cpu->cx = (uint16_t)m_x; cpu->dx = (uint16_t)m_y;
    cpu->si = 0; cpu->di = 0;
    rt_call_far(cpu, m_handler_seg, m_handler_off);
    *cpu = save;
}

void mouse_int33_impl(CPU *cpu)
{
    switch (cpu->ax) {
    case 0x00: case 0x21:                          /* reset */
        cpu->ax = 0xFFFF; cpu->bx = 2; m_shown = -1;
        m_minx = 0; m_maxx = 639; m_miny = 0; m_maxy = 199;
        break;
    case 0x01: if (m_shown < 0) m_shown++; break;
    case 0x02: m_shown--; break;
    case 0x03: cpu->bx = (uint16_t)m_btn; cpu->cx = (uint16_t)m_x; cpu->dx = (uint16_t)m_y; break;
    case 0x04: m_x = clampi(cpu->cx, m_minx, m_maxx); m_y = clampi(cpu->dx, m_miny, m_maxy); break;
    case 0x05: {
        int b = cpu->bx & 3; if (b > 2) b = 2;
        cpu->ax = (uint16_t)m_btn; cpu->bx = (uint16_t)m_press_count[b];
        cpu->cx = (uint16_t)m_press_x[b]; cpu->dx = (uint16_t)m_press_y[b]; m_press_count[b] = 0;
        break;
    }
    case 0x06: {
        int b = cpu->bx & 3; if (b > 2) b = 2;
        cpu->ax = (uint16_t)m_btn; cpu->bx = (uint16_t)m_release_count[b];
        cpu->cx = (uint16_t)m_release_x[b]; cpu->dx = (uint16_t)m_release_y[b]; m_release_count[b] = 0;
        break;
    }
    case 0x07: m_minx = (int16_t)cpu->cx; m_maxx = (int16_t)cpu->dx; break;
    case 0x08: m_miny = (int16_t)cpu->cx; m_maxy = (int16_t)cpu->dx; break;
    case 0x0B: cpu->cx = 0; cpu->dx = 0; break;    /* motion counters */
    case 0x0C:
        m_handler_mask = cpu->cx; m_handler_seg = cpu->es; m_handler_off = cpu->dx;
        TRACE("[mouse] handler %04X:%04X mask %02X\n", cpu->es, cpu->dx, cpu->cx);
        break;
    case 0x14: {                                   /* swap handler */
        uint16_t s = m_handler_seg, o = m_handler_off, k = m_handler_mask;
        m_handler_mask = cpu->cx; m_handler_seg = cpu->es; m_handler_off = cpu->dx;
        cpu->cx = k; cpu->es = s; cpu->dx = o;
        break;
    }
    case 0x24: cpu->bx = 0x0626; cpu->ch = 2; cpu->cl = 0; break;   /* version, PS/2 */
    default: TRACE("[mouse] unhandled AX=%04X\n", cpu->ax); break;
    }
}

/* ---- the service loop ---------------------------------------------------- */
int g_recomp_tick_budget = 2000;
static unsigned long g_last_present, g_frames;
static double g_pit_due;              /* guest timer ticks owed, fractional */
static unsigned long g_pit_last_ms;
static char g_film_dir[512];
static unsigned long g_film_every, g_film_next, g_quit_after;

/* GG_KEYS=ms:sc[,ms:sc...]: press scancode sc (hex) at ms, released 100 ms
 * later -- drives a headless run through the screens. */
#define MAXKEYS 64
static unsigned long g_key_at[MAXKEYS]; static uint8_t g_key_sc[MAXKEYS]; static int g_nkeys, g_key_next;
static void key_script(void)
{
    unsigned long now = rt_ms();
    while (g_key_next < g_nkeys && now >= g_key_at[g_key_next]) {
        uint8_t sc = g_key_sc[g_key_next];
        kbd_push_scancode((uint8_t)(sc & 0x7F));
        g_key_at[g_key_next] = 0;
        g_key_next++;
        if (g_nkeys < MAXKEYS) {               /* schedule the release */
            int i = g_nkeys++;
            for (; i > g_key_next && g_key_at[i - 1] > now + 100; i--) { g_key_at[i] = g_key_at[i - 1]; g_key_sc[i] = g_key_sc[i - 1]; }
            g_key_at[i] = now + 100; g_key_sc[i] = (uint8_t)(sc | 0x80);
        }
    }
}

static void present(CPU *cpu)
{
    static uint32_t px[320 * 200];
    vga_compose_rgba(cpu, px);
    host_present(px);
    g_frames++;
    if (g_film_every && rt_ms() >= g_film_next) {
        char p[600];
        snprintf(p, sizeof p, "%s/frame_%06lu.bmp", g_film_dir, rt_ms());
        vga_dump_bmp(cpu, p);
        g_film_next = rt_ms() + g_film_every;
    }
}

void rt_service(CPU *cpu)
{
    static int inside;
    if (inside || g_in_isr) return;
    inside = 1;
    unsigned long now = rt_ms();

    /* timer: deliver the guest's INT 8 at the programmed PIT rate */
    if (!g_pit_last_ms) g_pit_last_ms = now;
    g_pit_due += (now - g_pit_last_ms) * pit_ch0_hz() / 1000.0;
    g_pit_last_ms = now;
    if (g_pit_due > 8) g_pit_due = 8;            /* drop a backlog, don't burst it */
    /* The interrupted code's IF is not honoured: lifted code runs in C calls,
     * so a cli/sti pair around a critical section never spans a service point
     * except inside a loop that waits for the interrupt itself. */
    while (g_pit_due >= 1.0) {
        g_pit_due -= 1.0;
        rt_call_isr(cpu, 8);
    }

    /* host input, ~250 Hz; keys go through the guest's INT 9 */
    static unsigned long last_pump;
    if (now - last_pump >= 4) {
        last_pump = now;
        host_pump();
        key_script();
        if (host_quit_requested()) { host_shutdown(); exit(0); }
        kbd_service(cpu);
        mouse_service(cpu);
    }

    sound_update();

    /* present at ~60 Hz */
    if (now - g_last_present >= 16) { g_last_present = now; present(cpu); }

    if (g_quit_after && now > g_quit_after) {
        fprintf(stderr, "[rt] GG_QUIT_AFTER reached after %lu frames\n", g_frames);
        vga_dump_bmp(cpu, "last_frame.bmp");
        recomp_dump_misses("misses.txt");
        host_shutdown();
        exit(0);
    }
    inside = 0;
}

void rt_service_config(void)
{
    const char *e;
    if ((e = getenv("GG_TRACE"))) g_trace = atoi(e);
    if ((e = getenv("GG_FILM"))) {              /* GG_FILM=dir[:ms] */
        snprintf(g_film_dir, sizeof g_film_dir, "%s", e);
        char *c = strrchr(g_film_dir, ':');
        g_film_every = 1000;
        if (c && c != g_film_dir + 1) { g_film_every = strtoul(c + 1, NULL, 10); *c = 0; }
    }
    if ((e = getenv("GG_QUIT_AFTER"))) g_quit_after = strtoul(e, NULL, 10);
    if ((e = getenv("GG_KEYS"))) {
        while (*e && g_nkeys < MAXKEYS / 2) {
            char *c;
            g_key_at[g_nkeys] = strtoul(e, &c, 10);
            if (*c != ':') break;
            g_key_sc[g_nkeys++] = (uint8_t)strtoul(c + 1, &c, 16);
            if (*c != ',') break;
            e = c + 1;
        }
    }
}

/* Lifted loops call this every few thousand back-edges (RECOMP_IRQ). */
void recomp_tick(CPU *cpu)
{
    g_recomp_tick_budget = 2000;
    rt_service(cpu);
}

/* ---- INT dispatch -------------------------------------------------------- */
void int_handler(CPU *cpu, int vec)
{
    if (g_trace > 1 && vec != 0x21 && vec != 0x33)
        fprintf(stderr, "[int] %02X AX=%04X BX=%04X CX=%04X DX=%04X ES=%04X\n", vec, cpu->ax, cpu->bx, cpu->cx, cpu->dx, cpu->es);
    rt_service(cpu);
    switch (vec) {
    case 0x21: dos_int21_impl(cpu); break;
    case 0x33: mouse_int33_impl(cpu); break;
    case 0x10:
        switch (cpu->ah) {
        case 0x00:
            g_video_mode = cpu->al & 0x7F;
            TRACE("[bios] set video mode %02X\n", g_video_mode);
            if (g_video_mode == 0x13 && !(cpu->al & 0x80)) memset(&cpu->mem[0xA0000], 0, 64000);
            break;
        case 0x0F: cpu->al = (uint8_t)g_video_mode; cpu->ah = g_video_mode == 0x13 ? 40 : 80; cpu->bh = 0; break;
        case 0x10:
            if (cpu->al == 0x10) { g_dac[cpu->bx & 0xFF][0] = cpu->dh & 63; g_dac[cpu->bx & 0xFF][1] = cpu->ch & 63; g_dac[cpu->bx & 0xFF][2] = cpu->cl & 63; }
            else if (cpu->al == 0x12) {         /* set block of DAC registers */
                for (int i = 0; i < cpu->cx; i++)
                    for (int c = 0; c < 3; c++)
                        g_dac[(cpu->bx + i) & 0xFF][c] = mem_read8(cpu, cpu->es, (uint16_t)(cpu->dx + i * 3 + c)) & 63;
            } else if (cpu->al == 0x17) {       /* read block */
                for (int i = 0; i < cpu->cx; i++)
                    for (int c = 0; c < 3; c++)
                        mem_write8(cpu, cpu->es, (uint16_t)(cpu->dx + i * 3 + c), g_dac[(cpu->bx + i) & 0xFF][c]);
            }
            break;
        case 0x12: if (cpu->bl == 0x10) { cpu->bh = 0; cpu->bl = 3; cpu->cx = 0; } break;
        case 0x1A:                              /* display combination code */
            /* AX=001Ah: the game's detector (overlay 0E25 entry 0) treats AH
             * still holding 1Ah as "function not supported". */
            if (cpu->al == 0) { cpu->ax = 0x001A; cpu->bl = 0x08; cpu->bh = 0; }
            break;
        default: TRACE("[bios] INT 10h AH=%02X\n", cpu->ah); break;
        }
        break;
    case 0x16:
        kbd_service(cpu);
        if (cpu->ah == 0x01 || cpu->ah == 0x11) {
            uint16_t k = kbd_bios_peek();
            cpu->ax = k ? k : cpu->ax;
            if (k) cpu->flags &= ~FLAG_ZF; else cpu->flags |= FLAG_ZF;
        } else if (cpu->ah == 0x00 || cpu->ah == 0x10) {
            uint16_t k;
            while (!(k = kbd_bios_take())) { host_pump(); kbd_service(cpu); rt_service(cpu); }
            cpu->ax = k;
        } else if (cpu->ah == 0x02) cpu->al = 0;
        break;
    case 0x1A:
        if (cpu->ah == 0) { unsigned long t = rt_bios_ticks(); cpu->cx = (uint16_t)(t >> 16); cpu->dx = (uint16_t)t; cpu->al = 0; }
        break;
    case 0x11: cpu->ax = 0x0021; break;            /* equipment list */
    case 0x12: cpu->ax = 640; break;               /* conventional memory KB */
    case 0x15: cpu->flags |= FLAG_CF; cpu->ah = 0x86; break;
    default: TRACE("[int] unhandled INT %02X AX=%04X\n", vec, cpu->ax); break;
    }
}

void dos_int21(CPU *cpu)  { int_handler(cpu, 0x21); }
void bios_int10(CPU *cpu) { int_handler(cpu, 0x10); }
void bios_int16(CPU *cpu) { int_handler(cpu, 0x16); }
void mouse_int33(CPU *cpu){ int_handler(cpu, 0x33); }

/* A guest interrupt handler chaining to the previous vector (`pushf; call far
 * [old]` or `jmp far [old]`) lands here: every vector starts out pointing at
 * F000:(vec*4). The BIOS handler it would reach does nothing the runtime has
 * not already done, so return as its IRET would: pop IP, CS and FLAGS. */
int rt_bios_dispatch(CPU *cpu, unsigned off)
{
    if (off == 9 * 4) kbd_bios_from_port60();   /* BIOS INT 9: buffer the key for INT 16h */
    cpu->sp = (uint16_t)(cpu->sp + 6);
    return 1;
}
