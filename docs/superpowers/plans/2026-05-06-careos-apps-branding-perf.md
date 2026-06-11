# CareOS Apps, Branding & Performance Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add CareScript Studio (PyQt6 CL IDE), replace all OS logos with the heart-C SVG mark, enable zram + disable Baloo, and unify CareOS app identity in the KDE app menu.

**Architecture:** All changes land inside `iso/airootfs/` (the archiso overlay) — no new build steps needed. CareScript Studio is a single-file PyQt6 script that shells out to the existing `cl` interpreter. Logo changes are SVG file replacements. Performance tweaks are config file drops + one `customize_airootfs.sh` line. Identity changes are `Categories=` edits on existing `.desktop` files.

**Tech Stack:** Python 3 + PyQt6, SVG, systemd-zram-generator, KDE Plasma `.desktop` spec

---

## File Map

| Action | Path |
|---|---|
| Create | `iso/airootfs/usr/local/bin/carescript-studio` |
| Create | `iso/airootfs/usr/share/applications/carescript-studio.desktop` |
| Create | `iso/airootfs/etc/skel/Desktop/CareScript Studio.desktop` |
| Create | `iso/airootfs/usr/share/icons/hicolor/scalable/apps/careos-carescript.svg` |
| Create | `iso/airootfs/usr/share/careos/branding/heart-c.svg` |
| Replace | `iso/airootfs/usr/share/icons/hicolor/scalable/apps/distributor-logo-careos.svg` |
| Replace | `iso/airootfs/etc/calamares/branding/careos/logo.svg` |
| Create | `iso/airootfs/etc/systemd/zram-generator.conf` |
| Create | `iso/airootfs/etc/skel/.config/baloofilerc` |
| Create | `iso/airootfs/etc/skel/.config/autostart/org.kde.kdeconnect.daemon.desktop` |
| Modify | `iso/packages.x86_64` (add `python-pyqt6`, `zram-generator`) |
| Modify | `iso/airootfs/root/customize_airootfs.sh` (chmod + zram enable) |
| Modify | `iso/airootfs/usr/share/applications/careos-install.desktop` |
| Modify | `iso/airootfs/usr/share/applications/careos-control.desktop` |
| Modify | `iso/airootfs/usr/share/applications/careos-welcome.desktop` |
| Modify | `iso/airootfs/usr/share/applications/cl-run.desktop` |
| Modify | `iso/airootfs/usr/local/bin/careos-info` |

---

## Task 1: Heart-C SVG Logo

**Files:**
- Create: `iso/airootfs/usr/share/careos/branding/heart-c.svg`
- Replace: `iso/airootfs/usr/share/icons/hicolor/scalable/apps/distributor-logo-careos.svg`
- Replace: `iso/airootfs/etc/calamares/branding/careos/logo.svg`
- Create: `iso/airootfs/usr/share/icons/hicolor/scalable/apps/careos-carescript.svg`

- [ ] **Step 1: Create the master heart-C SVG (with background card)**

Create `iso/airootfs/usr/share/careos/branding/heart-c.svg`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="256" height="256" viewBox="0 0 256 256">
  <!-- Background card matching CareOS dark surface -->
  <rect x="18" y="18" width="220" height="220" rx="42" fill="#080d17"/>
  <rect x="18" y="18" width="220" height="220" rx="42" fill="none" stroke="#2c3c58" stroke-width="3"/>
  <!-- Heart shape — CareOS primary blue -->
  <path d="M128 190 C68 158 36 118 36 84 C36 56 58 40 82 40 C100 40 116 52 128 68 C140 52 156 40 174 40 C198 40 220 56 220 84 C220 118 188 158 128 190 Z"
        fill="none" stroke="#559aff" stroke-width="11" stroke-linejoin="round"/>
  <!-- C letterform — interlocking arc, accent blue -->
  <!-- Center ~(108,128), radius 50, open gap on the right at ±50° -->
  <path d="M140 90 A50 50 0 1 0 140 166"
        fill="none" stroke="#82bcff" stroke-width="11" stroke-linecap="round"/>
</svg>
```

- [ ] **Step 2: Replace the distributor logo**

Copy the master SVG over the existing distributor logo:

```bash
cp iso/airootfs/usr/share/careos/branding/heart-c.svg \
   iso/airootfs/usr/share/icons/hicolor/scalable/apps/distributor-logo-careos.svg
