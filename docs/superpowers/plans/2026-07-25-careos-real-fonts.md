# CareOS Real Fonts Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the hardcoded 8×12 one-bit bitmap font with antialiased JetBrains Mono at five real sizes, switchable at runtime and regenerable from any `.ttf`.

**Architecture:** A Python tool (`tools/gen-font.py`) rasterizes a `.ttf` into committed C glyph tables with 8-bit coverage. The kernel gains a font registry (`gui/font.c`) whose active family drives the `FONT_W`/`FONT_H` globals; `gfx.c` blends glyph coverage through the existing `gfx_setpixel_blend()`. Preference persists in system settings and per-user records, both following existing versioned-migration patterns.

**Tech Stack:** C (freestanding x86-64, `-mno-sse` in kernel objects), NASM, GRUB2/xorriso, Python 3.12 + freetype-py 2.13.2 (dev machine only), QEMU + QMP for verification.

## Global Constraints

- **No floating point in kernel objects.** `KERN_CFLAGS` sets `-mno-mmx -mno-sse -mno-sse2`. All blending is integer arithmetic. A float in `gui/gfx.c` or `gui/font.c` fails the build.
- **Monospace only.** Every face must report one uniform advance. `gfx_str_width()` and ~120 layout sites depend on fixed advance.
- **ASCII 32–126 only.** Exactly 95 glyphs per face.
- **Exactly 5 faces per family**, ordered to match `font_size_t` in `gui/gui.h:155-160`: `FONT_CAPTION`=11px, `FONT_BODY`=13, `FONT_H3`=16, `FONT_H2`=20, `FONT_H1`=28.
- **`make` must never require Python or the `.ttf`.** Generated C is committed; the ISO must build with only gcc/ld/nasm/grub-mkrescue.
- **Cell-top positioning contract.** Callers pass `y` as the top of the text cell, never the baseline. Glyph pixel row 0 lands at `y + baseline - bearing_y`; column 0 at `x + bearing_x`.
- **Build command** (from WSL Ubuntu): `cd /mnt/d/Users/mhetm/Downloads/CareOS_v9_full/CareOS_v9 && make kernel/kernel.elf careos.iso`
- **Licensing.** JetBrains Mono is SIL OFL 1.1. `fonts/OFL.txt` must ship and generated files must carry attribution headers.

## Testing Approach

This project has **no unit-test framework for kernel code** — `tests/` holds ring-3 assembly binaries, not a harness. Therefore:

- **`tools/gen-font.py` gets real TDD** with pytest. Tests run on the dev machine.
- **Kernel changes are verified by build + boot + screenshot.** Each kernel task states the exact QEMU command and what must appear. Task 5 builds a reusable screenshot harness.

Baseline "before" screenshots of the current bitmap font exist from the 2026-07-25 black-screen investigation; capture fresh ones in Task 5 Step 1 if unavailable.

## File Structure

**Create:**
- `fonts/JetBrainsMono-Regular.ttf`, `fonts/JetBrainsMono-Bold.ttf` — committed source fonts
- `fonts/OFL.txt` — license
- `tools/requirements.txt` — `freetype-py==2.13.2`
- `tools/gen-font.py` — TTF → C converter
- `tools/classic_glyphs.py` — the existing 95×12 bitmap as Python data
- `tools/gen-classic.py` — emits the Classic family C file
- `tools/test_gen_font.py` — pytest suite
- `tools/qemu-shot.ps1` — boot + screenshot harness
- `gui/font.h` — data model + registry API
- `gui/font.c` — registry and active-family state
- `gui/fonts/font_jetbrains_mono.c`, `font_jetbrains_mono_bold.c`, `font_classic.c` — generated

**Modify:**
- `gui/gui.h` — `FONT_W`/`FONT_H` become variables; add `gfx_str_width_ex`
- `gui/gfx.c` — delete `font_data`, blend-based glyph rendering
- `Makefile` — add new sources to `C_SRC`
- `include/kernel.h` — `careos_settings_t.font_family`, `user_rec_t.font_pref`, declarations
- `kernel/settings.c` — v4 blob + migration
- `kernel/users.c` — `font_pref` + apply at login
- `apps/app_settings.c` — Font picker
- `apps/app_about.c` — typeface credit

---

### Task 1: Font acquisition, licensing, and Python toolchain

**Files:**
- Create: `fonts/JetBrainsMono-Regular.ttf`, `fonts/JetBrainsMono-Bold.ttf`, `fonts/OFL.txt`, `tools/requirements.txt`
- Modify: `.gitignore`

**Interfaces:**
- Consumes: nothing
- Produces: TTFs at `fonts/JetBrainsMono-{Regular,Bold}.ttf`; a venv at `tools/.venv` with `freetype-py`

- [ ] **Step 1: Download and extract JetBrains Mono v2.304**

Run from WSL:

```bash
cd /mnt/d/Users/mhetm/Downloads/CareOS_v9_full/CareOS_v9
mkdir -p fonts
curl -L -o /tmp/jbm.zip \
  https://github.com/JetBrains/JetBrainsMono/releases/download/v2.304/JetBrainsMono-2.304.zip
cd /tmp && unzip -o -q jbm.zip -d /tmp/jbm
cp /tmp/jbm/fonts/ttf/JetBrainsMono-Regular.ttf \
   /tmp/jbm/fonts/ttf/JetBrainsMono-Bold.ttf \
   /mnt/d/Users/mhetm/Downloads/CareOS_v9_full/CareOS_v9/fonts/
cp /tmp/jbm/OFL.txt /mnt/d/Users/mhetm/Downloads/CareOS_v9_full/CareOS_v9/fonts/OFL.txt
```

- [ ] **Step 2: Verify the fonts loaded and are monospace**

```bash
cd /mnt/d/Users/mhetm/Downloads/CareOS_v9_full/CareOS_v9
ls -l fonts/
```

Expected: both `.ttf` files present (~200 KB each) and `OFL.txt` present.

- [ ] **Step 3: Create the venv and pin the dependency**

Create `tools/requirements.txt`:

```
freetype-py==2.13.2
Pillow==10.4.0
```

Pillow is required only by `gen-font.py --preview`; freetype-py does the rasterizing.

Then:

```bash
cd /mnt/d/Users/mhetm/Downloads/CareOS_v9_full/CareOS_v9
python3 -m venv tools/.venv
tools/.venv/bin/pip install -q -r tools/requirements.txt pytest
tools/.venv/bin/python -c "import freetype; print('freetype-py', freetype.version())"
```

