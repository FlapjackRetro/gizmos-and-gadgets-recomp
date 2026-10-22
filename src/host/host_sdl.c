/*
 * host_sdl.c - SDL2 window, keyboard and mouse.
 *
 * The 320x200 frame is drawn into a 4:3 window (each row 1.2x tall, as on a
 * VGA monitor). Keys are translated to PC set-1 scancodes and queued for the
 * game's own INT 9 handler; the mouse is reported in 320x200 coordinates.
 */
#include "../runtime/rt.h"
#define SDL_MAIN_HANDLED          /* our main() stays main(); no SDL2main */
#include <SDL.h>
#include <stdlib.h>

static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Texture *tex;
static int quit;
static SDL_AudioDeviceID audio;
static int audio_rate;

int host_init(const char *title, int scale)
{
    SDL_SetMainReady();
    if (getenv("GG_HEADLESS")) return 1;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) return 1;
    if (scale < 1) scale = 1;
    win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                           320 * scale, 240 * scale, SDL_WINDOW_RESIZABLE);
    if (!win) return 1;
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, 0);
    SDL_RenderSetLogicalSize(ren, 320, 240);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 320, 200);
    SDL_ShowCursor(SDL_DISABLE);
    return 0;
}

void host_present(const uint32_t *px)
{
    if (!tex) return;
    SDL_UpdateTexture(tex, NULL, px, 320 * 4);
    SDL_RenderClear(ren);
    SDL_Rect dst = { 0, 0, 320, 240 };
    SDL_RenderCopy(ren, tex, NULL, &dst);
    SDL_RenderPresent(ren);
}

/* SDL scancode (USB HID usage) -> PC set-1 make code; 0x80 bit = extended (E0). */
static int set1(SDL_Scancode s)
{
    if (s >= SDL_SCANCODE_A && s <= SDL_SCANCODE_Z) {
        static const uint8_t az[26] = { 0x1E,0x30,0x2E,0x20,0x12,0x21,0x22,0x23,0x17,0x24,0x25,0x26,0x32,
                                        0x31,0x18,0x19,0x10,0x13,0x1F,0x14,0x16,0x2F,0x11,0x2D,0x15,0x2C };
        return az[s - SDL_SCANCODE_A];
    }
    if (s >= SDL_SCANCODE_1 && s <= SDL_SCANCODE_0) return 0x02 + (s - SDL_SCANCODE_1);
    if (s >= SDL_SCANCODE_F1 && s <= SDL_SCANCODE_F10) return 0x3B + (s - SDL_SCANCODE_F1);
    switch (s) {
    case SDL_SCANCODE_RETURN: return 0x1C;   case SDL_SCANCODE_ESCAPE: return 0x01;
    case SDL_SCANCODE_BACKSPACE: return 0x0E; case SDL_SCANCODE_TAB: return 0x0F;
    case SDL_SCANCODE_SPACE: return 0x39;    case SDL_SCANCODE_MINUS: return 0x0C;
    case SDL_SCANCODE_EQUALS: return 0x0D;   case SDL_SCANCODE_LEFTBRACKET: return 0x1A;
    case SDL_SCANCODE_RIGHTBRACKET: return 0x1B; case SDL_SCANCODE_SEMICOLON: return 0x27;
    case SDL_SCANCODE_APOSTROPHE: return 0x28; case SDL_SCANCODE_GRAVE: return 0x29;
    case SDL_SCANCODE_BACKSLASH: return 0x2B; case SDL_SCANCODE_COMMA: return 0x33;
    case SDL_SCANCODE_PERIOD: return 0x34;   case SDL_SCANCODE_SLASH: return 0x35;
    case SDL_SCANCODE_LSHIFT: return 0x2A;   case SDL_SCANCODE_RSHIFT: return 0x36;
    case SDL_SCANCODE_LCTRL: return 0x1D;    case SDL_SCANCODE_LALT: return 0x38;
    case SDL_SCANCODE_F11: return 0x57;      case SDL_SCANCODE_F12: return 0x58;
    case SDL_SCANCODE_UP: return 0x80 | 0x48;    case SDL_SCANCODE_DOWN: return 0x80 | 0x50;
    case SDL_SCANCODE_LEFT: return 0x80 | 0x4B;  case SDL_SCANCODE_RIGHT: return 0x80 | 0x4D;
    case SDL_SCANCODE_HOME: return 0x80 | 0x47;  case SDL_SCANCODE_END: return 0x80 | 0x4F;
    case SDL_SCANCODE_PAGEUP: return 0x80 | 0x49; case SDL_SCANCODE_PAGEDOWN: return 0x80 | 0x51;
    case SDL_SCANCODE_INSERT: return 0x80 | 0x52; case SDL_SCANCODE_DELETE: return 0x80 | 0x53;
    case SDL_SCANCODE_RCTRL: return 0x80 | 0x1D; case SDL_SCANCODE_RALT: return 0x80 | 0x38;
    case SDL_SCANCODE_KP_ENTER: return 0x80 | 0x1C;
    default: return 0;
    }
}

