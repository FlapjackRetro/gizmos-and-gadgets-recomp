#!/usr/bin/env python3
"""build.py - generate src/gen from your own original/ssg.exe.

  1. tools/unvroomm.py      flatten the VROOMM overlays (overlays at C000:0000)
  2. tools/find_functions.py  function map (Ghidra seeds + pcrecomp analyzer)
  3. tools/lift.py          lift every function to C (pcrecomp lift16)
  4. tools/embed_image.py   embed the load image (initial data) as C

Then build with CMake. Requires pcrecomp next to this repo or PCRECOMP_HOME.
"""
import hashlib
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXPECTED_SHA256 = "79fa7b8d5b1e5d7be04f9f7a455a0f04d0823d41c6cbbdc6a8bd90033ba532f9"   # ssg.exe v1.0


def run(*args):
    print("+", " ".join(args), flush=True)
    subprocess.run([sys.executable, *args], cwd=ROOT, check=True)


def main():
    orig = os.path.join(ROOT, "original", "ssg.exe")
    if not os.path.exists(orig):
        sys.exit("Put the game's ssg.exe (DOS v1.0) in original/ first.")
    h = hashlib.sha256(open(orig, "rb").read()).hexdigest()
    if h != EXPECTED_SHA256:
        print(f"warning: ssg.exe sha256 {h} is not the known v1.0 build; continuing anyway")
    os.makedirs(os.path.join(ROOT, "work"), exist_ok=True)
    run("tools/unvroomm.py", "original/ssg.exe", "work/ssg_flat.exe", "--overlay-seg", "0xC000")
    run("tools/find_functions.py", "work/ssg_flat.exe", "original/ssg.exe")
    run("tools/lift.py")
    run("tools/embed_image.py", "work/ssg_flat.exe")
    print("done: now configure and build with CMake")


if __name__ == "__main__":
    main()
