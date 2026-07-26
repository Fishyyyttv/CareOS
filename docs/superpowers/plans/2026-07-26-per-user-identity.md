# Per-User Identity Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give each CareOS account its own persistent look-and-feel (theme, font, wallpaper, mouse sensitivity, clock format, taskbar layout) that follows its login, a distro-style greeter to pick an account, and clean Log Out / Switch User / Lock — without any user's settings stomping another's.

**Architecture:** Extend the on-disk user record (userdb v5→v6) with four new preference fields; add a `user_prefs_t` accessor pair in `users.c`; add an apply/capture bridge in `settings.c` so login loads a user's prefs into live RAM state (never the global blob) and setting changes are captured back to the logged-in user's record; funnel all login/logout through a single `session_begin`/`session_end` pair; and replace the typed-username login with an account-picker greeter plus launcher session controls.

**Tech Stack:** C (freestanding, `-ffreestanding -m64`), NASM, GRUB Multiboot2, QEMU. No host-side unit-test framework exists — verification is "build → boot in QEMU → read serial log." This plan therefore uses a **boot-time serial self-test** (`users_selftest()`) for pure-logic tasks, and documented manual QEMU checks for GUI tasks. Every "run the test" step greps the serial log for a `PASS`/`FAIL` marker.

## Global Constraints

- **Target:** bare-metal CareOS kernel only (`kernel/`, `gui/`, `drivers/`, `net/`) — NOT the Arch/KDE distro under `iso/airootfs/`.
- **`user_t` (include/kernel.h) and `user_rec_t` (kernel/users.c) MUST stay field-for-field identical** — `apps/app_users.c` and `apps/app_settings.c` cast the `void*` from `user_get_by_uid()` to `user_t*`. Any layout drift is silent memory corruption. Every field added to one MUST be added to the other in the same order.
- **Preserve existing security behavior verbatim:** PBKDF2-HMAC-SHA256 verify path, failed-login lockout, seeded `root`/`user` accounts, and the `must_change_password` login gate must all keep working unchanged.
- **Userdb versioned-migration pattern:** add a new `userdb_entry_v6_t` + a v6 load branch; keep all existing v1–v5 load branches so old installs upgrade with no data loss. Bump `USERDB_VERSION` to `6`.
- **Preference "unset" sentinel:** `USER_PREF_UNSET == 0xFFFFFFFFu`. An unset per-user field means "fall back to the global system default." Existing `USER_THEME_SYSTEM_DEFAULT`/`USER_FONT_SYSTEM_DEFAULT` (both `0xFFFFFFFFu`) already follow this convention.
- **Build/run commands** (from repo root `D:/Users/mhetm/Downloads/CareOS_v9_full/CareOS_v9`):
  - Build: `make` (or `make -j`)
  - Headless boot + serial capture (self-tests):
    `timeout 60 make run-nowindow > "$SCRATCH/serial.log" 2>&1 || true` then `grep -n "selftest" "$SCRATCH/serial.log"`
    where `$SCRATCH` is the scratchpad dir. `run-nowindow` uses `-nographic`, so all serial output lands on stdout. QEMU never exits (infinite GUI loop) — the `timeout` kills it after the self-test has already printed at boot.
  - GUI (windowed) manual check: `make run`.

---

## File Structure

- `include/kernel.h` — add 4 fields to `user_t`; add `user_prefs_t`, `USER_PREF_UNSET`, and new function decls (`user_prefs_get`, `user_prefs_set_current`, `user_session_active`, `user_enum_count`, `user_enum_at`, `session_begin`, `session_end`, `settings_apply_prefs`, `settings_capture_to_current_user`, `users_selftest`).
- `kernel/users.c` — add 4 fields to `user_rec_t`; add `userdb_entry_v6_t` + v6 load branch + v6 save; implement prefs accessors, session enum, `session_begin`/`session_end`, `user_session_active`, and `users_selftest`.
- `kernel/settings.c` — add `settings_apply_prefs` / `settings_capture_to_current_user`; route the six per-user setters to capture-to-user when a real session is active.
- `kernel/kernel.c` — call `users_selftest()` once at boot after `users_init()`.
- `gui/gui.c` — replace typed-username login with account-picker greeter; call `session_begin`/`theme_switch` on login; restructure desktop entry into a re-enterable path; handle the session-action signal in the main loop.
- `gui/launcher.c` — wire the existing footer "power" button to a session-action popup (Lock / Log Out / Switch User) that sets a global signal.
- `gui/gui.h` — declare the session-action signal (`g_session_action`) and its enum.

---

## Task 1: userdb v6 — per-user preference fields + accessors

**Files:**
- Modify: `include/kernel.h` (the `user_t` struct ~lines 406–426; add decls near the other `user_*` decls ~lines 428–449)
- Modify: `kernel/users.c` (`user_rec_t` ~lines 18–39; version macro line 12; add v6 entry struct, load branch, save)
- Modify: `kernel/kernel.c` (call `users_selftest()` after `users_init()`)

