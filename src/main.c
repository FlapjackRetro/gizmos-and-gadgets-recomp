/*
 * main.c - Gizmos & Gadgets, natively recompiled.
 *
 * Usage: gizmos [--game-dir DIR] [--scale N] [-- game switches...]
 *   The game directory holds the original data files (gizmo000-004.dat,
 *   players.dat). Game switches are passed to the game as on DOS; the
 *   default is "-sb" (Sound Blaster FM music and effects).
 *
 * Environment (for bring-up):
 *   GG_TRACE=1           log DOS/BIOS/port traffic
 *   GG_FILM=dir[:ms]     write a BMP of the screen every ms (default 1000)
 *   GG_QUIT_AFTER=ms     exit after ms, dumping last_frame.bmp and misses.txt
 *   GG_HEADLESS=1        no window (always the case without SDL2)
 *   GG_WAV=file.wav      also write the audio to a WAV file
 *   GG_MUTE=1            no audio output;  GG_VOLUME=percent
 */
#include "runtime/rt.h"
#include "gen/recomp_all.h"
#include <stdlib.h>
#include <string.h>

void rt_load(CPU *cpu, const char *cmdline);
void rt_service_config(void);

int main(int argc, char **argv)
{
    int scale = 3;
    char cmd[128] = " -sb";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--game-dir") && i + 1 < argc) snprintf(g_game_dir, sizeof g_game_dir, "%s", argv[++i]);
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc) scale = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--")) {
            cmd[0] = 0;
            for (int j = i + 1; j < argc; j++) {
                strncat(cmd, " ", sizeof cmd - strlen(cmd) - 1);
                strncat(cmd, argv[j], sizeof cmd - strlen(cmd) - 1);
            }
            break;
        }
    }
    rt_service_config();

    static CPU cpu;
    cpu_init(&cpu);
    if (cpu_alloc_mem(&cpu)) return 1;
    rt_load(&cpu, cmd);
    if (host_init("Gizmos & Gadgets", scale)) fprintf(stderr, "[host] no window; running headless\n");
    sound_init();

    fprintf(stderr, "[gg] starting, game dir '%s', switches '%s'\n", g_game_dir, cmd);
    fn_00000(&cpu);                       /* Borland c0 startup -> main() -> exit */
    fprintf(stderr, "[gg] startup returned without exiting\n");
    recomp_dump_misses("misses.txt");
    host_shutdown();
    return 0;
}
