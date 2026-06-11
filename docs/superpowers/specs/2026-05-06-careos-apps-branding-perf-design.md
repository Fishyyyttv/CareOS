# CareOS Apps, Branding & Performance Design

**Date:** 2026-05-06
**Goal:** Add CareScript Studio (CL IDE), integrate heart-C logo across the OS, apply targeted performance tweaks, and strengthen CareOS identity so the distro feels native rather than KDE-skinned.

## Scope

Four deliverables, all under the existing `iso/airootfs/` build profile:

1. **CareScript Studio** — PyQt6 GUI IDE for the Care Language
2. **Heart-C Logo Integration** — replace all SVG/icon placeholders with the Gemini-generated heart-C logotype
3. **Performance Tweaks** — zram, disable Baloo, trim autostart
4. **CareOS Identity** — unified `Categories=CareOS;` grouping, updated `careos-info`

---

## 1. CareScript Studio

### Location
`iso/airootfs/usr/local/bin/carescript-studio` (Python, executable)

### Dependencies added to `packages.x86_64`
- `python-pyqt6`

### UI Layout
```
┌─────────────────────────────────────────────────────┐
│  [New] [Open] [Save]  ──────────────  [▶ Run]        │  toolbar
├─────────────────────────────────────────────────────┤
│                                                     │
│   code editor (QPlainTextEdit + QSyntaxHighlighter) │  ~70% height
│                                                     │
├─────────────────────────────────────────────────────┤
│  Output ▾                                           │
│  > Hello from CareOS!                               │  ~30% height
└─────────────────────────────────────────────────────┘
```

### Syntax Highlighting (QSyntaxHighlighter)
| Token class | Color |
|---|---|
| Keywords (`var if else while func return print`) | `#559aff` (Primary) |
| Native fns (`sys_alert sys_window sys_beep sys_exec len str num`) | `#82bcff` (Accent) |
| Strings (`"..."`) | `#2ecc8e` (Success) |
| Numbers | `#eaf0ff` (Text, italic) |
| Comments (`//`, `#`) | `#8a99ba` (Dim) |
| Operators (`= + - * / == != < > <= >=`) | `#f56060` (Error/red accent) |

### Run behavior
- Saves current buffer to a temp file (`/tmp/carescript_run.cl`)
- Spawns `cl /tmp/carescript_run.cl` via `QProcess`
- stdout → output panel (success style: `#2ecc8e`)
- stderr → output panel (error style: `#f56060`)
- Output panel cleared on each run

### Color palette (matches CareOS spec)
- Window background: `#080d17`
- Editor background: `#0f1726`
- Text: `#eaf0ff`
- Border: `#2c3c58`
- Toolbar background: `#0f1726`

### Desktop integration
- `iso/airootfs/usr/share/applications/carescript-studio.desktop`
  - `Name=CareScript Studio`
  - `Icon=careos-carescript`
  - `Exec=carescript-studio %f`
  - `MimeType=text/x-care-language`
  - `Categories=CareOS;Development;`
- Icon: `iso/airootfs/usr/share/icons/hicolor/scalable/apps/careos-carescript.svg` (heart-C in `#559aff`)
- Default handler for `.cl` files (via existing `care-language.xml` MIME registration)

---

## 2. Heart-C Logo Integration

The "Interlocking Heart-C Logotype" from the Gemini asset sheet is the primary CareOS mark.

### Files to replace/create
| Path | Purpose |
|---|---|
| `iso/airootfs/usr/share/icons/hicolor/scalable/apps/distributor-logo-careos.svg` | System-wide distro icon |
| `iso/airootfs/etc/calamares/branding/careos/logo.svg` | Installer logo |
| `iso/airootfs/usr/share/plasma/look-and-feel/org.careos.desktop/contents/splash/Splash.qml` | Boot splash (reference updated SVG) |
| `iso/airootfs/usr/share/sddm/themes/careos/Main.qml` | Login screen logo reference |

### SVG spec for heart-C mark
- Canvas: 128×128 viewBox
- Shape: interlocking C letterform with heart negative space, stroke `#559aff`, fill `#0f1726`
- Exported as clean SVG (no raster embeds) so it scales to any size
- All four logo variants stored in `iso/airootfs/usr/share/careos/branding/` for future use

---

## 3. Performance Tweaks

### `iso/airootfs/root/customize_airootfs.sh` additions
```bash
# Enable zram swap (compresses RAM, ~30% effective memory gain on 4GB systems)
systemctl enable systemd-zram-setup@zram0.service
```

### `iso/packages.x86_64` changes
- Add: `zram-generator`
- Remove: `tracker` `tracker-miners` (GNOME indexer, pulled in transitively by some KDE apps)

### `iso/airootfs/etc/skel/.config/` additions
- `baloofilerc` — pre-placed file disabling indexing (avoids needing kwriteconfig5 in chroot):
  ```ini
  [Basic Settings]
  Indexing-Enabled=false
  ```
- `autostart/org.kde.kdeconnect.daemon.desktop` — drop this file (empty/masked) to prevent KDE Connect from autolaunching on first login

---

## 4. CareOS Identity

### App menu grouping
All CareOS-first `.desktop` files get `Categories=CareOS;` prepended:
- `careos-install.desktop`
- `careos-control.desktop`
- `careos-welcome.desktop`
- `cl-run.desktop`
- `carescript-studio.desktop` (new)

KDE Plasma renders a "CareOS" submenu automatically when multiple apps share this category.

### `careos-info` update
`iso/airootfs/usr/local/bin/careos-info` updated to:
- Show CL version (hardcoded `v1.0` — interpreter has no --version flag)
- Show heart-C ASCII art block in `#559aff`
- List installed CareOS apps

---

## Architecture summary

No new daemons, no new services beyond `zram-generator`. Everything lands inside the existing `iso/airootfs/` overlay. Build pipeline (`iso/build.sh`) is unchanged.

**CareScript Studio** is the only net-new binary. It shells out to the existing `cl` interpreter rather than reimplementing it, so language improvements automatically flow through.