```

- [ ] **Step 3: Replace the Calamares installer logo**

```bash
cp iso/airootfs/usr/share/careos/branding/heart-c.svg \
   iso/airootfs/etc/calamares/branding/careos/logo.svg
```

- [ ] **Step 4: Create the CareScript Studio app icon (transparent bg, 128×128)**

Create `iso/airootfs/usr/share/icons/hicolor/scalable/apps/careos-carescript.svg`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="128" height="128" viewBox="0 0 128 128">
  <!-- Soft dark background for app icon context -->
  <rect x="8" y="8" width="112" height="112" rx="22" fill="#0f1726"/>
  <!-- Heart (half-scale of master) -->
  <path d="M64 95 C34 79 18 59 18 42 C18 28 29 20 41 20 C50 20 58 26 64 34 C70 26 78 20 87 20 C99 20 110 28 110 42 C110 59 94 79 64 95 Z"
        fill="none" stroke="#559aff" stroke-width="6" stroke-linejoin="round"/>
  <!-- C letterform (half-scale: center ~(54,64), radius 25) -->
  <path d="M70 45 A25 25 0 1 0 70 83"
        fill="none" stroke="#82bcff" stroke-width="6" stroke-linecap="round"/>
</svg>
```

- [ ] **Step 5: Validate all SVGs**

```bash
xmllint --noout iso/airootfs/usr/share/careos/branding/heart-c.svg
xmllint --noout iso/airootfs/usr/share/icons/hicolor/scalable/apps/distributor-logo-careos.svg
xmllint --noout iso/airootfs/etc/calamares/branding/careos/logo.svg
xmllint --noout iso/airootfs/usr/share/icons/hicolor/scalable/apps/careos-carescript.svg
```

Expected: no output (all valid).

- [ ] **Step 6: Commit**

```bash
git add iso/airootfs/usr/share/careos/branding/heart-c.svg \
        iso/airootfs/usr/share/icons/hicolor/scalable/apps/distributor-logo-careos.svg \
        iso/airootfs/etc/calamares/branding/careos/logo.svg \
        iso/airootfs/usr/share/icons/hicolor/scalable/apps/careos-carescript.svg
git commit -m "brand: replace logo with heart-C SVG mark across OS"
```

---

## Task 2: Package List Updates

**Files:**
- Modify: `iso/packages.x86_64`

- [ ] **Step 1: Add PyQt6 and zram-generator to the package list**

In `iso/packages.x86_64`, find the Python section (line 169–170) and replace it:

```
# --- Python (for CL interpreter + CareOS tools) ---
python
```

Replace with:

```
# --- Python (for CL interpreter + CareOS tools) ---
python
python-pyqt6

# --- Zram (compressed RAM swap) ---
zram-generator
```

- [ ] **Step 2: Commit**

```bash
git add iso/packages.x86_64
git commit -m "pkg: add python-pyqt6 and zram-generator"
```

---

## Task 3: Performance Config Files

**Files:**
- Create: `iso/airootfs/etc/systemd/zram-generator.conf`
- Create: `iso/airootfs/etc/skel/.config/baloofilerc`
- Create: `iso/airootfs/etc/skel/.config/autostart/org.kde.kdeconnect.daemon.desktop`
- Modify: `iso/airootfs/root/customize_airootfs.sh`

- [ ] **Step 1: Create zram-generator config**

Create `iso/airootfs/etc/systemd/zram-generator.conf`:

```ini
[zram0]
zram-size = min(ram / 2, 4096)
compression-algorithm = zstd
```

This creates a zram swap device sized at half of RAM (max 4 GB), using zstd compression — gives ~30% effective memory gain with minimal CPU overhead.

- [ ] **Step 2: Pre-disable Baloo for all new users**

Create `iso/airootfs/etc/skel/.config/baloofilerc`:

```ini
[Basic Settings]
Indexing-Enabled=false
```

This lands in every user's `~/.config/baloofilerc` via skel copy and prevents the file indexer from spinning up on first login.

- [ ] **Step 3: Mask KDE Connect daemon autostart**

Create `iso/airootfs/etc/skel/.config/autostart/org.kde.kdeconnect.daemon.desktop`:

```ini
[Desktop Entry]
Hidden=true
```

