# Per-User Identity Foundation — Design Spec

Date: 2026-07-26
Status: Approved for planning
Target: CareOS bare-metal x86_64 kernel (NOT the Arch/KDE distro)

## Context

This is sub-project #1 of a larger effort. The full ask was decomposed into four
independent sub-projects, to be built in order:

1. **Per-user identity foundation** (this spec) — real per-user accounts,
   profiles, and sessions.
2. First-run OOBE wizard (timezone → wifi → create account) — depends on #1.
3. Admin/root console (all users + live system health) — depends on #1.
4. Real HTTPS browsing (wire `net/tls.c` into `app_browser.c` + cert
   verification + better rendering) — independent.

Each sub-project gets its own spec → plan → implementation cycle. This spec
covers **#1 only**.

## Summary

Today CareOS has a capable `kernel/users.c` (UIDs/GIDs, PBKDF2-HMAC-SHA256
hashing, salt, lockout, `must_change_password`, per-user `theme_pref` and
`font_pref` that already persist) but two limitations that stop it from feeling
like a real multi-user system:

- **Single global session.** One `session_t` struct; login writes the user's
  theme into the *global* settings store, so logging in as another user stomps
  the previous user's look-and-feel.
- **Most personalization is global.** Wallpaper, mouse sensitivity, clock
  12/24h, and taskbar layout live in `kernel/settings.c` as system-wide values,
  not per-user.

This sub-project makes each user a **real personal account**: their own
look-and-feel and home directory that follow their login, a **distro-style
greeter** to pick an account, and clean **Log Out / Switch User / Lock**. One
user is active at a time (single framebuffer, single-screen OS).

## Scope

### Becomes per-user
Loaded on login, saved back only when *that* user changes them:
- theme
- font
- wallpaper
- mouse sensitivity
- clock 12/24h
- taskbar layout (centered vs left)

(`theme_pref` and `font_pref` already exist per-user in the user record; this
generalizes that pattern to all six settings.)

### Stays global / admin-only
- screen resolution (VESA mode)
- wifi profile
- boot-fast
- timezone

### Explicitly out of scope for this pass
- **Concurrent sessions** — genuinely keeping multiple users logged in and
  hot-swapping with apps still running. Very large effort on a single-framebuffer
  OS with a 32-task scheduler; high risk. One active session at a time.
- **Draggable / per-user desktop icon arrangement** — the "desktop icons" are
  actually an auto-laid-out sidebar launcher (`gui/wm.c` `layout_icons()`
  computes their positions from a fixed list); they are not free-floating
  draggable icons. Per-user icon arrangement would first require building
  icon-dragging, which is a separate feature. Deferred.
