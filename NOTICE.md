# Third-party code

| Path | Upstream | Licence |
|---|---|---|
| `src/runtime/cpu.h`, `src/runtime/cpu.c` | [pcrecomp](https://github.com/sp00nznet/pcrecomp) `runtime/recomp16`, Copyright (c) 2026 sp00nz | MIT, [LICENSES/pcrecomp-MIT.txt](LICENSES/pcrecomp-MIT.txt) |
| `tools/lift.py` | forked from DinoPark Tycoon recomp `tools/lift_full.py` ([sp00nznet/dinopark](https://github.com/sp00nznet/dinopark)), Copyright (c) 2025-2026 sp00nz | MIT, [LICENSES/dinopark-MIT.txt](LICENSES/dinopark-MIT.txt) |
| `src/runtime/*.c` (parts) | DOS/BIOS handling and the guest-ISR call follow DinoPark's `src/recomp/runtime16.c` | MIT, [LICENSES/dinopark-MIT.txt](LICENSES/dinopark-MIT.txt) |
| build-time tools | pcrecomp `tools/disasm` and `tools/lift` (`decode16`, `analyze`, `lift16`), used from a checkout, not vendored | MIT |
| `third_party/emu8950/` | [emu8950](https://github.com/digital-sound-antiques/emu8950), Copyright (c) 2014 Mitsutaka Okazaki, vendored unmodified | MIT, [third_party/emu8950/LICENSE](third_party/emu8950/LICENSE) |
| SDL2 | [libsdl.org](https://www.libsdl.org/), downloaded by CMake when not installed | zlib |