An autostart file with `Hidden=true` masks the upstream KDE Connect daemon entry so it doesn't launch automatically on login.

- [ ] **Step 4: Enable zram service in customize_airootfs.sh**

In `iso/airootfs/root/customize_airootfs.sh`, find the services block:

```bash
systemctl enable NetworkManager
systemctl enable sddm
systemctl enable bluetooth
systemctl enable cups
systemctl enable power-profiles-daemon
systemctl enable fstrim.timer
```

Add one line at the end of that block:

```bash
systemctl enable NetworkManager
systemctl enable sddm
systemctl enable bluetooth
systemctl enable cups
systemctl enable power-profiles-daemon
systemctl enable fstrim.timer
systemctl enable systemd-zram-setup@zram0.service
```

- [ ] **Step 5: Commit**

```bash
git add iso/airootfs/etc/systemd/zram-generator.conf \
        iso/airootfs/etc/skel/.config/baloofilerc \
        iso/airootfs/etc/skel/.config/autostart/org.kde.kdeconnect.daemon.desktop \
        iso/airootfs/root/customize_airootfs.sh
git commit -m "perf: enable zram, disable Baloo indexer, mask KDE Connect autostart"
```

---

## Task 4: CareScript Studio Application

**Files:**
- Create: `iso/airootfs/usr/local/bin/carescript-studio`
- Modify: `iso/airootfs/root/customize_airootfs.sh`

- [ ] **Step 1: Write a smoke test before creating the app**

Save as `/tmp/test_carescript_syntax.sh` and run it after creating the app file:

```bash
#!/bin/bash
python3 -m py_compile iso/airootfs/usr/local/bin/carescript-studio && \
  echo "PASS: syntax OK" || echo "FAIL: syntax error"
```

- [ ] **Step 2: Create the CareScript Studio application**

Create `iso/airootfs/usr/local/bin/carescript-studio` with this exact content (the shebang line must be the very first line):

