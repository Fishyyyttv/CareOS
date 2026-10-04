# CareOS v10 Redesign — Roadmap and Parallel Execution

**Date:** 2026-10-03 · **Branch:** `redesign/v10` (off `perf/qol-30fps`)
**Design source:** the "CareOS Design" canvas, https://claude.ai/artifact/PFBAuwUucbFqSAnw4ZidZ1 (30 artboards). A local copy of the artboards is attached to each phase plan as its reference.

## Goal

Make CareOS look and behave like the canvas, and make it use the hardware the VM is given (all cores, all RAM, any disk, a real virtual GPU driver).

## Decisions taken in brainstorming (2026-10-03)

| Topic | Decision |
|---|---|
| Decomposition | Seven phases, each with its own spec → plan → implementation |
| Glass | **Real blur**, made affordable by a per-panel backdrop cache, half-resolution blur and the worker pool |
| Motion | The 2026-09-26 motion plan becomes phase 5, rebased onto the new look; its "no glassmorphism / solid navy" visuals are **dropped** |
| Data | **Real data or hide.** Nothing in the UI shows invented values (no fake weather, battery, Bluetooth, media) |
| Architecture | **Design tokens + shared UI kit** (`gui/ui.c`), font roles, build-time icons |
| Window controls | Traffic lights on the **left**, close/min/max (red/yellow/green), as in the app artboards |
| Multi-core | **Worker pool first** (phase 0); full SMP scheduling is phase 6 |
| GPU | VMSVGA (SVGA II) 2D driver. VirtualBox never passes through the physical GPU; SVGA3D is out of scope |

## Phases