Expected: `freetype-py (2, 13, 2)`

- [ ] **Step 4: Confirm the font is monospace and read its metrics**

```bash
cd /mnt/d/Users/mhetm/Downloads/CareOS_v9_full/CareOS_v9
tools/.venv/bin/python - <<'PY'
import freetype
f = freetype.Face("fonts/JetBrainsMono-Regular.ttf")
f.set_pixel_sizes(0, 13)
advances = set()
for c in range(32, 127):
    f.load_char(chr(c), freetype.FT_LOAD_RENDER)
    advances.add(f.glyph.advance.x >> 6)
print("advances:", advances)
print("ascender:", f.size.ascender >> 6, "descender:", f.size.descender >> 6,
      "height:", f.size.height >> 6)
PY
```

Expected: `advances:` contains exactly one value (monospace confirmed). Record the ascender/height values — Task 2 uses them for `baseline` and `line_h`.

- [ ] **Step 5: Keep the venv out of git**

Append to `.gitignore`:

```
tools/.venv/
```

- [ ] **Step 6: Commit**

```bash
git add fonts/ tools/requirements.txt .gitignore
git commit -m "feat(fonts): vendor JetBrains Mono v2.304 (OFL 1.1) and pin font tooling"
```

---

### Task 2: `tools/gen-font.py` converter, test-first

**Files:**
- Create: `tools/gen-font.py`, `tools/test_gen_font.py`

**Interfaces:**
- Consumes: `fonts/JetBrainsMono-Regular.ttf` from Task 1
- Produces: module-level functions `build_face(ttf_path, px) -> Face` and `emit_c(families, out_path)`; a `Face` dataclass with fields `px, advance, line_h, baseline, glyphs, coverage`; a `Glyph` dataclass with `w, h, bearing_x, bearing_y, offset`. CLI: `gen-font.py TTF --name NAME --symbol SYM --sizes 11,13,16,20,28 --out FILE [--preview PNG]`

- [ ] **Step 1: Write the failing tests**

Create `tools/test_gen_font.py`:

```python
import subprocess, sys, pathlib
import pytest

sys.path.insert(0, str(pathlib.Path(__file__).parent))
import importlib
gen = importlib.import_module("gen-font")

TTF = pathlib.Path(__file__).parent.parent / "fonts" / "JetBrainsMono-Regular.ttf"
SIZES = [11, 13, 16, 20, 28]


def test_face_has_95_glyphs():
    face = gen.build_face(str(TTF), 13)
    assert len(face.glyphs) == 95


def test_advance_is_uniform_monospace():
    face = gen.build_face(str(TTF), 13)
    assert face.advance > 0


def test_coverage_length_matches_sum_of_glyph_areas():
    face = gen.build_face(str(TTF), 13)
    assert len(face.coverage) == sum(g.w * g.h for g in face.glyphs)


def test_every_glyph_offset_is_in_range():
    face = gen.build_face(str(TTF), 13)
    for g in face.glyphs:
        assert 0 <= g.offset
        assert g.offset + g.w * g.h <= len(face.coverage)


def test_baseline_fits_within_line_height():
    face = gen.build_face(str(TTF), 13)
    assert 0 < face.baseline <= face.line_h


def test_larger_size_has_larger_advance():
    assert gen.build_face(str(TTF), 28).advance > gen.build_face(str(TTF), 11).advance


def test_cli_requires_exactly_five_sizes(tmp_path):
    out = tmp_path / "f.c"
    r = subprocess.run(
        [sys.executable, str(pathlib.Path(__file__).parent / "gen-font.py"),
         str(TTF), "--name", "X", "--symbol", "x", "--sizes", "11,13", "--out", str(out)],
        capture_output=True, text=True)
    assert r.returncode != 0
    assert "exactly 5" in (r.stderr + r.stdout)


def test_cli_emits_expected_c_symbols(tmp_path):
    out = tmp_path / "font_x.c"
    r = subprocess.run(
        [sys.executable, str(pathlib.Path(__file__).parent / "gen-font.py"),
         str(TTF), "--name", "Test Face", "--symbol", "test_face",
         "--sizes", "11,13,16,20,28", "--out", str(out)],
        capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    src = out.read_text()
    assert "const font_family_t font_test_face" in src
    assert '"Test Face"' in src
    assert "OFL" in src           # attribution header required
    for px in SIZES:
        assert f"test_face_cov_{px}" in src
        assert f"test_face_glyphs_{px}" in src
```

- [ ] **Step 2: Run the tests to verify they fail**

```bash
cd /mnt/d/Users/mhetm/Downloads/CareOS_v9_full/CareOS_v9
tools/.venv/bin/pytest tools/test_gen_font.py -v
```

Expected: FAIL — `ModuleNotFoundError: No module named 'gen-font'`

- [ ] **Step 3: Implement the converter**

Create `tools/gen-font.py`:

```python
#!/usr/bin/env python3
"""Rasterize a TTF into CareOS C glyph tables.

Run once on the dev machine and commit the output, exactly like
tools/gen-sounds.py. The kernel build never invokes this script.

Usage:
  python3 tools/gen-font.py fonts/JetBrainsMono-Regular.ttf \
      --name "JetBrains Mono" --symbol jetbrains_mono \
      --sizes 11,13,16,20,28 --out gui/fonts/font_jetbrains_mono.c
"""
import argparse
import sys
from dataclasses import dataclass, field
from typing import List

import freetype

FIRST_CH, LAST_CH = 32, 126
GLYPH_COUNT = LAST_CH - FIRST_CH + 1  # 95


@dataclass
class Glyph:
    w: int
    h: int
    bearing_x: int
    bearing_y: int
    offset: int


@dataclass
class Face:
    px: int
    advance: int
    line_h: int
    baseline: int
    glyphs: List[Glyph] = field(default_factory=list)
    coverage: bytearray = field(default_factory=bytearray)


def build_face(ttf_path: str, px: int) -> Face:
    face = freetype.Face(ttf_path)
    face.set_pixel_sizes(0, px)

    baseline = face.size.ascender >> 6
    line_h = face.size.height >> 6
    if line_h <= 0:
        line_h = baseline - (face.size.descender >> 6)

    glyphs: List[Glyph] = []
    coverage = bytearray()
    advances = set()

    for code in range(FIRST_CH, LAST_CH + 1):
        face.load_char(chr(code), freetype.FT_LOAD_RENDER)
        slot = face.glyph
        bmp = slot.bitmap
        advances.add(slot.advance.x >> 6)

        w, h, pitch = bmp.width, bmp.rows, bmp.pitch
        offset = len(coverage)
        # bitmap.buffer is padded to `pitch` per row; copy the live columns only.
        for row in range(h):
            base = row * pitch
            coverage.extend(bmp.buffer[base:base + w])

        glyphs.append(Glyph(w, h, slot.bitmap_left, slot.bitmap_top, offset))

    if len(advances) != 1:
        sys.exit(f"error: {ttf_path} is not monospace at {px}px; advances={sorted(advances)}")

    return Face(px=px, advance=advances.pop(), line_h=line_h,
                baseline=baseline, glyphs=glyphs, coverage=coverage)


def _fits_u8(name, value):
    if not (0 <= value <= 255):
        sys.exit(f"error: {name}={value} does not fit in u8")


def emit_c(name: str, symbol: str, faces: List[Face], out_path: str, src_note: str) -> None:
    lines = []
    a = lines.append
    a("/* GENERATED by tools/gen-font.py -- do not edit by hand.")
    a(f" * Typeface: {name}")
    a(f" * Source:   {src_note}")
    a(" * JetBrains Mono is licensed under the SIL Open Font License 1.1.")
    a(" * See fonts/OFL.txt. */")
    a('#include "kernel.h"')
    a('#include "font.h"')
    a("")

    for f in faces:
        _fits_u8("advance", f.advance)
        _fits_u8("line_h", f.line_h)
        _fits_u8("baseline", f.baseline)

        a(f"static const u8 {symbol}_cov_{f.px}[] = {{")
        blob = bytes(f.coverage)
        for i in range(0, len(blob), 24):
            a("    " + ",".join(str(b) for b in blob[i:i + 24]) + ",")
        a("};")
        a("")
        a(f"static const glyph_t {symbol}_glyphs_{f.px}[{GLYPH_COUNT}] = {{")
        for g in f.glyphs:
            _fits_u8("glyph.w", g.w)
            _fits_u8("glyph.h", g.h)
            a(f"    {{ {g.w},{g.h},{g.bearing_x},{g.bearing_y},{g.offset} }},")
        a("};")
        a("")

    a(f"const font_family_t font_{symbol} = {{")
    a(f'    .name = "{name}",')
    a("    .faces = {")
    for f in faces:
        a(f"        {{ .px={f.px}, .advance={f.advance}, .line_h={f.line_h}, "
          f".baseline={f.baseline}, .glyphs={symbol}_glyphs_{f.px}, "
          f".coverage={symbol}_cov_{f.px} }},")
    a("    },")
    a("};")
    a("")

    with open(out_path, "w") as fh:
        fh.write("\n".join(lines))


def write_preview(ttf_path: str, faces: List[Face], png_path: str) -> None:
    """Dump a glyph sheet so output can be eyeballed before rebuilding."""
    from PIL import Image  # optional; only needed for --preview
    pad = 4
    width = max(f.advance for f in faces) * 32 + pad * 2
    height = sum(f.line_h * 3 + pad for f in faces) + pad
    img = Image.new("L", (width, height), 0)
    y = pad
    for f in faces:
        for i, g in enumerate(f.glyphs):
            col, row = i % 32, i // 32
            ox = pad + col * f.advance + g.bearing_x
            oy = y + row * f.line_h + (f.baseline - g.bearing_y)
            for gy in range(g.h):
                for gx in range(g.w):
                    v = f.coverage[g.offset + gy * g.w + gx]
                    px, py = ox + gx, oy + gy
                    if 0 <= px < width and 0 <= py < height:
                        img.putpixel((px, py), v)
        y += f.line_h * 3 + pad
    img.save(png_path)


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("ttf")
    p.add_argument("--name", required=True)
    p.add_argument("--symbol", required=True)
    p.add_argument("--sizes", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--preview")
    args = p.parse_args()

    sizes = [int(s) for s in args.sizes.split(",")]
    if len(sizes) != 5:
        sys.exit("error: --sizes must list exactly 5 sizes, one per font_size_t "
                 "(CAPTION,BODY,H3,H2,H1)")

    faces = [build_face(args.ttf, px) for px in sizes]
    emit_c(args.name, args.symbol, faces, args.out, args.ttf)
    total = sum(len(f.coverage) for f in faces)
    print(f"{args.out}: 5 faces, {total} coverage bytes")

    if args.preview:
        write_preview(args.ttf, faces, args.preview)
        print(f"{args.preview}: preview written")


if __name__ == "__main__":
    main()
```

- [ ] **Step 4: Run the tests to verify they pass**

```bash
cd /mnt/d/Users/mhetm/Downloads/CareOS_v9_full/CareOS_v9
tools/.venv/bin/pytest tools/test_gen_font.py -v
```

Expected: all 8 tests PASS.

- [ ] **Step 5: Commit**

```bash
git add tools/gen-font.py tools/test_gen_font.py
git commit -m "feat(fonts): add TTF-to-C glyph converter with tests"
```

---

### Task 3: Generate the three font families

**Files:**
- Create: `tools/classic_glyphs.py`, `tools/gen-classic.py`, `gui/fonts/font_jetbrains_mono.c`, `gui/fonts/font_jetbrains_mono_bold.c`, `gui/fonts/font_classic.c`

**Interfaces:**
- Consumes: `build_face`/`emit_c` from Task 2
- Produces: C symbols `font_jetbrains_mono`, `font_jetbrains_mono_bold`, `font_classic`, each of type `const font_family_t`

- [ ] **Step 1: Copy the existing bitmap glyphs into Python**

Create `tools/classic_glyphs.py` containing the 95-entry array copied verbatim from `font_data` in `gui/gfx.c:305-401`:

```python
"""The original CareOS 8x12 bitmap font, preserved so the "Classic" family
reproduces pre-JetBrains-Mono rendering pixel for pixel.

Copied verbatim from the font_data array in gui/gfx.c before it was removed.
Each entry is 12 rows of 8 bits, MSB = leftmost pixel.
"""
CLASSIC = [
    [0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00],  # 32
    [0x00,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00,0x18,0x18,0x00],  # 33 !
    # ... copy all 95 rows from gui/gfx.c:306-400 in order ...
]
assert len(CLASSIC) == 95, f"expected 95 glyphs, got {len(CLASSIC)}"
```

Copy every row from `gui/gfx.c:306` through `gui/gfx.c:400` inclusive. Do not retype them — transcribe mechanically and let the `assert` catch a miscount.

