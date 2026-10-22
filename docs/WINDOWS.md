# Windows

Two ways to get a Windows x64 build. Both produce one `gizmos.exe` with SDL2 linked in; it needs
only DLLs that ship with Windows 10/11.

## Run it

Put `gizmos.exe` in the game folder (the one with `gizmo000.dat`…`gizmo004.dat`, `PLAYERS.DAT` and
the original `ssg.exe`) and double-click it, or run it from a terminal:

```bat
gizmos.exe                         :: game folder = current folder
gizmos.exe --game-dir C:\DOS\Gizmos
gizmos.exe --game-dir C:\DOS\Gizmos --scale 4 -- -sb
```

A console window opens alongside the game; it shows the log. Close the game window to quit.
For a bug report, run with `set GG_TRACE=1` first and save the console output.

## Install script (easiest)

Download the repository (*Code → Download ZIP* on GitHub, or just `install-windows.bat` and
`install-windows.ps1`) and double-click **`install-windows.bat`**. It:

1. checks for Python 3.8+, CMake 3.20+, Ninja and a MinGW-w64 compiler, and downloads portable
   copies of any that are missing (Python embeddable, CMake, Ninja, llvm-mingw) into
   `%LOCALAPPDATA%\GizmosRecomp\tools` -- no admin rights, nothing installed system-wide;
2. gets this repository (unless it is run from inside one) and pcrecomp;
3. asks for your `ssg.exe` with a file dialog and checks its SHA-256;
4. generates the C, builds `gizmos.exe` (SDL2 is downloaded and built too), puts it in
   `%LOCALAPPDATA%\GizmosRecomp\bin` and offers to copy it into your game folder.

Options: `install-windows.bat -SsgExe C:\DOS\Gizmos\ssg.exe`, `-InstallDir D:\Gizmos`, `-NoCopy`.
Downloads total about 250 MB the first time; the build takes several minutes.

## Build natively with MSVC

Install: Visual Studio 2022 (or Build Tools) with *Desktop development with C++*, CMake ≥ 3.20
(included with VS), Python 3, Git.

From a *Developer PowerShell for VS 2022*:

```powershell
git clone https://github.com/sp00nznet/pcrecomp
git clone https://github.com/FlapjackRetro/gizmos-and-gadgets-recomp
cd gizmos-and-gadgets-recomp
copy C:\DOS\Gizmos\ssg.exe original\
python tools\build.py
cmake -S . -B build
cmake --build build --config Release
build\Release\gizmos.exe --game-dir C:\DOS\Gizmos
```

The generated C is ~6 MB; the first build takes a few minutes. SDL2 is downloaded and built by
CMake (WASAPI/DirectSound audio).

## Cross-compile from Linux or macOS

With [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) (or a `gcc-mingw-w64` package):

```sh
python tools/build.py
cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake \
      -DMINGW_ROOT=/path/to/llvm-mingw -DCMAKE_BUILD_TYPE=Release
cmake --build build-win
```

## Status

- Built with llvm-mingw 20241217 (Clang 19) through the install script, and run on Windows.
- MSVC build: not yet tried. The code avoids GCC extensions; the lifted code's anonymous
  struct-in-union register layout (`cpu.h`) is C11 and accepted by MSVC.
