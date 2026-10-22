#!/usr/bin/env python3
"""
unvroomm.py - flatten a Borland VROOMM-overlaid DOS EXE for static analysis.

Each overlay's code is appended to the MZ load image as an ordinary segment,
its overlay fixups (segment-table selectors) are resolved to real segments and
become MZ relocations, and every INT 3Fh stub entry
(CD 3F lo hi 00) is rewritten into a far jump (EA lo hi seg seg) to the real
code. The result is meant for Ghidra/IDA, not for running.

Usage: python3 unvroomm.py ssg.exe ssg_flat.exe [--overlay-seg 0x8000]
Writes ssg_flat.exe plus ssg_flat.map (stub -> new segment table).

By default the overlays are packed right after the load image, which is the
compact layout for Ghidra. The recompilation passes --overlay-seg: the original
program's stack and far heap grow upward from the end of its load image, so the
overlay code has to sit above everything DOS will hand out.
"""
import struct
import sys

STUB_ENTRY_BASE = 0x20  # entries start after the 32-byte stub header
STUB_ENTRY_SIZE = 5


def u16(b, o):
    return struct.unpack_from("<H", b, o)[0]


def u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


def main(src, dst, overlay_seg=None):
    data = open(src, "rb").read()
    if data[:2] not in (b"MZ", b"ZM"):
        sys.exit("not an MZ executable")

    cblp, cp, crlc, cparhdr = struct.unpack_from("<4H", data, 2)
    minalloc, maxalloc, ss, sp, csum, ip, cs, lfarlc, ovno = struct.unpack_from("<9H", data, 0x0A)

    hdr_size = cparhdr * 16
    load_end = cp * 512 - ((512 - cblp) if cblp else 0)
    image = bytearray(data[hdr_size:load_end])
    relocs = [struct.unpack_from("<HH", data, lfarlc + 4 * i) for i in range(crlc)]
    relocs = [(seg, off) for off, seg in relocs]  # stored as off,seg

    if data[load_end:load_end + 4] != b"FBOV":
        sys.exit(f"no FBOV overlay header at file offset {load_end:#x}")
    ovr_size, segtab_foff, nseg = struct.unpack_from("<IIH", data, load_end + 4)
    ovr_base = load_end + 16
    segtab = segtab_foff - hdr_size  # convert file offset to image offset
    # Selector (index * 8) -> segment paragraph, as the original table holds it.
    segtable = [u16(image, segtab + 8 * i) for i in range(nseg)]
    print(f"load image {len(image):#x} bytes, overlay data {ovr_size:#x} bytes, "
          f"{nseg} segment-table entries at image {segtab:#x}")

    # Leave a gap after the original image (stack lives at its end).
    def pad_to_para(extra=0):
        image.extend(b"\0" * extra)
        image.extend(b"\0" * (-len(image) % 16))

    if overlay_seg is None:
        pad_to_para(0x100)
    else:
        if overlay_seg * 16 < len(image):
            sys.exit(f"--overlay-seg {overlay_seg:#x} is inside the load image")
        image.extend(b"\0" * (overlay_seg * 16 - len(image)))

    map_lines = ["stub_seg  new_seg  code_size  relocs  entries (stub_off -> new_seg:off)"]
    seen = set()
    nstubs = 0
    for i in range(nseg):
        seg, maxoff, flags, minoff = struct.unpack_from("<4H", image, segtab + 8 * i)
        lin = seg * 16
        if seg in seen or image[lin:lin + 2] != b"\xcd\x3f":
            continue
        seen.add(seg)
        fileoff = u32(image, lin + 4)
        codesize = u16(image, lin + 8)
        relsize = u16(image, lin + 10)
        nentries = u16(image, lin + 12)

        start = ovr_base + fileoff
        code = data[start:start + codesize]
        fix = data[start + codesize:start + codesize + relsize]
        if len(code) != codesize or len(fix) != relsize:
            sys.exit(f"overlay for stub {seg:#06x} runs past end of file")

        new_seg = len(image) // 16
        if new_seg > 0xFFFF:
            sys.exit("flattened image exceeds 1 MB")
        # Overlay fixups hold *selectors* (index * 8 into the segment table), not
        # paragraphs: the overlay manager swaps in the real segment at load time.
        # Do the same here, then let the MZ loader add the load base as usual.
        code = bytearray(code)
        for j in range(relsize // 2):
            off = u16(fix, 2 * j)
            sel = u16(code, off)
            if sel % 8 or sel // 8 >= nseg:
                sys.exit(f"overlay {seg:#06x} fixup at {off:#06x} holds {sel:#06x}, not a selector")
            struct.pack_into("<H", code, off, segtable[sel // 8])
            relocs.append((new_seg, off))
        image.extend(code)
        pad_to_para()

        entries = []
        for e in range(nentries):
            eo = STUB_ENTRY_BASE + STUB_ENTRY_SIZE * e
            p = lin + eo
            if image[p:p + 2] != b"\xcd\x3f":
                sys.exit(f"stub {seg:#06x} entry {e} is not INT 3Fh")
            target = u16(image, p + 2)
            # JMP FAR new_seg:target, and relocate its segment word
            image[p] = 0xEA
            struct.pack_into("<HH", image, p + 1, target, new_seg)
            relocs.append((seg, eo + 3))
            entries.append(f"{eo:04x}->{new_seg:04x}:{target:04x}")

        map_lines.append(f"{seg:04x}      {new_seg:04x}     {codesize:04x}       "
                         f"{relsize // 2:<6}  " + " ".join(entries))
        nstubs += 1

    # Build the new MZ header.
    new_crlc = len(relocs)
    new_lfarlc = 0x1C
    hdr_len = new_lfarlc + 4 * new_crlc
    hdr_len += -hdr_len % 512
    total = hdr_len + len(image)
    new_cp = (total + 511) // 512
    new_cblp = total % 512

    hdr = bytearray(hdr_len)
    struct.pack_into("<2s4H9H", hdr, 0, b"MZ", new_cblp, new_cp, new_crlc, hdr_len // 16,
                     minalloc, maxalloc, ss, sp, 0, ip, cs, new_lfarlc, 0)
    for k, (seg, off) in enumerate(relocs):
        struct.pack_into("<HH", hdr, new_lfarlc + 4 * k, off, seg)

    with open(dst, "wb") as f:
        f.write(hdr)
        f.write(image)
    map_path = dst.rsplit(".", 1)[0] + ".map"
    with open(map_path, "w") as f:
        f.write("\n".join(map_lines) + "\n")

    print(f"flattened {nstubs} overlays, {new_crlc} relocations ({crlc} original)")
    print(f"wrote {dst} ({total:#x} bytes) and {map_path}")


if __name__ == "__main__":
    args = sys.argv[1:]
    ovl = None
    if "--overlay-seg" in args:
        i = args.index("--overlay-seg")
        ovl = int(args[i + 1], 0)
        del args[i:i + 2]
    if len(args) != 2:
        sys.exit(__doc__)
    main(args[0], args[1], ovl)
