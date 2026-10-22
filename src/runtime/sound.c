/*
 * sound.c - the game's sound hardware: an OPL2 (YM3812) FM chip and the PC
 * speaker, mixed to one 44.1 kHz mono stream.
 *
 * The game drives the OPL directly at 388h/389h for both music and effects
 * (switch -sb; -sm uses the same chip). It never touches the Sound Blaster's
 * digital DSP, so none is emulated. -si uses the PC speaker: PIT channel 2's
 * square wave, gated by port 61h bits 0 and 1.
 *
 * Samples are rendered on the game thread up to the current wall-clock time,
 * before every register write and on every service tick, so writes take effect
 * at the moment the game made them. The OPL itself is emu8950 (MIT).
 */
#include "rt.h"
#include "../../third_party/emu8950/emu8950.h"
#include <stdlib.h>
#include <string.h>

#define RATE      44100
#define OPL_CLOCK 3579545

static OPL *opl;
static uint8_t opl_index;
static uint64_t g_rendered;            /* samples produced since start */
static int g_enabled = 1;
static int g_volume = 100;             /* percent of the default gain */
#define GAIN 4                         /* emu8950's OPL2 peaks around 1/8 of full scale */

/* PC speaker */
static uint8_t spk_gate;               /* port 61h bits 0-1 */
static double  spk_hz;
static double  spk_phase;

/* WAV capture (GG_WAV=path) for headless verification */
static FILE *g_wav;
static uint32_t g_wav_samples;

static void wav_finish(void)
{
    if (!g_wav) return;
    uint32_t data = g_wav_samples * 2, riff = 36 + data;
    fseek(g_wav, 4, SEEK_SET);  fwrite(&riff, 4, 1, g_wav);
    fseek(g_wav, 40, SEEK_SET); fwrite(&data, 4, 1, g_wav);
    fclose(g_wav);
    g_wav = NULL;
}

void sound_init(void)
{
    const char *e;
    if ((e = getenv("GG_MUTE")) && atoi(e)) g_enabled = 0;
    if ((e = getenv("GG_VOLUME"))) g_volume = atoi(e);
    opl = OPL_new(OPL_CLOCK, RATE);
    OPL_setChipType(opl, 2);            /* YM3812 */
    OPL_reset(opl);
    host_audio_open(RATE);
    if ((e = getenv("GG_WAV"))) {
        g_wav = fopen(e, "wb");
        if (g_wav) {
            static const uint8_t hdr[44] = {
                'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,1,0,
                0x44,0xAC,0,0, 0x88,0x58,0x01,0, 2,0,16,0,'d','a','t','a',0,0,0,0 };
            fwrite(hdr, 1, 44, g_wav);
            atexit(wav_finish);
        }
    }
}

void sound_shutdown(void) { wav_finish(); }

void sound_set_speaker(uint8_t port61_bits, double hz)
{
    spk_gate = port61_bits & 3;
    spk_hz = hz;
}

/* Render everything owed up to now. */
void sound_update(void)
{
    if (!opl) return;
    uint64_t due = (uint64_t)rt_ms() * RATE / 1000;
    if (due <= g_rendered) return;
    uint64_t n = due - g_rendered;
    if (n > RATE / 4) {                 /* fell far behind (a stall): skip, don't burst */
        g_rendered = due - RATE / 50;
        n = RATE / 50;
    }
    static int16_t buf[RATE / 4];
    for (uint64_t i = 0; i < n; i++) {
        int32_t s = OPL_calc(opl);
        if (spk_gate == 3 && spk_hz > 20 && spk_hz < 20000) {
            spk_phase += spk_hz / RATE;
            if (spk_phase >= 1.0) spk_phase -= (int)spk_phase;
            s += spk_phase < 0.5 ? 6000 : -6000;
        }
        s = s * GAIN * g_volume / 100;
        /* soft knee above 24000 so loud chords round off instead of clipping */
        if (s > 24000)  s = 24000 + (s - 24000) / 4;
        if (s < -24000) s = -24000 + (s + 24000) / 4;
        buf[i] = (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
    }
    g_rendered += n;
    if (g_wav) { fwrite(buf, 2, (size_t)n, g_wav); g_wav_samples += (uint32_t)n; }
    if (g_enabled) host_audio_push(buf, (int)n);
}

uint8_t sound_in(uint16_t port, int *handled)
{
    *handled = 0;
    if (!opl) return 0xFF;
    switch (port) {
    case 0x388: case 0x228: case 0x220: case 0x222:
        *handled = 1;
        /* OPL2 status: timer flags in bits 5-7; the low bits read 6 on a YM3812. */
        return (uint8_t)((OPL_status(opl) & 0xE0) | 0x06);
    case 0x389: case 0x229: case 0x221: case 0x223:
        *handled = 1;
        return 0xFF;
    default:
        return 0xFF;
    }
}

int sound_out(uint16_t port, uint8_t val)
{
    if (!opl) return 0;
    switch (port) {
    case 0x388: case 0x228: case 0x220: case 0x222:
        opl_index = val;
        return 1;
    case 0x389: case 0x229: case 0x221: case 0x223:
        sound_update();                   /* land the write at the right sample */
        OPL_writeReg(opl, opl_index, val);
        return 1;
    default:
        return 0;
    }
}
