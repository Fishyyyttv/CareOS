# CareOS v10 W0 — Split and Interface Freeze Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prepare `redesign/v10` so eight agents can work on phases 0–3 in parallel without editing the same files. Split `gui/gui.c` into owned files, and freeze every interface the phases share, with working stub implementations.

**Architecture:**
- No visible behaviour change. Code moves out of `gui/gui.c` verbatim.
- New shared headers declare the final APIs: `gui/ui.h`, font roles, theme tokens, `kernel/hwinfo.h`, `kernel/workpool.h`, `drivers/storage/blockdev.h`, window toolbar/menu hooks, `notify_push_ex`, settings v6 and user DB v7 fields.
- The stubs work today using existing primitives. Each phase then replaces stub bodies inside the files it owns, and never changes the frozen signatures.

**Tech Stack:** Freestanding C (gcc `-ffreestanding -mno-sse` for kernel/gui, FP flags for `apps/`), NASM, GNU make in WSL Ubuntu, host unit tests (`make host-test`, `include/pure.h`), QEMU (`tools/qa/boot-check.sh`).

**Spec:** `docs/superpowers/specs/2026-10-03-careos-v10-redesign-roadmap.md` (Step W0), plus the frozen-interface requirements in `…-hardware-platform-design.md`, `…-visual-system-shell-design.md`, `…-start-screens-design.md` and `…-app-restyle-design.md`.

## Global Constraints

