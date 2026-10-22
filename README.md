# Gizmos & Gadgets — native recompilation

A native build of *Super Solvers: Gizmos & Gadgets* (The Learning Company, 1993, DOS v1.0)
that runs without DOSBox. The game's 16-bit x86 code is statically recompiled to C and linked
against a small runtime that stands in for DOS, the BIOS and the PC hardware, with SDL2 for the
window and input.

This repository is the **recompilation** only: a faithful, moddable native port. The Archipelago
randomizer is a separate project built on top of it.

No game code or data is in this repository. You bring your own `ssg.exe` and data files; the C is
generated from your copy at build time.

## Status

| Area | State |
|---|---|
| Lift | all 1,346 functions lift to C and compile (GCC, Clang; MSVC untested) |
| Boot | Borland startup, VROOMM overlay manager, hardware checks, resource archives |
| Video | VGA mode 13h, palette, 60 Hz presentation |
| Input | keyboard through the game's own INT 9 handler; INT 33h mouse driver |
| Reached | title, intro, Sign In, Technology Center, blueprints (tooltips, part pages), assembly room, warehouse (walking, jumping, doors), door puzzles |
| Sound | OPL2 FM music and effects (`-sb`, the default) via emu8950; PC speaker (`-si`) wired but unverified |
| Platforms | Linux: built and run (headless and SDL). Windows x64: built with the install script and run. macOS: untested |

## Build

You need Python 3, CMake ≥ 3.20, a C compiler, and a checkout of
[pcrecomp](https://github.com/sp00nznet/pcrecomp) next to this repository (or `PCRECOMP_HOME`
pointing at one). SDL2 is downloaded and built by CMake if it is not installed; on Linux install
your distribution's package instead (`sudo apt install libsdl2-dev` on Ubuntu), because a
from-source SDL2 only gets audio back-ends whose development headers are present.

```sh
git clone https://github.com/sp00nznet/pcrecomp ../pcrecomp
cp /path/to/your/game/ssg.exe original/
python tools/build.py                       # generates src/gen from your ssg.exe
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

**Windows:** double-click `install-windows.bat`. It fetches anything missing, asks for your
`ssg.exe` and builds `gizmos.exe`. Manual MSVC builds and cross-compiling: [docs/WINDOWS.md](docs/WINDOWS.md).

## Run

```sh
build/gizmos --game-dir /path/to/your/game        # folder with gizmo000-004.dat, PLAYERS.DAT, ssg.exe
build/gizmos --game-dir ... -- -sn -gv            # pass the game's own switches after --
```

The game folder needs the original `ssg.exe` too: the game's overlay manager opens it at startup
to check the overlay block, even though the overlays themselves are already compiled in.

Bring-up switches (environment variables):

| Variable | Effect |
|---|---|
| `GG_TRACE=1` / `2` | log DOS, BIOS, unhandled ports / every BIOS interrupt |
| `GG_FILM=dir[:ms]` | save the screen as BMP every *ms* (default 1000) |
| `GG_KEYS=ms:sc[/hold],...` | press PC scancode *sc* (hex) at *ms* for *hold* ms (default 100), e.g. `20000:1C` = Enter; extended keys with their prefix, `E048` = Up arrow |
| `GG_MOUSE=ms:x,y,b;...` | at *ms* put the pointer at (*x*,*y*) (320x200) with buttons *b* (1 = left) |
| `GG_QUIT_AFTER=ms` | exit after *ms*, writing `last_frame.bmp` and `misses.txt` |
| `GG_MEMDUMP=file` | with `GG_QUIT_AFTER`: also save the 1 MB guest memory |
| `GG_WATCH=addr:len,...` | build with `-DGG_WATCH=ON`: report every write to those linear addresses, with a host backtrace |
| `GG_HEADLESS=1` | no window |
| `GG_WAV=file.wav` | also record the audio to a WAV file |
| `GG_MUTE=1`, `GG_VOLUME=percent` | audio off / level (100 = default) |

## How it works

```
original/ssg.exe
  └─ tools/unvroomm.py      flatten Borland VROOMM overlays into one MZ image
                            (overlay code moved to C000:0000, selectors resolved)
  └─ tools/find_functions.py  function starts: Ghidra seeds (data/) + pcrecomp's analyzer
  └─ tools/lift.py          pcrecomp lift16 → src/gen/recomp_*.c  (+ dispatch table)
  └─ tools/embed_image.py   the load image (initial data, tables) → src/gen/image_data.c
src/runtime/                DOS (INT 21h), BIOS (10h/16h/1Ah), INT 33h mouse, PIT, PIC,
                            keyboard controller, VGA DAC, OPL2 + PC speaker audio;
                            calls the game's own INT 8/9 handlers
third_party/emu8950/        OPL2 (YM3812) emulator, MIT
src/host/                   SDL2 window/input, or headless
```

Lifted functions are named after their address in the flattened image (`fn_XXXXX`). Addresses the
game reaches only at run time (interrupt handlers it installs, function pointers) are recorded in
`data/runtime_*.txt` so the lift is reproducible. See [docs/BRINGUP.md](docs/BRINGUP.md) for the
decisions and fixes made along the way.

## Roadmap

1. A mod hook API at the points already mapped by the AP project: race won, door opened, part box
   opened, building select, level entry, item grant.
2. Replace lifted functions with readable C, starting with the ones mods touch.
3. Windows and macOS builds tested and packaged.

## Credits

Built on [pcrecomp](https://github.com/sp00nznet/pcrecomp) and the
[DinoPark Tycoon recomp](https://github.com/sp00nznet/dinopark) by sp00nz (both MIT);
see [NOTICE.md](NOTICE.md). *Gizmos & Gadgets* is © 1993 The Learning Company.
