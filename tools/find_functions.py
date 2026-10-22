#!/usr/bin/env python3
"""find_functions.py - function map of the flattened Gizmos & Gadgets EXE.

Usage: python tools/find_functions.py [work/ssg_flat.exe] [original/ssg.exe]

Merges two sources of function starts, clipped to the real code ranges:
  * data/ghidra_function_starts.txt  (Ghidra auto-analysis of ssg_flat.exe)
  * pcrecomp's prologue-driven analyzer (tools/disasm/analyze.py)
and adds every overlay stub entry (a 5-byte JMP FAR after unvroomm.py) as its
own function, since resident code reaches overlays only through them.

Code ranges come from the original EXE's VROOMM segment table:
  resident code   [0, first non-code segment)   segments with flags 1
  overlay stubs   35 x {0x20 header, 5-byte entries}
  overlay code    [first appended segment, end of image)   (from ssg_flat.map)
DGROUP, the overlay manager's data and the stack sit between them.

Writes work/functions.json in the shape lift.py (forked from DinoPark's
lift_full.py) reads: file offsets (image offset + header size).
"""
import json
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
from pcrecomp_path import pcrecomp_home  # noqa: E402

sys.path.insert(0, os.path.join(pcrecomp_home(), "tools", "disasm"))
from analyze import Analyzer  # noqa: E402


def vroomm_layout(orig):
    d = open(orig, "rb").read()
    cblp, cp, _, cpar = struct.unpack_from("<4H", d, 2)
    hdr = cpar * 16
    end = cp * 512 - ((512 - cblp) if cblp else 0)
    img = d[hdr:end]
    _, segtab_f, n = struct.unpack_from("<IIH", d, end + 4)
    tab = [struct.unpack_from("<4H", img, segtab_f - hdr + 8 * i) for i in range(n)]
    stubs = sorted({s for s, _, fl, _ in tab if fl == 3})
    data_segs = sorted({s for s, _, fl, _ in tab if fl in (0, 4)})
    res_code = sorted({s for s, _, fl, _ in tab if fl == 1 and s not in stubs})
    res_end = min(s for s in data_segs if s > res_code[-1]) * 16
    vroomm_layout.res_code = res_code
    entries = []
    for s in stubs:
        ne = struct.unpack_from("<H", img, s * 16 + 12)[0]
        entries += [s * 16 + 0x20 + 5 * k for k in range(ne)]
    return res_end, stubs, entries


def load_map(map_path):
    """stub segment -> (new segment, code size), from unvroomm.py's map."""
    m = {}
    for line in open(map_path).read().splitlines()[1:]:
        f = line.split()
        if len(f) >= 3 and int(f[2], 16):
            m[int(f[0], 16)] = (int(f[1], 16), int(f[2], 16))
    return m


def overlay_range(ovmap, img_size):
    return min(ns for ns, _ in ovmap.values()) * 16, img_size