```python
#!/usr/bin/env python3
"""CareScript Studio — CareOS Care Language IDE."""

import sys, tempfile
from pathlib import Path

from PyQt6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout,
    QSplitter, QPlainTextEdit, QTextEdit, QToolBar,
    QFileDialog, QLabel, QStatusBar,
)
from PyQt6.QtGui import (
    QSyntaxHighlighter, QTextCharFormat, QColor, QFont,
    QKeySequence, QAction,
)
from PyQt6.QtCore import Qt, QProcess, QRegularExpression, QSize

# ── Palette ───────────────────────────────────────────────────────────────────
BG      = "#080d17"
SURFACE = "#0f1726"
PRIMARY = "#559aff"
ACCENT  = "#82bcff"
TEXT    = "#eaf0ff"
DIM     = "#8a99ba"
BORDER  = "#2c3c58"
SUCCESS = "#2ecc8e"
ERROR   = "#f56060"


# ── Syntax highlighter ────────────────────────────────────────────────────────
class CLHighlighter(QSyntaxHighlighter):
    def __init__(self, parent):
        super().__init__(parent)
        self._rules = []

        def fmt(color, italic=False, bold=False):
            f = QTextCharFormat()
            f.setForeground(QColor(color))
            if italic:
                f.setFontItalic(True)
            if bold:
                f.setFontWeight(700)
            return f

        patterns = [
            (r'\b(var|if|else|while|func|return|print)\b',            fmt(PRIMARY, bold=True)),
            (r'\b(sys_alert|sys_window|sys_beep|sys_exec|len|str|num)\b', fmt(ACCENT)),
            (r'"[^"]*"',                                               fmt(SUCCESS)),
            (r'\b\d+(\.\d+)?\b',                                      fmt(TEXT, italic=True)),
            (r'(//[^\n]*|#[^\n]*)',                                    fmt(DIM, italic=True)),
            (r'[+\-*/=<>!]+',                                          fmt(ERROR)),
        ]
        for pattern, fmt_ in patterns:
            self._rules.append((QRegularExpression(pattern), fmt_))

    def highlightBlock(self, text):
        for rx, fmt in self._rules:
            it = rx.globalMatch(text)
            while it.hasNext():
                m = it.next()
                self.setFormat(m.capturedStart(), m.capturedLength(), fmt)


# ── Main window ───────────────────────────────────────────────────────────────
class CareScriptStudio(QMainWindow):
    def __init__(self):
        super().__init__()
        self._filepath: Path | None = None
        self._process = QProcess(self)
        self._process.readyReadStandardOutput.connect(self._on_stdout)
        self._process.readyReadStandardError.connect(self._on_stderr)
        self._process.finished.connect(self._on_done)
        self._build_ui()
        self._new_file()

    def _build_ui(self):
        self.setWindowTitle("CareScript Studio")
        self.setMinimumSize(900, 620)
        self.setStyleSheet(f"""
            QMainWindow, QWidget   {{ background: {BG}; color: {TEXT}; }}
            QToolBar               {{ background: {SURFACE}; border-bottom: 1px solid {BORDER};
                                      spacing: 4px; padding: 4px 8px; }}
            QToolButton            {{ color: {TEXT}; background: transparent;
                                      border: 1px solid transparent; border-radius: 4px;
                                      padding: 4px 12px; }}
            QToolButton:hover      {{ background: {BORDER}; }}
            QToolButton:pressed    {{ background: {PRIMARY}; color: {BG}; }}
            QSplitter::handle      {{ background: {BORDER}; }}
            QStatusBar             {{ background: {SURFACE}; color: {DIM}; }}
            QLabel                 {{ color: {DIM}; }}
        """)

        tb = QToolBar()
        tb.setMovable(False)
        tb.setIconSize(QSize(16, 16))
        self.addToolBar(tb)

        for label, shortcut, slot in [
            ("New",       "Ctrl+N", self._new_file),
            ("Open",      "Ctrl+O", self._open_file),
            ("Save",      "Ctrl+S", self._save_file),
        ]:
            act = QAction(label, self)
            act.setShortcut(QKeySequence(shortcut))
            act.triggered.connect(slot)
            tb.addAction(act)

        tb.addSeparator()
        run_act = QAction("▶  Run", self)
        run_act.setShortcut(QKeySequence("Ctrl+R"))
        run_act.triggered.connect(self._run)
        tb.addAction(run_act)

        self._editor = QPlainTextEdit()
        self._editor.setFont(QFont("Cascadia Code", 12))
        self._editor.setStyleSheet(f"""
            QPlainTextEdit {{
                background: {SURFACE}; color: {TEXT};
                border: 1px solid {BORDER}; border-radius: 6px; padding: 8px;
                selection-background-color: {BORDER};
            }}
        """)
        CLHighlighter(self._editor.document())

        self._output = QTextEdit()
        self._output.setReadOnly(True)
        self._output.setFont(QFont("Cascadia Code", 11))
        self._output.setStyleSheet(f"""
            QTextEdit {{
                background: {BG}; color: {DIM};
                border: 1px solid {BORDER}; border-radius: 6px; padding: 8px;
            }}
        """)

        out_label = QLabel("Output")
        out_label.setStyleSheet(f"color: {DIM}; font-size: 11px; padding: 2px 0;")

        out_box = QWidget()
        out_layout = QVBoxLayout(out_box)
        out_layout.setContentsMargins(0, 0, 0, 0)
        out_layout.setSpacing(2)
        out_layout.addWidget(out_label)
        out_layout.addWidget(self._output)

        splitter = QSplitter(Qt.Orientation.Vertical)
        splitter.addWidget(self._editor)
        splitter.addWidget(out_box)
        splitter.setSizes([420, 180])

        central = QWidget()
        layout = QVBoxLayout(central)
        layout.setContentsMargins(12, 8, 12, 12)
        layout.setSpacing(0)
        layout.addWidget(splitter)
        self.setCentralWidget(central)

        self._status = QStatusBar()
        self.setStatusBar(self._status)
        self._status.showMessage("Ready")

    # ── File ops ──────────────────────────────────────────────────────────────

    def _new_file(self):
        self._editor.setPlainText('// New CL script\nprint "Hello from CareOS!";')
        self._filepath = None
        self.setWindowTitle("CareScript Studio — untitled.cl")
        self._output.clear()

    def _open_file(self):
        path, _ = QFileDialog.getOpenFileName(
            self, "Open CL Script", str(Path.home()),
            "Care Language (*.cl);;All Files (*)"
        )
        if path:
            self._filepath = Path(path)
            self._editor.setPlainText(self._filepath.read_text())
            self.setWindowTitle(f"CareScript Studio — {self._filepath.name}")

    def _save_file(self):
        if not self._filepath:
            path, _ = QFileDialog.getSaveFileName(
                self, "Save CL Script", str(Path.home() / "script.cl"),
                "Care Language (*.cl);;All Files (*)"
            )
            if not path:
                return
            self._filepath = Path(path)
        self._filepath.write_text(self._editor.toPlainText())
        self.setWindowTitle(f"CareScript Studio — {self._filepath.name}")
        self._status.showMessage(f"Saved {self._filepath.name}")

    # ── Run ───────────────────────────────────────────────────────────────────

    def _run(self):
        self._output.clear()
        if self._process.state() != QProcess.ProcessState.NotRunning:
            return
        tmp = Path(tempfile.gettempdir()) / "carescript_run.cl"
        tmp.write_text(self._editor.toPlainText())
        self._status.showMessage("Running…")
        self._process.start("cl", [str(tmp)])

    def _on_stdout(self):
        data = self._process.readAllStandardOutput().data().decode(errors="replace")
        self._output.setTextColor(QColor(SUCCESS))
        self._output.insertPlainText(data)
        self._output.ensureCursorVisible()

    def _on_stderr(self):
        data = self._process.readAllStandardError().data().decode(errors="replace")
        self._output.setTextColor(QColor(ERROR))
        self._output.insertPlainText(data)
        self._output.ensureCursorVisible()

    def _on_done(self, exit_code, _):
        self._status.showMessage("Done ✓" if exit_code == 0 else f"Exit {exit_code}")


# ── Entry point ───────────────────────────────────────────────────────────────

def main():
    app = QApplication(sys.argv)
    app.setApplicationName("CareScript Studio")
    win = CareScriptStudio()
    if len(sys.argv) >= 2:
        p = Path(sys.argv[1])
        if p.exists():
            win._filepath = p
            win._editor.setPlainText(p.read_text())
            win.setWindowTitle(f"CareScript Studio — {p.name}")
    win.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
```