- [ ] **Step 2: Write the Classic generator**

Create `tools/gen-classic.py`:

```python
#!/usr/bin/env python3
"""Emit the Classic family: the original 8x12 bitmap, integer-scaled to the
five font_size_t slots using the same factors gfx_font_scale_for() used
(1x for CAPTION/BODY/H3, 2x for H2/H1). Coverage is 0 or 255 only, so it
flows through the same blended render path as the TrueType families.
"""
import importlib.util
import pathlib
import sys

HERE = pathlib.Path(__file__).parent
spec = importlib.util.spec_from_file_location("genfont", HERE / "gen-font.py")
gen = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gen)

from classic_glyphs import CLASSIC

# (nominal px label, integer scale) per font_size_t slot
SLOTS = [(11, 1), (13, 1), (16, 1), (20, 2), (28, 2)]
CELL_W, CELL_H = 8, 12


def build_scaled_face(scale: int, px_label: int) -> gen.Face:
    glyphs, coverage = [], bytearray()
    w, h = CELL_W * scale, CELL_H * scale
    for rows in CLASSIC:
        offset = len(coverage)
        for row in range(h):
            bits = rows[row // scale]
            for col in range(w):
                lit = (bits >> (7 - (col // scale))) & 1
                coverage.append(255 if lit else 0)
        # bearing_y = h places pixel row 0 at cell top, matching the old
        # renderer which drew rows 0..11 downward from y.
        glyphs.append(gen.Glyph(w, h, 0, h, offset))
    return gen.Face(px=px_label, advance=w, line_h=h + scale,
                    baseline=h, glyphs=glyphs, coverage=coverage)


def main() -> None:
    faces = [build_scaled_face(scale, px) for px, scale in SLOTS]
    out = str(HERE.parent / "gui" / "fonts" / "font_classic.c")
    gen.emit_c("Classic 8x12", "classic", faces, out,
               "original CareOS bitmap font (tools/classic_glyphs.py)")
    print(f"{out}: written")


if __name__ == "__main__":
    main()
```

- [ ] **Step 3: Generate all three families**

```bash
cd /mnt/d/Users/mhetm/Downloads/CareOS_v9_full/CareOS_v9
mkdir -p gui/fonts
tools/.venv/bin/python tools/gen-font.py fonts/JetBrainsMono-Regular.ttf \
  --name "JetBrains Mono" --symbol jetbrains_mono \
  --sizes 11,13,16,20,28 --out gui/fonts/font_jetbrains_mono.c
tools/.venv/bin/python tools/gen-font.py fonts/JetBrainsMono-Bold.ttf \
  --name "JetBrains Mono Bold" --symbol jetbrains_mono_bold \
  --sizes 11,13,16,20,28 --out gui/fonts/font_jetbrains_mono_bold.c
tools/.venv/bin/python tools/gen-classic.py
```

Expected: three files written, each printing its coverage byte count.

- [ ] **Step 4: Record the BODY metrics**

```bash
cd /mnt/d/Users/mhetm/Downloads/CareOS_v9_full/CareOS_v9
grep -n "\.px=13" gui/fonts/font_jetbrains_mono.c
```

Expected: one line showing `.advance=` and `.line_h=`. **Write these two numbers down** — Task 6 needs them to reason about vertical reflow. The spec predicts advance ≈ 8 and line_h ≈ 17.

- [ ] **Step 5: Sanity-check total size**

```bash
cd /mnt/d/Users/mhetm/Downloads/CareOS_v9_full/CareOS_v9
du -ch gui/fonts/*.c | tail -1
```

Expected: a few hundred KB of C source. If any single file exceeds ~2 MB, a size was mistyped.

- [ ] **Step 6: Commit**

```bash
git add tools/classic_glyphs.py tools/gen-classic.py gui/fonts/
git commit -m "feat(fonts): generate JetBrains Mono Regular/Bold and Classic glyph tables"
```

---

### Task 4: Font data model, registry, and build wiring

**Files:**
- Create: `gui/font.h`, `gui/font.c`
- Modify: `Makefile:98-105` (the `gui/` block of `C_SRC`)

**Interfaces:**
- Consumes: `font_jetbrains_mono`, `font_jetbrains_mono_bold`, `font_classic` from Task 3
- Produces: `glyph_t`, `font_face_t`, `font_family_t` types; `font_init(void)`, `font_registry_count(void) -> u32`, `font_registry_name(u32) -> const char *`, `font_set_family(u32)`, `font_active_index(void) -> u32`, `font_face(font_size_t) -> const font_face_t *`, `font_bold_face(font_size_t) -> const font_face_t *`

- [ ] **Step 1: Create the header**

Create `gui/font.h`:

```c
/* CareOS gui/font.h -- font data model and registry.
 * Glyph tables are generated by tools/gen-font.py and committed. */
#ifndef CAREOS_FONT_H
#define CAREOS_FONT_H

#include "kernel.h"

#define FONT_FIRST_CH 32
#define FONT_LAST_CH  126
#define FONT_GLYPHS   95
#define FONT_FACES    5   /* one per font_size_t */

typedef struct {
    u8  w, h;
    i8  bearing_x;
    i8  bearing_y;
    u32 offset;
} glyph_t;

typedef struct {
    u8             px;
    u8             advance;
    u8             line_h;
    u8             baseline;
    const glyph_t *glyphs;
    const u8      *coverage;
} font_face_t;

typedef struct {
    const char        *name;
    const font_face_t  faces[FONT_FACES];
} font_family_t;

/* font_size_t lives in gui.h; forward-declare the enum tag usage via u32 to
 * avoid a circular include. Callers pass font_size_t values directly. */
void               font_init(void);
u32                font_registry_count(void);
const char        *font_registry_name(u32 index);
void               font_set_family(u32 index);
u32                font_active_index(void);
const font_face_t *font_face_at(u32 size_index);
const font_face_t *font_bold_face_at(u32 size_index);

#endif /* CAREOS_FONT_H */
```

- [ ] **Step 2: Create the registry**

Create `gui/font.c`:

