/*
 * hw.c - the PC hardware the game programs directly: the PIT (timer and PC
 * speaker), the PIC, the keyboard controller, and the VGA's DAC and status
 * registers. Sound Blaster / OPL ports are passed to sound.c.
 */
#include "rt.h"
#include <string.h>

/* ---- PIT 8253/8254 ------------------------------------------------------ */
#define PIT_HZ 1193182.0
static uint16_t pit_reload[3] = { 0, 0, 0 };     /* 0 means 65536 */
static int pit_lohi[3], pit_wrote_lo[3];
static uint8_t pit_latch_lo[3];
static int pit_access[3] = { 3, 3, 3 };          /* 1 lo, 2 hi, 3 lo/hi */
static uint8_t port61;
static void speaker_changed(void);

static void speaker_changed(void)
{
    double div = pit_reload[2] ? pit_reload[2] : 65536.0;
    sound_set_speaker(port61, PIT_HZ / div);
}

double pit_ch0_hz(void)
{
    double div = pit_reload[0] ? pit_reload[0] : 65536.0;
    return PIT_HZ / div;
}

static uint16_t pit_count(int ch)
{
    double div = pit_reload[ch] ? pit_reload[ch] : 65536.0;
    double ticks = rt_ms() * (PIT_HZ / 1000.0);
    unsigned long n = (unsigned long)ticks % (unsigned long)div;
    return (uint16_t)(div - n);
}

void pit_out(uint16_t port, uint8_t val)
{
    if (port == 0x43) {
        int ch = val >> 6;
        if (ch == 3) return;                       /* read-back: ignore */
        int acc = (val >> 4) & 3;
        if (acc == 0) {                            /* counter latch */
            uint16_t c = pit_count(ch);
            pit_latch_lo[ch] = (uint8_t)c;
            pit_lohi[ch] = (c >> 8) | 0x100;
            return;
        }
        pit_access[ch] = acc;
        pit_wrote_lo[ch] = 0;
        return;
    }
    int ch = port - 0x40;
    if (ch < 0 || ch > 2) return;
    switch (pit_access[ch]) {
    case 1: pit_reload[ch] = (pit_reload[ch] & 0xFF00) | val; break;
    case 2: pit_reload[ch] = (uint16_t)((pit_reload[ch] & 0x00FF) | (val << 8)); break;
    default:
        if (!pit_wrote_lo[ch]) { pit_reload[ch] = (pit_reload[ch] & 0xFF00) | val; pit_wrote_lo[ch] = 1; }
        else { pit_reload[ch] = (uint16_t)((pit_reload[ch] & 0x00FF) | (val << 8)); pit_wrote_lo[ch] = 0;
               if (ch == 0) TRACE("[pit] ch0 reload %u (%.1f Hz)\n", pit_reload[0], pit_ch0_hz()); }
    }
    if (ch == 2) speaker_changed();
}

uint8_t pit_in(uint16_t port)
{
    int ch = port - 0x40;
    if (ch < 0 || ch > 2) return 0xFF;
    if (pit_lohi[ch] & 0x100) {                    /* latched value: lo then hi */
        if (pit_lohi[ch] & 0x200) { pit_lohi[ch] = 0; return (uint8_t)0; }
        uint8_t lo = pit_latch_lo[ch];
        pit_lohi[ch] |= 0x200;
        return lo;
    }
    return (uint8_t)pit_count(ch);
}

/* ---- keyboard controller ------------------------------------------------- */
#define KQ 64
static uint8_t kq[KQ];
static int kq_head, kq_tail;
static uint8_t port60 = 0;

void kbd_push_scancode(uint8_t sc)
{
    int n = (kq_tail + 1) % KQ;
    if (n == kq_head) return;
    kq[kq_tail] = sc; kq_tail = n;
}
int kbd_pending(void) { return kq_head != kq_tail; }
uint8_t kbd_port60(void) { return port60; }

/* BIOS keyboard buffer, for when the game asks INT 16h instead of hooking INT 9. */
#define BQ 32
static uint16_t bq[BQ];
static int bq_head, bq_tail;

static uint8_t scancode_ascii(uint8_t sc)
{
    static const char map[] =
        "\0\x1b" "1234567890-=\b\tqwertyuiop[]\r\0asdfghjkl;'`\0\\zxcvbnm,./\0*\0 ";
    return sc < sizeof map - 1 ? (uint8_t)map[sc] : 0;
}

