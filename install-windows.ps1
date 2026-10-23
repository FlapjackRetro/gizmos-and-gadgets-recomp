<#
.SYNOPSIS
  Installs the Gizmos & Gadgets native recompilation on Windows.

.DESCRIPTION
  1. Checks for the build prerequisites -- Python 3, CMake, Ninja and a MinGW-w64 C compiler --
     and downloads portable copies of any that are missing into the install folder (no admin
     rights, nothing installed system-wide).
  2. Gets the recompilation sources and the pcrecomp toolkit.
  3. Asks for your copy of the game's ssg.exe (DOS v1.0) and checks it.
  4. Recompiles the game into gizmos.exe and offers to copy it into your game folder.

  Run it by double-clicking install-windows.bat, or from PowerShell:
    powershell -ExecutionPolicy Bypass -File install-windows.ps1 [-SsgExe C:\DOS\Gizmos\ssg.exe]

.PARAMETER InstallDir
  Where tools, sources and the build go. Default: %LOCALAPPDATA%\GizmosRecomp

.PARAMETER SsgExe
  Path to ssg.exe. If omitted, a file dialog asks for it.

.PARAMETER NoCopy
  Do not offer to copy gizmos.exe into the game folder.
#>
[CmdletBinding()]
param(
    [string]$InstallDir = (Join-Path $env:LOCALAPPDATA 'GizmosRecomp'),
    [string]$SsgExe = '',
    [switch]$NoCopy
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'          # Invoke-WebRequest is very slow with a progress bar
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$KnownSsgSha256 = '79FA7B8D5B1E5D7BE04F9F7A455A0F04D0823D41C6CBBDC6A8BD90033BA532F9'

# Portable tool downloads (used only when a tool is not already installed)
$Downloads = @{
    Python    = @{ Url = 'https://www.python.org/ftp/python/3.12.7/python-3.12.7-embed-amd64.zip'; Dir = 'python' }
    CMake     = @{ Url = 'https://github.com/Kitware/CMake/releases/download/v3.30.5/cmake-3.30.5-windows-x86_64.zip'; Dir = 'cmake' }
    Ninja     = @{ Url = 'https://github.com/ninja-build/ninja/releases/download/v1.12.1/ninja-win.zip'; Dir = 'ninja' }
    LlvmMingw = @{ Url = 'https://github.com/mstorsjo/llvm-mingw/releases/download/20241217/llvm-mingw-20241217-ucrt-x86_64.zip'; Dir = 'llvm-mingw' }
}
$RecompZip   = 'https://github.com/FlapjackRetro/gizmos-and-gadgets-recomp/archive/refs/heads/main.zip'
$PcrecompZip = 'https://github.com/sp00nznet/pcrecomp/archive/refs/heads/main.zip'

$ToolsDir = Join-Path $InstallDir 'tools'

# ---------------------------------------------------------------------------- helpers

function Write-Step([string]$msg) { Write-Host ''; Write-Host "==> $msg" -ForegroundColor Cyan }
function Write-Ok([string]$msg)   { Write-Host "    $msg" -ForegroundColor Green }
function Write-Note([string]$msg) { Write-Host "    $msg" }
function Fail([string]$msg) {
    Write-Host ''
    Write-Host "ERROR: $msg" -ForegroundColor Red
    throw $msg
}

function Get-Download([string]$url, [string]$dest) {
    Write-Note "downloading $url"
    $tmp = "$dest.part"
    Invoke-WebRequest -Uri $url -OutFile $tmp -UseBasicParsing
    Move-Item -Force $tmp $dest
}

# Download a zip and unpack it into $ToolsDir\<dir>, flattening a single top-level folder.
function Install-PortableZip([string]$name, [string]$url, [string]$dir) {
    $target = Join-Path $ToolsDir $dir
    if (Test-Path $target) { Remove-Item -Recurse -Force $target }
    New-Item -ItemType Directory -Force -Path $ToolsDir | Out-Null
    $zip = Join-Path $ToolsDir "$dir.zip"
    Get-Download $url $zip
    $staging = Join-Path $ToolsDir "$dir.unpack"
    if (Test-Path $staging) { Remove-Item -Recurse -Force $staging }
    Expand-Archive -Path $zip -DestinationPath $staging -Force
    Remove-Item -Force $zip
    $items = @(Get-ChildItem -Force $staging)
    if ($items.Count -eq 1 -and $items[0].PSIsContainer) {
        Move-Item $items[0].FullName $target
        Remove-Item -Recurse -Force $staging
    } else {
        Move-Item $staging $target
    }
    Write-Ok "$name installed to $target"
    return $target
}

function Add-ToPath([string]$dir) {
    if (($env:PATH -split ';') -notcontains $dir) { $env:PATH = "$dir;$env:PATH" }
}

# Run a program; returns its output, or $null if it cannot be run.
function Try-Run([string]$exe, [string[]]$arguments) {
    $ErrorActionPreference = 'Continue'       # a probe writing to stderr is not a failure
    try {
        $out = & $exe @arguments 2>&1
        if ($LASTEXITCODE -ne 0) { return $null }
        return ($out | Out-String)
    } catch { return $null }
}

function Invoke-Checked([string]$exe, [string[]]$arguments, [string]$what) {
    $ErrorActionPreference = 'Continue'       # build tools print warnings to stderr
    & $exe @arguments
    if ($LASTEXITCODE -ne 0) { Fail "$what failed (exit code $LASTEXITCODE). See the output above." }
}

# ---------------------------------------------------------------------------- prerequisites

function Resolve-Python {
    # A real CPython 3.8+; the Microsoft Store alias "python.exe" prints nothing useful and fails.
    $local = Join-Path $ToolsDir 'python\python.exe'
    foreach ($cand in @($local, 'python', 'py')) {
        $argsPrefix = @()
        if ($cand -eq 'py') { $argsPrefix = @('-3') }
        $v = Try-Run $cand ($argsPrefix + @('-c', 'import sys; print("%d.%d" % sys.version_info[:2])'))
        if ($v -and ([version]$v.Trim()) -ge [version]'3.8') {
            if ($cand -eq 'py') {
                $exe = (Try-Run 'py' @('-3', '-c', 'import sys; print(sys.executable)')).Trim()
                return $exe
            }
            if ($cand -eq 'python') { return (Get-Command python).Source }
            return $cand
        }
    }
    # The embeddable package's ._pth file keeps a script's own folder off sys.path; the build
    # scripts add every folder they import from explicitly, so it can stay as shipped.
    $dir = Install-PortableZip 'Python' $Downloads.Python.Url $Downloads.Python.Dir
    return (Join-Path $dir 'python.exe')
}

function Resolve-CMake {
    $local = Join-Path $ToolsDir 'cmake\bin\cmake.exe'
    foreach ($cand in @($local, 'cmake')) {
        $v = Try-Run $cand @('--version')
        if ($v -and $v -match 'cmake version (\d+\.\d+)') {
            if ([version]$Matches[1] -ge [version]'3.20') {
                if ($cand -eq 'cmake') { return (Get-Command cmake).Source }
                return $cand
            }
        }
    }
    $dir = Install-PortableZip 'CMake' $Downloads.CMake.Url $Downloads.CMake.Dir
    return (Join-Path $dir 'bin\cmake.exe')
}

function Resolve-Ninja {
    $local = Join-Path $ToolsDir 'ninja\ninja.exe'
    foreach ($cand in @($local, 'ninja')) {
        if (Try-Run $cand @('--version')) {
            if ($cand -eq 'ninja') { return (Get-Command ninja).Source }
            return $cand
        }
    }
    $dir = Install-PortableZip 'Ninja' $Downloads.Ninja.Url $Downloads.Ninja.Dir
    return (Join-Path $dir 'ninja.exe')
}

function Resolve-Compiler {
    # Any MinGW-w64 GCC/Clang targeting x86_64 Windows. MSVC users can follow docs\WINDOWS.md.
    $local = Join-Path $ToolsDir 'llvm-mingw\bin\x86_64-w64-mingw32-gcc.exe'
    foreach ($cand in @($local, 'x86_64-w64-mingw32-gcc', 'gcc')) {
        $m = Try-Run $cand @('-dumpmachine')
        if ($m -and $m.Trim() -match '^x86_64-.*mingw32') {
            if ($cand -ne $local) { return (Get-Command $cand).Source }
            return $cand
        }
    }
    Write-Note 'no MinGW-w64 compiler found; fetching llvm-mingw (about 150 MB)'
    $dir = Install-PortableZip 'llvm-mingw' $Downloads.LlvmMingw.Url $Downloads.LlvmMingw.Dir
    return (Join-Path $dir 'bin\x86_64-w64-mingw32-gcc.exe')
}

# ---------------------------------------------------------------------------- sources

function Get-SourceZip([string]$name, [string]$url, [string]$target) {
    if (Test-Path $target) { Remove-Item -Recurse -Force $target }
    $zip = "$target.zip"
    Get-Download $url $zip
    $staging = "$target.unpack"
    if (Test-Path $staging) { Remove-Item -Recurse -Force $staging }
    Expand-Archive -Path $zip -DestinationPath $staging -Force
    Remove-Item -Force $zip
    $top = @(Get-ChildItem $staging)[0]
    Move-Item $top.FullName $target
    Remove-Item -Recurse -Force $staging
    Write-Ok "$name -> $target"
}

function Resolve-Sources {
    $srcDir = Join-Path $InstallDir 'src'
    New-Item -ItemType Directory -Force -Path $srcDir | Out-Null

    # Run from inside a checkout of the repository? Build that checkout.
    $here = Split-Path -Parent $PSCommandPath
    if (Test-Path (Join-Path $here 'tools\build.py')) {
        Write-Ok "using the repository this script is in: $here"
        $recomp = $here
    } else {
        $recomp = Join-Path $srcDir 'gizmos-and-gadgets-recomp'
        Get-SourceZip 'gizmos-and-gadgets-recomp' $RecompZip $recomp
    }

    $pcrecomp = Join-Path $srcDir 'pcrecomp'
    if ($env:PCRECOMP_HOME -and (Test-Path (Join-Path $env:PCRECOMP_HOME 'tools\disasm'))) {
        $pcrecomp = $env:PCRECOMP_HOME
        Write-Ok "using PCRECOMP_HOME: $pcrecomp"
    } else {
        Get-SourceZip 'pcrecomp' $PcrecompZip $pcrecomp
    }
    return @{ Recomp = $recomp; Pcrecomp = $pcrecomp }
}

# ---------------------------------------------------------------------------- ssg.exe

function Select-SsgExe {
    if ($SsgExe) {
        if (-not (Test-Path $SsgExe)) { Fail "ssg.exe not found at $SsgExe" }
        return (Resolve-Path $SsgExe).Path
    }
    $haveForms = $true
    try { Add-Type -AssemblyName System.Windows.Forms } catch { $haveForms = $false }
    if ($haveForms) {
        Write-Note 'Select the game''s ssg.exe in the window that opens.'
        $dlg = New-Object System.Windows.Forms.OpenFileDialog
        $dlg.Title = 'Select ssg.exe from Super Solvers: Gizmos & Gadgets (DOS)'
        $dlg.Filter = 'ssg.exe|ssg.exe|DOS programs (*.exe)|*.exe|All files (*.*)|*.*'
        if (Test-Path 'C:\DOS\Gizmos') { $dlg.InitialDirectory = 'C:\DOS\Gizmos' }
        if ($dlg.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK) { return $dlg.FileName }
        Fail 'No ssg.exe selected.'
    }
    $p = Read-Host 'Full path to ssg.exe'
    if (-not (Test-Path $p)) { Fail "ssg.exe not found at $p" }
    return (Resolve-Path $p).Path
}

function Test-GameFolder([string]$dir) {
    $missing = @()
    foreach ($f in @('gizmo000.dat', 'gizmo001.dat', 'gizmo002.dat', 'gizmo003.dat')) {
        if (-not (Test-Path (Join-Path $dir $f))) { $missing += $f }
    }
    return $missing
}

# ---------------------------------------------------------------------------- main

Write-Host 'Gizmos & Gadgets - native recompilation installer' -ForegroundColor White
Write-Host "Install folder: $InstallDir"
New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null

Write-Step 'Checking prerequisites'
$python = Resolve-Python;   Write-Ok "Python:   $python"
$cmake  = Resolve-CMake;    Write-Ok "CMake:    $cmake"
$ninja  = Resolve-Ninja;    Write-Ok "Ninja:    $ninja"
$cc     = Resolve-Compiler; Write-Ok "Compiler: $cc"
# SDL2's CMake project also enables C++ and compiles a Windows resource file, so hand CMake the
# matching g++ and windres from the same toolchain (otherwise it may go looking for MSVC).
$ccDir = Split-Path -Parent $cc
$prefix = ''
if ((Split-Path -Leaf $cc) -match '^(.*-)gcc(\.exe)?$') { $prefix = $Matches[1] }
$cxx = Join-Path $ccDir "${prefix}g++.exe"
$rc  = Join-Path $ccDir "${prefix}windres.exe"
if (-not (Test-Path $cxx)) { $cxx = Join-Path $ccDir 'g++.exe' }
if (-not (Test-Path $rc))  { $rc  = Join-Path $ccDir 'windres.exe' }
if (-not (Test-Path $cxx)) { Fail "found $cc but no matching g++ next to it" }
if (-not (Test-Path $rc))  { Fail "found $cc but no matching windres next to it" }
Write-Ok "C++:      $cxx"
Write-Ok "windres:  $rc"
Add-ToPath (Split-Path -Parent $ninja)
Add-ToPath (Split-Path -Parent $cc)

Write-Step 'Getting the sources'
$src = Resolve-Sources
$env:PCRECOMP_HOME = $src.Pcrecomp

Write-Step 'Your copy of the game'
$ssg = Select-SsgExe
$hash = (Get-FileHash -Algorithm SHA256 $ssg).Hash
if ($hash -eq $KnownSsgSha256) {
    Write-Ok "ssg.exe is the known DOS v1.0 build ($ssg)"
} else {
    Write-Host "    Warning: this ssg.exe (SHA-256 $hash) is not the v1.0 build the recompilation" -ForegroundColor Yellow
    Write-Host '    was made from. The build will probably fail or the game misbehave.' -ForegroundColor Yellow
    $ans = Read-Host '    Continue anyway? [y/N]'
    if ($ans -notmatch '^[yY]') { Fail 'Stopped: unknown ssg.exe.' }
}
$gameDir = Split-Path -Parent $ssg
$missing = Test-GameFolder $gameDir
if ($missing.Count) {
    Write-Host "    Note: $gameDir is missing $($missing -join ', ') - the game needs these next to gizmos.exe." -ForegroundColor Yellow
}
$origDir = Join-Path $src.Recomp 'original'
New-Item -ItemType Directory -Force -Path $origDir | Out-Null
Copy-Item -Force $ssg (Join-Path $origDir 'ssg.exe')

Write-Step 'Recompiling the game code to C (a few minutes)'
Push-Location $src.Recomp
try {
    Invoke-Checked $python @('tools\build.py') 'Generating the C code'

    Write-Step 'Compiling gizmos.exe (several minutes; SDL2 is downloaded and built too)'
    $buildDir = Join-Path $src.Recomp 'build-windows'
    # A configure that failed (or used other tools) leaves its compiler checks behind: start clean.
    foreach ($stale in @('CMakeCache.txt', 'CMakeFiles')) {
        $p = Join-Path $buildDir $stale
        if (Test-Path $p) { Remove-Item -Recurse -Force $p }
    }
    # Forward slashes: CMake writes CMAKE_RC_COMPILER verbatim into CMakeRCCompiler.cmake, where a
    # backslash path (C:\Users\...) is read as escape sequences ("Invalid character escape '\U'").
    $cmakePath = { param($p) $p -replace '\\', '/' }
    Invoke-Checked $cmake @('-S', '.', '-B', $buildDir, '-G', 'Ninja',
                            '-DCMAKE_BUILD_TYPE=Release',
                            "-DCMAKE_C_COMPILER=$(& $cmakePath $cc)",
                            "-DCMAKE_CXX_COMPILER=$(& $cmakePath $cxx)",
                            "-DCMAKE_RC_COMPILER=$(& $cmakePath $rc)",
                            "-DCMAKE_MAKE_PROGRAM=$(& $cmakePath $ninja)") 'Configuring the build'
    Invoke-Checked $cmake @('--build', $buildDir) 'Compiling'
} finally {
    Pop-Location
}

$exe = Join-Path $buildDir 'gizmos.exe'
if (-not (Test-Path $exe)) { Fail "the build finished but $exe is missing" }
$outDir = Join-Path $InstallDir 'bin'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
Copy-Item -Force $exe (Join-Path $outDir 'gizmos.exe')
# The example mod is not activated: mods load from <game folder>\mods\ or with --mod.
$exampleMod = Join-Path $buildDir 'example_mod.dll'
if (Test-Path $exampleMod) { Copy-Item -Force $exampleMod (Join-Path $outDir 'example_mod.dll') }
$debugBat = "@echo off`r`nrem Runs the game with full logging. Afterwards send gizmos-log.txt and misses.txt.`r`nset GG_TRACE=1`r`ngizmos.exe %* 2> gizmos-log.txt`r`n"
Set-Content -Path (Join-Path $outDir 'gizmos-debug.bat') -Value $debugBat -NoNewline -Encoding ASCII
Write-Ok "built: $(Join-Path $outDir 'gizmos.exe')"

if (-not $NoCopy) {
    Write-Step 'Install into the game folder'
    $ans = Read-Host "    Copy gizmos.exe and gizmos-debug.bat into $gameDir ? [Y/n]"
    if ($ans -notmatch '^[nN]') {
        Copy-Item -Force (Join-Path $outDir 'gizmos.exe') $gameDir
        Copy-Item -Force (Join-Path $outDir 'gizmos-debug.bat') $gameDir
        Write-Ok "copied. Start the game with $(Join-Path $gameDir 'gizmos.exe')"
    } else {
        Write-Note "Run it with: $(Join-Path $outDir 'gizmos.exe') --game-dir `"$gameDir`""
    }
}

Write-Host ''
Write-Host 'Done. This gizmos.exe contains code generated from your copy of the game: keep it to yourself.' -ForegroundColor Green