- [ ] **Step 3: Run the smoke test**

```bash
python3 -m py_compile iso/airootfs/usr/local/bin/carescript-studio && \
  echo "PASS: syntax OK" || echo "FAIL: syntax error"
```

Expected output: `PASS: syntax OK`

- [ ] **Step 4: Add chmod to customize_airootfs.sh**

In `iso/airootfs/root/customize_airootfs.sh`, find the chmod block:

```bash
chmod +x /usr/bin/cl
chmod +x /usr/local/bin/careos-update
chmod +x /usr/local/bin/carepkg
chmod +x /usr/local/bin/careos-info
chmod +x /usr/local/bin/careos-help
chmod +x /usr/local/bin/careos-install
chmod +x /usr/local/bin/carectl
```

Add one line:

```bash
chmod +x /usr/bin/cl
chmod +x /usr/local/bin/careos-update
chmod +x /usr/local/bin/carepkg
chmod +x /usr/local/bin/careos-info
chmod +x /usr/local/bin/careos-help
chmod +x /usr/local/bin/careos-install
chmod +x /usr/local/bin/carectl
chmod +x /usr/local/bin/carescript-studio
```

- [ ] **Step 5: Commit**

```bash
git add iso/airootfs/usr/local/bin/carescript-studio \
        iso/airootfs/root/customize_airootfs.sh
git commit -m "feat: add CareScript Studio PyQt6 CL IDE"
```

---

## Task 5: CareScript Studio Desktop Integration

**Files:**
- Create: `iso/airootfs/usr/share/applications/carescript-studio.desktop`
- Create: `iso/airootfs/etc/skel/Desktop/CareScript Studio.desktop`

- [ ] **Step 1: Create the application desktop entry**

Create `iso/airootfs/usr/share/applications/carescript-studio.desktop`:

```ini
[Desktop Entry]
Name=CareScript Studio
Comment=Write and run Care Language (.cl) scripts
Exec=carescript-studio %f
Icon=careos-carescript
Terminal=false
Type=Application
MimeType=text/x-care-language;
Categories=CareOS;Development;
```

- [ ] **Step 2: Create the Desktop shortcut for the live user**

Create `iso/airootfs/etc/skel/Desktop/CareScript Studio.desktop`:

```ini
[Desktop Entry]
Name=CareScript Studio
Comment=Write and run Care Language (.cl) scripts
Exec=carescript-studio
Icon=careos-carescript
Terminal=false
Type=Application
Categories=CareOS;Development;
```

- [ ] **Step 3: Validate both desktop files**

```bash
desktop-file-validate iso/airootfs/usr/share/applications/carescript-studio.desktop
desktop-file-validate "iso/airootfs/etc/skel/Desktop/CareScript Studio.desktop"
```

Expected: no output (valid).

If `desktop-file-validate` is not installed: `sudo pacman -S desktop-file-utils` or `sudo apt install desktop-file-utils`.

- [ ] **Step 4: Commit**

```bash
git add iso/airootfs/usr/share/applications/carescript-studio.desktop \
        "iso/airootfs/etc/skel/Desktop/CareScript Studio.desktop"
git commit -m "feat: add CareScript Studio desktop entry and shortcut"
```

---

## Task 6: CareOS Identity — App Menu & careos-info

**Files:**
- Modify: `iso/airootfs/usr/share/applications/careos-install.desktop`
- Modify: `iso/airootfs/usr/share/applications/careos-control.desktop`
- Modify: `iso/airootfs/usr/share/applications/careos-welcome.desktop`
- Modify: `iso/airootfs/usr/share/applications/cl-run.desktop`
- Modify: `iso/airootfs/usr/local/bin/careos-info`

- [ ] **Step 1: Update careos-install.desktop**

Change `Categories=System;` to `Categories=CareOS;System;`:

```ini
[Desktop Entry]
Name=Install CareOS
Comment=Start the CareOS installer
Exec=konsole --hold -e careos-install
Icon=distributor-logo-careos
Terminal=false
Type=Application
Categories=CareOS;System;
```

- [ ] **Step 2: Update careos-control.desktop**

Change `Categories=System;` to `Categories=CareOS;System;`:

```ini
[Desktop Entry]
Name=CareOS Control
Comment=Open the CareOS control command
Exec=konsole --hold -e carectl
Icon=distributor-logo-careos
Terminal=false
Type=Application
Categories=CareOS;System;
```

- [ ] **Step 3: Update careos-welcome.desktop**

Change `Categories=System;` to `Categories=CareOS;System;`:

```ini
[Desktop Entry]
Name=CareOS Welcome
Comment=Open the CareOS welcome center
Exec=firefox /usr/share/careos/welcome/index.html
Icon=distributor-logo-careos
Terminal=false
Type=Application
Categories=CareOS;System;
```

- [ ] **Step 4: Update cl-run.desktop**

Add `CareOS;` to the existing categories:

```ini
[Desktop Entry]
Name=Run CL Script
Comment=Run a Care Language (.cl) script
Exec=konsole --hold -e cl %f
Icon=utilities-terminal
Terminal=false
Type=Application
MimeType=text/x-care-language;
Categories=CareOS;Development;
NoDisplay=true
```

- [ ] **Step 5: Update careos-info with CL version and heart-C ASCII art**

Replace the entire `main()` function in `iso/airootfs/usr/local/bin/careos-info`.

Find:

```python
def main() -> None:
    version = os.environ.get("CAREOS_VERSION", "2026.05")
    release = file_value("/etc/careos-release")
    cpu = file_value("/proc/cpuinfo", "model name")
    mem_kb = run(["awk", "/MemTotal/ {print $2}", "/proc/meminfo"])
    mem = "N/A"
    if mem_kb.isdigit():
        mem = f"{int(mem_kb) / 1024 / 1024:.1f} GB"

    print(f"\n{BLUE}{BOLD}CareOS {version}{RESET}  {DIM}system profile{RESET}\n")
    row("Edition", release)
    row("Base", "Arch Linux rolling core")
    row("Desktop", os.environ.get("XDG_CURRENT_DESKTOP", "KDE Plasma"))
    row("Kernel", platform.release())
    row("Host", run(["hostname"]))
    row("Shell", pathlib.Path(os.environ.get("SHELL", "N/A")).name)
    row("CPU", cpu)
    row("GPU", run(["sh", "-c", "lspci | grep -Ei 'vga|3d|display' | head -1 | cut -d: -f3-"]))
    row("Memory", mem)
    row("Packages", run(["sh", "-c", "pacman -Q 2>/dev/null | wc -l"]))
    row("Session", "live" if pathlib.Path("/run/archiso").exists() else "installed")
    print(f"\n{DIM}Try:{RESET} {ACCENT}carectl{RESET} {DIM}for CareOS controls.{RESET}\n")
```

