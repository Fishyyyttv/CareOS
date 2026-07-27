# True Per-Process Address Spaces Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give every CareOS ring-3 process its own physical page tables for user-space memory, fixing a memory-corruption bug in the current paging path along the way, and prove it with an automated boot-time isolation test.

**Architecture:** Give user space a dedicated PML4 slot (index 1, virtual `0x0000008000000000`+) that is never aliased into the kernel's shared identity map (PML4[0], unchanged) or between processes. Since a fresh PML4 slot starts fully absent, the existing "allocate on absent" page-table-walk logic in `paging_map_internal` works correctly there without modification — this is what avoids the current bug where it misinterprets an already-present 2MB kernel huge page as a page-table pointer. Relink user ELF binaries to the new address range, add explicit (previously-missing) user-stack mapping, and rewrite `paging_free_dir` to actually free what it allocates.

**Tech Stack:** C (freestanding, `-m64 -ffreestanding`), x86_64 NASM assembly, GNU `ld`/`objcopy`, QEMU. No test framework exists in this project — verification is exclusively "build with `make`, boot in QEMU, read the serial log," so this plan adapts the usual TDD steps accordingly (see note below).

## Global Constraints

- Kernel identity map at PML4[0] (0–2GB, shared across all processes) must remain completely untouched — do not modify `paging_init()`.
- User-space private region: PML4 index 1, i.e. virtual `0x0000008000000000` (`USER_CODE_BASE`) through `0x0000008040000000` (`USER_CODE_MAX` / `USER_STACK_TOP`), per `docs/superpowers/specs/2026-07-20-per-process-address-spaces-design.md`.
- User stack size: reuse `TASK_STACK_SIZE` (2MB, `TASK_STACK_PAGES=512 * PAGE_SIZE=4096`), eagerly mapped — no demand paging in this plan (explicitly out of scope per the spec).
- Every allocation/mapping failure path must call `paging_free_dir(dir)` before returning an error, matching the existing convention in `elf_load_user`.
- **No build toolchain is available in this working session** (`gcc`/`ld`/`nasm`/`objcopy`/`qemu-system-x86_64`/`make` were all confirmed absent). Per-task verification is therefore a careful self-review of each diff, not a compile/run cycle. Task 8 is a handoff step: the user builds and boots CareOS in their own environment and reports the serial log back.
- Follow existing code style exactly: `serial_write`/`kutoa`/`kitoa` for logging (no `printf` in kernel-side boot code), `kmemset`/`kmemcpy` for memory ops, tabs/braces matching surrounding code.

---

### Task 1: Shared address-space and stack-size constants

**Files:**
- Modify: `include/kernel.h:337` (insert after `void paging_free_dir(pde_t *dir);`, before the `/* -- Preemptive scheduler ... */` comment)
- Modify: `include/kernel.h:340` (insert after `void scheduler_init(void);`)

**Interfaces:**
- Produces: `USER_PML4_INDEX`, `USER_CODE_BASE`, `USER_CODE_MAX`, `USER_STACK_TOP`, `TASK_STACK_PAGES`, `TASK_STACK_SIZE` (macros), `u64 paging_translate(pde_t *dir, u64 virt);`, `u64 task_get_cr3(int tid);`, `bool task_has_exited(int tid);` — all consumed by Tasks 2–4 and 7.

- [ ] **Step 1: Add the address-space and stack-size constants, plus the `paging_translate` declaration**

In `include/kernel.h`, immediately after this existing line (part of the paging block):

```c
void   paging_free_dir(pde_t *dir);
```

insert:

```c
u64    paging_translate(pde_t *dir, u64 virt);

/* User-space private address region: a dedicated PML4 slot so every
 * process gets its own physical PDPT/PD/PT frames, never aliased with the
 * kernel's shared identity map at PML4[0] or with other processes.
 * See docs/superpowers/specs/2026-07-20-per-process-address-spaces-design.md */
#define USER_PML4_INDEX   1
#define USER_CODE_BASE    ((u64)USER_PML4_INDEX << 39)     /* 0x0000008000000000 */
#define USER_CODE_MAX     (USER_CODE_BASE + 0x40000000ULL) /* +1GB: code/data */
#define USER_STACK_TOP    USER_CODE_MAX                    /* stack sits right after */

#define TASK_STACK_PAGES  512
#define TASK_STACK_SIZE   (TASK_STACK_PAGES * PAGE_SIZE)
```

- [ ] **Step 2: Add the new scheduler accessor declarations**

Immediately after this existing line:

```c
void scheduler_init(void);
```

insert:

```c
u64  task_get_cr3(int tid);
bool task_has_exited(int tid);
```

- [ ] **Step 3: Self-review**

