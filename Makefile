CC        := gcc
LD        := ld
NASM      := nasm
QEMU      := qemu-system-x86_64

GCC_INC   := $(shell gcc -print-file-name=include 2>/dev/null)
LIBGCC    := $(shell gcc -print-libgcc-file-name 2>/dev/null)

KERN_CFLAGS := -MMD -MP -m64 -ffreestanding -fno-stack-protector -fno-pie -fno-pic \
               -nostdlib -nostdinc -isystem $(GCC_INC) -O2 -g \
               -Iinclude -Iinclude/libc -Igui -fno-builtin \
               -mno-red-zone -mno-mmx -mno-sse -mno-sse2

APP_CFLAGS  := -MMD -MP -m64 -ffreestanding -fno-stack-protector -fno-pie -fno-pic \
               -nostdlib -nostdinc -isystem $(GCC_INC) -O2 \
               -Iinclude -Iinclude/libc -Igui -fno-builtin \
               -mno-red-zone

CFLAGS      := $(KERN_CFLAGS)
DOOM_CFLAGS := $(APP_CFLAGS) -DDOOMGENERIC_RESX=640 -DDOOMGENERIC_RESY=400

DOOM_EXCLUDE := doomgeneric/doomgeneric/doomgeneric_allegro.c \
                doomgeneric/doomgeneric/doomgeneric_emscripten.c \
                doomgeneric/doomgeneric/doomgeneric_linuxvt.c \
                doomgeneric/doomgeneric/doomgeneric_sdl.c \
                doomgeneric/doomgeneric/doomgeneric_soso.c \
                doomgeneric/doomgeneric/doomgeneric_sosox.c \
                doomgeneric/doomgeneric/doomgeneric_win.c \
                doomgeneric/doomgeneric/doomgeneric_xlib.c \
                doomgeneric/doomgeneric/i_allegromusic.c \
                doomgeneric/doomgeneric/i_allegrosound.c \
                doomgeneric/doomgeneric/i_sdlmusic.c \
                doomgeneric/doomgeneric/i_sdlsound.c \
                doomgeneric/doomgeneric/i_cdmus.c \
                doomgeneric/doomgeneric/dummy.c

