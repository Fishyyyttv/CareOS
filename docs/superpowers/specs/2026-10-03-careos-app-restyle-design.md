# Phase 3 — App Restyle Design

**Date:** 2026-10-03 · Part of the v10 roadmap (`2026-10-03-careos-v10-redesign-roadmap.md`)
**Reference artboards** (`docs/design/careos-v10/`): `Terminal`, `Files` (light), `Browser`, `Editor`, `Notes` (light), `Store` (light), `SettingsLook` (light), `SettingsNetwork` (dark).

## Goal

The seven core apps match the canvas in both themes. Some artboards are drawn in only one theme (marked "(light)" or "(dark)"); the other theme is derived from the phase 1 tokens. Everything is drawn through the phase 1 kit and window chrome (title-bar toolbar hook, traffic lights on the left).

## Rules
- **Real data or hide.** Example content in the artboards ("Fishbowl 5G", "fishfish", "3 updates", file lists, notes, popular packages) is replaced by real data. Where the data doesn't exist, the element is hidden or shows an honest empty state with a next step.
- App behaviour, keyboard shortcuts and file formats are kept. Changes are presentation, plus the small features listed below.
- Responsive rules from perf Task 18 (per-app minimum sizes, collapse rules) still apply. Sidebars collapse below the existing breakpoints.

## Files (owned per roadmap)
- app-a: `apps/app_terminal.c`, `apps/term_layout.*`, `apps/app_editor.c`, `apps/app_notes.c`
- app-b: `apps/app_files.c`, `apps/app_browser.c`, `apps/app_pkgmgr.c`, `apps/app_settings.c`

## 1. Terminal — `Terminal`
- Window body `#0D1120` @ 92 %. Title-bar toolbar:
  - session tabs as 32 px pills: the active one raised with a green dot and "~ \<cwd basename\>", the others ghost
  - "+" new tab only if the terminal supports tabs; otherwise one pill showing the cwd
  - right side: the colour scheme name ("Care Dark"), which opens a menu of the existing terminal themes
- Body: Mono 14, line height 1.7, padding 22/26.
- **Prompt as pills:**
  - user in a care pill (`on_care` ink)
  - cwd in a `#26305A` pill (`#B7C1FF` ink)
  - then the command in ink
  - Pills are drawn by the renderer from a prompt-segment structure, not escape codes. Commands that print a raw prompt keep working.
- **`help` output** = a 3-column grid of cards (`#161C33`, radius 14):
  - one per command group (Files, Network, System, CareOS, Look, Care language)
  - the title in the group colour (UI bold) and the commands in Mono 13
  - The grid is built from the real command table.
- **"Did you mean" card:** when a command or file isn't found and a close match exists (edit distance ≤ 2, or a file of that name under `~`), show a danger-soft card: "Can't find **x** here. Did you mean `y`?" with a care **Run it** button (click or Enter on an empty line runs the suggestion).
- **Tab-completion popover:** a raised popover above the cursor listing the matching commands/paths with a hint column, Tab/arrows to pick. It extends the existing completion.
- Status bar (28 px, surface_alt): shell name + version, cwd, hint chips "Ctrl+T theme" (and "Ctrl+Shift+D split" only if split exists), "UTF-8".
- Scrollbar: thin, rounded, shown on scroll.

## 2. Files — `Files` (light)
- **Sidebar** (surface_alt):
  - "Favorites": Home, Desktop, Documents, Downloads, Pictures, Projects, each with a coloured glyph tile. A favorite is shown only if the folder exists; Projects is user-addable.
  - "Locations": the primary disk with a real "X GB free of Y GB" bar (from blockdev + ext2), plus Trash.
- **Toolbar** in the title bar: back/forward, a breadcrumb of pill segments, a search field "Search in \<folder\>" (filters the current folder), view toggle (grid/list), "New" primary button (folder/file menu).
- **Grid:** 96 px tiles. Folders are drawn as a coloured folder shape (colour from a small name-hash palette, or per-favorite colour). Files are a paper tile tinted by extension (`.cl` coral, `.asm` calm, `.md` yellow, `.png` pink, `make` green, `.iso` violet, others grey) with the extension label. Selected = care-soft fill + 2 px care border.
- **Preview pane** (right, collapsible):
  - text files (`.cl`, `.md`, `.c`, `.asm`, `.txt`): first 12 lines in Mono with Care-language syntax colours for `.cl`
  - images: thumbnail via the existing decoders
  - Then: name, kind · size, Modified, Location, Opens with
  - Buttons: Open (primary) and **Run in Terminal** for `.cl`
- Status bar: "N items · M selected" + key hints.

## 3. Browser — `Browser`
- Title bar holds the **tabs** (the existing tabs from perf Task 22), styled as rounded-top tabs with favicon letters and a close button on hover, and "+".
- Toolbar row: back/forward/reload, an address field (pill, lock glyph for https, "localhost:8080"-style host display, full URL on focus), menu button.
- **New Tab page:**
  - "Where to?" (Display 44)
  - a large search field (submits to the configured search URL if one exists, otherwise treats the input as an address)
  - speed-dial tiles with a lettered coloured circle: built from **real bookmarks** (bookmarks file) plus "Add site". Empty → only "Add site".
  - "Pick up where you left off": the 2 most recent history entries, if any
  - Privacy card with copy that matches reality: "Your history stays on this computer." The tracker-blocking line is dropped, since there's no blocker.