**Interfaces:**
- Produces:
  - `typedef struct { u32 theme, font, wallpaper, mouse_sensitivity, clock_24h, taskbar_centered; } user_prefs_t;`
  - `#define USER_PREF_UNSET 0xFFFFFFFFu`
  - `void user_prefs_get(u32 uid, user_prefs_t *out);` — fills `out` from the account's record; any field the record marks unset is returned as `USER_PREF_UNSET`.
  - `void user_prefs_set_current(const user_prefs_t *p);` — writes the logged-in user's record from `p` (ignoring `USER_PREF_UNSET` fields) and persists to the userdb.
  - `void users_selftest(void);` — boot-time self-test, prints `[selftest] ... PASS`/`FAIL` to serial.

- [ ] **Step 1: Add the new record fields (both structs, identical order)**

In `include/kernel.h`, inside `user_t`, immediately after `u32 font_pref;` (line ~418) add:

```c
    u32  wallpaper_pref;
    u32  mouse_pref;
    u32  clock24_pref;
    u32  taskbar_pref;
```

In `kernel/users.c`, inside `user_rec_t`, immediately after `u32 font_pref;` (line ~31) add the identical four lines:

```c
    u32  wallpaper_pref;
    u32  mouse_pref;
    u32  clock24_pref;
    u32  taskbar_pref;
```

- [ ] **Step 2: Add `USER_PREF_UNSET`, `user_prefs_t`, and decls to the header**

In `include/kernel.h`, just above `void *user_get_by_uid(u32 uid);` (line ~428) add:

```c
#define USER_PREF_UNSET 0xFFFFFFFFu
typedef struct {
    u32 theme;
    u32 font;
    u32 wallpaper;
    u32 mouse_sensitivity;
    u32 clock_24h;
    u32 taskbar_centered;
} user_prefs_t;
void user_prefs_get(u32 uid, user_prefs_t *out);
void user_prefs_set_current(const user_prefs_t *p);
void users_selftest(void);
```

- [ ] **Step 3: Add the v6 on-disk entry + bump version**

In `kernel/users.c`, change line 12 to:

```c
#define USERDB_VERSION 6u
```

Add a new entry struct directly above `userdb_entry_v5_t` (line ~55):

```c
typedef struct __attribute__((packed)) {
    u32  uid;
    u32  gid;
    u8   hash_algo;
    u8   must_change;
    u8   pass_hash[USER_PASS_HASH_LEN];
    u8   salt[USER_SALT_LEN];
    u8   active;
    u8   is_root;
    char name[32];
    char home[64];
    char shell[32];
    u32  theme_pref;
    u32  font_pref;
    u32  wallpaper_pref;
    u32  mouse_pref;
    u32  clock24_pref;
    u32  taskbar_pref;
    u16  last_login_year;
    u8   last_login_month;
    u8   last_login_day;
    u8   last_login_hour;
    u8   last_login_minute;
} userdb_entry_v6_t;
```

- [ ] **Step 4: Default the new fields on account creation and in the record initialiser**

In `kernel/users.c`, in `user_add()` (after `u->font_pref = USER_FONT_SYSTEM_DEFAULT;`, line ~690) add:

```c
    u->wallpaper_pref = USER_PREF_UNSET;
    u->mouse_pref     = USER_PREF_UNSET;
    u->clock24_pref   = USER_PREF_UNSET;
    u->taskbar_pref   = USER_PREF_UNSET;
```

- [ ] **Step 5: Add the v6 load branch and make save write v6**

In `kernel/users.c` `users_persist_load()`, replace the `if (hdr->version == USERDB_VERSION)` block's body so it reads `userdb_entry_v6_t` and copies the four new fields. Add this branch (mirroring the existing v5 branch but for v6, and copying the new fields):

