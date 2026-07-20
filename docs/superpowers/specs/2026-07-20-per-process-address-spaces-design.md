# CareOS: True Per-Process Address Spaces — Design

Status: Approved for implementation
Date: 2026-07-20
Related: [CAREOS_OVERVIEW.md](../../../CAREOS_OVERVIEW.md) §1.3, §12 items 7–8; [CAREOS_ROADMAP.md](../../../CAREOS_ROADMAP.md) Phase 1 — Memory Management

## Problem

CareOS's ring-3 user tasks do not have real, isolated address spaces, and the current implementation has two concrete bugs beyond the general lack of isolation:

1. **Shared top-level page tables.** `paging_create_dir()` (`kernel/paging.c:153-160`) sets every new process's `PML4[0]` to alias the kernel's own PDPT frame (`pml4[0] = kernel_pml4[0]`). Because `USER_VADDR_MIN..USER_VADDR_MAX` (`0x400000`–`0xBFF00000`, `kernel/elf.c:24-25`) falls entirely inside PML4 index 0, every process's user-space page tables are physically the same PDPT/PD frames as the kernel's and every other process's.

2. **Huge-page misinterpretation corrupts memory.** Most of that address range is already mapped by `paging_init()` (`kernel/paging.c:126-137`) as 2MB huge pages (`PDE_4MB`) for the kernel's own identity map. When `elf_load_user` asks `paging_map_internal` to install a 4KB mapping there, the "already present" check at `kernel/paging.c:99` is true, so the code treats the huge page's physical base address as if it were a page-table pointer and writes a PTE-sized value directly into physical memory at that address — silently corrupting a few bytes of whatever is actually stored there (likely kernel heap) on every ring-3 program load. This has not caused a visible crash so far only because the sole program ever loaded this way (`tests/ring3_exit.asm`) is tiny and the corrupted offset happens to fall in unused memory.

3. **The user stack is never mapped at all.** `task_create_user` (`kernel/scheduler.c:241-268`) allocates `t->ustack` via `kmalloc` but never uses it. `task_user_trampoline` (`kernel/scheduler.c:207-210`) hardcodes the user `RSP` to `0xBFF00000 + TASK_STACK_SIZE`, and nothing ever calls `paging_map` for that address. Any ring-3 program that pushes to or calls through its stack will page-fault immediately. This is latent only because `ring3_exit.asm` never touches the stack.

4. **`paging_free_dir()` leaks memory.** It frees only the PML4 frame (`kernel/paging.c:170-173`), never the PDPT/PD/PT frames allocated underneath it.

## Goals

- Every ring-3 process gets its own physical page-table frames for its user-space mappings — no process can read, write, or corrupt another process's or the kernel's memory through a shared table.
- The user stack is properly, explicitly mapped with real frames.
- `paging_free_dir()` fully reclaims a process's private frames on exit.
- Prove isolation with an automated boot-time test, not just "it doesn't crash."

## Non-goals (deferred to later Memory Management roadmap items)

- Copy-on-write, demand paging, lazy allocation, ASLR, shared memory, mmap, huge pages for user space, slab allocator. These build on top of this work but are separate sub-projects.
- Moving the kernel to a higher-half layout. The kernel's existing low-memory identity map is left untouched and continues to be shared (correctly) across all address spaces.

## Design

### Address space layout

The kernel's existing identity map stays exactly as-is at **PML4 index 0** (virtual `0x0`–`0x8000000000`), shared and aliased into every process (`pml4[0] = kernel_pml4[0]`, unchanged) — this sharing is correct and necessary, since ring-0 code (interrupt/syscall handlers, the active kernel stack) must stay reachable no matter which process's directory is loaded in `CR3`.

**PML4 index 1** (virtual `0x0000008000000000`–`0x0000010000000000`, a 512GB range) becomes the private per-process user region. `paging_create_dir()` already leaves this slot zeroed (it only ever touches index 0), so no change is needed there — a fresh, all-absent slot means `paging_map_internal`'s existing "allocate on absent" logic (`kernel/paging.c:83-104`) works correctly and never encounters a pre-existing huge page, which is what sidesteps bug #2 entirely.

