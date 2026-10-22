# Bring-up log

What it took to get from a lifted binary to the Sign In screen, and why each piece is the way it is.

## Overlays (tools/unvroomm.py)

The EXE uses Borland VROOMM: about half the code (36 overlays, ~100 KB) sits in an `FBOV` block
after the load image and is reached through `INT 3Fh` stubs. The flattener:

- appends each overlay's code as a segment and rewrites every stub entry `CD 3F lo hi 00` into
  `JMP FAR newseg:lo`;
- resolves the overlay fixups. They hold **segment-table selectors** (index × 8), not paragraphs —
  all 2,027 of them (1,587 to resident code, 440 to other overlays' stubs) — and only then become
  MZ relocations;
- places the overlays with `--overlay-seg`. The default packs them after the image (best for
  Ghidra); the recomp uses `0xC000`, see below.

## Memory layout

- The image loads at linear 0 ("image space"): `fn_XXXXX` is both the function's name and its
  address, and relocated far pointers stay image offsets. The real IVT and BIOS data area are game
  code at that point, so vectors live in a shadow table (`g_vec_seg/off`).
- The game requires `farcoreleft() >= 0x58BEC` (~363 KB) at startup or it prints "not enough
  memory". Overlays therefore go into the empty upper-memory range at **C000:0000**; the far heap
  runs from the end of the stack up to the PSP at **9F00**.
- The PSP sits *above* the program, so Borland's `brk` asks to resize the PSP's block to a size that
  wraps around 64 K. INT 21h AH=4Ah grants that up to 9F00 and refuses beyond.
- Every vector starts at `F000:(vec*4)`. A handler that chains to its predecessor lands in
  `rt_bios_dispatch`, which pops the IRET frame — and for INT 9 does the BIOS's job of buffering
  the key for INT 16h (the game reads keys through INT 16h after its own INT 9 handler chains).

## Hardware checks the game makes

| Check | Code | Runtime answer |
|---|---|---|
| VGA | overlay 0E25 entry 0: INT 10h AX=1A00, **fails if AH is still 1Ah** | AX=001Ah, BL=08 (`-gv` skips the check) |
| Mouse | overlay 0E25 entry 1: INT 33h vector non-null and not an `IRET` | vector F000:00CC, INT 33h AX=0 → FFFF |
| Memory | `farcoreleft()` ≥ 0x58BEC | ~450 KB free |
| 286 | exit code 5 otherwise (ssg.bat prints the message) | passes |

The overlay manager opens the program's own EXE (`C:\GIZMOS\SSG.EXE` from the environment, then
its internal name `gizmo.exe`) at startup; the original `ssg.exe` must be in the game folder.

## Lifter fixes (tools/lift.py, on top of DinoPark's driver)

- **Code ranges.** Code is in two ranges (resident `[0, DD70)`, overlays `[C0000, D76B0)`);
  nothing outside them may become a function, and no function may run past a range's end. Before
  this, one near call into DGROUP produced a 41 MB function.
- **Exact code segments.** Each function's CS comes from the VROOMM segment table / flattener map,
  not a heuristic. The wrong CS for the overlay manager made it read its data segment from the
  wrong `cs:[3]`.
- **Shared bodies.** Borland's `malloc` builds a frame and `jmp`s into the middle of `farmalloc`.
  Functions are extended over bodies their jumps reach (22 of them), so the jump is a `goto` and
  the shared epilogue runs.
- **`inc bp` frames.** Overlaid modules use `inc bp; push bp; mov bp, sp … dec bp; ret`; entries are
  moved back onto the `inc bp` (75 functions).
- **Startup `_INIT_` table.** Found by scanning the first 0x400 bytes of the c0 startup.
- **Entry points discovered at run time** are promoted when they are a Borland prologue, a lone
  `ret/retf`, or start right after a `ret/retf/iret` (hand-written assembly such as the LPX
  decoder at 0399:0465).

## Debugging aids

- `GG_SPCHECK=1 python tools/lift.py` + `-DGG_SPCHECK=ON`: every lifted function audits its stack
  delta and BP; the first console output prints the live call chain.
- Benign audit noise: `_setargv` (fn_016B8) leaves the argument strings on the stack (−36), and
  `inc bp` entries split across two lifted functions show BP odd→even.
