# Phase 1 (visual system + shell) — progress

Spec: `docs/superpowers/specs/2026-10-03-careos-visual-system-shell-design.md`.
Work landed directly on `redesign/v10` (serially, not as the planned W0 + agent
wave). QA screenshots: `D:\Users\mhetm\Downloads\CareOS_v9_full\qa\v10-shell\`.

## Done

| Spec § | What | Where |
|---|---|---|
| 1 | v10 tokens on `theme_t`; legacy `COL_*` fields remapped to them; accent list Care, Calm, Blue, Green, Purple, Red, Auto (Care default) | `gui/theme.c`, `gui/gui.h` |
| 2 | Font roles with real faces: UI = Figtree 400/700, Display = Bricolage Grotesque 700/800 (opsz = px, -0.02/-0.03 em tracking), Mono = JetBrains Mono. `gfx_str_role*`, `gfx_str_role_fit` (ellipsis) | `gui/font_roles.[ch]`, `gui/fonts/font_{figtree,bricolage}*.c`, `tools/gen-font.py` (`--axes`, `--opsz-auto`, `--tracking`) |
| 3 | All 23 AppIcon-artboard icons, rasterised at runtime (gradient tile, 28 % radius, inset highlight/shade, stroke glyph) and cached; live drop shadow | `tools/gen-ui-glyphs.py`, `gui/ui_glyph_raster.c`, `gui/ui.c` |
| 4 | Care Night / Care Day wallpaper, procedural at the exact screen size; theme switch flips it | `gui/care_wallpaper.[ch]`, `gui/icon.c` |
| 5 | UI kit `gui/ui.h` (frozen W0 API) + 30 anti-aliased system glyphs | `gui/ui.c`, `gui/ui_glyph.c` |
| 7 | Window titles in UI 13 bold (rest of the chrome landed earlier, see `window-chrome-progress.md`) | `gui/wm.c` |
| 8 | Menu bar: 40 px glass, CareOS menu (About, Settings, Lock, Log out, Restart, Shut down), focused app name, Search / Network (real link) / Sound (real mute) / Control Center / date · time | `gui/gui.c` |
| 9 | Dock: glass pill, 56 px slots, 52 px icons, coral active bar, running dots, glass hover labels; folder popover removed | `gui/taskbar.c` |
| 10 | Desktop: shortcut column (Home, Wallpapers), hero (date, greeting, search bar → launcher), widgets restyled (Clock, Calendar, System with real CPU / memory / disk) | `gui/wm.c`, `gui/widgets.c` |
| 11 | Full-screen launcher: blurred backdrop (built once per opening), search, category chips, 7-column grid, pages, keyboard nav, action tiles | `gui/launcher.c` |
| 12 | Control Center: Ethernet tile (real IP), Dark mode, Focus (DND, suppresses toasts), Lock, Screenshot (BMP to `/home/Pictures`), brightness slider, uptime, Settings / Power | `gui/gui.c` |
| 12 | Notifications: one store for toasts and the Notification Center (beside the Control Center, or alone from the clock); app icon + name, relative time, title, 2-line body, optional two actions via `notify_push_ex`; "Clear all"; DND keeps history but no toasts | `gui/notify.c` |
| 13 | Network popover under the network icon: real link state + IP, "No wireless adapter", Network settings | `gui/gui.c` |
| 6 | Glass backdrop cache (24 entries, idle-freed), half-resolution blur, rounded blit (no corner bleed), slow-frame guard (3 frames > 33 ms → tint-only until 1 s ≤ 20 ms, logged on serial), `ui_set_reduce_transparency` | `gui/glass.[ch]`, `gui/ui.c` |
| 10/11 | Ctrl+Space toggles the launcher | `gui/gui.c` |

Real data or hide: removed the fake "CareOS-5G" Wi-Fi, Bluetooth, volume
slider, battery icon and the three invented starter notifications. Trash and
Care-scripts shortcuts, resume chips, weather, media and battery stay hidden
until CareOS has a source for them.

## Deviations from the plans (for whoever picks up W0 / later phases)

- **Settings blob is now v6** (same layout as v5) to remap the accent index
  (old Blue default and Orange → Care) and move the shipped default photo to
  the Care wallpaper (`WALLPAPER_CARE` = 6). W0 Task 3's "settings v6" must
  become **v7**.
- The wallpaper is procedural, not QOI files from `tools/gen-wallpaper.py`.
- Icons are rasterised at runtime from the artboard definitions, not baked
  into the icon archive.
- The glass cache decides staleness from a sparse **signature** of the
  backdrop pixels (every 4th pixel of every 3rd row, plus the last row and
  column), not the spec's 64 px damage grid, which would have needed every draw
  call under a panel to report damage. A change that misses every sampled
  pixel can leave a cached blur stale until the next sampled change; under a
  12–14 px blur that is not visible. `ui_damage()` still forces a re-blur.
- The desktop layer (hero, widgets, shortcuts) is rebuilt when what it shows
  changes (minute, CPU in 5 % steps, memory, disk at most once a second;
  hover, hero visibility, theme, widget drags at once), not every 500 ms.
- The network popover shows link status instead of the artboard's toggle:
  the e1000 driver cannot be switched off. The Wi-Fi list and join dialog
  wait for a wireless driver.
- "Reduce transparency" has its kit call (`ui_set_reduce_transparency`); the
  Settings switch and its persistence land with the phase 3 Settings restyle.
- `gfx_str_ex` still uses the user's font family. Apps move to role text in
  phase 3; the Terminal/Editor must switch to `FONT_ROLE_MONO` before the
  legacy path can become Figtree.
- `SIDEBAR_W` is 0 (no app sidebar); `TOPBAR_H` 40; `TASKBAR_H` 88. Help,
  Maze, Wolf3D and Doom were added to the app registry's builtins so the
  launcher reaches them.
- `gui/gui.c` has not been split (W0 Task 2); the menu bar and Control Center
  still live there.

## Verification (2026-10-04)

- Normal and QA ISO builds clean; 20 sanitizer-enabled host tests pass
  (new: `care_wallpaper`, `font_roles`, `ui_icons`, `ui_glyph`, `glass`).
- QEMU: window-chrome regression script 8/8; the normal ISO still stops at
  the login gate. Screenshots in `qa/v10-shell/`.
- VirtualBox `CareOS-perf` VM, 10 s compositor benchmark: **43 fps, p95
  28 ms, PASS** (pre-redesign baseline 41 fps, p95 27 ms). Before the
  desktop-layer change it was 42 fps, p95 34 ms, FAIL.
- Under QEMU TCG frames run over 33 ms, so the guard puts glass in tint-only
  mode there; blur is visible on VirtualBox.

## Left for later phases

- Window body glass (`window_body` token) and app interiors (phase 3)
- Settings › Appearance: wallpaper picker restyle, Reduce transparency, glass
  strength (phase 3)
- Spring motion, toast glide, dock magnification spring (phase 5)
