# CareOS v9 — Full System Overview

_Generated from a source-level audit of the repository on 2026-07-20. This is a snapshot of what actually exists in the code, not the aspirational docs. Use it as the starting point for deciding what to build next._

_See [CAREOS_ROADMAP.md](./CAREOS_ROADMAP.md) for the phased plan to take CareOS from where §12 below leaves it toward a modern desktop platform._

---

## 0. The most important fact: this repo contains TWO unrelated operating systems

They share branding (name, heart-C logo, colors, the "Care Language" name) but **zero code**.

| | **CareOS Kernel** (bare-metal) | **CareOS Linux** (Arch-based distro) |
|---|---|---|
| What it is | A from-scratch x86_64 kernel + GUI + apps, written in C/ASM | A customized Arch Linux live/install ISO (KDE Plasma, SDDM, systemd) |
| Lives in | `boot/`, `kernel/`, `drivers/`, `gui/`, `apps/`, `shell/`, `net/`, `include/libc/`, `doomgeneric/` | `iso/airootfs/`, `iso/packages.x86_64`, `iso/pacman.conf`, `iso/profiledef.sh`, `iso/build.sh` |
| Built with | Root `Makefile`: `gcc -ffreestanding`, `nasm`, `ld`, GRUB Multiboot2, run in QEMU | `mkarchiso` (archiso), needs root + `pacman -S archiso` |
| Output | `careos.iso` (21 MB GRUB rescue ISO), `careos.img` (4 GB raw ATA disk image) | `out/CareOS-<date>-x86_64.iso` (~2.9 GB each, 4 builds present, 2026-05-05 → 2026-06-11) |
| Status | Actively developed, boots and runs real apps in QEMU | A later pivot — see `docs/superpowers/specs/2026-05-05-kde-plasma-distro-design.md`, which literally says "Convert CareOS from a bare-metal x86 OS into an Arch Linux-based desktop distribution shipping KDE Plasma" |

**Practical implication:** both trees coexist inside `iso/`, and the bare-metal kernel's `grub-mkrescue -o careos.iso iso/` build step technically scoops up the unrelated Arch `airootfs` tree as extra (harmless but confusing) ISO content. Decide which of these two projects "CareOS" actually means going forward — the rest of this doc treats them separately.

---

## 1. CareOS Kernel (bare-metal x86_64 OS)

### 1.1 Boot process
- `boot/boot.asm`: 32-bit Multiboot2 entry point (`_start`), requests a 1920×1080×32 framebuffer via a `.multiboot2` tag.
- Builds temporary identity-mapped page tables (PML4→PDPT→PD, 2 MB huge pages, first 1 GB), enables PAE, sets `EFER.LME`, flips `CR0.PG`, loads a minimal 64-bit GDT, far-jumps to `_start64`.
- `_start64` sets up segments and calls `kernel_main(magic, mbi_ptr)` per SysV ABI (`rdi`/`rsi`).
- `kernel.ld`: loads at 1 MiB, sections `.multiboot2/.text/.rodata/.data/.bss`, entry `_start`.
- History: `MIGRATION.md` confirms this was migrated from an earlier **32-bit** kernel.

### 1.2 `kernel_main` boot sequence (in order, logged to serial via `slog_stage`)
PMM → GDT → SSE enable → VGA terminal → serial → IDT → MB2 magic check (halts on mismatch) → PIT timer → PS/2 keyboard → VFS init (seeds `/usr/bin`, `/var/pkg`, `/System/version`, sample `.cl` scripts) → paging → syscalls → **ring-3 smoke test** (embeds and runs `tests/ring3_exit.asm` via `int 0x80`) → RTC → PCI → ATA → ext2 mount → settings → home persistence → embeds `DOOM1.WAD` into `/home/user/DOOM1.WAD` → users → carepkg (package manager) → scheduler → e1000 NIC → net stack → framebuffer detection (MB2 tag, falls back to BGA/VBE port probing) → GUI init/run. If `gui_run()` ever returns, falls back to the text-mode `shell_run()`.

