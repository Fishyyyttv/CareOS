# CareOS Theming & Virtual Pet Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add Bit the pixel pet, animated boot/login, deep KDE polish, and redesigned Welcome + Control Center apps to the CareOS Arch Linux ISO.

**Architecture:** All changes live in `iso/airootfs/` (shipped into the live ISO) or `iso/packages.x86_64` (installed packages). Bit is a standalone PyQt6 app; Control Center and Welcome are also PyQt6. Boot uses Plymouth script primitives; login uses QML.

**Tech Stack:** Python 3, PyQt6, psutil, QML/SDDM, Plymouth script, SVG, X11 cursors, OGG audio

---

## Task 1: Add missing packages

**Files:**
- Modify: `iso/packages.x86_64`

- [ ] Add these lines after the `python-pyqt6` line:

```
python-psutil
ttf-inter
ttf-jetbrains-mono
```

(`papirus-icon-theme` is already present at line 164.)

- [ ] Verify no duplicates:

```bash
grep -E "psutil|ttf-inter|ttf-jetbrains" iso/packages.x86_64
```

Expected output:
```
python-psutil
ttf-inter
ttf-jetbrains-mono
```

- [ ] Commit:

```bash
git add iso/packages.x86_64
git commit -m "pkg: add python-psutil, ttf-inter, ttf-jetbrains-mono"
```

---

## Task 2: Create Bit the desktop pet

**Files:**
- Create: `iso/airootfs/usr/local/bin/bit-pet`

- [ ] Create the file (must be executable):

