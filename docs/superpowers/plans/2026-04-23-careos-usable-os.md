# CareOS Usable OS Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Transform CareOS v9 into a genuinely usable desktop OS with ring 3 process isolation, ext2 filesystem, HTTPS browsing via mbedTLS + Links2, and concurrent multi-user GUI sessions.

**Architecture:** Foundation-first — ring 3 → ext2 → HTTPS/browser → multi-session GUI. Each phase has a hard gate test before the next begins. Each layer extends existing code rather than replacing it.

**Tech Stack:** C (freestanding, gcc -m32), x86 assembly (NASM), QEMU i386, mbedTLS (vendored), Links2 (vendored), ext2 (implemented from scratch atop existing ATA driver).

**Build environment:** All `make` and `gcc` commands run inside WSL (or a Linux cross-compilation toolchain). QEMU runs from Windows. `e2fsck` and `mkfs.ext2` are from the `e2fsprogs` package inside WSL.

**Known cross-file type issue:** `PTE_PRESENT`, `PTE_RW`, `PTE_USER` are currently `#define`'d only inside `kernel/paging.c`. Task 2 uses them in `kernel/scheduler.c`. Move these three defines to `include/kernel.h` before starting Task 2, or the build will fail with "undeclared identifier" errors. Same applies to `PDE_USER`.

---

## Codebase State (read this first)

Before touching anything, know what already exists:

| Feature | Status | Location |
|---------|--------|----------|
| Ring 3 GDT segments (0x1B code, 0x23 data) | **Done** | `kernel/gdt_idt.c:26-29` |
| TSS setup + `tss_set_kernel_stack()` | **Done** | `kernel/scheduler.c:41-65` |
| `int 0x80` IDT gate with DPL=3 (`0xEF`) | **Done** | `kernel/syscall.c:277-281` |
| Per-process page dir: create/map/free/switch | **Done** | `kernel/paging.c:253-302` |
| Scheduler CR3 swap + TSS.ESP0 update on switch | **Done** | `kernel/scheduler.c:186-187` |
| TCB fields: `is_user`, `cr3`, `kstack`, `ustack` | **Done** | `kernel/scheduler.c:72-94` |
| ELF loader (loads into kernel space) | **Partial** | `kernel/elf.c` — needs user page table |
| Syscalls: exit, read, write, open, close, brk, yield | **Done** | `kernel/syscall.c` |
| Page fault handler (panics on all faults) | **Needs fix** | `kernel/paging.c:104-168` |
| ATA driver with sector read/write | **Done** | `drivers/storage/ata.c` |
| VFS with in-memory backend | **Done** | `kernel/vfs.c` |

---

## File Map

### Phase 1 — Ring 3 User-Space
| File | Action | What changes |
|------|--------|-------------|
| `kernel/paging.c` | Modify | Page fault handler: kill ring-3 process instead of panic |
| `kernel/paging.h` / `include/kernel.h` | Modify | Export `paging_map_user_pages()` helper |
| `kernel/scheduler.c` | Modify | Add `task_create_user()`, `enter_userspace()`, add `session_id` field to TCB |
| `kernel/elf.c` | Modify | Add `elf_load_user()` — loads into user page table, launches ring 3 task |
| `kernel/syscall.c` | Modify | Add `copy_from_user()`, `copy_to_user()`, fix `sys_brk`, add `fork`/`exec`/`waitpid` |
| `tests/ring3_exit.asm` | Create | Minimal ring 3 ELF: calls `exit(0)` via `int 0x80` |
| `tests/ring3_fault.asm` | Create | Ring 3 ELF: dereferences null pointer — verifies kernel survives |

### Phase 2 — ext2 Filesystem
| File | Action | What changes |
|------|--------|-------------|
| `kernel/ext2.h` | Create | All ext2 structs, constants, and public API |
| `kernel/ext2.c` | Create | Full ext2 driver (~800 lines) |
| `kernel/vfs.c` | Modify | Swap in-memory backend for ext2 calls |
| `kernel/kernel.c` | Modify | Call `ext2_mount()` after `ata_init()` in stage 2 |
| `kernel/users.c` | Modify | Create `/home/username` on first login |
| `Makefile` | Modify | Add `format-disk` target, grow disk to 512MB |

### Phase 3 — mbedTLS + HTTPS
| File | Action | What changes |
|------|--------|-------------|
| `net/mbedtls/` | Create | Vendored mbedTLS `library/` sources |
| `net/mbedtls/config.h` | Create | Kernel-appropriate mbedTLS config (no platform/fs/timing) |
| `net/tls.h` | Create | Public TLS API: `tls_connect`, `tls_read`, `tls_write`, `tls_close` |
| `net/tls.c` | Create | mbedTLS glue: callbacks, entropy, context management |
| `net/ca_bundle.c` | Create | Mozilla CA bundle as C array |

### Phase 4 — Links2 Browser Engine
| File | Action | What changes |
|------|--------|-------------|
| `apps/links2/` | Create | Vendored Links2 sources |
| `apps/links2/careos_fb.c` | Create | Framebuffer shim: Links2 draw calls → `gfx.c` |
| `apps/links2/careos_input.c` | Create | Input shim: Links2 event reads → our keyboard/mouse |
| `apps/links2/careos_net.c` | Create | Network shim: Links2 sockets → our TCP + TLS |
| `apps/app_browser.c` | Modify | Replace custom renderer with Links2 launcher |

### Phase 5 — Multi-Session GUI
| File | Action | What changes |
|------|--------|-------------|
| `gui/session.h` | Create | `session_t` struct, session states, public API |
| `gui/session.c` | Create | Session manager: create, switch, destroy, overlay |
| `gui/gui.c` | Modify | Replace single framebuffer with session-managed virtual buffers |
| `gui/wm.c` | Modify | WM state becomes per-session |
| `drivers/keyboard.c` | Modify | Route `Ctrl+Alt+F1..F4` to session manager |

---

## Phase 1 — Ring 3 User-Space

### Task 1: Fix page fault handler to survive ring-3 faults

**Files:**
- Modify: `kernel/paging.c:104-168`

The current handler calls `kernel_panic()` on any unhandled fault. A ring-3 page fault should kill only the offending process.

- [ ] **Step 1: Add CPL check at top of `page_fault_handler`**

In `kernel/paging.c`, replace the `fault_halt:` label and `kernel_panic` call:

```c
static void page_fault_handler(registers_t *r) {
    u32 fault_addr;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(fault_addr));
    char buf[12];

    serial_write("[PAGE FAULT] addr=0x");
    kutoa(fault_addr, buf, 16); serial_write(buf);
    serial_write(" err=0x");
    kutoa(r->err_code, buf, 16); serial_write(buf);
    serial_write("\n");

    /* Check if fault came from ring 3 (CS low 2 bits = 3) */
    bool from_user = (r->cs & 3) == 3;

    u32 pd_idx = fault_addr >> 22;
    u32 pt_idx = (fault_addr >> 12) & 0x3FF;

    /* On-demand mapping for kernel heap region */
    if (!from_user && fault_addr >= 0x400000 && fault_addr < 0xBF000000) {
        pde_t *pd = kernel_page_dir;
        if (!(pd[pd_idx] & PDE_PRESENT)) {
            u32 frame = pmm_alloc_frame();
            if (frame == (u32)~0u) goto fault_kill;
            pte_t *pt = (pte_t*)(frame * PAGE_SIZE);
            kmemset(pt, 0, PAGE_SIZE);
            pd[pd_idx] = (frame * PAGE_SIZE) | PDE_PRESENT | PDE_RW;
        }
        pte_t *pt = (pte_t*)(pd[pd_idx] & ~0xFFFu);
        if (!(pt[pt_idx] & PTE_PRESENT)) {
            u32 frame = pmm_alloc_frame();
            if (frame == (u32)~0u) goto fault_kill;
            pt[pt_idx] = (frame * PAGE_SIZE) | PTE_PRESENT | PTE_RW;
            __asm__ volatile ("invlpg (%0)" : : "r"(fault_addr & ~0xFFFu) : "memory");
            return;
        }
    }

fault_kill:
    if (from_user) {
        serial_write("[PAGE FAULT] ring-3 fault — killing task\n");
        task_exit();   /* kills current task, scheduler picks next */
        return;
    }
    kernel_panic(0x0E, "Unhandled kernel page fault");
}
```

- [ ] **Step 2: Build and verify it compiles**

```bash
make clean && make 2>&1 | grep -E "error:|warning:"
```
Expected: no errors.

- [ ] **Step 3: Commit**

```bash
git add kernel/paging.c
git commit -m "fix: kill ring-3 task on page fault instead of kernel panic"
```

---

### Task 2: Add `task_create_user()` and `enter_userspace()` to scheduler

**Files:**
- Modify: `kernel/scheduler.c`

Add a `session_id` field to TCB, a user-space task trampoline, and a public `task_create_user()`.

- [ ] **Step 1: Add `session_id` to TCB struct**

In `kernel/scheduler.c`, in the `tcb_t` struct after `exit_code`:

```c
    int  exit_code;
    int  session_id;   /* which GUI session owns this task; -1 = kernel */
```

- [ ] **Step 2: Initialise `session_id` in `task_create()`**

In `task_create()`, after `t->is_user = false;`:
```c
    t->session_id = -1;
```

- [ ] **Step 3: Add `enter_userspace()` function**

Add after `task_trampoline()`:

```c
/* Switch from ring 0 into a ring-3 task for the first time.
   Never returns — iret lands in user code. */
static void __attribute__((noreturn)) enter_userspace(u32 entry, u32 user_esp) {
    __asm__ volatile (
        /* Load ring-3 data segments */
        "mov $0x23, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        /* Build iret frame: SS, ESP, EFLAGS, CS, EIP */
        "push $0x23\n"        /* SS  = ring-3 data */
        "push %1\n"           /* ESP = user stack top */
        "push $0x00000202\n"  /* EFLAGS: reserved bit + IF */
        "push $0x1B\n"        /* CS  = ring-3 code */
        "push %0\n"           /* EIP = entry point */
        "iret\n"
        :
        : "r"(entry), "r"(user_esp)
        : "memory"
    );
    __builtin_unreachable();
}
```

- [ ] **Step 4: Add ring-3 trampoline**

Add after `enter_userspace()`:

```c
static void __attribute__((noinline)) task_user_trampoline(void) {
    tcb_t *t = &tasks[current_task];
    enter_userspace(t->entry, (u32)t->ustack + TASK_STACK_SIZE);
}
```

- [ ] **Step 5: Add `task_create_user()` public function**

Add after `task_create()`:

