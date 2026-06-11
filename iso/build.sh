#!/usr/bin/env bash
# CareOS ISO build script — run inside Arch Linux (WSL2 or native)
# Usage: sudo bash iso/build.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT_DIR="${SCRIPT_DIR}/../out"
WORK_DIR="/tmp/careos-work"

if [[ $EUID -ne 0 ]]; then
    echo "Error: this script must be run as root (sudo bash iso/build.sh)"
    exit 1
fi

if ! command -v mkarchiso &>/dev/null; then
    echo "Error: archiso not found. Install it with: sudo pacman -S archiso"
    exit 1
fi

echo "==> Cleaning previous work directory..."
rm -rf "${WORK_DIR}"
mkdir -p "${OUT_DIR}"

# ── Generate GRUB theme background ───────────────────────────────────────────
echo "==> Generating GRUB theme background..."
GRUB_THEME_DIR="${SCRIPT_DIR}/grub/themes/careos"
export GRUB_THEME_DIR
mkdir -p "${GRUB_THEME_DIR}"

PYTHON_BIN="$(command -v python3 || command -v python || true)"
if [[ -n "${PYTHON_BIN}" ]]; then
"${PYTHON_BIN}" - << 'PYEOF'
import struct, zlib, os, sys

# Generate a 1920x1080 PNG: CareOS navy with blue/green/coral bands.
W, H = 1920, 1080
BG = (8, 13, 23)
BLUE = (85, 154, 255)
GREEN = (46, 204, 142)
CORAL = (245, 96, 96)

def lerp(a, b, t):
    return int(a + (b - a) * t)

def mix(c1, c2, t):
    return tuple(lerp(c1[i], c2[i], t) for i in range(3))

rows = []

for y in range(H):
    row = []
    for x in range(W):
        base = mix(BG, (15, 23, 38), (x + y) / (W + H) * 0.5)
        band_a = max(0.0, 1.0 - abs((y - 0.40 * x) - 130) / 110)
        band_b = max(0.0, 1.0 - abs((y - 0.18 * x) - 560) / 90)
        band_c = max(0.0, 1.0 - abs((y + 0.26 * x) - 1090) / 130)
        color = base
        color = mix(color, BLUE, band_a * 0.16)
        color = mix(color, GREEN, band_b * 0.10)
        color = mix(color, CORAL, band_c * 0.08)
        r, g, b = color
        row += [r, g, b]
    rows.append(bytes([0] + row))

raw = b''.join(rows)
def chunk(tag, data):
    c = zlib.crc32(tag + data) & 0xFFFFFFFF
    return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', c)

sig   = b'\x89PNG\r\n\x1a\n'
ihdr  = chunk(b'IHDR', struct.pack('>IIBBBBB', W, H, 8, 2, 0, 0, 0))
idat  = chunk(b'IDAT', zlib.compress(raw, 1))
iend  = chunk(b'IEND', b'')

out = os.path.join(os.environ.get('GRUB_THEME_DIR', '/tmp'), 'background.png')
with open(out, 'wb') as f:
    f.write(sig + ihdr + idat + iend)
print(f"  Generated {out}")
PYEOF
else
    if [[ -f "${GRUB_THEME_DIR}/background.png" ]]; then
        echo "  python not found, using existing ${GRUB_THEME_DIR}/background.png"
    else
        echo "  python not found, skipping background generation"
    fi
fi

# Copy GRUB theme to ISO grub directory
cp "${SCRIPT_DIR}/airootfs/usr/share/grub/themes/careos/theme.txt" \
   "${GRUB_THEME_DIR}/theme.txt" 2>/dev/null || true

echo "==> Building CareOS ISO..."
mkarchiso -v -w "${WORK_DIR}" -o "${OUT_DIR}" "${SCRIPT_DIR}"

echo ""
echo "==> Build complete!"
ISO_PATH="$(ls -t "${OUT_DIR}"/*.iso 2>/dev/null | head -n 1 || true)"
if [[ -z "${ISO_PATH}" ]]; then
    ISO_PATH="${OUT_DIR}/CareOS.iso"
fi
echo "    ISO: ${ISO_PATH}"
echo ""
echo "    Flash to USB:  sudo dd if=${ISO_PATH} of=/dev/sdX bs=4M status=progress"
echo "    Test in QEMU:  qemu-system-x86_64 -enable-kvm -m 4G -bios /usr/share/ovmf/x64/OVMF.fd -cdrom ${ISO_PATH}"