- **Build:** `wsl.exe -d Ubuntu -- bash -lc "cd /mnt/d/Users/mhetm/Downloads/CareOS_v9_full/CareOS_v9 && make"` (after Task 1, use `tools/wt/locked-make.sh` instead). The Windows Bash tool has no toolchain.
- **No scratch files on C:** (it's full). QA output goes under `D:\Users\mhetm\Downloads\CareOS_v9_full\qa\` or the worktree's `qa-out/`.
- **No visual or behaviour change** in W0. Boot, login, desktop, Control Center and notifications look and act as before.
- Kernel/gui code: no `float`/`double`, no SIMD. No allocation in per-frame paths.
- Every commit ends with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Commit per task on branch `redesign/v10`. Never commit `careos.iso`, `careos.img`, `*.o`, `*.d`, `build/`.
- Frozen signatures in this plan are **final**. Later phases may add functions to their own headers, but must not change these.

## Review Focus

1. **Moved code loses a `static` dependency.** For example, `draw_glass_panel` was shared by the top bar and the login screen, and `ui_clampi` by Control Center and login. Expected: each new file still compiles and behaves the same. *Pinned by* Task 2 Step 6 (build with `-Werror=implicit-function-declaration`) and Step 8 (boot-check + login + Control Center screenshots).
2. **An old settings blob (v5) or user DB (v6) on an existing disk image.** Expected: boot keeps every existing preference, and existing users are **not** sent to onboarding. *Pinned by* Task 3 host tests `test_settings_v5_to_v6` and `test_userdb_v6_to_v7`, plus Task 7 Step 3 (boot on the pre-W0 `careos.img`).
3. **A disk with more than 2^32 sectors, or a request crossing a 255-sector chunk.** Expected: blockdev splits requests and never truncates the LBA to 32 bits. *Pinned by* Task 6 `test_blockdev_chunking`.
4. **`wp_parallel_for(0, …)`, and `n` smaller than the core count.** Expected: no call for n=0, each index exactly once otherwise. *Pinned by* Task 6 `test_wp_parallel_for_counts`.
5. **Two worktrees building at once** while WSL is fragile. Expected: at most 3 `make` runs at once; the others wait instead of failing. *Pinned by* Task 1 Step 4.

---

### Task 1: Worktree bootstrap and a build-slot lock

The repository embeds `doomgeneric/` as a bare gitlink (no `.gitmodules`), and `DOOM1.WAD` is untracked. A plain `git worktree add` therefore produces a tree that can't build.

**Files:**
- Create: `tools/wt/new-worktree.sh`, `tools/wt/locked-make.sh`, `tools/wt/README.md`

**Interfaces:**
- Produces: `bash tools/wt/new-worktree.sh <agent>` creates `D:\Users\mhetm\Downloads\CareOS_v9_full\wt\<agent>` on branch `redesign/v10-<agent>`, ready to build. `bash tools/wt/locked-make.sh [make args]` runs `make` while holding one of 3 build slots.

- [ ] **Step 1: Write `tools/wt/new-worktree.sh`**

```bash
#!/usr/bin/env bash
# Create an isolated, buildable worktree for one v10 agent.
# Usage (WSL, from the main checkout): bash tools/wt/new-worktree.sh <agent>
set -euo pipefail
AGENT=${1:?agent name, e.g. hw-a}
MAIN=$(git rev-parse --show-toplevel)
WT_ROOT=$(cd "$MAIN/.." && pwd)/wt
WT="$WT_ROOT/$AGENT"
BR="redesign/v10-$AGENT"
mkdir -p "$WT_ROOT"
if [[ -d "$WT" ]]; then echo "exists: $WT"; exit 0; fi
git -C "$MAIN" worktree add -b "$BR" "$WT" redesign/v10
# Untracked / non-submodule build inputs are shared read-only from the main checkout.
rmdir "$WT/doomgeneric" 2>/dev/null || true
ln -s "$MAIN/doomgeneric" "$WT/doomgeneric"
[[ -f "$MAIN/DOOM1.WAD" ]] && ln -s "$MAIN/DOOM1.WAD" "$WT/DOOM1.WAD"
mkdir -p "$WT/qa-out"
echo "ready: $WT on $BR"
```

- [ ] **Step 2: Write `tools/wt/locked-make.sh`**

```bash
#!/usr/bin/env bash
# Run make while holding one of N global build slots (WSL is fragile under
# heavy parallel writes). Usage: bash tools/wt/locked-make.sh [make args...]
set -uo pipefail
SLOTS=${CAREOS_BUILD_SLOTS:-3}
LOCKDIR=/mnt/d/Users/mhetm/Downloads/CareOS_v9_full/wt
mkdir -p "$LOCKDIR"
while :; do
  for i in $(seq 1 "$SLOTS"); do
    exec {fd}>"$LOCKDIR/.build-lock-$i"
    if flock -n "$fd"; then
      make -j2 "$@"; rc=$?
      flock -u "$fd"; exit $rc
    fi
    exec {fd}>&-
  done
  sleep 3
done
```

- [ ] **Step 3: Write `tools/wt/README.md`** explaining both scripts, the doomgeneric/DOOM1.WAD symlinks, per-agent `qa-out/`, and that each agent's QEMU runs must use `QA_OUT=$PWD/qa-out`.

- [ ] **Step 4: Verify**

Run (WSL, main checkout):
```bash
bash tools/wt/new-worktree.sh w0-probe
cd ../wt/w0-probe && (bash tools/wt/locked-make.sh >/tmp/p1.log 2>&1 &) ; (bash tools/wt/locked-make.sh -n >/dev/null && echo second-ok)
wait; tail -3 /tmp/p1.log; ls careos.iso
```
Expected:
- the probe worktree builds `careos.iso`
- the concurrent dry run takes a different slot and prints `second-ok`
Then clean up: `git worktree remove --force ../wt/w0-probe && git branch -D redesign/v10-w0-probe`.

- [ ] **Step 5: Commit**

```bash
git add tools/wt
git commit -m "tools(wt): buildable per-agent worktrees and a 3-slot build lock

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Split `gui/gui.c` into owned files (pure move)

**Files:**
- Create:
  - `gui/shell/shell.h`, `gui/shell/menubar.c`, `gui/shell/control_center.c`, `gui/shell/notify_center.c`
  - `gui/session/session.h`, `gui/session/boot_splash.c`, `gui/session/login.c`, `gui/session/lock.c`, `gui/session/power_menu.c`, `gui/session/onboarding.c`
  - `gui/gui_private.h`
- Modify: `gui/gui.c` (keeps `gui_init`, `draw_elite_wallpaper`, `enter_desktop`, `desktop_fade_in`, `gui_run`), `Makefile` (`C_SRC`)

**Interfaces:**
- Produces `gui/gui_private.h` (helpers formerly `static` in gui.c and used by more than one new file):
```c
#ifndef CAREOS_GUI_PRIVATE_H
#define CAREOS_GUI_PRIVATE_H
#include "gui.h"
i32  ui_clampi(i32 v, i32 lo, i32 hi);
void draw_glass_panel(rect_t r, i32 radius);
void draw_elite_wallpaper(void);
#endif
```
- Produces `gui/shell/shell.h`:
```c
#ifndef CAREOS_SHELL_H
#define CAREOS_SHELL_H
#include "gui.h"
/* menubar.c */
void menubar_draw(const mouse_t *m);
bool menubar_handle_mouse(mouse_t *m);
/* control_center.c */
extern bool g_cc_open;            /* was s_control_center_open */
extern bool g_dnd_enabled;        /* was s_dnd_enabled */
extern int  g_brightness_pct;     /* was s_brightness, 10..100 */
void control_center_draw(mouse_t *m);
bool control_center_handle_mouse(mouse_t *m);
/* notify_center.c */
extern bool g_nc_open;            /* was s_notif_center_open */
void notify_center_add(const char *title, const char *body, u32 color);  /* was control_center_add_notif */
void notify_center_init_defaults(void);                                  /* was init_nc_default_items */
void notify_center_draw(mouse_t *m);
bool notify_center_handle_mouse(mouse_t *m);
#endif
```
- Produces `gui/session/session.h`:
```c
#ifndef CAREOS_SESSION_H
#define CAREOS_SESSION_H
#include "gui.h"
#define BOOT_STAGE_COUNT 6
void boot_splash_draw(int done, u32 now_ms);            /* was draw_boot_splash */
bool session_run_login(mouse_t *mouse, bool lock_mode); /* was run_login_flow */
void session_run_screensaver(mouse_t *mouse);           /* was run_matrix_screensaver */
/* Phase 2 entry points; W0 stubs keep today's behaviour. */
void session_lock(mouse_t *mouse);                      /* stub: session_run_login(mouse, true) */
void session_power_menu(mouse_t *mouse);                /* stub: returns immediately */
bool session_onboarding_needed(void);                   /* stub: false */
void session_run_onboarding(mouse_t *mouse);            /* stub: returns immediately */
/* Phase 4 fills this; until then it draws nothing. mood: 0 idle,1 nap,2 wave,3 happy. */
void session_pet_slot(rect_t r, u32 mood);
#endif
```

- [ ] **Step 1: Record the baseline.** Build, then run `QA_OUT=../qa/w0-before bash tools/qa/boot-check.sh 3`. Expected `3/3`. Take a desktop screenshot via `tools/qemu-shot.ps1` into `..\qa\w0-before\desktop.png` (see `memory: careos-qemu-qa-harness` for absolute screendump paths).

- [ ] **Step 2: Create `gui/gui_private.h`** (above). In `gui/gui.c`, remove `static` from `ui_clampi` (line 18) and `draw_glass_panel` (line 898, plus the forward declaration at line 12), and remove `static` from `draw_elite_wallpaper` (line 122). Include `gui_private.h`.

- [ ] **Step 3: Move blocks verbatim** (line numbers are from `gui/gui.c` at commit `d062801`; move whole functions with their preceding comments):

| Source lines (functions) | Destination | Renames |
|---|---|---|
| 54–121 `BOOT_STAGE_COUNT`, `draw_boot_splash` | `gui/session/boot_splash.c` | `draw_boot_splash` → `boot_splash_draw` (non-static) |
| 158–186 `draw_status_wifi/speaker/battery` | `gui/shell/menubar.c` (static) | — |
| 187–192 CC state vars | `gui/shell/control_center.c` | `s_control_center_open`→`g_cc_open`, `s_dnd_enabled`→`g_dnd_enabled`, `s_brightness`→`g_brightness_pct` (non-static); `s_wifi_enabled`, `s_bt_enabled`, `s_volume` stay static there |
| 195–263 `nc_item_t`, `s_nc_items`, `s_nc_count`, `s_notif_center_open`, `s_nc_inited`, `control_center_add_notif`, `init_nc_default_items` | `gui/shell/notify_center.c` | `s_notif_center_open`→`g_nc_open`, `control_center_add_notif`→`notify_center_add`, `init_nc_default_items`→`notify_center_init_defaults` |
| 264–406 `draw_top_bar`, `handle_top_bar_mouse` | `gui/shell/menubar.c` | → `menubar_draw`, `menubar_handle_mouse` |
| 407–623 `draw_control_center`, `handle_control_center_mouse` | `gui/shell/control_center.c` | → `control_center_draw`, `control_center_handle_mouse` |
| 624–754 `draw_notification_center`, `handle_notification_center_mouse` | `gui/shell/notify_center.c` | → `notify_center_draw`, `notify_center_handle_mouse` |
| 755–1582 login types, layout, greeter, `login_*`, `draw_login_screen`, `draw_greeter`, `login_fade_out`, `run_matrix_screensaver`, `run_login_flow` | `gui/session/login.c` | `run_login_flow`→`session_run_login`, `run_matrix_screensaver`→`session_run_screensaver` |

- Each new `.c` starts with `#include "kernel.h"`, `#include "gui.h"`, `#include "../gui_private.h"`, its own `shell.h`/`session.h`, plus whichever of `image.h`, `resource_cache.h`, `icon.h`, `perf.h`, `keychord.h` its moved code uses.
- Where moved code references another moved file's renamed symbol, use the new name.
- `desktop_fade_in` (1283) stays in `gui.c`, because it's used by `enter_desktop`. If `session_run_login` calls it, declare it in `gui_private.h` as `void desktop_fade_in(mouse_t *mouse);` and make it non-static.

- [ ] **Step 4: Create the phase 2 stub files.**

`gui/session/lock.c`:
```c
#include "kernel.h"
#include "gui.h"
#include "session.h"
/* Phase 2 (start-a) replaces this with the Lock screen artboard. */
void session_lock(mouse_t *mouse) { session_run_login(mouse, true); }
/* Phase 4 draws Pip here; phase 2 layouts reserve the rect. */
void session_pet_slot(rect_t r, u32 mood) { (void)r; (void)mood; }
```
`gui/session/power_menu.c`:
```c
#include "kernel.h"
#include "gui.h"
#include "session.h"
/* Phase 2 (start-a) replaces this with the Power menu artboard. */
void session_power_menu(mouse_t *mouse) { (void)mouse; }
```
`gui/session/onboarding.c`:
```c
#include "kernel.h"
#include "gui.h"
#include "session.h"
/* Phase 2 (start-b) replaces these with the 4-step onboarding. */
bool session_onboarding_needed(void) { return false; }
void session_run_onboarding(mouse_t *mouse) { (void)mouse; }
```

- [ ] **Step 5: Update `gui/gui.c` call sites.**
  - `draw_boot_splash(`→`boot_splash_draw(`
  - `run_login_flow(&mouse, true)`→`session_lock(&mouse)` (the three lock call sites at 1650, 1739, 1753)
  - `run_login_flow(&mouse, false)`→`session_run_login(&mouse, false)`
  - `draw_top_bar`→`menubar_draw`, `handle_top_bar_mouse`→`menubar_handle_mouse`
  - `draw_control_center`→`control_center_draw`, `handle_control_center_mouse`→`control_center_handle_mouse`
  - `draw_notification_center`→`notify_center_draw`, `handle_notification_center_mouse`→`notify_center_handle_mouse`
  - `s_control_center_open`→`g_cc_open`, `s_notif_center_open`→`g_nc_open`, `s_brightness`→`g_brightness_pct`
  - Include `shell/shell.h` and `session/session.h`.
  - Right after login succeeds (before `enter_desktop`), add `if (session_onboarding_needed()) session_run_onboarding(&mouse);`. It's a no-op until phase 2.

- [ ] **Step 6: Makefile and a strict compile check.** Add to `C_SRC` after `gui/gui.c \`:
```make
             gui/shell/menubar.c   \
             gui/shell/control_center.c \
             gui/shell/notify_center.c \
             gui/session/boot_splash.c \
             gui/session/login.c   \
             gui/session/lock.c    \
             gui/session/power_menu.c \
             gui/session/onboarding.c \
```
Run: `bash tools/wt/locked-make.sh CFLAGS_EXTRA=-Werror=implicit-function-declaration 2>&1 | grep -E "error|warning: implicit" ; echo rc=$?`. If the Makefile has no `CFLAGS_EXTRA` hook, add `KERN_CFLAGS += $(CFLAGS_EXTRA)` once, after its definition.
Expected: no errors, no implicit declarations. `wc -l gui/gui.c` is under 450.

- [ ] **Step 7: Host tests still pass.** Run `make host-test`. Expected: every `HOST` line runs, exit 0.

- [ ] **Step 8: Behaviour unchanged.**
  - Run `QA_OUT=../qa/w0-after bash tools/qa/boot-check.sh 3`. Expected `3/3`.
  - Take screenshots of the login screen, desktop and opened Control Center.
  - Compare them with Step 1 by eye: identical layout and colours.

- [ ] **Step 9: Commit**

```bash
git add gui/gui.c gui/gui_private.h gui/shell gui/session Makefile
git commit -m "refactor(gui): split gui.c into shell/ and session/ files with phase-owned stubs (no behaviour change)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Settings v6 and user DB v7 fields (shared persistence freeze)

**Files:**
- Create: `kernel/persist_migrate.c`, `kernel/persist_migrate.h`, `tests/host/test_persist_migrate.c`
- Modify: `include/kernel.h` (`careos_settings_t`, `user_t`, new prototypes), `kernel/settings.c`, `kernel/users.c`, `Makefile` (`C_SRC`, `HOST_TESTS`, `HT_SRC_persist_migrate`)

**Interfaces:**
- Produces, in `include/kernel.h`:
```c
/* careos_settings_t gains (append after `accent`): */
    u32  theme_mode;           /* 0 dark, 1 light, 2 auto (07:00 light, 19:00 dark) */
    u32  glass_alpha_pct;      /* 40..100, default 100 = token alphas as designed */
    u32  reduce_transparency;  /* 0/1 */
    u32  dock_position;        /* 0 bottom, 1 left, 2 right */
    u32  dock_autohide;        /* 0/1 */
    u32  brightness_pct;       /* 10..100, default 100 */
    u32  dnd;                  /* 0/1 */
void settings_set_theme_mode(u32 mode);
void settings_set_glass(u32 alpha_pct, bool reduce);
void settings_set_dock(u32 position, bool autohide);
void settings_set_brightness(u32 pct);
void settings_set_dnd(bool on);

/* user_t gains (append before hash_algo): */
    char display_name[32];
    u8   avatar_color;        /* 0..5 index into the avatar gradient table */
    char password_hint[64];
    bool onboarded;
    u8   onboard_step;        /* 0..3, resume point */

typedef struct {
    char display_name[32];
    u8   avatar_color;
    char password_hint[64];
    bool onboarded;
    u8   onboard_step;
} user_profile_t;
bool user_profile_get(u32 uid, user_profile_t *out);         /* false if no such user */
int  user_profile_set_current(const user_profile_t *p);      /* 0 ok, <0 no session/IO */
```
- Produces, in `kernel/persist_migrate.h` (pure, host-tested):
```c
#include "pure.h"
typedef struct { u32 theme, glass_alpha_pct, reduce_transparency, dock_position,
                 dock_autohide, brightness_pct, dnd, theme_mode; } settings_v6_ext_t;
void settings_v6_ext_defaults(u32 v5_theme, settings_v6_ext_t *out);
typedef struct { char name[32]; u32 uid; } userdb_v6_identity_t;
typedef struct { char display_name[32]; u8 avatar_color; char password_hint[64];
                 u8 onboarded; u8 onboard_step; } userdb_v7_ext_t;
void userdb_v7_ext_from_v6(const userdb_v6_identity_t *in, userdb_v7_ext_t *out);
```

- [ ] **Step 1: Write the failing host test** `tests/host/test_persist_migrate.c`:
```c
#include "pure.h"
#include "persist_migrate.h"
#include <assert.h>

static void test_settings_v5_to_v6(void) {
    settings_v6_ext_t e;
    settings_v6_ext_defaults(1, &e);            /* v5 user had Light */
    assert(e.theme_mode == 1);
    assert(e.glass_alpha_pct == 100 && e.reduce_transparency == 0);
    assert(e.dock_position == 0 && e.dock_autohide == 0);
    assert(e.brightness_pct == 100 && e.dnd == 0);
    settings_v6_ext_defaults(7, &e);            /* garbage theme -> dark */
    assert(e.theme_mode == 0);
}

static void test_userdb_v6_to_v7(void) {
    userdb_v6_identity_t in = { "fishfish", 1000 };
    userdb_v7_ext_t out;
    userdb_v7_ext_from_v6(&in, &out);
    assert(strcmp(out.display_name, "fishfish") == 0);
    assert(out.avatar_color == 1000 % 6);
    assert(out.password_hint[0] == 0);
    assert(out.onboarded == 1);                 /* existing users skip onboarding */
    assert(out.onboard_step == 0);
    userdb_v6_identity_t longname = { "abcdefghijklmnopqrstuvwxyz0123456789", 7 };
    userdb_v7_ext_from_v6(&longname, &out);
    assert(strlen(out.display_name) == 31);     /* always terminated */
}

int main(void) { test_settings_v5_to_v6(); test_userdb_v6_to_v7(); puts("persist_migrate ok"); return 0; }
```
Add to the Makefile: `HT_SRC_persist_migrate := kernel/persist_migrate.c` and append `persist_migrate` to `HOST_TESTS`.

- [ ] **Step 2: Run it.** Run `make host-test`. Expected: FAIL (`persist_migrate.h: No such file`).

- [ ] **Step 3: Implement `kernel/persist_migrate.c`:**
```c
#include "persist_migrate.h"

void settings_v6_ext_defaults(u32 v5_theme, settings_v6_ext_t *out) {
    out->theme = (v5_theme == 1) ? 1u : 0u;
    out->theme_mode = out->theme;
    out->glass_alpha_pct = 100;
    out->reduce_transparency = 0;
    out->dock_position = 0;
    out->dock_autohide = 0;
    out->brightness_pct = 100;
    out->dnd = 0;
}

void userdb_v7_ext_from_v6(const userdb_v6_identity_t *in, userdb_v7_ext_t *out) {
    kmemset(out, 0, sizeof(*out));
    kstrncpy(out->display_name, in->name, sizeof(out->display_name) - 1);
    out->display_name[sizeof(out->display_name) - 1] = 0;
    out->avatar_color = (u8)(in->uid % 6u);
    out->onboarded = 1;
    out->onboard_step = 0;
}
```
Run `make host-test`. Expected: `persist_migrate ok`.

- [ ] **Step 4: Settings blob v6.** In `kernel/settings.c`:
  - Add `settings_blob_v6_t` = v5 fields + the 7 new `u32` fields, in the order of `careos_settings_t`.
  - Bump the written version to 6.
  - In load: when the version is 5 (or older, after the existing upgrade chain), call `settings_v6_ext_defaults(g_settings.theme, &e)` and copy the fields.
  - Implement the five setters: clamp the ranges (`glass_alpha_pct` 40–100, `brightness_pct` 10–100, `dock_position` ≤ 2, `theme_mode` ≤ 2), then persist like `settings_set_accent` does.
  - `settings_set_theme_mode(2)` keeps `theme` as the currently applied value; phase 1 adds the time-based switch.
  - Add `kernel/persist_migrate.c` to `C_SRC`.

- [ ] **Step 5: User DB v7.** In `kernel/users.c`:
  - Add `userdb_entry_v7_t` = `userdb_entry_v6_t` + `display_name[32]`, `avatar_color`, `password_hint[64]`, `onboarded`, `onboard_step`.
  - Set `USERDB_VERSION 7u`.
  - Load v7 directly. Load v6 through the existing v6 path, then `userdb_v7_ext_from_v6` per entry.
  - Save as v7. `max_entries` uses `sizeof(userdb_entry_v7_t)`; check that the userdb sector budget still fits `MAX_USERS`, and raise the sector count constant if not.
  - New accounts (`user_create`/`user_register`): `display_name = name`, `avatar_color = uid % 6`, `onboarded = false`, `onboard_step = 0`.
  - Implement `user_profile_get` and `user_profile_set_current`. The latter persists via `users_persist_save()`, sanitises `display_name` and `password_hint` (printable ASCII only, always terminated) and clamps `avatar_color < 6` and `onboard_step < 4`.

- [ ] **Step 6: Build and boot on the existing disk image.** Run `bash tools/wt/locked-make.sh && QA_OUT=../qa/w0-t3 bash tools/qa/boot-check.sh 2`. Expected: `2/2`. The serial log shows the settings and users loading with no "checksum"/"reset" message.

- [ ] **Step 7: Commit**

```bash
git add include/kernel.h kernel/settings.c kernel/users.c kernel/persist_migrate.[ch] tests/host/test_persist_migrate.c Makefile
git commit -m "feat(persist): settings v6 (theme mode, glass, dock, brightness, DND) and user DB v7 profile fields with migrations

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Theme tokens and font roles (frozen API, stub rendering)

**Files:**
- Create: `gui/font_roles.c`, `gui/font_roles.h`, `tests/host/test_font_roles.c`
- Modify: `gui/gui.h` (`theme_t` fields, font-role API), `gui/theme.c` (token values), `Makefile`

**Interfaces:**
- Produces, in `gui/gui.h` (append to `theme_t` before `is_dark`):
```c
    /* v10 design tokens (docs/superpowers/specs/2026-10-03-careos-visual-system-shell-design.md §1) */
    u32 ground, surface_alt, raised, raised2, track;
    u32 ink, ink2, care, on_care, care_soft, calm;
    u32 hairline;         u8 hairline_alpha;
    u32 menubar_tint;     u8 menubar_alpha;
    u32 dock_tint;        u8 dock_alpha;
    u32 panel_tint;       u8 panel_alpha;
    u32 window_body;      u8 window_alpha;
    u32 term_green;
```
- Produces, in `gui/gui.h` after the `font_size_t` enum:
```c
typedef enum { FONT_ROLE_UI = 0, FONT_ROLE_DISPLAY, FONT_ROLE_MONO, FONT_ROLE_COUNT } font_role_t;
typedef enum { FONT_W_REGULAR = 0, FONT_W_BOLD } font_weight_t;   /* Display: regular = Bold, bold = ExtraBold */
/* Size slots 0..4 per role (px):  UI 11 13 15 17 20 · DISPLAY 17 22 28 52 64 · MONO 11 13 14 16 20 */
void gfx_str_role(i32 x, i32 y, const char *s, u32 fg, font_role_t role, u32 slot, font_weight_t w);
i32  gfx_str_role_width(const char *s, font_role_t role, u32 slot, font_weight_t w);
i32  gfx_str_role_width_n(const char *s, u32 n, font_role_t role, u32 slot, font_weight_t w);
i32  gfx_role_line_h(font_role_t role, u32 slot);
/* Large clock digits ("0-9 : AMP"), Display Bold 120 px; stub draws H1. */
void gfx_str_huge(i32 x, i32 y, const char *s, u32 fg);
i32  gfx_str_huge_width(const char *s);
```
- Produces `gui/font_roles.h` (pure):
```c
#include "pure.h"
u32 font_role_px(u32 role, u32 slot);              /* design px for a role/slot; slot clamped to 4 */
u32 font_role_fallback_size(u32 role, u32 slot);   /* stub mapping to today's font_size_t */
```

- [ ] **Step 1: Write the failing test** `tests/host/test_font_roles.c`:
```c
#include "pure.h"
#include "font_roles.h"
#include <assert.h>
int main(void) {
    assert(font_role_px(0, 0) == 11 && font_role_px(0, 4) == 20);
    assert(font_role_px(1, 3) == 52 && font_role_px(1, 4) == 64);
    assert(font_role_px(2, 2) == 14);
    assert(font_role_px(1, 99) == 64);                 /* slot clamps */
    assert(font_role_px(9, 0) == font_role_px(0, 0));  /* unknown role -> UI */
    /* fallback maps onto today's 5 sizes: CAPTION=0 .. H1=4 */
    assert(font_role_fallback_size(0, 1) == 1);        /* UI 13 -> FONT_BODY */
    assert(font_role_fallback_size(1, 4) == 4);        /* Display 64 -> FONT_H1 */
    assert(font_role_fallback_size(1, 0) == 2);        /* Display 17 -> FONT_H3 */
    puts("font_roles ok"); return 0;
}
```
Makefile: `HT_SRC_font_roles := gui/font_roles.c`; append `font_roles` to `HOST_TESTS`. Run `make host-test`. Expected: FAIL (missing header).

- [ ] **Step 2: Implement `gui/font_roles.c`:**
```c
#include "font_roles.h"
static const u8 PX[3][5] = { {11,13,15,17,20}, {17,22,28,52,64}, {11,13,14,16,20} };
/* nearest of today's sizes 11,13,16,20,28 -> FONT_CAPTION..FONT_H1 */
static u32 nearest(u32 px) {
    static const u8 S[5] = {11,13,16,20,28};
    u32 best = 0, bd = 0xFFFFFFFFu;
    for (u32 i = 0; i < 5; i++) { u32 d = px > S[i] ? px - S[i] : S[i] - px; if (d < bd) { bd = d; best = i; } }
    return best;
}
u32 font_role_px(u32 role, u32 slot) { if (role > 2) role = 0; if (slot > 4) slot = 4; return PX[role][slot]; }
u32 font_role_fallback_size(u32 role, u32 slot) { return nearest(font_role_px(role, slot)); }
```
Run `make host-test`. Expected: `font_roles ok`.

- [ ] **Step 3: Stub renderers** in `gui/font_roles.c`, under `#ifndef HOST_TEST`. They map to today's renderer; phase 1 (vis-a) replaces the bodies with real Figtree/Bricolage tables.
```c
#ifndef HOST_TEST
#include "gui.h"
void gfx_str_role(i32 x, i32 y, const char *s, u32 fg, font_role_t role, u32 slot, font_weight_t w) {
    font_size_t fs = (font_size_t)font_role_fallback_size((u32)role, slot);
    if (w == FONT_W_BOLD || role == FONT_ROLE_DISPLAY) {
        if (fs == FONT_BODY) { gfx_str_bold(x, y, s, fg, COL_TRANSPARENT); return; }
    }
    gfx_str_ex(x, y, s, fg, COL_TRANSPARENT, fs);
}
i32 gfx_str_role_width(const char *s, font_role_t role, u32 slot, font_weight_t w) {
    (void)w; return gfx_str_width_ex(s, (font_size_t)font_role_fallback_size((u32)role, slot));
}
i32 gfx_str_role_width_n(const char *s, u32 n, font_role_t role, u32 slot, font_weight_t w) {
    (void)w; return gfx_str_width_n(s, n, (font_size_t)font_role_fallback_size((u32)role, slot));
}
i32 gfx_role_line_h(font_role_t role, u32 slot) {
    return gfx_line_h_ex((font_size_t)font_role_fallback_size((u32)role, slot));
}
void gfx_str_huge(i32 x, i32 y, const char *s, u32 fg) { gfx_str_ex(x, y, s, fg, COL_TRANSPARENT, FONT_H1); }
i32  gfx_str_huge_width(const char *s) { return gfx_str_width_ex(s, FONT_H1); }
#endif
```
Add `gui/font_roles.c` to `C_SRC`. If `COL_TRANSPARENT` is not visible in `gui.h`, use the constant `gfx.c` uses for "no background" (grep `COL_TRANSPARENT`).

- [ ] **Step 4: Token values** in `gui/theme.c`. Add to `theme_dark`:
```c
    .ground = rgb(0x0B,0x10,0x20), .surface_alt = rgb(0x16,0x1C,0x33),
    .raised = rgb(0x1C,0x24,0x40), .raised2 = rgb(0x22,0x2A,0x48), .track = rgb(0x2E,0x37,0x57),
    .ink = rgb(0xEE,0xF1,0xFA), .ink2 = rgb(0xB4,0xBB,0xD2),
    .care = rgb(0xFF,0x7A,0x59), .on_care = rgb(0x2A,0x12,0x0A), .care_soft = rgb(0xFF,0x9A,0x76),
    .calm = rgb(0x8C,0x9B,0xFF),
    .hairline = rgb(0xFF,0xFF,0xFF), .hairline_alpha = 26,
    .menubar_tint = rgb(0x0A,0x0E,0x1E), .menubar_alpha = 158,
    .dock_tint = rgb(0x12,0x17,0x2C), .dock_alpha = 168,
    .panel_tint = rgb(0x15,0x1B,0x2E), .panel_alpha = 184,
    .window_body = rgb(0x0D,0x11,0x20), .window_alpha = 235,
    .term_green = rgb(0x7C,0xF0,0xB5),
```
and to `theme_light`:
```c
    .ground = rgb(0xE9,0xEC,0xF7), .surface_alt = rgb(0xF5,0xF6,0xFA),
    .raised = rgb(0xE1,0xE4,0xEE), .raised2 = rgb(0xE1,0xE4,0xEE), .track = rgb(0xD5,0xDA,0xE8),
    .ink = rgb(0x16,0x1A,0x2B), .ink2 = rgb(0x4A,0x51,0x68),
    .care = rgb(0xFF,0x7A,0x59), .on_care = rgb(0x2A,0x12,0x0A), .care_soft = rgb(0xE5,0x53,0x2E),
    .calm = rgb(0x4F,0x5E,0xF2),
    .hairline = rgb(0x16,0x1A,0x2B), .hairline_alpha = 22,
    .menubar_tint = rgb(0xFA,0xFA,0xFD), .menubar_alpha = 199,
    .dock_tint = rgb(0xFF,0xFF,0xFF), .dock_alpha = 184,
    .panel_tint = rgb(0xFA,0xFA,0xFD), .panel_alpha = 199,
    .window_body = rgb(0xFF,0xFF,0xFF), .window_alpha = 255,
    .term_green = rgb(0x1F,0xA5,0x62),
```
Existing fields and `COL_*` macros are untouched (phase 1 remaps them).

- [ ] **Step 5: Build, then commit.** Run `bash tools/wt/locked-make.sh && make host-test`. Expected: success.

```bash
git add gui/gui.h gui/theme.c gui/font_roles.[ch] tests/host/test_font_roles.c Makefile
git commit -m "feat(ui): freeze v10 theme tokens and font-role API (stub rendering)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: UI kit interface, window toolbar/menu hooks, `notify_push_ex`

**Files:**
- Create: `gui/ui.h`, `gui/ui.c`, `gui/ui_icons.c`, `gui/ui_icons.h`, `tests/host/test_ui_icons.c`
- Modify: `gui/gui.h` (`window_t` hooks, `app_menu_t`, `notify_push_ex`), `gui/gfx.c` (`notify_push_ex` stub), `Makefile`

**Interfaces:**
- Produces `gui/ui.h`:
```c
#ifndef CAREOS_UI_H
#define CAREOS_UI_H
#include "gui.h"

typedef enum { UI_NORMAL = 0, UI_HOVER, UI_PRESSED, UI_FOCUSED, UI_DISABLED } ui_state_t;
typedef enum { UI_BTN_PRIMARY = 0, UI_BTN_SECONDARY, UI_BTN_GHOST, UI_BTN_PILL_LIGHT, UI_BTN_DANGER } ui_button_kind_t;

/* Radii (px) */
#define UI_R_SMALL     10
#define UI_R_BUTTON    14
#define UI_R_INPUT     18
#define UI_R_CARD      20
#define UI_R_WINDOW    22
#define UI_R_PANEL     24
#define UI_R_CC        28
#define UI_TOGGLE_W    46
#define UI_TOGGLE_H    28
#define UI_HIT_MIN     44

/* ui_glass flags */
#define UI_GLASS_HAIRLINE  0x01u
#define UI_GLASS_SHADOW_S  0x02u
#define UI_GLASS_SHADOW_M  0x04u
#define UI_GLASS_SHADOW_L  0x08u
#define UI_GLASS_MOVING    0x10u   /* being dragged: tint only this frame */

typedef enum {
    UI_GLYPH_WIFI = 0, UI_GLYPH_WIRED, UI_GLYPH_BLUETOOTH, UI_GLYPH_SOUND, UI_GLYPH_MUTE,
    UI_GLYPH_BATTERY, UI_GLYPH_SEARCH, UI_GLYPH_BELL, UI_GLYPH_MOON, UI_GLYPH_SUN,
    UI_GLYPH_LOCK, UI_GLYPH_POWER, UI_GLYPH_RESTART, UI_GLYPH_USER, UI_GLYPH_CLOCK,
    UI_GLYPH_PLUS, UI_GLYPH_SLIDERS, UI_GLYPH_GRID, UI_GLYPH_CHEVRON_L, UI_GLYPH_CHEVRON_R,
    UI_GLYPH_CHEVRON_D, UI_GLYPH_CLOSE, UI_GLYPH_CHECK, UI_GLYPH_ARROW_R, UI_GLYPH_EYE,
    UI_GLYPH_CAMERA, UI_GLYPH_HOME, UI_GLYPH_FOLDER, UI_GLYPH_TRASH, UI_GLYPH_HEART,
    UI_GLYPH_COUNT
} ui_glyph_t;
#define UI_GLYPH_NONE UI_GLYPH_COUNT

typedef enum {
    UI_ICON_CARE = 0, UI_ICON_TERMINAL, UI_ICON_FILES, UI_ICON_BROWSER, UI_ICON_NOTES,
    UI_ICON_EDITOR, UI_ICON_PAINT, UI_ICON_MONITOR, UI_ICON_CLOCK, UI_ICON_CALC,
    UI_ICON_NETMON, UI_ICON_STORE, UI_ICON_SETTINGS, UI_ICON_USERS, UI_ICON_ABOUT,
    UI_ICON_GAMES, UI_ICON_PHOTOS, UI_ICON_MUSIC, UI_ICON_MAIL, UI_ICON_CALENDAR,
    UI_ICON_PET, UI_ICON_LAUNCHER, UI_ICON_TRASH, UI_ICON_COUNT
} ui_icon_t;

/* Frame + damage (compositor). gui_run calls frame_begin/end; anything that
 * changes pixels under glass calls ui_damage. */
void ui_frame_begin(void);
void ui_frame_end(u32 frame_ms);
void ui_damage(rect_t r);
void ui_damage_all(void);

/* Surfaces */
void ui_glass(u32 id, rect_t r, i32 radius, u32 tint, u8 alpha, i32 blur, u32 flags);
void ui_glass_forget(u32 id);
void ui_panel(rect_t r, i32 radius, u32 fill, u32 flags);

/* Controls */
ui_state_t ui_state_for(rect_t r, const mouse_t *m, bool pressed, bool disabled);
void ui_button(rect_t r, const char *label, ui_button_kind_t kind, ui_state_t st);
void ui_icon_button(rect_t r, ui_glyph_t g, ui_state_t st, bool active);
void ui_toggle(i32 x, i32 y, bool on, ui_state_t st);
void ui_slider(rect_t r, u32 value_pct, ui_state_t st);
void ui_chip(rect_t r, const char *label, ui_icon_t icon /* UI_ICON_COUNT = none */, bool selected, ui_state_t st);
void ui_input(rect_t r, const textinput_t *t, const char *placeholder, ui_glyph_t icon, bool focused);
void ui_progress(rect_t r, u32 pct, u32 color);
i32  ui_kbd(i32 x, i32 y, const char *label);          /* returns drawn width */
void ui_traffic_lights(i32 x, i32 y, bool hover, bool focused);
#define UI_TRAFFIC_W  58                                /* 3 x 14 + 2 x 8 */
i32  ui_traffic_hit(i32 x, i32 y, i32 mx, i32 my);      /* -1 none, 0 close, 1 min, 2 max */

/* Glyphs and icons */
void ui_glyph(ui_glyph_t g, i32 x, i32 y, i32 size, u32 color);
void ui_app_icon(ui_icon_t icon, i32 x, i32 y, i32 size);
ui_icon_t ui_icon_for_app(app_id_t app);
#endif
```
- Produces, in `gui/gui.h`, before `typedef struct window`:
```c
struct window;
typedef struct { const char *label; void (*action)(struct window *w); const char *shortcut; } app_menu_item_t;
typedef struct { const char *title; const app_menu_item_t *items; u32 count; } app_menu_t;
```
and appended to `window_t`:
```c
    /* v10 hooks (all optional; NULL = default behaviour) */
    void (*toolbar_draw)(struct window *w, rect_t bar);         /* draw in the 48px title bar after the traffic lights + title */
    bool (*toolbar_click)(struct window *w, i32 x, i32 y, mouse_t *m); /* true = consumed */
    const app_menu_t *menus; u32 menu_count;                     /* menu-bar menus for this app */
    void *app_state;                                             /* per-app heap state; freed by wm_close via kfree */
```
and after `notify_push`:
```c
/* actions may be NULL; on_action(which) gets 0 or 1 when a button is pressed. */
void notify_push_ex(app_id_t app, const char *title, const char *body,
                    const char *action0, const char *action1, void (*on_action)(u32 which));
```
- Produces `gui/ui_icons.h` (pure): `u32 ui_icon_key_for_app(u32 app_id);`, returning a `ui_icon_t` value (as u32).

- [ ] **Step 1: Write the failing test** `tests/host/test_ui_icons.c`:
```c
#include "pure.h"
#include "ui_icons.h"
#include <assert.h>
/* app ids from gui.h app_id_t order */
enum { NONE, TERMINAL, NOTES, FILES, SYSMON, CALC, ABOUT, HELP, BROWSER, SETTINGS, PKGMGR,
       EDITOR, PAINT, CLOCK, NETMON, USERS, MAZE, D3, DOOM };
enum { I_CARE, I_TERMINAL, I_FILES, I_BROWSER, I_NOTES, I_EDITOR, I_PAINT, I_MONITOR, I_CLOCK,
       I_CALC, I_NETMON, I_STORE, I_SETTINGS, I_USERS, I_ABOUT, I_GAMES };
int main(void) {
    assert(ui_icon_key_for_app(TERMINAL) == I_TERMINAL);
    assert(ui_icon_key_for_app(SYSMON) == I_MONITOR);
    assert(ui_icon_key_for_app(PKGMGR) == I_STORE);
    assert(ui_icon_key_for_app(HELP) == I_ABOUT);
    assert(ui_icon_key_for_app(MAZE) == I_GAMES && ui_icon_key_for_app(D3) == I_GAMES && ui_icon_key_for_app(DOOM) == I_GAMES);
    assert(ui_icon_key_for_app(NONE) == I_CARE);
    assert(ui_icon_key_for_app(999) == I_CARE);
    puts("ui_icons ok"); return 0;
}
```
Makefile: `HT_SRC_ui_icons := gui/ui_icons.c`; append `ui_icons`. Run `make host-test`. Expected: FAIL.

- [ ] **Step 2: Implement `gui/ui_icons.c`:**
```c
#include "ui_icons.h"
/* index = app_id_t; value = ui_icon_t (gui/ui.h order) */
static const u8 MAP[] = {
    0 /*NONE->CARE*/, 1 /*TERMINAL*/, 4 /*NOTES*/, 2 /*FILES*/, 7 /*SYSMON->MONITOR*/,
    9 /*CALC*/, 14 /*ABOUT*/, 14 /*HELP->ABOUT*/, 3 /*BROWSER*/, 12 /*SETTINGS*/,
    11 /*PKGMGR->STORE*/, 5 /*EDITOR*/, 6 /*PAINT*/, 8 /*CLOCK*/, 10 /*NETMON*/,
    13 /*USERS*/, 15 /*MAZE->GAMES*/, 15 /*3D->GAMES*/, 15 /*DOOM->GAMES*/
};
u32 ui_icon_key_for_app(u32 app_id) { return app_id < sizeof(MAP) ? MAP[app_id] : 0u; }
```
Run `make host-test`. Expected: `ui_icons ok`.

- [ ] **Step 3: Implement the `gui/ui.c` stubs** with today's primitives. Stubs must draw something recognisable, because phases 2 and 3 code against them before phase 1 lands. Complete file:
```c
#include "kernel.h"
#include "gui.h"
#include "ui.h"
#include "ui_icons.h"

void ui_frame_begin(void) {}
void ui_frame_end(u32 frame_ms) { (void)frame_ms; }
void ui_damage(rect_t r) { (void)r; }
void ui_damage_all(void) {}

/* Phase 1 (vis-a) replaces with the cached real-blur backdrop. */
void ui_glass(u32 id, rect_t r, i32 radius, u32 tint, u8 alpha, i32 blur, u32 flags) {
    (void)id; (void)blur;
    if (flags & (UI_GLASS_SHADOW_S | UI_GLASS_SHADOW_M | UI_GLASS_SHADOW_L))
        gfx_shadow_soft(r.x, r.y, r.w, r.h, radius);
    gfx_rect_rounded_blend(r.x, r.y, r.w, r.h, radius, tint, alpha);
    if (flags & UI_GLASS_HAIRLINE) gfx_rect_rounded_outline(r.x, r.y, r.w, r.h, radius, g_theme->hairline);
}
void ui_glass_forget(u32 id) { (void)id; }
void ui_panel(rect_t r, i32 radius, u32 fill, u32 flags) {
    if (flags & (UI_GLASS_SHADOW_S | UI_GLASS_SHADOW_M | UI_GLASS_SHADOW_L))
        gfx_shadow_soft(r.x, r.y, r.w, r.h, radius);
    gfx_rect_rounded(r.x, r.y, r.w, r.h, radius, fill);
    if (flags & UI_GLASS_HAIRLINE) gfx_rect_rounded_outline(r.x, r.y, r.w, r.h, radius, g_theme->hairline);
}

ui_state_t ui_state_for(rect_t r, const mouse_t *m, bool pressed, bool disabled) {
    if (disabled) return UI_DISABLED;
    if (!m || !rect_contains(r, m->x, m->y)) return UI_NORMAL;
    return pressed ? UI_PRESSED : UI_HOVER;
}

static void label_centered(rect_t r, const char *s, u32 fg, font_weight_t w) {
    i32 tw = gfx_str_role_width(s, FONT_ROLE_UI, 2, w);
    i32 th = gfx_role_line_h(FONT_ROLE_UI, 2);
    gfx_str_role(r.x + (r.w - tw) / 2, r.y + (r.h - th) / 2, s, fg, FONT_ROLE_UI, 2, w);
}

void ui_button(rect_t r, const char *label, ui_button_kind_t kind, ui_state_t st) {
    u32 bg = g_theme->raised2, fg = g_theme->ink;
    switch (kind) {
    case UI_BTN_PRIMARY:    bg = g_theme->care;  fg = g_theme->on_care; break;
    case UI_BTN_DANGER:     bg = g_theme->error; fg = rgb(0xFF,0xFF,0xFF); break;
    case UI_BTN_PILL_LIGHT: bg = g_theme->ink;   fg = g_theme->ground; break;
    case UI_BTN_GHOST:      bg = (st == UI_HOVER || st == UI_PRESSED) ? g_theme->raised : 0; fg = g_theme->ink2; break;
    default: break;
    }
    i32 rad = (kind == UI_BTN_PILL_LIGHT) ? r.h / 2 : UI_R_BUTTON;
    if (kind != UI_BTN_GHOST || bg) gfx_rect_rounded(r.x, r.y, r.w, r.h, rad, bg);
    if (kind == UI_BTN_SECONDARY) gfx_rect_rounded_outline(r.x, r.y, r.w, r.h, rad, g_theme->hairline);
    if (st == UI_HOVER)   gfx_rect_rounded_blend(r.x, r.y, r.w, r.h, rad, rgb(0xFF,0xFF,0xFF), 20);
    if (st == UI_PRESSED) gfx_rect_rounded_blend(r.x, r.y, r.w, r.h, rad, 0, 30);
    label_centered(r, label, st == UI_DISABLED ? g_theme->muted : fg, FONT_W_BOLD);
    if (st == UI_FOCUSED) gfx_rect_rounded_outline(r.x - 2, r.y - 2, r.w + 4, r.h + 4, rad + 2, g_theme->care);
}

void ui_icon_button(rect_t r, ui_glyph_t g, ui_state_t st, bool active) {
    if (active || st == UI_HOVER || st == UI_PRESSED)
        gfx_rect_rounded_blend(r.x, r.y, r.w, r.h, 8, g_theme->is_dark ? rgb(0xFF,0xFF,0xFF) : g_theme->ink, active ? 36 : 22);
    i32 s = r.h < 24 ? r.h - 4 : 17;
    ui_glyph(g, r.x + (r.w - s) / 2, r.y + (r.h - s) / 2, s, g_theme->ink);
}

void ui_toggle(i32 x, i32 y, bool on, ui_state_t st) {
    (void)st;
    gfx_rect_rounded(x, y, UI_TOGGLE_W, UI_TOGGLE_H, UI_TOGGLE_H / 2, on ? g_theme->care : g_theme->track);
    i32 kx = on ? x + UI_TOGGLE_W - 3 - 22 : x + 3;
    gfx_circle_fill(kx + 11, y + 14, 11, on ? rgb(0xFF,0xFF,0xFF) : g_theme->ink2);
}

void ui_slider(rect_t r, u32 value_pct, ui_state_t st) {
    (void)st;
    if (value_pct > 100) value_pct = 100;
    i32 ty = r.y + r.h / 2 - 3;
    gfx_rect_rounded(r.x, ty, r.w, 6, 3, g_theme->track);
    i32 fw = r.w * (i32)value_pct / 100;
    if (fw > 0) gfx_rect_rounded(r.x, ty, fw, 6, 3, g_theme->care);
    gfx_circle_fill(r.x + fw, r.y + r.h / 2, 9, rgb(0xFF,0xFF,0xFF));
}

void ui_chip(rect_t r, const char *label, ui_icon_t icon, bool selected, ui_state_t st) {
    u32 bg = selected ? g_theme->ink : g_theme->raised;
    u32 fg = selected ? g_theme->ground : g_theme->ink;
    gfx_rect_rounded(r.x, r.y, r.w, r.h, r.h / 2, bg);
    if (st == UI_HOVER && !selected) gfx_rect_rounded_blend(r.x, r.y, r.w, r.h, r.h / 2, rgb(0xFF,0xFF,0xFF), 20);
    i32 tx = r.x + 16;
    if (icon < UI_ICON_COUNT) { ui_app_icon(icon, r.x + 8, r.y + (r.h - 24) / 2, 24); tx = r.x + 40; }
    gfx_str_role(tx, r.y + (r.h - gfx_role_line_h(FONT_ROLE_UI, 1)) / 2, label, fg, FONT_ROLE_UI, 1, FONT_W_BOLD);
}

void ui_input(rect_t r, const textinput_t *t, const char *placeholder, ui_glyph_t icon, bool focused) {
    gfx_rect_rounded_blend(r.x, r.y, r.w, r.h, UI_R_INPUT, g_theme->panel_tint, g_theme->panel_alpha);
    gfx_rect_rounded_outline(r.x, r.y, r.w, r.h, UI_R_INPUT, focused ? g_theme->care : g_theme->hairline);
    i32 tx = r.x + 18;
    if (icon != UI_GLYPH_NONE) { ui_glyph(icon, r.x + 18, r.y + (r.h - 20) / 2, 20, g_theme->ink2); tx = r.x + 50; }
    i32 ty = r.y + (r.h - gfx_role_line_h(FONT_ROLE_UI, 3)) / 2;
    if (t && t->len) gfx_str_role(tx, ty, t->buf, g_theme->ink, FONT_ROLE_UI, 3, FONT_W_REGULAR);
    else if (placeholder) gfx_str_role(tx, ty, placeholder, g_theme->muted, FONT_ROLE_UI, 3, FONT_W_REGULAR);
}

void ui_progress(rect_t r, u32 pct, u32 color) {
    if (pct > 100) pct = 100;
    gfx_rect_rounded(r.x, r.y, r.w, r.h, r.h / 2, g_theme->track);
    i32 fw = r.w * (i32)pct / 100;
    if (fw > 0) gfx_rect_rounded(r.x, r.y, fw, r.h, r.h / 2, color);
}

i32 ui_kbd(i32 x, i32 y, const char *label) {
    i32 w = gfx_str_role_width(label, FONT_ROLE_MONO, 0, FONT_W_REGULAR) + 16;
    gfx_rect_rounded(x, y, w, 24, 8, g_theme->raised2);
    gfx_str_role(x + 8, y + 4, label, g_theme->ink2, FONT_ROLE_MONO, 0, FONT_W_REGULAR);
    return w;
}

void ui_traffic_lights(i32 x, i32 y, bool hover, bool focused) {
    (void)hover;
    u32 c0 = focused ? rgb(0xFF,0x5F,0x57) : g_theme->raised2;
    u32 c1 = focused ? rgb(0xF5,0xB8,0x3D) : g_theme->raised2;
    u32 c2 = focused ? rgb(0x3D,0xD6,0x8C) : g_theme->raised2;
    gfx_circle_fill(x + 7, y + 7, 7, c0);
    gfx_circle_fill(x + 29, y + 7, 7, c1);
    gfx_circle_fill(x + 51, y + 7, 7, c2);
}
i32 ui_traffic_hit(i32 x, i32 y, i32 mx, i32 my) {
    if (my < y - 4 || my > y + 18) return -1;
    for (i32 i = 0; i < 3; i++) { i32 cx = x + 7 + i * 22; if (mx >= cx - 10 && mx <= cx + 10) return i; }
    return -1;
}

/* Phase 1 (vis-a) replaces with pre-rasterized stroke glyphs. */
void ui_glyph(ui_glyph_t g, i32 x, i32 y, i32 size, u32 color) {
    (void)g; gfx_rect_rounded_outline(x + 2, y + 2, size - 4, size - 4, 3, color);
}
/* Phase 1 (vis-a) replaces with the design's gradient icons. */
void ui_app_icon(ui_icon_t icon, i32 x, i32 y, i32 size) {
    static const app_id_t BACK[UI_ICON_COUNT] = {
        APP_ABOUT, APP_TERMINAL, APP_FILES, APP_BROWSER, APP_NOTES, APP_EDITOR, APP_PAINT,
        APP_SYSMON, APP_CLOCK, APP_CALC, APP_NETMON, APP_PKGMGR, APP_SETTINGS, APP_USERS,
        APP_ABOUT, APP_MAZE, APP_PAINT, APP_ABOUT, APP_ABOUT, APP_CLOCK, APP_ABOUT,
        APP_NONE, APP_NONE };
    gfx_draw_icon(icon < UI_ICON_COUNT ? BACK[icon] : APP_ABOUT, x, y, size, g_theme->primary);
}
ui_icon_t ui_icon_for_app(app_id_t app) { return (ui_icon_t)ui_icon_key_for_app((u32)app); }
```
If `rect_contains` or `gfx_rect_rounded_outline` have different names, grep `gui/gui.h` and use the existing ones. Add `gui/ui.c` and `gui/ui_icons.c` to `C_SRC`.

- [ ] **Step 4: `notify_push_ex` stub** in `gui/gfx.c`, next to `notify_push`:
```c
/* Phase 1 (vis-b) gives this real app icons and action buttons. */
void notify_push_ex(app_id_t app, const char *title, const char *body,
                    const char *action0, const char *action1, void (*on_action)(u32 which)) {
    (void)app; (void)action0; (void)action1; (void)on_action;
    notify_push(title, body, COL_PRIMARY);
}
```

- [ ] **Step 5: Window hooks and app_state cleanup.** Add the `window_t` fields above. In `gui/wm.c`:
  - `wm_close`: `if (w->app_state) { kfree(w->app_state); w->app_state = NULL; }` before the slot is released
  - window creation: zero the new fields (`toolbar_draw = NULL`, `toolbar_click = NULL`, `menus = NULL`, `menu_count = 0`, `app_state = NULL`). Check whether the slot is already `kmemset` on open, and add the explicit zeroing only if it isn't.
  - The hooks aren't called yet; phase 1 (vis-b) wires them into the new chrome.

- [ ] **Step 6: Build and verify.** Run `bash tools/wt/locked-make.sh && make host-test && QA_OUT=../qa/w0-t5 bash tools/qa/boot-check.sh 2`. Expected: success, `2/2`. Nothing calls the kit yet, so visuals are unchanged.

- [ ] **Step 7: Commit**

```bash
git add gui/ui.[ch] gui/ui_icons.[ch] gui/gui.h gui/gfx.c gui/wm.c tests/host/test_ui_icons.c Makefile
git commit -m "feat(ui): freeze UI kit API with working stubs; window toolbar/menu hooks, app_state, notify_push_ex

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: Hardware interfaces — `hwinfo`, `workpool`, `blockdev`

**Files:**
- Create:
  - `kernel/hwinfo.h`, `kernel/hwinfo.c`
  - `kernel/workpool.h`, `kernel/workpool.c`
  - `drivers/storage/blockdev.h`, `drivers/storage/blockdev.c`, `drivers/storage/blockdev_pick.c`
  - `tests/host/test_workpool.c`, `tests/host/test_blockdev.c`
- Modify: `drivers/storage/ata.c` (register as a blockdev), `kernel/ext2.c`, `kernel/users.c`, `kernel/settings.c` (use blockdev), `kernel/kernel.c` (`hwinfo_init`, `wp_init`, `blockdev_select_primary`), `kernel/vfs.c` (`/proc/cpuinfo`, `/proc/meminfo` from hwinfo), `shell/shell.c` (`hwinfo` command), `Makefile`

**Interfaces:**
- Produces `kernel/hwinfo.h`:
```c
#ifndef CAREOS_HWINFO_H
#define CAREOS_HWINFO_H
#include "pure.h"
#define HW_MAX_DISKS 8
#define HW_MAX_NICS  4
#define HW_CPU_APIC    0x01u
#define HW_CPU_X2APIC  0x02u
#define HW_CPU_PDPE1GB 0x04u
#define HW_CPU_INVTSC  0x08u
#define HW_CPU_SSE2    0x10u
typedef struct { char vendor[13]; char brand[49]; u32 family, model, stepping;
                 u32 cores_present, cores_online, tsc_mhz, features, l2_kb, l3_kb; } hw_cpu_t;
typedef struct { char controller[16]; char model[41]; char serial[21];
                 u64 sectors; u32 sector_size; bool atapi; bool is_primary; } hw_disk_t;
typedef struct { char adapter[32]; u16 pci_vendor, pci_device; u32 vram_bytes;
                 u32 width, height, bpp, max_width, max_height; } hw_display_t;
typedef struct { char model[32]; u8 mac[6]; bool link_up; } hw_nic_t;
typedef struct {
    hw_cpu_t     cpu;
    u64          ram_usable_bytes, ram_max_phys;
    u32          disk_count;  hw_disk_t disks[HW_MAX_DISKS];
    hw_display_t display;
    u32          nic_count;   hw_nic_t  nics[HW_MAX_NICS];
    bool         has_battery; u32 battery_pct;
    bool         has_audio;
} hwinfo_t;
void            hwinfo_init(void);           /* CPUID + RAM; call after pmm_init */
const hwinfo_t *hwinfo_get(void);
hwinfo_t       *hwinfo_mut(void);            /* drivers fill their own section */
u32             hwinfo_format(char *buf, u32 cap);   /* multi-line text for `hwinfo` */
#endif
```
- Produces `kernel/workpool.h`:
```c
#ifndef CAREOS_WORKPOOL_H
#define CAREOS_WORKPOOL_H
#include "pure.h"
/* Jobs MUST NOT allocate, take kernel locks, touch VFS/net/scheduler, or block. */
typedef void (*wp_fn_t)(u32 index, void *ctx);
typedef u32 wp_job_t;
void     wp_init(void);                       /* W0: single core; phase 0 starts APs */
u32      wp_cores(void);                      /* >= 1 */
void     wp_parallel_for(u32 n, wp_fn_t fn, void *ctx);
wp_job_t wp_submit(void (*fn)(void *arg), void *arg);
void     wp_wait(wp_job_t job);
/* pure helper: index range [*lo,*hi) of chunk c of `chunks` over n items */
void     wp_split(u32 n, u32 chunks, u32 c, u32 *lo, u32 *hi);
#endif
```
- Produces `drivers/storage/blockdev.h`:
```c
#ifndef CAREOS_BLOCKDEV_H
#define CAREOS_BLOCKDEV_H
#include "pure.h"
#define BLOCKDEV_MAX 8
typedef struct blockdev {
    char name[16];      /* "ata0", "ahci0" */
    char model[41];
    u64  sectors;
    u32  sector_size;   /* 512 for disks */
    bool atapi;
    u32  max_xfer;      /* max sectors per read/write call */
    int (*read)(struct blockdev *d, u64 lba, u32 count, void *buf);
    int (*write)(struct blockdev *d, u64 lba, u32 count, const void *buf);
    void *priv;
} blockdev_t;
int         blockdev_register(blockdev_t *d);   /* index, or -1 if full */
u32         blockdev_count(void);
blockdev_t *blockdev_at(u32 i);
void        blockdev_select_primary(void);       /* after all drivers registered */
blockdev_t *blockdev_primary(void);              /* NULL if no disk */
bool        blockdev_present(void);
u64         blockdev_sectors(void);
int         blockdev_read(u64 lba, u32 count, void *buf);         /* primary; chunks by max_xfer */
int         blockdev_write(u64 lba, u32 count, const void *buf);
/* pure: choose primary. has_ext2[i] = superblock found. Returns index or -1. */
i32         blockdev_pick_primary(const u64 *sectors, const bool *atapi, const bool *has_ext2, u32 n);
/* pure: number of driver calls a request makes, and each call's (lba,count) */
u32         blockdev_chunks(u64 lba, u32 count, u32 max_xfer, u64 *out_lba, u32 *out_cnt, u32 max_out);
#endif
```

- [ ] **Step 1: Write the failing tests.**

`tests/host/test_workpool.c`:
```c
#include "pure.h"
#include "workpool.h"
#include <assert.h>
static u32 hits[1000];
static void bump(u32 i, void *ctx) { (void)ctx; hits[i]++; }
static void test_wp_parallel_for_counts(void) {
    wp_init();
    assert(wp_cores() >= 1);
    memset(hits, 0, sizeof hits);
    wp_parallel_for(0, bump, NULL);
    for (u32 i = 0; i < 1000; i++) assert(hits[i] == 0);
    wp_parallel_for(3, bump, NULL);
    assert(hits[0] == 1 && hits[1] == 1 && hits[2] == 1 && hits[3] == 0);
    memset(hits, 0, sizeof hits);
    wp_parallel_for(1000, bump, NULL);
    for (u32 i = 0; i < 1000; i++) assert(hits[i] == 1);
}
static void test_wp_split(void) {
    u32 lo, hi, total = 0, prev = 0;
    for (u32 c = 0; c < 4; c++) { wp_split(10, 4, c, &lo, &hi); assert(lo == prev); total += hi - lo; prev = hi; }
    assert(total == 10 && prev == 10);
    wp_split(2, 4, 3, &lo, &hi); assert(lo == hi);          /* more chunks than items */
}
static int ran;
static void job(void *a) { ran += *(int *)a; }
static void test_wp_submit(void) { int v = 5; ran = 0; wp_job_t j = wp_submit(job, &v); wp_wait(j); assert(ran == 5); }
int main(void) { test_wp_parallel_for_counts(); test_wp_split(); test_wp_submit(); puts("workpool ok"); return 0; }
```
`tests/host/test_blockdev.c`:
```c
#include "pure.h"
#include "blockdev.h"
#include <assert.h>
static void test_pick_primary(void) {
    u64 s[3] = { 2097152, 41943040, 0 }; bool at[3] = { false, false, true }; bool e2[3] = { false, true, false };
    assert(blockdev_pick_primary(s, at, e2, 3) == 1);                    /* ext2 wins */
    bool none[3] = { false, false, false };
    assert(blockdev_pick_primary(s, at, none, 3) == 0);                  /* first disk >= 64 MB */
    u64 tiny[1] = { 1000 }; bool f[1] = { false };
    assert(blockdev_pick_primary(tiny, f, f, 1) == -1);                  /* too small */
    bool atonly[1] = { true }; u64 big[1] = { 4000000 };
    assert(blockdev_pick_primary(big, atonly, f, 1) == -1);              /* ATAPI never primary */
    assert(blockdev_pick_primary(s, at, e2, 0) == -1);
}
static void test_blockdev_chunking(void) {
    u64 l[8]; u32 c[8];
    u32 n = blockdev_chunks(0x1FFFFFF00ull, 600, 255, l, c, 8);
    assert(n == 3);
    assert(l[0] == 0x1FFFFFF00ull && c[0] == 255);
    assert(l[1] == 0x1FFFFFF00ull + 255 && c[1] == 255);
    assert(l[2] == 0x1FFFFFF00ull + 510 && c[2] == 90);
    assert(blockdev_chunks(5, 0, 255, l, c, 8) == 0);
    assert(blockdev_chunks(5, 10, 0, l, c, 8) == 0);                   /* bad max_xfer */
}
int main(void) { test_pick_primary(); test_blockdev_chunking(); puts("blockdev ok"); return 0; }
```
Makefile: `HT_SRC_workpool := kernel/workpool.c`, `HT_SRC_blockdev := drivers/storage/blockdev_pick.c`; append `workpool blockdev`; add `-Idrivers/storage` to `HOST_CFLAGS`. Run `make host-test`. Expected: FAIL.

- [ ] **Step 2: Implement `kernel/workpool.c`** (single-core; phase 0 replaces the bodies, keeping `wp_split`):
```c
#include "workpool.h"
static u32 g_cores = 1;
void wp_init(void) { g_cores = 1; }
u32  wp_cores(void) { return g_cores; }
void wp_split(u32 n, u32 chunks, u32 c, u32 *lo, u32 *hi) {
    if (chunks == 0) chunks = 1;
    u32 base = n / chunks, rem = n % chunks;
    *lo = c * base + (c < rem ? c : rem);
    *hi = *lo + base + (c < rem ? 1u : 0u);
    if (c >= chunks) *lo = *hi = n;
}
void wp_parallel_for(u32 n, wp_fn_t fn, void *ctx) { for (u32 i = 0; i < n; i++) fn(i, ctx); }
wp_job_t wp_submit(void (*fn)(void *arg), void *arg) { fn(arg); return 0; }
void wp_wait(wp_job_t job) { (void)job; }
```

- [ ] **Step 3: Implement `drivers/storage/blockdev_pick.c`** (pure):
```c
#include "blockdev.h"
#define MIN_PRIMARY_SECTORS (64ull * 1024 * 1024 / 512)
i32 blockdev_pick_primary(const u64 *sectors, const bool *atapi, const bool *has_ext2, u32 n) {
    for (u32 i = 0; i < n; i++) if (!atapi[i] && has_ext2[i]) return (i32)i;
    for (u32 i = 0; i < n; i++) if (!atapi[i] && sectors[i] >= MIN_PRIMARY_SECTORS) return (i32)i;
    return -1;
}
u32 blockdev_chunks(u64 lba, u32 count, u32 max_xfer, u64 *out_lba, u32 *out_cnt, u32 max_out) {
    if (max_xfer == 0) return 0;
    u32 n = 0;
    while (count && n < max_out) {
        u32 c = count < max_xfer ? count : max_xfer;
        out_lba[n] = lba; out_cnt[n] = c; n++;
        lba += c; count -= c;
    }
    return n;
}
```
Run `make host-test`. Expected: `workpool ok`, `blockdev ok`.

- [ ] **Step 4: Implement `drivers/storage/blockdev.c`** (kernel):
```c
#include "kernel.h"
#include "blockdev.h"
static blockdev_t *g_devs[BLOCKDEV_MAX];
static u32 g_n;
static i32 g_primary = -1;
int blockdev_register(blockdev_t *d) { if (g_n >= BLOCKDEV_MAX) return -1; g_devs[g_n] = d; return (int)g_n++; }
u32 blockdev_count(void) { return g_n; }
blockdev_t *blockdev_at(u32 i) { return i < g_n ? g_devs[i] : NULL; }
static bool probe_ext2(blockdev_t *d) {
    static u8 sb[1024];
    if (d->atapi || d->sectors < 4) return false;
    if (d->read(d, 2, 2, sb) != 0) return false;   /* superblock at byte 1024 */
    return sb[56] == 0x53 && sb[57] == 0xEF;       /* s_magic 0xEF53 */
}
void blockdev_select_primary(void) {
    u64 s[BLOCKDEV_MAX]; bool a[BLOCKDEV_MAX], e[BLOCKDEV_MAX];
    for (u32 i = 0; i < g_n; i++) { s[i] = g_devs[i]->sectors; a[i] = g_devs[i]->atapi; e[i] = probe_ext2(g_devs[i]); }
    g_primary = blockdev_pick_primary(s, a, e, g_n);
}
blockdev_t *blockdev_primary(void) { return g_primary >= 0 ? g_devs[g_primary] : NULL; }
bool blockdev_present(void) { return g_primary >= 0; }
u64  blockdev_sectors(void) { return g_primary >= 0 ? g_devs[g_primary]->sectors : 0; }
static int xfer(u64 lba, u32 count, void *buf, bool wr) {
    blockdev_t *d = blockdev_primary();
    if (!d) return -1;
    if (lba + count > d->sectors) return -2;
    u64 L[16]; u32 C[16];
    u8 *p = (u8 *)buf;
    while (count) {
        u32 n = blockdev_chunks(lba, count, d->max_xfer, L, C, 16);
        if (n == 0) return -3;
        for (u32 i = 0; i < n; i++) {
            int rc = wr ? d->write(d, L[i], C[i], p) : d->read(d, L[i], C[i], p);
            if (rc != 0) return rc;
            p += (u64)C[i] * d->sector_size; lba += C[i]; count -= C[i];
        }
    }
    return 0;
}
int blockdev_read(u64 lba, u32 count, void *buf) { return xfer(lba, count, buf, false); }
int blockdev_write(u64 lba, u32 count, const void *buf) { return xfer(lba, count, (void *)buf, true); }
```

- [ ] **Step 5: Register ATA as a blockdev.** At the end of a successful `ata_init()` in `drivers/storage/ata.c`:
```c
static int ata_bd_read(struct blockdev *d, u64 lba, u32 n, void *buf) {
    (void)d; if (lba > 0x0FFFFFFFull || n > 255) return -4; return ata_read_sectors((u32)lba, (u8)n, buf);
}
static int ata_bd_write(struct blockdev *d, u64 lba, u32 n, const void *buf) {
    (void)d; if (lba > 0x0FFFFFFFull || n > 255) return -4; return ata_write_sectors((u32)lba, (u8)n, buf);
}
static blockdev_t ata_bd = { .name = "ata0", .sector_size = 512, .max_xfer = 255,
                             .read = ata_bd_read, .write = ata_bd_write };
/* in ata_init(), after IDENTIFY succeeds: */
    kstrncpy(ata_bd.model, ata_model, 40); ata_bd.sectors = ata_sectors;
    blockdev_register(&ata_bd);
```
(Phase 0 hw-b adds LBA48 and DMA behind the same callbacks.)

- [ ] **Step 6: Switch the callers to blockdev.**
  - `kernel/ext2.c` lines 20–25 and 359: `ata_read_sectors(lba + done, (u8)chunk, …)` → `blockdev_read(lba + done, chunk, …)`, and the same for write. Remove the 255 split comment, because blockdev chunks.
  - `kernel/users.c:319`, `kernel/settings.c:103`: `ata_get_sectors()` → `(u32)(blockdev_sectors() > 0xFFFFFFFFull ? 0xFFFFFFFFull : blockdev_sectors())`. The reserved LBAs stay below 2^32 by design; the users/settings areas are near the end of the first 2^32 sectors.
  - `kernel/users.c:325`, `kernel/settings.c:110`: `ata_is_present()` → `blockdev_present()`.
  - Lines 406/736 and 178/311: `ata_read_sectors(x, 1, p)` → `blockdev_read(x, 1, p)`; the same for writes.
  - Include `blockdev.h` (add `-Idrivers/storage` to `KERN_CFLAGS`, or use a relative include).

- [ ] **Step 7: `hwinfo` (W0 level of detail).** Implement `kernel/hwinfo.c`:
  - `hwinfo_init()` reads CPUID vendor (leaf 0), brand (0x80000002–4), family/model/stepping (leaf 1) and features (APIC = leaf1 edx bit 9, x2APIC = ecx bit 21, SSE2 = edx bit 26, PDPE1GB = 0x80000001 edx bit 26, INVTSC = 0x80000007 edx bit 8). It sets `cores_present = cores_online = 1` and `ram_usable_bytes` from the PMM's free+used frame count × 4096 (expose `pmm_total_frames()` from `kernel/paging.c` if it isn't public).
  - `hwinfo_format` prints:
```
CPU      <brand> (<vendor>)
Cores    <online> of <present> online
Memory   <GB with 1 decimal> GB
Disk N   <controller> · <model> · <GB> GB
Display  <adapter> · <vram MB> MB VRAM · <w>x<h>
Network  <model> · <mac> · <up|down>
```
  - The ATA driver fills `disks[0]` (controller "IDE"). `vesa_init` fills `display` (adapter "Bochs/VirtualBox VGA", VRAM from BGA register 0x0A × 64 KB if readable, else 0). e1000 fills `nics[0]`.
  - Call `hwinfo_init()` right after `pmm_init()` in `kernel/kernel.c`, `wp_init()` after `scheduler_init()`, and `blockdev_select_primary()` after `ata_init()`, before ext2 mount.
  - `/proc/cpuinfo` (`kernel/vfs.c:704`) renders one `processor : i` block per online core from `hwinfo_get()`.
  - `/proc/meminfo` shows `MemTotal: <kB> kB`.
  - Add a `hwinfo` shell command that prints `hwinfo_format`.

- [ ] **Step 8: Build and verify.** Run `bash tools/wt/locked-make.sh && make host-test && QA_OUT=../qa/w0-t6 bash tools/qa/boot-check.sh 2`. Expected: `2/2`. Then boot interactively, run `hwinfo` in Terminal and save its output to `..\qa\w0-t6\hwinfo.txt`. Expected: the real CPU brand, `1 of 1`, about 4.0 GB (the QEMU default `-m 4096M`), `IDE`, the BGA display and the e1000 NIC. Files in `/home` written before W0 still open, which proves ext2 works over blockdev.