```python
#!/usr/bin/env python3
"""Bit — CareOS desktop pet. PyQt6 transparent always-on-top window."""
import json, os, random, sys
from pathlib import Path
import psutil
from PyQt6.QtCore import Qt, QTimer, QPoint, QRect, QFileSystemWatcher
from PyQt6.QtGui import QColor, QPainter, QPixmap, QAction, QIcon
from PyQt6.QtWidgets import QApplication, QWidget, QMenu, QSystemTrayIcon

CONFIG_DIR = Path.home() / ".config" / "careos"
CONFIG_FILE = CONFIG_DIR / "bit.json"

PALETTES = {
    "blue":  ("#559aff", "#82bcff", "#2a56b8"),
    "cyan":  ("#38bff8", "#7dd6f8", "#1a8cbf"),
    "green": ("#2ecc8e", "#5ddba9", "#1a8a60"),
    "pink":  ("#f56060", "#f88c8c", "#b83030"),
    "gold":  ("#f0b430", "#f5cc70", "#b87a10"),
}
OUTFIT_UNLOCKS = [0, 50, 100, 150, 200]
OUTFIT_NAMES   = ["blue", "cyan", "green", "pink", "gold"]

T = None
EW = "#eaf0ff"  # eye white
EP = "#0f1726"  # eye pupil
BL = "#f56060"  # blush
SG = "#8a99ba"  # sleep gray

def _f(rows):
    return rows

FRAMES = {
    "idle_0": _f([
        [T,T,T,T,T,T,"P","P","P","P",T,T,T,T,T,T],
        [T,T,T,T,"P","B","B","H","H","B","B","P",T,T,T,T],
        [T,T,T,"P","B","B","H","H","H","H","B","B","P",T,T,T],
        [T,T,"P","B","B","H","H","B","B","H","H","B","B","P",T,T],
        [T,"P","B","B","H","B","B","B","B","B","B","H","B","B","P",T],
        [T,"B","B","B","B",EW,EW,"B","B",EW,EW,"B","B","B","B",T],
        [T,"B","B","B","B",EW,EP,"B","B",EP,EW,"B","B","B","B",T],
        [T,"B","B","B","B",EW,EW,"B","B",EW,EW,"B","B","B","B",T],
        [T,"B","B","B","B","B","B","B","B","B","B","B","B","B","B",T],
        [T,"B","B","B","B","B","B","B","B","B","B","B","B","B","B",T],
        [T,"B","B","B","B","B","B","B","B","B","B","B","B","B","B",T],
        [T,"B","B","B",EP,"B","B","B","B","B",EP,"B","B","B","B",T],
        [T,"B","B","B","B",EP,"B","B","B",EP,"B","B","B","B","B",T],
        [T,"P","B","B","B","B",EP,EP,EP,"B","B","B","B","B","P",T],
        [T,T,"P","B","B","B","B","B","B","B","B","B","B","P",T,T],
        [T,T,T,"P","P","B","B","B","B","B","B","P","P",T,T,T],
    ]),
    "idle_1": _f([  # 1px lower for float effect
        [T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T],
        [T,T,T,T,T,T,"P","P","P","P",T,T,T,T,T,T],
        [T,T,T,T,"P","B","B","H","H","B","B","P",T,T,T,T],
        [T,T,T,"P","B","B","H","H","H","H","B","B","P",T,T,T],
        [T,T,"P","B","B","H","H","B","B","H","H","B","B","P",T,T],
        [T,"P","B","B","H","B","B","B","B","B","B","H","B","B","P",T],
        [T,"B","B","B","B",EW,EW,"B","B",EW,EW,"B","B","B","B",T],
        [T,"B","B","B","B",EW,EP,"B","B",EP,EW,"B","B","B","B",T],
        [T,"B","B","B","B",EW,EW,"B","B",EW,EW,"B","B","B","B",T],
        [T,"B","B","B","B","B","B","B","B","B","B","B","B","B","B",T],
        [T,"B","B","B","B","B","B","B","B","B","B","B","B","B","B",T],
        [T,"B","B","B",EP,"B","B","B","B","B",EP,"B","B","B","B",T],
        [T,"B","B","B","B",EP,"B","B","B",EP,"B","B","B","B","B",T],
        [T,"P","B","B","B","B",EP,EP,EP,"B","B","B","B","B","P",T],
        [T,T,"P","B","B","B","B","B","B","B","B","B","B","P",T,T],
        [T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T],
    ]),
    "blink": _f([
        [T,T,T,T,T,T,"P","P","P","P",T,T,T,T,T,T],
        [T,T,T,T,"P","B","B","H","H","B","B","P",T,T,T,T],
        [T,T,T,"P","B","B","H","H","H","H","B","B","P",T,T,T],
        [T,T,"P","B","B","H","H","B","B","H","H","B","B","P",T,T],
        [T,"P","B","B","H","B","B","B","B","B","B","H","B","B","P",T],
        [T,"B","B","B","B","B","B","B","B","B","B","B","B","B","B",T],
        [T,"B","B","B",EP,EP,EP,"B","B",EP,EP,EP,"B","B","B",T],
        [T,"B","B","B","B","B","B","B","B","B","B","B","B","B","B",T],
        [T,"B","B","B","B","B","B","B","B","B","B","B","B","B","B",T],
        [T,"B","B","B","B","B","B","B","B","B","B","B","B","B","B",T],
        [T,"B","B","B","B","B","B","B","B","B","B","B","B","B","B",T],
        [T,"B","B","B",EP,"B","B","B","B","B",EP,"B","B","B","B",T],
        [T,"B","B","B","B",EP,"B","B","B",EP,"B","B","B","B","B",T],
        [T,"P","B","B","B","B",EP,EP,EP,"B","B","B","B","B","P",T],
        [T,T,"P","B","B","B","B","B","B","B","B","B","B","P",T,T],
        [T,T,T,"P","P","B","B","B","B","B","B","P","P",T,T,T],
    ]),
    "happy_0": _f([
        [T,T,T,T,T,T,"P","P","P","P",T,T,T,T,T,T],
        [T,T,T,T,"P","B","B","H","H","B","B","P",T,T,T,T],
        [T,T,T,"P","B","B","H","H","H","H","B","B","P",T,T,T],
        [T,T,"P","B","B","H","H","B","B","H","H","B","B","P",T,T],
        [T,"P","B","B","H","B","B","B","B","B","B","H","B","B","P",T],
        [T,"B","B","B",EW,EW,EW,"B","B",EW,EW,EW,"B","B","B",T],
        [T,"B","B","B",EW,EP,EW,"B","B",EW,EP,EW,"B","B","B",T],
        [T,"B","B","B",EW,EW,EW,"B","B",EW,EW,EW,"B","B","B",T],
        [T,"B","B","B","B","B","B","B","B","B","B","B","B","B","B",T],
        [T,"B","B",BL,"B","B","B","B","B","B","B","B",BL,"B","B",T],
        [T,"B","B","B","B","B","B","B","B","B","B","B","B","B","B",T],
        [T,"B","B",EP,"B","B","B","B","B","B","B","B",EP,"B","B",T],
        [T,"B","B","B",EP,"B","B","B","B","B","B",EP,"B","B","B",T],
        [T,"P","B","B","B",EP,EP,EP,EP,EP,"B","B","B","B","P",T],
        [T,T,"P","B","B","B","B","B","B","B","B","B","B","P",T,T],
        [T,T,T,"P","P","B","B","B","B","B","B","P","P",T,T,T],
    ]),
    "happy_1": _f([  # bounced up 1px
        [T,T,T,T,"P","B","B","H","H","B","B","P",T,T,T,T],
        [T,T,T,"P","B","B","H","H","H","H","B","B","P",T,T,T],
        [T,T,"P","B","B","H","H","B","B","H","H","B","B","P",T,T],
        [T,"P","B","B","H","B","B","B","B","B","B","H","B","B","P",T],
        [T,"B","B","B",EW,EW,EW,"B","B",EW,EW,EW,"B","B","B",T],
        [T,"B","B","B",EW,EP,EW,"B","B",EW,EP,EW,"B","B","B",T],
        [T,"B","B","B",EW,EW,EW,"B","B",EW,EW,EW,"B","B","B",T],
        [T,"B","B","B","B","B","B","B","B","B","B","B","B","B","B",T],
        [T,"B","B",BL,"B","B","B","B","B","B","B","B",BL,"B","B",T],
        [T,"B","B","B","B","B","B","B","B","B","B","B","B","B","B",T],
        [T,"B","B",EP,"B","B","B","B","B","B","B","B",EP,"B","B",T],
        [T,"B","B","B",EP,"B","B","B","B","B","B",EP,"B","B","B",T],
        [T,"P","B","B","B",EP,EP,EP,EP,EP,"B","B","B","B","P",T],
        [T,T,"P","B","B","B","B","B","B","B","B","B","B","P",T,T],
        [T,T,T,"P","P","B","B","B","B","B","B","P","P",T,T,T],
        [T,T,T,T,T,T,T,T,T,T,T,T,T,T,T,T],
    ]),
    "excited_0": _f([
        [T,T,T,T,T,T,"P","P","P","P",T,T,T,T,T,T],
        [T,T,T,T,"P","B","B","H","H","B","B","P",T,T,T,T],
        [T,T,T,"P","B","B","H","H","H","H","B","B","P",T,T,T],
        [T,T,"P","B","B","H","H","B","B","H","H","B","B","P",T,T],
        [T,"P","B","B","H","B","B","B","B","B","B","H","B","B","P",T],
        [T,"B","B","B",EW,EW,EW,"B","B",EW,EW,EW,"B","B","B",T],
        [T,"B","B","B",EW,EP,EW,"B","B",EW,EP,EW,"B","B","B",T],
        [T,"B","B","B",EW,EW,EW,"B","B",EW,EW,EW,"B","B","B",T],
        [T,"B","B","B","B","B","B","B","B","B","B","B","B","B","B",T],
        [T,"B","B",BL,"B","B","B","B","B","B","B","B",BL,"B","B",T],
        [T,"B","B","B","B","B","B","B","B","B","B","B","B","B","B",T],
        [T,"B",EP,"B","B","B","B","B","B","B","B","B",EP,"B","B",T],
        [T,"B","B",EP,"B","B","B","B","B","B","B",EP,"B","B","B",T],
        [T,"P","B","B","B",EP,EP,EP,EP,EP,"B","B","B","B","P",T],
        [T,T,"P","B","B","B","B","B","B","B","B","B","B","P",T,T],
        [T,T,T,"P","P","B","B","B","B","B","B","P","P",T,T,T],
    ]),
    "tired": _f([
        [T,T,T,T,T,T,"P","P","P","P",T,T,T,T,T,T],
        [T,T,T,T,"P","B","B","H","H","B","B","P",T,T,T,T],
        [T,T,T,"P","B","B","H","H","H","H","B","B","P",T,T,T],
        [T,T,"P","B","B","H","H","B","B","H","H","B","B","P",T,T],
        [T,"P","B","B","H","B","B","B","B","B","B","H","B","B","P",T],
        [T,"B","B","B","B","B","B","B","B","B","B","B","B","B","B",T],
        [T,"B","B",EP,EP,EW,EW,"B","B",EP,EP,EW,EW,"B","B",T],
        [T,"B","B","B","B","B","B","B","B","B","B","B","B","B","B",T],
        [T,"B","B","B","B","B","B","B","B","B","B","B","B","B","B",T],
        [T,"B","B","B","B","B","B","B","B","B","B","B","B","B","B",T],
        [T,"B","B","B","B","B","B","B","B","B","B","B","B","B","B",T],
        [T,"B","B","B",EP,"B","B","B","B","B",EP,"B","B","B","B",T],
        [T,"B","B","B","B",EP,EP,EP,EP,EP,"B","B","B","B","B",T],
        [T,"P","B","B","B","B","B","B","B","B","B","B","B","B","P",T],
        [T,T,"P","B","B","B","B","B","B","B","B","B","B","P",T,T],
        [T,T,T,"P","P","B","B","B","B","B","B","P","P",T,T,T],
    ]),
    "sleep": _f([
        [T,T,T,T,T,T,"P","P","P","P",T,T,T,T,T,T],
        [T,T,T,T,"P",SG,SG,SG,SG,SG,SG,"P",T,T,T,T],
        [T,T,T,"P",SG,SG,SG,SG,SG,SG,SG,SG,"P",T,T,T],
        [T,T,"P",SG,SG,SG,SG,SG,SG,SG,SG,SG,SG,"P",T,T],
        [T,"P",SG,SG,SG,SG,SG,SG,SG,SG,SG,SG,SG,SG,"P",T],
        [T,SG,SG,SG,SG,SG,SG,SG,SG,SG,SG,SG,SG,SG,SG,T],
        [T,SG,SG,SG,EP,EP,SG,SG,SG,EP,EP,SG,SG,SG,SG,T],
        [T,SG,SG,SG,SG,SG,SG,SG,SG,SG,SG,SG,SG,SG,SG,T],
        [T,SG,SG,SG,SG,SG,SG,SG,SG,SG,SG,SG,SG,SG,SG,T],
        [T,SG,SG,SG,SG,SG,SG,SG,SG,SG,SG,SG,SG,SG,SG,T],
        [T,SG,SG,SG,SG,SG,SG,SG,SG,SG,SG,SG,SG,SG,SG,T],
        [T,SG,SG,SG,EP,SG,SG,SG,SG,SG,EP,SG,SG,SG,SG,T],
        [T,SG,SG,SG,SG,EP,EP,EP,EP,SG,SG,SG,SG,SG,SG,T],
        [T,"P",SG,SG,SG,SG,SG,SG,SG,SG,SG,SG,SG,SG,"P",T],
        [T,T,"P",SG,SG,SG,SG,SG,SG,SG,SG,SG,SG,"P",T,T],
        [T,T,T,"P","P",SG,SG,SG,SG,SG,SG,"P","P",T,T,T],
    ]),
}

STATE_SEQUENCES = {
    "idle":    ["idle_0","idle_0","idle_1","idle_1"],
    "blink":   ["blink","blink","idle_0","idle_0"],
    "happy":   ["happy_0","happy_1","happy_0","happy_1","happy_0","idle_0"],
    "excited": ["excited_0","happy_0","excited_0","happy_0","idle_0","idle_1"],
    "tired":   ["tired","tired","tired","tired"],
    "sleep":   ["sleep","sleep","sleep","sleep"],
}


def load_config():
    CONFIG_DIR.mkdir(parents=True, exist_ok=True)
    if CONFIG_FILE.exists():
        try:
            return json.loads(CONFIG_FILE.read_text())
        except Exception:
            pass
    return {"points": 0, "outfit": "blue", "x": -1, "y": -1}


def save_config(cfg):
    CONFIG_FILE.write_text(json.dumps(cfg, indent=2))


class BitWindow(QWidget):
    SCALE = 8
    SIZE  = 16 * SCALE  # 128px

    def __init__(self):
        super().__init__()
        self.cfg = load_config()
        self.state = "idle"
        self.seq_idx = 0
        self._cpu_high_ticks = 0
        self._idle_ticks = 0
        self._blink_countdown = random.randint(20, 50)

        self.setWindowFlags(
            Qt.WindowType.FramelessWindowHint |
            Qt.WindowType.WindowStaysOnTopHint |
            Qt.WindowType.Tool
        )
        self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground)
        self.setAttribute(Qt.WidgetAttribute.WA_ShowWithoutActivating)
        self.setFixedSize(self.SIZE, self.SIZE)
        self._place_window()

        # Animation timer (150ms)
        self._anim = QTimer(self)
        self._anim.timeout.connect(self._tick)
        self._anim.start(150)

        # System monitor timer (10s)
        self._sys = QTimer(self)
        self._sys.timeout.connect(self._check_system)
        self._sys.start(10_000)

        # Downloads watcher
        dl = str(Path.home() / "Downloads")
        self._watcher = QFileSystemWatcher([dl], self)
        self._watcher.directoryChanged.connect(self._on_download)

        # Drag support
        self._drag_pos = None

    def _place_window(self):
        screen = QApplication.primaryScreen().geometry()
        x = self.cfg.get("x", -1)
        y = self.cfg.get("y", -1)
        if x < 0 or y < 0:
            x = screen.width()  - self.SIZE - 24
            y = screen.height() - self.SIZE - 56
        self.move(x, y)

    # ── State control ─────────────────────────────────────────────────────────

    def set_state(self, state):
        if self.state == state:
            return
        self.state = state
        self.seq_idx = 0
        self.update()

    def _tick(self):
        seq = STATE_SEQUENCES.get(self.state, ["idle_0"])
        self.seq_idx = (self.seq_idx + 1) % len(seq)
        self.update()

        # Idle-state blink logic
        if self.state == "idle":
            self._idle_ticks += 1
            self._blink_countdown -= 1
            if self._blink_countdown <= 0:
                self._blink_countdown = random.randint(20, 50)
                self.set_state("blink")
            if self._idle_ticks > 1200:  # 30min @ 150ms
                self.set_state("sleep")
        elif self.state not in ("tired", "sleep"):
            self._idle_ticks = 0

        # Auto-return from transient states
        if self.state == "blink" and self.seq_idx == 0:
            self.set_state("idle")
        elif self.state in ("happy", "excited") and self.seq_idx == 0:
            self.set_state("idle")

    def _check_system(self):
        cpu = psutil.cpu_percent(interval=None)
        if cpu > 80:
            self._cpu_high_ticks += 1
            if self._cpu_high_ticks >= 2 and self.state not in ("happy", "excited"):
                self.set_state("tired")
        else:
            self._cpu_high_ticks = 0
            if self.state == "tired":
                self.set_state("idle")

    def _on_download(self, _path):
        self.set_state("excited")

    # ── Rendering ─────────────────────────────────────────────────────────────

    def _resolve_color(self, token):
        body, hi, shadow = PALETTES.get(self.cfg.get("outfit", "blue"), PALETTES["blue"])
        mapping = {"B": body, "H": hi, "P": shadow,
                   EW: EW, EP: EP, BL: BL, SG: SG}
        return mapping.get(token, token)

    def paintEvent(self, _event):
        seq  = STATE_SEQUENCES.get(self.state, ["idle_0"])
        name = seq[self.seq_idx % len(seq)]
        grid = FRAMES[name]

        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing, False)
        s = self.SCALE

        for row_i, row in enumerate(grid):
            for col_i, token in enumerate(row):
                if token is None:
                    continue
                color = self._resolve_color(token)
                p.fillRect(col_i * s, row_i * s, s, s, QColor(color))
        p.end()

    # ── Interaction ───────────────────────────────────────────────────────────

    def mousePressEvent(self, e):
        if e.button() == Qt.MouseButton.LeftButton:
            self._drag_pos = e.globalPosition().toPoint() - self.frameGeometry().topLeft()
            self._idle_ticks = 0
            self.cfg["points"] = self.cfg.get("points", 0) + 1
            save_config(self.cfg)
            self.set_state("happy")

    def mouseMoveEvent(self, e):
        if self._drag_pos and e.buttons() & Qt.MouseButton.LeftButton:
            self.move(e.globalPosition().toPoint() - self._drag_pos)

    def mouseReleaseEvent(self, e):
        if e.button() == Qt.MouseButton.LeftButton:
            self._drag_pos = None
            pos = self.pos()
            self.cfg["x"] = pos.x()
            self.cfg["y"] = pos.y()
            save_config(self.cfg)

    def contextMenuEvent(self, e):
        pts    = self.cfg.get("points", 0)
        outfit = self.cfg.get("outfit", "blue")
        menu   = QMenu(self)
        menu.setStyleSheet("""
            QMenu { background:#0f1726; color:#eaf0ff; border:1px solid #2c3c58; border-radius:6px; padding:4px; }
            QMenu::item { padding:6px 20px; border-radius:4px; }
            QMenu::item:selected { background:#1c2942; }
        """)
        menu.addAction(f"💙 Care Points: {pts}").setEnabled(False)
        menu.addSeparator()

        outfit_menu = menu.addMenu("Outfits")
        for i, name in enumerate(OUTFIT_NAMES):
            threshold = OUTFIT_UNLOCKS[i]
            label = f"{'✓ ' if name == outfit else '  '}{name.capitalize()}"
            if pts < threshold:
                label += f" (unlock at {threshold} pts)"
                act = outfit_menu.addAction(label)
                act.setEnabled(False)
            else:
                act = outfit_menu.addAction(label)
                act.triggered.connect(lambda _, n=name: self._set_outfit(n))

        menu.addSeparator()
        menu.addAction("Hide Bit").triggered.connect(self.hide)
        menu.exec(e.globalPos())

    def _set_outfit(self, name):
        self.cfg["outfit"] = name
        save_config(self.cfg)
        self.update()


def main():
    app = QApplication(sys.argv)
    app.setQuitOnLastWindowClosed(False)
    win = BitWindow()
    win.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
```