```c
    if (hdr->version == USERDB_VERSION) {
        u32 payload_len = hdr->count * (u32)sizeof(userdb_entry_v6_t);
        u32 max_payload = USERDB_SECTORS * USERDB_SECTOR_SIZE - (u32)sizeof(userdb_hdr_t);
        if (payload_len > max_payload) return false;

        u8 *payload = userdb_io + sizeof(userdb_hdr_t);
        if (userdb_checksum(payload, payload_len) != hdr->checksum) return false;

        userdb_entry_v6_t *entries = (userdb_entry_v6_t*)payload;
        for (u32 i = 0; i < hdr->count && i < MAX_USERS; i++) {
            user_rec_t *u = &users[user_count++];
            u->uid = entries[i].uid;
            u->gid = entries[i].gid;
            u->hash_algo = entries[i].hash_algo;
            u->must_change_password = entries[i].must_change ? true : false;
            kmemcpy(u->pass_hash, entries[i].pass_hash, USER_PASS_HASH_LEN);
            kmemcpy(u->salt, entries[i].salt, USER_SALT_LEN);
            u->active = entries[i].active ? true : false;
            u->is_root = entries[i].is_root ? true : false;
            u->theme_pref = entries[i].theme_pref;
            u->font_pref = entries[i].font_pref;
            u->wallpaper_pref = entries[i].wallpaper_pref;
            u->mouse_pref = entries[i].mouse_pref;
            u->clock24_pref = entries[i].clock24_pref;
            u->taskbar_pref = entries[i].taskbar_pref;
            u->last_login_year = entries[i].last_login_year;
            u->last_login_month = entries[i].last_login_month;
            u->last_login_day = entries[i].last_login_day;
            u->last_login_hour = entries[i].last_login_hour;
            u->last_login_minute = entries[i].last_login_minute;

            kstrncpy(u->name, entries[i].name, sizeof(u->name) - 1);
            u->name[sizeof(u->name) - 1] = '\0';
            kstrncpy(u->home, entries[i].home, sizeof(u->home) - 1);
            u->home[sizeof(u->home) - 1] = '\0';
            kstrncpy(u->shell, entries[i].shell, sizeof(u->shell) - 1);
            u->shell[sizeof(u->shell) - 1] = '\0';
            users_sanitize_profile(u);
        }
        return user_count > 0;
    }
```

Then update the **existing v5 branch** (now a legacy branch): after it copies `u->font_pref = entries[i].font_pref;`, add the four new fields defaulted to unset so v5 installs upgrade cleanly:

```c
            u->wallpaper_pref = USER_PREF_UNSET;
            u->mouse_pref     = USER_PREF_UNSET;
            u->clock24_pref   = USER_PREF_UNSET;
            u->taskbar_pref   = USER_PREF_UNSET;
```

Do the same defaulting in the v4, v3, v2, v1 branches (each right after they set `font_pref`). This guarantees any older record loads with unset per-user prefs → global fallback.

In `users_persist_save()`, change the entry type and add the new fields. Replace `userdb_entry_v5_t` with `userdb_entry_v6_t` in the `max_entries`/`entries` computation (lines ~601, ~607), and in the write loop after `entries[i].font_pref = users[i].font_pref;` add:

```c
        entries[i].wallpaper_pref = users[i].wallpaper_pref;
        entries[i].mouse_pref     = users[i].mouse_pref;
        entries[i].clock24_pref   = users[i].clock24_pref;
        entries[i].taskbar_pref   = users[i].taskbar_pref;
```

And update the checksum length line to `hdr->count * (u32)sizeof(userdb_entry_v6_t)`.

- [ ] **Step 6: Implement the prefs accessors**

Add near the bottom of `kernel/users.c` (after `user_set_current_font_preference`):

```c
void user_prefs_get(u32 uid, user_prefs_t *out) {
    if (!out) return;
    user_rec_t *u = find_user_by_uid(uid);
    if (!u) {
        out->theme = out->font = out->wallpaper = USER_PREF_UNSET;
        out->mouse_sensitivity = out->clock_24h = out->taskbar_centered = USER_PREF_UNSET;
        return;
    }
    out->theme            = u->theme_pref;
    out->font             = u->font_pref;
    out->wallpaper        = u->wallpaper_pref;
    out->mouse_sensitivity= u->mouse_pref;
    out->clock_24h        = u->clock24_pref;
    out->taskbar_centered = u->taskbar_pref;
}

void user_prefs_set_current(const user_prefs_t *p) {
    if (!p) return;
    user_rec_t *u = find_user_by_uid(current_uid);
    if (!u) return;
    if (p->theme != USER_PREF_UNSET)            u->theme_pref     = p->theme;
    if (p->font != USER_PREF_UNSET)             u->font_pref      = p->font;
    if (p->wallpaper != USER_PREF_UNSET)        u->wallpaper_pref = p->wallpaper;
    if (p->mouse_sensitivity != USER_PREF_UNSET)u->mouse_pref     = p->mouse_sensitivity;
    if (p->clock_24h != USER_PREF_UNSET)        u->clock24_pref   = p->clock_24h;
    if (p->taskbar_centered != USER_PREF_UNSET) u->taskbar_pref   = p->taskbar_centered;
    users_persist_save();
}
```

- [ ] **Step 7: Write the boot-time self-test (the "failing test" first)**

Add to `kernel/users.c`:

```c
void users_selftest(void) {
    /* Round-trips per-user prefs through the live record set. Uses the seeded
     * 'user' account (uid 1000). Restores original values so boot state is
     * unchanged. */
    user_rec_t *u = find_user_by_name("user");
    if (!u) { serial_write("[selftest] users: FAIL (no 'user' account)\n"); return; }

    u32 save_uid = current_uid;
    current_uid = u->uid;
    user_prefs_t orig; user_prefs_get(u->uid, &orig);

    user_prefs_t p = { .theme = 1, .font = USER_PREF_UNSET, .wallpaper = 3,
                       .mouse_sensitivity = 150, .clock_24h = 0, .taskbar_centered = 0 };
    /* set_current persists; capture the pre-test disk state is out of scope for
     * a hobby self-test, so we only assert the in-RAM round-trip here. */
    if (u->theme_pref == 1) p.theme = 0; /* ensure we actually change it */
    user_prefs_t before; user_prefs_get(u->uid, &before);

    u->theme_pref = 0; u->wallpaper_pref = 7; u->mouse_pref = 42;
    user_prefs_t got; user_prefs_get(u->uid, &got);
    bool ok = (got.theme == 0 && got.wallpaper == 7 && got.mouse_sensitivity == 42);

    /* restore */
    u->theme_pref = orig.theme; u->font_pref = orig.font;
    u->wallpaper_pref = orig.wallpaper; u->mouse_pref = orig.mouse_sensitivity;
    u->clock24_pref = orig.clock_24h; u->taskbar_pref = orig.taskbar_centered;
    current_uid = save_uid;
    (void)p; (void)before;

    serial_write(ok ? "[selftest] users_prefs: PASS\n"
                     : "[selftest] users_prefs: FAIL\n");
}
```

Call it from `kernel/kernel.c` on the line immediately after the existing `users_init();` call (find it with `grep -n "users_init()" kernel/kernel.c`):

```c
    users_selftest();
```

- [ ] **Step 8: Run the self-test — verify FAIL first, then PASS**

Before Step 6 exists this won't compile; that's expected. With all steps in place:

Run:
```bash
SCRATCH="C:/Users/mhetm/AppData/Local/Temp/claude/D--Users-mhetm-Downloads-CareOS-v9-full/d1925cd4-bf53-4c42-84a9-e25c8d21857a/scratchpad"
make && timeout 60 make run-nowindow > "$SCRATCH/serial.log" 2>&1 || true
grep -n "selftest" "$SCRATCH/serial.log"
```
Expected: `[selftest] users_prefs: PASS`. To confirm the test is real, temporarily change one asserted value (e.g. `got.wallpaper == 8`), rebuild, see `FAIL`, then revert.

- [ ] **Step 9: Verify migration (no lockout for old installs)**

Boot once with the current on-disk userdb (do **not** `make reset-disk`). Confirm the serial log still shows `[users] loaded account database from disk` (or `initialized default account database` on a fresh disk) and `[selftest] users_prefs: PASS`, and that no `WARNING: userdb region too small` line appears.

- [ ] **Step 10: Commit**

```bash
git add include/kernel.h kernel/users.c kernel/kernel.c
git commit -m "feat(users): userdb v6 with per-user prefs fields and accessors"
```

---

## Task 2: user-scoped live settings bridge (apply / capture)

**Files:**
- Modify: `include/kernel.h` (add two decls near the `settings_*` decls ~lines 628–638)
- Modify: `kernel/settings.c` (add apply/capture; route the six per-user setters)

**Interfaces:**
- Consumes: `user_prefs_t`, `USER_PREF_UNSET`, `user_prefs_set_current`, `user_current_uid` (Task 1); `user_session_active` (added here in `users.c` — see Step 1).
- Produces:
  - `void settings_apply_prefs(const user_prefs_t *p);` — applies a user's prefs to **live** `g_settings` only (font via `font_set_family`), never writing the global blob. Skips `USER_PREF_UNSET` fields (those keep the current/default live value).
  - `void settings_capture_to_current_user(void);` — snapshots the six live per-user values from `g_settings` into a `user_prefs_t` and calls `user_prefs_set_current`.
  - `bool user_session_active(void);` (in `users.c`) — true when a real (non-guest) user is logged in.

- [ ] **Step 1: Add `user_session_active()` to users.c + header**

In `kernel/users.c`, after `user_current_uid()`:

```c
bool user_session_active(void) {
    return session.logged_in && current_uid != 65534;
}
```

In `include/kernel.h`, near the other `user_*` decls add:

```c
bool user_session_active(void);
```

- [ ] **Step 2: Declare the bridge in the header**

In `include/kernel.h`, after `void settings_set_font_family(u32 index);` add:

```c
void settings_apply_prefs(const user_prefs_t *p);
void settings_capture_to_current_user(void);
```

- [ ] **Step 3: Write the self-test extension first**

Extend `users_selftest()` (or add a sibling in settings.c called from it) so it asserts apply/capture. Simplest: append to `users_selftest()` in `users.c`:

```c
    /* apply/capture round-trip via the live settings blob */
    user_rec_t *su = find_user_by_name("user");
    if (su) {
        u32 sv = current_uid; current_uid = su->uid;
        user_prefs_t ap = { .theme = 1, .font = USER_PREF_UNSET, .wallpaper = 2,
                            .mouse_sensitivity = 120, .clock_24h = 0, .taskbar_centered = 0 };
        settings_apply_prefs(&ap);
        const careos_settings_t *cs = settings_get();
        bool ok2 = (cs->theme == 1 && cs->wallpaper == 2 &&
                    cs->mouse_sensitivity == 120 && cs->clock_24h == 0 &&
                    cs->taskbar_centered == 0);
        serial_write(ok2 ? "[selftest] settings_apply: PASS\n"
                         : "[selftest] settings_apply: FAIL\n");
        current_uid = sv;
    }
```

(This changes live `g_settings`, which is fine at boot — the real login path will re-apply the actual user's prefs before the desktop draws.)

- [ ] **Step 4: Implement apply/capture in settings.c**

Add to `kernel/settings.c` (after `settings_set_font_family`):

```c
void settings_apply_prefs(const user_prefs_t *p) {
    if (!p) return;
    if (p->theme != USER_PREF_UNSET)             g_settings.theme = p->theme;
    if (p->wallpaper != USER_PREF_UNSET)         g_settings.wallpaper = p->wallpaper;
    if (p->mouse_sensitivity != USER_PREF_UNSET) g_settings.mouse_sensitivity = p->mouse_sensitivity;
    if (p->clock_24h != USER_PREF_UNSET)         g_settings.clock_24h = p->clock_24h ? 1u : 0u;
    if (p->taskbar_centered != USER_PREF_UNSET)  g_settings.taskbar_centered = p->taskbar_centered ? 1u : 0u;
    settings_clamp();
    if (p->font != USER_PREF_UNSET && p->font < font_registry_count()) {
        g_settings.font_family = p->font;
        font_set_family(p->font);
    }
    /* NOTE: no settings_save() — per-user prefs must not touch the global blob.
     * The live theme switch (g_theme) is done by the GUI session path via
     * theme_switch(), mirroring gui.c's boot-time theme selection. */
}

void settings_capture_to_current_user(void) {
    user_prefs_t p;
    p.theme            = g_settings.theme;
    p.font             = g_settings.font_family;
    p.wallpaper        = g_settings.wallpaper;
    p.mouse_sensitivity= g_settings.mouse_sensitivity;
    p.clock_24h        = g_settings.clock_24h;
    p.taskbar_centered = g_settings.taskbar_centered;
    user_prefs_set_current(&p);
}
```

- [ ] **Step 5: Route the six per-user setters to capture-when-logged-in**

For each of `settings_set_theme`, `settings_set_mouse_sensitivity`, `settings_set_clock_24h`, `settings_set_wallpaper`, `settings_set_taskbar_centered`, and `settings_set_font_family`, replace the trailing `settings_save();` with:

```c
    if (user_session_active()) settings_capture_to_current_user();
    else settings_save();
```

Leave `settings_set_boot_fast`, `settings_set_wifi_profile`, and `settings_set_vesa_mode` calling `settings_save()` unchanged — those stay global. (For `settings_set_font_family`, keep the existing `font_set_family(index);` call before this block.)

- [ ] **Step 6: Remove the global-stomping login theme write**

In `kernel/users.c` `user_login()`, delete these four lines (~761–764):

```c
    if (u->theme_pref != USER_THEME_SYSTEM_DEFAULT)
        settings_set_theme(u->theme_pref);
    if (u->font_pref != USER_FONT_SYSTEM_DEFAULT)
        settings_set_font_family(u->font_pref);
```

Prefs are now applied by `session_begin()` (Task 3) via `settings_apply_prefs()`, on the live-only path.

- [ ] **Step 7: Build and run the self-tests**

Run:
```bash
SCRATCH="C:/Users/mhetm/AppData/Local/Temp/claude/D--Users-mhetm-Downloads-CareOS-v9-full/d1925cd4-bf53-4c42-84a9-e25c8d21857a/scratchpad"
make && timeout 60 make run-nowindow > "$SCRATCH/serial.log" 2>&1 || true
grep -n "selftest" "$SCRATCH/serial.log"
```
Expected: both `[selftest] users_prefs: PASS` and `[selftest] settings_apply: PASS`.

- [ ] **Step 8: Commit**

```bash
git add include/kernel.h kernel/settings.c kernel/users.c
git commit -m "feat(settings): per-user apply/capture bridge; stop login stomping global theme"
```

---

## Task 3: session_begin / session_end (single login/logout funnel)

**Files:**
- Modify: `include/kernel.h` (decls)
- Modify: `kernel/users.c` (implement)
- Modify: `gui/gui.c` (call on login)

**Interfaces:**
- Consumes: `user_prefs_get`, `settings_apply_prefs`, `user_current_uid`, `user_logout` (Tasks 1–2).
- Produces:
  - `void session_begin(u32 uid);` — loads the account's prefs and applies them live (`user_prefs_get` → `settings_apply_prefs`). Safe to call right after a successful `user_login()`.
  - `void session_end(void);` — ends the session (wraps `user_logout()`), returning to the guest/greeter state.

- [ ] **Step 1: Declare in header**

In `include/kernel.h` near the `user_*` decls:

```c
void session_begin(u32 uid);
void session_end(void);
```

- [ ] **Step 2: Implement in users.c**

```c
void session_begin(u32 uid) {
    user_prefs_t p;
    user_prefs_get(uid, &p);
    settings_apply_prefs(&p);
    serial_write("[session] begin for uid, prefs applied\n");
}

void session_end(void) {
    user_logout();
    serial_write("[session] end\n");
}
```

- [ ] **Step 3: Call `session_begin` + live theme switch after login succeeds**

In `gui/gui.c` `gui_run()`, the login gate is:

```c
    if (!run_login_flow(&mouse)) {
        serial_write("  [gui_run] login flow returned failure\n");
    }
```

Replace it with a call that applies the just-logged-in user's session, then makes the theme live:

```c
    if (!run_login_flow(&mouse)) {
        serial_write("  [gui_run] login flow returned failure\n");
    }
    session_begin(user_current_uid());
    theme_switch(settings_get()->theme == 0);
```

(`theme_switch` is already declared in `gui.h`/used at gui.c line ~20; `settings_get()` is already used at the top of `gui_run`.)

- [ ] **Step 4: Build, verify serial shows the session applying**

Run:
```bash
SCRATCH="C:/Users/mhetm/AppData/Local/Temp/claude/D--Users-mhetm-Downloads-CareOS-v9-full/d1925cd4-bf53-4c42-84a9-e25c8d21857a/scratchpad"
make && timeout 75 make run-nowindow > "$SCRATCH/serial.log" 2>&1 || true
grep -n "selftest\|\[session\]\|gui_run" "$SCRATCH/serial.log"
```
Expected: self-tests still PASS and the build is clean. (Headless can't log in without a mouse; the `[session] begin` line is exercised interactively in Task 4/5. This step only guards compilation + regression.)

- [ ] **Step 5: Commit**

```bash
git add include/kernel.h kernel/users.c gui/gui.c
git commit -m "feat(session): session_begin/session_end funnel; apply prefs on login"
```

---

## Task 4: distro-style greeter (account picker)

**Files:**
- Modify: `include/kernel.h` (enumerator decls)
- Modify: `kernel/users.c` (enumerator impl)
- Modify: `gui/gui.c` (greeter list before the password step)

**Interfaces:**
- Consumes: session state from Tasks 1–3.
- Produces:
  - `u32 user_enum_count(void);` — number of active accounts.
  - `bool user_enum_at(u32 idx, u32 *uid_out, char *name_out, u32 name_cap, bool *is_root_out);` — fills the idx-th active account; returns false if `idx` out of range. Guest (uid 65534) is never enumerated.

- [ ] **Step 1: Implement the enumerator in users.c**

```c
u32 user_enum_count(void) {
    u32 n = 0;
    for (u32 i = 0; i < user_count; i++)
        if (users[i].active) n++;
    return n;
}

bool user_enum_at(u32 idx, u32 *uid_out, char *name_out, u32 name_cap, bool *is_root_out) {
    u32 seen = 0;
    for (u32 i = 0; i < user_count; i++) {
        if (!users[i].active) continue;
        if (seen == idx) {
            if (uid_out) *uid_out = users[i].uid;
            if (is_root_out) *is_root_out = users[i].is_root;
            if (name_out && name_cap) {
                kstrncpy(name_out, users[i].name, name_cap - 1);
                name_out[name_cap - 1] = '\0';
            }
            return true;
        }
        seen++;
    }
    return false;
}
```

Declare both in `include/kernel.h`.

- [ ] **Step 2: Add a greeter mode + state to the login flow**

In `gui/gui.c`, add a new login mode to the `LOGIN_MODE_*` enum (find it with `grep -n "LOGIN_MODE_SIGNIN" gui/gui.c` — it is defined near the top / in `gui.h`): add `LOGIN_MODE_PICK` as the first/greeter mode. Add to `login_state_t` a `u32 picked_uid;` field.

- [ ] **Step 3: Draw the account list**

Add a `draw_greeter(const login_state_t *s, mouse_t *mouse)` in `gui/gui.c` that renders, inside the same glass panel used by `draw_login_screen`, one clickable row per `user_enum_at(i, ...)`: a colored circle with the account's first initial + the account name. Derive the circle color from the uid (e.g. `0x4a6fff ^ (uid * 0x9E3779B1u)` masked to `0xFFFFFF`) so accounts are visually distinct. Reuse `gfx_circle_fill`, `gfx_str`, `gfx_rect_rounded` (all already used in this file). Store each row's rect in a static array for hit-testing (mirror the `layout` pattern already used by `login_make_layout`).

```c
/* Row geometry: reuse the panel from login_make_layout(); stack rows from
   l.title_y downward, ROW_H ~ 52px, full panel width minus 84px side margin. */
```

- [ ] **Step 4: Handle greeter clicks → transition to password entry**

In `run_login_flow`, when `login.mode == LOGIN_MODE_PICK`, on a left click test each account row; on a hit, set `login.picked_uid`, copy that account's name into `login.username` (so the existing `user_login(s->username, ...)` path works unchanged), set `login.mode = LOGIN_MODE_SIGNIN`, `login.field = 1` (focus password), and set a status like `"Enter password for <name>"`. In `LOGIN_MODE_SIGNIN`, add a small "Back" affordance (e.g. clicking the avatar or an added secondary button) that returns to `LOGIN_MODE_PICK`. The first render of `run_login_flow` must start in `LOGIN_MODE_PICK` when `user_enum_count() > 0`.

Update the top of `run_login_flow`:

```c
    login.mode = (user_enum_count() > 0) ? LOGIN_MODE_PICK : LOGIN_MODE_SIGNUP;
```

- [ ] **Step 5: Route drawing + keep must-change/create paths intact**

In the frame render, call `draw_greeter(&login, mouse)` when `login.mode == LOGIN_MODE_PICK`, else `draw_login_screen(&login, mouse)` as today. The `LOGIN_MODE_SIGNIN`, `LOGIN_MODE_MUST_CHANGE`, and `LOGIN_MODE_SIGNUP` behavior is unchanged — the greeter is purely an added front step that pre-fills the username.

- [ ] **Step 6: Manual QEMU verification (GUI)**

Run `make run`. Verify:
1. Boot lands on the greeter showing `root` and `user` as clickable rows with distinct avatars.
2. Click `user` → password field appears titled for that account → complete the forced password change → desktop loads.
3. Serial log (`make run` prints serial to the terminal) shows `[session] begin`.

Document the observed result in the commit message.

- [ ] **Step 7: Commit**

```bash
git add include/kernel.h kernel/users.c gui/gui.c
git commit -m "feat(gui): distro-style greeter account picker in front of login"
```

---

## Task 5: Log Out / Switch User / Lock

**Files:**
- Modify: `gui/gui.h` (session-action signal)
- Modify: `gui/launcher.c` (footer power button → session actions)
- Modify: `gui/gui.c` (main loop handles the signal by re-entering the greeter)

**Interfaces:**
- Consumes: `session_begin`, `session_end`, `run_login_flow`, `user_current_uid`, `theme_switch` (Tasks 3–4).
- Produces:
  - In `gui/gui.h`: `typedef enum { SESSION_ACT_NONE, SESSION_ACT_LOCK, SESSION_ACT_LOGOUT } session_action_t;` and `extern volatile session_action_t g_session_action;`

- [ ] **Step 1: Declare the signal**

In `gui/gui.h` add the enum + extern above. In `gui/gui.c`, define it near the other file-scope globals (e.g. by `g_last_activity_tick`):

```c
volatile session_action_t g_session_action = SESSION_ACT_NONE;
```

- [ ] **Step 2: Turn the launcher footer button into a session menu**

In `gui/launcher.c` `launcher_render()` footer (lines ~167–172), replace the single power button with three small labeled buttons — `Lock`, `Log Out`, `Switch` — laid out right-to-left from `px + pw - 20`. In `launcher_handle_mouse()`, hit-test them (compute the same rects, as the file already recomputes geometry in the `draw==false` pass) and on click set the global and close the launcher:

```c
    /* Lock */    g_session_action = SESSION_ACT_LOCK;   launcher_open = false;
    /* Log Out */ g_session_action = SESSION_ACT_LOGOUT; launcher_open = false;
    /* Switch */  g_session_action = SESSION_ACT_LOGOUT; launcher_open = false;
```

(Log Out and Switch User both return to the greeter; the greeter *is* the switch-user surface. Keeping two labels is a UX affordance, one code path.) Add `#include` of the header that declares `g_session_action` if not already visible (launcher.c already includes `gui.h`).

- [ ] **Step 3: Extract a re-enterable "enter desktop" helper in gui.c**

Refactor the post-login desktop setup in `gui_run()` (the block from `rc_care_run_startup();` through `desktop_fade_in(&mouse);`, lines ~804–814) into a helper so it can run again after a switch:

```c
static void enter_desktop(mouse_t *mouse) {
    rc_care_run_startup();
    i32 sw = (i32)SCREEN_W, sh = (i32)SCREEN_H;
    i32 tw = sw * 62 / 100, th = sh * 66 / 100;
    if (!wm_find_app(APP_TERMINAL))
        wm_open(APP_TERMINAL, "Terminal", (sw - tw) / 2, (sh - th) / 2 - 24, tw, th);
    notify_push("CareOS", "Desktop ready.", COL_PRIMARY);
    speaker_startup();
    desktop_fade_in(mouse);
}
```

Call `enter_desktop(&mouse)` where that block used to be (after `session_begin` + `theme_switch` from Task 3).

- [ ] **Step 4: Handle the signal in the main loop**

At the top of the `while (1)` main loop body in `gui_run()` (line ~826), add:

```c
        if (g_session_action != SESSION_ACT_NONE) {
            session_action_t act = g_session_action;
            g_session_action = SESSION_ACT_NONE;
            launcher_open = false;
            if (act == SESSION_ACT_LOGOUT) {
                wm_close_all();          /* see Step 5 */
                session_end();
            }
            /* LOCK keeps the session; LOGOUT dropped it above. Either way we
               show the greeter/login and re-apply the resulting session. */
            run_login_flow(&mouse);
            session_begin(user_current_uid());
            theme_switch(settings_get()->theme == 0);
            enter_desktop(&mouse);
            g_last_activity_tick = timer_get_ticks();
            needs_redraw = true;
            continue;
        }
```

- [ ] **Step 5: Add `wm_close_all()` (close all windows on logout)**

Check whether `wm_close_all()` exists: `grep -n "wm_close_all\|void wm_close" gui/wm.c gui/gui.h`. If absent, add to `gui/wm.c` a function that iterates the window table and closes each open window (mirror the existing single `wm_close()`), and declare it in `gui/gui.h`. On **Lock** do NOT close windows (the same user returns to their desktop); on **Log Out/Switch** close them so the next user starts clean.

- [ ] **Step 6: Manual QEMU verification (the acceptance test)**

Run `make run` and perform the spec's acceptance script:
1. Boot → greeter → log in as `user`, complete forced password change.
2. Open Settings → set a distinct theme + wallpaper + clock format. Confirm they apply live.
3. Open the launcher → **Switch** → greeter appears → (as `root`, first complete its forced password change) → set root a *different* theme + wallpaper.
4. Launcher → **Switch** → back to `user` → confirm `user`'s theme/wallpaper/clock are intact (not root's).
5. Launcher → **Lock** → confirm the same session's desktop returns (windows still open) after re-entering the password.
6. Reboot (`Ctrl+C` the QEMU, `make run` again) → log in as each → confirm each still has their own look, and that global settings (screen resolution) are shared.

- [ ] **Step 7: Commit**

```bash
git add gui/gui.h gui/gui.c gui/launcher.c gui/wm.c
git commit -m "feat(gui): Log Out / Switch User / Lock via greeter re-entry"
```

---

## Self-Review

**Spec coverage:**
- Per-user theme/font/wallpaper/mouse/clock/taskbar → Task 1 (storage) + Task 2 (apply/capture) + routed setters. ✓
- userdb v5→v6 migration, no data loss → Task 1 Steps 3,5,9. ✓
- Apply-on-login / capture-on-change, no global stomp → Task 2 (Steps 4–6) + Task 3. ✓
- Distro greeter (click account → password) → Task 4. ✓
- Log Out / Switch User / Lock via one session funnel → Task 3 (`session_begin/end`) + Task 5. ✓
- Guest not shown in greeter → Task 4 enumerator excludes uid 65534. ✓
- Deleting logged-in user drops to greeter → existing `user_delete` resets to guest; greeter shows on next `run_login_flow`. ✓ (no new code needed)
- Serial-logged assertions + manual acceptance script → self-test (Tasks 1–2) + manual checks (Tasks 4–5). ✓
- Global settings stay shared (resolution/wifi/boot-fast) → Task 2 Step 5 leaves those on `settings_save()`. ✓

**Placeholder scan:** No TBD/TODO; every code step has concrete code. GUI drawing (Task 4 Step 3, Task 5 Step 2) specifies exact geometry anchors and the existing primitives to reuse rather than pixel-perfect literals, because those must be tuned against the live font metrics — acceptable and explicitly bounded.

**Type consistency:** `user_prefs_t` field names (`theme`, `font`, `wallpaper`, `mouse_sensitivity`, `clock_24h`, `taskbar_centered`) are used identically in Tasks 1–3. Record fields (`theme_pref`, `font_pref`, `wallpaper_pref`, `mouse_pref`, `clock24_pref`, `taskbar_pref`) are consistent between `user_t`, `user_rec_t`, and `userdb_entry_v6_t`. `session_begin`/`session_end`, `settings_apply_prefs`/`settings_capture_to_current_user`, `user_session_active`, `user_enum_count`/`user_enum_at`, `g_session_action`/`session_action_t` are each declared once and used consistently.