### 1.3 Memory management
- **Heap** (`kernel/memory.c`): bump/free-list allocator over a **static 192 MB `heap_mem[]` array**. `kmalloc`/`kfree` do first-fit + coalescing.
- **Physical frames**: bitmap allocator over a **hardcoded 4 GB** physical range; first 256 MB reserved at boot.
- **Paging** (`kernel/paging.c`): real 4-level x86_64 paging (PML4→PDPT→PD→PT). Identity-maps first 2 GB with 2 MB huge pages. Page-fault handler (int 14) calls `kernel_panic`. Per-process directories exist (`paging_create_dir`/`paging_map`/`paging_switch_dir`) but **all directories share PML4[0] (the kernel identity map)** — so there is **no real user/kernel address-space isolation**, just page permission bits.
- `paging_map_mmio` maps arbitrary physical ranges for MMIO (e1000, VBE framebuffer).
- **Known bug:** `paging_free_dir` only frees the PML4 frame — leaks the PDPT/PD/PT frames underneath.
- **Footprint concern:** `fs_node_t` embeds a `5 MB` inline data buffer per node, and the VFS node pool is 128 nodes → **~640 MB of static BSS** for the in-memory VFS alone, on top of the 192 MB heap. This is why QEMU is launched with `-m 4096M`.

### 1.4 GDT / IDT / interrupts (`kernel/gdt_idt.c`)
8-entry 64-bit GDT (null, kernel CS/DS, user CS/DS, TSS). 256-entry IDT, PIC remapped to 0x20/0x28. IRQ0 (timer) and IRQ1 (keyboard) unmasked by default.
- **Quirk:** `timer_init()` registers IRQ0 → `timer_irq`, but `scheduler_init()` later overwrites the same vector with `scheduler_tick` (which internally also advances the timer) — `timer_irq` becomes dead code post-scheduler-init. Not broken, but worth cleaning up.

### 1.5 Scheduler (`kernel/scheduler.c`)
Preemptive round-robin, x86_64 TSS, `MAX_TASKS = 32`, fixed 5-tick (50 ms) timeslice. Context switch is hand-written asm (GPRs + RFLAGS + FXSAVE/FXRSTOR for SSE state, conditional CR3 reload). User tasks enter ring 3 via `iretq` (CS=0x1B, SS=0x23), fixed user stack top at `0xBFF00000`. Signals are minimal: `SIGKILL` marks task dead, `SIGINT` injects Ctrl+C into the keyboard buffer.
- **Stub:** `task_list()` is an explicit no-op (`/* omitted for brevity */`).

### 1.6 Syscalls (`kernel/syscall.c`)
INT 0x80, 11-entry table: `exit, read, write, open, close, sleep, getpid, brk, gettime, yield`. 32-entry FD table (stdin/stdout/stderr pre-opened). `sys_read`/`sys_write` operate directly on `fs_node_t` buffers.
- `user_ptr_ok` is a **coarse heuristic range check**, not real memory protection (the kernel identity-maps the first 2 GB into every address space anyway).
- `sys_brk` doesn't grow any mapping — just tracks a counter.

### 1.7 VFS (`kernel/vfs.c`)
Hybrid in-memory tree + ext2-backed subtree. `/home` bridges to a real ext2 inode when storage is online, with a **custom fallback binary format** ("homefs", magic `CHOM`, FNV-1a checksum) written directly to raw sectors when ext2 mount fails. Standard dirs seeded at boot (`/etc/passwd`, `/etc/os-release`, `/proc/cpuinfo`, `/proc/meminfo`, placeholder `/usr/bin/*` files containing the literal string `"ELF binary"`).

### 1.8 ext2 driver (`kernel/ext2.c`)
A genuinely complete from-scratch implementation: superblock mount, block-group descriptors, inode read/write, single+double indirect block resolution (**no triple-indirect**), directory add/lookup/list/unlink, block/inode bitmap allocation, `ext2_create_file`/`mkdir`/`unlink`/`write_data`. Formatted via `make format-disk` (1 KB blocks, 128-byte inodes).
- **Limitation:** the write path only supports **direct blocks** — files/dirs can't grow past 12 direct blocks (12 KB) via writes, even though the read path supports indirect blocks.

### 1.9 ELF loader (`kernel/elf.c`)
ELF64 validation (`ET_EXEC`, `EM_X86_64`). `elf_load_vfs` = flat kernel-space load, no relocation. `elf_load_user` = real ring-3 loader: fresh page directory, per-page PT_LOAD mapping with correct R/W/U flags, enforces `0x400000`–`0xBFF00000` user address range, creates a ring-3 task.

