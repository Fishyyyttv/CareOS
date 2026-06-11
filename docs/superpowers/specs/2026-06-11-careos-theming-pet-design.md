# CareOS Theming & Virtual Pet Design
**Date:** 2026-06-11  
**Target:** Arch Linux KDE Plasma ISO (`iso/` tree)  
**Status:** Approved

---

## Overview

Four parallel workstreams that make CareOS feel unmistakably itself on KDE Plasma:

1. **Bit** — a 32×32 pixel sprite desktop pet (PyQt6, always-on-screen, toggleable)
2. **Boot & Login** — animated Plymouth theme + redesigned SDDM login screen
3. **Deep System Polish** — icon theme, typography, cursor, sounds, wallpaper
4. **Custom Apps** — redesigned Welcome app + new CareOS Control Center

---

## 1. Bit — Desktop Pet

### Implementation
- **Type:** PyQt6 standalone app, transparent frameless always-on-top window
- **File:** `iso/airootfs/usr/local/bin/bit-pet` (Python script)
- **Autostart:** `iso/airootfs/etc/skel/.config/autostart/bit-pet.desktop`
- **Size:** 128×128px window (16×16 sprite rendered at 8× scale)
- **Position:** Bottom-right corner by default; draggable, position persisted to `~/.config/careos/bit.json`
- **Focus:** Never steals focus (`Qt.WindowDoesNotAcceptFocus`)
- **Dependencies:** `python-pyqt6`, `python-psutil` (both already in `iso/packages.x86_64`)

### Animation State Machine
A `QTimer` at 150ms drives the following states:

| State | Trigger | Animation |
|-------|---------|-----------|
| `idle` | Default | 2-frame float cycle; random blink every 4–8s |
| `happy` | User clicks Bit | 4-frame bounce + blue heart particles; returns to idle |
| `excited` | System event (download, notification) | 3-frame wiggle |
| `tired` | CPU >80% for 10s | Droopy eyes, slower float |
| `sleepy` | 30min no user activity | Eyes closed, Zzz speech bubble |

### Pixel Art Frames
16×16 sprites drawn in CareOS palette, rendered via `QPainter` at 8× with `SmoothPixmapTransform` disabled (crisp pixels). Each state has its own frame array defined in `FRAMES` dict at top of script.

Palette used:
- Body: `#559aff` (primary blue)
- Highlight: `#82bcff` (accent blue)
- Shadow: `#2a56b8` (deep blue)
- Eyes: `#eaf0ff` (white) + `#0f1726` (pupil)
- Blush: `#f56060`
- Sleep: `#8a99ba`

### System Monitoring
`psutil` polled every 10s via `QTimer`:
- `cpu_percent()` → triggers `tired` state if >80% sustained
- `QFileSystemWatcher` on `~/Downloads` → triggers `excited` on new file

### Care Points & Outfits
- Left-click Bit → +1 Care Point, plays `happy` animation
- Points stored in `~/.config/careos/bit.json` as `{"points": N, "outfit": "blue"}`
- Unlocks at 50-point intervals:

| Points | Outfit | Palette swap |
|--------|--------|-------------|
| 0 | Blue (default) | `#559aff` body |
| 50 | Cyan | `#38bff8` body |
| 100 | Green | `#2ecc8e` body |
| 150 | Pink | `#f56060` body |
| 200 | Gold | `#f0b430` body |

- Right-click Bit → context menu: "Care Points: N", "Outfits", "Hide Bit"

### Settings Toggle
- Stored in `~/.config/careos/settings.ini` under `[Bit]` `enabled=true`
- CareOS Control Center checkbox calls `systemctl --user enable/disable --now bit-pet`
- Systemd user service: `iso/airootfs/etc/skel/.config/systemd/user/bit-pet.service`

---

## 2. Boot & Login

### Plymouth Boot Animation
**Files:** `iso/airootfs/usr/share/plymouth/themes/careos/careos.script` (rewrite), `careos.plymouth`

