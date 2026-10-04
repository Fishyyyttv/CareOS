# Phase 3 (app restyle) — progress

Spec: `docs/superpowers/specs/2026-10-03-careos-app-restyle-design.md`.
QA screenshots: `qa/v10-shell/` (`shots.py` now has a `type:<text>` step).

## Shared

- **Title-bar toolbar hook.** `app_toolbar_draw(w, bar)` / `app_toolbar_click(w, x, y)`
  (`apps/apps.c`, declared in `gui/gui.h`). The bar is the title-bar area
  right of the traffic lights. An app that draws a toolbar replaces the
  title; a click it consumes doesn't start a window drag. During open/close
  transitions the plain title is drawn.

## Order and status

| # | App | Status |
|---|---|---|
| 1 | Terminal | **done** |
| 2 | Settings | **done** |
| 3 | Files | next |
| 4 | Notes | |
| 5 | Editor | |
| 6 | Browser | |
| 7 | Packages (Store) | |

## 1. Terminal

- Body `#0D1120` at 92 %, Mono 14 with ~1.7 line height, padding 22/26
  (14/10 on small windows).
- **The prompt is drawn as pills**: the user in coral, the cwd (`~`, `~/x`
  or an absolute path) in `#26305A` / `#B7C1FF`, then the command. Rich
  lines live in the scrollback as marked lines (`TERM_MARK`, see
  `apps/term_layout.h`). They are laid out by `term_wrap_rich` as whole
  rows, so scrolling still works by rows. Ordinary output can't forge a
  marker, because `term_out` turns marker bytes into `?`.
- **`help`** is a 3/2/1-column grid of cards built from the command table
  (`apps/term_cmds.c`), with group colours from the artboard.
- **"Did you mean" card**: for a mistyped command (OSA edit distance ≤ 2, and
  smaller than the typed length) or a missing file or folder for
  `cat` / `care` / `cd`. A file is searched by name under `~`, up to 5 levels
  deep. **Run it**, or Enter on an empty line, runs the suggestion.
- **Tab completion** (new; the old terminal had none): commands and common
  flag forms (`network --status` …), plus paths for `cd ls cat rm care`. One
  match completes it; several extend the input to the common prefix and open
  a popover. Tab or arrows pick, Enter accepts, Esc closes.
- **Toolbar**: a session pill with a green dot and `~ <basename>` (no tabs
  exist, so there is no "+"), and the colour scheme on the right. It opens a
  menu of the 7 schemes; arrows and Enter work while it is open. "CareOS
  Dark" is now **Care Dark** (the canvas palette), and "Light" is **Care
  Light**.
- **Status bar** (28 px): `CareOS shell 9.0`, the cwd, and the hints
  `Tab complete` / `Ctrl+T theme`.
- **Scrollbar**: thin and rounded, shown while scrolled back or for 1.2 s
  after a scroll.
- Host tests: `test_term_cmds` covers help-grid content, distance,
  suggestion ranking and the completion model. `test_term_layout` adds the
  rich-block cases.

### Deviations (real data or hide)
- No "Ctrl+Shift+D split" hint and no split button: split panes don't exist.
- No "UTF-8" chip: the terminal fonts are ASCII only.
- No "+" new-tab button: the terminal has one session per window.
- No ghost auto-suggestion text after the cursor.
- "Done! (0.02 s)" timing isn't shown, because scripts don't report timing.

## 2. Settings

- Sidebar (surface_alt): a **search** field that filters the sections by
  title and keywords (`apps/settings_index.c`, host-tested; arrows move
  through the matches, Esc clears), an **account card** (gradient avatar,
  display name, Administrator/Standard), and sections with coloured glyph
  tiles. The selected section gets a care-soft fill and bold text.
- One hit list is built during draw and used for click and drag, so the old
  draw/click offset drift is gone. Sliders apply live while dragging and
  save once on release.
- Sections (real data only):
  - **Network**: Ethernet card (status, IP, router, DNS, link speed from
    the e1000 STATUS register). **Run diagnostics** is new and real: it
    checks the adapter, the link, the address and a DNS lookup, then says
    what to try next. Also Renew DHCP, and the `network --status` hint.
  - **Display**: resolution cards (BGA modes, or an honest "set by the
    boot loader"), Brightness (software dim), Pointer speed.
  - **Appearance**: Light / Dark / Auto cards, the wallpaper strip (Care
    plus the installed wallpapers only), accent swatches, a Window glass
    slider (scales the glass alphas; new `theme_set_glass`), Reduce
    transparency, Font.
  - **Sound**: system sounds (PC speaker) on/off.
  - **Notifications**: Do Not Disturb, plus the list size and Clear all.
  - **Date & Time** (added): current time, 24-hour clock, time zone
    stepper (fixed UTC offsets).
  - **Accounts**: profile card, display name, avatar colour, password
    with a strength meter. A guest session sees an honest note instead.
  - **Storage**: ext2 used / total / free bar, or "No disk is mounted"
    with what that means.
  - **About CareOS**: logo and version; CPU brand (CPUID, cached), RAM
    (from the memory map, new `pmm_ram_mib`), display, network adapter,
    disk, uptime; Fast boot.
- New kernel getters: `net_get_gateway`, `e1000_link_mbps`,
  `pmm_ram_mib`, `ext2_space`, `gui_set_brightness_live`,
  `theme_set_glass` (applied at session start too).

### Deviations (real data or hide)
- No Wi-Fi card or known networks: there is no wireless driver. The old
  fake "Connect Wi-Fi" profile button is gone from Settings (the terminal
  `wifi` command is untouched).
- **Dock position / autohide hidden**: the setting is stored, but the dock
  can't move yet. It returns once the dock layout supports it.
- **Updates hidden**: carepkg has no update source.
- Notifications has no toast duration or per-app mute, because neither
  exists in the store.
- The title bar is the normal window chrome; the canvas puts the traffic
  lights inside the sidebar.
- No "Your photo" wallpaper tile: there is no file picker yet.