### 1.10 Care Language interpreter (`kernel/care_lang.c`)
A real, hand-written tree-walking interpreter for `.cl` scripts: lexer, recursive-descent parser, `var/print/if-else/while` (capped at 10,000 iterations), `func` (up to 4 args, single-level closures over globals), string/numeric values, native bindings (`sys_alert`, `sys_window`, `sys_beep`, `sys_exec`, `sys_launch`, `sys_set_theme`, `sys_set_wallpaper`, `sys_get_setting`, `sys_username`, `sys_is_root`, `sys_first_run`). No arrays, no proper closures, no REPL (explicitly descoped).
- **Startup scripts** (`kernel/rc_care.c`, the newest kernel feature per the 2026-07-16 spec): runs `/etc/rc.care` then `/home/<user>/rc.care` at login. Shows a modal error dialog on script failure but always continues booting.

### 1.11 Package manager — "carepkg" (`kernel/carepkg.c`)
Custom `.care`/`.cpk` package format: text manifest (`name=`, `version=`, `exec=`, `deps=`, etc.) + `---FILES---` blocks. Installs to `/apps/<name>/`, registry (max 64) persisted to `/var/pkg/installed.db`. Dependency checking, install/remove/list/info/create via CLI. Two demo packages (`hello`, `quickcalc`) auto-install at boot.

### 1.12 Users (`kernel/users.c`)
Up to 16 accounts. Password hashing: 512-round salted FNV-variant — **adequate for a toy OS, not a real KDF**. Failed-login lockout (5 attempts → 30s). Persistent DB on raw ATA sectors with a 3-generation format migration (v1→v2→v3) + checksum.
- **Default credentials shipped in source:** `root`/`root`, `user`/`CareOS123` — flag this if this ever leaves a hobby/local context.
- Session model is a **single global session**, not the multi-session design originally scoped.

### 1.13 Settings (`kernel/settings.c`)
Theme, mouse sensitivity, boot_fast, clock format, wallpaper, taskbar layout, VESA resolution, Wi-Fi profile — persisted on raw ATA sectors, same versioned-migration + checksum pattern as users.c.

