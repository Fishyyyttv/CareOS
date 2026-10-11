# CareOS v10 — Phase 4 progress: Pip

Branch `feat/pip` (worktree `wt/pip`, cut from `aaa0583` on `feat/web-engine`).
Spec: `docs/superpowers/specs/2026-10-04-careos-pip-design.md`.
Plan: `docs/superpowers/plans/2026-10-04-careos-pip.md`.

## What was built

| Task | Commit | What |
|---|---|---|
| 1 | 45ffa37 | `pip_raster`: integer-only AA rasterizer (Q16 matrices, SVG path subset, non-zero fill, strokes, radial gradients, premultiplied compositing). Host-tested. |
| 2 | aaf5cab | `pip_art`: Pip from the Pet artboard: 9 moods, 8 colours, hats, outfits, extras, effects. Host-tested. |
| 3 | 376b7aa | `pip_brain`: the real pet model (happiness, energy, friendship level, age, naps, save/load). Host-tested. |
| 4 | b949232 | `pip_sprite` (LRU frame cache, 12 frames per cycle, 6 MB budget), `pip.c` session/events/draw, lock and power cameos, sign-in and logout hooks. |
| 5 | 31785cc | Pip on the desktop: perch by the dock, roam, nap, hover pill, pat, speech bubbles, right-click menu, reactions to real events. |
| 6 | b73fab7 | Pip's Home app: room, live Pip, stats, mood preview, Pat / Tuck in / Surprise me, rename, dress-up with live previews, toggles, "Let Pip out". |
| 7 | ecd3408 | Care natives `pip_mood` / `pip_say`, the Editor's "Talk to Pip" docs card, cameo QA. |
| 8 | (this commit) | Benchmark hook, benchmark, this write-up. |

Real events Pip reacts to: an unknown terminal command (confused), an Editor run
(excited / sad), a window opening or a browser page loading (thinking), an error
notification (confused), pats, and a script calling `pip_mood` / `pip_say`.

## Deviations from the plan (all in the ledger as rulings)

- **Worktree.** Another session was editing the main checkout, so Phase 4 ran
  on `feat/pip` in `wt/pip`. Merging needs coordination (see below).
- **`APP_PIP`** was added in Task 5, not Task 6, because the desktop menu's
  "Dress up…" needs it.
- **Launcher category:** Games. The plan said "Fun", but no Fun category exists.
- **Pip's Home layout**, corrected from QA shots: the big Pip now stands above
  the stats panel instead of behind it, the actions sit on their own row, and
  the dress-up tiles shrink so no option is ever dropped.
- **Care call statements.** `f(...);` on its own line was skipped as an
  "unknown expression statement", so `sys_alert("hi");` did nothing outside an
  expression. That bug predates Pip and is also in main. It is fixed here,
  because the "Talk to Pip" card would otherwise teach code that silently does
  nothing.
- **The Editor's run reaction** steps aside when the script spoke to Pip itself.
- **Perch** lowered so his feet sit just above the screen edge.
- A raw NUL byte in `apps/app_browser.c`, left over from Phase 3, was replaced with `'\0'`.
- **Final review fixes:** Pip walks on a fixed 12 Hz clock, only walks to
  windows on the current workspace (and only counts those when he says how many
  are open), and his right-click menu is kept on screen.

## Performance

VirtualBox `CareOS-perf`, 10 s compositor benchmark. Under
`CAREOS_PERF_BENCH_AT_BOOT`, Pip starts roaming at sign-in and keeps going for
the whole run. The floor is fps ≥ 30 and p95 ≤ 33 ms. The host was busy, so the
runs alternate between a baseline build with Pip hidden and the real build, to
spread the noise evenly (`qa/v10-shell/build-pip-ab.sh` + `pip-ab.ps1`):

| Run | fps | p95 | worst | frames > 33 ms | |
|---|---|---|---|---|---|
| Pip hidden, round 1 | 38 | 45 ms | 69 ms | 59 | FAIL |
| **Pip roaming, round 1** | 38 | **31 ms** | 66 ms | 18 | **PASS** |
| Pip hidden, round 2 | 39 | 41 ms | 99 ms | 41 | FAIL |
| **Pip roaming, round 2** | 36 | **26 ms** | 97 ms | 10 | **PASS** |