```c
/* CareOS gui/font.c -- active font family and registry. */
#include "kernel.h"
#include "font.h"
#include "gui.h"

extern const font_family_t font_jetbrains_mono;
extern const font_family_t font_jetbrains_mono_bold;
extern const font_family_t font_classic;

static const font_family_t *registry[] = {
    &font_jetbrains_mono,
    &font_jetbrains_mono_bold,
    &font_classic,
};
#define REGISTRY_COUNT (sizeof(registry) / sizeof(registry[0]))

static u32 active_index = 0;

u32 font_registry_count(void) { return (u32)REGISTRY_COUNT; }
u32 font_active_index(void)   { return active_index; }

const char *font_registry_name(u32 index) {
    if (index >= REGISTRY_COUNT) return "";
    return registry[index]->name;
}

const font_face_t *font_face_at(u32 size_index) {
    if (size_index >= FONT_FACES) size_index = FONT_BODY;
    return &registry[active_index]->faces[size_index];
}

/* Bold always resolves to the Bold family regardless of the active choice, so
 * gfx_str_bold() renders real weight rather than a synthetic one. */
const font_face_t *font_bold_face_at(u32 size_index) {
    if (size_index >= FONT_FACES) size_index = FONT_BODY;
    return &font_jetbrains_mono_bold.faces[size_index];
}

void font_set_family(u32 index) {
    if (index >= REGISTRY_COUNT) return;
    active_index = index;
    const font_face_t *body = &registry[active_index]->faces[FONT_BODY];
    GFX_FONT_W = body->advance;
    GFX_FONT_H = body->line_h;
}

void font_init(void) { font_set_family(0); }
```

- [ ] **Step 3: Wire the new sources into the build**

In `Makefile`, inside the `C_SRC` list, add these four lines immediately after `gui/gfx.c` (line 98):

```make
             gui/font.c            \
             gui/fonts/font_jetbrains_mono.c \
             gui/fonts/font_jetbrains_mono_bold.c \
             gui/fonts/font_classic.c \
```

Also add `-Igui` is already present in `KERN_CFLAGS`, so `#include "font.h"` resolves with no further change.

- [ ] **Step 4: Declare the metric globals (temporary shim so this task builds standalone)**

In `gui/gui.h`, immediately after the existing `extern u32 GFX_FONT_SCALE;` at line 16, add:

```c
extern u32 GFX_FONT_W;   /* active family's BODY advance  */
extern u32 GFX_FONT_H;   /* active family's BODY line height */
```

In `gui/gfx.c`, next to the existing `u32 GFX_FONT_SCALE = 1;` at line 13, add:

```c
u32 GFX_FONT_W = 8;
u32 GFX_FONT_H = 13;
```

`FONT_W`/`FONT_H` still expand to the literals `8`/`13` at this point — Task 5 flips them. This task only proves the registry compiles and links.

- [ ] **Step 5: Build**

```bash
cd /mnt/d/Users/mhetm/Downloads/CareOS_v9_full/CareOS_v9
make kernel/kernel.elf careos.iso
```

Expected: clean build, no warnings about the new files. A float slipping into `font.c` would fail here under `-mno-sse`.

- [ ] **Step 6: Commit**

```bash
git add gui/font.h gui/font.c gui/gui.h gui/gfx.c Makefile
git commit -m "feat(fonts): add font data model, registry, and build wiring"
```

---

### Task 5: Switch the renderer to antialiased glyphs

**Files:**
- Modify: `gui/gui.h:162-163` (FONT_W/FONT_H), `gui/gfx.c:304-401` (delete `font_data`), `gui/gfx.c:403-475` (rendering)
- Create: `tools/qemu-shot.ps1`

**Interfaces:**
- Consumes: `font_face_at()`, `font_bold_face_at()`, `font_init()` from Task 4
- Produces: `gfx_str_width_ex(const char *s, font_size_t size) -> i32`; `gfx_str_bold()` now defined

- [ ] **Step 1: Capture "before" screenshots**

Create `tools/qemu-shot.ps1`:

```powershell
# Boot careos.iso headless and screenshot at given elapsed seconds.
#   tools\qemu-shot.ps1 -Shots 14,30 -Prefix before
param([int[]]$Shots = @(14), [string]$Prefix = "shot", [string]$OutDir = "shots")

Add-Type -AssemblyName System.Drawing
$qemu = "C:\Program Files\qemu\qemu-system-x86_64.exe"
$root = Split-Path -Parent $PSScriptRoot
New-Item -ItemType Directory -Force -Path "$root\$OutDir" | Out-Null

$p = Start-Process -FilePath $qemu -PassThru -ArgumentList @(
  "-m","4096M","-smp","4","-cdrom","$root\careos.iso","-no-reboot",
  "-serial","file:$root\$OutDir\$Prefix.log","-vga","std",
  "-machine","pc,usb=off","-display","none",
  "-qmp","tcp:127.0.0.1:4499,server,nowait")

Start-Sleep -Seconds 6
$cli = New-Object System.Net.Sockets.TcpClient("127.0.0.1",4499)
$ns = $cli.GetStream()
$sr = New-Object System.IO.StreamReader($ns)
$sw = New-Object System.IO.StreamWriter($ns); $sw.AutoFlush = $true
$sr.ReadLine() | Out-Null
$sw.WriteLine('{"execute":"qmp_capabilities"}'); $sr.ReadLine() | Out-Null

function Invoke-Qmp([string]$j) {
  $sw.WriteLine($j); $x = $sr.ReadLine()
  while ($x -match '"event"') { $x = $sr.ReadLine() }
  return $x
}

$elapsed = 6
foreach ($t in $Shots) {
  if ($t -gt $elapsed) { Start-Sleep -Seconds ($t - $elapsed); $elapsed = $t }
  $ppm = "$root\$OutDir\$Prefix-$t.ppm"
  Invoke-Qmp ('{"execute":"screendump","arguments":{"filename":"' + ($ppm -replace '\\','\\') + '"}}') | Out-Null
  # PPM (P6) -> PNG
  $b = [System.IO.File]::ReadAllBytes($ppm)
  $pos = 2; $tok = @()
  while ($tok.Count -lt 3) {
    while ([char]$b[$pos] -match '\s') { $pos++ }
    $s = ""; while (-not ([char]$b[$pos] -match '\s')) { $s += [char]$b[$pos]; $pos++ }
    $tok += $s
  }
  $pos++
  $w = [int]$tok[0]; $h = [int]$tok[1]
  $bmp = New-Object System.Drawing.Bitmap($w,$h,[System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
  $d = $bmp.LockBits((New-Object System.Drawing.Rectangle(0,0,$w,$h)),
        [System.Drawing.Imaging.ImageLockMode]::WriteOnly,
        [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
  $row = New-Object byte[] $d.Stride
  for ($y=0; $y -lt $h; $y++) {
    $src = $pos + $y*$w*3
    for ($x=0; $x -lt $w; $x++) {
      $row[$x*3]=$b[$src+$x*3+2]; $row[$x*3+1]=$b[$src+$x*3+1]; $row[$x*3+2]=$b[$src+$x*3]
    }
    [System.Runtime.InteropServices.Marshal]::Copy($row,0,[IntPtr]::Add($d.Scan0,$y*$d.Stride),$d.Stride)
  }
  $bmp.UnlockBits($d)
  $bmp.Save("$root\$OutDir\$Prefix-$t.png",[System.Drawing.Imaging.ImageFormat]::Png)
  $bmp.Dispose(); Remove-Item $ppm -Force
  Write-Output "wrote $OutDir\$Prefix-$t.png"
}
$cli.Close(); Stop-Process -Id $p.Id -Force
```

