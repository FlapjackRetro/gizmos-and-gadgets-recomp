/* Memory watchpoints for bring-up (built only with -DGG_WATCH=ON).
 *
 * GG_WATCH=<linear hex>[:<len>][,...] prints every guest write into the
 * watched ranges with the host call stack, so a lifted function that writes
 * the wrong thing -- or a cleared structure nobody expected -- can be traced
 * back to the function that did it. Symbolise the addresses with addr2line
 * (Linux) or run under a debugger; build with -g for names.
 *
 * GG_WATCH_MAX=<n> stops reporting after n hits (default 200). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cpu.h"

#if defined(__GLIBC__)
#include <execinfo.h>
#define HAVE_BT 1
#endif

#define WMAX 8
static uint32_t w_lo[WMAX], w_hi[WMAX];
static int w_n = -1;
static long w_hits, w_max = 200;

static void w_init(void) {
    w_n = 0;
    const char *e = getenv("GG_WATCH");
    while (e && *e && w_n < WMAX) {
        char *end;
        uint32_t a = (uint32_t)strtoul(e, &end, 16);
        uint32_t len = 2;
        if (*end == ':') len = (uint32_t)strtoul(end + 1, &end, 16);
        w_lo[w_n] = a; w_hi[w_n] = a + len; w_n++;
        e = strchr(end, ',');
        if (!e) break;
        e++;
    }
    if ((e = getenv("GG_WATCH_MAX"))) w_max = strtol(e, NULL, 10);
}

int recomp_mem_write8(CPU *cpu, uint32_t addr, uint8_t val) {
    if (w_n < 0) w_init();
    for (int i = 0; i < w_n; i++) {
        if (addr >= w_lo[i] && addr < w_hi[i] && w_hits < w_max) {
            w_hits++;
            fprintf(stderr, "[watch] %05X <- %02X (was %02X) cs=%04X ds=%04X es=%04X ss=%04X sp=%04X\n",
                    addr, val, cpu->mem[addr], cpu->cs, cpu->ds, cpu->es, cpu->ss, cpu->sp);
#ifdef HAVE_BT
            void *bt[12];
            int n = backtrace(bt, 12);
            backtrace_symbols_fd(bt + 1, n - 1, 2);
#endif
        }
    }
    return 0;
}

int recomp_mem_read8(CPU *cpu, uint32_t addr, uint8_t *out) {
    (void)cpu; (void)addr; (void)out;
    return 0;
}