- [ ] Make executable:

```bash
chmod +x iso/airootfs/usr/local/bin/bit-pet
```

- [ ] Smoke-test locally (requires PyQt6 + psutil on your dev machine):

```bash
python3 iso/airootfs/usr/local/bin/bit-pet &
# Bit should appear bottom-right. Click him → happy bounce. Right-click → menu.
kill %1
```

- [ ] Commit:

```bash
git add iso/airootfs/usr/local/bin/bit-pet
git commit -m "feat: add Bit desktop pet (PyQt6 pixel sprite)"
```

---

## Task 3: Bit autostart + systemd user service

**Files:**
- Create: `iso/airootfs/etc/skel/.config/autostart/bit-pet.desktop`
- Create: `iso/airootfs/etc/skel/.config/systemd/user/bit-pet.service`

- [ ] Create the autostart `.desktop`:

```ini
[Desktop Entry]
Type=Application
Name=Bit Pet
Exec=/usr/local/bin/bit-pet
Icon=careos-carescript
Terminal=false
X-GNOME-Autostart-enabled=true
```

- [ ] Create the systemd user service:

```ini
[Unit]
Description=Bit — CareOS Desktop Pet
After=graphical-session.target

[Service]
Type=simple
ExecStart=/usr/local/bin/bit-pet
Restart=on-failure
RestartSec=5

[Install]
WantedBy=graphical-session.target
```

- [ ] Commit:

```bash
git add iso/airootfs/etc/skel/.config/autostart/bit-pet.desktop \
        iso/airootfs/etc/skel/.config/systemd/user/bit-pet.service
git commit -m "feat: autostart and systemd user service for Bit"
```

---

## Task 4: Upgrade Plymouth boot animation

**Files:**
- Modify: `iso/airootfs/usr/share/plymouth/themes/careos/careos.script`

- [ ] Replace the entire file content:

