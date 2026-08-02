# CareOS Real Fonts — Design

**Date:** 2026-07-25
**Status:** Approved, pending implementation plan

## Problem

CareOS renders all text from a single hardcoded bitmap font: `font_data[95][12]` in
`gui/gfx.c:305`. It is 8×12, one bit per pixel, ASCII 32–126 only, with no
antialiasing (`bool lit` selects foreground or background outright). This is why
UI text reads as chunky and aliased at 1920×1080.

The five `font_size_t` values are largely decorative. `gfx_font_scale_for()`
doubles the integer scale for `FONT_H1` and `FONT_H2` only, so `FONT_CAPTION`,
`FONT_BODY` and `FONT_H3` all rasterize identically, and H1/H2 are just
pixel-doubled versions of the same 8×12 glyphs rather than genuinely larger type.

Goal: render real JetBrains Mono with antialiasing at genuine per-size metrics,
let the user switch fonts at runtime, and let a developer drop in any `.ttf`.

## Constraints

These are properties of the existing codebase that the design must respect.

1. **No floating point in `gfx.c`.** The kernel objects build with
   `-mno-mmx -mno-sse -mno-sse2` (Makefile `KERN_CFLAGS`). All glyph blending
   must be integer arithmetic. This rules out an in-kernel `stb_truetype`-style
   rasterizer without a fixed-point port.
2. **Monospace is load-bearing.** `gfx_str_width()` is `kstrlen(s) * FONT_W *
   GFX_FONT_SCALE`, `gfx_str_ex()` advances by a constant `FONT_W * sc`,
   `gfx_str_clipped()` divides by a constant cell width, and text-field cursors
   position at `t->cursor * FONT_W`. A proportional font would invalidate all of
   it. JetBrains Mono is monospace, so this costs nothing.
3. **ASCII 32–126 only.** Matches the current glyph table. Out of scope to widen.

## Enabling facts

Three things discovered during design make this far less invasive than it looks.

- **`gfx.c` already renders into a 32-bpp RAM backbuffer** (`BACKBUFFER`, flipped
  once per frame with dirty-rect tracking). Alpha blending reads plain RAM, not
  uncached VRAM, so antialiasing is cheap.
- **`gfx_setpixel_blend(x, y, col, alpha)` already exists** (`gui/gfx.c:188`) and
  already does integer blending through `color_blend()`, respecting the clip
  rectangle and the active render target `g_target`. Antialiased glyph output is
  a direct call to it.
- **No call site uses `FONT_W`/`FONT_H` in a constant expression.** All 120
  occurrences across 20 files are runtime arithmetic. The only matches inside
  macro definitions are `FW`/`FH` in `apps/app_browser.c:13-14`, which themselves
  expand to runtime expressions. Converting the macros to variables is therefore
  source-compatible and requires no edits to the app files.

## Architecture

### 1. Build-time converter — `tools/gen-font.py`

