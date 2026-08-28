#!/usr/bin/env python3
"""Merge a PWAD's lumps into an IWAD (simple append-merge).

GBADoom expects custom lumps (STGANUM0-9, PLAYPAL1-5, CREDITS, M_ARUN, M_GAMMA)
that live in the PWAD shipped at GBADoom/GbaWadUtil/gbadoom.wad.

Usage:
    python3 merge_pwad.py <IWAD> <PWAD> <output.wad>

Example:
    python3 tools/merge_pwad.py DOOM1.WAD D:/2.Project/GBADoom/GbaWadUtil/gbadoom.wad DOOM1_GBA.WAD
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path


def read_wad(path: Path):
    data = bytearray(path.read_bytes())
    magic, numlumps, dirofs = struct.unpack_from("<4sII", data, 0)
    # Uppercase every lump name in the directory. Stock DOOM1.WAD has a
    # known defect: PNAMES entry 162 is lowercase "w94_1". GBADoom's
    # FindLumpByName compares names byte-exact (no case folding), so the
    # lowercase entry can never match the W94_1 lump and the engine aborts.
    for i in range(numlumps):
        off = dirofs + i * 16 + 8
        data[off:off + 8] = bytes(data[off:off + 8]).upper()
    entries = []
    for i in range(numlumps):
        off = dirofs + i * 16
        lump_ofs, lump_size = struct.unpack_from("<II", data, off)
        name = data[off + 8 : off + 16].split(b"\x00")[0]
        entries.append((lump_ofs, lump_size, name))

    # Also fix the name table INSIDE the PNAMES lump (the real bug site):
    # R_LoadTexture reads patch names straight from PNAMES data with no
    # case folding, so the lowercase "w94_1" entry must be uppercased.
    for ofs, size, name in reversed(entries):
        if name == b"PNAMES" and size >= 4:
            count = struct.unpack_from("<I", data, ofs)[0]
            for j in range(count):
                noff = ofs + 4 + j * 8
                data[noff:noff + 8] = bytes(data[noff:noff + 8]).upper()
            break

    return magic, bytes(data), entries


def main() -> int:
    if len(sys.argv) != 4:
        print(__doc__, file=sys.stderr)
        return 2

    iwad_path, pwad_path, out_path = (Path(a) for a in sys.argv[1:4])
    iwad_magic, iwad_data, iwad_entries = read_wad(iwad_path)
    pwad_magic, pwad_data, pwad_entries = read_wad(pwad_path)

    if iwad_magic != b"IWAD":
        print(f"ERROR: {iwad_path} is not an IWAD", file=sys.stderr)
        return 1
    if pwad_magic != b"PWAD":
        print(f"ERROR: {pwad_path} is not a PWAD", file=sys.stderr)
        return 1

    # Layout: [IWAD file][PWAD file][merged directory]
    # All original offsets stay valid; PWAD entries are shifted by len(iwad).
    pwad_shift = len(iwad_data)
    dir_offset = pwad_shift + len(pwad_data)
    total_lumps = len(iwad_entries) + len(pwad_entries)

    directory = bytearray()
    for ofs, size, name in iwad_entries:
        directory += struct.pack("<II", ofs, size) + name.ljust(8, b"\x00")
    for ofs, size, name in pwad_entries:
        directory += struct.pack("<II", ofs + pwad_shift, size) + name.ljust(8, b"\x00")

    header = struct.pack("<4sII", b"IWAD", total_lumps, dir_offset)
    merged = header + iwad_data[12:] + pwad_data + bytes(directory)

    out_path.write_bytes(merged)
    print(f"Merged {len(iwad_entries)} IWAD lumps + {len(pwad_entries)} PWAD lumps")
    print(f"Output: {out_path} ({len(merged)} bytes, dir at 0x{dir_offset:x})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