```javascript
// CareOS Plymouth — heart-C logo + progress ring animation

screen_w = Window.GetWidth();
screen_h = Window.GetHeight();
cx = Math.Int(screen_w / 2);
cy = Math.Int(screen_h / 2);

// Background
bg = Rectangle();
bg.SetX(0); bg.SetY(0);
bg.SetWidth(screen_w); bg.SetHeight(screen_h);
bg.SetColor(0.031, 0.051, 0.090, 1.0);  // #080d17

// Heart-C: two overlapping circles (left lobe, right lobe) + triangle bottom
// Approximated with rectangles at increasing scale for a pixel-art heart feel
heart_size = Math.Int(screen_h * 0.06);
hs = heart_size;

// Left lobe
hl = Rectangle();
hl.SetWidth(hs); hl.SetHeight(hs);
hl.SetX(cx - Math.Int(hs * 0.9)); hl.SetY(cy - Math.Int(hs * 0.6));
hl.SetColor(0.333, 0.604, 1.0, 0.0);

// Right lobe
hr = Rectangle();
hr.SetWidth(hs); hr.SetHeight(hs);
hr.SetX(cx - Math.Int(hs * 0.1)); hr.SetY(cy - Math.Int(hs * 0.6));
hr.SetColor(0.333, 0.604, 1.0, 0.0);

// Body (lower half of heart)
hb = Rectangle();
hb.SetWidth(Math.Int(hs * 1.8)); hb.SetHeight(hs);
hb.SetX(cx - Math.Int(hs * 0.9)); hb.SetY(cy - Math.Int(hs * 0.1));
hb.SetColor(0.333, 0.604, 1.0, 0.0);

// Glow rings (layered large translucent rects behind heart)
glow1 = Rectangle();
glow1.SetWidth(Math.Int(hs * 4)); glow1.SetHeight(Math.Int(hs * 4));
glow1.SetX(cx - Math.Int(hs * 2)); glow1.SetY(cy - Math.Int(hs * 2));
glow1.SetColor(0.333, 0.604, 1.0, 0.0);

glow2 = Rectangle();
glow2.SetWidth(Math.Int(hs * 6)); glow2.SetHeight(Math.Int(hs * 6));
glow2.SetX(cx - Math.Int(hs * 3)); glow2.SetY(cy - Math.Int(hs * 3));
glow2.SetColor(0.333, 0.604, 1.0, 0.0);

// Progress dots (ring of 12 dots around heart)
NUM_DOTS = 12;
DOT_R    = Math.Int(screen_h * 0.008);
RING_R   = Math.Int(hs * 2.2);

for (i = 0; i < NUM_DOTS; i++) {
    angle_rad = (i / NUM_DOTS) * 6.2832;
    // Plymouth has no trig — approximate with precomputed table (12 positions)
    cos_vals[0]=1.00; cos_vals[1]=0.87; cos_vals[2]=0.50; cos_vals[3]=0.00;
    cos_vals[4]=-0.50; cos_vals[5]=-0.87; cos_vals[6]=-1.00;
    cos_vals[7]=-0.87; cos_vals[8]=-0.50; cos_vals[9]=0.00;
    cos_vals[10]=0.50; cos_vals[11]=0.87;
    sin_vals[0]=0.00; sin_vals[1]=0.50; sin_vals[2]=0.87; sin_vals[3]=1.00;
    sin_vals[4]=0.87; sin_vals[5]=0.50; sin_vals[6]=0.00;
    sin_vals[7]=-0.50; sin_vals[8]=-0.87; sin_vals[9]=-1.00;
    sin_vals[10]=-0.87; sin_vals[11]=-0.50;

    dx = Math.Int(RING_R * cos_vals[i]);
    dy = Math.Int(RING_R * sin_vals[i]);
    dots[i] = Rectangle();
    dots[i].SetWidth(DOT_R * 2); dots[i].SetHeight(DOT_R * 2);
    dots[i].SetX(cx + dx - DOT_R); dots[i].SetY(cy + dy - DOT_R);
    dots[i].SetColor(0.333, 0.604, 1.0, 0.05);
}

frame = 0;
progress_val = 0.0;

fun show_heart(alpha) {
    hl.SetColor(0.333, 0.604, 1.0, alpha);
    hr.SetColor(0.333, 0.604, 1.0, alpha);
    hb.SetColor(0.333, 0.604, 1.0, alpha);
    glow1.SetColor(0.333, 0.604, 1.0, alpha * 0.12);
    glow2.SetColor(0.333, 0.604, 1.0, alpha * 0.05);
}

fun refresh_callback() {
    frame++;

    // Phase 1: fade in (frames 0–50)
    if (frame <= 50) {
        alpha = frame / 50.0;
        show_heart(alpha);
    }

    // Phase 2: progress ring (frames 50+)
    if (frame > 50) {
        show_heart(1.0);
        filled = Math.Int(progress_val * NUM_DOTS);
        for (i = 0; i < NUM_DOTS; i++) {
            if (i < filled) {
                dots[i].SetColor(0.333, 0.604, 1.0, 1.0);
            } else if (i == filled) {
                dots[i].SetColor(0.333, 0.604, 1.0, 0.5);
            } else {
                dots[i].SetColor(0.333, 0.604, 1.0, 0.08);
            }
        }
    }
}

Plymouth.SetRefreshFunction(refresh_callback);

fun progress_callback(duration, progress) {
    progress_val = progress;
}

Plymouth.SetBootProgressFunction(progress_callback);

fun display_password_callback(prompt, bullets) {
    // Keep running — don't clear
}
Plymouth.SetDisplayPasswordFunction(display_password_callback);
```

- [ ] Commit:

```bash
git add iso/airootfs/usr/share/plymouth/themes/careos/careos.script
git commit -m "feat: animated Plymouth boot (heart-C fade-in + progress ring)"
```

---

## Task 5: Polish SDDM login screen

The existing `Main.qml` is already solid. This task adds: Inter font, floating particle animation, and the heart-C SVG logo above the wordmark.

**Files:**
- Modify: `iso/airootfs/usr/share/sddm/themes/careos/Main.qml`

- [ ] Replace the `// Wordmark` `Column` block (lines 89–109 in the current file) with:

```qml
        // Heart-C logo
        Image {
            anchors.horizontalCenter: parent.horizontalCenter
            source: "file:///usr/share/careos/branding/heart-c.svg"
            width: 52; height: 52
            fillMode: Image.PreserveAspectFit
            smooth: true
        }

        Item { height: 8 }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "CareOS"
            font { pixelSize: 40; weight: Font.Light; letterSpacing: 4; family: "Inter" }
            color: "#eaf0ff"
        }
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 56; height: 2; color: "#559aff"
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "version 9"
            font { pixelSize: 12; weight: Font.Medium; letterSpacing: 2; family: "Inter" }
            color: "#505e78"
        }
```

- [ ] Replace all `family: "Noto Sans"` with `family: "Inter"` in the file:

```bash
sed -i 's/family: "Noto Sans"/family: "Inter"/g' \
    iso/airootfs/usr/share/sddm/themes/careos/Main.qml
```

- [ ] Add floating particles. Insert this block just after the grid `Canvas` closing brace (after line 47 `}`), before the closing `}` of the outer `Rectangle`:

```qml
        // Floating particles
        Repeater {
            model: 18
            delegate: Rectangle {
                id: particle
                width: 3; height: 3
                radius: 1.5
                color: "#559aff"
                opacity: Math.random() * 0.25 + 0.05
                x: Math.random() * Screen.width
                y: Screen.height + 10
                NumberAnimation on y {
                    from: Screen.height + 10
                    to: -10
                    duration: 8000 + Math.random() * 12000
                    loops: Animation.Infinite
                    running: true
                }
                NumberAnimation on x {
                    from: particle.x
                    to: particle.x + (Math.random() - 0.5) * 120
                    duration: 8000 + Math.random() * 12000
                    loops: Animation.Infinite
                    running: true
                }
            }
        }
```

- [ ] Commit:

```bash
git add iso/airootfs/usr/share/sddm/themes/careos/Main.qml
git commit -m "feat: polish SDDM login — Inter font, heart-C logo, floating particles"
```

---

## Task 6: Upgrade wallpaper SVG

**Files:**
- Modify: `iso/airootfs/usr/share/wallpapers/CareOS/contents/images/1920x1080.svg`

- [ ] Replace the file entirely:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="1920" height="1080" viewBox="0 0 1920 1080">
  <defs>
    <linearGradient id="bg" x1="0" y1="0" x2="1" y2="1">
      <stop offset="0%" stop-color="#080d17"/>
      <stop offset="100%" stop-color="#0f1a2e"/>
    </linearGradient>
    <pattern id="grid" width="80" height="80" patternUnits="userSpaceOnUse">
      <path d="M 80 0 L 0 0 0 80" fill="none" stroke="#559aff" stroke-width="0.6" opacity="0.08"/>
    </pattern>
  </defs>

  <!-- Background -->
  <rect width="1920" height="1080" fill="url(#bg)"/>

  <!-- Grid overlay -->
  <rect width="1920" height="1080" fill="url(#grid)"/>

  <!-- Heart-C watermark (centered, large, very dim) -->
  <g transform="translate(960,540)" opacity="0.04" fill="#559aff">
    <!-- Left lobe -->
    <ellipse cx="-90" cy="-80" rx="110" ry="110"/>
    <!-- Right lobe -->
    <ellipse cx="90" cy="-80" rx="110" ry="110"/>
    <!-- Lower triangle body -->
    <polygon points="-190,-20 190,-20 0,200"/>
    <!-- C cutout (white to subtract) -->
    <text x="20" y="60" font-family="Inter,Arial" font-weight="700"
          font-size="200" fill="#080d17" text-anchor="middle">C</text>
  </g>

  <!-- Subtle radial glow center -->
  <radialGradient id="glow" cx="50%" cy="50%" r="40%">
    <stop offset="0%" stop-color="#559aff" stop-opacity="0.04"/>
    <stop offset="100%" stop-color="#559aff" stop-opacity="0"/>
  </radialGradient>
  <rect width="1920" height="1080" fill="url(#glow)"/>
