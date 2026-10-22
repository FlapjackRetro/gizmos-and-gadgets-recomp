/*
 * rt.h - state shared between the runtime's pieces (not part of the lifted
 * code's interface; that is runtime16.h).
 */
#ifndef GG_RT_H
#define GG_RT_H

#include <stdint.h>
#include <stdio.h>
#include "runtime16.h"

/* ---- memory layout ------------------------------------------------------
 * The image loads at linear 0 ("image space"), so a lifted fn_XXXXX and the
 * guest address it was lifted from are the same number and every far pointer
 * the loader relocates stays an image offset.
 *
 * The game insists on ~363 KB of free far heap at startup (farcoreleft() >=
 * 0x58BEC), which conventional memory only has once the overlay code is out
 * of the way. tools/unvroomm.py --overlay-seg 0xC000 puts it in the empty
 * upper-memory area at C000:0000 (overlays end below D800); the heap then runs
 * from the end of the stack to the PSP at 9F00, just under the VGA window.
 */
#define RT_PSP_SEG   0x9F00
#define RT_ENV_SEG   0x9F10
#define RT_MEM_TOP   0x9F00   /* PSP:[2]: what Borland's c0 sizes its heap to */
#define RT_HIGH_LO   0xD800   /* above the overlays: INT 21h AH=48h hands out from here */
#define RT_HIGH_HI   0xEF00
#define RT_BIOS_SEG  0xF000   /* fake "previous handler" vectors point here */

/* ---- interrupt vectors (the real IVT at 0000:0000 is game code here) ---- */
extern uint16_t g_vec_seg[256], g_vec_off[256];
void rt_call_isr(CPU *cpu, int vec);                  /* run a guest handler */
void rt_call_far(CPU *cpu, uint16_t seg, uint16_t off); /* far-call guest code */

/* ---- services ------------------------------------------------------------ */
extern char g_game_dir[1024];
extern int  g_trace;
#define TRACE(...) do { if (g_trace) fprintf(stderr, __VA_ARGS__); } while (0)

void dos_int21_impl(CPU *cpu);
void mouse_int33_impl(CPU *cpu);
FILE *rt_open_game_file(const char *dos_name, const char *mode);

/* PIT / timing */
unsigned long rt_ms(void);                 /* host milliseconds since start */
unsigned long rt_bios_ticks(void);         /* 18.2 Hz BIOS tick count */
void pit_out(uint16_t port, uint8_t val);
uint8_t pit_in(uint16_t port);
double pit_ch0_hz(void);

/* keyboard: scancodes queued by the host, read through port 60h */
void kbd_push_scancode(uint8_t sc);
int  kbd_pending(void);
uint8_t kbd_port60(void);
void kbd_service(CPU *cpu);                /* deliver one queued key */
void kbd_bios_from_port60(void);           /* the BIOS INT 9's work */
uint16_t kbd_bios_peek(void);              /* INT 16h view */
uint16_t kbd_bios_take(void);

/* mouse state, fed by the host */
void mouse_host_update(int x320, int y200, int buttons);
void mouse_service(CPU *cpu);              /* call the guest's event handler */
int  mouse_cursor_visible(int *x, int *y);

/* VGA */
extern uint8_t g_dac[256][3];              /* 6-bit DAC values */
extern int g_video_mode;
uint8_t vga_in(uint16_t port);
void vga_out(uint16_t port, uint8_t val);
void vga_compose_rgba(CPU *cpu, uint32_t *out);   /* 320x200 */
void vga_dump_bmp(CPU *cpu, const char *path);

/* sound hardware: OPL2 FM + PC speaker; see sound.c */
uint8_t sound_in(uint16_t port, int *handled);
int     sound_out(uint16_t port, uint8_t val);
void    sound_init(void);
void    sound_update(void);                /* render audio up to now */
void    sound_shutdown(void);
void    sound_set_speaker(uint8_t port61_bits, double hz);

/* the main service point, run from recomp_tick and from port/int traffic */
void rt_service(CPU *cpu);

/* host layer (host_sdl.c / host_null.c) */
int  host_init(const char *title, int scale);
void host_present(const uint32_t *rgba320x200);
void host_pump(void);                      /* feeds kbd_/mouse_ */
int  host_quit_requested(void);
void host_shutdown(void);
int  host_headless(void);
void host_audio_open(int rate);            /* mono S16 */
void host_audio_push(const int16_t *samples, int n);

#endif