Confirm: `PAGE_SIZE` (used by `TASK_STACK_SIZE`) is already defined earlier in the file (`include/kernel.h:134`, `#define PAGE_SIZE 4096`) — yes, it is, so no ordering problem. Confirm no other macro in the file is already named `USER_PML4_INDEX`, `USER_CODE_BASE`, `USER_CODE_MAX`, `USER_STACK_TOP`, `TASK_STACK_PAGES`, or `TASK_STACK_SIZE` (search the file) — there isn't; `TASK_STACK_PAGES`/`TASK_STACK_SIZE` currently only exist as local `#define`s inside `kernel/scheduler.c:45-46`, which Task 3 removes so there's no duplicate-definition conflict.

- [ ] **Step 4: Commit**

```bash
git add include/kernel.h
git commit -m "Add per-process address-space constants and accessor declarations"
```

---

### Task 2: `paging_translate` and a real `paging_free_dir`

**Files:**
- Modify: `kernel/paging.c:170-173` (replace `paging_free_dir` body)
- Modify: `kernel/paging.c` (add new `paging_translate` function, placed after `paging_free_dir`)

**Interfaces:**
- Consumes: `USER_PML4_INDEX` (Task 1), existing `PML4_INDEX`/`PDPT_INDEX`/`PD_INDEX`/`PT_INDEX` macros and `pmm_free_frame` already in this file.
- Produces: `u64 paging_translate(pde_t *dir, u64 virt)` — returns the mapped physical address, or `~0ULL` if any page-table level along the walk is not present. Consumed by Task 7's isolation test.
- Produces: rewritten `paging_free_dir(pde_t *dir)` — frees every frame allocated under `PML4[USER_PML4_INDEX]` (PDPT, PD, PT, and leaf frames), then the PML4 frame itself. Never touches `PML4[0]`.

- [ ] **Step 1: Replace `paging_free_dir`**

Replace this existing code (`kernel/paging.c:170-173`):

```c
void paging_free_dir(pde_t *dir) {
    /* For now just free the PML4 frame */
    pmm_free_frame((u64)dir / PAGE_SIZE);
}
```

with:

```c
void paging_free_dir(pde_t *dir) {
    pml4e_t *pml4 = (pml4e_t *)dir;

    if (pml4[USER_PML4_INDEX] & PDE_PRESENT) {
        pdpte_t *pdpt = (pdpte_t *)(pml4[USER_PML4_INDEX] & ~0xFFFULL);
        for (u32 pi = 0; pi < 512; pi++) {
            if (!(pdpt[pi] & PDE_PRESENT)) continue;
            pde_t *pd = (pde_t *)(pdpt[pi] & ~0xFFFULL);
            for (u32 di = 0; di < 512; di++) {
                if (!(pd[di] & PDE_PRESENT) || (pd[di] & PDE_4MB)) continue;
                pte_t *pt = (pte_t *)(pd[di] & ~0xFFFULL);
                for (u32 ti = 0; ti < 512; ti++) {
                    if (pt[ti] & PTE_PRESENT) {
                        pmm_free_frame((u32)((pt[ti] & ~0xFFFULL) / PAGE_SIZE));
                    }
                }
                pmm_free_frame((u32)((u64)pt / PAGE_SIZE));
            }
            pmm_free_frame((u32)((u64)pd / PAGE_SIZE));
        }
        pmm_free_frame((u32)((u64)pdpt / PAGE_SIZE));
    }

    pmm_free_frame((u32)((u64)dir / PAGE_SIZE));
}
```

- [ ] **Step 2: Add `paging_translate`**

Immediately after the new `paging_free_dir`, add:

```c
u64 paging_translate(pde_t *dir, u64 virt) {
    pml4e_t *pml4 = (pml4e_t *)dir;
    u32 pml4_i = PML4_INDEX(virt);
    u32 pdpt_i = PDPT_INDEX(virt);
    u32 pd_i   = PD_INDEX(virt);
    u32 pt_i   = PT_INDEX(virt);

    if (!(pml4[pml4_i] & PDE_PRESENT)) return ~0ULL;
    pdpte_t *pdpt = (pdpte_t *)(pml4[pml4_i] & ~0xFFFULL);

    if (!(pdpt[pdpt_i] & PDE_PRESENT)) return ~0ULL;
    pde_t *pd = (pde_t *)(pdpt[pdpt_i] & ~0xFFFULL);

    if (!(pd[pd_i] & PDE_PRESENT)) return ~0ULL;
    if (pd[pd_i] & PDE_4MB) {
        return (pd[pd_i] & ~0x1FFFFFULL) | (virt & 0x1FFFFFULL);
    }
    pte_t *pt = (pte_t *)(pd[pd_i] & ~0xFFFULL);

    if (!(pt[pt_i] & PTE_PRESENT)) return ~0ULL;
    return (pt[pt_i] & ~0xFFFULL) | (virt & 0xFFFULL);
}
```

- [ ] **Step 3: Self-review**

