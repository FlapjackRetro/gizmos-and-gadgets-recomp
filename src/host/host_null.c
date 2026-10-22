/* host_null.c - headless host: no window, no input, no audio. */
#include "../runtime/rt.h"

int  host_init(const char *title, int scale) { (void)title; (void)scale; return 1; }
void host_present(const uint32_t *rgba) { (void)rgba; }
void host_pump(void) {}
int  host_quit_requested(void) { return 0; }
void host_shutdown(void) {}
int  host_headless(void) { return 1; }