Run it against the current build:

```powershell
cd D:\Users\mhetm\Downloads\CareOS_v9_full\CareOS_v9
powershell -File tools\qemu-shot.ps1 -Shots 14 -Prefix before
```

Expected: `shots\before-14.png` showing the login screen in the old chunky font.

- [ ] **Step 2: Make FONT_W/FONT_H resolve to the active font**

In `gui/gui.h`, replace lines 162-164:

```c
#define FONT_W       8
#define FONT_H      13
#define FONT_SPACING 3
```

with:

```c
/* Active font metrics. Variables, not constants -- font_set_family() updates
 * them. No call site uses these in a constant expression, so every existing
 * user keeps compiling unchanged. */
#define FONT_W  GFX_FONT_W
#define FONT_H  GFX_FONT_H
```

`FONT_SPACING` is deleted: it is defined here and referenced nowhere in the tree.

Add the new width accessor near the other `gfx_str_*` declarations:

```c
i32  gfx_str_width_ex(const char *s, font_size_t size);
```

- [ ] **Step 3: Delete the old glyph table**

In `gui/gfx.c`, delete the comment and array spanning lines 304-401 (`/* -- New High-Fidelity 8x12 Sans-Serif Font ... */` through the closing `};`). Add `#include "font.h"` next to the existing includes at the top.

- [ ] **Step 4: Rewrite glyph rendering**

Replace `gfx_char`, `gfx_font_scale_for`, `gfx_str_ex`, and `gfx_str_width` in `gui/gfx.c` with:

```c
/* Blit one glyph. (x,y) is the TOP-LEFT of the text cell, never the baseline;
 * every existing caller depends on that. */
static void glyph_blit(const font_face_t *f, const glyph_t *g,
                       i32 x, i32 y, u32 fg, i32 sc) {
    i32 ox = x + (i32)g->bearing_x * sc;
    i32 oy = y + ((i32)f->baseline - (i32)g->bearing_y) * sc;
    for (i32 row = 0; row < (i32)g->h; row++) {
        const u8 *src = &f->coverage[g->offset + (u32)row * g->w];
        for (i32 col = 0; col < (i32)g->w; col++) {
            u8 a = src[col];
            if (!a) continue;
            for (i32 sy = 0; sy < sc; sy++)
                for (i32 sx = 0; sx < sc; sx++)
                    gfx_setpixel_blend(ox + col*sc + sx, oy + row*sc + sy, fg, a);
        }
    }
}

static void draw_text(i32 x, i32 y, const char *s, u32 fg, u32 bg,
                      font_size_t size, bool bold) {
    const font_face_t *f = bold ? font_bold_face_at((u32)size)
                                : font_face_at((u32)size);
    i32 sc = (i32)GFX_FONT_SCALE;
    i32 adv = (i32)f->advance * sc;
    i32 lh  = (i32)f->line_h  * sc;
    i32 cx = x, cy = y, start_x = x, start_y = y, max_x = x;

    while (*s) {
        if (*s == '\n') { if (cx > max_x) max_x = cx; cx = x; cy += lh; s++; continue; }
        if (*s >= FONT_FIRST_CH && *s <= FONT_LAST_CH) {
            if (bg != COL_TRANSPARENT) gfx_rect(cx, cy, adv, lh, bg);
            glyph_blit(f, &f->glyphs[(u8)*s - FONT_FIRST_CH], cx, cy, fg, sc);
        }
        cx += adv; s++;
    }
    if (cx > max_x) max_x = cx;
    gfx_dirty(start_x, start_y, max_x - start_x, (cy - start_y) + lh);
}

void gfx_char(i32 x, i32 y, char c, u32 fg, u32 bg) {
    char buf[2] = { c, '\0' };
    draw_text(x, y, buf, fg, bg, FONT_BODY, false);
}

void gfx_str_ex(i32 x, i32 y, const char *s, u32 fg, u32 bg, font_size_t size) {
    draw_text(x, y, s, fg, bg, size, false);
}

void gfx_str_bold(i32 x, i32 y, const char *s, u32 fg, u32 bg) {
    draw_text(x, y, s, fg, bg, FONT_BODY, true);
}

i32 gfx_str_width_ex(const char *s, font_size_t size) {
    return (i32)(kstrlen(s) * font_face_at((u32)size)->advance * GFX_FONT_SCALE);
}

i32 gfx_str_width(const char *s) { return gfx_str_width_ex(s, FONT_BODY); }
```

Then update `gfx_str_centered_ex` to measure with the real per-size advance:

```c
void gfx_str_centered_ex(i32 x, i32 y, i32 w, const char *s, u32 fg, u32 bg, font_size_t size) {
    gfx_str_ex(x + (w - gfx_str_width_ex(s, size)) / 2, y, s, fg, bg, size);
}
```

And `gfx_str_clipped` to use the active advance:

```c
void gfx_str_clipped(i32 x, i32 y, i32 maxw, const char *s, u32 fg, u32 bg) {
    i32 cw = (i32)font_face_at((u32)FONT_BODY)->advance * (i32)GFX_FONT_SCALE;
    if (cw <= 0) return;
    int maxchars = maxw / cw;
    i32 px = x;
    for (int i = 0; s[i] && i < maxchars; i++) { gfx_char(px, y, s[i], fg, bg); px += cw; }
}
```

- [ ] **Step 5: Initialise the font before first use**

In `gui/gfx.c`'s `gfx_init()` (line 126), add `font_init();` as the first statement, before the existing `GFX_FONT_SCALE` assignment at line 129 — so `GFX_FONT_W`/`GFX_FONT_H` hold real values before anything draws.

- [ ] **Step 6: Build**

