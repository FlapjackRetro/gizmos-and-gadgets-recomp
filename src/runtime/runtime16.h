/*
 * runtime16.h - the host runtime the lifted Gizmos & Gadgets code calls into.
 *
 * The lifter (pcrecomp lift16) emits three escape hatches out of the guest:
 * software interrupts, port I/O and address-indirect calls. This runtime
 * implements just enough of DOS, the BIOS and the PC hardware for the game,
 * on top of a small host layer (SDL2 or headless).
 *
 * Shape and much of the DOS behaviour follow DinoPark Tycoon's runtime16
 * (https://github.com/sp00nznet/dinopark, MIT, Copyright (c) 2025-2026 sp00nz).
 */
#ifndef GG_RUNTIME16_H
#define GG_RUNTIME16_H

#include "cpu.h"

/* ---- what the lifted code calls ---------------------------------------- */
void int_handler(CPU *cpu, int vec);   /* INT nn */
void dos_int21(CPU *cpu);
void bios_int10(CPU *cpu);
void bios_int16(CPU *cpu);
void mouse_int33(CPU *cpu);

uint8_t  port_in8(CPU *cpu, uint16_t port);
void     port_out8(CPU *cpu, uint16_t port, uint8_t val);
uint16_t port_in16(CPU *cpu, uint16_t port);
void     port_out16(CPU *cpu, uint16_t port, uint16_t val);

void recomp_div0(const char *kind);

/* generated in src/gen/recomp_dispatch.c */
int  recomp_dispatch(CPU *cpu, unsigned seg, unsigned off);   /* 1 = dispatched */
void dispatch_near(CPU *cpu, unsigned seg, unsigned off);
void dispatch_far(CPU *cpu, unsigned seg, unsigned off);
void recomp_dump_misses(const char *path);
void recomp_dump_ring(void);

#endif