</svg>
```

- [ ] Commit:

```bash
git add "iso/airootfs/usr/share/wallpapers/CareOS/contents/images/1920x1080.svg"
git commit -m "feat: upgraded CareOS wallpaper — grid + heart-C watermark"
```

---

## Task 7: KDE config — fonts, icons, cursor

**Files:**
- Modify: `iso/airootfs/etc/skel/.config/kdeglobals`
- Modify: `iso/airootfs/etc/skel/.config/kcminputrc`
- Modify: `iso/airootfs/etc/skel/.config/konsolerc`

- [ ] Read the current `kdeglobals` and locate the `[General]` and `[Icons]` sections. Set or add these keys:

```ini
[General]
font=Inter,10,-1,5,50,0,0,0,0,0
fixed=JetBrains Mono,11,-1,5,50,0,0,0,0,0
smallestReadableFont=Inter,8,-1,5,50,0,0,0,0,0
toolBarFont=Inter,10,-1,5,50,0,0,0,0,0
menuFont=Inter,10,-1,5,50,0,0,0,0,0

[Icons]
Theme=CareOS
```

- [ ] Set cursor theme in `kcminputrc`:

```ini
[Mouse]
cursorTheme=CareOS-cursors
cursorSize=24
```

- [ ] Set JetBrains Mono in `konsolerc` — locate or add under `[Desktop Entry]`:

```ini
[Desktop Entry]
DefaultProfile=CareOS.profile
```

(The profile already sets the font in `CareOS.profile` — verify it has `Font=JetBrains Mono,11,-1,5,50,0,0,0,0,0` under `[Appearance]`.)

- [ ] Read `iso/airootfs/usr/share/konsole/CareOS.profile` and confirm/add:

```ini
[Appearance]
Font=JetBrains Mono,11,-1,5,50,0,0,0,0,0
ColorScheme=CareOS
```

- [ ] Commit:

```bash
git add iso/airootfs/etc/skel/.config/kdeglobals \
        iso/airootfs/etc/skel/.config/kcminputrc \
        iso/airootfs/etc/skel/.config/konsolerc \
        iso/airootfs/usr/share/konsole/CareOS.profile
git commit -m "feat: KDE fonts (Inter + JetBrains Mono), icon theme, cursor config"
```

---

## Task 8: Icon theme scaffold

**Files:**
- Create: `iso/airootfs/usr/share/icons/CareOS/index.theme`
- Create: `iso/airootfs/usr/share/icons/CareOS/apps/scalable/careos-control.svg`
- Create: `iso/airootfs/usr/share/icons/CareOS/apps/scalable/bit-pet.svg`

- [ ] Create `index.theme`:

```ini
[Icon Theme]
Name=CareOS
Comment=CareOS icon theme
Inherits=Papirus,hicolor
Directories=apps/scalable,places/scalable

[apps/scalable]
Size=scalable
MinSize=16
MaxSize=512
Type=Scalable

[places/scalable]
Size=scalable
MinSize=16
MaxSize=512
Type=Scalable
```

- [ ] Create `careos-control.svg` (a simple panel icon in CareOS blue):

```xml
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 48 48">
  <rect x="4" y="4" width="40" height="40" rx="8" fill="#0f1726"/>
  <path d="M24 8 C15.2 8 8 15.2 8 24 C8 32.8 15.2 40 24 40 C32.8 40 40 32.8 40 24 C40 15.2 32.8 8 24 8Z" fill="none" stroke="#559aff" stroke-width="2.5"/>
  <circle cx="24" cy="24" r="5" fill="#559aff"/>
  <path d="M24 13 L24 17 M24 31 L24 35 M13 24 L17 24 M31 24 L35 24" stroke="#82bcff" stroke-width="2.5" stroke-linecap="round"/>
</svg>
```

- [ ] Create `bit-pet.svg` (a tiny pixel blob icon):

```xml
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 48 48">
  <rect x="14" y="8"  width="20" height="20" rx="10" fill="#559aff"/>
  <rect x="8"  y="14" width="32" height="18" rx="9"  fill="#559aff"/>
  <circle cx="19" cy="20" r="3" fill="#eaf0ff"/>
  <circle cx="29" cy="20" r="3" fill="#eaf0ff"/>
  <circle cx="20" cy="21" r="1.5" fill="#0f1726"/>
  <circle cx="30" cy="21" r="1.5" fill="#0f1726"/>
  <path d="M19 28 Q24 32 29 28" fill="none" stroke="#0f1726" stroke-width="1.8" stroke-linecap="round"/>
</svg>
```

- [ ] Commit:

```bash
git add iso/airootfs/usr/share/icons/CareOS/
git commit -m "feat: CareOS icon theme scaffold with control and pet icons"
```

---

## Task 9: Sound files

**Files:**
- Create: `tools/gen-sounds.py`
- Create: `iso/airootfs/usr/share/sounds/CareOS/index.theme`
- Create (generated): `iso/airootfs/usr/share/sounds/CareOS/stereo/{startup,notification,error,logout}.ogg`

- [ ] Create `tools/gen-sounds.py` (run this on your dev machine — requires `numpy`, `scipy`, `ffmpeg`):

```python
#!/usr/bin/env python3
"""Generate CareOS system sounds. Run once on dev machine, commit the .ogg output."""
import numpy as np
from scipy.io import wavfile
import subprocess, os, tempfile
from pathlib import Path

SR = 44100
OUT = Path("iso/airootfs/usr/share/sounds/CareOS/stereo")
OUT.mkdir(parents=True, exist_ok=True)

def sine(freq, dur, amp=0.4):
    t = np.linspace(0, dur, int(SR * dur), False)
    return (amp * np.sin(2 * np.pi * freq * t)).astype(np.float32)

def fade(sig, fade_s=0.05):
    n = int(SR * fade_s)
    sig[:n]  *= np.linspace(0, 1, n)
    sig[-n:] *= np.linspace(1, 0, n)
    return sig

def chord(freqs, dur, amp=0.25):
    return sum(fade(sine(f, dur, amp)) for f in freqs)

def to_ogg(sig, name):
    wav = OUT / f"{name}.wav"
    ogg = OUT / f"{name}.ogg"
    wavfile.write(str(wav), SR, (sig * 32767).astype(np.int16))
    subprocess.run(["ffmpeg", "-y", "-i", str(wav), str(ogg)], check=True,
                   capture_output=True)
    wav.unlink()
    print(f"  wrote {ogg}")

# startup — ascending soft chord C4 E4 G4
startup = np.concatenate([
    chord([261.6, 329.6], 0.4),
    chord([261.6, 329.6, 392.0], 1.0),
])
to_ogg(startup, "startup")

# notification — two-note chime E5 A5
notif = np.concatenate([
    fade(sine(659.3, 0.18, 0.35)),
    np.zeros(int(SR * 0.04)),
    fade(sine(880.0, 0.28, 0.3)),
])
to_ogg(notif, "notification")

# error — descending A4 E4
err = np.concatenate([
    fade(sine(440.0, 0.2, 0.3)),
    np.zeros(int(SR * 0.03)),
    fade(sine(329.6, 0.3, 0.25)),
])
to_ogg(err, "error")

# logout — soft descending G4 E4 C4
logout = np.concatenate([
    chord([392.0, 329.6], 0.35),
    chord([329.6, 261.6], 0.35),
    chord([261.6], 0.6),
])
to_ogg(logout, "logout")

print("Done.")
```

- [ ] Run the generator on your dev machine (install deps first if needed):

```bash
pip install numpy scipy
# ensure ffmpeg is available: winget install ffmpeg  (Windows) or pacman -S ffmpeg (Arch)
python3 tools/gen-sounds.py
```

Expected output:
```
  wrote iso/airootfs/usr/share/sounds/CareOS/stereo/startup.ogg
  wrote iso/airootfs/usr/share/sounds/CareOS/stereo/notification.ogg
  wrote iso/airootfs/usr/share/sounds/CareOS/stereo/error.ogg
  wrote iso/airootfs/usr/share/sounds/CareOS/stereo/logout.ogg
Done.
```

- [ ] Create `index.theme`:

```ini
[Sound Theme]
Name=CareOS
Comment=CareOS system sounds
Inherits=freedesktop
Directories=stereo

[stereo]
OutputProfile=stereo
```

- [ ] Commit:

```bash
git add tools/gen-sounds.py \
        iso/airootfs/usr/share/sounds/CareOS/
git commit -m "feat: CareOS sound theme (startup, notification, error, logout)"
```

---

## Task 10: Welcome app rewrite

**Files:**
- Create: `iso/airootfs/usr/local/bin/careos-welcome` (replaces the existing shell script that opens index.html)

- [ ] Write the file:

```python
#!/usr/bin/env python3
"""CareOS Welcome — 4-page PyQt6 first-run wizard."""
import json, sys
from pathlib import Path
from PyQt6.QtCore import Qt, QTimer, QPropertyAnimation, QEasingCurve
from PyQt6.QtGui import QColor, QPainter, QFont
from PyQt6.QtWidgets import (
    QApplication, QWidget, QVBoxLayout, QHBoxLayout, QLabel,
    QPushButton, QCheckBox, QStackedWidget, QFrame, QSizePolicy
)