```c
/*
 * task_create_user: create a ring-3 task.
 *   entry    – ELF virtual entry point
 *   page_dir – user page directory (from paging_create_dir())
 *   session  – owning session ID
 */
int task_create_user(const char *name, u32 entry, pde_t *page_dir, int session) {
    if (task_count >= MAX_TASKS) return -1;

    tcb_t *t = &tasks[task_count];
    kmemset(t, 0, sizeof(tcb_t));
    t->id              = task_count + 1;
    kstrncpy(t->name, name, 31);
    t->state           = TASK_READY;
    t->timeslice       = TIMESLICE_DEFAULT;
    t->ticks_remaining = TIMESLICE_DEFAULT;
    t->entry           = entry;
    t->is_user         = true;
    t->cr3             = (u32)page_dir;
    t->session_id      = session;

    /* Kernel stack for ring-0 re-entry (syscalls, interrupts) */
    t->kstack = (u8*)kmalloc(TASK_STACK_SIZE);
    if (!t->kstack) return -1;
    kmemset(t->kstack, 0, TASK_STACK_SIZE);

    /* User stack — allocated from kernel heap, mapped into user page dir */
    t->ustack = (u8*)kmalloc(TASK_STACK_SIZE);
    if (!t->ustack) { kfree(t->kstack); return -1; }
    kmemset(t->ustack, 0, TASK_STACK_SIZE);

    /* Map user stack at 0xBFF00000 in the user page directory */
    u32 ustack_virt = 0xBFF00000;
    for (u32 off = 0; off < TASK_STACK_SIZE; off += PAGE_SIZE) {
        u32 phys = (u32)(t->ustack + off);
        paging_map(page_dir, ustack_virt + off, phys, PTE_PRESENT | PTE_RW | PTE_USER);
    }

    /* Kernel stack initial frame — returns to task_user_trampoline */
    u32 *sp = (u32*)(t->kstack + TASK_STACK_SIZE);
    *--sp = (u32)task_user_trampoline;
    *--sp = 0x00000202;  /* EFLAGS */
    for (int i = 0; i < 8; i++) *--sp = 0;  /* pushal */
    t->esp = (u32)sp;

    task_count++;
    serial_write("[sched] user task created: "); serial_write(name); serial_write("\n");
    return (int)(task_count - 1);
}
```

- [ ] **Step 6: Declare `task_create_user` in `include/kernel.h`**

Find the existing `task_create` declaration and add after it:

```c
int  task_create_user(const char *name, u32 entry, pde_t *page_dir, int session);
```

- [ ] **Step 7: Build**

```bash
make clean && make 2>&1 | grep -E "error:"
```
Expected: no errors.

- [ ] **Step 8: Commit**

```bash
git add kernel/scheduler.c include/kernel.h
git commit -m "feat: add task_create_user() and enter_userspace() for ring-3 tasks"
```

---

### Task 3: Fix ELF loader to load into user page table

**Files:**
- Modify: `kernel/elf.c`

Add `elf_load_user()` which creates a user address space, maps ELF segments into it, and launches a ring-3 task.

- [ ] **Step 1: Add `elf_load_user()` after existing `elf_load_vfs()`**

```c
/*
 * elf_load_user: load an ELF32 binary into a fresh user address space.
 * Returns task ID on success, negative on error.
 */
int elf_load_user(fs_node_t *node, const char *name, int session) {
    if (!node || node->type != FS_FILE) return -1;

    const u8 *data = (const u8*)node->data;
    u32       size  = node->size;

    if (elf_validate(data, size) != 0) return -2;

    const elf32_ehdr_t *eh = (const elf32_ehdr_t*)data;

    /* Create a fresh user page directory (inherits kernel mappings) */
    pde_t *dir = paging_create_dir();
    if (!dir) return -3;

    /* Load each PT_LOAD segment into the user address space */
    for (u32 i = 0; i < eh->e_phnum; i++) {
        u32 ph_off = eh->e_phoff + i * eh->e_phentsize;
        if (ph_off + sizeof(elf32_phdr_t) > size) continue;
        const elf32_phdr_t *ph = (const elf32_phdr_t*)(data + ph_off);

        if (ph->p_type != PT_LOAD || ph->p_memsz == 0) continue;

        /* Allocate a kernel-heap buffer for this segment's data */
        u8 *buf = (u8*)kmalloc(ph->p_memsz);
        if (!buf) { paging_free_dir(dir); return -4; }
        kmemset(buf, 0, ph->p_memsz);
        if (ph->p_filesz > 0)
            kmemcpy(buf, data + ph->p_offset,
                    ph->p_filesz < ph->p_memsz ? ph->p_filesz : ph->p_memsz);

        /* Map into the user page directory, page by page */
        u32 pages = (ph->p_memsz + PAGE_SIZE - 1) / PAGE_SIZE;
        for (u32 p = 0; p < pages; p++) {
            u32 virt = (ph->p_vaddr & ~(PAGE_SIZE-1)) + p * PAGE_SIZE;
            u32 phys = (u32)(buf + p * PAGE_SIZE);
            u32 flags = PTE_PRESENT | PTE_USER |
                        ((ph->p_flags & PF_W) ? PTE_RW : 0);
            paging_map(dir, virt, phys, flags);
        }

        serial_write("[elf_user] mapped segment vaddr=0x");
        char tmp[12]; kutoa(ph->p_vaddr, tmp, 16); serial_write(tmp);
        serial_write("\n");
    }

    /* Launch as ring-3 task */
    int tid = task_create_user(name, eh->e_entry, dir, session);
    if (tid < 0) { paging_free_dir(dir); return -5; }

    serial_write("[elf_user] launched '"); serial_write(name);
    serial_write("' entry=0x");
    char tmp[12]; kutoa(eh->e_entry, tmp, 16); serial_write(tmp);
    serial_write("\n");
    return tid;
}
```

- [ ] **Step 2: Declare `elf_load_user` in `include/kernel.h`**

Add after the existing `elf_load_vfs` declaration:

```c
int elf_load_user(fs_node_t *node, const char *name, int session);
```

- [ ] **Step 3: Build**

```bash
make clean && make 2>&1 | grep -E "error:"
```
Expected: no errors.

- [ ] **Step 4: Commit**

```bash
git add kernel/elf.c include/kernel.h
git commit -m "feat: elf_load_user() maps ELF into user address space and launches ring-3 task"
```

---

### Task 4: Add `copy_from_user` / `copy_to_user` and fix `sys_brk`

**Files:**
- Modify: `kernel/syscall.c`

- [ ] **Step 1: Add bounds-checking helpers before the syscall dispatch table**

```c
/* Validate that a user pointer + length is within safe user-space range */
static bool user_ptr_ok(const void *ptr, u32 len) {
    u32 addr = (u32)ptr;
    /* User space: 0x400000 – 0xBFFFFFFF */
    return addr >= 0x400000 && addr + len <= 0xBFFFFFFF && addr + len > addr;
}

int copy_from_user(void *dst, const void *user_src, u32 len) {
    if (!user_ptr_ok(user_src, len)) return -EFAULT;
    kmemcpy(dst, user_src, len);
    return 0;
}

int copy_to_user(void *user_dst, const void *src, u32 len) {
    if (!user_ptr_ok(user_dst, len)) return -EFAULT;
    kmemcpy(user_dst, src, len);
    return 0;
}
```

- [ ] **Step 2: Declare in `include/kernel.h`**

```c
int copy_from_user(void *dst, const void *user_src, u32 len);
int copy_to_user(void *user_dst, const void *src, u32 len);
```

- [ ] **Step 3: Fix `sys_brk` to map real pages**

Replace the existing `sys_brk`:

```c
static i32 sys_brk(u32 addr) {
    task_t *t = task_current();
    tcb_t  *tcb = (tcb_t *)t;

    if (addr == 0) return (i32)user_brk;
    if (addr <= user_brk) return (i32)user_brk;

    /* Map new pages into the current process's page directory */
    u32 cur_page = (user_brk + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    u32 end_page = (addr    + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    for (u32 v = cur_page; v < end_page; v += PAGE_SIZE) {
        u32 frame = pmm_alloc_frame();
        if (frame == (u32)~0u) return -(i32)ENOMEM;
        /* Use the current task's page directory (cr3 field) */
        pde_t *dir = (pde_t *)tcb->cr3;
        if (!dir) dir = (pde_t *)0; /* kernel dir fallback */
        if (dir)
            paging_map(dir, v, frame * PAGE_SIZE,
                       PTE_PRESENT | PTE_RW | PTE_USER);
    }
    user_brk = addr;
    return (i32)user_brk;
}
```

- [ ] **Step 4: Add user-pointer validation to `sys_read` and `sys_write`**

In `sys_read`, before `if (fd == FD_STDIN)`:
```c
    if (!user_ptr_ok(buf, count)) return -(i32)EFAULT;
```

In `sys_write`, before writing:
```c
    if (fd != FD_STDOUT && fd != FD_STDERR && !user_ptr_ok(buf, count))
        return -(i32)EFAULT;
```

- [ ] **Step 5: Build**

```bash
make clean && make 2>&1 | grep -E "error:"
```

- [ ] **Step 6: Commit**

```bash
git add kernel/syscall.c include/kernel.h
git commit -m "feat: add copy_from/to_user bounds checks, fix sys_brk to map real pages"
```

---

### Task 5: Write and run the ring-3 smoke test ELF

**Files:**
- Create: `tests/ring3_exit.asm`
- Create: `tests/ring3_fault.asm`
- Modify: `Makefile`

- [ ] **Step 1: Write the exit test**

Create `tests/ring3_exit.asm`:

```nasm
; Minimal ring-3 ELF: calls exit(0) via int 0x80
; Link with: ld -m elf_i386 -Ttext 0x400000 -o ring3_exit ring3_exit.o
BITS 32
SECTION .text
GLOBAL _start
_start:
    mov eax, 1      ; sys_exit
    xor ebx, ebx   ; status = 0
    int 0x80
```

- [ ] **Step 2: Write the fault test**

Create `tests/ring3_fault.asm`:

```nasm
; Ring-3 ELF: dereferences null — kernel should survive
BITS 32
SECTION .text
GLOBAL _start
_start:
    xor eax, eax
    mov dword [eax], 42   ; null deref — triggers page fault
    mov eax, 1
    xor ebx, ebx
    int 0x80
```

- [ ] **Step 3: Add build rules to Makefile**

```makefile
tests/ring3_exit: tests/ring3_exit.asm
	nasm -f elf32 -o tests/ring3_exit.o tests/ring3_exit.asm
	ld -m elf_i386 -Ttext 0x400000 -o tests/ring3_exit tests/ring3_exit.o

tests/ring3_fault: tests/ring3_fault.asm
	nasm -f elf32 -o tests/ring3_fault.o tests/ring3_fault.asm
	ld -m elf_i386 -Ttext 0x400000 -o tests/ring3_fault tests/ring3_fault.o

test-elfs: tests/ring3_exit tests/ring3_fault
```

