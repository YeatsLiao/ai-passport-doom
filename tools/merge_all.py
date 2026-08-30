#!/usr/bin/env python3
"""Merge bootloader + partition table + app + WAD into a single flash image."""
import sys
from pathlib import Path

build = Path("build")
boot = (build / "bootloader" / "bootloader.bin").read_bytes()
pt   = (build / "partition_table" / "partition-table.bin").read_bytes()
app  = (build / "ai_passport_doom.bin").read_bytes()
wad  = Path("DOOM1_PROCESSED.WAD").read_bytes()

print(f"bootloader:      {len(boot):>8} bytes")
print(f"partition-table: {len(pt):>8} bytes")
print(f"app:             {len(app):>8} bytes")
print(f"WAD:             {len(wad):>8} bytes")

# Firmware only (pad to WAD offset)
fw_padded = boot + b"\xff" * (0x8000 - len(boot))
fw_padded += pt + b"\xff" * (0x10000 - 0x8000 - len(pt))
fw_padded += app
fw_path = build / "ai-passport-doom-firmware.bin"
fw_path.write_bytes(fw_padded)
print(f"\nFirmware: {fw_path} ({len(fw_padded)} bytes)")

# Full image (firmware + WAD)
end = 0x310000 + len(wad)
img = bytearray(fw_padded) + b"\xff" * (0x310000 - len(fw_padded)) + wad
full_path = build / "ai-passport-doom-full.bin"
full_path.write_bytes(bytes(img[:end]))
print(f"Full image: {full_path} ({end} bytes, {end/1024/1024:.1f} MB)")