Three phases using Plymouth script primitives (no bitmaps):
1. **Fade in (0–1s):** Heart-C logo built from Plymouth arcs and fill, opacity animates 0→1 with a `#559aff` glow bloom via layered translucent copies
2. **Loading (1–8s):** Thin arc progress ring orbits the heart-C, `progress` variable fills 0→1 from Plymouth's boot progress callback
3. **Done (8–9s):** Ring completes, logo brightness ramps up, full fade to black

Color: `#559aff` glow, `#eaf0ff` logo fill, `#080d17` background.

### SDDM Login Screen
**File:** `iso/airootfs/usr/share/sddm/themes/careos/Main.qml` (full rewrite)

Layout:
- Full-screen dark gradient background (`#080d17` → `#0f1726`)
- Subtle floating particle animation — small `#559aff` rectangles drifting upward, pure QML `NumberAnimation`
- Centered frosted-glass card (`Rectangle`, `opacity: 0.85`, `layer.effect: FastBlur`)
  - Heart-C SVG logo (from `usr/share/careos/branding/heart-c.svg`) at 48px
  - "CareOS" title in Inter Bold 24px
  - Username field (if multi-user) + Password field
  - Password field: `#559aff` border on focus, `border.width: 2`
  - Login button: `#559aff` fill, `#eaf0ff` text
- Top-right clock: Inter Regular 13px, `#8a99ba`
- No third-party QML imports — self-contained

---

## 3. Deep System Polish

### Icon Theme
**Path:** `iso/airootfs/usr/share/icons/CareOS/`

- `index.theme`: `Inherits=Papirus,hicolor`; `papirus-icon-theme` added to `packages.x86_64`
- Overrides for CareOS-specific apps: folder icons recolored to `#559aff`, heart-C mark used for `careos-*` app icons
- Key overrides: `places/` (folders), `apps/` (careos-control, careos-welcome, bit-pet, carescript-studio)
- Applied in `kdeglobals`: `Theme=CareOS` under `[Icons]`

### Typography
- Add `ttf-inter` to `iso/packages.x86_64`
- `iso/airootfs/etc/skel/.config/kdeglobals`: set Inter 10pt for all KDE font roles
- `iso/airootfs/etc/skel/.config/konsolerc`: JetBrains Mono 11pt for terminal (add `ttf-jetbrains-mono`)

### Cursor
**Path:** `iso/airootfs/usr/share/icons/CareOS-cursors/`

- Standard X11 cursor theme structure (`cursors/` dir with symlinks)
- Arrow cursor: `#559aff` fill, `#eaf0ff` outline, 24px default size
- **Generation:** A `tools/gen-cursors.py` script renders cursor PNGs via `Pillow` then calls `xcursorgen` (from `xorg-xcursorgen`) to produce the binary cursor files. Only the 5 essential cursors are custom (arrow, hand2, watch, xterm, crossed-circle); all others inherit from `Adwaita`. Run once during development; committed output ships in repo.
- Applied via `~/.config/kcminputrc`: `cursorTheme=CareOS-cursors`

### System Sounds
**Path:** `iso/airootfs/usr/share/sounds/CareOS/`

Four `.ogg` clips, soft synth-pad aesthetic:
- `startup.ogg` — gentle ascending chord (~1.5s)
- `notification.ogg` — soft two-note chime (~0.5s)
- `error.ogg` — low descending tone (~0.5s)
- `logout.ogg` — soft descending chord (~1s)

**Generation:** A `tools/gen-sounds.py` helper script (uses `numpy` + `scipy.io.wavfile` → `ffmpeg` for ogg conversion) generates all four files from sine wave math. Run once during development; committed output `.ogg` files ship in the repo. `numpy`, `scipy`, `ffmpeg` are build-time only dependencies, not added to `packages.x86_64`.

KDE sound theme registered via `index.theme`. Applied in `~/.config/knotifyrc`.

