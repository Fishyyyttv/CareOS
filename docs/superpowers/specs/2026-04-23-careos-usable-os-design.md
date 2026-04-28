# CareOS v9 — Usable Multi-User OS Design Spec

**Date:** 2026-04-23  
**Goal:** Transform CareOS v9 from a demo 32-bit x86 OS into a genuinely usable desktop OS with multi-user concurrent sessions, a real filesystem, HTTPS internet browsing, and ring 3 process isolation.  
**Approach:** Foundation-first (bottom-up). Each layer is stable before the next is added.  
**Strategy:** Hybrid — keep our own TCP/IP stack, integrate battle-tested external libs for security-critical and rendering components.

---

## External Components Required

| Component | Purpose | Size | License | Integration complexity |
|-----------|---------|------|---------|----------------------|
| **mbedTLS** (ARM Mbed) | TLS 1.2 for HTTPS | ~300KB C | Apache 2.0 | Medium — callback-based, no libc deps beyond memcpy/memset |
| **Links2** (browser engine) | HTML 4 + CSS 1/2 + images | ~1.5MB C | GPL v2 | Medium — strip Linux FB driver, swap in our gfx.c + input |
| **Mozilla CA bundle** | HTTPS certificate verification | ~250KB (compiled C array) | MPL 2.0 | Low — one-time conversion via `curl-ca-bundle` script |