Confirm `PDE_4MB`, `PDE_PRESENT`, `PTE_PRESENT` are already defined in `include/kernel.h` (`PDE_PRESENT`/`PDE_4MB` at lines 261-268, `PTE_PRESENT` at line 271 — confirmed present, no new defines needed). Confirm `USER_PML4_INDEX` (Task 1) is visible here — `kernel/paging.c` includes `"kernel.h"` at the top of the file already. Confirm the loop bounds (512) match the 512-entry-per-table x86_64 paging structure used throughout this file (`pml4e_t pml4[512]` pattern used elsewhere, e.g. `kernel_pml4[512]` at `kernel/paging.c:50`).

- [ ] **Step 4: Commit**

```bash
git add kernel/paging.c
git commit -m "Fix paging_free_dir to actually free per-process frames; add paging_translate"
```

---

### Task 3: Scheduler — stack-size dedup, dead code removal, stack-top fix, new accessors

**Files:**
- Modify: `kernel/scheduler.c:45-46` (remove local stack-size defines, now sourced from `kernel.h`)
- Modify: `kernel/scheduler.c:209` (`task_user_trampoline`)
- Modify: `kernel/scheduler.c:256-257` (`task_create_user`, remove dead `ustack` alloc)
- Modify: `kernel/scheduler.c` (add `task_get_cr3`/`task_has_exited`, placed after `task_count_active`)

**Interfaces:**
- Consumes: `USER_STACK_TOP`, `TASK_STACK_PAGES`, `TASK_STACK_SIZE` (Task 1, now from `kernel.h`).
- Produces: `u64 task_get_cr3(int tid)` — returns `tasks[tid].cr3`, or `0` if `tid` is out of range. `bool task_has_exited(int tid)` — returns `true` if `tasks[tid].state == TASK_DEAD`, or `true` if `tid` is out of range (fail-safe so a bad tid can't hang a caller's wait loop). Both consumed by Task 7.

- [ ] **Step 1: Remove the now-duplicate local stack-size defines**

Delete these two lines from `kernel/scheduler.c` (lines 45-46):

```c
#define TASK_STACK_PAGES 512
#define TASK_STACK_SIZE  (TASK_STACK_PAGES * PAGE_SIZE)
```

They now come from `include/kernel.h` (Task 1), which `kernel/scheduler.c` already includes via `#include "kernel.h"` at the top of the file. Leave `#define TIMESLICE_DEFAULT 5` (the line right after) in place.

- [ ] **Step 2: Fix the hardcoded user-stack address in `task_user_trampoline`**

Replace this existing line (`kernel/scheduler.c:209`):

```c
    enter_userspace(t->entry, 0xBFF00000ULL + TASK_STACK_SIZE);
```

with:

```c
    enter_userspace(t->entry, USER_STACK_TOP);
```

- [ ] **Step 3: Remove the dead, never-mapped `ustack` kmalloc**

In `task_create_user`, replace these two existing lines (`kernel/scheduler.c:256-257`):

```c
    t->kstack = (u8*)kmalloc(TASK_STACK_SIZE);
    t->ustack = (u8*)kmalloc(TASK_STACK_SIZE);
```

with:

```c
    t->kstack = (u8*)kmalloc(TASK_STACK_SIZE);
```