```bash
cd /mnt/d/Users/mhetm/Downloads/CareOS_v9_full/CareOS_v9
make kernel/kernel.elf careos.iso
```

Expected: clean build. Undefined references to `font_data` mean Step 3 missed a use.

- [ ] **Step 7: Capture "after" screenshots and compare**

```powershell
cd D:\Users\mhetm\Downloads\CareOS_v9_full\CareOS_v9
powershell -File tools\qemu-shot.ps1 -Shots 14 -Prefix after
```

Expected: `shots\after-14.png` shows the login screen in smooth, antialiased JetBrains Mono. Open `before-14.png` and `after-14.png` side by side. Text must be legible and correctly positioned — some vertical crowding is expected and is Task 6's job.

- [ ] **Step 8: Commit**

```bash
git add gui/gui.h gui/gfx.c tools/qemu-shot.ps1
git commit -m "feat(fonts): render antialiased glyphs from the active font family"
```

---

### Task 6: Repair vertical reflow

**Files:**
- Modify: layout constants in `apps/app_terminal.c`, `apps/app_files.c`, `apps/app_sysmon.c`, `apps/app_editor.c`, and any other app showing collisions

**Interfaces:**
- Consumes: the renderer from Task 5
- Produces: no new API

BODY line height grows from 13 to roughly 17 (confirm against the number recorded in Task 3 Step 4). Advance stays at 8, so horizontal layout is unchanged. Fixed-height rows are where text will collide.

- [ ] **Step 1: Screenshot the text-dense apps**

The desktop autostarts Terminal, so a boot screenshot covers it. For the others, drive the launcher via QMP or click through manually in a windowed QEMU:

```powershell
cd D:\Users\mhetm\Downloads\CareOS_v9_full\CareOS_v9
& "C:\Program Files\qemu\qemu-system-x86_64.exe" -m 4096M -smp 4 -cdrom careos.iso `
  -vga std -machine pc,usb=off -display sdl -no-reboot
```

Log in as `user` / `CareOS123` (set a new password when prompted), then open Terminal, Files, Monitor and Editor in turn.

- [ ] **Step 2: List every collision, and find the literals behind them**

For each app, note rows that overlap, text clipped at a panel edge, or scrollback showing fewer lines than the window fits. Write the list down before editing anything — fixing blind causes churn.

Then locate the hardcoded row pitches. Any small integer literal near a `_y`, `row`, `line` or `+= ` in a drawing loop is a candidate:

```bash
cd /mnt/d/Users/mhetm/Downloads/CareOS_v9_full/CareOS_v9
grep -nE '(\*|\+=) *1[0-9]\b' apps/*.c | grep -viE 'FONT_H|GFX_FONT_SCALE'
grep -nE '/ *1[0-9]\b' apps/*.c | grep -viE 'FONT_H|GFX_FONT_SCALE'
```

The first command finds row-stepping literals, the second finds visible-row-count divisions. Cross-reference the hits against the collisions observed in the running system — only change literals that are genuinely line pitches, not padding or widths.

- [ ] **Step 3: Fix each collision at its source**

Replace hardcoded row pitches with `FONT_H`-derived values. The pattern to apply, using `apps/app_terminal.c` as the example — a line-stepping loop written as a literal:

```c
/* before */
i32 line_y = top + i * 14;

/* after */
i32 line_y = top + i * (i32)(FONT_H * GFX_FONT_SCALE);
```

Do the same for visible-row counts, which must divide by the live line height rather than a literal:

```c
/* before */
int visible_rows = panel_h / 14;

/* after */
int visible_rows = panel_h / (i32)(FONT_H * GFX_FONT_SCALE);
```

Apply this transformation to every literal row pitch found in Step 2. Do not widen panels to hide overlap — derive the pitch so the layout tracks whatever font is active.

- [ ] **Step 4: Rebuild and re-verify**

```bash
cd /mnt/d/Users/mhetm/Downloads/CareOS_v9_full/CareOS_v9
make kernel/kernel.elf careos.iso
```

Repeat Step 1 and confirm every collision from Step 2 is resolved.

- [ ] **Step 5: Commit**

```bash
git add apps/
git commit -m "fix(gui): derive row pitch from live font metrics"
```

---

### Task 7: Persist the font choice

**Files:**
- Modify: `include/kernel.h:610-622` (`careos_settings_t`), `include/kernel.h:405-421` (`user_rec_t`), `kernel/settings.c`, `kernel/users.c`

**Interfaces:**
- Consumes: `font_set_family()`, `font_registry_count()` from Task 4
- Produces: `settings_set_font_family(u32)`, `user_set_current_font_preference(u32)`; `careos_settings_t.font_family`; `USER_FONT_SYSTEM_DEFAULT`

- [ ] **Step 1: Extend the settings struct**

In `include/kernel.h`, add to `careos_settings_t` after `vesa_h` (line 621):

```c
    u32  font_family;
```

And declare next to the other setters (near line 626):

```c
void settings_set_font_family(u32 index);
```

- [ ] **Step 2: Add the v4 blob and migration**

In `kernel/settings.c`, bump the version at line 4:

```c
#define SETTINGS_VERSION  4u
```

Add `settings_blob_v4_t` as a copy of `settings_blob_v3_t` with `u32 font_family;` appended as its last field. Add `settings_load_v4()` mirroring `settings_load_v3()` plus the new field, and change the save path at lines 101-103 to write `settings_blob_v4_t`.

In the load dispatcher around lines 211-233, insert the v4 case first and make v3 migrate forward:

```c
    if (version == SETTINGS_VERSION && settings_load_v4((const settings_blob_v4_t*)settings_io)) {
        serial_write("[settings] loaded v4 settings from disk\n");
        return true;
    }
    if (version == 3u && settings_load_v3((const settings_blob_v3_t*)settings_io)) {
        g_settings.font_family = 0u;   /* default to JetBrains Mono */
        serial_write("[settings] migrated v3 settings to v4\n");
        return true;
    }
```

Leave the existing v2 and v1 branches in place, and have each also set `font_family = 0u`.

Implement the setter:

```c
void settings_set_font_family(u32 index) {
    if (index >= font_registry_count()) return;
    g_settings.font_family = index;
    font_set_family(index);
    settings_save();
}
```

`kernel/settings.c` must `#include "font.h"` for those two calls.

- [ ] **Step 3: Add the per-user preference**

In `include/kernel.h`, add to `user_rec_t` after `theme_pref` (line 417):

```c
    u32  font_pref;
```