No other external libs needed. All other layers extend existing CareOS code.

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────┐
│            MULTI-SESSION GUI (Layer 4)               │
│   Session A (user1)  │  Session B (user2)  │  ...   │
│   Virtual framebuffer│  Virtual framebuffer│         │
│   WM + apps          │  WM + apps          │         │
├─────────────────────────────────────────────────────┤
│         BROWSER STACK (Layer 3)                      │
│   mbedTLS over our TCP socket layer                  │
│   Links2 framebuffer engine → our gfx.c layer        │
├─────────────────────────────────────────────────────┤
│         EXT2 FILESYSTEM (Layer 2)                    │
│   VFS abstraction (keep) → ext2 driver (new)         │
│   ATA driver (keep) → 512MB disk image               │
├─────────────────────────────────────────────────────┤
│         RING 3 USER-SPACE (Layer 1)                  │
│   TSS + ring 3 GDT segments (extend existing GDT)    │
│   Per-process page tables (extend existing paging)   │
│   Kernel/user stacks per process                     │
│   Syscall gate (extend existing syscall.c)           │
│   Scheduler extended for ring 3 context saves        │
└─────────────────────────────────────────────────────┘
│         KERNEL (unchanged foundation)                │
│   GDT/IDT, PMM, PIC, PIT, RTC, PCI, e1000, PS/2    │
└─────────────────────────────────────────────────────┘
```

**Principle:** Each layer only touches the one below it. Existing VFS, scheduler, GDT/IDT, and ATA driver are extended, not replaced.

---

## Layer 1 — Ring 3 User-Space Process Isolation

### Motivation

Currently all 15 apps run in ring 0 (kernel space). A crash in any app crashes the entire OS. Multi-session safety requires that a crash in one user's process cannot affect another user's session.

### What Already Exists

- `kernel/gdt_idt.c` — GDT with ring 0 code/data segments
- `kernel/paging.c` — paging enabled, physical memory manager
- `kernel/elf.c` — ELF binary loader (loads into kernel space today)
- `kernel/scheduler.c` — preemptive task scheduler with context switching
- `kernel/syscall.c` — `int 0x80` syscall interface

### Changes

**GDT extension (`kernel/gdt_idt.c`):**
- Add ring 3 code segment descriptor (selector `0x1B`, DPL=3)
- Add ring 3 data segment descriptor (selector `0x23`, DPL=3)
- Add TSS descriptor pointing to a single TSS structure

**TSS (`kernel/gdt_idt.c`):**
- One TSS per CPU (CareOS is uniprocessor)
- `TSS.ESP0` = kernel stack pointer for the currently running process
- CPU loads `ESP0` automatically on any ring 3 → ring 0 transition

**Per-process page tables (`kernel/paging.c`):**
- Each process gets its own `CR3` (page directory)
- Kernel mapped at `0xC0000000+` in every process's page table (shared, read-only from ring 3)
- User code loaded at `0x00400000`
- User stack grows down from `0xBFFFF000`
- On context switch: swap `CR3`, update `TSS.ESP0` to incoming process's kernel stack base

**Kernel stack per process:**
- Each PCB gets a dedicated 8KB kernel stack (allocated from kernel heap)
- Used during syscalls and interrupts while that process is running

**Syscall gate (`kernel/syscall.c`):**
- IDT entry for `int 0x80` gets DPL=3 (so ring 3 can invoke it)
- Handler saves all registers, executes syscall dispatch, returns via `iret`
- Add `copy_from_user(dst, user_ptr, len)` and `copy_to_user(user_ptr, src, len)` — validate pointer is within the process's user address range before touching it

**ELF loader (`kernel/elf.c`):**
- Currently allocates pages in kernel space
- Change: allocate pages in the *process's user page table* instead
- First launch fakes an `iret` frame: `CS=0x1B`, `EFLAGS` with IF=1, `SS=0x23`, `EIP=entry_point`, `ESP=user_stack_top`

**Process exit:**
- `exit()` syscall frees all user-space pages for this process
- Removes PCB from scheduler run queue
- Sets exit status for any waiting parent (`waitpid` syscall)

### Syscall Table (minimum viable set)

| Number | Name | Description |
|--------|------|-------------|
| 1 | `exit(status)` | Terminate process |
| 2 | `read(fd, buf, len)` | Read from fd |
| 3 | `write(fd, buf, len)` | Write to fd |
| 4 | `open(path, flags)` | Open file, return fd |
| 5 | `close(fd)` | Close fd |
| 6 | `getpid()` | Return process ID |
| 7 | `waitpid(pid, status)` | Wait for child |
| 8 | `exec(path, argv)` | Replace process image |
| 9 | `fork()` | Clone process |
| 10 | `mmap(addr, len, prot)` | Map memory region |
| 11 | `munmap(addr, len)` | Unmap memory region |
| 12 | `sbrk(increment)` | Grow heap |

### Testing

1. Compile a minimal ELF (`_start: mov eax, 1 / xor ebx, ebx / int 0x80`) — just calls `exit(0)`
2. Load it via the ELF loader, verify clean exit without kernel panic
3. Test a page fault in ring 3 (`*(int*)0 = 1`) — verify kernel catches it, kills only that process, continues running
4. Run two processes simultaneously in the scheduler — verify independent stacks and page tables

---

## Layer 2 — ext2 Filesystem

### Motivation

Current VFS: 4KB max file size, 256 files, 64 files per directory. Browser cache, user home directories, and apps make this instantly impractical. ext2 removes all meaningful limits for CareOS's scale.

### What Already Exists

- `drivers/storage/ata.c` — ATA/IDE driver with sector-level read/write and caching
- `kernel/vfs.c` — VFS abstraction with `vfs_open`, `vfs_read`, `vfs_write`, `vfs_mkdir`, `vfs_unlink`, etc.
- `careos.img` — 64MB raw disk image

### Changes

**Disk image:** Grow to 512MB. New Makefile target:
```makefile
format-disk:
    dd if=/dev/zero of=careos.img bs=1M count=512
    mkfs.ext2 -b 1024 -I 128 careos.img