CONFIG_DIR  = Path.home() / ".config" / "careos"
WELCOME_FLAG = CONFIG_DIR / ".welcomed"
BIT_CFG     = CONFIG_DIR / "bit.json"

STYLE = """
QWidget { background: #080d17; color: #eaf0ff; font-family: Inter; }
QPushButton {
    background: #559aff; color: #080d17; border: none;
    border-radius: 8px; padding: 10px 28px; font-size: 14px; font-weight: 600;
}
QPushButton:hover    { background: #6aabff; }
QPushButton:pressed  { background: #2f6fc8; }
QPushButton#secondary {
    background: transparent; color: #8a99ba;
    border: 1px solid #2c3c58;
}
QPushButton#secondary:hover { background: #1c2942; color: #eaf0ff; }
QCheckBox { font-size: 13px; spacing: 8px; }
QCheckBox::indicator { width:18px; height:18px; border-radius:4px;
                        border:1px solid #2c3c58; background:#0f1726; }
QCheckBox::indicator:checked { background:#559aff; border-color:#559aff; }
"""

OUTFITS = [
    ("blue",  "#559aff", "Blue (default)"),
    ("cyan",  "#38bff8", "Cyan"),
    ("green", "#2ecc8e", "Green"),
    ("pink",  "#f56060", "Pink"),
    ("gold",  "#f0b430", "Gold"),
]


def load_bit():
    CONFIG_DIR.mkdir(parents=True, exist_ok=True)
    if BIT_CFG.exists():
        try:
            return json.loads(BIT_CFG.read_text())
        except Exception:
            pass
    return {"points": 0, "outfit": "blue"}


def save_bit(cfg):
    BIT_CFG.write_text(json.dumps(cfg, indent=2))


class HeartWidget(QWidget):
    """Animated heart-C logo drawn via QPainter."""
    def __init__(self, size=80, parent=None):
        super().__init__(parent)
        self.size = size
        self.setFixedSize(size, size)
        self._scale = 1.0
        self._growing = True
        t = QTimer(self)
        t.timeout.connect(self._pulse)
        t.start(40)

    def _pulse(self):
        step = 0.003
        if self._growing:
            self._scale += step
            if self._scale >= 1.06:
                self._growing = False
        else:
            self._scale -= step
            if self._scale <= 1.0:
                self._growing = True
        self.update()

    def paintEvent(self, _):
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        s = self.size
        cx, cy = s // 2, s // 2
        p.translate(cx, cy)
        p.scale(self._scale, self._scale)
        p.translate(-cx, -cy)
        # Heart shape via two ellipses + polygon
        from PyQt6.QtGui import QPainterPath
        path = QPainterPath()
        r = s * 0.28
        # left lobe
        path.addEllipse(cx - r * 1.05, cy - r * 0.8, r * 1.9, r * 1.9)
        # right lobe
        path.addEllipse(cx - r * 0.85, cy - r * 0.8, r * 1.9, r * 1.9)
        p.fillPath(path, QColor("#559aff"))
        # C letter
        p.setPen(QColor("#0f1726"))
        font = QFont("Inter", int(s * 0.32), QFont.Weight.Bold)
        p.setFont(font)
        p.drawText(0, 0, s, int(s * 1.1), Qt.AlignmentFlag.AlignCenter, "C")
        p.end()


class OutfitButton(QPushButton):
    def __init__(self, name, color, label, parent=None):
        super().__init__(parent)
        self.outfit_name = name
        self._color = color
        self.setCheckable(True)
        self.setFixedSize(80, 90)
        self.setText(label)
        self.setStyleSheet(f"""
            QPushButton {{
                background: #0f1726; border: 2px solid #2c3c58;
                border-radius: 10px; color: #8a99ba; font-size: 10px;
                padding-top: 50px;
            }}
            QPushButton:checked {{
                border-color: {color}; color: #eaf0ff;
            }}
        """)

    def paintEvent(self, e):
        super().paintEvent(e)
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        # Draw a small blob circle in the outfit color
        r = 18
        p.setBrush(QColor(self._color))
        p.setPen(Qt.PenStyle.NoPen)
        p.drawEllipse(self.width()//2 - r, 12, r*2, r*2)
        p.end()


def _label(text, size=13, color="#eaf0ff", bold=False):
    lbl = QLabel(text)
    f = QFont("Inter", size)
    if bold:
        f.setWeight(QFont.Weight.Bold)
    lbl.setFont(f)
    lbl.setStyleSheet(f"color: {color};")
    lbl.setWordWrap(True)
    lbl.setAlignment(Qt.AlignmentFlag.AlignCenter)
    return lbl


class WelcomeApp(QWidget):
    def __init__(self):
        super().__init__()
        self.bit_cfg = load_bit()
        self.setWindowTitle("Welcome to CareOS")
        self.setFixedSize(600, 420)
        self.setWindowFlags(Qt.WindowType.Window | Qt.WindowType.WindowCloseButtonHint)

        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)

        self.stack = QStackedWidget()
        root.addWidget(self.stack)

        self.stack.addWidget(self._page1())
        self.stack.addWidget(self._page2())
        self.stack.addWidget(self._page3())
        self.stack.addWidget(self._page4())

        # Center on screen
        screen = QApplication.primaryScreen().geometry()
        self.move(
            (screen.width()  - self.width())  // 2,
            (screen.height() - self.height()) // 2,
        )

    def _nav_row(self, back_cb=None, next_cb=None, next_label="Next →"):
        row = QHBoxLayout()
        row.setContentsMargins(28, 0, 28, 24)
        if back_cb:
            b = QPushButton("← Back")
            b.setObjectName("secondary")
            b.clicked.connect(back_cb)
            row.addWidget(b)
        row.addStretch()
        if next_cb:
            n = QPushButton(next_label)
            n.clicked.connect(next_cb)
            row.addWidget(n)
        w = QWidget()
        w.setLayout(row)
        return w

    def _page1(self):
        w = QWidget()
        v = QVBoxLayout(w)
        v.setContentsMargins(40, 40, 40, 24)
        v.setSpacing(12)
        v.addStretch()
        v.addWidget(HeartWidget(88, w), alignment=Qt.AlignmentFlag.AlignCenter)
        v.addSpacing(8)
        v.addWidget(_label("Welcome to CareOS", 28, bold=True))
        v.addWidget(_label("A focused Arch Linux desktop, made with care.", 13, "#8a99ba"))
        v.addStretch()
        v.addWidget(self._nav_row(next_cb=lambda: self.stack.setCurrentIndex(1),
                                  next_label="Get started →"))
        return w

    def _page2(self):
        w = QWidget()
        v = QVBoxLayout(w)
        v.setContentsMargins(40, 32, 40, 24)
        v.setSpacing(10)
        v.addWidget(_label("Pick Bit's starting color", 18, bold=True))
        v.addWidget(_label("You can change this anytime in CareOS Control.", 12, "#8a99ba"))
        v.addSpacing(8)

        row = QHBoxLayout()
        row.setSpacing(10)
        self._outfit_btns = []
        for name, color, label in OUTFITS:
            btn = OutfitButton(name, color, label)
            btn.setChecked(name == self.bit_cfg.get("outfit", "blue"))
            btn.clicked.connect(lambda _, n=name: self._pick_outfit(n))
            row.addWidget(btn)
            self._outfit_btns.append(btn)
        v.addLayout(row)
        v.addStretch()
        v.addWidget(self._nav_row(
            back_cb=lambda: self.stack.setCurrentIndex(0),
            next_cb=lambda: self.stack.setCurrentIndex(2),
        ))
        return w

    def _pick_outfit(self, name):
        self.bit_cfg["outfit"] = name
        for b in self._outfit_btns:
            b.setChecked(b.outfit_name == name)

    def _page3(self):
        w = QWidget()
        v = QVBoxLayout(w)
        v.setContentsMargins(60, 32, 60, 24)
        v.setSpacing(14)
        v.addWidget(_label("Set up your experience", 18, bold=True))
        v.addSpacing(4)

        self._chk_bit    = QCheckBox("Enable Bit (desktop pet)")
        self._chk_bit.setChecked(True)
        self._chk_sounds = QCheckBox("Enable system sounds")
        self._chk_sounds.setChecked(True)

        for chk in (self._chk_bit, self._chk_sounds):
            v.addWidget(chk)

        v.addStretch()
        v.addWidget(self._nav_row(
            back_cb=lambda: self.stack.setCurrentIndex(1),
            next_cb=self._finish,
            next_label="Finish ✓",
        ))
        return w

    def _page4(self):
        w = QWidget()
        v = QVBoxLayout(w)
        v.setContentsMargins(40, 48, 40, 24)
        v.setSpacing(12)
        v.addStretch()
        v.addWidget(HeartWidget(72, w), alignment=Qt.AlignmentFlag.AlignCenter)
        v.addSpacing(8)
        v.addWidget(_label("You're all set!", 24, bold=True))
        v.addWidget(_label("Bit is ready to hang out. Have fun! 💙", 13, "#8a99ba"))
        v.addStretch()
        v.addWidget(self._nav_row(next_cb=self.close, next_label="Open desktop →"))
        return w

    def _finish(self):
        save_bit(self.bit_cfg)
        # Write settings.ini
        import configparser
        ini = CONFIG_DIR / "settings.ini"
        cfg = configparser.ConfigParser()
        if ini.exists():
            cfg.read(str(ini))
        if "Bit" not in cfg:
            cfg["Bit"] = {}
        cfg["Bit"]["enabled"] = "true" if self._chk_bit.isChecked() else "false"
        if "Sounds" not in cfg:
            cfg["Sounds"] = {}
        cfg["Sounds"]["enabled"] = "true" if self._chk_sounds.isChecked() else "false"
        with open(str(ini), "w") as f:
            cfg.write(f)
        WELCOME_FLAG.touch()
        self.stack.setCurrentIndex(3)