/* What the BIOS INT 9 handler does with the byte in port 60h. */
void kbd_bios_from_port60(void)
{
    if (port60 & 0x80 || port60 == 0xE0) return;  /* breaks and prefixes */
    int n = (bq_tail + 1) % BQ;
    if (n != bq_head) { bq[bq_tail] = (uint16_t)((port60 << 8) | scancode_ascii(port60)); bq_tail = n; }
}

void kbd_service(CPU *cpu)
{
    if (!kbd_pending()) return;
    port60 = kq[kq_head]; kq_head = (kq_head + 1) % KQ;
    if (g_vec_seg[9] != RT_BIOS_SEG)
        rt_call_isr(cpu, 9);                       /* the game's handler reads 60h, may chain */
    else
        kbd_bios_from_port60();
}
uint16_t kbd_bios_peek(void) { return bq_head == bq_tail ? 0 : bq[bq_head]; }
uint16_t kbd_bios_take(void)
{
    if (bq_head == bq_tail) return 0;
    uint16_t k = bq[bq_head]; bq_head = (bq_head + 1) % BQ; return k;
}

/* ---- VGA ----------------------------------------------------------------- */
uint8_t g_dac[256][3];
int g_video_mode = 3;
static int dac_w, dac_r, dac_wc, dac_rc;
static uint8_t seq_idx, gc_idx, crtc_idx, seq_regs[8], gc_regs[16];

uint8_t vga_in(uint16_t port)
{
    switch (port) {
    case 0x3DA: {                                  /* input status 1 */
        /* 70 Hz; vertical retrace for the last ~1.4 ms of each frame. Waiting
         * on bit 3 paces the game to real time, as on the hardware. */
        unsigned long us = (unsigned long)(rt_ms() * 1000UL);
        unsigned long ph = us % 14286UL;
        uint8_t v = 0;
        if (ph > 12857UL) v |= 0x08 | 0x01;
        else if ((us / 32) & 1) v |= 0x01;         /* display-enable toggling */
        return v;
    }
    case 0x3C9: {
        uint8_t v = g_dac[dac_r][dac_rc];
        if (++dac_rc == 3) { dac_rc = 0; dac_r = (dac_r + 1) & 0xFF; }
        return v;
    }
    case 0x3C8: return (uint8_t)dac_w;
    case 0x3C7: return 0;
    case 0x3C4: return seq_idx;
    case 0x3C5: return seq_regs[seq_idx & 7];
    case 0x3CE: return gc_idx;
    case 0x3CF: return gc_regs[gc_idx & 15];
    case 0x3CC: return 0x63;
    default: return 0xFF;
    }
}

void vga_out(uint16_t port, uint8_t val)
{
    switch (port) {
    case 0x3C8: dac_w = val; dac_wc = 0; break;
    case 0x3C7: dac_r = val; dac_rc = 0; break;
    case 0x3C9:
        g_dac[dac_w][dac_wc] = val & 0x3F;
        if (++dac_wc == 3) { dac_wc = 0; dac_w = (dac_w + 1) & 0xFF; }
        break;
    case 0x3C4: seq_idx = val; break;
    case 0x3C5:
        seq_regs[seq_idx & 7] = val;
        if ((seq_idx & 7) == 4 && !(val & 0x08))
            TRACE("[vga] sequencer: chain-4 off (unchained mode) -- not emulated yet\n");
        break;
    case 0x3CE: gc_idx = val; break;
    case 0x3CF: gc_regs[gc_idx & 15] = val; break;
    case 0x3D4: crtc_idx = val; break;
    default: break;
    }
}

/* ---- port dispatch ------------------------------------------------------- */
uint8_t port_in8(CPU *cpu, uint16_t port)
{
    rt_service(cpu);
    if (port >= 0x40 && port <= 0x43) return pit_in(port);
    if (port == 0x60) return port60;
    if (port == 0x64) return kbd_pending() ? 0x15 : 0x14;
    if (port == 0x61) {
        /* bit 5 toggles with the PIT ch2 output / refresh; delay loops watch it */
        return (uint8_t)((port61 & 0x0F) | ((rt_ms() & 1) ? 0x20 : 0) | 0x10);
    }
    if (port == 0x21 || port == 0xA1) return 0x00;
    if (port == 0x20) return 0x00;
    if (port >= 0x3C0 && port <= 0x3DF) return vga_in(port);
    int handled = 0;
    uint8_t v = sound_in(port, &handled);
    if (handled) return v;
    TRACE("[port] in  %04X\n", port);
    return 0xFF;
}