```
`make reset-disk` is repurposed to call `format-disk`.

**ext2 driver (`kernel/ext2.c`, new file ~800 lines):**

| Component | Description |
|-----------|-------------|
| Superblock | Read block 1; validate magic `0xEF53`; extract block size, inode size, group count |
| Block group descriptors | Locate inode tables and block/inode bitmaps per group |
| Inode read/write | 128-byte structs; 12 direct + 1 indirect + 1 double-indirect block pointers |
| Directory traversal | Linked list of variable-length `ext2_dir_entry_t` records |
| Block allocator | Scan block bitmap; mark allocated bit; write back to disk |
| Inode allocator | Scan inode bitmap; return free inode number |
| File read | Follow inode block pointers; handle indirect blocks; read via ATA driver |
| File write | Allocate new blocks as needed; update inode size; write via ATA driver |
| Directory create/delete | Add/remove `ext2_dir_entry` records; update parent inode |

**VFS adapter (`kernel/vfs.c`):**
- Keep all existing function signatures unchanged
- Replace the in-memory tree backend with calls into `ext2.c`
- No other kernel code changes — VFS callers are unaffected

**Mount at boot (`kernel/kernel.c` stage 2):**
```c
ata_init();
ext2_mount();   // reads superblock, caches root inode
```

**User home directories:**
- On first login for a user, check if `/home/username` exists on ext2
- If not, `ext2_mkdir("/home/username")`, `ext2_chown` to that user's UID

**Filesystem limits (ext2 with 1KB blocks, 128-byte inodes):**
- Max file size: ~16GB (well beyond our 512MB disk)
- Max files: millions (inode table size)
- Max directory entries: limited only by directory file size
- Max filename: 255 characters

### Testing

1. After every write operation during development, run `e2fsck -fn careos.img` from the host — must report clean
2. Write a 1MB file, read it back, verify byte-for-byte correctness
3. Create 1000 files in a directory — verify directory entry traversal
4. Fill disk to 90% — verify allocator handles near-full gracefully
5. Simulate ATA read error (corrupt a sector in the image) — verify VFS returns `-EIO`, no kernel panic

---

## Layer 3 — Browser Stack (mbedTLS + Links2)

### Motivation

Current browser: HTTP/1.0 only, no CSS, no images, basic tag stripping. mbedTLS adds HTTPS. Links2 replaces the HTML renderer with a production-quality engine that handles CSS 1/2, tables, forms, PNG/JPEG images.

### What Already Exists

- `net/net.c` — TCP/IP stack with 16 socket slots, ARP, DNS, DHCP
- `apps/app_browser.c` — browser UI shell, window, URL bar, basic renderer
- `gui/gfx.c` — graphics primitives (rectangles, text, lines, gradients)

### mbedTLS Integration (`net/tls.c`, new ~200 lines of glue)

**Source:** https://github.com/Mbed-TLS/mbedtls — vendor the `library/` subdirectory into `net/mbedtls/`

**Integration points:**

```c
// Send callback — called by mbedTLS when it wants to write bytes
static int tls_send(void *ctx, const unsigned char *buf, size_t len) {
    int sock = *(int*)ctx;
    return tcp_socket_write(sock, buf, len);   // our existing API
}

// Receive callback — called by mbedTLS when it wants to read bytes
static int tls_recv(void *ctx, unsigned char *buf, size_t len) {
    int sock = *(int*)ctx;
    return tcp_socket_read(sock, buf, len);    // our existing API
}
```

**Entropy source (`net/tls_entropy.c`):**
- Primary: RTC sub-second register jitter (read CMOS 0x00 rapidly, XOR values)
- Secondary: PIT counter at time of read (`inw(0x40)`)
- Tertiary: Add `RDRAND` instruction support for real hardware (CPUID check first)
- Sufficient entropy for TLS 1.2 session keys in QEMU

**CA bundle:**
- Download Mozilla's `cacert.pem`, convert to C array via `xxd -i cacert.pem > net/ca_bundle.c`
- Embedded in the kernel binary (adds ~250KB to kernel size)
- Alternative: load from ext2 at boot (saves kernel binary size, requires ext2 mounted first)
- Recommendation: embed in kernel for simplicity in v1

**TLS version:** TLS 1.2. TLS 1.3 requires more robust entropy and a larger PRNG state — defer to a follow-up.

### Links2 Integration (`apps/links2/`, new directory)

**Source:** http://links.twibright.com/ — vendor into `apps/links2/`

**Steps:**
1. Remove `links2/frontend/fb.c` (Linux framebuffer driver) — replace with `links2/frontend/careos_fb.c`:
   ```c
   // Map Links2's draw calls to our gfx.c primitives
   void fb_draw_rect(int x, int y, int w, int h, uint32_t color) {
       gfx_fill_rect(x, y, w, h, color);
   }
   void fb_draw_text(int x, int y, const char *str, uint32_t color) {
       gfx_draw_string(x, y, str, color);
   }
   ```
2. Replace Links2's event loop with a polling function called from our window manager's message pump
3. Wire Links2 keyboard input to our `keyboard_get_scancode()` / `keyboard_get_char()`
4. Wire Links2 mouse input to our `mouse_get_state()`
5. Wire Links2 socket calls:
   - Plain HTTP → our `tcp_socket_*` API
   - HTTPS → our `tls_connect()` / `tls_read()` / `tls_write()` wrappers around mbedTLS

**`app_browser.c` becomes a launcher:**
```c
void app_browser_open(window_t *win, const char *url) {
    links2_init(win->framebuffer, win->width, win->height);
    links2_navigate(url);
    // links2_event_pump() called each frame from WM
}
```

**Links2 capabilities after integration:**
- HTML 4.01 (tables, forms, anchors, lists)
- CSS 1 and CSS 2 (layout, colors, fonts, box model)
- PNG and JPEG images (built-in decoders, no external deps)
- HTTP/1.1 and HTTPS (via our mbedTLS layer)
- Cookies (stored in `/home/username/.links2/cookies` on ext2)
- Basic JavaScript: Links2 has no JS engine — pages requiring JS will degrade gracefully

### Testing

1. mbedTLS unit: TLS handshake against `google.com` via QEMU user-mode networking (`-netdev user`) before wiring Links2
2. Full HTTPS GET: load `https://example.com` — verify HTML received and certificate validated
3. Links2 rendering: load a CSS-heavy page (Wikipedia article) — verify layout is correct in framebuffer
4. Image loading: load a page with PNG/JPEG images — verify they render in the browser window
5. Invalid cert: connect to a self-signed HTTPS server — verify browser shows error, doesn't crash