Add near `user_set_current_theme_preference` (line 443):

```c
#define USER_FONT_SYSTEM_DEFAULT 0xFFFFFFFFu
void        user_set_current_font_preference(u32 index);
```

In `kernel/users.c`, add `font_pref` to the in-memory record and to the on-disk entry struct, following exactly how `theme_pref` is handled. In each older-version load path that currently defaults `theme_pref` (lines 458 and 496), also set:

```c
            u->font_pref = USER_FONT_SYSTEM_DEFAULT;
```

Copy it in the save loop next to line 548, and default it for new accounts next to line 619.

- [ ] **Step 4: Apply the preference at login**

In `kernel/users.c`, directly after the existing theme application at lines 690-691:

```c
    if (u->font_pref != USER_FONT_SYSTEM_DEFAULT)
        settings_set_font_family(u->font_pref);
```

And implement the setter next to `user_set_current_theme_preference` (line 926):

```c
void user_set_current_font_preference(u32 index) {
    user_rec_t *u = find_user_by_uid(current_uid);
    if (!u) return;
    u->font_pref = (index < font_registry_count()) ? index : USER_FONT_SYSTEM_DEFAULT;
    users_persist_save();
}
```

- [ ] **Step 5: Apply the saved system setting at boot**

In `gui/font.c`, change `font_init()` to honour persisted settings:

```c
void font_init(void) {
    const careos_settings_t *s = settings_get();
    font_set_family(s->font_family < REGISTRY_COUNT ? s->font_family : 0u);
}
```

- [ ] **Step 6: Build and verify the migration**

The existing `careos.img` carries a v3 settings blob, which exercises the migration path:

```bash
cd /mnt/d/Users/mhetm/Downloads/CareOS_v9_full/CareOS_v9
make kernel/kernel.elf careos.iso
```

Then boot against a **throwaway overlay** so the real disk image is not written:

```powershell
cd D:\Users\mhetm\Downloads\CareOS_v9_full\CareOS_v9
& "C:\Program Files\qemu\qemu-img.exe" create -f qcow2 -b careos.img -F raw shots\ov.qcow2
& "C:\Program Files\qemu\qemu-system-x86_64.exe" -m 4096M -smp 4 -cdrom careos.iso `
  -drive file=shots\ov.qcow2,format=qcow2,if=ide,index=0 `
  -serial file:shots\mig.log -vga std -machine pc,usb=off -display none -no-reboot
```

Let it run 30 seconds, stop it, then check:

```powershell
Select-String -Path shots\mig.log -Pattern "settings"
```

Expected: `[settings] migrated v3 settings to v4`. On a second boot of the same overlay, expect `[settings] loaded v4 settings from disk`.

- [ ] **Step 7: Commit**

```bash
git add include/kernel.h kernel/settings.c kernel/users.c gui/font.c
git commit -m "feat(fonts): persist font choice in settings v4 and per-user records"
```

---

### Task 8: Settings UI picker and About credit

**Files:**
- Modify: `apps/app_settings.c` (near the theme buttons at lines 396-402), `apps/app_about.c`

**Interfaces:**
- Consumes: `font_registry_count()`, `font_registry_name()`, `settings_set_font_family()`, `user_set_current_font_preference()`
- Produces: no new API

- [ ] **Step 1: Add the Font section**

In `apps/app_settings.c`, add `#include "font.h"`, then render one button per registry entry using the file's own `settings_button()` helper (defined at `app_settings.c:19`) exactly as the theme buttons do. Note `button_draw()` takes a **single** argument — active state is carried inside `button_t` by `settings_button()`:

```c
    gfx_str_ex(px, fy, "Font", COL_TEXT, COL_TRANSPARENT, FONT_H3);
    fy += (i32)(FONT_H * GFX_FONT_SCALE) * 2;
    for (u32 i = 0; i < font_registry_count(); i++) {
        bool is_active = (i == font_active_index());
        button_t b = settings_button(rect_make(px, fy, 240, 32),
                                     font_registry_name(i), is_active,
                                     is_active ? COL_PRIMARY : COL_SURFACE,
                                     is_active ? COL_WHITE : COL_TEXT);
        button_draw(&b);
        if (button_take_click(&b, mouse)) {
            settings_set_font_family(i);
            user_set_current_font_preference(i);
            gfx_dirty(0, 0, (i32)SCREEN_W, (i32)SCREEN_H);
        }
        fy += 40;
    }
```

If `COL_SURFACE` is not the colour token this file uses for inactive buttons, use whichever token the neighbouring theme buttons pass to `settings_button()`.

- [ ] **Step 2: Credit the typeface**

In `apps/app_about.c`, add a line to the existing information block:

```c
    "Typeface: JetBrains Mono, SIL Open Font License 1.1",
```

- [ ] **Step 3: Build**

```bash
cd /mnt/d/Users/mhetm/Downloads/CareOS_v9_full/CareOS_v9
make kernel/kernel.elf careos.iso
```

- [ ] **Step 4: Verify live switching and persistence**

Boot windowed, log in, open Settings, and click each font in turn:

```powershell
cd D:\Users\mhetm\Downloads\CareOS_v9_full\CareOS_v9
& "C:\Program Files\qemu\qemu-system-x86_64.exe" -m 4096M -smp 4 -cdrom careos.iso `
  -drive file=shots\ov.qcow2,format=qcow2,if=ide,index=0 `
  -vga std -machine pc,usb=off -display sdl -no-reboot
```

Expected, in order:
1. Selecting **Classic 8x12** immediately reverts all text to the original blocky font.
2. Selecting **JetBrains Mono** immediately restores smooth text.
3. Open About and confirm the OFL credit line renders.
4. Shut down, boot the same overlay again, and confirm the last-selected font is still active.

- [ ] **Step 5: Commit**

```bash
git add apps/app_settings.c apps/app_about.c
git commit -m "feat(fonts): add Settings font picker and About typeface credit"
```

---

## Definition of Done

- `make kernel/kernel.elf careos.iso` builds clean with `-mno-sse` intact.
- Login, desktop and Terminal render antialiased JetBrains Mono with no clipped or overlapping rows.
- Settings switches between JetBrains Mono, JetBrains Mono Bold and Classic 8x12 live.
- The choice survives a reboot, per user.
- A v3 settings blob migrates to v4 without data loss.
- `fonts/OFL.txt` ships and About credits the typeface.
- `tools/.venv/bin/pytest tools/test_gen_font.py` passes.
- Regenerating a font is one command against any `.ttf`.
