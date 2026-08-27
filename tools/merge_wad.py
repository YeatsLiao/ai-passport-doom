#!/usr/bin/env python3
"""Merge WAD data into the firmware image at a specified flash offset.

Usage:
    python3 merge_wad.py <firmware.bin> <DOOM1.WAD> <output.bin> <wad_offset_hex>

Example:
    python3 merge_wad.py build/ai-passport-doom-firmware.bin DOOM1.WAD build/ai-passport-doom-full.bin 0x310000
"""

from __future__ import annotations

import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 5:
        print(__doc__, file=sys.stderr)
        return 2

    fw_path = Path(sys.argv[1])
    wad_path = Path(sys.argv[2])
    out_path = Path(sys.argv[3])
    wad_offset = int(sys.argv[4], 16)

    if not fw_path.is_file():
        print(f"ERROR: firmware file not found: {fw_path}", file=sys.stderr)
        return 1
    if not wad_path.is_file():
        print(f"ERROR: WAD file not found: {wad_path}", file=sys.stderr)
        return 1

    fw_data = fw_path.read_bytes()
    wad_data = wad_path.read_bytes()

    # Validate WAD header
    if wad_data[:4] != b"IWAD":
        print(f"ERROR: WAD file does not start with 'IWAD' header", file=sys.stderr)
        return 1

    # Build the full image: pad firmware to wad_offset, then append WAD
    if len(fw_data) > wad_offset:
        print(f"ERROR: firmware ({len(fw_data)} bytes) exceeds WAD offset (0x{wad_offset:x})", file=sys.stderr)
        return 1

    padding = b"\xff" * (wad_offset - len(fw_data))
    full_data = fw_data + padding + wad_data

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(full_data)

    print(f"Merged: {len(fw_data)} bytes firmware + {len(padding)} bytes padding + {len(wad_data)} bytes WAD")
    print(f"Output: {out_path} ({len(full_data)} bytes, WAD at 0x{wad_offset:x})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
