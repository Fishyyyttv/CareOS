# CareOS Startup Scripts (rc.care) Design

**Date:** 2026-07-16
**Status:** Approved
**Goal:** Make CareLang the OS's real configuration/automation layer, starting with system- and per-user startup scripts that run after login.

---

## Context

CareOS already has a working interpreted language, CareLang (`kernel/care_lang.c`): variables, functions, `if`/`while`, string + numeric expressions, and a handful of native hooks (`sys_alert`, `sys_window`, `sys_beep`, `sys_exec`). Today it is almost unused — one shell command and one terminal code path can run a `.care` script manually, and that's it.

This is phase one of a longer plan to make CareLang the thing that actually distinguishes CareOS from a themed Linux desktop: instead of another generic app, give the OS a scripting layer nothing else has. This phase's slice is startup and config — the OS (and each user) configures its own session behavior in CareLang.

Two things this design deliberately does **not** touch:
- `kernel/settings.c`'s versioned binary blob on raw disk sectors. It runs before any filesystem is mounted and is fast/crash-safe. Rewriting it as a `.care` file would create a chicken-and-egg problem (need to read a script to know how to boot, before the FS layer that would serve that script exists). This design adds a layer *on top* of it instead.
- Language features not needed for this phase (arrays/lists, WM event hooks, a REPL app). Those are later phases.

---

## Architecture

```
gui_run() [gui/gui.c]
  ├─ boot splash (unchanged)
  ├─ run_login_flow()               (unchanged)
  ├─ rc_care_run_startup()          <-- NEW, runs here
  │    ├─ read /etc/rc.care (VFS)          — system-wide, optional
  │    ├─ care_lang_exec(...)              — own isolated env
  │    ├─ read /home/<user>/rc.care (VFS)  — per-user, optional
  │    └─ care_lang_exec(...)              — own isolated env
  └─ desktop opens (wm_open(APP_TERMINAL...), notify_push, ...)  (unchanged)
```

- New file: `kernel/rc_care.c`, exposing `void rc_care_run_startup(void)`.
- Hook point: inside `gui_run()` in `gui/gui.c`, immediately after `run_login_flow()` returns success and before the first window (`wm_open(APP_TERMINAL, ...)`) is opened.
- System script and user script run as two independent `care_lang_exec` calls — no shared variables or state between them. Keeps the model simple: each is a flat, self-contained script.
- Missing file (system, user, or both) is the expected common case on a fresh disk and is not an error — silent skip, boot proceeds exactly as it does today.

---

## New Native Functions

Added to `cl_init_natives` in `kernel/care_lang.c`, alongside the existing `sys_alert`, `sys_window`, `sys_beep`, `sys_exec` (all four are kept unchanged for backward compatibility):

| Function | Signature | Behavior |
|---|---|---|
| `sys_launch` | `sys_launch(name: string)` | Opens an app by name via a small `name -> app_id_t` lookup table (e.g. `"notes"`, `"terminal"`, `"files"`, `"settings"`, `"browser"`, ...), matching the existing `app_id_t` enum in `gui/gui.h`. Unknown name is a no-op. Friendlier than the existing numeric `sys_exec(id)`. |
| `sys_set_theme` | `sys_set_theme(n: number)` | Calls `settings_set_theme(n)` |
| `sys_set_wallpaper` | `sys_set_wallpaper(n: number)` | Calls `settings_set_wallpaper(n)` |
| `sys_get_setting` | `sys_get_setting(name: string) -> number` | Reads back a known setting from `settings_get()`: `"theme"`, `"wallpaper"`, `"clock_24h"`, `"taskbar_centered"`, `"mouse_sensitivity"`. Unknown name returns `0`. |
| `sys_username` | `sys_username() -> string` | Wraps `user_current_name()` |
| `sys_is_root` | `sys_is_root() -> number` | Wraps `user_is_root()`, returns `1`/`0` |
| `sys_first_run` | `sys_first_run() -> number` | Returns `1` exactly once per user — true if `/home/<user>/.welcomed` does not yet exist (and creates it as a side effect), `0` on every run after |

---

## Script Storage & Editing

- `/etc/rc.care` (system-wide) and `/home/<user>/rc.care` (per-user) are plain text files living in the existing VFS.
- They are editable today with zero new UI — the existing text editor app (`apps/app_editor.c`) opens arbitrary VFS files already, reachable via the Files app.
- On first boot (fresh disk, no `/etc/rc.care` present) and on first-time user home directory creation (no `/home/<user>/rc.care` present), CareOS seeds a default file with commented-out example lines (CareLang supports both `//` and `#` comments), so the feature is discoverable rather than shipping as a silent empty capability. Example seed content:

```
# CareOS startup script — this runs every time you log in.
# Uncomment any line below to try it out.

# sys_launch("notes");
# sys_set_theme(1);
# if (sys_first_run()) {
#     sys_alert("Welcome to CareOS!");
# }
```

---

## Error Handling

- `care_lang_exec` already reports failure via its return value and `env.had_error`. `rc_care_run_startup` checks this after each of the two script runs.
- On failure, a modal dialog is shown naming which script failed (`system` or `user`) before continuing. This uses the existing window/dialog mechanism (`wm_open` with a message-style window, matching patterns already used elsewhere in `gui/gui.c` for alerts).
- The dialog must be dismissed before the boot sequence continues to opening the desktop. Either way — dismissed after error, or no error at all — the desktop reaches the same ready state. A broken script cannot corrupt settings (native functions either fully succeed or no-op) or crash boot.

---

## Testing

1. Create `/etc/rc.care` containing `sys_alert("hi");` — verify it fires once, after login, before the desktop's first window appears.
2. Create `/home/<user>/rc.care` containing `sys_set_theme(1);` — verify the theme has flipped by the time the desktop is visible.
3. Write a script with a deliberate syntax error — verify the error dialog appears, names the right script, and the desktop still loads normally after dismissal (no crash, no hang).
4. Fresh disk / fresh user with no rc.care files present — verify totally silent, boot behaves identically to today.
5. `sys_first_run()` — verify it returns true exactly once per user (checked across two consecutive logins for the same account).
6. `sys_launch("notes")` — verify it opens the Notes app; an unknown app name is a no-op, not a crash.

---

## Explicitly Out of Scope (future phases)

- REPL / live console app for CareLang
- WM automation hooks (`on_window_open`, `on_app_launch`, timers)
- Richer language features (arrays/lists, more operators, wider stdlib)
- Apps written entirely as `.care` scripts

These are later phases of the broader "make CareLang the OS's identity" roadmap, not part of this slice.