- [ ] **Step 4: Build the test ELFs**

```bash
make test-elfs
file tests/ring3_exit
```
Expected: `ELF 32-bit LSB executable, Intel 80386, statically linked`

- [ ] **Step 5: Load the exit ELF via kernel init and run QEMU**

In `kernel/kernel.c` in the boot sequence (after VFS init), temporarily add:

```c
/* TEMP: ring-3 smoke test — remove after Phase 1 gate passes */
{
    fs_node_t *n = vfs_create_file(vfs_root(), "ring3_exit");
    /* Copy the ELF binary into the VFS node — in practice, load from disk.
       For testing, embed it: extern u8 _binary_tests_ring3_exit_start[];
       n->data = _binary_tests_ring3_exit_start;
       n->size = (u32)_binary_tests_ring3_exit_end - (u32)_binary_tests_ring3_exit_start; */
    int tid = elf_load_user(n, "ring3_exit", -1);
    serial_write(tid >= 0 ? "[test] ring3_exit launched OK\n"
                          : "[test] ring3_exit launch FAILED\n");
}
```

Add to `Makefile` OBJS:
```makefile
OBJS += tests/ring3_exit.bin.o
```

And add a rule to convert the ELF to a linkable object:
```makefile
tests/ring3_exit.bin.o: tests/ring3_exit
	objcopy -I binary -O elf32-i386 -B i386 $< $@
```

- [ ] **Step 6: Run QEMU and check serial output**

```bash
make run
```

Check QEMU serial output (or `make run-serial`). Expected:
```
[elf_user] mapped segment vaddr=0x400000
[elf_user] launched 'ring3_exit' entry=0x400000
[sched] user task created: ring3_exit
[syscall] exit(0)
```
**No kernel panic.**

- [ ] **Step 7: Swap in the fault test, verify kernel survives**

Replace `ring3_exit` with `ring3_fault` in the kernel init test. Run QEMU.

Expected serial output:
```
[PAGE FAULT] addr=0x00000000 err=0x07
[PAGE FAULT] ring-3 fault — killing task
```
**Kernel keeps running** (no freeze, desktop appears).

- [ ] **Step 8: Remove the test code from kernel.c, commit**

```bash
git add tests/ Makefile kernel/kernel.c
git commit -m "test: ring-3 exit and page-fault smoke tests — Phase 1 gate passed"
```

**--- PHASE 1 GATE: ring-3 process exits cleanly; null deref kills only the process, kernel continues. ---**

---

## Phase 2 — ext2 Filesystem

### Task 6: Grow disk and add format target to Makefile

**Files:**
- Modify: `Makefile`

- [ ] **Step 1: Update disk targets in Makefile**

Find the existing `reset-disk` or disk image target. Replace/add:

```makefile
DISK_IMG = careos.img
DISK_MB  = 512

format-disk:
	dd if=/dev/zero of=$(DISK_IMG) bs=1M count=$(DISK_MB) status=progress
	mkfs.ext2 -b 1024 -I 128 -L "CareOS" $(DISK_IMG)
	@echo "Disk formatted: $(DISK_MB)MB ext2"

reset-disk: format-disk
```

- [ ] **Step 2: Run format-disk**

```bash
make format-disk
```
Expected output ends with: `Disk formatted: 512MB ext2`

- [ ] **Step 3: Verify**

```bash
e2fsck -fn careos.img 2>&1 | tail -5
```
Expected: `careos.img: clean, N/N files, N/N blocks`

- [ ] **Step 4: Commit**

```bash
git add Makefile
git commit -m "build: grow disk to 512MB ext2, add format-disk target"
```

---

### Task 7: Write ext2 structs and mount

**Files:**
- Create: `kernel/ext2.h`
- Create: `kernel/ext2.c` (first half — superblock and block reading)

- [ ] **Step 1: Create `kernel/ext2.h`**

```c
#pragma once
#include "kernel.h"

/* ext2 magic number (bytes 56-57 of superblock) */
#define EXT2_MAGIC       0xEF53u
#define EXT2_ROOT_INODE  2u

/* File type constants (inode->mode & 0xF000) */
#define EXT2_S_IFREG  0x8000u
#define EXT2_S_IFDIR  0x4000u

/* Directory entry file_type field */
#define EXT2_FT_REG_FILE 1
#define EXT2_FT_DIR      2

/* ── On-disk structures ───────────────────────────────────────────────── */

typedef struct {
    u32 s_inodes_count;
    u32 s_blocks_count;
    u32 s_r_blocks_count;
    u32 s_free_blocks_count;
    u32 s_free_inodes_count;
    u32 s_first_data_block;    /* 0 for 1KB blocks, 1 for others? No — 1 for 1KB */
    u32 s_log_block_size;      /* block_size = 1024 << s_log_block_size */
    u32 s_log_frag_size;
    u32 s_blocks_per_group;
    u32 s_frags_per_group;
    u32 s_inodes_per_group;
    u32 s_mtime, s_wtime;
    u16 s_mnt_count, s_max_mnt_count;
    u16 s_magic;               /* must be EXT2_MAGIC */
    u16 s_state;
    u16 s_errors;
    u16 s_minor_rev_level;
    u32 s_lastcheck, s_checkinterval;
    u32 s_creator_os;
    u32 s_rev_level;
    u16 s_def_resuid, s_def_resgid;
    /* EXT2_DYNAMIC_REV fields */
    u32 s_first_ino;
    u16 s_inode_size;
    u16 s_block_group_nr;
    u32 s_feature_compat, s_feature_incompat, s_feature_ro_compat;
    u8  s_uuid[16];
    u8  s_volume_name[16];
    u8  s_last_mounted[64];
    u32 s_algo_bitmap;
    /* padding to 1024 bytes */
    u8  _pad[820];
} __attribute__((packed)) ext2_superblock_t;

typedef struct {
    u32 bg_block_bitmap;
    u32 bg_inode_bitmap;
    u32 bg_inode_table;
    u16 bg_free_blocks_count;
    u16 bg_free_inodes_count;
    u16 bg_used_dirs_count;
    u16 bg_pad;
    u8  bg_reserved[12];
} __attribute__((packed)) ext2_bgd_t;

typedef struct {
    u16 i_mode;
    u16 i_uid;
    u32 i_size;
    u32 i_atime, i_ctime, i_mtime, i_dtime;
    u16 i_gid;
    u16 i_links_count;
    u32 i_blocks;    /* count in 512-byte units */
    u32 i_flags;
    u32 i_osd1;
    u32 i_block[15]; /* 0-11 direct, 12 indirect, 13 double, 14 triple */
    u32 i_generation;
    u32 i_file_acl, i_dir_acl;
    u32 i_faddr;
    u8  i_osd2[12];
} __attribute__((packed)) ext2_inode_t;

typedef struct {
    u32 de_inode;
    u16 de_rec_len;
    u8  de_name_len;
    u8  de_file_type;
    char de_name[];   /* not null-terminated in spec; name_len gives length */
} __attribute__((packed)) ext2_dirent_t;

/* ── Public API ──────────────────────────────────────────────────────── */

int  ext2_mount(void);   /* call after ata_init(); returns 0 on success */

/* Read inode number `ino` into `out`. Returns 0 on success. */
int  ext2_read_inode(u32 ino, ext2_inode_t *out);

/* Read `len` bytes from inode `ino` at offset `off` into `buf`. */
int  ext2_read_data(const ext2_inode_t *ino, u32 off, void *buf, u32 len);

/* Look up `name` in directory inode `dir_ino`. Returns inode number or 0. */
u32  ext2_lookup(u32 dir_ino, const char *name);

/* Walk an absolute path. Returns inode number or 0 if not found. */
u32  ext2_path_to_inode(const char *path);

/* Write len bytes to inode; allocates blocks as needed. Returns 0 on success. */
int  ext2_write_data(u32 ino_num, u32 off, const void *buf, u32 len);

/* Create a regular file in parent directory. Returns new inode number or 0. */
u32  ext2_create_file(u32 parent_ino, const char *name);

/* Create a directory in parent. Returns new inode number or 0. */
u32  ext2_mkdir(u32 parent_ino, const char *name);

/* Delete a directory entry (does not free inode if links_count > 0). */
int  ext2_unlink(u32 parent_ino, const char *name);
```

- [ ] **Step 2: Create `kernel/ext2.c` — mount and block reader**