---

## Layer 4 — Multi-Session GUI (Fast User Switching)

### Motivation

CareOS already supports multiple user accounts. This layer makes multiple users *concurrently active*, each with their own desktop, running processes, and window manager state — switchable via `Ctrl+Alt+F1` through `Ctrl+Alt+F4`.

### What Already Exists

- `gui/wm.c` — window manager (8 windows, z-order, drag, resize, minimize, maximize)
- `gui/gui.c` — login screen, boot splash, main GUI loop
- `kernel/users.c` — user accounts, auth, password hashing, session tracking
- `gui/theme.c` — per-user theme preferences

### New: Session Manager (`gui/session.c`, new ~400 lines)

**Session struct:**
```c
#define MAX_SESSIONS 4

typedef enum {
    SESSION_EMPTY,      // slot unused
    SESSION_LOGIN,      // showing login screen
    SESSION_ACTIVE,     // user logged in, desktop running
    SESSION_SUSPENDED,  // backgrounded, not displayed
} session_state_t;

typedef struct {
    uint32_t        *framebuffer;       // off-screen pixel buffer (heap allocated)
    wm_state_t       wm;               // this session's window manager state
    user_t          *user;             // NULL if not logged in
    uint32_t         proc_ids[32];     // ring-3 PIDs owned by this session
    uint32_t         proc_count;
    session_state_t  state;
    int              session_id;
} session_t;

static session_t sessions[MAX_SESSIONS];
static int       active_session = 0;
```

**Virtual framebuffers:**
- Each session: `session->framebuffer = kmalloc(screen_width * screen_height * 4)`
- All rendering for a session goes into its off-screen buffer via `gfx_set_target(session->framebuffer)`
- Once per frame, the active session's buffer is blitted to physical framebuffer memory

**Session switching (`Ctrl+Alt+F1` through `Ctrl+Alt+F4`):**
1. Keyboard handler detects `Ctrl+Alt+Fn` — calls `session_switch(n-1)`
2. Current session state set to `SESSION_SUSPENDED`, input routing frozen
3. Target session's framebuffer blitted to physical display
4. `gfx_set_target()` updated to target session's buffer
5. Keyboard and mouse events routed to target session's WM
6. Brief overlay (1.5 seconds) shows target user's name — then dismissed

**Session switcher overlay:**
- Centered panel listing all non-empty sessions
- Each entry: user avatar (colored circle with initial), username, session state
- Arrow keys to select, Enter to switch, Escape to cancel
- Keyboard shortcut `Ctrl+Alt+Tab` cycles through active sessions

