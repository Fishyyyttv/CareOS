#!/usr/bin/env bash
# =============================================================================
# CareOS — setup.sh
# Installs dependencies, builds the OS, and launches it in QEMU
# =============================================================================
set -e

RED='\033[0;31m'; GREEN='\033[0;32m'; CYAN='\033[0;36m'
YELLOW='\033[1;33m'; NC='\033[0m'; BOLD='\033[1m'

banner() {
    echo -e "${CYAN}"
    echo "  ╔══════════════════════════════════════╗"
    echo "  ║       CareOS Build System 1.0        ║"
    echo "  ╚══════════════════════════════════════╝${NC}"
    echo ""
}

step() { echo -e "${GREEN}[•]${NC} ${BOLD}$1${NC}"; }
warn() { echo -e "${YELLOW}[!]${NC} $1"; }
err()  { echo -e "${RED}[✗]${NC} $1"; exit 1; }
ok()   { echo -e "${GREEN}[✓]${NC} $1"; }

# ── Detect distro ──────────────────────────────────────────────────────────
detect_pkg_manager() {
    if command -v apt-get &>/dev/null; then echo "apt"
    elif command -v dnf &>/dev/null;   then echo "dnf"
    elif command -v pacman &>/dev/null; then echo "pacman"
    elif command -v zypper &>/dev/null; then echo "zypper"
    else echo "unknown"; fi
}

# ── Install dependencies ───────────────────────────────────────────────────
install_deps() {
    step "Installing build dependencies..."
    local pm; pm=$(detect_pkg_manager)

    case "$pm" in
    apt)
        sudo apt-get update -qq
        sudo apt-get install -y \
            nasm gcc binutils grub-pc-bin grub-common \
            xorriso qemu-system-x86 mtools 2>/dev/null || \
        sudo apt-get install -y \
            nasm gcc binutils grub-pc-bin grub-common \
            xorriso qemu-system-i386 mtools
        ;;
    dnf)
        sudo dnf install -y \
            nasm gcc binutils grub2-tools-extra \
            xorriso qemu-system-x86
        ;;
    pacman)
        sudo pacman -S --noconfirm \
            nasm gcc binutils grub xorriso qemu
        ;;
    zypper)
        sudo zypper install -y \
            nasm gcc binutils grub2 xorriso qemu-x86
        ;;
    *)
        warn "Unknown package manager — install manually:"
        warn "  nasm gcc binutils grub-mkrescue xorriso qemu-system-i386"
        read -rp "Continue anyway? [y/N] " yn
        [[ $yn == [Yy] ]] || exit 1
        ;;
    esac
    ok "Dependencies installed"
}

# ── Verify tools ───────────────────────────────────────────────────────────
check_tools() {
    step "Checking required tools..."
    local missing=()
    for t in nasm gcc ld xorriso; do
        command -v "$t" &>/dev/null || missing+=("$t")
    done
    # grub-mkrescue or grub2-mkrescue
    if ! command -v grub-mkrescue &>/dev/null && \
       ! command -v grub2-mkrescue &>/dev/null; then
        missing+=("grub-mkrescue")
    fi
    if [ ${#missing[@]} -ne 0 ]; then
        err "Missing tools: ${missing[*]}"
    fi
    ok "All tools present"
}

# ── Build ──────────────────────────────────────────────────────────────────
build() {
    step "Building CareOS kernel..."
    make clean 2>/dev/null || true
    make
    ok "Build complete"
}

# ── Run ────────────────────────────────────────────────────────────────────
run() {
    step "Launching CareOS in QEMU..."
    echo ""
    echo -e "${YELLOW}  Controls:${NC}"
    echo "  • Type commands in the serial console (this terminal)"
    echo "  • QEMU window shows VGA output"
    echo "  • Close QEMU window or press Ctrl+C to exit"
    echo ""

    # Prefer KVM if available
    if [ -r /dev/kvm ]; then
        warn "KVM available — using hardware acceleration"
        make run-kvm
    else
        make run
    fi
}

# ── Main ───────────────────────────────────────────────────────────────────
banner

# Parse args
SKIP_DEPS=0
BUILD_ONLY=0
for arg in "$@"; do
    case "$arg" in
        --skip-deps)  SKIP_DEPS=1 ;;
        --build-only) BUILD_ONLY=1 ;;
        --help|-h)
            echo "Usage: $0 [--skip-deps] [--build-only]"
            echo "  --skip-deps   Don't install dependencies"
            echo "  --build-only  Build ISO but don't launch QEMU"
            exit 0
            ;;
    esac
done

[ $SKIP_DEPS -eq 0 ] && install_deps
check_tools
build

if [ $BUILD_ONLY -eq 0 ]; then
    run
else
    step "ISO ready: careos.iso"
    ok "Run with: make run"
fi
