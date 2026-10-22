/*
 * sound.c - Sound Blaster DSP and OPL2 (AdLib) ports.
 *
 * Bring-up stub: nothing answers yet, so the game finds no card. Task: a
 * DSP that passes the reset handshake and plays 8-bit DMA transfers, and an
 * OPL emulator for the FM music, both mixed through the host audio device.
 */
#include "rt.h"

uint8_t sound_in(uint16_t port, int *handled)
{
    (void)port;
    *handled = 0;
    return 0xFF;
}

int sound_out(uint16_t port, uint8_t val)
{
    (void)port; (void)val;
    return 0;
}
