# CareOS rc.care Startup Scripts Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give CareLang a real job by adding system-wide and per-user startup scripts (`rc.care`) that run after login, backed by a small new set of native functions (app launching, settings read/write, user/system info).

**Architecture:** A new `kernel/rc_care.c` module reads `/etc/rc.care` and `/home/<user>/rc.care` from the existing VFS and runs each through the existing `care_lang_exec` interpreter, right after login succeeds in `gui/gui.c`'s `gui_run()`. CareLang itself gains 7 new native functions so scripts can do something real. A missing script is seeded with a commented-out example and silently skipped that boot; a script that errors shows a blocking modal dialog naming which script failed, then boot continues either way.

**Tech Stack:** Bare-metal C (no libc), existing CareOS kernel/GUI internals (VFS, `gfx_*` drawing primitives, `wm_open`, `settings_*`, `user_*`). No new external dependencies. No host-side test framework exists for this codebase — verification is done by building with `make` and manually exercising the boot flow in QEMU (`make run`), reading the `-serial stdio` log CareOS already writes diagnostics to throughout `gui.c`.

## Global Constraints

- Boot-critical settings storage (`kernel/settings.c`, raw ATA sectors) is not touched by this work — startup scripts are strictly a layer on top, per the approved design spec.
- No new language features (arrays, WM event hooks, REPL) — out of scope for this phase per `docs/superpowers/specs/2026-07-16-careos-rc-care-startup-scripts-design.md`.
- All new native functions follow the existing `cl_val_t (*)(cl_val_t *args, u32 nargs)` signature and are registered through the existing `cl_register_native` helper — do not invent a new registration mechanism.
- A script error must never crash boot or corrupt settings; worst case is a dialog the user has to dismiss.

---

## File Structure

| File | Change |
|---|---|
| `kernel/care_lang.c` | Modify — bump `CL_MAX_FUNCS`, add 7 native functions, register them |
| `kernel/rc_care.c` | Create — `rc_care_run_startup()`, script reading/seeding/execution, error modal |
| `include/kernel.h` | Modify — declare `rc_care_run_startup(void)` |
| `gui/gui.c` | Modify — call `rc_care_run_startup()` after login, before the desktop opens |
| `Makefile` | Modify — add `kernel/rc_care.c` to `C_SRC` |

---

### Task 1: Extend CareLang's native function surface

**Files:**
- Modify: `kernel/care_lang.c:142` (`CL_MAX_FUNCS` define)
- Modify: `kernel/care_lang.c:502-524` (native function definitions + `cl_init_natives`)

**Interfaces:**
- Consumes: existing `cl_val_t`, `vnum()`, `vstr()`, `cl_register_native()`, `app_id_t`, `app_default_size()`, `wm_open()`, `settings_get()`, `settings_set_theme()`, `settings_set_wallpaper()`, `user_current_name()`, `user_is_root()`, `vfs_resolve_path()`, `vfs_mkfile()`, `ksprintf()`, `kstrcmp()` — all already declared in `include/kernel.h` / `gui/gui.h`, both already included at the top of `care_lang.c`.
- Produces: 7 new CareLang-callable native functions — `sys_launch(name)`, `sys_set_theme(n)`, `sys_set_wallpaper(n)`, `sys_get_setting(name)`, `sys_username()`, `sys_is_root()`, `sys_first_run()` — usable from any `.care` script from this point on, including the scripts Task 2 will write and execute.

- [ ] **Step 1: Bump `CL_MAX_FUNCS`**

`kernel/care_lang.c` line 142 currently reads:

```c
#define CL_MAX_FUNCS 16
```

The 4 existing natives (`sys_alert`, `sys_window`, `sys_beep`, `sys_exec`) plus the 7 new ones below eat 11 of those 16 slots in the root environment, leaving only 5 for user-defined `func` declarations inside scripts. Change it to:

```c
#define CL_MAX_FUNCS 24  /* 11 natives (4 existing + 7 rc.care) + headroom for user-defined funcs in scripts */
```

- [ ] **Step 2: Add the 7 native function implementations**

Insert immediately after the existing `nat_sys_exec` function (`kernel/care_lang.c:514-517`), before `cl_init_natives`:

```c
static cl_val_t nat_sys_launch(cl_val_t *args, u32 nargs) {
    if (nargs < 1 || !args[0].is_str) return vnum(0);
    static const struct { const char *name; app_id_t app; const char *title; } table[] = {
        { "terminal", APP_TERMINAL, "Terminal" },
        { "notes",    APP_NOTES,    "Notes" },
        { "files",    APP_FILES,    "Files" },
        { "sysmon",   APP_SYSMON,   "Monitor" },
        { "calc",     APP_CALC,     "Calculator" },
        { "about",    APP_ABOUT,    "About" },
        { "help",     APP_HELP,     "Help" },
        { "browser",  APP_BROWSER,  "Browser" },
        { "settings", APP_SETTINGS, "Settings" },
        { "pkgmgr",   APP_PKGMGR,   "Packages" },
        { "editor",   APP_EDITOR,   "Editor" },
        { "paint",    APP_PAINT,    "Paint" },
        { "clock",    APP_CLOCK,    "Clock" },
        { "netmon",   APP_NETMON,   "NetMon" },
        { "users",    APP_USERS,    "Users" },
        { "maze",     APP_MAZE,     "Maze" },
        { "3d",       APP_3D,       "3D Demo" },
        { "doom",     APP_DOOM,     "DOOM" },
    };
    for (u32 i = 0; i < sizeof(table)/sizeof(table[0]); i++) {
        if (kstrcmp(args[0].str, table[i].name) == 0) {
            i32 scrw = (i32)SCREEN_W, scrh = (i32)SCREEN_H, ww, wh;
            app_default_size(table[i].app, scrw, scrh, &ww, &wh);
            wm_open(table[i].app, table[i].title, (scrw - ww) / 2, (scrh - wh) / 2, ww, wh);
            return vnum(1);
        }
    }
    return vnum(0);
}
static cl_val_t nat_sys_set_theme(cl_val_t *args, u32 nargs) {
    if (nargs >= 1 && !args[0].is_str) settings_set_theme((u32)args[0].num);
    return vnum(0);
}
static cl_val_t nat_sys_set_wallpaper(cl_val_t *args, u32 nargs) {
    if (nargs >= 1 && !args[0].is_str) settings_set_wallpaper((u32)args[0].num);
    return vnum(0);
}
static cl_val_t nat_sys_get_setting(cl_val_t *args, u32 nargs) {
    if (nargs < 1 || !args[0].is_str) return vnum(0);
    const careos_settings_t *cfg = settings_get();
    if (!cfg) return vnum(0);
    if (kstrcmp(args[0].str, "theme") == 0)             return vnum((i32)cfg->theme);
    if (kstrcmp(args[0].str, "wallpaper") == 0)          return vnum((i32)cfg->wallpaper);
    if (kstrcmp(args[0].str, "clock_24h") == 0)          return vnum((i32)cfg->clock_24h);
    if (kstrcmp(args[0].str, "taskbar_centered") == 0)   return vnum((i32)cfg->taskbar_centered);
    if (kstrcmp(args[0].str, "mouse_sensitivity") == 0)  return vnum((i32)cfg->mouse_sensitivity);
    return vnum(0);
}
static cl_val_t nat_sys_username(cl_val_t *args, u32 nargs) {
    (void)args; (void)nargs;
    return vstr(user_current_name());
}
static cl_val_t nat_sys_is_root(cl_val_t *args, u32 nargs) {
    (void)args; (void)nargs;
    return vnum(user_is_root() ? 1 : 0);
}
static cl_val_t nat_sys_first_run(cl_val_t *args, u32 nargs) {
    (void)args; (void)nargs;
    char marker_path[64];
    ksprintf(marker_path, "/home/%s/.welcomed", user_current_name());
    if (vfs_resolve_path(marker_path)) return vnum(0);
    char home_path[48];
    ksprintf(home_path, "/home/%s", user_current_name());
    fs_node_t *home = vfs_resolve_path(home_path);
    if (home) vfs_mkfile(home, ".welcomed");
    return vnum(1);
}
```

- [ ] **Step 3: Register the new natives**

Replace `cl_init_natives` (`kernel/care_lang.c:519-524`):

```c
static void cl_init_natives(cl_env_t *env) {
    cl_register_native(env, "sys_alert", nat_sys_alert);
    cl_register_native(env, "sys_window", nat_sys_window);
    cl_register_native(env, "sys_beep", nat_sys_beep);
    cl_register_native(env, "sys_exec", nat_sys_exec);
    cl_register_native(env, "sys_launch", nat_sys_launch);
    cl_register_native(env, "sys_set_theme", nat_sys_set_theme);
    cl_register_native(env, "sys_set_wallpaper", nat_sys_set_wallpaper);
    cl_register_native(env, "sys_get_setting", nat_sys_get_setting);
    cl_register_native(env, "sys_username", nat_sys_username);
    cl_register_native(env, "sys_is_root", nat_sys_is_root);
    cl_register_native(env, "sys_first_run", nat_sys_first_run);
}
```