```c
#include "kernel.h"

/* ── State ─────────────────────────────────────────────────────────────── */
static ext2_superblock_t sb;
static u32 block_size;          /* bytes per block */
static u32 sectors_per_block;   /* ATA sectors per ext2 block */
static u32 inodes_per_group;
static u32 blocks_per_group;
static u32 num_groups;
static bool ext2_ready = false;

/* ── ATA sector helpers ────────────────────────────────────────────────── */
/* Read one or more ATA sectors into buf. lba is 0-based 512-byte sector. */
static int read_sectors(u32 lba, u32 count, void *buf) {
    for (u32 i = 0; i < count; i++) {
        if (ata_read_sector(lba + i, (u8*)buf + i * 512) != 0)
            return -1;
    }
    return 0;
}

/* Read one ext2 block (block_size bytes) into buf */
static int read_block(u32 block_num, void *buf) {
    u32 lba = block_num * sectors_per_block;
    return read_sectors(lba, sectors_per_block, buf);
}

/* ── Mount ─────────────────────────────────────────────────────────────── */
int ext2_mount(void) {
    /* Superblock starts at byte 1024 = LBA 2 (512-byte sectors) */
    if (read_sectors(2, 2, &sb) != 0) {
        serial_write("[ext2] failed to read superblock\n"); return -1;
    }
    if (sb.s_magic != EXT2_MAGIC) {
        serial_write("[ext2] bad magic — run make format-disk\n"); return -2;
    }

    block_size       = 1024u << sb.s_log_block_size;
    sectors_per_block = block_size / 512;
    inodes_per_group  = sb.s_inodes_per_group;
    blocks_per_group  = sb.s_blocks_per_group;
    num_groups        = (sb.s_blocks_count + blocks_per_group - 1) / blocks_per_group;
    ext2_ready        = true;

    serial_write("[ext2] mounted, block_size=");
    char buf[12]; kutoa(block_size, buf, 10); serial_write(buf);
    serial_write(" groups="); kutoa(num_groups, buf, 10); serial_write(buf);
    serial_write("\n");
    return 0;
}

/* ── Block Group Descriptor ────────────────────────────────────────────── */
static int read_bgd(u32 group, ext2_bgd_t *out) {
    /* BGD table starts at block (s_first_data_block + 1) */
    u32 bgdt_block = sb.s_first_data_block + 1;
    u8  *bgdt = (u8*)kmalloc(block_size);
    if (!bgdt) return -1;
    if (read_block(bgdt_block, bgdt) != 0) { kfree(bgdt); return -2; }
    kmemcpy(out, bgdt + group * sizeof(ext2_bgd_t), sizeof(ext2_bgd_t));
    kfree(bgdt);
    return 0;
}

/* ── Inode reader ──────────────────────────────────────────────────────── */
int ext2_read_inode(u32 ino, ext2_inode_t *out) {
    if (!ext2_ready || ino == 0) return -1;

    u32 group  = (ino - 1) / inodes_per_group;
    u32 index  = (ino - 1) % inodes_per_group;

    ext2_bgd_t bgd;
    if (read_bgd(group, &bgd) != 0) return -2;

    u32 inode_size  = sb.s_inode_size ? sb.s_inode_size : 128;
    u32 inodes_per_block = block_size / inode_size;
    u32 block_num   = bgd.bg_inode_table + index / inodes_per_block;
    u32 block_off   = (index % inodes_per_block) * inode_size;

    u8 *blk = (u8*)kmalloc(block_size);
    if (!blk) return -3;
    if (read_block(block_num, blk) != 0) { kfree(blk); return -4; }

    kmemcpy(out, blk + block_off, sizeof(ext2_inode_t));
    kfree(blk);
    return 0;
}

/* ── Block pointer resolution (handles indirect blocks) ─────────────────── */
/* Returns the physical block number for logical block `logical` of inode */
static u32 resolve_block(const ext2_inode_t *ino, u32 logical) {
    u32 ptrs_per_block = block_size / 4;

    if (logical < 12) {
        return ino->i_block[logical];  /* direct */
    }
    logical -= 12;

    if (logical < ptrs_per_block) {
        /* Single indirect */
        u32 *ind = (u32*)kmalloc(block_size);
        if (!ind) return 0;
        read_block(ino->i_block[12], ind);
        u32 result = ind[logical];
        kfree(ind);
        return result;
    }
    logical -= ptrs_per_block;

    if (logical < ptrs_per_block * ptrs_per_block) {
        /* Double indirect */
        u32 *dind = (u32*)kmalloc(block_size);
        u32 *ind  = (u32*)kmalloc(block_size);
        if (!dind || !ind) { kfree(dind); kfree(ind); return 0; }
        read_block(ino->i_block[13], dind);
        read_block(dind[logical / ptrs_per_block], ind);
        u32 result = ind[logical % ptrs_per_block];
        kfree(dind); kfree(ind);
        return result;
    }

    serial_write("[ext2] triple indirect not implemented\n");
    return 0;
}

/* ── Data reader ──────────────────────────────────────────────────────── */
int ext2_read_data(const ext2_inode_t *ino, u32 off, void *buf, u32 len) {
    if (!ext2_ready || !buf) return -1;
    if (off >= ino->i_size) return 0;
    if (off + len > ino->i_size) len = ino->i_size - off;

    u8 *blk = (u8*)kmalloc(block_size);
    if (!blk) return -2;

    u32 written = 0;
    while (written < len) {
        u32 logical    = (off + written) / block_size;
        u32 block_off  = (off + written) % block_size;
        u32 chunk      = block_size - block_off;
        if (chunk > len - written) chunk = len - written;

        u32 phys_block = resolve_block(ino, logical);
        if (phys_block == 0 || read_block(phys_block, blk) != 0) {
            kfree(blk); return -3;
        }
        kmemcpy((u8*)buf + written, blk + block_off, chunk);
        written += chunk;
    }
    kfree(blk);
    return (int)written;
}
```

- [ ] **Step 3: Build**

```bash
make clean && make 2>&1 | grep -E "error:"
```

- [ ] **Step 4: Commit**

```bash
git add kernel/ext2.h kernel/ext2.c
git commit -m "feat: ext2 mount, inode reader, block resolver, data reader"
```

---

### Task 8: ext2 directory lookup and path resolution

**Files:**
- Modify: `kernel/ext2.c`

- [ ] **Step 1: Add `ext2_lookup()` and `ext2_path_to_inode()`**

Append to `kernel/ext2.c`:

```c
/* ── Directory lookup ─────────────────────────────────────────────────── */
u32 ext2_lookup(u32 dir_ino, const char *name) {
    ext2_inode_t inode;
    if (ext2_read_inode(dir_ino, &inode) != 0) return 0;
    if ((inode.i_mode & 0xF000) != EXT2_S_IFDIR) return 0;

    u32 name_len = kstrlen(name);
    u8 *buf = (u8*)kmalloc(block_size);
    if (!buf) return 0;

    u32 off = 0;
    while (off < inode.i_size) {
        u32 logical = off / block_size;
        u32 phys    = resolve_block(&inode, logical);
        if (phys == 0 || read_block(phys, buf) != 0) break;

        u32 blk_off = off % block_size;
        while (blk_off < block_size) {
            ext2_dirent_t *de = (ext2_dirent_t*)(buf + blk_off);
            if (de->de_rec_len == 0) break;
            if (de->de_inode != 0 && de->de_name_len == name_len &&
                kmemcmp(de->de_name, name, name_len) == 0) {
                u32 result = de->de_inode;
                kfree(buf);
                return result;
            }
            blk_off += de->de_rec_len;
        }
        off += block_size;
    }
    kfree(buf);
    return 0;
}

u32 ext2_path_to_inode(const char *path) {
    if (!path || path[0] != '/') return 0;
    u32 ino = EXT2_ROOT_INODE;

    char component[256];
    const char *p = path + 1;  /* skip leading '/' */

    while (*p) {
        /* Extract next component */
        u32 i = 0;
        while (*p && *p != '/') component[i++] = *p++;
        component[i] = '\0';
        if (*p == '/') p++;
        if (i == 0) continue;  /* trailing slash */

        ino = ext2_lookup(ino, component);
        if (ino == 0) return 0;  /* not found */
    }
    return ino;
}
```

- [ ] **Step 2: Add a kernel-boot self-test (temporary)**

In `kernel/kernel.c`, after `ext2_mount()`:

```c
/* TEMP: ext2 read test */
u32 root_ino = ext2_path_to_inode("/");
serial_write(root_ino == 2 ? "[ext2 test] root inode OK\n"
                           : "[ext2 test] root inode FAILED\n");
u32 lost_ino = ext2_path_to_inode("/lost+found");
serial_write(lost_ino != 0 ? "[ext2 test] /lost+found found OK\n"
                           : "[ext2 test] /lost+found NOT found\n");
```

- [ ] **Step 3: Build and run**

```bash
make clean && make && make run
```

Check serial: `[ext2 test] root inode OK` and `[ext2 test] /lost+found found OK`.

- [ ] **Step 4: Remove temp test, commit**

```bash
git add kernel/ext2.c kernel/kernel.c
git commit -m "feat: ext2 directory lookup and path resolution"
```

---

### Task 9: ext2 write path — block allocator and file/dir creation

**Files:**
- Modify: `kernel/ext2.c`

- [ ] **Step 1: Add write-sector helper and block allocator**

```c
static int write_sectors(u32 lba, u32 count, const void *buf) {
    for (u32 i = 0; i < count; i++) {
        if (ata_write_sector(lba + i, (const u8*)buf + i * 512) != 0)
            return -1;
    }
    return 0;
}

static int write_block(u32 block_num, const void *buf) {
    return write_sectors(block_num * sectors_per_block, sectors_per_block, buf);
}

/* Allocate a free block. Returns block number or 0 on failure. */
static u32 alloc_block(void) {
    u8 *bitmap = (u8*)kmalloc(block_size);
    if (!bitmap) return 0;

    for (u32 g = 0; g < num_groups; g++) {
        ext2_bgd_t bgd;
        if (read_bgd(g, &bgd) != 0) continue;
        if (bgd.bg_free_blocks_count == 0) continue;

        read_block(bgd.bg_block_bitmap, bitmap);
        for (u32 byte = 0; byte < block_size; byte++) {
            if (bitmap[byte] == 0xFF) continue;
            for (u32 bit = 0; bit < 8; bit++) {
                if (!(bitmap[byte] & (1u << bit))) {
                    bitmap[byte] |= (1u << bit);
                    write_block(bgd.bg_block_bitmap, bitmap);
                    /* Update BGD free count in superblock area */
                    bgd.bg_free_blocks_count--;
                    sb.s_free_blocks_count--;
                    /* Write updated BGD back */
                    u8 *bgdt_blk = (u8*)kmalloc(block_size);
                    if (bgdt_blk) {
                        read_block(sb.s_first_data_block + 1, bgdt_blk);
                        kmemcpy(bgdt_blk + g * sizeof(ext2_bgd_t), &bgd, sizeof(bgd));
                        write_block(sb.s_first_data_block + 1, bgdt_blk);
                        kfree(bgdt_blk);
                    }
                    kfree(bitmap);
                    return g * blocks_per_group + byte * 8 + bit;
                }
            }
        }
    }
    kfree(bitmap);
    return 0;  /* disk full */
}

/* Allocate a free inode. Returns inode number or 0 on failure. */
static u32 alloc_inode(void) {
    u8 *bitmap = (u8*)kmalloc(block_size);
    if (!bitmap) return 0;

    for (u32 g = 0; g < num_groups; g++) {
        ext2_bgd_t bgd;
        if (read_bgd(g, &bgd) != 0) continue;
        if (bgd.bg_free_inodes_count == 0) continue;

        read_block(bgd.bg_inode_bitmap, bitmap);
        for (u32 byte = 0; byte < block_size; byte++) {
            if (bitmap[byte] == 0xFF) continue;
            for (u32 bit = 0; bit < 8; bit++) {
                if (!(bitmap[byte] & (1u << bit))) {
                    bitmap[byte] |= (1u << bit);
                    write_block(bgd.bg_inode_bitmap, bitmap);
                    bgd.bg_free_inodes_count--;
                    sb.s_free_inodes_count--;
                    u8 *bgdt_blk = (u8*)kmalloc(block_size);
                    if (bgdt_blk) {
                        read_block(sb.s_first_data_block + 1, bgdt_blk);
                        kmemcpy(bgdt_blk + g * sizeof(ext2_bgd_t), &bgd, sizeof(bgd));
                        write_block(sb.s_first_data_block + 1, bgdt_blk);
                        kfree(bgdt_blk);
                    }
                    kfree(bitmap);
                    /* Inode numbers are 1-based; index in group is 0-based */
                    return g * inodes_per_group + byte * 8 + bit + 1;
                }
            }
        }
    }
    kfree(bitmap);
    return 0;
}
```

- [ ] **Step 2: Add inode writer**