**Login flow per session:**
- Session slots start as `SESSION_EMPTY`
- `Ctrl+Alt+Fn` on an empty slot → show login screen in that slot → `SESSION_LOGIN`
- Successful login → load user profile, apply theme, create `/home/username` if needed → `SESSION_ACTIVE`
- Failed login → lockout logic (existing `users.c`) applies per-session

**Logout:**
1. User selects logout from settings or shell `exit`
2. `SIGTERM` sent to all PIDs in `session->proc_ids` (ring 3 processes exit cleanly)
3. All session windows closed, WM state cleared
4. Virtual framebuffer freed
5. Session slot reset to `SESSION_EMPTY`
6. Switch to another active session, or show session selector if all empty

**Per-session process ownership:**
- Each ring-3 process PCB has a `session_id` field
- Processes inherit the session ID of the process that spawned them
- A ring-3 crash kills only that PID — session continues
- Logout terminates all PIDs with matching `session_id`

**Resource limits per session:**
- Max 32 owned processes (matches `proc_ids` array)
- Virtual framebuffer: `1920 * 1080 * 4 = ~8MB` per session at 1080p
- 4 sessions at 1080p ≈ 32MB just for framebuffers — within our 256MB QEMU RAM budget

### Testing

1. Log in as `user` in session 1, `root` in session 2 — switch between them, verify each desktop is independent
2. Open a file in session 1, verify it is not visible in session 2's file browser
3. Crash a ring-3 process in session 1 — verify session 2 is unaffected and session 1 continues
4. Log out of session 2 — verify all session 2 processes are gone, slot resets to `SESSION_EMPTY`
5. Fill all 4 session slots — verify 5th login attempt is rejected gracefully

---

## Error Handling

| Scenario | Handling |
|----------|---------|
| Ring 3 page fault | `#PF` handler checks CPL; ring 3 → kill process, log to dmesg, session continues |
| Ring 3 GPF | Same as page fault — kill process, session unaffected |
| ext2 read error | ATA retry once; second failure returns `-EIO` up VFS chain; app sees read error |
| ext2 write error | Return `-EIO`; do not corrupt existing data; log to dmesg |
| TLS handshake failure | Browser shows inline error page; socket closed cleanly; no kernel impact |
| Invalid TLS certificate | Browser shows certificate warning; user can choose to abort |
| Session OOM | Refuse new app launches in that session; show OOM dialog; other sessions unaffected |
| All 4 sessions full | Show "no session slots available" on login attempt |
| Corrupt ext2 superblock | Kernel panics at mount with "ext2: invalid magic" — requires `make format-disk` |

---

## Build System Changes

```makefile
# Grow and format disk
format-disk:
    dd if=/dev/zero of=careos.img bs=1M count=512
    mkfs.ext2 -b 1024 -I 128 careos.img

# Include mbedTLS sources
MBEDTLS_SRCS := $(wildcard net/mbedtls/*.c)

# Include Links2 sources  
LINKS2_SRCS := $(wildcard apps/links2/*.c)

# Add to kernel objects
OBJS += net/tls.o $(MBEDTLS_SRCS:.c=.o) $(LINKS2_SRCS:.c=.o) \
        kernel/ext2.o gui/session.o net/ca_bundle.o
```

**Host build dependencies (new):**
- `e2fsprogs` — for `mkfs.ext2` and `e2fsck` (development/testing only, not shipped)
- `xxd` — for CA bundle conversion (one-time, at setup)

---

## Implementation Order

Each phase must be complete and tested before the next begins.

| Phase | Work | Gate |
|-------|------|------|
| 1 | Ring 3 user-space — GDT, TSS, per-process page tables, syscall gate, ELF into user-space | Minimal ELF exits cleanly; ring 3 page fault kills process, not kernel |
| 2 | ext2 filesystem — driver, VFS adapter, format-disk, user home dirs | `e2fsck` reports clean after all operations |
| 3 | mbedTLS — vendor, glue layer, entropy, CA bundle, HTTPS GET | `https://example.com` loads successfully |
| 4 | Links2 — vendor, FB + input + socket shim, wire to app_browser | Wikipedia article renders in browser window |
| 5 | Multi-session GUI — virtual framebuffers, session struct, session switching, per-session process ownership | Two users concurrently active; crash in one doesn't affect the other |