(`t->ustack` was allocated from the kernel heap but never mapped into the user page directory and never read anywhere — the real user stack is now explicitly mapped with dedicated physical frames by `elf_load_user`, Task 4. The `ustack` field itself stays in `tcb_t` since removing a struct field isn't needed for this fix and risks touching unrelated code; it will just stay zero-initialized from the `kmemset(t, 0, sizeof(tcb_t))` at the top of `task_create_user`.)

- [ ] **Step 4: Add the new task accessors**

Immediately after the existing `task_count_active` function (`kernel/scheduler.c:310-316`), add:

```c
u64 task_get_cr3(int tid) {
    if (tid < 0 || (u32)tid >= task_count) return 0;
    return tasks[tid].cr3;
}

bool task_has_exited(int tid) {
    if (tid < 0 || (u32)tid >= task_count) return true;
    return tasks[tid].state == TASK_DEAD;
}
```

- [ ] **Step 5: Self-review**

Confirm `tasks[]` and `task_count` (file-scope `static` in this file) are visible at the new functions' location — yes, both declared at file scope (`kernel/scheduler.c:75-76`), and the new functions are added below their declaration point. Confirm the `tid` returned by `task_create`/`task_create_user` (`(int)(task_count - 1)`, i.e. the 0-based array index at creation time) is what `task_get_cr3`/`task_has_exited` expect — yes, both index `tasks[tid]` directly, matching that return-value convention (this is deliberately **not** the same as each `tcb_t`'s internal 1-based `.id` field, which `task_get(u32 id)` uses instead — don't mix the two).

- [ ] **Step 6: Commit**

```bash
git add kernel/scheduler.c
git commit -m "Scheduler: fix user stack address, drop dead ustack alloc, add task accessors"
```

---

### Task 4: ELF loader — new address range and explicit stack mapping

**Files:**
- Modify: `kernel/elf.c:24-25` (remove local `USER_VADDR_MIN`/`USER_VADDR_MAX` defines)
- Modify: `kernel/elf.c` (rename their two usages in `elf_load_user`'s bounds check, and add the new stack-mapping block before `task_create_user` is called)

**Interfaces:**
- Consumes: `USER_CODE_BASE`, `USER_CODE_MAX`, `USER_STACK_TOP`, `TASK_STACK_SIZE` (Task 1), `paging_map`/`paging_free_dir`/`pmm_alloc_frame`/`pmm_free_frame` (existing), `PTE_PRESENT`/`PTE_USER`/`PTE_RW` (existing).
- Produces: `elf_load_user` now also maps a real, dedicated user stack before creating the task — no interface signature change, existing callers (`kernel/kernel.c`) are unaffected.

- [ ] **Step 1: Remove the local address-range defines**

Delete these two lines from `kernel/elf.c` (lines 24-25):

```c
#define USER_VADDR_MIN  0x400000ULL
#define USER_VADDR_MAX  0xBFF00000ULL
```

- [ ] **Step 2: Update the bounds check to use the shared constants**

Replace this existing line inside `elf_load_user` (`kernel/elf.c:153`):

```c
        if (virt_start < USER_VADDR_MIN || virt_end > USER_VADDR_MAX) {
```

with:

```c
        if (virt_start < USER_CODE_BASE || virt_end > USER_CODE_MAX) {
```

- [ ] **Step 3: Add explicit user-stack mapping**

Immediately before this existing line in `elf_load_user` (`kernel/elf.c:190`):

```c
    int tid = task_create_user(name, eh->e_entry, dir, session);
```

insert:

```c
    /* Map a dedicated, eagerly-allocated user stack right after the
     * code/data window — private frames, never shared with any other
     * process. See docs/superpowers/specs/
     * 2026-07-20-per-process-address-spaces-design.md */
    {
        u64 stack_bottom = USER_STACK_TOP - TASK_STACK_SIZE;
        for (u64 virt = stack_bottom; virt < USER_STACK_TOP; virt += PAGE_SIZE) {
            u32 frame = pmm_alloc_frame();
            if (frame == (u32)~0u) { paging_free_dir(dir); return -8; }

            kmemset((void *)((u64)frame * PAGE_SIZE), 0, PAGE_SIZE);

            u32 flags = PTE_PRESENT | PTE_USER | PTE_RW;
            if (paging_map(dir, virt, (u64)frame * PAGE_SIZE, flags) != 0) {
                pmm_free_frame(frame);
                paging_free_dir(dir);
                return -9;
            }
        }
    }

```

(leave the blank line before `int tid = task_create_user(...)` as shown, matching the file's existing spacing between logical blocks)

- [ ] **Step 4: Self-review**

Confirm no other code in this file (or in `kernel/kernel.c`) references the old `USER_VADDR_MIN`/`USER_VADDR_MAX` names — `elf_load_vfs` (the other loader in this file) doesn't use them (it does a flat kernel-space load, unaffected by this change). Confirm the new error codes `-8`/`-9` don't collide with existing return values in `elf_load_user` (`-1` through `-7` already used, per the function as read — `-8`/`-9` are new and unused). Confirm `PTE_USER`/`PTE_RW`/`PTE_PRESENT` are already defined in `include/kernel.h:271-273` and already used elsewhere in this exact function's `PT_LOAD` mapping loop (`kernel/elf.c:178`), so the flag combination pattern matches existing style.

- [ ] **Step 5: Commit**

```bash
git add kernel/elf.c
git commit -m "elf_load_user: use dedicated user address range, map a real user stack"
```

---

### Task 5: New isolation-test assembly programs

**Files:**
- Create: `tests/ring3_isolate_a.asm`
- Create: `tests/ring3_isolate_b.asm`

**Interfaces:**
- Produces: two tiny ring-3 programs, each writing a distinct marker (`0xAAAAAAAA` / `0xBBBBBBBB`) to the fixed virtual address `0x8000000800` (chosen to sit safely inside the single 4KB page these binaries' `PT_LOAD` segment occupies, since each binary is only a few bytes and links at `USER_CODE_BASE` = `0x8000000000`), then exits via `int 0x80` (`SYS_EXIT`). Consumed by Task 6 (build rules) and Task 7 (kernel-side launch + verification), which must reference this exact address `0x8000000800` — if either test file's write address is ever changed, `kernel/kernel.c`'s isolation-test block must be updated to match.

- [ ] **Step 1: Create `tests/ring3_isolate_a.asm`**

```asm
; Ring-3 isolation test A: writes a marker value into its own address space,
; then exits. Paired with ring3_isolate_b.asm — the kernel launches both as
; separate processes and checks each retains its own value at the SAME
; virtual address, proving per-process address spaces are truly isolated.
; See docs/superpowers/specs/2026-07-20-per-process-address-spaces-design.md
BITS 64
SECTION .text
GLOBAL _start
_start:
    mov rax, 0x8000000800   ; test data address (must match kernel.c's check)
    mov dword [rax], 0xAAAAAAAA
    xor edi, edi             ; arg1 (exit code) = 0
    mov eax, 1                ; syscall 1 = SYS_EXIT
    int 0x80
    hlt
```

- [ ] **Step 2: Create `tests/ring3_isolate_b.asm`**

```asm
; Ring-3 isolation test B: writes a DIFFERENT marker value into its own
; address space, then exits. See ring3_isolate_a.asm for the full
; explanation — this file only differs by the marker constant.
BITS 64
SECTION .text
GLOBAL _start
_start:
    mov rax, 0x8000000800   ; test data address (must match kernel.c's check)
    mov dword [rax], 0xBBBBBBBB
    xor edi, edi             ; arg1 (exit code) = 0
    mov eax, 1                ; syscall 1 = SYS_EXIT
    int 0x80
    hlt
```

- [ ] **Step 3: Self-review**

Confirm both files use `mov rax, <addr>` before dereferencing rather than a direct absolute memory operand (`mov dword [0x8000000800], ...`) — x86-64 direct absolute addressing without a base register only encodes a sign-extended 32-bit displacement, which cannot reach `0x8000000800` (~512GB); loading the full 64-bit address into a register first, as done here, is required. Confirm the address `0x8000000800` is within `[USER_CODE_BASE, USER_CODE_BASE + 0x1000)` = `[0x8000000000, 0x8000001000)`, i.e. inside the single page that `elf_load_user`'s `PT_LOAD` mapping loop will map for these tiny binaries (each is well under 4KB) — `0x8000000800` is `0x800` bytes past `USER_CODE_BASE`, safely inside that one page and safely past the ~10 bytes of actual code at the start of it.

- [ ] **Step 4: Commit**

```bash
git add tests/ring3_isolate_a.asm tests/ring3_isolate_b.asm
git commit -m "Add ring3_isolate_a/b test programs for address-space isolation test"
```

---

### Task 6: Makefile — relink existing tests, build/embed the new ones

**Files:**
- Modify: `Makefile:127` (`ALL_OBJ`)
- Modify: `Makefile:205` (`tests/ring3_exit` link address)
- Modify: `Makefile:211` (`tests/ring3_fault` link address)
- Modify: `Makefile:214-215` (comment + rule, for clarity alongside new rules)
- Modify: `Makefile:222` (`test-elfs` prerequisites)
- Modify: `Makefile:224-226` (`clean` target)

**Interfaces:**
- Consumes: `tests/ring3_isolate_a.asm`, `tests/ring3_isolate_b.asm` (Task 5).
- Produces: `tests/ring3_isolate_a.bin.o`, `tests/ring3_isolate_b.bin.o` — linked into `kernel/kernel.elf` via `ALL_OBJ`, exposing `_binary_tests_ring3_isolate_a_start/end` and `_binary_tests_ring3_isolate_b_start/end` symbols (objcopy's standard naming for input path `tests/ring3_isolate_a` → symbol prefix `_binary_tests_ring3_isolate_a`, matching the existing `_binary_tests_ring3_exit_start/end` convention). Consumed by Task 7.

- [ ] **Step 1: Add the new object files to `ALL_OBJ`**

Replace this existing line (`Makefile:127`):

```makefile
ALL_OBJ   := $(ASM_OBJ) $(C_OBJ) $(DOOM_OBJ) tests/ring3_exit.bin.o DOOM1.WAD.bin.o
```

with:

```makefile
ALL_OBJ   := $(ASM_OBJ) $(C_OBJ) $(DOOM_OBJ) tests/ring3_exit.bin.o tests/ring3_isolate_a.bin.o tests/ring3_isolate_b.bin.o DOOM1.WAD.bin.o
```

- [ ] **Step 2: Relink `ring3_exit` and `ring3_fault` at the new user address**

Replace this existing line (`Makefile:205`):

```makefile
	ld -m elf_x86_64 -Ttext 0x400000 -o tests/ring3_exit tests/ring3_exit.o
```

with:

```makefile
	ld -m elf_x86_64 -Ttext 0x8000000000 -o tests/ring3_exit tests/ring3_exit.o
```

Replace this existing line (`Makefile:211`):

```makefile
	ld -m elf_x86_64 -Ttext 0x400000 -o tests/ring3_fault tests/ring3_fault.o
```

with:

```makefile
	ld -m elf_x86_64 -Ttext 0x8000000000 -o tests/ring3_fault tests/ring3_fault.o
```

- [ ] **Step 3: Add build rules for the two new isolation-test binaries**

Immediately after this existing block (`Makefile:213-215`):

```makefile
# Convert ring3_exit ELF to a linkable object so it can be embedded in the kernel
tests/ring3_exit.bin.o: tests/ring3_exit
	objcopy -I binary -O elf64-x86-64 -B i386:x86-64 tests/ring3_exit tests/ring3_exit.bin.o
```

insert:

```makefile
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
```

- [ ] **Step 4: Update `test-elfs` prerequisites**

Replace this existing line (`Makefile:222`):

```makefile
test-elfs: tests/ring3_exit tests/ring3_fault
```

with:

```makefile
test-elfs: tests/ring3_exit tests/ring3_fault tests/ring3_isolate_a tests/ring3_isolate_b
```

- [ ] **Step 5: Update `clean` to remove the new intermediate files**

Replace this existing line (`Makefile:226`):

```makefile
	@rm -f tests/ring3_exit.o tests/ring3_exit tests/ring3_fault.o tests/ring3_fault tests/ring3_exit.bin.o
```

with:

```makefile
	@rm -f tests/ring3_exit.o tests/ring3_exit tests/ring3_fault.o tests/ring3_fault tests/ring3_exit.bin.o
	@rm -f tests/ring3_isolate_a.o tests/ring3_isolate_a tests/ring3_isolate_a.bin.o
	@rm -f tests/ring3_isolate_b.o tests/ring3_isolate_b tests/ring3_isolate_b.bin.o
```

- [ ] **Step 6: Self-review**

Confirm `kernel/kernel.elf: $(ALL_OBJ)` (existing rule, unmodified) will now transitively build and embed both new binaries automatically on a plain `make`/`make run` — same mechanism that already makes `ring3_exit.bin.o` build automatically without needing `make test-elfs` manually. Confirm the `-Ttext 0x8000000000` value matches `USER_CODE_BASE` from Task 1 exactly (`((u64)1 << 39)` = `0x8000000000`). Confirm no other Makefile rule references the old `0x400000` address (search the file) — none do.

- [ ] **Step 7: Commit**

```bash
git add Makefile
git commit -m "Makefile: relink test ELFs to new user address, build isolation-test binaries"
```

---

### Task 7: Kernel boot sequence — relocate ring3_exit test, add isolation test

**Files:**
- Modify: `kernel/kernel.c` (add two helper functions before `kernel_main`)
- Modify: `kernel/kernel.c:188-210` (remove the old, misplaced ring3_exit smoke-test block)
- Modify: `kernel/kernel.c:285` (insert the relocated + new test blocks after `scheduler_init()`)

**Interfaces:**
- Consumes: `task_yield`, `task_has_exited`, `task_get_cr3` (Task 3), `paging_translate` (Task 2), `elf_load_user` (existing), `_binary_tests_ring3_exit_start/end`, `_binary_tests_ring3_isolate_a_start/end`, `_binary_tests_ring3_isolate_b_start/end` (Task 6, objcopy-generated symbols), the fixed test address `0x8000000800` (Task 5).
- Produces: boot-time serial log lines `[isolation-test] PASS` or `[isolation-test] FAIL: ...` — this is the plan's success signal, checked in Task 8.

- [ ] **Step 1: Understand why the existing smoke test needs to move**

`scheduler_init()` (`kernel/scheduler.c:283-298`) unconditionally does `kmemset(tasks, 0, sizeof(tasks))` and resets `task_count = 0`. The current ring3_exit smoke test (`kernel/kernel.c:188-210`) runs *before* `scheduler_init()` is called (`kernel/kernel.c:285`), so the task it creates is wiped out before the scheduler ever starts — it never actually executes. This step is just confirming that finding before editing; no code change here.

- [ ] **Step 2: Add two helper functions before `kernel_main`**

Immediately after the existing `enable_sse` function and before `void kernel_main(...)` (i.e. right after `kernel/kernel.c:72`, before line 74), insert:

```c
static void slog_hex64(const char *label, u64 v) {
    serial_write(label);
    serial_write("0x");
    char b[12];
    kutoa((u32)(v >> 32), b, 16); serial_write(b);
    kutoa((u32)(v & 0xFFFFFFFFu), b, 16); serial_write(b);
    serial_write("\n");
}

/* Copies an objcopy-embedded ELF blob into /bin/<bin_name> and launches it
 * as a ring-3 task. Returns the task id on success, -1 on failure. */
static int smoke_embed_and_launch(const char *bin_name, const u8 *start,
                                   const u8 *end, const char *task_name) {
    u32 elf_size = (u32)(uintptr_t)(end - start);
    serial_write("[smoke] "); serial_write(bin_name); serial_write(" ELF size = ");
    char sb[12]; kutoa(elf_size, sb, 10); serial_write(sb); serial_write("\n");

    if (elf_size == 0 || elf_size > FS_FILE_DATA_MAX) {
        serial_write("[smoke] ERROR: bad ELF size for "); serial_write(bin_name); serial_write("\n");
        return -1;
    }

    fs_node_t *bin_dir = vfs_mkdir(vfs_root(), "bin");
    if (!bin_dir) bin_dir = vfs_find(vfs_root(), "bin");
    if (!bin_dir) return -1;

    fs_node_t *n = vfs_mkfile(bin_dir, bin_name);
    if (!n) return -1;

    kmemcpy(n->data, start, elf_size);
    n->size = elf_size;

    int tid = elf_load_user(n, task_name, -1);
    serial_write("[smoke] elf_load_user("); serial_write(bin_name); serial_write(") returned ");
    kitoa(tid, sb, 10); serial_write(sb); serial_write("\n");
    return tid;
}
```

- [ ] **Step 3: Remove the old, misplaced ring3_exit smoke-test block**

Delete this entire existing block (`kernel/kernel.c:188-210`):

```c
    /* ── Ring-3 smoke test: embed ring3_exit ELF and launch it ────────────────── */
    {
        extern u8 _binary_tests_ring3_exit_start[];
        extern u8 _binary_tests_ring3_exit_end[];
        u32 elf_size = (u32)(uintptr_t)(_binary_tests_ring3_exit_end - _binary_tests_ring3_exit_start);
        serial_write("[smoke] ring3_exit ELF size = ");
        char _sb[12]; kutoa(elf_size, _sb, 10); serial_write(_sb); serial_write("\n");

        fs_node_t *bin_dir = vfs_mkdir(vfs_root(), "bin");
        if (!bin_dir) bin_dir = vfs_find(vfs_root(), "bin");
        if (bin_dir && elf_size > 0 && elf_size <= FS_FILE_DATA_MAX) {
            fs_node_t *n = vfs_mkfile(bin_dir, "ring3_exit");
            if (n) {
                kmemcpy(n->data, _binary_tests_ring3_exit_start, elf_size);
                n->size = elf_size;
                int tid = elf_load_user(n, "ring3_exit", -1);
                serial_write("[smoke] elf_load_user returned ");
                kitoa(tid, _sb, 10); serial_write(_sb); serial_write("\n");
            }
        } else if (elf_size > FS_FILE_DATA_MAX) {
            serial_write("[smoke] ELF too large for VFS node data field\n");
        }
    }

```

leaving `paging_init(); slog_ok("Paging"); syscall_init(); slog_ok("Syscalls");` directly followed by the `/* Stage 3: ... */` comment.

- [ ] **Step 4: Insert the relocated test and the new isolation test after `scheduler_init()`**

Immediately after this existing line (`kernel/kernel.c:285`):

```c
    scheduler_init();    slog_ok("Scheduler");
```

insert:

```c

    /* ── Ring-3 smoke tests: must run after scheduler_init(), which resets
     * the task table — anything launched before it never actually runs. ── */
    {
        extern u8 _binary_tests_ring3_exit_start[];
        extern u8 _binary_tests_ring3_exit_end[];
        smoke_embed_and_launch("ring3_exit", _binary_tests_ring3_exit_start,
                                _binary_tests_ring3_exit_end, "ring3_exit");
    }

    /* ── Address-space isolation test: launch two processes that each write
     * a distinct marker to the same virtual address, then verify each kept
     * its own physical frame. See docs/superpowers/specs/
     * 2026-07-20-per-process-address-spaces-design.md ── */
    {
        extern u8 _binary_tests_ring3_isolate_a_start[];
        extern u8 _binary_tests_ring3_isolate_a_end[];
        extern u8 _binary_tests_ring3_isolate_b_start[];
        extern u8 _binary_tests_ring3_isolate_b_end[];

        int tid_a = smoke_embed_and_launch("ring3_isolate_a",
                        _binary_tests_ring3_isolate_a_start,
                        _binary_tests_ring3_isolate_a_end, "isolate_a");
        int tid_b = smoke_embed_and_launch("ring3_isolate_b",
                        _binary_tests_ring3_isolate_b_start,
                        _binary_tests_ring3_isolate_b_end, "isolate_b");

        if (tid_a < 0 || tid_b < 0) {
            serial_write("[isolation-test] FAIL: could not launch test tasks\n");
        } else {
            int spins = 0;
            while (spins < 500 && !(task_has_exited(tid_a) && task_has_exited(tid_b))) {
                task_yield();
                spins++;
            }
            if (spins >= 500) {
                serial_write("[isolation-test] FAIL: tasks did not complete in time\n");
            } else {
                u64 dir_a = task_get_cr3(tid_a);
                u64 dir_b = task_get_cr3(tid_b);
                u64 phys_a = paging_translate((pde_t *)dir_a, 0x8000000800ULL);
                u64 phys_b = paging_translate((pde_t *)dir_b, 0x8000000800ULL);
                u32 val_a = (phys_a != ~0ULL) ? *(volatile u32 *)phys_a : 0;
                u32 val_b = (phys_b != ~0ULL) ? *(volatile u32 *)phys_b : 0;

                slog_hex64("[isolation-test] phys_a = ", phys_a);
                slog_hex64("[isolation-test] phys_b = ", phys_b);
                slog_hex64("[isolation-test] val_a  = ", (u64)val_a);
                slog_hex64("[isolation-test] val_b  = ", (u64)val_b);

                bool pass = (phys_a != ~0ULL) && (phys_b != ~0ULL) &&
                            (phys_a != phys_b) &&
                            (val_a == 0xAAAAAAAAu) && (val_b == 0xBBBBBBBBu);
                serial_write(pass ? "[isolation-test] PASS\n" : "[isolation-test] FAIL\n");
            }
        }
    }
```

- [ ] **Step 5: Self-review**

Confirm `task_yield`, `task_has_exited`, `task_get_cr3`, `paging_translate` are all declared in `include/kernel.h` (Tasks 1–3) and thus visible in `kernel/kernel.c` (already `#include "kernel.h"`). Confirm the hardcoded address `0x8000000800` matches exactly what `tests/ring3_isolate_a.asm`/`tests/ring3_isolate_b.asm` write to (Task 5) — both must agree or the test will always read back `0`. Confirm the objcopy symbol names (`_binary_tests_ring3_isolate_a_start/end`, `_binary_tests_ring3_isolate_b_start/end`) match the input file paths used in the new Makefile rules (Task 6: `tests/ring3_isolate_a`, `tests/ring3_isolate_b`) — objcopy derives symbol names from the input path with non-alphanumeric characters replaced by underscores, so `tests/ring3_isolate_a` → `_binary_tests_ring3_isolate_a_{start,end,size}`, matching. Confirm the 500-iteration `task_yield()` cap is generous enough: each isolate task is 4 instructions with no blocking I/O, `TIMESLICE_DEFAULT` is 5 ticks, and there are only 3 tasks in rotation (idle, isolate_a, isolate_b) at this point in boot, so a full round-robin cycle completes within roughly 15 scheduler ticks — 500 gives well over an order of magnitude of headroom.

- [ ] **Step 6: Commit**

```bash
git add kernel/kernel.c
git commit -m "Relocate ring3_exit smoke test after scheduler_init, add address-space isolation test"
```

---

### Task 8: Build and boot verification (handoff)

**Files:** none (verification only).

**Interfaces:** none.

- [ ] **Step 1: Build the kernel**

In an environment with the CareOS toolchain installed (`gcc`, `nasm`, `ld`, `objcopy`, `grub-mkrescue`, `qemu-system-x86_64`, `make`), run:

```bash
make clean
make kernel/kernel.elf
```

Expected: builds without errors. If it fails, capture the full compiler/linker error output — most likely causes given this plan's changes are a typo in one of the new constants, a missing `#include`, or a symbol-name mismatch between the Makefile's objcopy input paths (Task 6) and the `extern` declarations in `kernel/kernel.c` (Task 7).

- [ ] **Step 2: Boot in QEMU and capture the serial log**

```bash
make run-nowindow
```

This boots headless with serial output on stdout (no mouse/GUI, but sufficient to observe the boot sequence). Let it run until the log stabilizes (reaches the shell or GUI login stage, or repeats/hangs), then stop it (Ctrl+C).

- [ ] **Step 3: Check the log for the isolation-test result**

Look for these lines (in order) in the captured output:

```
[STAGE 2] Core services
...
  [OK] Scheduler
[smoke] ring3_exit ELF size = ...
[smoke] elf_load_user(ring3_exit) returned ...
[smoke] ring3_isolate_a ELF size = ...
[smoke] elf_load_user(ring3_isolate_a) returned ...
[smoke] ring3_isolate_b ELF size = ...
[smoke] elf_load_user(ring3_isolate_b) returned ...
[isolation-test] phys_a = 0x...
[isolation-test] phys_b = 0x...
[isolation-test] val_a  = 0x00000000aaaaaaaa
[isolation-test] val_b  = 0x00000000bbbbbbbb
[isolation-test] PASS
```

- **If you see `[isolation-test] PASS`**: `phys_a` and `phys_b` must also be different addresses in the log — that's the actual proof of isolation (two processes, two distinct physical frames, correct per-process values). Report success back.
- **If you see `[isolation-test] FAIL: ...`**: report back the exact failure line plus the four `phys_a`/`phys_b`/`val_a`/`val_b` lines (if printed) or the "did not complete in time" / "could not launch test tasks" message — this pinpoints which stage broke (launch, scheduling, or translation/value mismatch).
- **If the kernel hangs, resets, or triple-faults before reaching Stage 2's end**: report the last few lines of serial output before it stopped — likely a page-fault from the new stack-mapping or address-range logic (Tasks 2/4).

- [ ] **Step 4: Regression check**

Confirm the rest of the boot sequence still completes normally after the new test block (Stage 3 device init, GUI boot splash / login screen, or shell prompt on the fallback path) — this plan's changes sit early in boot and must not affect anything downstream. Report any new errors or behavior changes observed anywhere in the log, not just around the isolation test.

- [ ] **Step 5: Report back**

Paste the relevant serial log excerpt (Steps 3–4) back so any failures can be diagnosed and fixed. No commit for this task — it's verification only.