def main():
    flat = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "work", "ssg_flat.exe")
    orig = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, "original", "ssg.exe")
    data = open(flat, "rb").read()
    hdr = struct.unpack_from("<H", data, 8)[0] * 16
    img_size = len(data) - hdr

    res_end, stubs, stub_entries = vroomm_layout(orig)
    ovmap = load_map(os.path.splitext(flat)[0] + ".map")
    ovl_lo, ovl_hi = overlay_range(ovmap, img_size)
    ranges = [(0, res_end), (ovl_lo, ovl_hi)]
    in_code = lambda o: any(lo <= o < hi for lo, hi in ranges)

    seeds = set()
    for line in open(os.path.join(ROOT, "data", "ghidra_function_starts.txt")):
        line = line.split("#")[0].strip()
        if not line:
            continue
        kind, where = line.split()
        if kind == "R":
            seeds.add(int(where, 16))
        else:
            stub, off = (int(x, 16) for x in where.split(":"))
            seeds.add(ovmap[stub][0] * 16 + off)
    n_ghidra = len(seeds)

    az = Analyzer(data)
    az.detect_all_functions()
    fs = az.functions.values() if isinstance(az.functions, dict) else az.functions
    pc = {f.start - hdr for f in fs}
    seeds |= pc
    # Every overlay stub entry jumps to a function entry; the VROOMM stub
    # table names all of them. Nothing else may reach some of these (the
    # warehouse's building-entry code is one), so neither Ghidra nor the
    # prologue scan is guaranteed to find them.
    ovl_entries = set()
    for line in open(os.path.splitext(flat)[0] + ".map").read().splitlines()[1:]:
        f = line.split()
        for ent in f[4:]:
            seg_off = ent.split("->")[1]
            sg, of = (int(x, 16) for x in seg_off.split(":"))
            ovl_entries.add(sg * 16 + of)
    n_new_entries = len(ovl_entries - seeds)
    seeds |= ovl_entries
    # Each overlay's code starts with a function (the segment start is never
    # mid-routine), and `inc bp / push bp / mov bp, sp` is the far-frame
    # prologue of overlaid code -- it occurs nowhere but at an entry. Both are
    # missed when a jump table just before them throws the prologue scan and
    # the lifter's instruction-boundary check out of step; the blueprint's
    # set-up routine (overlay 0E30 + 13EB) was one, so its call became an empty
    # stub and the blueprint drew from an all-zero state block.
    img = data[hdr:]
    n_extra = 0
    for stub, (ns, size) in ovmap.items():
        if size and ns * 16 not in seeds:
            seeds.add(ns * 16)
            n_extra += 1
    for lo, hi in ranges:
        k = img.find(b"\x45\x55\x8b\xec", lo, hi)
        while k >= 0:
            if k not in seeds and k + 1 not in seeds:
                seeds.add(k)
                n_extra += 1
            k = img.find(b"\x45\x55\x8b\xec", k + 1, hi)
    print(f"  entries added: {n_extra} (overlay segment starts, inc-bp prologues)")
    seeds = {s for s in seeds if in_code(s)}


    # Borland's far-frame convention (used for overlaid modules): `inc bp`
    # one byte before `push bp; mov bp, sp`, undone by `dec bp` before the
    # return. Callers enter at the `inc bp`; prologue detection finds the push.
    img = data[hdr:]
    n_incbp = 0
    for s0 in sorted(seeds):
        if s0 > 0 and img[s0 - 1] == 0x45 and img[s0:s0 + 3] == b"\x55\x8b\xec" and (s0 - 1) not in seeds:
            seeds.discard(s0)
            seeds.add(s0 - 1)
            n_incbp += 1

    funcs = []
    for lo, hi in ranges:
        pts = sorted(s for s in seeds if lo <= s < hi)
        for i, s in enumerate(pts):
            e = pts[i + 1] if i + 1 < len(pts) else hi
            funcs.append({"start": s, "end": e})
    for e in stub_entries:
        funcs.append({"start": e, "end": e + 5, "stub": True})

    out = [{"name": f"fn_{f['start']:05X}", "start": f["start"] + hdr, "end": f["end"] + hdr,
            "size": f["end"] - f["start"], "stub": f.get("stub", False)} for f in sorted(funcs, key=lambda f: f["start"])]
    os.makedirs(os.path.join(ROOT, "work"), exist_ok=True)
    # Every function's code segment, exactly: resident segments from the
    # VROOMM table, each overlay's new segment from the map, each stub its own.
    segments = sorted(set(vroomm_layout.res_code) | {ns for ns, _ in ovmap.values()} | set(stubs))
    json.dump({"exe": os.path.basename(flat), "image": {"hdr": hdr, "img_size": img_size,
               "code_ranges": ranges, "stub_segments": stubs, "code_segments": segments},
               "functions": out}, open(os.path.join(ROOT, "work", "functions.json"), "w"), indent=1)

    print(f"code ranges: resident [0,{res_end:#x})  overlays [{ovl_lo:#x},{ovl_hi:#x})")
    print(f"overlay entry points not found by Ghidra/analyzer, added: {n_new_entries}")
    print(f"inc-bp entries moved back one byte: {n_incbp}")
    print(f"seeds: {n_ghidra} from Ghidra, {len(pc)} from pcrecomp analyzer -> {len(seeds)} merged in code")
    print(f"functions: {len(out) - len(stub_entries)} + {len(stub_entries)} overlay stub entries -> work/functions.json")
    big = sorted((f for f in out if not f["stub"]), key=lambda f: -f["size"])[:5]
    print("largest:", ", ".join(f"{f['name']} {f['size']}B" for f in big))


if __name__ == "__main__":
    main()