### 1.14 Pipes (`kernel/pipe.c`)
Trivial fixed pool of 8 single-buffer pipes, no real blocking producer/consumer semantics — used by the shell for `|` (captures left command's output, feeds it as literal stdin to the right command).

### 1.15 libc shim (`kernel/libc_shim.c` + `include/libc/*`)
Just enough of a hosted C library to compile `doomgeneric` and other ported C: `malloc/free/calloc/realloc` (wrap `kmalloc`/`kfree`), 16-slot `FILE*` over `fs_node_t` (**`fwrite` is a no-op — files are effectively read-only** through this API), `printf` family, minimal `string.h`/`ctype.h`, `sscanf` (only `%x`/`%d`), `atof`.
- **Non-functional stubs:** `setjmp`/`longjmp` don't save/restore state (`longjmp` infinite-loops); `signal()`/`raise()` are no-ops; raw POSIX `open/close/read/write/lseek/stat/opendir/readdir` are unconditional failure stubs; `getenv` always NULL; `exit()` calls `kernel_panic`.

---

## 2. Drivers (`drivers/`)

| Driver | File | Details |
|---|---|---|
| Keyboard | `drivers/keyboard.c` | PS/2, IRQ1, full scancode-set-1→ASCII (normal+shift), Ctrl+letter control codes, 256-byte ring buffer |
| PCI | `drivers/pci.c` | Brute-force config scan (bus 0–7 × dev 0–31 × fn 0–7), vendor/device/class name tables |
| Timer/PIT | `drivers/timer.c` | Channel 0, mode 3, default 100 Hz |
| RTC | `drivers/rtc.c` | CMOS 0x70/0x71, BCD conversion, century-aware year |
| Speaker | `drivers/speaker.c` | PIT channel 2 PC speaker beeper |
| VGA/VESA/BGA | `drivers/vga.c`, `drivers/vesa.c` | Text-mode fallback; Bochs Graphics Adapter mode-switch via I/O ports, resolutions up to 2560×1440 |
| ATA/IDE | `drivers/storage/ata.c` | Primary channel/master only, PIO mode, **LBA28 only (128 GB cap, no LBA48)**; has an 8-entry sector cache that **ext2.c bypasses entirely** — likely dead code |
| Ethernet | `drivers/net/e1000.c` | Intel 8254x MMIO driver for QEMU's emulated NIC, 8-descriptor TX/RX rings, **polling only, interrupts masked** |

---

## 3. GUI (`gui/`)

32-bpp linear framebuffer compositor/window manager. `gui_backup_v9/` and `gui_v9_original/` are earlier snapshots, **not referenced by the Makefile** (dead reference copies).

- **`gfx.c`** (727 ln): double-buffered renderer, SSE2-accelerated blit with scalar fallback, dirty-rectangle tracking (32 rects max), runtime pixel-format detection, glyph cache, primitives (rounded rects, circles, lines, triangles, gradients, alpha blend), clip-rect stack, offscreen per-window buffers.
- **`wm.c`** (924 ln): window manager, `MAX_WINDOWS = 24`. Drag/resize (8-direction), maximize/minimize/restore, cascading placement, z-order/focus, Alt+Tab, snap-to-quadrant with animated transitions, magnetic edge-snap, 4 virtual desktops + sticky windows, per-app click/key/draw dispatch table.
- **`mouse.c`**: PS/2, IRQ12, IntelliMouse scroll-wheel auto-detection, Q8.8 fixed-point sub-pixel tracking, clamped raw deltas, custom 13×19 cursor with drop shadow.
- **`gui.c`**: boot splash (6 stages), full login/signup screen (lockout after 5 fails/10s), main event loop, global hotkeys (Alt+Tab, ESC, Ctrl+1-4 desktops, Ctrl+Alt+H/J/K/L snapping), 10-min idle auto-lock, 30s Matrix-style screensaver, periodic sysmon/netmon/notification ticks.
- **`taskbar.c`**: dock-style pinned+running app tray.
- **`launcher.c`**: full-screen app launcher overlay.
- **`theme.c`**: `theme_dark`/`theme_light`, ~25 semantic color roles each, `COL_*` macros.
- **`widget.c`** (99 ln): minimal retained-mode widget tree (panel/button/label/input/scrollbar) — **under-utilized**; most apps draw immediate-mode via `gfx_*` directly instead of using it.

### ⚠️ Known broken feature: DOOM never actually renders
`wm_open` allocates a per-window offscreen `win_buffer`, but **nothing in the current `draw_window()` path ever calls `gfx_blit()`** to composite it to screen — `gfx_blit` exists in `gfx.c` but is never called anywhere in the live `gui/` tree. `app_doom.c`'s `DG_DrawFrame()` writes into `win_buffer.pixels` expecting compositing that never happens. Worse, `app_doom_init()` sets `w->app = APP_NONE`, so the WM's `switch(w->app)` dispatch never routes draw/key events to `app_doom_draw`/`app_doom_key` either. **Net result: DOOM runs as a real kernel task but its window renders blank and doesn't take input.** (`app_maze.c`/`app_3d.c` avoid this bug by drawing directly via `gfx_rect`/`gfx_setpixel` and staying in the dispatch switch.) The README even hints at this: _"even DOOM running natively inside it. (No it will actually crash but oh well.)"_

---

## 4. Applications (`apps/`)

Uniform interface: `app_X_init/draw/key/click(window_t*)`, dispatched from `gui/wm.c`. State lives inline in the shared `window_t` struct rather than per-app heap allocations.

| App | File | Notes |
|---|---|---|
| Terminal | `app_terminal.c` (410 ln) | Independent mini-shell (`ls/cd/pwd/cat/mkdir/rm/touch/echo/ps/carepkg/whoami/ifconfig/uname/date/free/uptime/sysinfo/network/dmesg/settings/wifi/care/ping/curl`), scrollback, clipboard |
| Notes | `app_notes.c` | Plain-text scratchpad |
| Files | `app_files.c` (464 ln) | File manager over the VFS |
| System Monitor | `app_sysmon.c` (182 ln) | 4 tabs, CPU/mem history graphs |
| Calculator | `app_calc.c` | 4-function calc |
| About | `app_about.c` | Static info screen |
| Settings | `app_settings.c` (446 ln) | Account/Display/Personalize/Network/System tabs |
| Web Browser | `app_browser.c` (1240 ln, largest app) | Mini HTML renderer (title/links/tables/forms/headings), 4 tabs, 8 bookmarks, 10-entry history, find-in-page, view-source, chunked-transfer decode, redirect-loop guard. Uses plain-HTTP `http_get`; HTTPS (`net/tls.c`) exists but wiring into the browser UI wasn't confirmed |
| Package Manager | `app_pkgmgr.c` (319 ln) | GUI front-end to carepkg |
| Code Editor | `app_editor.c` (574 ln) | Explorer/Docs sidebar, file save/open |
| Paint | `app_paint.c` | Freehand pixel painting |
| Clock | `app_clock.c` | Analog/digital display |
| Network Monitor | `app_netmon.c` | Live net stats |
| Users | `app_users.c` (315 ln) | GUI account management |
| Maze | `app_maze.c` | Iterative recursive-backtracker generator, 21×15 grid, multi-level |
| 3D Demo | `app_3d.c` | Wolfenstein-3D-style raycaster, fixed-point trig, 10×10 tile map |
| DOOM | `app_doom.c` (84 ln) | Wires `doomgeneric`; **broken compositing (see §3)**; `DG_GetKey` has a literal `TODO: Map CareOS keyboard driver to Doom keys` — forwards raw ASCII, no scancode mapping |
| Help | `app_help.c` | In-app docs viewer |

`apps_v9_original/` is a pre-3D/Maze/DOOM snapshot kept as rollback reference, not built by the Makefile.

**Launch paths:** desktop icons (17 entries in `wm.c`), taskbar dock, app launcher overlay, or CareLang's `sys_launch("name")`. Single-instance-per-app enforced (refocuses existing window instead of relaunching).

---

## 5. Shell (`shell/shell.c`, 799 ln)

A **separate, independent** text-mode (VGA terminal) shell, only reached if `gui_run()` returns or on the headless QEMU target. Line editing, 64-entry history, tab-completion scaffolding, colored prompt, pipe support (same simulated-pipe model as `kernel/pipe.c`).

**Maintenance smell:** `shell/shell.c` and `apps/app_terminal.c` independently reimplement almost the same command set (`ls/cd/pwd/cat/ping/wifi/settings/...`) — near-total duplication between the two.

---

## 6. Networking (`net/`)

A real, from-scratch stack — not a vendored library.

- **`net.c`** (772 ln): Ethernet framing, ARP (32-entry cache), IPv4 (checksum; **no fragmentation**), ICMP echo, UDP send, minimal hand-rolled **TCP** (SYN/ACK/PSH/FIN, blocking 3-way-handshake with 500-tick deadline, **no retransmission/congestion control/proper windowing**), 16-socket table, real UDP **DNS** resolution (`dns_query_a`, name-compression aware) with a small hardcoded fallback table tried first, and `http_get` (HTTP/1.0 client for the browser and `curl` shell command).
- **DHCP is fully simulated**: `net_dhcp_renew()` sends no packets at all — just hardcodes `10.0.2.15/24` gw `10.0.2.2` (QEMU user-net defaults) whenever the link/Wi-Fi is "up."
- **Wi-Fi is entirely fake**: no wireless driver exists; `wifi scan/connect/disconnect` just flip a settings flag and call the same fake DHCP path (cosmetic; CareOS only ever runs on emulated wired e1000 hardware).
- **`sha256.c`, `aes_gcm.c`, `x25519.c`**: real from-scratch crypto primitives — no OpenSSL/mbedTLS dependency (an earlier spec proposed vendoring mbedTLS; the team built their own instead).
- **`tls.c`** (599 ln): a genuine **TLS 1.3 client** (RFC 8446) — x25519 key exchange, full handshake state machine, HKDF key schedule. **Certificate verification is explicitly skipped ("trust all")** by the file's own header comment — no PKI trust chain checking. `https_get` is declared but its wiring into the browser UI wasn't confirmed during the audit.

---

## 7. Boot & ISO tooling

- `boot/boot.asm` — see §1.1.
- `iso/boot/grub/grub.cfg`, `iso/grub/grub.cfg` — GRUB Multiboot2 entries pointing at `iso/boot/kernel.elf`.
- `iso/grub/themes/careos/` — GRUB theme for the bare-metal kernel's ISO (separate from the Arch distro's own GRUB theme under `iso/airootfs/usr/share/grub/themes/careos/`).
- `iso/airootfs/**` — the entire **Arch Linux distro root overlay**: systemd units, `/etc` configs, SDDM theme (`Main.qml`), Plymouth boot theme, KDE Plasma look-and-feel package, Konsole/Kvantum branding, **Calamares installer branding + module config (staged, not confirmed wired to a live install flow — the actual installer is a script, `careos-install`)**, `.desktop` shortcuts, and Python userland tools:
  - `usr/bin/cl` — a **315-line Python re-implementation of the Care Language interpreter** (separate from the kernel's C one)
  - `carepkg`, `carectl`, `careos-control`, `carescript-studio` (PyQt6 IDE), `bit-pet` (desktop pet)
- `iso/packages.x86_64` (188 ln) — archiso package manifest: base/base-devel/linux, GRUB+os-prober+memtest, NetworkManager, Xorg, SDDM, KDE Plasma.
- `iso/build.sh` — root-required, generates a GRUB background PNG via a pure-Python PNG encoder, then calls `mkarchiso`.
- `iso/profiledef.sh` — ISO label `CAREOS_<yyyymm>`, **UEFI-only GRUB boot (no legacy/BIOS boot mode configured)**, squashfs zstd level-15.

---

## 8. Tools & tests

- `tools/gen-sounds.py` — Python utility, presumably generates UI sound assets for `iso/airootfs/usr/share/sounds/CareOS/`.
- `tests/ring3_exit.asm` — 9-line program issuing `int 0x80` (`SYS_EXIT`) to verify ring-3→ring-0 syscalls. **Actually embedded and run automatically every boot** — the closest thing to a real regression test in the project.
- `tests/ring3_fault.asm` — presumably a negative-path companion test, **not compiled into the kernel image or exercised by `make run`** (builds standalone via `make test-elfs` only).
- No host-side unit test framework or CI exists anywhere. Verification is exclusively "build → boot in QEMU → read serial log."

---

## 9. `doomgeneric/`

Vendored copy of the [doomgeneric](https://github.com/ozkl/doomgeneric) portable DOOM engine, own `.git`. CareOS implements the platform callback layer in `apps/app_doom.c` (`DG_Init/DG_DrawFrame/DG_SleepMs/DG_GetTicksMs/DG_GetKey/DG_SetWindowTitle`). Makefile excludes all other backends (SDL/X11/Windows/etc). `DOOM1.WAD` is embedded directly into the kernel ELF via `objcopy` and materialized at `/home/user/DOOM1.WAD` at boot. Rendering is broken end-to-end per §3.

---

## 10. Root-level files

- **`Makefile`** — single flat `make`/`make run` workflow, host `gcc`/`ld`/`nasm` (`-m64 -ffreestanding`, no enforced cross-compiler), `run-kvm`, `run-nowindow` (headless), `debug` (auto-attaches GDB to QEMU gdbstub, breaks at `kernel_main`), `format-disk`/`reset-disk`, `clean` (preserves disk image) vs `clean-all`.
- **`CHANGES_v5.md`** — documents a prior architecture/code-review pass: fixed hardcoded-BGR framebuffer bug, missing MB2 magic validation, backbuffer-aliasing corruption on `kmalloc` failure, a signed/unsigned coordinate cast bug (could write to address 0), plus SSE2 blit/dirty-rect/glyph-cache perf work.
- **`CHANGES_v6.md`** — login-gate hardening (removed auto-login, added lockout), VFS reliability fixes, bounded-write path-string fix, ATA-backed user persistence.
- **`MIGRATION.md`** — the (completed) 32-bit → x86_64 migration checklist.
- **`BUILD_INFO.txt`** — says **"CareOS v6 — Desktop Release Build, March 2026"**, stale relative to the current v9 codebase (`kernel_main` seeds `/System/version` as "CareOS v9 / Kernel: 9.0.0").
- **`README.md`** — candidly notes DOOM's instability.
- **`setup.sh`** — dev-environment bootstrap, not read in depth.
- **Cruft:** `mouse_test2.log`, `mouse_test3.log` (debug logs left in repo root), and an oddly-named `-boot` file (likely an accidental artifact from a mistyped command).

---

## 11. `docs/superpowers/` — development history

Plan+spec pairs tracking real development history via an AI-agent workflow:
1. **2026-04-23** — "usable OS" ambition doc (ring-3, ext2, HTTPS, multi-user) — only partially realized (see gaps below).
2. **2026-04-26** — 1080p/UX pass.
3. **2026-04-28** — "beefier browser" (pipes/signals/BGA/browser tabs).
4. **2026-05-05** — **the pivot to the Arch/KDE Plasma distro** (see §0).
5. **2026-05-06** — apps/branding/perf (CareScript Studio, logo, zram).
6. **2026-06-11** — theming/pet (Bit the pixel pet, Plymouth/SDDM theming).
7. **2026-07-16** — rc.care startup scripts (most recent kernel-side feature).

These are the most authoritative source of "what was intended" vs. "what's actually built" — several original goals (mbedTLS/Links2 integration, a live Calamares graphical install, true multi-session GUI) were explicitly descoped or only partially completed.

`out/` contains only the Arch-distro ISOs (4 builds, ~2.9 GB each, dated to match the plan cadence) — entirely separate from the bare-metal kernel's `careos.iso`/`careos.img` (21 MB / 4 GB, most recently rebuilt today).

---

## 12. Everything that's incomplete, stubbed, or broken (punch list for "what's next")

1. **DOOM window never composites to screen** — `win_buffer` is allocated but `gfx_blit()` is never called anywhere in the live `gui/` tree; `app_doom_init` also sets `w->app = APP_NONE`, breaking WM draw/key dispatch for that window entirely.
2. **DOOM keyboard mapping is a literal `TODO`** — raw ASCII forwarded, no scancode→DOOM-key translation.
3. **DHCP and Wi-Fi are fully simulated** — no real packet exchange, no wireless driver.
4. **TLS 1.3 client skips certificate verification entirely** ("trust all") — real crypto, no trust chain.
5. **ATA driver has no LBA48 support** (128 GB cap); its sector cache is dead code (ext2.c bypasses it).
6. **ext2 write path has no indirect-block allocation** — writes cap at 12 direct blocks (12 KB) even though reads support indirect blocks.
7. **`paging_free_dir` leaks PDPT/PD/PT frames** (only frees the PML4).
8. **No real address-space isolation** — every process shares the kernel's PML4[0] identity map; `user_ptr_ok` is a heuristic, not page-table-enforced.
9. **`setjmp`/`longjmp` are non-functional** (`longjmp` infinite-loops).
10. **`task_list()` is an explicit no-op stub.**
11. **Session model is single-global**, not the multi-session design originally scoped.
12. **`gui/widget.c` is under-adopted** — most apps bypass it for immediate-mode drawing.
13. **Heavy duplication** between `shell/shell.c` and `apps/app_terminal.c` — two near-identical command interpreters.
14. **`BUILD_INFO.txt` is stale** ("v6" vs actual v9).
15. **~640 MB static BSS** for the VFS node pool (128 nodes × 5 MB inline data) — worth reviewing for a hobby kernel's memory footprint.
16. **Weak default credentials shipped in source** (`root`/`root`, `user`/`CareOS123`).
17. **`gui_backup_v9/`, `gui_v9_original/`, `apps_v9_original/`** — unused historical snapshots, not referenced by the Makefile.
18. **Repo-root cruft** — `mouse_test2.log`, `mouse_test3.log`, an oddly-named `-boot` file.
19. **Calamares graphical installer (Arch distro side) is staged but not confirmed wired up** — the real installer path today is the `careos-install` script.
20. **`tests/ring3_fault.asm` exists but isn't embedded/run** — only the happy-path smoke test (`ring3_exit`) actually executes at boot.
21. **No CI, no host-side unit tests** — verification is exclusively manual (build → QEMU → serial log).
22. **Two unrelated projects share the `iso/` directory and the "CareOS" name** — worth resolving which direction (bare-metal kernel vs. Arch/KDE distro) is the actual product going forward, since effort is currently split across both.

---

_This file was generated by an automated source audit and reflects code state as of 2026-07-20. Re-run the audit after significant changes rather than trusting this as a live source of truth._