void port_out8(CPU *cpu, uint16_t port, uint8_t val)
{
    if (port >= 0x40 && port <= 0x43) { pit_out(port, val); return; }
    if (port == 0x61) { if (g_trace > 2 && (val & 3) != (port61 & 3)) fprintf(stderr, "[spk] 61h <- %02X\n", val);
                        sound_update(); port61 = val; speaker_changed(); return; }
    if (g_trace > 2 && (port == 0x42 || port == 0x43)) fprintf(stderr, "[spk] %02Xh <- %02X\n", port, val);
    if (port == 0x20 || port == 0x21 || port == 0xA0 || port == 0xA1) return;   /* PIC */
    if (port >= 0x3C0 && port <= 0x3DF) { vga_out(port, val); return; }
    if (sound_out(port, val)) return;
    TRACE("[port] out %04X <- %02X\n", port, val);
    (void)cpu;
}

uint16_t port_in16(CPU *cpu, uint16_t port)
{
    uint8_t lo = port_in8(cpu, port);
    uint8_t hi = port_in8(cpu, (uint16_t)(port + 1));
    return (uint16_t)(lo | (hi << 8));
}

void port_out16(CPU *cpu, uint16_t port, uint16_t val)
{
    port_out8(cpu, port, (uint8_t)val);
    port_out8(cpu, (uint16_t)(port + 1), (uint8_t)(val >> 8));
}

/* ---- composing the screen (mode 13h, chained) ---------------------------- */
void vga_compose_rgba(CPU *cpu, uint32_t *out)
{
    uint32_t pal[256];
    for (int i = 0; i < 256; i++) {
        uint32_t r = (g_dac[i][0] * 255u) / 63u, g = (g_dac[i][1] * 255u) / 63u, b = (g_dac[i][2] * 255u) / 63u;
        pal[i] = 0xFF000000u | (r << 16) | (g << 8) | b;
    }
    const uint8_t *fb = &cpu->mem[0xA0000];
    for (int i = 0; i < 320 * 200; i++) out[i] = pal[fb[i]];
    int mx, my;
    if (mouse_cursor_visible(&mx, &my)) {           /* driver-drawn arrow */
        static const char *arrow[] = {
            "X.........", "XX........", "XWX.......", "XWWX......", "XWWWX.....", "XWWWWX....",
            "XWWWWWX...", "XWWWWWWX..", "XWWWWWWWX.", "XWWWWWXXXX", "XWWXWWX...", "XWX.XWWX..",
            "XX..XWWX..", "X....XWWX.", ".....XWWX.", "......XX.." };
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 10; x++) {
                char c = arrow[y][x];
                int px = mx + x, py = my + y;
                if (c == '.' || px >= 320 || py >= 200) continue;
                out[py * 320 + px] = c == 'X' ? 0xFF000000u : 0xFFFFFFFFu;
            }
    }
}

void vga_dump_bmp(CPU *cpu, const char *path)
{
    static uint32_t px[320 * 200];
    vga_compose_rgba(cpu, px);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    uint32_t size = 54 + 320 * 200 * 3;
    uint8_t h[54] = { 'B', 'M' };
    memcpy(h + 2, &size, 4);
    h[10] = 54; h[14] = 40;
    int32_t w = 320, hh = 200; memcpy(h + 18, &w, 4); memcpy(h + 22, &hh, 4);
    h[26] = 1; h[28] = 24;
    fwrite(h, 1, 54, f);
    for (int y = 199; y >= 0; y--)
        for (int x = 0; x < 320; x++) {
            uint32_t c = px[y * 320 + x];
            uint8_t bgr[3] = { (uint8_t)c, (uint8_t)(c >> 8), (uint8_t)(c >> 16) };
            fwrite(bgr, 1, 3, f);
        }
    fclose(f);
}