Replace with:

```python
HEART_C = f"""\
{BLUE}    ████  ████{RESET}
{BLUE}  ██████████████{RESET}   {ACCENT}◜{RESET}
{BLUE}  ██████████████{RESET}  {ACCENT}◜ ◝{RESET}
{BLUE}   ████████████{RESET}  {ACCENT}◜   ◝{RESET}
{BLUE}    ██████████{RESET}   {ACCENT}◝   ◞{RESET}
{BLUE}      ██████{RESET}      {ACCENT}◝ ◞{RESET}
{BLUE}        ██{RESET}         {ACCENT}◞{RESET}"""


def main() -> None:
    version = os.environ.get("CAREOS_VERSION", "2026.05")
    release = file_value("/etc/careos-release")
    cpu = file_value("/proc/cpuinfo", "model name")
    mem_kb = run(["awk", "/MemTotal/ {print $2}", "/proc/meminfo"])
    mem = "N/A"
    if mem_kb.isdigit():
        mem = f"{int(mem_kb) / 1024 / 1024:.1f} GB"

    print()
    print(HEART_C)
    print(f"\n{BLUE}{BOLD}CareOS {version}{RESET}  {DIM}system profile{RESET}\n")
    row("Edition", release)
    row("Base", "Arch Linux rolling core")
    row("Desktop", os.environ.get("XDG_CURRENT_DESKTOP", "KDE Plasma"))
    row("Kernel", platform.release())
    row("Host", run(["hostname"]))
    row("Shell", pathlib.Path(os.environ.get("SHELL", "N/A")).name)
    row("CPU", cpu)
    row("GPU", run(["sh", "-c", "lspci | grep -Ei 'vga|3d|display' | head -1 | cut -d: -f3-"]))
    row("Memory", mem)
    row("Packages", run(["sh", "-c", "pacman -Q 2>/dev/null | wc -l"]))
    row("CL Version", "v1.0")
    row("CareOS Apps", "CareScript Studio, carectl, carepkg, careos-install")
    row("Session", "live" if pathlib.Path("/run/archiso").exists() else "installed")
    print(f"\n{DIM}Try:{RESET} {ACCENT}carescript-studio{RESET} {DIM}to write CL scripts.{RESET}\n")
```

- [ ] **Step 6: Validate the Python syntax of careos-info**

```bash
python3 -m py_compile iso/airootfs/usr/local/bin/careos-info && \
  echo "PASS" || echo "FAIL"
```

Expected: `PASS`

- [ ] **Step 7: Validate all updated desktop files**

```bash
for f in \
  iso/airootfs/usr/share/applications/careos-install.desktop \
  iso/airootfs/usr/share/applications/careos-control.desktop \
  iso/airootfs/usr/share/applications/careos-welcome.desktop \
  iso/airootfs/usr/share/applications/cl-run.desktop; do
  desktop-file-validate "$f" && echo "OK: $f" || echo "FAIL: $f"
done
```

Expected: all `OK`.

- [ ] **Step 8: Commit**

```bash
git add \
  iso/airootfs/usr/share/applications/careos-install.desktop \
  iso/airootfs/usr/share/applications/careos-control.desktop \
  iso/airootfs/usr/share/applications/careos-welcome.desktop \
  iso/airootfs/usr/share/applications/cl-run.desktop \
  iso/airootfs/usr/local/bin/careos-info
git commit -m "identity: CareOS app menu category, CL version in careos-info"
```

---

## Self-Review Notes

- `python-pyqt6` and `zram-generator` are confirmed absent from `packages.x86_64` — Task 2 adds both.
- `tracker` / `tracker-miners` are not in the package list — no removal needed.
- `kwriteconfig5` is not called in the chroot — Baloo is disabled via pre-placed `baloofilerc` (Task 3).
- `cl --version` flag doesn't exist — careos-info hardcodes `v1.0` (Task 6).
- CareScript Studio opens `.cl` files via `%f` arg and the existing `text/x-care-language` MIME type — no new MIME registration needed.
- All tasks are independently committable.
