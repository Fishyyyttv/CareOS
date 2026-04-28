# CareOS x86_64 Migration Plan

## Phase 1: Build System & Tools
- [ ] Update `Makefile` to use x86_64 tools and flags.
    - `QEMU := qemu-system-x86_64`
    - `CFLAGS := -m64 ...`
    - `LDFLAGS := -m elf_x86_64`
    - `NASMFLAGS := -f elf64`
- [ ] Update `kernel.ld` for 64-bit (possibly higher-half).

## Phase 2: Boot & Long Mode Entry
- [ ] Rewrite `boot/boot.asm` to:
    - Stay in 32-bit initially (Multiboot2 requirement).
    - Create 4-level page tables (PML4, PDPT, PD).
    - Enable PAE and Paging.
    - Set `EFER.LME` (Long Mode Enable).
    - Perform a far jump to a 64-bit code segment.
    - Transition to 64-bit `_start64`.

## Phase 3: Kernel Core Updates
- [ ] Update `kernel/gdt_idt.c` for 64-bit descriptors.
- [ ] Update `kernel/paging.c` to handle 4-level paging.
- [ ] Update pointer types (e.g., `uint32_t` -> `uintptr_t` or `uint64_t` for addresses).

## Phase 4: Driver & Context Switching
- [ ] Update context switching in `kernel/scheduler.c` (or associated ASM) to save/restore 64-bit registers (RAX, RBX, RCX, RDX, RSI, RDI, RBP, R8-R15).
- [ ] AHCI Driver implementation (Target for disk access).

## Phase 5: GUI & Apps
- [ ] Verify GUI and apps compile and run in 64-bit mode (should be mostly fine if using abstraction types).