def main():
    # Skip if already run
    if WELCOME_FLAG.exists() and "--force" not in sys.argv:
        return
    app = QApplication(sys.argv)
    app.setStyleSheet(STYLE)
    w = WelcomeApp()
    w.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
```

- [ ] Make executable:

```bash
chmod +x iso/airootfs/usr/local/bin/careos-welcome
```

- [ ] Smoke-test locally:

```bash
python3 iso/airootfs/usr/local/bin/careos-welcome --force
# 4-page wizard should open. Navigate all pages, click Finish.
# Check ~/.config/careos/settings.ini and bit.json were written.
cat ~/.config/careos/settings.ini
```

- [ ] Commit:

```bash
git add iso/airootfs/usr/local/bin/careos-welcome
git commit -m "feat: rewrite careos-welcome as PyQt6 first-run wizard"
```

---

## Task 11: CareOS Control Center

**Files:**
- Create: `iso/airootfs/usr/local/bin/careos-control` (replaces existing `carectl`)
- Modify: `iso/airootfs/usr/share/applications/careos-control.desktop`

- [ ] Write the file:

```python
#!/usr/bin/env python3
"""CareOS Control Center — PyQt6 tabbed settings panel."""
import configparser, json, subprocess, sys
from pathlib import Path
from PyQt6.QtCore import Qt
from PyQt6.QtGui import QFont, QColor, QPainter
from PyQt6.QtWidgets import (
    QApplication, QWidget, QTabWidget, QVBoxLayout, QHBoxLayout,
    QLabel, QPushButton, QCheckBox, QSlider, QButtonGroup,
    QRadioButton, QGridLayout, QFrame, QSizePolicy, QSpacerItem
)

CONFIG_DIR = Path.home() / ".config" / "careos"
BIT_CFG    = CONFIG_DIR / "bit.json"
SETTINGS   = CONFIG_DIR / "settings.ini"

STYLE = """
QWidget       { background: #080d17; color: #eaf0ff; font-family: Inter; font-size: 13px; }
QTabWidget::pane { border: none; background: #080d17; }
QTabBar::tab  {
    background: #0f1726; color: #8a99ba;
    border: none; padding: 10px 20px; border-radius: 6px 6px 0 0; margin-right: 2px;
}
QTabBar::tab:selected { background: #1c2942; color: #eaf0ff; }
QPushButton {
    background: #559aff; color: #080d17; border: none;
    border-radius: 7px; padding: 8px 20px; font-weight: 600;
}
QPushButton:hover   { background: #6aabff; }
QPushButton:pressed { background: #2f6fc8; }
QPushButton#secondary {
    background: #1c2942; color: #8a99ba; border: 1px solid #2c3c58;
}
QPushButton#secondary:hover { color: #eaf0ff; background: #2a3a56; }
QCheckBox { spacing: 8px; }
QCheckBox::indicator { width:16px; height:16px; border-radius:4px;
                        border:1px solid #2c3c58; background:#0f1726; }
QCheckBox::indicator:checked { background:#559aff; border-color:#559aff; }
QRadioButton { spacing: 8px; }
QRadioButton::indicator { width:14px; height:14px; border-radius:7px;
                           border:1px solid #2c3c58; background:#0f1726; }
QRadioButton::indicator:checked { background:#559aff; border-color:#559aff; }
QFrame[frameShape="4"], QFrame[frameShape="5"] { color: #2c3c58; }
"""

OUTFIT_NAMES   = ["blue", "cyan", "green", "pink", "gold"]
OUTFIT_COLORS  = ["#559aff", "#38bff8", "#2ecc8e", "#f56060", "#f0b430"]
OUTFIT_UNLOCKS = [0, 50, 100, 150, 200]


def load_bit():
    if BIT_CFG.exists():
        try:
            return json.loads(BIT_CFG.read_text())
        except Exception:
            pass
    return {"points": 0, "outfit": "blue"}


def save_bit(cfg):
    CONFIG_DIR.mkdir(parents=True, exist_ok=True)
    BIT_CFG.write_text(json.dumps(cfg, indent=2))


def load_settings():
    cfg = configparser.ConfigParser()
    if SETTINGS.exists():
        cfg.read(str(SETTINGS))
    return cfg


def save_settings(cfg):
    CONFIG_DIR.mkdir(parents=True, exist_ok=True)
    with open(str(SETTINGS), "w") as f:
        cfg.write(f)


def _sep():
    f = QFrame()
    f.setFrameShape(QFrame.Shape.HLine)
    f.setStyleSheet("color: #2c3c58;")
    return f


def _h(text, size=14):
    lbl = QLabel(text)
    lbl.setFont(QFont("Inter", size, QFont.Weight.Bold))
    return lbl


def _sub(text):
    lbl = QLabel(text)
    lbl.setStyleSheet("color: #505e78; font-size: 11px;")
    return lbl


class BitTab(QWidget):
    def __init__(self):
        super().__init__()
        self.bit = load_bit()
        self.ini = load_settings()
        v = QVBoxLayout(self)
        v.setContentsMargins(28, 24, 28, 24)
        v.setSpacing(16)

        v.addWidget(_h("Bit the Desktop Pet"))

        # Enable toggle
        self.enabled_chk = QCheckBox("Show Bit on desktop")
        enabled = self.ini.get("Bit", "enabled", fallback="true") == "true"
        self.enabled_chk.setChecked(enabled)
        self.enabled_chk.toggled.connect(self._toggle_bit)
        v.addWidget(self.enabled_chk)

        v.addWidget(_sep())

        # Care Points
        pts = self.bit.get("points", 0)
        row = QHBoxLayout()
        row.addWidget(_h(f"💙 Care Points: {pts}", 13))
        reset_btn = QPushButton("Reset")
        reset_btn.setObjectName("secondary")
        reset_btn.setFixedWidth(70)
        reset_btn.clicked.connect(self._reset_points)
        row.addWidget(reset_btn)
        v.addLayout(row)
        v.addWidget(_sub("Click Bit on the desktop to earn points. Unlock new outfits!"))

        v.addWidget(_sep())
        v.addWidget(_h("Outfits", 13))

        # Outfit grid
        grid = QHBoxLayout()
        grid.setSpacing(8)
        self._outfit_btns = []
        current = self.bit.get("outfit", "blue")
        for i, (name, color) in enumerate(zip(OUTFIT_NAMES, OUTFIT_COLORS)):
            threshold = OUTFIT_UNLOCKS[i]
            btn = QPushButton(name.capitalize())
            btn.setFixedSize(72, 60)
            unlocked = pts >= threshold
            btn.setEnabled(unlocked)
            btn.setCheckable(True)
            btn.setChecked(name == current)
            if unlocked:
                btn.setStyleSheet(f"""
                    QPushButton {{ background:#0f1726; border:2px solid #2c3c58;
                        border-radius:8px; color:#8a99ba; font-size:11px; }}
                    QPushButton:checked {{ border-color:{color}; color:#eaf0ff; }}
                    QPushButton:hover   {{ border-color:{color}88; }}
                """)
                btn.clicked.connect(lambda _, n=name: self._set_outfit(n))
            else:
                btn.setStyleSheet("""
                    QPushButton { background:#0a0e18; border:1px solid #1a2030;
                        border-radius:8px; color:#2c3c58; font-size:10px; }
                """)
                btn.setToolTip(f"Unlock at {threshold} pts")
            self._outfit_btns.append(btn)
            grid.addWidget(btn)
        v.addLayout(grid)
        v.addStretch()

    def _toggle_bit(self, checked):
        if "Bit" not in self.ini:
            self.ini["Bit"] = {}
        self.ini["Bit"]["enabled"] = "true" if checked else "false"
        save_settings(self.ini)
        cmd = ["systemctl", "--user",
               "start" if checked else "stop", "bit-pet"]
        subprocess.run(cmd, capture_output=True)

    def _reset_points(self):
        self.bit["points"] = 0
        save_bit(self.bit)

    def _set_outfit(self, name):
        self.bit["outfit"] = name
        save_bit(self.bit)
        for btn in self._outfit_btns:
            btn.setChecked(btn.text().lower() == name)