- [ ] **Step 9: Commit**

```bash
git add kernel/hwinfo.[ch] kernel/workpool.[ch] drivers/storage/blockdev*.[ch] drivers/storage/ata.c kernel/ext2.c kernel/users.c kernel/settings.c kernel/kernel.c kernel/vfs.c kernel/paging.c shell/shell.c tests/host/test_workpool.c tests/host/test_blockdev.c Makefile
git commit -m "feat(hw): freeze hwinfo/workpool/blockdev interfaces; ext2, users and settings go through blockdev; hwinfo command

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: W0 gate, tag, worktrees for wave 1

**Files:**
- Modify: `docs/superpowers/specs/2026-10-03-careos-v10-redesign-roadmap.md` (mark W0 done; record the tag)

- [ ] **Step 1: Clean build from scratch.** Run `make clean && bash tools/wt/locked-make.sh && make host-test`. Expected: success; every host test prints `ok`.

- [ ] **Step 2: Boot check.** Run `QA_OUT=../qa/w0-gate bash tools/qa/boot-check.sh 5`. Expected `5/5`. (If a pre-existing TCG reboot loop appears, see `memory: careos-qemu-qa-harness`; the failure count must not exceed the Task 2 Step 1 baseline.)

- [ ] **Step 3: Old-disk compatibility.** Boot with the pre-W0 `careos.img`. Log in as an existing user. Expected: theme, wallpaper and font kept; no onboarding; home files intact.

- [ ] **Step 4: Tag and create the wave 1 worktrees.**
```bash
git tag v10-w0
for a in hw-a hw-b vis-a vis-b start-a start-b app-a app-b; do bash tools/wt/new-worktree.sh $a; done
git worktree list
```
Expected: 8 worktrees under `D:\Users\mhetm\Downloads\CareOS_v9_full\wt\`.

- [ ] **Step 5: Commit** the roadmap status line `W0: done at tag v10-w0 (<sha>)`:
```bash
git add docs/superpowers/specs/2026-10-03-careos-v10-redesign-roadmap.md
git commit -m "docs(v10): W0 complete; wave 1 worktrees created

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```