void host_pump(void)
{
    if (!ren) return;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT: quit = 1; break;
        case SDL_KEYDOWN: case SDL_KEYUP: {
            if (e.key.repeat && e.type == SDL_KEYUP) break;
            int c = set1(e.key.keysym.scancode);
            if (!c) break;
            if (c & 0x80) kbd_push_scancode(0xE0);
            kbd_push_scancode((uint8_t)((c & 0x7F) | (e.type == SDL_KEYUP ? 0x80 : 0)));
            break;
        }
        default: break;
        }
    }
    int wx, wy;
    uint32_t b = SDL_GetMouseState(&wx, &wy);
    float lx = 0, ly = 0;
    SDL_RenderWindowToLogical(ren, wx, wy, &lx, &ly);
    int x = (int)lx, y = (int)(ly * 200.0f / 240.0f);
    int buttons = ((b & SDL_BUTTON_LMASK) ? 1 : 0) | ((b & SDL_BUTTON_RMASK) ? 2 : 0) | ((b & SDL_BUTTON_MMASK) ? 4 : 0);
    mouse_host_update(x < 0 ? 0 : x > 319 ? 319 : x, y < 0 ? 0 : y > 199 ? 199 : y, buttons);
}

int host_quit_requested(void) { return quit; }
int host_headless(void) { return win == NULL; }

/* Push-model audio: the runtime renders on the game thread in step with the
 * wall clock and queues it here. Keep ~40-120 ms queued: below that, pad with
 * silence before it underruns; above, drop the excess so latency stays low. */
void host_audio_open(int rate)
{
    if (getenv("GG_HEADLESS")) return;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return;
    SDL_AudioSpec want = {0}, have;
    want.freq = rate; want.format = AUDIO_S16SYS; want.channels = 1; want.samples = 512;
    audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!audio) return;
    audio_rate = rate;
    SDL_PauseAudioDevice(audio, 0);
}

void host_audio_push(const int16_t *s, int n)
{
    if (!audio) return;
    Uint32 queued = SDL_GetQueuedAudioSize(audio) / 2;
    Uint32 lo = (Uint32)audio_rate / 25, hi = (Uint32)audio_rate / 8;   /* 40 ms, 125 ms */
    if (queued > hi) return;
    if (queued < lo / 2) {
        static int16_t pad[2048];
        Uint32 need = lo - queued;
        if (need > 2048) need = 2048;
        SDL_QueueAudio(audio, pad, need * 2);
    }
    SDL_QueueAudio(audio, s, (Uint32)n * 2);
}

void host_shutdown(void)
{
    if (audio) { SDL_CloseAudioDevice(audio); audio = 0; }
    if (!win && !ren) return;
    if (tex) SDL_DestroyTexture(tex);
    if (ren) SDL_DestroyRenderer(ren);
    if (win) SDL_DestroyWindow(win);
    tex = NULL; ren = NULL; win = NULL;
    SDL_Quit();
}
