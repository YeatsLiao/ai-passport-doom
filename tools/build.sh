#!/usr/bin/env bash
set -euo pipefail

# build.sh - Build firmware, merge with WAD, verify
# Usage: ./tools/build.sh [--firmware-only]
#
# Outputs:
#   build/ai-passport-doom-firmware.bin  (firmware only, ~1-2MB)
#   build/ai-passport-doom-full.bin      (firmware + WAD, ~6MB, optional)

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
mode="${1:---full}"

if ! command -v idf.py >/dev/null 2>&1; then
    echo "ERROR: idf.py not found. Activate ESP-IDF 5.5.3 first." >&2
    echo "  Windows: open 'ESP-IDF 5.5.3 CMD' terminal" >&2
    echo "  Linux:   source \$IDF_PATH/export.sh" >&2
    exit 1
fi

cd "${repo_root}"

echo "=== Step 1: Build firmware ==="
idf.py build

echo "=== Step 2: Merge firmware (bootloader + partition table + app) ==="
idf.py merge-bin -o build/ai-passport-doom-firmware.bin

echo "=== Step 3: Verify firmware layout ==="
python3 tools/verify_firmware.py build

if [[ "${mode}" == "--firmware-only" ]]; then
    echo ""
    echo "=== DONE ==="
    echo "Firmware: build/ai-passport-doom-firmware.bin"
    echo "Flash:    esptool.py -p <port> -b 460800 write_flash 0x0 build/ai-passport-doom-firmware.bin"
    exit 0
fi

echo "=== Step 4: Merge WAD into full image ==="
WAD_FILE="${repo_root}/DOOM1.WAD"
if [[ ! -f "${WAD_FILE}" ]]; then
    echo "WARNING: DOOM1.WAD not found at ${WAD_FILE}" >&2
    echo "Skipping WAD merge. Use --firmware-only or place DOOM1.WAD in project root." >&2
    echo ""
    echo "=== DONE (firmware only) ==="
    echo "Firmware: build/ai-passport-doom-firmware.bin"
    exit 0
fi

python3 tools/merge_wad.py \
    build/ai-passport-doom-firmware.bin \
    "${WAD_FILE}" \
    build/ai-passport-doom-full.bin \
    0x310000

FULL_SIZE=$(stat -c%s "build/ai-passport-doom-full.bin" 2>/dev/null || stat -f%z "build/ai-passport-doom-full.bin" 2>/dev/null)
MAX_SIZE=$((8 * 1024 * 1024))

if [[ "${FULL_SIZE}" -gt "${MAX_SIZE}" ]]; then
    echo "ERROR: Full image (${FULL_SIZE} bytes) exceeds 8MB Flash!" >&2
    exit 1
fi

echo ""
echo "=== DONE ==="
echo "Firmware only:  build/ai-passport-doom-firmware.bin"
echo "Full (with WAD): build/ai-passport-doom-full.bin (${FULL_SIZE} bytes)"
echo ""
echo "Flash firmware only:"
echo "  esptool.py -p <port> -b 460800 write_flash 0x0 build/ai-passport-doom-firmware.bin"
echo ""
echo "Flash full image (one-shot, includes WAD):"
echo "  esptool.py -p <port> -b 460800 write_flash 0x0 build/ai-passport-doom-full.bin"
