CC        := gcc
LD        := ld
NASM      := nasm
QEMU      := qemu-system-x86_64

GCC_INC   := $(shell gcc -print-file-name=include 2>/dev/null)
LIBGCC    := $(shell gcc -print-libgcc-file-name 2>/dev/null)

KERN_CFLAGS := -m64 -ffreestanding -fno-stack-protector -fno-pie -fno-pic \
               -nostdlib -nostdinc -isystem $(GCC_INC) -O2 \
               -Iinclude -Iinclude/libc -Igui -fno-builtin \
               -mno-red-zone -mno-mmx -mno-sse -mno-sse2

APP_CFLAGS  := -m64 -ffreestanding -fno-stack-protector -fno-pie -fno-pic \
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
DISK_RESERVED_SECTORS := 104

# -machine pc,usb=off   forces PS/2 mouse on IRQ12 (no USB tablet)
# -drive                4GB disk image so ATA driver finds a drive
# -display sdl          proper window that captures mouse (Ctrl+Alt+G to release)
# -serial stdio         boot stage logs in your terminal
QEMUBASE  := -m 4096M -smp 4 -cpu max -cdrom careos.iso -no-reboot -serial stdio -vga std \
             -machine pc,usb=off \
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
             kernel/carepkg.c      \
             kernel/elf.c          \
             kernel/ext2.c         \
             kernel/pipe.c         \
             kernel/care_lang.c    \
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
             gui/theme.c           \
             gui/launcher.c        \
             gui/mouse.c           \
             gui/wm.c              \
             gui/taskbar.c         \
             gui/widget.c          \
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
ALL_OBJ   := $(ASM_OBJ) $(C_OBJ) $(DOOM_OBJ) tests/ring3_exit.bin.o DOOM1.WAD.bin.o

.PHONY: all run run-1080p run-kvm run-nowindow debug clean clean-all help disk reset-disk test-elfs format-disk

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
	$(QEMU) $(QEMUBASE)

run-1080p: $(DISK) careos.iso
	$(QEMU) $(QEMUBASE) -full-screen

run-kvm: $(DISK) careos.iso
	$(QEMU) $(QEMUBASE) -enable-kvm -cpu host

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
	ld -m elf_x86_64 -Ttext 0x400000 -o tests/ring3_exit tests/ring3_exit.o

tests/ring3_fault.o: tests/ring3_fault.asm
	nasm -f elf64 -o tests/ring3_fault.o tests/ring3_fault.asm

tests/ring3_fault: tests/ring3_fault.o
	ld -m elf_x86_64 -Ttext 0x400000 -o tests/ring3_fault tests/ring3_fault.o

# Convert ring3_exit ELF to a linkable object so it can be embedded in the kernel
tests/ring3_exit.bin.o: tests/ring3_exit
	objcopy -I binary -O elf64-x86-64 -B i386:x86-64 tests/ring3_exit tests/ring3_exit.bin.o

# Embed DOOM1.WAD directly into the kernel binary
DOOM1.WAD.bin.o: DOOM1.WAD
	@echo "  EMBED DOOM1.WAD ($$(du -h DOOM1.WAD | cut -f1))"
	objcopy -I binary -O elf64-x86-64 -B i386:x86-64 DOOM1.WAD DOOM1.WAD.bin.o

test-elfs: tests/ring3_exit tests/ring3_fault

clean:
	@rm -f $(ALL_OBJ) kernel/kernel.elf careos.iso DOOM1.WAD.bin.o
	@rm -f tests/ring3_exit.o tests/ring3_exit tests/ring3_fault.o tests/ring3_fault tests/ring3_exit.bin.o
	@echo "  Cleaned (kept $(DISK) for persistent users/data)"

clean-all: clean reset-disk
	@echo "  Full clean complete"

help:
	@echo "  make              build disk + ISO"
	@echo "  make run          run in QEMU (SDL window, PS/2 mouse)"
	@echo "  make run-1080p    run fullscreen"
	@echo "  make run-kvm      run with KVM acceleration"
	@echo "  make run-nowindow headless serial-only"
	@echo "  make debug        run with GDB"
	@echo "  make clean        remove build artifacts (keeps disk/user data)"
	@echo "  make reset-disk   recreate blank disk image (wipes users/data)"
	@echo "  make clean-all    clean + reset disk"
	@echo ""
	@echo "  Mouse: click QEMU window to capture, Ctrl+Alt+G to release"



