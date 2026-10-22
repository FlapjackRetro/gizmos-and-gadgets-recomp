# Modding

The recompiled game loads mods: shared libraries built against one header,
[`include/gg_mod.h`](../include/gg_mod.h). A mod is a separate project. It needs this header
and nothing else from the recomp: no game code, no generated C, no rebuild of the game.

## Loading

`gizmos` loads every library in `<game folder>\mods\` (`.dll` on Windows, `.so` on Linux, `.dylib`
or `.so` on macOS), then each one given with `--mod <path>`. `--no-mods` skips the folder. The
console log shows what was loaded:

```
[mod] example mod ready (game dir C:\DOS\Gizmos)
[mod] loaded C:\DOS\Gizmos\mods\example_mod.dll
[mod] 1 mod(s) active
```

A mod exports one function:

```c
#include "gg_mod.h"

static const GGApi *api;

static int on_race_end(void *user, GGEvent *e)
{
    api->log("race %s", e->won ? "won" : "lost");
    return GG_CONTINUE;
}

GG_MOD_EXPORT int gg_mod_init(const GGApi *a)
{
    if (a->version < GG_API_VERSION) return 1;     /* host too old */
    api = a;
    api->on_event(GG_EV_RACE_END, on_race_end, NULL);
    return 0;                                      /* nonzero: unload me */
}
```

[`mods/example/example_mod.c`](../mods/example/example_mod.c) uses every event. It is built with
the game (`example_mod.dll` next to `gizmos.exe`; the install script puts it in
`%LOCALAPPDATA%\GizmosRecomp\bin`). Copy it into `mods\` to try it, and set
`EXAMPLE_MOD_CHEATS=1` to see a mod change the game.

### Building a mod

Any C compiler. The game is 64-bit, so the mod must be too.

```sh
# Windows, MinGW / llvm-mingw
x86_64-w64-mingw32-gcc -shared -O2 -I path/to/recomp/include my_mod.c -o my_mod.dll
# Windows, MSVC (x64 Native Tools prompt)
cl /LD /O2 /I path\to\recomp\include my_mod.c /Fe:my_mod.dll
# Linux
gcc -shared -fPIC -O2 -I path/to/recomp/include my_mod.c -o my_mod.so
```

In CMake: `add_library(my_mod MODULE my_mod.c)` with the recomp's `include/` on the include path.

## Events

Register with `api->on_event(type, fn, user)`. Callbacks run in the order registered; the first
answer other than `GG_CONTINUE` decides. Fields marked *in/out* can be changed, and the game uses
the new value. Buildings: 0 Automotive, 1 Aircraft, 2 Alternative Energy. Levels are 0-based.

| Event | When | Fields | Mod can |
|---|---|---|---|
| `GG_EV_BUILDING_CLICK` | a building is clicked at the Technology Center | building | return `GG_ALLOW` (enter even when all 5 races are won), `GG_REFUSE` |
| `GG_EV_BUILDING_SELECTED` | the building is chosen | building | |
| `GG_EV_LEVEL_ENTER` | about to load a level | building, level *in/out*, restore *in/out* | load another level; generate a fresh warehouse (0) or restore the saved one (1) |
| `GG_EV_LEVEL_READY` | warehouse built or restored | building, level | |
| `GG_EV_WAREHOUSE_TICK` | each pass of the warehouse loop | building, level | act (safe point) |
| `GG_EV_ASSEMBLY_TICK` | each pass of the assembly room / blueprint loop | building, level | act (safe point) |
| `GG_EV_BOX_OPEN` | the player opens a box | room, object, contents *in/out* | change what is inside (`GG_BOX_EMPTY` = +5 bananas); return `GG_SUPPRESS`: the box goes, the game gives nothing |
| `GG_EV_PUZZLE_RESULT` | a door puzzle finished | room, door, puzzle_type, puzzle_variant, result *in/out* | pass or fail it (0 = the door stays shut) |
| `GG_EV_DOOR_OPEN` | a locked door opens | room, door, dest | |
| `GG_EV_RACE_START` | a race begins | building, level | |
| `GG_EV_RACE_END` | a race ended, before the win is counted | building, level, aborted, won *in/out* | decide the result |
| `GG_EV_FRAME` | ~60 times a second, everywhere (menus too) | | read/write memory; `defer` actions |

## Actions and safe points

`add_bananas`, `add_score`, `give_part` and `call_far` run the game's own routines. They do it
from inside the game's code, so call them only from an event or site callback, never from
`GG_EV_FRAME` (refused, with a log line). Work that arrives from outside (a network message, a
timer) goes through `api->defer(fn, user)`: `fn` runs at the next event or site, the same way.

- `give_part(part)` does what picking a part out of a box does: the part goes to the player's
  inventory, its name is shown, PARTS NEEDED counts down if the part is needed. Valid in a
  building (parts are per level). Returns 1 counted, 0 not needed, -1 refused (the player already
  holds two parts of that kind).
- `add_bananas(n)`, `add_score(points)` also refresh the HUD.
- `call_far(seg, off, n, args)` calls any routine (cdecl, word arguments) and returns DX:AX.

Memory: `ds_read16`/`ds_write16` (and 8-bit) work on the data segment, where the player record
lives; `mem()` is the whole 1 MB. `gg_mod.h` names the useful offsets (`GG_DS_BANANAS`,
`GG_DS_PARTS_NEEDED`, `GG_DS_RACES_WON_IDX`, ...). The full record layout, part tables, rooms and
doors are in the Archipelago project's reverse-engineering notes.

## Raw hook sites

Under the events are *hook sites*: instructions in [`data/hook_sites.txt`](../data/hook_sites.txt)
that the lifter compiles as

```c
if (!gg_site(cpu, 0x08F81)) {            /* hook site: box_open */
    flags_cmp16(cpu, mem_read16(cpu, cpu->es, cpu->bx + 0x8), 0x1);  /* cmp es:[bx+8], 1 */
}
```

`api->on_site(addr, fn, user)` (or `api->site_addr("box_open")`) gets the registers just before
the instruction: change them and the game carries on with the new values, return 1 to skip the
instruction. Skipping a `cmp` and setting `GG_FLAG_ZF` is how the events steer the game's
branches. The built-in event for a site runs first, then the raw hooks.

Only listed addresses can be hooked (`on_site` returns 0 otherwise); a site costs nothing
anywhere else. To add one, put the image offset of the instruction in `data/hook_sites.txt`,
rebuild, and check the lift's summary (`hook sites: N/N`). Site handlers that should become
events go in `src/mods/events.c`.

**Addresses** are image offsets in the flattened EXE: a resident routine `R<seg>_<off>` is at
`seg*16 + off` (R02E0_0A24 = 0x3824), an overlay routine `O<stub>_<off>` at
`newseg*16 + off` with `newseg` from `work/ssg_flat.map` (O0E30 -> C36E). `GGRegs` holds 16-bit
real-mode registers; segment values are image-space paragraphs, so `seg*16 + off` indexes `mem()`.

## Testing headless

The bring-up switches drive the game without a window; with `--mod` that tests a mod end to end.
See [BRINGUP.md](BRINGUP.md) for `GG_KEYS` / `GG_MOUSE` recipes, e.g. through the intro into
Aircraft and the warehouse.

## Compatibility

`GGApi` only grows: new members go at the end and `GG_API_VERSION` goes up. A mod built for
version N runs on any host with version >= N. The event and site addresses refer to the
DOS v1.0 `ssg.exe` (SHA-256 `79fa7b8d...`), the only version the recomp builds.