Pip adds no measurable frame cost: the sprite cache serves his frames, and only
his own rectangles are damaged. The Pip-hidden baseline failing on this host is
not caused by Pip. Phase 1 measured 43 fps / p95 28 ms on a quieter host. A
plausible but unconfirmed reason the Pip build does better: his 12 Hz redraw
keeps the GUI loop from idling, which reduces wake-up jitter. Worth a look in
Phase 5 (motion).

The final review also put Pip's walking on a fixed 12 Hz clock. He used to step
once per drawn frame, so he walked about three times faster whenever the desktop
was busy.

## QA screenshots (`qa/v10-shell/`)

- `pipsheet-01-desktop-dark.png`: all nine moods, matching PetMoods.
- `pd-hover.png`, `pd-pat.png`, `pd-menu.png`: hover pill, pat (hearts and bubble), right-click menu.
- `pd2-play.png`, `pd2-confused.png`, `pd2-excited.png`: roaming, an unknown command, an Editor run.
- `ph-home.png`, `ph2-dressed.png`, `ph3-named.png`, `ph3-colors.png`: Pip's Home (crown everywhere, rename, colours).
- `p7-native.png`: a Care script's `pip_say` / `pip_mood`.
- `p7b-power.png`: the power-menu cameo (worried).
- `drive-pip-login.png`, `drive-pip-login-power.png`, `drive-pip-lock.png`: the normal ISO at the real login gate (no Pip before sign-in), the worried Pip in the power menu, the sleeping Pip on the lock screen.
- `p8-roam2.png`: roaming on the fixed clock.

## Open items

- **DOOM-hide check:** pending. DOOM panics in the tree this branch was cut
  from (the formatter lacked `%i`). The fix landed on `feat/web-engine` in
  `de55854`, so check that Pip hides behind full-screen DOOM after the merge.
- **Merge overlap** with `feat/web-engine`: Makefile, `gui/gui.h`, `gui/gui.c`,
  `gui/wm.c`, `include/kernel.h`, `kernel/care_lang.c`, `apps/app_browser.c`.

## Assistant pass (2026-10-11): Pip as the desktop AI helper

Pip is now an assistant as well as a pet. All real: every action goes through an
existing OS API, and anything CareOS can't do, he says so.

- **`gui/pip/pip_assist.c`** (pure, host-tested, `test_pip_assist`): free text to one
  intent. This is the swap point for a smarter backend later; it only has to fill
  `pip_parsed_t`.
- **`gui/pip/pip_chat.c`**: session history (40 messages), a short "typing" delay,
  and the actions: open/focus any app, jump to a Settings page, find files (walks
  the VFS), time/date/memory/uptime/user/system/network/windows/packages, dark/light/auto,
  Do Not Disturb, lock, minimize all, close all (asks first), pat/play/nap/come back,
  hide himself, turn himself off (asks first), tips on/off, and a how-to knowledge base
  (packages, Terminal, Care, shortcuts, workspaces, snapping, accounts, ...).
  He refuses deleting files and power actions, and says why. Weather, battery, music
  and web search answer "CareOS doesn't have that yet".
- **Pip's Home** has a Chat tab (conversation, quick-action chips, input) next to Dress up.
  Clicking Pip on the desktop opens it; the right-click menu gained "Chat with me".
- **Settings > Pip**: Enable Pip, Show on Desktop, Animations, Tips and Suggestions,
  Open Home, Reset Pip (click twice). Saved per user in `~/.pip` (new keys on, show,
  anim, tips, intro); guests get a note that nothing is saved.
- **Off means off**: disabled Pip draws nothing, takes no events, shows no tips, and the
  GUI loop stops waking for him (`pip_desktop_frame_ms()` returns 0).
- **Animations**: slide in at sign-in and when re-enabled, slide out when hidden; new
  moods for greeting, opening/closing Home, thinking, acting, success and "oops".
  With Animations off he stands still in a calm pose and never wanders.
- **Tips**: at most one every four minutes, only after 25 s of no input, never during
  Do Not Disturb, a maximized window, or a fullscreen game; contextual to the focused app.
- Verified: `make host-test` passes (new `pip_assist` test, extended `pip_brain`);
  full build clean; QEMU QA (`qa/v10-shell/pip-chat-qa*.sh`) shows chat replies, real
  Settings navigation, the close-all confirmation, the unknown-command reply, the Settings
  > Pip page, toggles hiding him, and the turned-off Home.
