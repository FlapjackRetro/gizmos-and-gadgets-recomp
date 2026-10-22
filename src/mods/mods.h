/* mods.h - the runtime side of the mod API (include/gg_mod.h is the mods' side). */
#ifndef GG_MODS_H
#define GG_MODS_H

#include "../runtime/rt.h"
#include "../../include/gg_mod.h"

#define GG_DGROUP_SEG 0x0EA7

extern CPU *g_mod_cpu;

/* mods.c */
void gg_mods_init(CPU *cpu, const char *const *extra, int n_extra, int scan_dir);
void gg_mods_frame(CPU *cpu);                     /* ~60 Hz, from the presentation timer */
int  gg_site(CPU *cpu, unsigned addr);            /* called by lifted code; 1 = skip */
int  gg_fire(GGEvent *ev);                        /* run the mods' callbacks for ev */
int  gg_has_listeners(int type);
uint32_t gg_call_far(uint16_t seg, uint16_t off, int nargs, const uint16_t *args);

/* events.c: the built-in meaning of the named sites */
GGEvent gg_event_new(int type);
int  gg_event_site(CPU *cpu, unsigned addr);      /* 1 = skip the instruction */

#endif