### Wallpaper
**File:** `iso/airootfs/usr/share/wallpapers/CareOS/contents/images/1920x1080.svg` (upgrade)

- Deep navy gradient: `#080d17` top-left → `#0f1726` bottom-right
- Geometric grid overlay: `#559aff` lines at 5% opacity, 80px spacing
- Heart-C watermark: centered, large (400px), `#559aff` at 4% opacity
- Pure SVG, scales to any resolution

---

## 4. Custom Apps

### Welcome App
**File:** `iso/airootfs/usr/local/bin/careos-welcome` (full rewrite, PyQt6)  
**Autostart trigger:** Checks for `~/.config/careos/.welcomed`; if absent, launches on login

4-page wizard, `QStackedWidget`:

| Page | Content |
|------|---------|
| 1 | Animated heart-C logo + "Welcome to CareOS" + user's name |
| 2 | Pick Bit's starting outfit (shows sprite in each unlocked color, click to select) |
| 3 | Toggle options: enable Bit, dark/light mode, enable system sounds |
| 4 | "You're all set!" + Bit happy animation; writes `~/.config/careos/.welcomed` |

Styling: CareOS dark theme, Inter font, `#559aff` accent buttons, 600×400px fixed size window, centered on screen.

### CareOS Control Center
**File:** `iso/airootfs/usr/local/bin/careos-control` (replaces existing `carectl` shell script)  
**Type:** PyQt6, `QTabWidget`, 700×500px

| Tab | Contents |
|-----|---------|
| **Bit** | Enable/disable toggle (writes settings.ini + systemctl), Care Points display, outfit grid (locked outfits shown greyed), Reset Points button |
| **Appearance** | Dark/Light theme radio buttons, wallpaper variant picker (3 SVG variants: Navy Grid, Deep Space, Minimal Dark), accent color picker (6 swatches: Blue `#559aff`, Cyan `#38bff8`, Green `#2ecc8e`, Pink `#f56060`, Gold `#f0b430`, Purple `#a78bfa`) |
| **System** | Update button (`carepkg update`), system info (version, uptime, kernel), Open Terminal button |
| **About** | Heart-C logo, CareOS version, brief description, link to GitHub |

Desktop entry: `iso/airootfs/usr/share/applications/careos-control.desktop` (updates existing)

---

## File Change Summary

| File | Action |
|------|--------|
| `iso/airootfs/usr/local/bin/bit-pet` | **New** — PyQt6 pet app |
| `iso/airootfs/etc/skel/.config/autostart/bit-pet.desktop` | **New** — autostart entry |
| `iso/airootfs/etc/skel/.config/systemd/user/bit-pet.service` | **New** — user service |
| `iso/airootfs/usr/share/plymouth/themes/careos/careos.script` | **Rewrite** — animated boot |
| `iso/airootfs/usr/share/sddm/themes/careos/Main.qml` | **Rewrite** — login screen |
| `iso/airootfs/usr/share/icons/CareOS/` | **New** — icon theme dir |
| `iso/airootfs/usr/share/icons/CareOS-cursors/` | **New** — cursor theme |
| `iso/airootfs/usr/share/sounds/CareOS/` | **New** — sound theme |
| `iso/airootfs/usr/share/wallpapers/CareOS/contents/images/1920x1080.svg` | **Upgrade** |
| `iso/airootfs/etc/skel/.config/kdeglobals` | **Update** — font + icon theme |
| `iso/airootfs/etc/skel/.config/kcminputrc` | **Update** — cursor theme |
| `iso/airootfs/usr/local/bin/careos-welcome` | **Rewrite** — PyQt6 wizard |
| `iso/airootfs/usr/local/bin/careos-control` | **Rewrite** — PyQt6 control center |
| `iso/packages.x86_64` | **Update** — add `ttf-inter`, `ttf-jetbrains-mono`, `papirus-icon-theme` |