- [ ] **Step 4: Build**

Run: `make clean && make`
Expected: Build succeeds with no new warnings/errors from `kernel/care_lang.c`. If the compiler complains about unused `app_id_t`/`wm_open`/`app_default_size` symbols not being visible, confirm `#include "../gui/gui.h"` is still present at the top of `care_lang.c` (it already is — this step should not require adding it).

- [ ] **Step 5: Manually verify the new natives in QEMU**

Run: `make run`

In the CareOS GUI: log in as the default account (`user` / `CareOS123` — the account whose home directory is `/home/user`, matching Editor's hardcoded default browse path used below), open **Terminal**, then:

```
touch test.care
```

Open **Editor**, browse to `test.care` in the sidebar, and enter this content, then save:

```
print sys_username();
print sys_is_root();
sys_set_theme(1);
print sys_get_setting("theme");
sys_launch("notes");
print sys_first_run();
print sys_first_run();
```

Back in Terminal, run:

```
care test.care
```

Expected output: the current username, `1` or `0` depending on the account, `1` (theme now light), a Notes window opens on screen, then `1` followed by `0` (first-run marker fires exactly once).

- [ ] **Step 6: Commit**

```bash
git add kernel/care_lang.c
git commit -m "feat: add rc.care native functions to CareLang (sys_launch, settings, user info)"
```

---

### Task 2: Startup script execution — `kernel/rc_care.c`

**Files:**
- Create: `kernel/rc_care.c`
- Modify: `include/kernel.h:313` (add declaration)
- Modify: `gui/gui.c:617-622` (call the new function after login)
- Modify: `Makefile:71` (add new source file to build)

**Interfaces:**
- Consumes: `care_lang_exec()`, `vfs_root()`, `vfs_find()`, `vfs_mkdir()`, `vfs_mkfile()`, `vfs_resolve_path()`, `vfs_read()`, `vfs_write()`, `user_current_name()`, `ksprintf()`, `kstrlen()`, `serial_write()` — all in `include/kernel.h` already.
- Produces: `void rc_care_run_startup(void)`, called once from `gui_run()` right after login succeeds. This task's error branches only `serial_write` a diagnostic — Task 3 replaces that with a blocking modal dialog.

- [ ] **Step 1: Create `kernel/rc_care.c`**

```c
/* CareOS v9 -- kernel/rc_care.c -- system & per-user startup scripts (rc.care) */
#include "kernel.h"

#define RC_CARE_MAX_SIZE 8192

static const char *RC_CARE_DEFAULT_SCRIPT =
    "# CareOS startup script -- this runs every time you log in.\n"
    "# Uncomment any line below to try it out.\n"
    "\n"
    "# sys_launch(\"notes\");\n"
    "# sys_set_theme(1);\n"
    "# if (sys_first_run()) {\n"
    "#     sys_alert(\"Welcome to CareOS!\");\n"
    "# }\n";

static void rc_care_seed_default(fs_node_t *dir, const char *filename) {
    fs_node_t *f = vfs_mkfile(dir, filename);
    if (f) vfs_write(f, RC_CARE_DEFAULT_SCRIPT, (u32)kstrlen(RC_CARE_DEFAULT_SCRIPT));
}

static bool rc_care_read_file(const char *path, char *buf, u32 buf_max) {
    fs_node_t *f = vfs_resolve_path(path);
    if (!f) return false;
    int n = vfs_read(f, buf, buf_max - 1);
    if (n < 0) return false;
    buf[n] = '\0';
    return true;
}

static void rc_care_run_one(const char *path, const char *which) {
    char buf[RC_CARE_MAX_SIZE];
    if (!rc_care_read_file(path, buf, sizeof(buf))) return;
    serial_write("  [rc_care] running ");
    serial_write(which);
    serial_write(" script\n");
    if (care_lang_exec(buf, (u32)kstrlen(buf)) != 0) {
        serial_write("  [rc_care] ");
        serial_write(which);
        serial_write(" script error\n");
    }
}

void rc_care_run_startup(void) {
    /* system-wide: /etc/rc.care */
    fs_node_t *etc = vfs_find(vfs_root(), "etc");
    if (!etc) etc = vfs_mkdir(vfs_root(), "etc");
    if (etc) {
        if (!vfs_find(etc, "rc.care")) {
            rc_care_seed_default(etc, "rc.care");
            serial_write("  [rc_care] seeded /etc/rc.care\n");
        } else {
            rc_care_run_one("/etc/rc.care", "system");
        }
    }

    /* per-user: /home/<user>/rc.care */
    char home_path[48];
    ksprintf(home_path, "/home/%s", user_current_name());
    fs_node_t *home = vfs_resolve_path(home_path);
    if (home) {
        if (!vfs_find(home, "rc.care")) {
            rc_care_seed_default(home, "rc.care");
            serial_write("  [rc_care] seeded user rc.care\n");
        } else {
            char user_path[64];
            ksprintf(user_path, "/home/%s/rc.care", user_current_name());
            rc_care_run_one(user_path, "user");
        }
    }
}
```

- [ ] **Step 2: Declare it in `include/kernel.h`**

`include/kernel.h:313` currently ends the CareLang section with:

```c
int care_lang_exec_buf(const char *src, u32 len, char *out, u32 out_max);
```

Add immediately after it:

```c

/* -- Startup scripts (rc.care) --------------------------------------------- */
void rc_care_run_startup(void);
```

- [ ] **Step 3: Add to the build**

`Makefile:71` currently reads:

```makefile
             kernel/care_lang.c    \
```

Add a new line right after it:

```makefile
             kernel/care_lang.c    \
             kernel/rc_care.c      \
```

- [ ] **Step 4: Wire the call into `gui_run()`**

`gui/gui.c:617-622` currently reads:

```c
    kmemset(&mouse, 0, sizeof(mouse));
    if (!run_login_flow(&mouse)) {
        serial_write("  [gui_run] login flow returned failure\n");
    }

    sw = (i32)SCREEN_W;
```

Change to:

```c
    kmemset(&mouse, 0, sizeof(mouse));
    if (!run_login_flow(&mouse)) {
        serial_write("  [gui_run] login flow returned failure\n");
    }

    rc_care_run_startup();

    sw = (i32)SCREEN_W;
```

- [ ] **Step 5: Build**

Run: `make clean && make`
Expected: builds cleanly, `kernel/rc_care.o` is produced.

- [ ] **Step 6: Verify fresh-disk behavior (silent, seeds files)**

Run: `make reset-disk && make run`

In QEMU's serial output (visible in the terminal `make run` runs in, since `QEMUBASE` includes `-serial stdio`), after logging in for the first time, expect to see:

```
  [rc_care] seeded /etc/rc.care
  [rc_care] seeded user rc.care
```

and no `script error` lines. The desktop should reach its normal ready state (Terminal window opens, "Desktop ready." toast) exactly as before this change.

- [ ] **Step 7: Verify scripts actually run on a second boot**

Still in the running QEMU session (or after `make run` again without `reset-disk`), open Terminal and run:

```
cat /etc/rc.care
cat /home/user/rc.care
```

Expected: both show the commented-out default script content seeded in Step 6. Edit `/home/user/rc.care` via the Editor app to uncomment `sys_set_theme(1);`, save, then log out and back in (or `make run` again without resetting the disk). Serial log should show:

```
  [rc_care] running user script
```

and the desktop should come up in light theme.

- [ ] **Step 8: Commit**

```bash
git add kernel/rc_care.c include/kernel.h gui/gui.c Makefile
git commit -m "feat: run /etc/rc.care and per-user rc.care on login"
```

---

### Task 3: Startup script error dialog + full acceptance pass

**Files:**
- Modify: `kernel/rc_care.c` (add blocking modal, call it from error branches)

**Interfaces:**
- Consumes: `mouse_t`, `button_t`, `rect_make()`, `button_take_click()`, `button_draw()`, `mouse_update()`, `mouse_draw_cursor()`, `keyboard_haschar()`, `keyboard_getchar()`, `keyboard_flush()`, `gfx_rect()`, `gfx_shadow()`, `gfx_rect_rounded()`, `gfx_rect_rounded_outline()`, `gfx_str_centered()`, `gfx_flip()`, `COL_BG`, `COL_SURFACE`, `COL_RED`, `COL_TEXT`, `COL_DIM`, `COL_PRIMARY`, `COL_WHITE`, `COL_TRANSPARENT`, `SCREEN_W`, `SCREEN_H`, `ksprintf()`, `kstrcpy()` — all already declared in `gui/gui.h` / `include/kernel.h`. `rc_care.c` needs a new `#include "../gui/gui.h"` for the drawing/input types (it currently only includes `kernel.h`).
- Produces: no new public interface — this task finishes the module Task 2 started.

- [ ] **Step 1: Add the `gui.h` include**

`kernel/rc_care.c` currently starts with:

```c
#include "kernel.h"
```

Change to:

```c
#include "kernel.h"
#include "../gui/gui.h"
```

- [ ] **Step 2: Add the blocking error modal**

Insert into `kernel/rc_care.c`, after `rc_care_read_file` and before `rc_care_run_one`:

```c
static void rc_care_show_error_modal(const char *which) {
    mouse_t mouse;
    kmemset(&mouse, 0, sizeof(mouse));
    mouse.x = (i32)SCREEN_W / 2;
    mouse.y = (i32)SCREEN_H / 2;
    keyboard_flush();

    char title[64];
    ksprintf(title, "Startup script error (%s)", which);

    i32 pw = 420, ph = 160;
    i32 px = ((i32)SCREEN_W - pw) / 2;
    i32 py = ((i32)SCREEN_H - ph) / 2;
    button_t ok_btn = (button_t){
        .rect = rect_make(px + pw / 2 - 60, py + ph - 52, 120, 34),
        .hover = false,
        .pressed = false,
        .active = true,
        .bg = COL_PRIMARY,
        .fg = COL_WHITE,
    };
    kstrcpy(ok_btn.label, "OK");

    while (1) {
        while (keyboard_haschar()) {
            char c = keyboard_getchar();
            if (c == '\n') return;
        }
        mouse_update(&mouse);
        if (button_take_click(&ok_btn, &mouse)) return;

        gfx_rect(0, 0, (i32)SCREEN_W, (i32)SCREEN_H, COL_BG);
        gfx_shadow(px, py, pw, ph);
        gfx_rect_rounded(px, py, pw, ph, 10, COL_SURFACE);
        gfx_rect_rounded_outline(px, py, pw, ph, 10, COL_RED);
        gfx_str_centered(px, py + 24, pw, title, COL_RED, COL_TRANSPARENT);
        gfx_str_centered(px, py + 60, pw, "Script stopped early due to an error.", COL_TEXT, COL_TRANSPARENT);
        gfx_str_centered(px, py + 84, pw, "Fix it in the Files/Editor app, then log in again.", COL_DIM, COL_TRANSPARENT);
        button_draw(&ok_btn);
        mouse_draw_cursor(mouse.x, mouse.y);
        gfx_flip();
        __asm__ volatile("sti; hlt");
    }
}
```

- [ ] **Step 3: Call the modal from the error branch**

In `rc_care_run_one` (from Task 2), replace:

```c
    if (care_lang_exec(buf, (u32)kstrlen(buf)) != 0) {
        serial_write("  [rc_care] ");
        serial_write(which);
        serial_write(" script error\n");
    }
```

with:

```c
    if (care_lang_exec(buf, (u32)kstrlen(buf)) != 0) {
        serial_write("  [rc_care] ");
        serial_write(which);
        serial_write(" script error\n");
        rc_care_show_error_modal(which);
    }
```

- [ ] **Step 4: Build**

Run: `make clean && make`
Expected: builds cleanly.

- [ ] **Step 5: Verify the error dialog**

Run: `make run`. Log in, open Editor, edit `/home/user/rc.care` to include a deliberate syntax error, e.g. append:

```
if (
```

Save, then log out and log back in (or restart CareOS in QEMU without resetting the disk).

Expected: after login, before the desktop's Terminal window appears, a dialog titled "Startup script error (user)" appears with an OK button. Press Enter (or click OK) — the dialog closes and the desktop loads normally (Terminal opens, "Desktop ready." toast, etc.), same as any other boot.

Fix the script back to valid CareLang (remove the broken line) and confirm on the next login the dialog no longer appears.

- [ ] **Step 6: Full acceptance pass (all design spec scenarios)**

Run through each, in QEMU (`make run`, `make reset-disk` where noted):

1. `sys_alert` from `/etc/rc.care` — edit `/etc/rc.care` to contain `sys_alert("hi");`, log in, confirm the alert toast/notification fires once, after login, before the desktop's first window appears.
2. `sys_set_theme` from user script — already covered in Task 2 Step 7; reconfirm here if not already verified this session.
3. Broken script → dialog → desktop still loads — covered in Step 5 above.
4. Fresh disk, no rc.care files — `make reset-disk && make run`, confirm totally silent (no dialogs, no alerts) and boot behaves identically to before this feature existed.
5. `sys_first_run()` — covered in Task 1 Step 5 (returns `1` then `0` in the same script run); additionally confirm across two separate logins (log out, log back in) that it stays `0` on the second login.
6. `sys_launch("notes")` opens Notes; `sys_launch("not-a-real-app")` is a silent no-op (no crash, no dialog, no window).

- [ ] **Step 7: Commit**

```bash
git add kernel/rc_care.c
git commit -m "feat: show blocking dialog when a startup script errors"
```