Concrete layout within the 512GB slot:

| Region | Range | Purpose |
|---|---|---|
| Code/data | `0x0000008000000000` – `+0x40000000` (1GB) | ELF `PT_LOAD` segments |
| Stack | `0x0000008040000000 - TASK_STACK_SIZE` – `0x0000008040000000` | User stack, eagerly mapped, `TASK_STACK_SIZE` = 2MB (matches existing kernel-stack sizing) |

This leaves ~511GB of headroom for later phases (heap/mmap/CoW) without another address-layout redesign.

### Component changes

**`kernel/paging.c`**
- `paging_create_dir()`: no change.
- `paging_free_dir()`: rewritten to walk PML4[1]'s PDPT → PD → PT, freeing every present frame via `pmm_free_frame`, then freeing the PML4 frame itself. PML4[0] is never touched or freed.
- New `u64 paging_translate(pde_t *dir, u64 virt)`: read-only walk returning the mapped physical address, or `~0ULL` if any level is absent. Used by the isolation test; generally useful for future debugging/signal work.

**`kernel/elf.c`**
- `USER_VADDR_MIN`/`USER_VADDR_MAX` updated to `0x0000008000000000` / `0x0000008040000000` (the code/data region).
- `elf_load_user`: existing per-page mapping loop is unchanged in logic; it now naturally operates against the fresh PML4[1] slot.
- Before calling `task_create_user`, allocate `TASK_STACK_SIZE / PAGE_SIZE` frames via `pmm_alloc_frame` and map them into `dir` at the stack region (`PTE_PRESENT | PTE_RW | PTE_USER`), mirroring the existing PT_LOAD mapping error-handling pattern (`paging_free_dir` + return an error code on any allocation failure).

**`kernel/scheduler.c`**
- `task_user_trampoline`: replace the hardcoded `0xBFF00000ULL + TASK_STACK_SIZE` with `USER_STACK_TOP` (new shared constant, see below).
- `task_create_user`: remove the dead `t->ustack = kmalloc(TASK_STACK_SIZE)` line — real stack frames now come from `elf_load_user`'s explicit mapping step, not the kernel heap.

**`include/kernel.h`**
- Add shared constants: `USER_CODE_BASE`, `USER_CODE_MAX`, `USER_STACK_TOP` (used by both `elf.c` and `scheduler.c`).

**`tests/`**
- `ring3_exit.asm`, `ring3_fault.asm`: relink from `0x400000` to `USER_CODE_BASE` (Makefile `test-elfs` target's `-Ttext` flag).
- New `ring3_isolate_a.asm` / `ring3_isolate_b.asm`: each writes a distinct constant (`0xAAAAAAAA` / `0xBBBBBBBB`) to a fixed data address within its own address space, then exits via `int 0x80` (`SYS_EXIT`).

**`kernel/kernel.c`**
- Extend the existing Stage-2 ring-3 smoke test: launch both isolate tasks, let the scheduler run a few ticks, then use `paging_translate()` against each task's own `cr3`/dir to read back the value directly from each one's physical frame. Assert both wrote their own value correctly and that the two physical frames differ. Logged to serial, consistent with the project's existing boot-time verification style — no new test harness required.

### Error handling

Unchanged pattern from the existing code: any allocation or mapping failure during ELF segment or stack setup calls `paging_free_dir()` and returns a negative error code, exactly as `elf_load_user` already does for `PT_LOAD` segments. The new stack-mapping step follows the same convention.

### Testing

1. Existing `ring3_exit` smoke test must still pass (proves nothing regressed).
2. New isolation test (above) must show both tasks retain their own distinct values in physically distinct frames — proving the address spaces are truly private, not merely non-crashing.
3. Manual: `make run`, confirm boot completes and the new serial log lines report the isolation test passing.
