/*
 * loader.c - put the embedded image into guest memory like the DOS loader:
 * copy it to linear 0, apply the relocations (load segment 0, so values are
 * unchanged but the machinery stays honest), build a PSP and environment,
 * and seed the registers from the MZ header.
 *
 * PSP/environment layout after DinoPark's dino_load_image (MIT, sp00nz).
 */
#include "rt.h"
#include <string.h>

extern const uint16_t g_img_ss, g_img_sp, g_img_cs, g_img_ip;
extern const size_t g_img_size, g_img_nrelocs;
extern const uint16_t g_img_relocs[][2];
extern const uint8_t g_img[];

#define LOADSEG 0

void rt_load(CPU *cpu, const char *cmdline)
{
    memcpy(cpu->mem, g_img, g_img_size);
    for (size_t i = 0; i < g_img_nrelocs; i++) {
        uint32_t a = seg_off(g_img_relocs[i][0], g_img_relocs[i][1]);
        uint16_t w = (uint16_t)(cpu->mem[a] | (cpu->mem[a + 1] << 8));
        w = (uint16_t)(w + LOADSEG);
        cpu->mem[a] = (uint8_t)w; cpu->mem[a + 1] = (uint8_t)(w >> 8);
    }

    uint32_t psp = seg_off(RT_PSP_SEG, 0), env = seg_off(RT_ENV_SEG, 0);
    memset(&cpu->mem[psp], 0, 0x100);
    cpu->mem[psp + 0] = 0xCD; cpu->mem[psp + 1] = 0x20;
    cpu->mem[psp + 2] = RT_MEM_TOP & 0xFF; cpu->mem[psp + 3] = RT_MEM_TOP >> 8;
    cpu->mem[psp + 0x2C] = RT_ENV_SEG & 0xFF; cpu->mem[psp + 0x2D] = RT_ENV_SEG >> 8;
    size_t n = strlen(cmdline);
    if (n > 126) n = 126;
    cpu->mem[psp + 0x80] = (uint8_t)n;
    memcpy(&cpu->mem[psp + 0x81], cmdline, n);
    cpu->mem[psp + 0x81 + n] = 0x0D;

    static const char *const envv[] = { "COMSPEC=C:\\COMMAND.COM", "PATH=C:\\DOS", "PROMPT=$p$g",
                                        "BLASTER=A220 I7 D1 T3" };   /* SB 2.0 at 220h, IRQ 7, DMA 1 */
    static const char progpath[] = "C:\\GIZMOS\\SSG.EXE";
    memset(&cpu->mem[env], 0, 0x400);
    uint32_t e = env;
    for (size_t i = 0; i < sizeof envv / sizeof *envv; i++) {
        size_t k = strlen(envv[i]) + 1;
        memcpy(&cpu->mem[e], envv[i], k);
        e += (uint32_t)k;
    }
    cpu->mem[e++] = 0;
    cpu->mem[e++] = 1; cpu->mem[e++] = 0;
    memcpy(&cpu->mem[e], progpath, sizeof progpath);

    /* Vectors a game might chain to or test for: point them at a fake BIOS
     * segment the ISR caller recognises and skips. INT 33h must be non-null
     * or the mouse looks absent. */
    for (int v = 0; v < 256; v++) { g_vec_seg[v] = RT_BIOS_SEG; g_vec_off[v] = (uint16_t)(v * 4); }

    cpu->cs = (uint16_t)(g_img_cs + LOADSEG); cpu->ip = g_img_ip;
    cpu->ss = (uint16_t)(g_img_ss + LOADSEG); cpu->sp = g_img_sp;
    cpu->ds = cpu->es = RT_PSP_SEG;
    cpu->flags = 0x0202;
}
