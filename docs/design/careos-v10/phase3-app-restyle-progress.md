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
| 3 | Files | **done** |
| 4 | Notes | **done** |
| 5 | Editor | next |
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

## 3. Files

- Per-window state (new generic `window_t.app_state`, freed on close).
  The hit list per window means two Files windows never cross clicks.
- **Sidebar**: Favorites (Home, Desktop, Documents, Downloads, Pictures,
  Projects), shown only when the folder exists, with coloured glyphs; the
  heading hides when there are none. Locations: CareOS disk with a real
  "X free of Y" bar (ext2), or "In memory (no disk mounted)".
- **Toolbar in the title bar**: back / forward (8-deep path history), a
  breadcrumb (home shows as your name, last segment in Bricolage, trailing
  segments when it doesn't fit), "Search <folder>" (filters this folder;
  Ctrl+F, Esc clears), grid / list toggle, **New** menu (folder / file).
- **Grid**: 132 px tiles; folders in a coloured shape (favorite colour or
  a name hash), files as paper tiles tinted by type with the extension
  label. Selected tiles get a care-soft fill and a 2 px care border.
  **List**: name, kind, size (item count for folders).
- **Preview pane** (300 px, hidden below ~1000 px window width): the first
  lines of text files in Mono with Care / C syntax colours
  (`apps/care_syntax.c`, host-tested, shared with the Editor next); an
  image thumbnail via `image_load` (cached per selection); a big folder for
  folders. Then name, kind - size, Location, Opens with, **Open**, and
  **Run in Terminal** for `.cl` (types `care <path>` into a terminal
  through the new `app_terminal_run`).
- Rename / New use a small modal sheet (Enter / Esc / buttons) instead of
  the old input strip. Empty and no-match states say what to do.
- Keyboard: arrows move in the grid, Enter opens, Backspace goes up,
  Del / d deletes, r renames, n / m new file / folder, g toggles view,
  Ctrl+C / X / V as before.

### Deviations
- No "Modified" date: the VFS keeps no timestamps.
- No Trash location: there is no trash; delete is immediate, as before.
- "Projects is user-addable" isn't done: there is no favorites store yet.
- Images open nowhere (there's no viewer), so they show Preview only and
  Open says so.

## 4. Notes

- Rebuilt around `~/Notes/` (created on the first note; an old
  `~/notes.txt` is copied in as `Notes.md` then). Notes are plain `.md`
  files, and subfolders of `~/Notes` are the folders.
- **Folders pane**: All notes, each folder and Pinned, with real counts, plus
  a dashed **New folder** that becomes a name field.
- **List pane**: search (title and first line), the honey **+** button, and
  cards with the title and first line. Pinned cards show a heart.
  Empty / no-match / nothing-pinned states say what to do.
- **Editor**: a live preview. Every line is drawn styled (Display 28 title,
  `##` headings, **checkboxes** that toggle on click and strike through
  when done, quote callouts, bullets, **bold**; italic shows in ink-2),
  except the caret line, which shows its Markdown source. The toolbar has
  B / I / H / checklist (B / I insert the markers; H and the checklist
  toggle the line prefix), Pin and Delete (a second click within 3 s
  confirms). Enter continues a checklist, bullet or quote, and ends it on an
  empty item. Arrows, Home / End and PgUp / PgDn work. Ctrl+N new, Ctrl+F
  search, Ctrl+S save, Ctrl+B bold.
- Saving is automatic: 2 s after the last edit, on switching notes, and on
  close. The file is renamed after the title when that name is free.
  Pinned notes are listed in `~/Notes/.pinned`.
- `apps/notes_md.c` (host-tested, including the spec's checklist
  round-trip): line kinds, checklist toggle, prefix toggle, title /
  snippet, file names, inline spans.

### Deviations
- No dates (the canvas "Today", "Saturday, October 3 · 9:28 AM"): the VFS
  keeps no timestamps.
- No insert-image button: there is no picker, and no image blocks in notes.
- Italic is drawn in ink-2, because there is no italic font face.
- Clicking puts the caret at the start or end of a line, not at the exact
  character.