```c
static int write_inode(u32 ino, const ext2_inode_t *src) {
    u32 group  = (ino - 1) / inodes_per_group;
    u32 index  = (ino - 1) % inodes_per_group;
    ext2_bgd_t bgd;
    if (read_bgd(group, &bgd) != 0) return -1;

    u32 inode_size       = sb.s_inode_size ? sb.s_inode_size : 128;
    u32 inodes_per_block = block_size / inode_size;
    u32 block_num        = bgd.bg_inode_table + index / inodes_per_block;
    u32 block_off        = (index % inodes_per_block) * inode_size;

    u8 *blk = (u8*)kmalloc(block_size);
    if (!blk) return -2;
    if (read_block(block_num, blk) != 0) { kfree(blk); return -3; }
    kmemcpy(blk + block_off, src, sizeof(ext2_inode_t));
    int r = write_block(block_num, blk);
    kfree(blk);
    return r;
}
```

- [ ] **Step 3: Add `ext2_write_data()`**

```c
int ext2_write_data(u32 ino_num, u32 off, const void *buf, u32 len) {
    if (!ext2_ready) return -1;
    ext2_inode_t inode;
    if (ext2_read_inode(ino_num, &inode) != 0) return -2;

    u8 *blk = (u8*)kmalloc(block_size);
    if (!blk) return -3;

    u32 written = 0;
    while (written < len) {
        u32 logical   = (off + written) / block_size;
        u32 blk_off   = (off + written) % block_size;
        u32 chunk     = block_size - blk_off;
        if (chunk > len - written) chunk = len - written;

        u32 phys = resolve_block(&inode, logical);
        if (phys == 0) {
            /* Allocate a new block */
            phys = alloc_block();
            if (phys == 0) { kfree(blk); return -4; }
            /* Zero the new block */
            kmemset(blk, 0, block_size);
            write_block(phys, blk);
            /* Store in inode direct blocks (simplified: direct only) */
            if (logical < 12) {
                inode.i_block[logical] = phys;
                inode.i_blocks += sectors_per_block;
            } else {
                kfree(blk); return -5; /* indirect not implemented in write path */
            }
        }

        if (read_block(phys, blk) != 0) { kfree(blk); return -6; }
        kmemcpy(blk + blk_off, (const u8*)buf + written, chunk);
        if (write_block(phys, blk) != 0) { kfree(blk); return -7; }
        written += chunk;
    }

    /* Update inode size */
    if (off + len > inode.i_size) inode.i_size = off + len;
    write_inode(ino_num, &inode);

    kfree(blk);
    return (int)written;
}
```

- [ ] **Step 4: Add `ext2_create_file()` and `ext2_mkdir()`**

```c
/* Add a directory entry to parent directory */
static int add_dirent(u32 parent_ino, u32 child_ino,
                      const char *name, u8 file_type) {
    ext2_inode_t parent;
    if (ext2_read_inode(parent_ino, &parent) != 0) return -1;

    u32 name_len  = kstrlen(name);
    u32 need_len  = (sizeof(ext2_dirent_t) + name_len + 3) & ~3u;

    u8 *blk = (u8*)kmalloc(block_size);
    if (!blk) return -2;

    /* Walk blocks of the parent directory looking for free space */
    u32 logical = 0;
    while (logical * block_size < parent.i_size) {
        u32 phys = resolve_block(&parent, logical);
        if (phys == 0) break;
        read_block(phys, blk);

        u32 pos = 0;
        while (pos < block_size) {
            ext2_dirent_t *de = (ext2_dirent_t*)(blk + pos);
            if (de->de_rec_len == 0) break;
            u32 real_len = (sizeof(ext2_dirent_t) + de->de_name_len + 3) & ~3u;
            u32 slack    = de->de_rec_len - real_len;
            if (de->de_inode == 0) {
                /* Empty slot — use it */
                if (de->de_rec_len >= need_len) {
                    de->de_inode     = child_ino;
                    de->de_name_len  = (u8)name_len;
                    de->de_file_type = file_type;
                    kmemcpy(de->de_name, name, name_len);
                    write_block(phys, blk);
                    kfree(blk); return 0;
                }
            } else if (slack >= need_len) {
                /* Split the record */
                ext2_dirent_t *new_de = (ext2_dirent_t*)(blk + pos + real_len);
                new_de->de_inode     = child_ino;
                new_de->de_rec_len   = (u16)slack;
                new_de->de_name_len  = (u8)name_len;
                new_de->de_file_type = file_type;
                kmemcpy(new_de->de_name, name, name_len);
                de->de_rec_len = (u16)real_len;
                write_block(phys, blk);
                kfree(blk); return 0;
            }
            pos += de->de_rec_len;
        }
        logical++;
    }

    /* Need a new block for the directory */
    u32 new_block = alloc_block();
    if (new_block == 0) { kfree(blk); return -3; }
    kmemset(blk, 0, block_size);
    ext2_dirent_t *de = (ext2_dirent_t*)blk;
    de->de_inode     = child_ino;
    de->de_rec_len   = (u16)block_size;
    de->de_name_len  = (u8)name_len;
    de->de_file_type = file_type;
    kmemcpy(de->de_name, name, name_len);
    write_block(new_block, blk);

    parent.i_block[logical] = new_block;
    parent.i_size += block_size;
    parent.i_blocks += sectors_per_block;
    write_inode(parent_ino, &parent);
    kfree(blk);
    return 0;
}

u32 ext2_create_file(u32 parent_ino, const char *name) {
    u32 ino = alloc_inode();
    if (ino == 0) return 0;

    ext2_inode_t inode;
    kmemset(&inode, 0, sizeof(inode));
    inode.i_mode       = EXT2_S_IFREG | 0644;
    inode.i_links_count = 1;
    if (write_inode(ino, &inode) != 0) return 0;
    if (add_dirent(parent_ino, ino, name, EXT2_FT_REG_FILE) != 0) return 0;
    return ino;
}

u32 ext2_mkdir(u32 parent_ino, const char *name) {
    u32 ino = alloc_inode();
    if (ino == 0) return 0;

    u32 new_block = alloc_block();
    if (new_block == 0) return 0;

    /* Write . and .. entries */
    u8 *blk = (u8*)kmalloc(block_size);
    if (!blk) return 0;
    kmemset(blk, 0, block_size);

    ext2_dirent_t *dot = (ext2_dirent_t*)blk;
    dot->de_inode = ino; dot->de_rec_len = 12;
    dot->de_name_len = 1; dot->de_file_type = EXT2_FT_DIR;
    dot->de_name[0] = '.';

    ext2_dirent_t *dotdot = (ext2_dirent_t*)(blk + 12);
    dotdot->de_inode = parent_ino;
    dotdot->de_rec_len = (u16)(block_size - 12);
    dotdot->de_name_len = 2; dotdot->de_file_type = EXT2_FT_DIR;
    dotdot->de_name[0] = '.'; dotdot->de_name[1] = '.';
    write_block(new_block, blk);
    kfree(blk);

    ext2_inode_t inode;
    kmemset(&inode, 0, sizeof(inode));
    inode.i_mode        = EXT2_S_IFDIR | 0755;
    inode.i_links_count = 2;  /* self + parent ref */
    inode.i_size        = block_size;
    inode.i_blocks      = sectors_per_block;
    inode.i_block[0]    = new_block;
    write_inode(ino, &inode);

    add_dirent(parent_ino, ino, name, EXT2_FT_DIR);
    return ino;
}
```

- [ ] **Step 5: Build**

```bash
make clean && make 2>&1 | grep -E "error:"
```

- [ ] **Step 6: Commit**

```bash
git add kernel/ext2.c
git commit -m "feat: ext2 write path — block/inode allocator, write_data, create_file, mkdir"
```

---

### Task 10: Wire VFS to ext2 backend

**Files:**
- Modify: `kernel/vfs.c`
- Modify: `kernel/kernel.c`
- Modify: `kernel/users.c`

The VFS keeps its function signatures. We add ext2 implementations that are called when the in-memory node is not found.

- [ ] **Step 1: Add ext2_mount() call in kernel.c stage 2**

In `kernel/kernel.c`, after the `ata_init()` call (stage 2):

```c
if (ext2_mount() != 0) {
    serial_write("[kernel] WARNING: ext2 mount failed — run make format-disk\n");
}
```

- [ ] **Step 2: Replace VFS read/write to use ext2 for /home paths**

In `kernel/vfs.c`, add at the top:

```c
/* ext2-backed read: used when node is backed by disk */
static i32 vfs_ext2_read(fs_node_t *node, u32 off, u32 len, u8 *buf) {
    ext2_inode_t ino;
    if (ext2_read_inode(node->inode_num, &ino) != 0) return -1;
    return ext2_read_data(&ino, off, buf, len);
}

static i32 vfs_ext2_write(fs_node_t *node, u32 off, u32 len, const u8 *buf) {
    return ext2_write_data(node->inode_num, off, buf, len);
}
```

Add `inode_num` field to `fs_node_t` in `include/kernel.h`:
```c
    u32 inode_num;   /* ext2 inode number; 0 = in-memory only */
```

In `vfs_read()` and `vfs_write()`, add dispatch at the start:
```c
fs_node_t *vfs_read_node(fs_node_t *node, u32 off, u32 len, u8 *buf) {
    if (node->inode_num != 0)
        return (fs_node_t*)(intptr_t)vfs_ext2_read(node, off, len, buf);
    /* existing in-memory implementation follows */
```

- [ ] **Step 3: Fix users.c to create /home/username on ext2 at first login**

In `kernel/users.c`, in the login success path, after setting `current_user`:

```c
/* Ensure /home/<username> exists on ext2 */
char home_path[64];
kstrcpy(home_path, "/home/");
kstrcat(home_path, user->username);

u32 home_ino = ext2_path_to_inode(home_path);
if (home_ino == 0) {
    u32 home_dir = ext2_path_to_inode("/home");
    if (home_dir == 0) {
        /* Create /home first */
        home_dir = ext2_mkdir(EXT2_ROOT_INODE, "home");
    }
    if (home_dir != 0) {
        ext2_mkdir(home_dir, user->username);
        serial_write("[users] created home dir for ");
        serial_write(user->username); serial_write("\n");
    }
}
```

- [ ] **Step 4: Run e2fsck validation after a test session**

```bash
make run
# Log in as 'user', open terminal, type some commands, log out
# Then in WSL:
e2fsck -fn careos.img
```
Expected: `clean, N/N files, N/N blocks` — no errors.

- [ ] **Step 5: Commit**

```bash
git add kernel/vfs.c kernel/kernel.c kernel/users.c include/kernel.h
git commit -m "feat: wire VFS to ext2 backend, create /home/<user> on first login"
```

**--- PHASE 2 GATE: e2fsck reports clean after a full login/file-ops session. ---**

---

## Phase 3 — mbedTLS + HTTPS

### Task 11: Vendor mbedTLS and write kernel config

**Files:**
- Create: `net/mbedtls/` (vendored)
- Create: `net/mbedtls/config.h`
- Modify: `Makefile`