Follows the precedent set by `tools/gen-sounds.py` ("Run once on dev machine,
commit the output"). Reads a `.ttf`, rasterizes ASCII 32–126 at each target pixel
size with 8-bit grayscale coverage, and emits a C source file that is committed
to the repo.

`make` never invokes this script and never needs Python or the `.ttf`. The kernel
build stays dependency-free so the ISO builds on any machine with the existing
toolchain.

The script depends on a Python imaging library (Pillow, or freetype-py for finer
metric control). Ubuntu 24.04 enforces PEP 668, so a bare `pip install` fails.
The script ships with a `tools/requirements.txt` and is run from a virtualenv
under `tools/.venv`, which needs no sudo.

Interface:

```
python3 tools/gen-font.py FONT.ttf --name "JetBrains Mono" \
        --sizes 11,13,16,20,28 --out gui/fonts/font_jetbrains_mono.c
python3 tools/gen-font.py FONT.ttf ... --preview preview.png
```

`--preview` writes a PNG glyph sheet so output can be inspected before rebuilding
the kernel.

The `--sizes` list is ordered to match the `font_size_t` enum declaration order in
`gui/gui.h:155-160`, so index 0 is `FONT_CAPTION` (11 px), then `FONT_BODY` (13),
`FONT_H3` (16), `FONT_H2` (20), `FONT_H1` (28). The script emits exactly five
faces and fails if the count differs.

### 2. Data model — `gui/font.h`

```c
typedef struct {
    u8  w, h;                 /* tight glyph bitmap size, pixels           */
    i8  bearing_x;            /* left side bearing from the pen            */
    i8  bearing_y;            /* rows from baseline up to bitmap top       */
    u32 offset;               /* start index into the face coverage blob   */
} glyph_t;

typedef struct {
    u8             px;        /* nominal em size: 11/13/16/20/28           */
    u8             advance;   /* monospace advance width                   */
    u8             line_h;    /* vertical advance for '\n'                 */
    u8             baseline;  /* rows from cell top down to the baseline   */
    const glyph_t *glyphs;    /* 95 entries, ASCII 32..126                 */
    const u8      *coverage;  /* 8-bit alpha, glyphs concatenated          */
} font_face_t;

typedef struct {
    const char        *name;
    const font_face_t  faces[5];   /* indexed by font_size_t               */
} font_family_t;
```

Storing tight bitmaps plus bearings rather than fixed cells keeps the coverage
blob small and lets each glyph sit correctly on the baseline.

### 3. Positioning contract

Every existing call site passes `y` as the **top of the text cell**, not the
baseline — `gfx_char()` currently draws rows 0..11 downward from `y`, and callers
vertically centre with expressions like `y + (h - FONT_H * GFX_FONT_SCALE) / 2`.

The new renderer preserves this exactly. For a glyph drawn at cell origin
`(x, y)`:

```
pixel column 0  ->  x + bearing_x
pixel row 0     ->  y + baseline - bearing_y
```

This invariant is what allows the 120 untouched call sites to keep working.

### 4. Font registry — `gui/font.c`

```c
const font_family_t *font_registry(u32 index);
u32                  font_registry_count(void);
void                 font_set_family(u32 index);   /* updates metrics, redraws */
const font_face_t   *font_face(font_size_t size);  /* active family            */
```

Shipping three families:

| Index | Family | Purpose |
|---|---|---|
| 0 | JetBrains Mono | Default UI and terminal face |
| 1 | JetBrains Mono Bold | Backs `gfx_str_bold()`, currently declared at `gui/gui.h:524` but never defined in `gui/gfx.c` and never called |
| 2 | Classic 8×12 | The existing bitmap font, kept as fallback and A/B reference |

Classic is stored in the same `font_family_t` shape with coverage values of only
0 or 255, so it flows through one render path rather than needing a second. Its
five faces are produced by integer-scaling the original 8×12 glyphs using the
same `gfx_font_scale_for()` factors in use today (1× for CAPTION/BODY/H3, 2× for
H2/H1), so selecting Classic reproduces the current appearance pixel for pixel
and gives a true A/B against the new rendering.

### 5. Changes to `gui/gfx.c` and `gui/gui.h`

- `#define FONT_W 8` / `#define FONT_H 13` become:
  ```c
  extern u32 GFX_FONT_W, GFX_FONT_H;
  #define FONT_W GFX_FONT_W
  #define FONT_H GFX_FONT_H
  ```
  updated by `font_set_family()` from the BODY face.
- `gfx_char()` and `gfx_str_ex()` replace the `bool lit` inner loop with a
  coverage lookup feeding `gfx_setpixel_blend(px, py, fg, alpha)`. When `bg` is
  not `COL_TRANSPARENT`, the cell is filled with `bg` first, then blended.
- `gfx_font_scale_for()` loses its H1/H2 doubling. Each `font_size_t` selects its
  own real face. `GFX_FONT_SCALE` survives as the hi-DPI integer multiplier set
  in `gfx_init()` for heights ≥ 1800.
- Add `gfx_str_width_ex(const char *s, font_size_t size)`. `gfx_str_width()`
  keeps its current meaning (BODY) so existing callers are unaffected.
- `gfx_str_centered_ex()` uses the per-size advance instead of `FONT_W * sc`.

### 6. Persistence and runtime switching

Mirrors the existing theme mechanism exactly.

**System settings** (`kernel/settings.c`): the file uses magic `CSTG` with
`SETTINGS_VERSION 3` and a migration chain `settings_load_v1/v2/v3`. Add a
`u32 font_family` field to `careos_settings_t`, introduce `settings_blob_v4_t`,
bump `SETTINGS_VERSION` to 4, add `settings_load_v4`, and have the v3 loader
migrate forward by defaulting `font_family` to 0. Add `settings_set_font_family()`.

**Per-user preference** (`kernel/users.c`): add `font_pref` next to the existing
`theme_pref` in `user_rec_t` and in the on-disk entry struct, following the same
migration pattern used at `users.c:382/421/458/496` where older records default
the field. Add `user_set_current_font_preference()` alongside
`user_set_current_theme_preference()`. Apply at login where
`users.c:690-691` already applies `theme_pref`.

**Settings UI** (`apps/app_settings.c`): a Font section listing the registry by
name, applying immediately via `font_set_family()` plus a full-screen redraw, and
persisting through both of the above — the same shape as the existing theme
buttons at `app_settings.c:396-402`.

## Risks

**Vertical reflow is the main risk.** There is a genuine tension in the metrics:
JetBrains Mono's advance is 0.6 em, so an 8 px advance (matching today's
`FONT_W`) implies a ~13 px em, and its ascender-to-descender span of ~1.32 em
then needs ~17 rows — taller than today's `FONT_H` of 13. Choosing instead to fit
13 rows would force a ~10 px em and a 6 px advance, which is narrower than today.

The design lets the font's natural metrics win: BODY targets an 8 px advance so
**horizontal** layout is unchanged, and accepts that line height grows from 13 to
roughly 17. Dense fixed-height layouts — the terminal grid, the file list rows,
sysmon tables — are the places where rows may collide and need constant nudging.
This is expected touch-up work, not a design flaw, and it is confined to layout
constants because all metrics now derive from `FONT_W`/`FONT_H`.

Secondary risk: `.rodata` growth of roughly 133 KB per family, ~270 KB for
Regular plus Bold. Negligible against a 16 MB kernel.

## Licensing

JetBrains Mono is licensed under SIL OFL 1.1, which permits embedding and
redistribution. Because CareOS ISOs are distributed, the license text ships as
`fonts/OFL.txt`, the generated C files carry an attribution header, and the About
app credits the typeface.

## Verification

1. `tools/gen-font.py --preview` glyph sheet inspected before any kernel rebuild.
2. Kernel builds clean with the existing `-mno-sse` flags — a float creeping into
   `gfx.c` fails the build, which is the guard against constraint 1.
3. Boot the ISO in QEMU and capture screenshots of splash, login, desktop,
   terminal, files and sysmon. Baseline screenshots of the current bitmap font
   already exist from the 2026-07-25 black-screen investigation and serve as the
   before/after comparison, specifically to catch vertical reflow collisions.
4. Switch families in Settings, confirm live application, reboot, and confirm the
   choice persisted for that user.
5. Confirm a settings file written by the v3 build still loads and migrates.

## Out of scope

- Unicode or Latin-1 beyond ASCII 32–126
- Proportional fonts and kerning
- Runtime `.ttf` parsing in the kernel
- Font hinting controls or subpixel (RGB) antialiasing