- The OOBE wizard (#2) and admin console (#3) — later sub-projects.

## Architecture

Three pieces, each with one job and a narrow interface.

### 1. `user_prefs_t` — the per-user profile blob

A small fixed-size struct holding the six per-user settings:

```
typedef struct {
    u32  theme_pref;      /* existing field, folded in */
    u32  font_pref;       /* existing field, folded in */
    u32  wallpaper;
    u32  mouse_sensitivity;/* percent */
    u8   clock_24h;
    u8   taskbar_centered;
    /* reserved padding for forward-compat */
} user_prefs_t;
```

Stored by **bumping the userdb on-disk format v5 → v6**, adding the four new
fields (`wallpaper`, `mouse_sensitivity`, `clock_24h`, `taskbar_centered`) to
`userdb_entry_v6_t`. This follows the exact versioned-migration + checksum
pattern `users.c` already uses (v1→v5). The userdb lives on reserved raw ATA
sectors (`users_db_lba()`), which persist reliably — unlike the ext2 home
write-path, which has a known 12 KB (12-direct-block) write cap. Per-user prefs
are tiny, so the userdb record is the robust home for them.

Interface:
- `void user_prefs_get(u32 uid, user_prefs_t *out)`
- `void user_prefs_set_current(const user_prefs_t *p)` — writes the logged-in
  user's record and persists.

### 2. User-scoped live settings layer

`kernel/settings.c` continues to hold the **system defaults** (its persisted
global blob is unchanged in role — it is now the fallback/default, plus the home
of the settings that stay global). Two new operations bridge it to per-user
prefs:

- `settings_apply_prefs(const user_prefs_t *p)` — on login, apply the user's
  prefs over the **live in-RAM settings only** (theme, font, wallpaper, mouse,
  clock, taskbar). Does **not** write the global persisted blob.
- `settings_capture_to_current_user()` — when the logged-in user changes one of
  the six per-user settings, capture the current live values into their
  `user_prefs_t` and persist to the userdb (via `user_prefs_set_current`).

The existing `settings_set_*` entry points for the six per-user settings are
routed so that, when a real user is logged in, a change is captured to that
user's record rather than written to the global blob. Changes to global settings
(resolution, wifi, boot-fast, timezone) still write the global blob as today.

**Key correctness fix:** `user_login()` today calls `settings_set_theme()` /
`settings_set_font_family()` directly, which persist globally. That direct call
is removed; theme/font now flow through `settings_apply_prefs()` on the
apply-in-RAM path, so login no longer stomps global or other users' state.

### 3. Greeter + session control (GUI)

- **Greeter (account picker):** replace the typed-username login in `gui/gui.c`
  with a list of accounts, each rendered as a colored circle + initial + name.
  Click an account → password field for that account → existing `user_login()`
  path (PBKDF2 verify, lockout, `must_change_password` gate all unchanged). The
  account list comes from a new read-only enumerator over `users.c`
  (`user_count_active()` / `user_name_at()` / `user_uid_at()` — no internal
  struct exposure).
- **Session control:** add **Log Out**, **Switch User**, and **Lock** to the
  start menu. All login/logout flows route through a single pair:
  - `session_begin(uid)` — called after a successful `user_login()`: loads the
    user's `user_prefs_t`, calls `settings_apply_prefs()`, ensures the home dir
    (existing code), stamps login.
  - `session_end()` — drops to the guest/logged-out state (existing
    `user_logout()`), returning to the greeter.

  This guarantees the apply/capture logic lives in exactly one place.

## Data flow — a login

```
Greeter: pick "alice" → type password
   → user_login("alice", pw)   [existing PBKDF2 verify, lockout, unchanged]
   → session_begin(alice.uid):
        user_prefs_get(alice.uid, &p)
        settings_apply_prefs(&p)   → theme/font/wallpaper/mouse/clock/taskbar
                                     go live in RAM (no global write)
        ensure /home/alice         [existing code]
        stamp last_login           [existing code]
   → desktop draws with ALICE's look

Alice changes wallpaper in Settings
   → settings_set_wallpaper(...) → (user logged in) settings_capture_to_current_user()
   → alice's userdb record updated + persisted

Start menu → Switch User
   → session_end()  [existing user_logout → guest state]
   → Greeter shown → bob picks his account → session_begin(bob.uid) → bob's look
```

## Migration, safety, edge cases

- **Migration:** a `userdb_entry_v6_t` + a v6 branch in `users_persist_load()`
  are added alongside the existing v1–v5 loaders. Old installs load through the
  existing lower-version branches and are upgraded to v6 on the next save. New
  fields default sensibly for upgraded records (wallpaper/mouse/clock/taskbar =
  system default). The seeded `root` / `user` accounts and the
  `must_change_password` login gate are unchanged.
- **Guest / logged-out** stays the internal fallback state (uid 65534); it is
  **not** shown in the greeter.
- **Deleting the logged-in user** drops to the greeter (existing `user_delete`
  already resets to guest when deleting `current_uid`).
- **A user with no saved prefs** (fresh account, or upgraded record) falls back
  to system defaults via the v6 default values.
- **No new address-space or scheduler work** — this is an identity/settings/GUI
  change only; it does not touch paging, the scheduler, or ring-3 isolation.

## Testing

Verification for this OS is "build → boot in QEMU → read serial log" (no
host-side unit framework exists). Accordingly:

- **Serial-logged assertions** at each session step: prefs loaded on login,
  applied to live settings, captured on change, and userdb migrated v5→v6.
- **Manual acceptance script** (documented in the plan):
  1. Boot fresh; complete `must_change_password` for `user`.
  2. As `root`, create a second account (e.g. `alice`).
  3. Log in as `user`; set a distinct theme + wallpaper + clock format.
  4. Switch User → `alice`; set a *different* theme + wallpaper.
  5. Switch back to `user`; confirm `user`'s look is intact (not alice's).
  6. Reboot; log in as each; confirm each still has their own look.
  7. Confirm global settings (resolution) are shared across both.

## Success criteria

- Two users can each set their own theme, font, wallpaper, mouse sensitivity,
  clock format, and taskbar layout, and those follow their login across Switch
  User and across reboot, with neither stomping the other.
- The greeter lists existing accounts and logs in by click-then-password.
- Start menu offers Log Out, Switch User, and Lock, all routed through
  `session_begin` / `session_end`.
- Existing installs (userdb v1–v5) upgrade to v6 with no data loss and no
  lockout.
- Global settings remain shared; per-user settings do not leak into the global
  blob.