- [ ] **Step 1: Download mbedTLS 3.x into the source tree**

In WSL:
```bash
cd net/
curl -L https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.1/mbedtls-3.6.1.tar.bz2 | tar xj
mv mbedtls-3.6.1/library mbedtls
mv mbedtls-3.6.1/include/mbedtls mbedtls/include
rm -rf mbedtls-3.6.1
```

Expected directory: `net/mbedtls/` with `*.c` source files and `net/mbedtls/include/mbedtls/*.h`

- [ ] **Step 2: Create `net/mbedtls/config.h` — stripped for kernel use**

```c
#pragma once

/* Disable features that require an OS, libc, or filesystem */
#define MBEDTLS_PLATFORM_MEMORY          /* use our kmalloc/kfree */
#define MBEDTLS_NO_PLATFORM_ENTROPY      /* we provide our own entropy */
#undef  MBEDTLS_FS_IO                   /* no file I/O */
#undef  MBEDTLS_NET_C                   /* we provide our own net callbacks */
#undef  MBEDTLS_TIMING_C                /* no OS timer */

/* Keep TLS 1.2 support */
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_PROTO_TLS1_2

/* Cipher suites we need */
#define MBEDTLS_AES_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_SHA1_C
#define MBEDTLS_RSA_C
#define MBEDTLS_BIGNUM_C
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C
#define MBEDTLS_X509_USE_C
#define MBEDTLS_X509_CRT_PARSE_C
#define MBEDTLS_CERTS_C

/* Entropy and RNG */
#define MBEDTLS_ENTROPY_C
#define MBEDTLS_CTR_DRBG_C

/* Required support */
#define MBEDTLS_OID_C
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C
#define MBEDTLS_BASE64_C
#define MBEDTLS_PEM_PARSE_C
#define MBEDTLS_MD_C
#define MBEDTLS_CIPHER_C

/* Platform: map mbedTLS alloc to our heap */
#define MBEDTLS_PLATFORM_C
#define MBEDTLS_PLATFORM_CALLOC_MACRO   kcalloc
#define MBEDTLS_PLATFORM_FREE_MACRO     kfree
```

- [ ] **Step 3: Add mbedTLS to Makefile**

```makefile
MBEDTLS_SRCS := $(wildcard net/mbedtls/*.c)
MBEDTLS_OBJS := $(MBEDTLS_SRCS:.c=.o)

CFLAGS += -Inet/mbedtls/include -DMBEDTLS_CONFIG_FILE='"net/mbedtls/config.h"'

OBJS += $(MBEDTLS_OBJS) net/tls.o net/ca_bundle.o
```

- [ ] **Step 4: Build (will have errors — fix them in next step)**

```bash
make 2>&1 | grep "error:" | head -20
```

Common errors to expect: missing `kcalloc`, missing `platform_util`, `timing` references. Fix each:
- If `kcalloc` missing: add `void *kcalloc(size_t n, size_t s) { return kmalloc(n*s); }` to `kernel/memory.c`
- If timing errors: add `#undef MBEDTLS_TIMING_C` to config.h (already there — check it compiled)

- [ ] **Step 5: Commit when it builds**

```bash
git add net/mbedtls/ Makefile
git commit -m "feat: vendor mbedTLS 3.6.1 with kernel-appropriate config"
```

---

### Task 12: Write TLS entropy, glue layer, and CA bundle

**Files:**
- Create: `net/tls.h`
- Create: `net/tls.c`
- Create: `net/ca_bundle.c` (generated)

- [ ] **Step 1: Create `net/tls.h`**

```c
#pragma once
#include "kernel.h"

/*
 * TLS connection handle. Opaque to callers.
 * Allocate with tls_connect(), free with tls_close().
 */
typedef struct tls_conn tls_conn_t;

/*
 * Open a TLS 1.2 connection to host:port.
 * Returns NULL on failure.
 * `sock` is an already-connected TCP socket fd from our TCP stack.
 */
tls_conn_t *tls_connect(int sock, const char *hostname);

/* Read up to `len` bytes. Returns bytes read, 0 on EOF, <0 on error. */
int tls_read(tls_conn_t *conn, void *buf, int len);

/* Write `len` bytes. Returns bytes written or <0 on error. */
int tls_write(tls_conn_t *conn, const void *buf, int len);

/* Close and free the TLS connection. */
void tls_close(tls_conn_t *conn);
```

- [ ] **Step 2: Create `net/tls.c`**

```c
#include "kernel.h"
#include "net/tls.h"
#include "mbedtls/include/mbedtls/ssl.h"
#include "mbedtls/include/mbedtls/entropy.h"
#include "mbedtls/include/mbedtls/ctr_drbg.h"
#include "mbedtls/include/mbedtls/x509_crt.h"
#include "mbedtls/include/mbedtls/error.h"

/* CA bundle — defined in ca_bundle.c */
extern const unsigned char ca_bundle_pem[];
extern const unsigned int  ca_bundle_pem_len;

struct tls_conn {
    mbedtls_ssl_context      ssl;
    mbedtls_ssl_config       conf;
    mbedtls_entropy_context  entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_x509_crt         ca_certs;
    int                      sock;
};

/* ── Entropy source: RTC + PIT jitter ──────────────────────────────────── */
static int careos_entropy(void *data, unsigned char *output, size_t len,
                          size_t *olen) {
    (void)data;
    for (size_t i = 0; i < len; i++) {
        /* XOR multiple jitter sources */
        u8 b = (u8)timer_get_ticks();
        b ^= (u8)rtc_read_seconds();
        /* PIT counter low byte */
        __asm__ volatile ("inb $0x40, %0" : "=a"(b) : "0"(b));
        output[i] = b;
    }
    *olen = len;
    return 0;
}

/* ── Send/recv callbacks for mbedTLS ───────────────────────────────────── */
static int tls_send_cb(void *ctx, const unsigned char *buf, size_t len) {
    int sock = *(int *)ctx;
    int n = tcp_socket_write(sock, (const char *)buf, (u32)len);
    return n > 0 ? n : MBEDTLS_ERR_NET_SEND_FAILED;
}

static int tls_recv_cb(void *ctx, unsigned char *buf, size_t len) {
    int sock = *(int *)ctx;
    int n = tcp_socket_read(sock, (char *)buf, (u32)len);
    return n > 0 ? n : MBEDTLS_ERR_NET_RECV_FAILED;
}

/* ── Public API ────────────────────────────────────────────────────────── */
tls_conn_t *tls_connect(int sock, const char *hostname) {
    tls_conn_t *c = (tls_conn_t *)kmalloc(sizeof(tls_conn_t));
    if (!c) return NULL;
    kmemset(c, 0, sizeof(*c));
    c->sock = sock;

    mbedtls_ssl_init(&c->ssl);
    mbedtls_ssl_config_init(&c->conf);
    mbedtls_entropy_init(&c->entropy);
    mbedtls_ctr_drbg_init(&c->ctr_drbg);
    mbedtls_x509_crt_init(&c->ca_certs);

    /* Add entropy source */
    mbedtls_entropy_add_source(&c->entropy, careos_entropy, NULL,
                               16, MBEDTLS_ENTROPY_SOURCE_STRONG);

    /* Seed DRBG */
    const char *pers = "careos_tls";
    if (mbedtls_ctr_drbg_seed(&c->ctr_drbg, mbedtls_entropy_func,
                              &c->entropy,
                              (const unsigned char *)pers, kstrlen(pers)) != 0) {
        tls_close(c); return NULL;
    }

    /* Load CA bundle */
    if (mbedtls_x509_crt_parse(&c->ca_certs, ca_bundle_pem,
                               ca_bundle_pem_len) < 0) {
        serial_write("[tls] CA bundle parse failed\n");
        tls_close(c); return NULL;
    }

    /* Configure TLS client */
    if (mbedtls_ssl_config_defaults(&c->conf, MBEDTLS_SSL_IS_CLIENT,
                                   MBEDTLS_SSL_TRANSPORT_STREAM,
                                   MBEDTLS_SSL_PRESET_DEFAULT) != 0) {
        tls_close(c); return NULL;
    }
    mbedtls_ssl_conf_rng(&c->conf, mbedtls_ctr_drbg_random, &c->ctr_drbg);
    mbedtls_ssl_conf_ca_chain(&c->conf, &c->ca_certs, NULL);
    mbedtls_ssl_conf_authmode(&c->conf, MBEDTLS_SSL_VERIFY_REQUIRED);

    if (mbedtls_ssl_setup(&c->ssl, &c->conf) != 0) {
        tls_close(c); return NULL;
    }
    if (mbedtls_ssl_set_hostname(&c->ssl, hostname) != 0) {
        tls_close(c); return NULL;
    }
    mbedtls_ssl_set_bio(&c->ssl, &c->sock, tls_send_cb, tls_recv_cb, NULL);

    /* Perform handshake */
    int ret;
    while ((ret = mbedtls_ssl_handshake(&c->ssl)) != 0) {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ &&
            ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            serial_write("[tls] handshake failed\n");
            tls_close(c); return NULL;
        }
    }

    serial_write("[tls] handshake OK: "); serial_write(hostname);
    serial_write("\n");
    return c;
}

int tls_read(tls_conn_t *conn, void *buf, int len) {
    return mbedtls_ssl_read(&conn->ssl, (unsigned char *)buf, (size_t)len);
}

int tls_write(tls_conn_t *conn, const void *buf, int len) {
    return mbedtls_ssl_write(&conn->ssl, (const unsigned char *)buf, (size_t)len);
}

void tls_close(tls_conn_t *conn) {
    if (!conn) return;
    mbedtls_ssl_free(&conn->ssl);
    mbedtls_ssl_config_free(&conn->conf);
    mbedtls_entropy_free(&conn->entropy);
    mbedtls_ctr_drbg_free(&conn->ctr_drbg);
    mbedtls_x509_crt_free(&conn->ca_certs);
    kfree(conn);
}
```

- [ ] **Step 3: Generate CA bundle**

In WSL:
```bash
curl -o /tmp/cacert.pem https://curl.se/ca/cacert.pem
xxd -i /tmp/cacert.pem > net/ca_bundle.c
# Rename symbols to match what tls.c expects:
sed -i 's/_tmp_cacert_pem/ca_bundle_pem/g' net/ca_bundle.c
```

- [ ] **Step 4: Build**

```bash
make clean && make 2>&1 | grep -E "error:"
```

- [ ] **Step 5: Test HTTPS GET in QEMU**

Add a temporary call in `kernel/kernel.c` after networking init:

```c
/* TEMP: TLS smoke test */
{
    int sock = tcp_connect_to("93.184.216.34", 443);  /* example.com */
    if (sock >= 0) {
        tls_conn_t *tls = tls_connect(sock, "example.com");
        if (tls) {
            const char *req = "GET / HTTP/1.0\r\nHost: example.com\r\n\r\n";
            tls_write(tls, req, kstrlen(req));
            char resp[256]; kmemset(resp, 0, 256);
            int n = tls_read(tls, resp, 255);
            serial_write("[tls test] response: ");
            serial_write(n > 0 ? resp : "(empty)");
            serial_write("\n");
            tls_close(tls);
        }
        tcp_socket_close(sock);
    }
}
```

Run: `make run` (requires QEMU user-mode networking to be enabled in Makefile)

Check serial for: `[tls] handshake OK: example.com` and `[tls test] response: HTTP/1.0 200 OK`

- [ ] **Step 6: Remove temp test, commit**

```bash
git add net/tls.h net/tls.c net/ca_bundle.c net/mbedtls/config.h Makefile
git commit -m "feat: mbedTLS TLS 1.2 glue layer with RTC entropy and Mozilla CA bundle"
```

**--- PHASE 3 GATE: HTTPS GET to example.com succeeds in QEMU. ---**

---

## Phase 4 — Links2 Browser Engine

### Task 13: Vendor Links2 and write shims

**Files:**
- Create: `apps/links2/` (vendored)
- Create: `apps/links2/careos_fb.c`
- Create: `apps/links2/careos_input.c`
- Create: `apps/links2/careos_net.c`
- Modify: `apps/app_browser.c`
- Modify: `Makefile`

- [ ] **Step 1: Download Links2 source**

In WSL:
```bash
cd apps/
curl -L http://links.twibright.com/download/links-2.30.tar.bz2 | tar xj
mv links-2.30 links2
```

- [ ] **Step 2: Identify integration points**

```bash
ls apps/links2/
# Key files to understand before writing shims:
head -100 apps/links2/links.c     # main entry
head -100 apps/links2/os_dep.h    # OS abstraction (what we replace)
head -50  apps/links2/graphics/fb.c  # framebuffer driver (what we replace)
```

Read the output and note the exact function signatures for:
- `os_calloc`, `os_free` — memory
- `select()` / event loop in `links2/select.c`
- `links_connect()` / socket calls in `links2/connect.c`
- Framebuffer `init_fb()`, `fb_draw_pixels()` etc. in `links2/graphics/fb.c`

- [ ] **Step 3: Write `apps/links2/careos_fb.c` — framebuffer shim**

Replace `apps/links2/graphics/fb.c` (or add alongside and adjust the build):

```c
/* CareOS framebuffer backend for Links2 graphics mode.
   Replaces the Linux /dev/fb0 driver with direct gfx.c calls. */
#include "kernel.h"
#include "../gui/gfx.h"   /* gfx_fill_rect, gfx_draw_pixel, etc. */

/* Links2 calls these to initialise the display */
int init_fb(int width, int height) {
    /* Our framebuffer is already initialised by the kernel.
       Just record dimensions for Links2's internal use. */
    (void)width; (void)height;
    return 0;  /* 0 = success */
}

void close_fb(void) { /* nothing to close */ }

/* Links2 calls this to draw a filled rectangle */
void fb_fill_rect(int x, int y, int w, int h, unsigned color) {
    gfx_fill_rect(x, y, w, h, color);
}

/* Links2 calls this to blit a pixel buffer (e.g. decoded image data) */
void fb_draw_bitmap(int x, int y, int w, int h,
                    const unsigned char *data, int pitch) {
    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            const unsigned char *px = data + row * pitch + col * 4;
            u32 color = ((u32)px[0] << 16) | ((u32)px[1] << 8) | px[2];
            gfx_draw_pixel(x + col, y + row, color);
        }
    }
}

/* Links2 calls this to draw a text string */
void fb_draw_text(int x, int y, const char *str, unsigned color) {
    gfx_draw_string(x, y, str, color);
}
```