DOOM_DIR  := doomgeneric/doomgeneric
DOOM_SRC  := $(filter-out $(DOOM_EXCLUDE), $(wildcard doomgeneric/doomgeneric/*.c))

LDFLAGS   := -m elf_x86_64 -T kernel.ld
NASMFLAGS := -f elf64

DISK      := careos.img
DISK_MB   = 4096
# Must equal CAREOS_DISK_RESERVED_SECTORS in include/kernel.h
# (HOMEFS 96 + SETTINGS 4 + USERDB 8). If these disagree, the ext2 filesystem
# overlaps the reserved tail where homefs/settings/userdb live.
DISK_RESERVED_SECTORS := 108

# Display backend. sdl gives a predictable pointer grab (click the window to
# grab, Ctrl+Alt+G to release) and a fullscreen toggle (Ctrl+Alt+F). Override
# with e.g. `make run QEMU_DISPLAY=gtk` if your QEMU build has no SDL support.
QEMU_DISPLAY ?= sdl

# Acceleration backend for the default `run` target. Multi-threaded TCG is pure
# software emulation, so it works everywhere -- including inside a nested VM
# where hardware virtualisation is unavailable -- and, unlike the single-thread
# TCG that QEMU picks by default, it spreads the emulated CPU work across the
# -smp cores below. That single change is the biggest desktop-smoothness win on
# a machine without hardware acceleration. On a bare-metal Linux host prefer
# `make run-kvm`; on bare-metal Windows prefer `make run-whpx`.
QEMU_ACCEL ?= tcg,thread=multi

# -machine pc,usb=off   forces PS/2 mouse on IRQ12. CareOS has no USB stack, so a
#                       usb-tablet would NOT work; the PS/2 mouse is RELATIVE and
#                       only moves the cursor once QEMU has grabbed the pointer.
# -display $(QEMU_DISPLAY)  window that captures the mouse (Ctrl+Alt+G releases)
# -drive                4GB disk image so ATA driver finds a drive
# -serial stdio         boot stage logs in your terminal
# -m 4096M / -smp 4     4 GB RAM and 4 vCPUs is already generous for CareOS;
#                       the desktop's memory footprint is tens of MB, so raising
#                       these further does not reduce lag. Keep them as-is.
QEMUBASE  := -m 4096M -smp 4 -cpu max -cdrom careos.iso -no-reboot -serial stdio -vga std \
             -machine pc,usb=off -display $(QEMU_DISPLAY) \
             -drive file=$(DISK),format=raw,if=ide,index=0 \
             -netdev user,id=net0 -device e1000,netdev=net0

ASM_SRC   := boot/boot.asm

C_SRC     := kernel/kernel.c       \
             kernel/gdt_idt.c      \
             kernel/memory.c       \
             kernel/paging.c       \
             kernel/vfs.c          \
             kernel/scheduler.c    \
             kernel/syscall.c      \
             kernel/users.c        \
             kernel/settings.c     \
             kernel/power.c        \
             kernel/appdb.c        \
             kernel/carepkg.c      \
             kernel/elf.c          \
             kernel/ext2.c         \
             kernel/pipe.c         \
             kernel/care_lang.c    \
             kernel/rc_care.c      \
             drivers/vga.c         \
             drivers/timer.c       \
             drivers/keyboard.c    \
             drivers/rtc.c         \
             drivers/pci.c         \
             drivers/vesa.c        \
             drivers/speaker.c     \
             drivers/storage/ata.c \
             drivers/net/e1000.c   \
             net/net.c             \
             net/sha256.c          \
             net/aes_gcm.c         \
             net/x25519.c          \
             net/tls.c             \
             shell/shell.c         \
             gui/gfx.c             \
             gui/image.c           \
             gui/image_bmp.c       \
             gui/image_tga.c       \
             gui/resource_cache.c  \
             gui/resource_boot.c   \
             gui/icon.c            \
             gui/font.c            \
             gui/fonts/font_jetbrains_mono.c \
             gui/fonts/font_jetbrains_mono_bold.c \
             gui/fonts/font_classic.c \
             gui/fonts/font_ibm_plex_mono.c \
             gui/fonts/font_ibm_plex_mono_bold.c \
             gui/fonts/font_ibm_plex_sans.c \
             gui/fonts/font_ibm_plex_sans_bold.c \
             gui/theme.c           \
             gui/launcher.c        \
             gui/mouse.c           \
             gui/wm.c              \
             gui/taskbar.c         \
             gui/widget.c          \
             gui/widgets.c         \
             gui/gui.c             \
             apps/apps.c           \
             apps/app_terminal.c   \
             apps/app_notes.c      \
             apps/app_files.c      \
             apps/app_sysmon.c     \
             apps/app_calc.c       \
             apps/app_about.c      \
             apps/app_settings.c   \
             apps/app_browser.c    \
             apps/app_pkgmgr.c     \
             apps/app_editor.c     \
             apps/app_paint.c      \
             apps/app_clock.c      \
             apps/app_netmon.c     \
             apps/app_users.c      \
             apps/app_help.c       \
             apps/app_maze.c       \
             apps/app_3d.c         \
             apps/app_doom.c       \
             kernel/libc_shim.c

ASM_OBJ   := $(ASM_SRC:.asm=.o)
C_OBJ     := $(C_SRC:.c=.o)
DOOM_OBJ  := $(DOOM_SRC:.c=.o)

# Baked GUI assets. The icon theme is rendered offline by tools/gen-icons.py
# into one .cra archive and linked in with objcopy, exactly like DOOM1.WAD --
# /system is an in-memory VFS rebuilt every boot, so system artwork cannot live
# on disk. gui/resource_boot.c publishes it into the VFS at gui_init().
ICON_ARCHIVE := assets/careos-icons.cra

ALL_OBJ   := $(ASM_OBJ) $(C_OBJ) $(DOOM_OBJ) tests/ring3_exit.bin.o tests/ring3_isolate_a.bin.o tests/ring3_isolate_b.bin.o DOOM1.WAD.bin.o $(ICON_ARCHIVE).o

.PHONY: all run run-1080p run-kvm run-whpx run-nowindow debug clean clean-all help disk reset-disk test-elfs format-disk

all: $(DISK) careos.iso

# Create a blank disk image for ATA (use make format-disk for ext2)
$(DISK):
	@echo "  DISK  creating $(DISK_MB)MB disk image..."
	@dd if=/dev/zero of=$(DISK) bs=1M count=$(DISK_MB) 2>/dev/null
	@echo "  DISK  $(DISK) ready"

%.o: %.asm
	@echo "  NASM  $<"
	$(NASM) $(NASMFLAGS) -o $@ $<

doomgeneric/doomgeneric/%.o: doomgeneric/doomgeneric/%.c
	@echo "  CC    $< (DOOM)"
	$(CC) $(DOOM_CFLAGS) -c -o $@ $<

kernel/libc_shim.o: kernel/libc_shim.c
	@echo "  CC    $< (SHIM)"
	$(CC) $(DOOM_CFLAGS) -c -o $@ $<

%.o: %.c
	@echo "  CC    $<"
	$(CC) $(CFLAGS) -c -o $@ $<

# Header dependency tracking. Without this, editing a header (e.g. the
# fs_node_t layout in include/kernel.h) rebuilds nothing, and stale objects
# get linked against new ones with mismatched struct layouts.
DEP_FILES := $(ALL_OBJ:.o=.d)
-include $(DEP_FILES)

kernel/kernel.elf: $(ALL_OBJ)
	@echo "  LD    kernel.elf"
	$(LD) $(LDFLAGS) -o $@ $(ALL_OBJ) $(LIBGCC)
	@echo "  Size: $$(du -h $@ | cut -f1)"

careos.iso: kernel/kernel.elf
	@echo "  ISO   building..."
	@mkdir -p iso/boot/grub
	@cp kernel/kernel.elf iso/boot/kernel.elf
	@grub-mkrescue -o careos.iso iso/ 2>&1 | grep -v "^$$" || \
	 grub2-mkrescue -o careos.iso iso/ 2>&1 | grep -v "^$$"
	@echo "  ISO: $$(du -h careos.iso | cut -f1)"

run: $(DISK) careos.iso
	$(QEMU) $(QEMUBASE) -accel $(QEMU_ACCEL)

# Boots straight into fullscreen. Click once to grab the mouse; Ctrl+Alt+F
# toggles fullscreen at any time, Ctrl+Alt+G releases the pointer grab.
run-1080p run-fullscreen: $(DISK) careos.iso
	$(QEMU) $(QEMUBASE) -accel $(QEMU_ACCEL) -full-screen

# Hardware acceleration on a bare-metal Linux host (KVM). Not available inside a
# nested VM unless the outer hypervisor exposes nested virtualisation.
run-kvm: $(DISK) careos.iso
	$(QEMU) $(QEMUBASE) -accel kvm -cpu host

# Hardware acceleration on a bare-metal Windows host (Windows Hypervisor
# Platform). Enable "Windows Hypervisor Platform" in Windows Features first.
# kernel-irqchip=off is required for WHPX. Like KVM, this needs real (or nested)
# virtualisation; if it errors, fall back to the default `make run` (MTTCG).
run-whpx: $(DISK) careos.iso
	$(QEMU) $(QEMUBASE) -accel whpx,kernel-irqchip=off

# Headless: no window, serial only (mouse will not work but good for testing)
run-nowindow: $(DISK) careos.iso
	$(QEMU) -m 1024M -cdrom careos.iso -nographic -no-reboot \
	        -machine pc,usb=off \
	        -drive file=$(DISK),format=raw,if=ide,index=0 \
	        -netdev user,id=net0 -device e1000,netdev=net0

debug: $(DISK) careos.iso
	$(QEMU) $(QEMUBASE) -s -S &
	gdb -ex "target remote :1234" \
	    -ex "symbol-file kernel/kernel.elf" \
	    -ex "break kernel_main" \
	    -ex "continue"

format-disk:
	@dd if=/dev/zero of=$(DISK) bs=1M count=$(DISK_MB) status=progress
	@blocks=$$(( $(DISK_MB) * 1024 - ($(DISK_RESERVED_SECTORS) / 2) )); \
	mkfs.ext2 -F -O ^resize_inode -b 1024 -I 128 -L "CareOS" $(DISK) $$blocks
	@echo "Disk formatted: $(DISK_MB)MB ext2"

reset-disk: format-disk

tests/ring3_exit.o: tests/ring3_exit.asm
	nasm -f elf64 -o tests/ring3_exit.o tests/ring3_exit.asm

tests/ring3_exit: tests/ring3_exit.o
	ld -m elf_x86_64 -Ttext 0x8000000000 -o tests/ring3_exit tests/ring3_exit.o

tests/ring3_fault.o: tests/ring3_fault.asm
	nasm -f elf64 -o tests/ring3_fault.o tests/ring3_fault.asm

tests/ring3_fault: tests/ring3_fault.o
	ld -m elf_x86_64 -Ttext 0x8000000000 -o tests/ring3_fault tests/ring3_fault.o

# Convert ring3_exit ELF to a linkable object so it can be embedded in the kernel
tests/ring3_exit.bin.o: tests/ring3_exit
	objcopy -I binary -O elf64-x86-64 -B i386:x86-64 tests/ring3_exit tests/ring3_exit.bin.o

tests/ring3_isolate_a.o: tests/ring3_isolate_a.asm
	nasm -f elf64 -o tests/ring3_isolate_a.o tests/ring3_isolate_a.asm

tests/ring3_isolate_a: tests/ring3_isolate_a.o
	ld -m elf_x86_64 -Ttext 0x8000000000 -o tests/ring3_isolate_a tests/ring3_isolate_a.o

tests/ring3_isolate_a.bin.o: tests/ring3_isolate_a
	objcopy -I binary -O elf64-x86-64 -B i386:x86-64 tests/ring3_isolate_a tests/ring3_isolate_a.bin.o

tests/ring3_isolate_b.o: tests/ring3_isolate_b.asm
	nasm -f elf64 -o tests/ring3_isolate_b.o tests/ring3_isolate_b.asm

tests/ring3_isolate_b: tests/ring3_isolate_b.o
	ld -m elf_x86_64 -Ttext 0x8000000000 -o tests/ring3_isolate_b tests/ring3_isolate_b.o

tests/ring3_isolate_b.bin.o: tests/ring3_isolate_b
	objcopy -I binary -O elf64-x86-64 -B i386:x86-64 tests/ring3_isolate_b tests/ring3_isolate_b.bin.o

# Embed the baked icon/wallpaper archive into the kernel binary.
#
# objcopy derives its symbol names from the path as written on the command line,
# so assets/careos-icons.cra becomes _binary_assets_careos_icons_cra_start --
# which is what gui/resource_boot.c declares. Moving or renaming the archive
# means renaming those externs too.
#
# Two objcopy flags beyond what DOOM1.WAD needs, because this blob is bigger
# and never written to:
#   --rename-section  moves it from .data (objcopy's default for -I binary)
#                     into .rodata, where kernel.ld already has a slot. The
#                     archive is mounted read-only and borrowed in place by
#                     image_decode_cri(), so nothing may write to it.
#   --add-section     supplies the empty .note.GNU-stack that modern ld wants;
#                     without it the link warns that the stack is executable.
$(ICON_ARCHIVE).o: $(ICON_ARCHIVE)
	@echo "  EMBED $(ICON_ARCHIVE) ($$(du -h $(ICON_ARCHIVE) | cut -f1))"
	objcopy -I binary -O elf64-x86-64 -B i386:x86-64 \
	        --rename-section .data=.rodata,alloc,load,readonly,data,contents \
	        --add-section .note.GNU-stack=/dev/null \
	        $(ICON_ARCHIVE) $@

# A checkout with no baked theme still has to link. This writes a valid, empty
# archive (16-byte header, zero entries); resource_boot.c sees the size and
# leaves the desktop on its vector glyphs. Run `make icons` to build the real
# one -- and note this rule does NOT overwrite an existing archive, because the
# real one is a committed build artefact.
$(ICON_ARCHIVE):
	@mkdir -p $(dir $@)
	@printf 'CRA1\000\000\000\000\020\000\000\000\000\000\000\000' > $@
	@echo "  STUB  $@ (empty icon theme -- run 'make icons' to bake the real one)"

# Bake the icon theme from upstream SVG sources. Needs a Python with Pillow and
# an SVG renderer; see tools/gen-icons.py --help and docs/graphics-assets.md.
# Offline step, like tools/gen-font.py: run it, commit the result.
#
#   make icons ICON_SOURCES="../papirus-icon-theme ../Tela-icon-theme"
#   make icons ICON_SOURCES=../papirus-icon-theme ICON_WALLPAPER=art/bg.png
PYTHON        ?= python3
ICON_SOURCES  ?=
ICON_WALLPAPER ?=

.PHONY: icons
icons:
	@test -n "$(ICON_SOURCES)$(ICON_WALLPAPER)" || { \
	  echo "usage: make icons ICON_SOURCES=\"<theme-dir> [<theme-dir>...]\" [ICON_WALLPAPER=<image>]"; \
	  echo "see docs/graphics-assets.md for where to get the themes"; exit 1; }
	$(PYTHON) tools/gen-icons.py \
	    $(foreach s,$(ICON_SOURCES),--source $(s)) \
	    $(if $(ICON_WALLPAPER),--wallpaper $(ICON_WALLPAPER),) \
	    --out $(ICON_ARCHIVE)
	@$(MAKE) --no-print-directory $(ICON_ARCHIVE).o

# Embed DOOM1.WAD directly into the kernel binary
DOOM1.WAD.bin.o: DOOM1.WAD
	@echo "  EMBED DOOM1.WAD ($$(du -h DOOM1.WAD | cut -f1))"
	objcopy -I binary -O elf64-x86-64 -B i386:x86-64 DOOM1.WAD DOOM1.WAD.bin.o

test-elfs: tests/ring3_exit tests/ring3_fault tests/ring3_isolate_a tests/ring3_isolate_b

clean:
	@rm -f $(ALL_OBJ) $(ALL_OBJ:.o=.d) kernel/kernel.elf careos.iso DOOM1.WAD.bin.o
	@rm -f tests/ring3_exit.o tests/ring3_exit tests/ring3_fault.o tests/ring3_fault tests/ring3_exit.bin.o
	@rm -f tests/ring3_isolate_a.o tests/ring3_isolate_a tests/ring3_isolate_a.bin.o
	@rm -f tests/ring3_isolate_b.o tests/ring3_isolate_b tests/ring3_isolate_b.bin.o
	@echo "  Cleaned (kept $(DISK) for persistent users/data)"

clean-all: clean reset-disk
	@echo "  Full clean complete"

help:
	@echo "  make              build disk + ISO"
	@echo "  make run          run in QEMU (multi-threaded TCG, SDL, PS/2 mouse)"
	@echo "  make run-1080p    run fullscreen (alias: run-fullscreen)"
	@echo "  make run-kvm      run with KVM acceleration (bare-metal Linux host)"
	@echo "  make run-whpx     run with WHPX acceleration (bare-metal Windows host)"
	@echo "  make run-nowindow headless serial-only"
	@echo "  make debug        run with GDB"
	@echo "  make clean        remove build artifacts (keeps disk/user data)"
	@echo "  make reset-disk   recreate blank disk image (wipes users/data)"
	@echo "  make clean-all    clean + reset disk"
	@echo ""
	@echo "  Mouse: click QEMU window to capture, Ctrl+Alt+G to release"
	@echo "  Fullscreen: Ctrl+Alt+F toggles it any time (or use make run-1080p)"
	@echo "  Display: override backend with make run QEMU_DISPLAY=gtk"