| # | Phase | Spec | Depends on |
|---|---|---|---|
| 0 | Hardware platform: ACPI, all cores (worker pool), all RAM, LBA48/DMA IDE + AHCI, VMSVGA, hwinfo | `2026-10-03-careos-hardware-platform-design.md` | — |
| 1 | Visual system + shell: tokens, fonts, icons, wallpaper, UI kit, blur cache, window chrome, menu bar, dock, desktop, launcher, Control Center, notifications, Wi-Fi menu | `2026-10-03-careos-visual-system-shell-design.md` | kit interface freeze (wave step W0) |
| 2 | Start screens: boot, login, sign up, lock, power, onboarding (4 steps) | `2026-10-03-careos-start-screens-design.md` | W0 |
| 3 | App restyles: Terminal, Files, Browser, Editor, Notes, Packages, Settings | `2026-10-03-careos-app-restyle-design.md` | W0 |
| 4 | Pip, the desktop pet (moods, Pip's Home, desktop roaming; lock/power cameos) | later | 1, 2 |
| 5 | Motion physics (springs, Genie, dock magnify, rubber-band, notification glide) | later (rebase of 2026-09-26 plan) | 1 |
| 6 | Full SMP scheduler (per-CPU run queues, kernel locking) | later | 0 |

## Parallel execution

### Wave 1: phases 0–3, eight agents (two per phase)

**Step W0 (serial, one agent, before fan-out).** Lands on `redesign/v10`:
1. Split `gui/gui.c` into owned files with no behaviour change:
   - `gui/shell/menubar.c`
   - `gui/shell/control_center.c`
   - `gui/shell/notify_center.c`
   - `gui/session/login.c` (login, greeter, account creation, screensaver)
   - `gui/session/boot_splash.c`
   - `gui/gui.c`, which keeps `gui_init`, `gui_run` and `enter_desktop`
2. Freeze the kit interface:
   - `gui/ui.h` (component API)
   - new `theme_t` token names
   - `font_role_t` and `gfx_str_role`
   - `gfx_draw_icon` size contract
   - The implementations start as stubs drawn with today's primitives, so everything compiles and runs.
3. Freeze the hardware interfaces: `kernel/hwinfo.h`, `kernel/workpool.h` (single-core inline fallback), `drivers/storage/blockdev.h`.
4. The build succeeds, `make host-test` passes, and the QEMU boot smoke test passes.

**Fan-out.** Each agent works in its own git worktree under `D:\Users\mhetm\Downloads\CareOS_v9_full\wt\<agent>` on branch `redesign/v10-<agent>`:

| Agent | Phase | Owns |
|---|---|---|
| hw-a | 0 | `kernel/acpi.*`, `kernel/smp.*`, `kernel/ap_trampoline.asm`, `kernel/workpool.*`, LAPIC/IOAPIC, `kernel/paging.c` + PMM (>4 GB), `kernel/hwinfo.*` |
| hw-b | 0 | `drivers/storage/*` (blockdev, IDE LBA48/DMA, AHCI), `drivers/svga.*`, `drivers/vesa.c` probe order, Settings › About data provider |
| vis-a | 1 | `gui/theme.c`, `gui/font.*`, `gui/fonts/*` (Figtree, Bricolage), `tools/gen-font.py`, `tools/gen-careos-icons.py` + icon archive, `tools/gen-wallpaper.py`, `gui/gfx.c` AA/blur primitives, `gui/ui.c` kit + blur cache |
| vis-b | 1 | `gui/wm.c` window chrome, `gui/shell/*`, `gui/taskbar.c` (dock), `gui/launcher.c`, `gui/widgets.c`, desktop hero |
| start-a | 2 | `gui/session/boot_splash.c`, `gui/session/login.c` (login + sign up), `gui/session/lock.c`, `gui/session/power_menu.c` |
| start-b | 2 | `gui/session/onboarding.c` (4 steps), first-run flag, persistence of onboarding choices in `kernel/settings.c` |
| app-a | 3 | `apps/app_terminal.c`, `apps/app_editor.c`, `apps/app_notes.c` |
| app-b | 3 | `apps/app_files.c`, `apps/app_browser.c`, `apps/app_pkgmgr.c`, `apps/app_settings.c` |

**Shared-file rules:**
- `Makefile`: each agent only *appends* its sources in a block marked with its agent name.
- `gui/gui.h` and `kernel/kernel.h`: no edits after W0, except new prototypes in a per-agent block. Phase-specific declarations go in the phase's own header.
- `kernel/kernel.c` init order: only hw-a and hw-b touch it, each in its marked block.
- Kit gaps: an agent that needs a kit component W0 didn't freeze adds a local `static` helper and reports it; vis-a promotes it into `ui.c` at merge.

**Build and QA isolation:**
- Every agent builds in its own worktree through WSL.
- QEMU QA uses per-agent sockets and per-agent absolute screenshot paths under `D:\...\wt\<agent>\qa-out`.
- No scratch on C: (it's full).
- Concurrent builds are limited to **3** at a time. A lock file `D:\Users\mhetm\Downloads\CareOS_v9_full\wt\.build-lock-N` is held for the duration of `make`, because WSL is fragile under heavy parallel writes.

**Merge order back into `redesign/v10`:** hw-a → hw-b → vis-a → vis-b → start-a → start-b → app-a → app-b. Each merge has to pass the build, `make host-test` and the QEMU smoke test. Then a two-stage review (spec compliance, then code quality).

### Pause

After wave 1 merges: full QA pass on QEMU and on the reconfigured VirtualBox VM (4 cores, 20 GB RAM, VMSVGA 128 MB, SATA disk), with screenshots next to the artboards. The user reviews before wave 2.

### Wave 2: phases 4–6, six agents

Specs and plans for phases 4, 5 and 6 are written at the pause, against the merged code. Phase 6 runs only once phase 0 is stable on the VM.

## VirtualBox VM changes (need user confirmation before running)

`careos` VM today: `vboxvga`, 9 MB VRAM, IDE disk, 4 CPUs, 20 GB RAM. Proposed:
- `--graphicscontroller vmsvga --vram 128`
- Add a SATA (IntelAhci) controller and move the disk to it. Keep the ISO on IDE.