*(Exact function names depend on Links2's `graphics/fb.c` — adjust to match after reading it in Step 2.)*

- [ ] **Step 4: Write `apps/links2/careos_input.c` — keyboard/mouse shim**

```c
#include "kernel.h"

/* Links2 calls this to poll for the next input event.
   We translate our keyboard/mouse state into Links2's event format. */
int links2_get_event(int *type, int *data1, int *data2) {
    /* Check keyboard */
    int c = keyboard_get_char_nonblock();  /* returns -1 if no key */
    if (c >= 0) {
        *type  = 1;   /* KEY_EVENT */
        *data1 = c;
        *data2 = 0;
        return 1;
    }

    /* Check mouse */
    int mx, my, mbuttons;
    if (mouse_get_event(&mx, &my, &mbuttons)) {
        *type  = 2;   /* MOUSE_EVENT */
        *data1 = mx;
        *data2 = my;
        return 1;
    }

    return 0;  /* no event */
}
```

- [ ] **Step 5: Write `apps/links2/careos_net.c` — network shim**

```c
#include "kernel.h"
#include "net/tls.h"

/* links2 uses BSD socket-like calls internally.
   We intercept connect/send/recv/close and route to our TCP + TLS stack. */

/* Plain TCP connection */
int links2_tcp_connect(const char *host, int port) {
    u32 ip = dns_resolve(host);
    if (ip == 0) return -1;
    return tcp_connect(ip, (u16)port);
}

int links2_tcp_send(int sock, const void *buf, int len) {
    return tcp_socket_write(sock, (const char *)buf, (u32)len);
}

int links2_tcp_recv(int sock, void *buf, int len) {
    return tcp_socket_read(sock, (char *)buf, (u32)len);
}

void links2_tcp_close(int sock) {
    tcp_socket_close(sock);
}

/* TLS (HTTPS) — wraps our tls_conn_t in an opaque int handle */
#define MAX_TLS_CONNS 4
static tls_conn_t *tls_table[MAX_TLS_CONNS];

int links2_tls_connect(const char *host, int port) {
    int sock = links2_tcp_connect(host, port);
    if (sock < 0) return -1;

    tls_conn_t *conn = tls_connect(sock, host);
    if (!conn) { links2_tcp_close(sock); return -1; }

    for (int i = 0; i < MAX_TLS_CONNS; i++) {
        if (!tls_table[i]) { tls_table[i] = conn; return 100 + i; /* fake fd */ }
    }
    tls_close(conn); return -1;
}

int links2_tls_send(int fd, const void *buf, int len) {
    int idx = fd - 100;
    if (idx < 0 || idx >= MAX_TLS_CONNS || !tls_table[idx]) return -1;
    return tls_write(tls_table[idx], buf, len);
}

int links2_tls_recv(int fd, void *buf, int len) {
    int idx = fd - 100;
    if (idx < 0 || idx >= MAX_TLS_CONNS || !tls_table[idx]) return -1;
    return tls_read(tls_table[idx], buf, len);
}

void links2_tls_close(int fd) {
    int idx = fd - 100;
    if (idx < 0 || idx >= MAX_TLS_CONNS || !tls_table[idx]) return;
    tls_close(tls_table[idx]);
    tls_table[idx] = NULL;
}
```

- [ ] **Step 6: Modify `apps/app_browser.c` to launch Links2**

Replace the existing HTML renderer with:

```c
#include "kernel.h"
#include "links2/links.h"   /* Links2 init/navigate/pump API */

static bool links2_inited = false;

void app_browser_open(window_t *win) {
    if (!links2_inited) {
        links2_init(win->fb, win->width, win->height);
        links2_inited = true;
    }
    /* URL bar rendering and input handled via our WM event loop */
}

void app_browser_navigate(const char *url) {
    links2_navigate(url);
}

/* Called once per frame from the window manager */
void app_browser_tick(window_t *win) {
    (void)win;
    links2_pump_events();   /* polls input, redraws if needed */
}
```

- [ ] **Step 7: Add Links2 to Makefile**

```makefile
# Exclude Linux-specific files, use our shims instead
LINKS2_EXCLUDE := apps/links2/graphics/fb.c apps/links2/os_dep_linux.c
LINKS2_ALL     := $(wildcard apps/links2/*.c apps/links2/graphics/*.c)
LINKS2_SRCS    := $(filter-out $(LINKS2_EXCLUDE), $(LINKS2_ALL))
LINKS2_SRCS    += apps/links2/careos_fb.c apps/links2/careos_input.c \
                  apps/links2/careos_net.c

OBJS += $(LINKS2_SRCS:.c=.o)
CFLAGS += -Iapps/links2
```

- [ ] **Step 8: Build — fix errors iteratively**

```bash
make 2>&1 | grep "error:" | head -30
```

Common errors and fixes:
- Undefined `os_malloc`/`os_free`: add to `careos_net.c`: `void *os_malloc(size_t n) { return kmalloc(n); } void os_free(void *p) { kfree(p); }`
- Missing POSIX headers (`unistd.h`, `errno.h`): create stub `apps/links2/posix_stub.h` with `#define ENOENT 2` etc.
- `select()` call: intercept in `careos_input.c`
- Iterate until `make` succeeds.

- [ ] **Step 9: Test — navigate to a real page in QEMU**

Open the browser app, navigate to `http://example.com` (plain HTTP first). Verify the page renders in the Links2 window. Then try `https://example.com`. Check serial for `[tls] handshake OK`.

- [ ] **Step 10: Commit**

```bash
git add apps/links2/ apps/app_browser.c Makefile
git commit -m "feat: integrate Links2 browser engine with CareOS framebuffer, input, and TLS"
```

**--- PHASE 4 GATE: https://example.com renders in the browser window inside QEMU. ---**

---

## Phase 5 — Multi-Session GUI

### Task 14: Write session.h and session.c — virtual framebuffers

**Files:**
- Create: `gui/session.h`
- Create: `gui/session.c`

- [ ] **Step 1: Create `gui/session.h`**

```c
#pragma once
#include "kernel.h"

#define MAX_SESSIONS 4

typedef enum {
    SESSION_EMPTY = 0,
    SESSION_LOGIN,
    SESSION_ACTIVE,
    SESSION_SUSPENDED,
} session_state_t;

typedef struct {
    u32              *framebuffer;        /* off-screen pixel buffer */
    u32               fb_width;
    u32               fb_height;
    void             *wm_state;           /* opaque wm_state_t* */
    user_t           *user;              /* NULL when not logged in */
    u32               proc_ids[32];
    u32               proc_count;
    session_state_t   state;
    int               id;
} session_t;

/* Initialise session manager (call once at boot, after paging) */
void session_init(u32 fb_width, u32 fb_height);

/* Create a new session in an empty slot. Returns session ID or -1. */
int  session_create(void);

/* Switch the active display to session `id`. */
void session_switch(int id);

/* Return the currently displayed session. */
session_t *session_active(void);

/* Return session by ID. */
session_t *session_get(int id);

/* Register a process as owned by a session. */
void session_add_proc(int session_id, u32 pid);

/* Called on logout: kill all owned processes, reset slot. */
void session_destroy(int session_id);

/* Draw the session-switcher overlay (Ctrl+Alt+Tab). */
void session_draw_switcher(void);
```

- [ ] **Step 2: Create `gui/session.c`**

```c
#include "kernel.h"
#include "gui/session.h"
#include "gui/gfx.h"
#include "gui/wm.h"

static session_t sessions[MAX_SESSIONS];
static int       active_id = -1;
static u32       s_fb_width, s_fb_height;
static u32       s_fb_bytes;

/* Physical framebuffer pointer — set at boot from multiboot */
extern u32 *phys_framebuffer;   /* defined in kernel/kernel.c */

void session_init(u32 fb_width, u32 fb_height) {
    s_fb_width  = fb_width;
    s_fb_height = fb_height;
    s_fb_bytes  = fb_width * fb_height * 4;

    kmemset(sessions, 0, sizeof(sessions));
    for (int i = 0; i < MAX_SESSIONS; i++) {
        sessions[i].id    = i;
        sessions[i].state = SESSION_EMPTY;
    }
    serial_write("[session] init OK\n");
}

int session_create(void) {
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (sessions[i].state == SESSION_EMPTY) {
            sessions[i].framebuffer = (u32*)kmalloc(s_fb_bytes);
            if (!sessions[i].framebuffer) return -1;
            kmemset(sessions[i].framebuffer, 0, s_fb_bytes);
            sessions[i].fb_width  = s_fb_width;
            sessions[i].fb_height = s_fb_height;
            sessions[i].state     = SESSION_LOGIN;
            sessions[i].user      = NULL;
            sessions[i].proc_count = 0;
            sessions[i].wm_state  = wm_create();  /* per-session WM */
            serial_write("[session] created slot ");
            char buf[4]; kitoa(i, buf, 10); serial_write(buf); serial_write("\n");
            return i;
        }
    }
    return -1;  /* all slots full */
}

session_t *session_active(void) {
    if (active_id < 0 || active_id >= MAX_SESSIONS) return NULL;
    return &sessions[active_id];
}

session_t *session_get(int id) {
    if (id < 0 || id >= MAX_SESSIONS) return NULL;
    return &sessions[id];
}

void session_switch(int id) {
    if (id < 0 || id >= MAX_SESSIONS) return;
    if (sessions[id].state == SESSION_EMPTY) return;

    /* Suspend current */
    if (active_id >= 0 && sessions[active_id].state == SESSION_ACTIVE)
        sessions[active_id].state = SESSION_SUSPENDED;

    active_id = id;
    sessions[id].state = SESSION_ACTIVE;

    /* Point the graphics layer at the new session's virtual framebuffer */
    gfx_set_target(sessions[id].framebuffer, s_fb_width, s_fb_height);

    /* Blit virtual framebuffer to physical display immediately */
    kmemcpy(phys_framebuffer, sessions[id].framebuffer, s_fb_bytes);

    serial_write("[session] switched to slot ");
    char buf[4]; kitoa(id, buf, 10); serial_write(buf); serial_write("\n");
}

void session_add_proc(int session_id, u32 pid) {
    session_t *s = session_get(session_id);
    if (!s || s->proc_count >= 32) return;
    s->proc_ids[s->proc_count++] = pid;
}

void session_destroy(int session_id) {
    session_t *s = session_get(session_id);
    if (!s || s->state == SESSION_EMPTY) return;

    /* Kill all owned processes */
    for (u32 i = 0; i < s->proc_count; i++) {
        task_kill(s->proc_ids[i]);  /* sends exit to the task */
    }

    kfree(s->framebuffer);
    wm_destroy(s->wm_state);
    kmemset(s, 0, sizeof(session_t));
    s->id    = session_id;
    s->state = SESSION_EMPTY;

    /* Switch to another active session */
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (i != session_id && sessions[i].state != SESSION_EMPTY) {
            session_switch(i); return;
        }
    }
    active_id = -1;  /* no sessions left — show boot screen */
}

void session_draw_switcher(void) {
    /* Draw a centered overlay listing all sessions */
    u32 panel_x = s_fb_width / 2 - 200;
    u32 panel_y = s_fb_height / 2 - 80;
    gfx_fill_rect(panel_x, panel_y, 400, 160, 0x1E1E2E);
    gfx_draw_rect(panel_x, panel_y, 400, 160, 0x89DCEB);

    gfx_draw_string(panel_x + 16, panel_y + 12, "Switch User Session", 0xCDD6F4);

    for (int i = 0; i < MAX_SESSIONS; i++) {
        u32 row_y = panel_y + 40 + i * 28;
        if (sessions[i].state == SESSION_EMPTY) {
            gfx_draw_string(panel_x + 24, row_y, "[ empty slot ]", 0x585B70);
        } else {
            const char *name = sessions[i].user
                ? sessions[i].user->username : "(login screen)";
            char label[64];
            kstrcpy(label, "F"); char num[4]; kitoa(i+1, num, 10);
            kstrcat(label, num); kstrcat(label, ": "); kstrcat(label, name);
            u32 color = (i == active_id) ? 0xA6E3A1 : 0xCDD6F4;
            gfx_draw_string(panel_x + 24, row_y, label, color);
        }
    }

    /* Blit to physical display immediately */
    kmemcpy(phys_framebuffer, sessions[active_id].framebuffer, s_fb_bytes);
}
```

- [ ] **Step 3: Add `wm_create()` and `wm_destroy()` to `gui/wm.c`**

The WM currently has global state. Refactor to take a `wm_state_t*` parameter. Add:

```c
void *wm_create(void) {
    wm_state_t *state = (wm_state_t*)kmalloc(sizeof(wm_state_t));
    if (!state) return NULL;
    kmemset(state, 0, sizeof(wm_state_t));
    return state;
}

void wm_destroy(void *state) {
    kfree(state);
}
```

- [ ] **Step 4: Build**

```bash
make clean && make 2>&1 | grep -E "error:"
```

- [ ] **Step 5: Commit**

```bash
git add gui/session.h gui/session.c gui/wm.c
git commit -m "feat: session manager with virtual framebuffers and per-session WM state"
```

---

### Task 15: Wire sessions into the GUI boot and keyboard handler

**Files:**
- Modify: `gui/gui.c`
- Modify: `drivers/keyboard.c`
- Modify: `kernel/kernel.c`

- [ ] **Step 1: Add `session_init()` call in kernel.c**

After framebuffer init (before `gui_init()`):
```c
session_init(fb_width, fb_height);
int sid = session_create();   /* create first session */
session_switch(sid);          /* make it active */
```

- [ ] **Step 2: Route `Ctrl+Alt+F1..F4` in keyboard driver**

In `drivers/keyboard.c`, in the key handler (where modifier keys are tracked), add after existing shortcut checks:

```c
/* Ctrl+Alt+F1..F4 → session switch */
if (ctrl_held && alt_held && scancode >= 0x3B && scancode <= 0x3E) {
    int target_session = scancode - 0x3B;  /* F1=0x3B, F2=0x3C, F3=0x3D, F4=0x3E */
    session_t *s = session_get(target_session);
    if (s && s->state == SESSION_EMPTY) {
        /* Create a new session in this slot */
        int new_sid = session_create();
        if (new_sid >= 0) session_switch(new_sid);
    } else if (s && s->state != SESSION_EMPTY) {
        session_switch(target_session);
    }
    return;
}

/* Ctrl+Alt+Tab → show switcher overlay */
if (ctrl_held && alt_held && scancode == 0x0F) {  /* Tab = 0x0F */
    session_draw_switcher();
    return;
}
```

- [ ] **Step 3: Make the login screen session-aware**

In `gui/gui.c`, in the login success callback:

```c
void on_login_success(user_t *user) {
    session_t *s = session_active();
    if (s) {
        s->user  = user;
        s->state = SESSION_ACTIVE;
    }
    /* Continue with existing desktop init... */
    gui_show_desktop();
}
```

In the logout/exit handler:
```c
void on_logout(void) {
    session_t *s = session_active();
    if (s) session_destroy(s->id);
}
```

- [ ] **Step 4: Add `task_kill()` to scheduler.c**

```c
void task_kill(u32 pid) {
    for (u32 i = 0; i < task_count; i++) {
        if (tasks[i].id == pid && tasks[i].state != TASK_DEAD) {
            tasks[i].state = TASK_DEAD;
            serial_write("[sched] task killed: ");
            char buf[8]; kutoa(pid, buf, 10);
            serial_write(buf); serial_write("\n");
            return;
        }
    }
}
```

Declare in `include/kernel.h`:
```c
void task_kill(u32 pid);
```

- [ ] **Step 5: Build and run**

```bash
make clean && make && make run
```

- [ ] **Step 6: Test multi-session**

1. Log in as `user` in session 1.
2. Press `Ctrl+Alt+F2` — verify a login screen appears.
3. Log in as `root` in session 2.
4. Press `Ctrl+Alt+F1` — verify you're back at `user`'s desktop.
5. Open a file in session 1 — verify session 2 cannot see it.
6. Press `Ctrl+Alt+Tab` — verify the switcher overlay appears listing both sessions.

Expected serial output:
```
[session] switched to slot 1
[session] switched to slot 0
```

- [ ] **Step 7: Crash test**

In session 1, launch `ring3_fault` test ELF (from Phase 1). Verify:
- Serial shows `[PAGE FAULT] ring-3 fault — killing task`
- Session 1 desktop remains usable
- Switch to session 2 — it is unaffected

- [ ] **Step 8: Logout test**

Log out of session 2. Verify:
- Serial shows `[sched] task killed: ...` for each session 2 process
- `Ctrl+Alt+F2` now shows an empty login screen

- [ ] **Step 9: Commit**

```bash
git add gui/gui.c drivers/keyboard.c kernel/kernel.c kernel/scheduler.c include/kernel.h
git commit -m "feat: wire sessions to GUI boot, keyboard Ctrl+Alt+Fn switching, and logout"
```

**--- PHASE 5 GATE: Two users concurrently active; crash in one does not affect the other; session switcher shows both. ---**

---

## Final Checklist

Run this before declaring completion:

- [ ] `e2fsck -fn careos.img` reports clean after a full login/file-ops/logout session
- [ ] `https://example.com` renders in the browser (not just plain HTTP)
- [ ] Two users can be logged in simultaneously (Ctrl+Alt+F1 / Ctrl+Alt+F2)
- [ ] A null-deref in a ring-3 process kills only that process, OS continues
- [ ] Creating a file in user A's session does not appear in user B's session
- [ ] Logging out destroys all processes owned by that session
- [ ] `make format-disk` + `make` + `make run` boots cleanly on a fresh disk