- The page render area is unchanged (the existing renderer and cache).

## 4. Editor — `Editor`
- Title bar: project name "\<folder\> · Editor" + **Run** primary button (runs a `.cl` file through care_lang and opens the output panel).
- Left **Explorer** (surface_alt): the folder tree of the opened file's directory. "Problems" section: real care_lang parse errors for the current file ("No problems found" otherwise).
- **Tabs** for open files, with a modified dot.
- Gutter + code: Mono 14, line numbers muted, current line highlighted. Care-language syntax colours: keywords care_soft, strings success, numbers calm, comments muted, builtins `#6CB8FF`.
- Bottom **Output** panel (Output | Terminal tabs): Run results with "Finished in X s".
- Right **docs** panel (collapsible): a static Care-language quick reference (Functions, Control flow) generated from the real built-ins list. The "Talk to Pip" section is phase 4.
- Status bar: git-less, so the branch is omitted; "Care \<version\>", "Ln X, Col Y", "Spaces: 4", "UTF-8".

## 5. Notes — `Notes` (light)
- 3 panes:
  - **Folders** (surface_alt): All notes with count, user folders with counts, "Pinned", "New folder". Folders are directories under `~/Notes/`.
  - **List**: search field, then cards with title (bold), relative date + first line, pinned marker.
  - **Editor**: B / I / H toolbar (marks stored as minimal Markdown `**`, `*`, `#`), date line, title (Display 28), body, **checklists** (`- [ ]` / `- [x]`, rendered as checkboxes, click to toggle), and quote blocks (`> `) drawn as a care-soft callout.
- Notes are plain `.md` files, so they stay compatible with Editor.

## 6. Packages (Store) — `Store` (light)
- Sidebar: search, then categories (Discover, Make & create, Developer tools, Games, Internet, Themes & icons; only categories that have packages are shown), then Installed, Updates with a real count badge.
- **Discover:**
  - hero "Editor's pick" card: the first featured package from the repository index if it marks one; otherwise the hero is hidden
  - "Popular this week" grid from the repository index (the "popular" field if present; otherwise "All packages" alphabetically), each card with icon, name, description, and a Get / Open / Update button
- An updates banner ("N updates ready · sizes" + Update all) only when carepkg reports updates.
- No repository reachable → an empty state: "Can't reach the package server. Check your connection, then try again." with Retry.

## 7. Settings — `SettingsLook` (light), `SettingsNetwork` (dark)
- **Sidebar** (surface_alt): search settings (filters sections and rows), an account card (avatar gradient, display name, Administrator/Standard), then sections with coloured glyph tiles. Selected = care-soft fill + bold.
- **Sections** (only those with real settings):

| Section | Contents |
|---|---|
| Wi-Fi & Network | Connection card (Ethernet/Wi-Fi, status, IP, router, DNS, link speed, all real from the net stack); buttons Run diagnostics (existing network test) and Renew DHCP; Known networks only with a wireless adapter; More connections: Ethernet status, Proxy if configured; the footer hint "Prefer the terminal? `network --status`" |
| Display | resolution list (phase 0 modes), scale (if present), brightness (software dim) |
| Appearance | Light / Dark / Auto preview cards; Wallpaper strip (Care Night/Day + installed wallpapers + "Your photo" → file picker); Accent swatches + label "Buttons, toggles and highlights"; **Window glass** slider (glass alpha) + "Reduce transparency" toggle; **Dock position** Bottom/Left/Right and **Hide the dock automatically** (layout support lands here; animated slide in phase 5); Font family (existing) |
| Sound | only if an audio device exists; otherwise the PC speaker toggle |
| Notifications | Do Not Disturb, toast duration, per-app mute list (apps that have posted) |
| Accounts | the existing Users functionality restyled (avatar colour, display name, password, admin) |
| Storage | per-disk usage bars (phase 0 blockdev + ext2 stats) |
| Updates | carepkg update status + Update all |
| About CareOS | phase 0 `hwinfo` restyled: CareOS logo, version, CPU, cores, RAM, disks, display, NIC |

- Bluetooth, Focus and Privacy are omitted: no backing feature exists. Focus = DND, which lives under Notifications.

## Error handling
Every load failure shows an inline card saying what happened and what to try next (the canvas voice: "If something goes wrong, the OS should say what happened and what to try next."). No silent blanks.

## Testing
- **Host tests:**
  - Care-language syntax tokenizer (Editor + Files preview)
  - "did you mean" ranking
  - completion model
  - help grid built from the command table
  - notes Markdown checklist toggle round-trip
  - Settings search filter
  - Store state selection (empty / offline / updates)
- **QEMU QA:** a screenshot of each app in both themes at 1440×900 and 1920×1080, compared with the artboards, plus each app at its minimum size (perf Task 18 layout QA mode).
- **Regression:** the existing terminal command suite, Editor save/load (no truncation, perf Task 11), browser fetch/tabs.

## Out of scope
Paint, Calc, Clock, Monitor, NetMon, Users (standalone), Help, About (standalone), games: they get the palette and chrome automatically and are restyled later. Also out of scope: Pip features in Editor/Notes, Wi-Fi driver, audio driver, split terminals.