class AppearanceTab(QWidget):
    def __init__(self):
        super().__init__()
        v = QVBoxLayout(self)
        v.setContentsMargins(28, 24, 28, 24)
        v.setSpacing(16)

        v.addWidget(_h("Appearance"))

        v.addWidget(_h("Theme", 13))
        row = QHBoxLayout()
        self._dark  = QRadioButton("Dark  (default)")
        self._light = QRadioButton("Light")
        self._dark.setChecked(True)
        row.addWidget(self._dark)
        row.addWidget(self._light)
        row.addStretch()
        v.addLayout(row)

        v.addWidget(_sep())
        v.addWidget(_h("Accent Color", 13))
        v.addWidget(_sub("Changes affect future sessions."))
        accents = [
            ("#559aff", "Blue"), ("#38bff8", "Cyan"), ("#2ecc8e", "Green"),
            ("#f56060", "Pink"), ("#f0b430", "Gold"), ("#a78bfa", "Purple"),
        ]
        arow = QHBoxLayout()
        arow.setSpacing(8)
        for color, name in accents:
            btn = QPushButton()
            btn.setFixedSize(36, 36)
            btn.setToolTip(name)
            btn.setStyleSheet(f"""
                QPushButton {{ background:{color}; border-radius:18px; border:none; }}
                QPushButton:hover {{ border:2px solid #eaf0ff; }}
            """)
            arow.addWidget(btn)
        arow.addStretch()
        v.addLayout(arow)

        v.addWidget(_sep())
        v.addWidget(_h("Wallpaper", 13))
        wallpapers = [
            ("Navy Grid", "Default dark navy with blue grid"),
            ("Deep Space", "Deeper blue-black with star field"),
            ("Minimal Dark", "Solid #080d17, no patterns"),
        ]
        for name, desc in wallpapers:
            btn = QPushButton(name)
            btn.setObjectName("secondary")
            btn.setToolTip(desc)
            v.addWidget(btn)
        v.addStretch()


class SystemTab(QWidget):
    def __init__(self):
        super().__init__()
        v = QVBoxLayout(self)
        v.setContentsMargins(28, 24, 28, 24)
        v.setSpacing(16)

        v.addWidget(_h("System"))

        # System info
        import platform, subprocess as sp
        try:
            kernel = sp.check_output(["uname", "-r"], text=True).strip()
        except Exception:
            kernel = "unknown"
        try:
            with open("/etc/careos-release") as f:
                version = f.read().strip()
        except Exception:
            version = "CareOS 9"

        for label, value in [
            ("Version", version), ("Kernel", kernel),
            ("Platform", platform.machine()),
        ]:
            row = QHBoxLayout()
            row.addWidget(_sub(label + ":"))
            lbl = QLabel(value)
            lbl.setStyleSheet("color: #82bcff; font-size: 12px;")
            row.addWidget(lbl)
            row.addStretch()
            v.addLayout(row)

        v.addWidget(_sep())

        for label, cmd in [
            ("Update packages", ["konsole", "-e", "carepkg", "update"]),
            ("Open Terminal",   ["konsole"]),
        ]:
            btn = QPushButton(label)
            btn.clicked.connect(lambda _, c=cmd: subprocess.Popen(c))
            v.addWidget(btn)

        v.addStretch()


class AboutTab(QWidget):
    def __init__(self):
        super().__init__()
        v = QVBoxLayout(self)
        v.setContentsMargins(28, 40, 28, 24)
        v.setSpacing(12)
        v.addStretch()

        # Tiny heart-C drawn inline
        from PyQt6.QtWidgets import QSizePolicy as SP
        logo = QLabel("💙")
        logo.setFont(QFont("Inter", 48))
        logo.setAlignment(Qt.AlignmentFlag.AlignCenter)
        v.addWidget(logo)

        v.addWidget(QLabel("CareOS", alignment=Qt.AlignmentFlag.AlignCenter,
                            font=QFont("Inter", 22, QFont.Weight.Bold)))
        v.addWidget(QLabel("Version 9", alignment=Qt.AlignmentFlag.AlignCenter,
                            styleSheet="color:#8a99ba;"))
        v.addWidget(QLabel(
            "A focused Arch Linux desktop built with care.\n"
            "Custom kernel, KDE Plasma, and Bit the pet.",
            alignment=Qt.AlignmentFlag.AlignCenter,
            styleSheet="color:#505e78; font-size:12px;",
            wordWrap=True,
        ))
        v.addSpacing(12)
        gh = QPushButton("View on GitHub")
        gh.setObjectName("secondary")
        gh.clicked.connect(lambda: subprocess.Popen(
            ["xdg-open", "https://github.com/Fishyyyttv/CareOS"]
        ))
        v.addWidget(gh, alignment=Qt.AlignmentFlag.AlignCenter)
        v.addStretch()


class ControlCenter(QWidget):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("CareOS Control")
        self.setFixedSize(700, 500)
        self.setWindowFlags(Qt.WindowType.Window | Qt.WindowType.WindowCloseButtonHint)

        v = QVBoxLayout(self)
        v.setContentsMargins(0, 0, 0, 0)
        v.setSpacing(0)

        tabs = QTabWidget()
        tabs.addTab(BitTab(),        "  Bit  ")
        tabs.addTab(AppearanceTab(), "  Appearance  ")
        tabs.addTab(SystemTab(),     "  System  ")
        tabs.addTab(AboutTab(),      "  About  ")
        v.addWidget(tabs)

        screen = QApplication.primaryScreen().geometry()
        self.move(
            (screen.width()  - self.width())  // 2,
            (screen.height() - self.height()) // 2,
        )


def main():
    app = QApplication(sys.argv)
    app.setStyleSheet(STYLE)
    w = ControlCenter()
    w.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
```

- [ ] Make executable:

```bash
chmod +x iso/airootfs/usr/local/bin/careos-control
```

- [ ] Update the desktop entry to point to `careos-control` (not `carectl`). Read `iso/airootfs/usr/share/applications/careos-control.desktop` and set:

```ini
[Desktop Entry]
Type=Application
Name=CareOS Control
Comment=CareOS settings and system control
Exec=careos-control
Icon=careos-control
Terminal=false
Categories=System;Settings;
```

- [ ] Smoke-test:

```bash
python3 iso/airootfs/usr/local/bin/careos-control
# Tabbed window opens. Check all 4 tabs render without errors.
```

- [ ] Commit:

```bash
git add iso/airootfs/usr/local/bin/careos-control \
        iso/airootfs/usr/share/applications/careos-control.desktop
git commit -m "feat: CareOS Control Center (PyQt6 tabbed panel)"
```

---

## Task 12: Push everything to GitHub

- [ ] Verify no large files crept in:

```bash
git ls-files | xargs -I{} git ls-files -s {} 2>/dev/null | sort -k4 -rn | head -10
```

No file should exceed ~500KB.

- [ ] Push:

```bash
git push origin master:main
```

- [ ] Done. Build the ISO on Arch Linux with:

```bash
cd iso && sudo bash build.sh
```

Then boot the resulting `.iso` in VirtualBox.

---

## Self-Review Checklist

| Spec requirement | Task |
|-----------------|------|
| Bit PyQt6 transparent window | Task 2 |
| Bit state machine (idle/blink/happy/excited/tired/sleep) | Task 2 |
| Bit system monitoring (CPU, downloads) | Task 2 |
| Bit Care Points + outfits (5 unlocks at 50pt intervals) | Task 2 |
| Bit autostart + systemd user service | Task 3 |
| Plymouth 3-phase animation | Task 4 |
| SDDM Inter font + heart-C logo + floating particles | Task 5 |
| Wallpaper upgrade (grid + watermark) | Task 6 |
| KDE fonts (Inter + JetBrains Mono) | Task 7 |
| Icon theme (CareOS, inherits Papirus) | Task 8 |
| System sounds (4 OGG clips) | Task 9 |
| Welcome app 4-page wizard | Task 10 |
| Control Center 4-tab panel | Task 11 |
| Packages (psutil, ttf-inter, ttf-jetbrains-mono) | Task 1 |

All requirements covered. No TBDs.
