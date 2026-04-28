# CareOS v5 -- Change Log

All changes in this release are documented by the ID used in the
Technical Architecture & Code Review report.

---

## Critical Bug Fixes

### B-01 -- Framebuffer color format (gui/gfx.c + kernel/kernel.c)
**Problem:** `fb_write_pixel` hardcoded a BGR byte layout. Any hardware that
uses RGB, RGBA, or a non-standard channel order rendered with swapped colors.

**Fix:** Added `gfx_set_pixel_format(r_shift, g_shift, b_shift)`.
`kernel_main` now reads the `red_field_pos`, `green_field_pos`, and
`blue_field_pos` fields from the Multiboot2 framebuffer tag (type=8) and
forwards them to `gfx_set_pixel_format()` before calling `gui_init()`.
A new `gfx_pack_color()` helper converts internal `0x00RRGGBB` values to the
hardware pixel layout at blit time.

**Files:** `gui/gfx.c`, `kernel/kernel.c`, `gui/gui.h`

---

### B-02 -- Missing Multiboot2 magic validation (kernel/kernel.c)
**Problem:** `kernel_main` dereferenced the MB2 info pointer even when `magic`
did not equal `0x36D76289`, risking a crash on any non-MB2 bootloader.

**Fix:** Added an explicit panic-halt at the top of `kernel_main`, *before*
any hardware init, that checks `magic != MB2_MAGIC` and executes
`cli; hlt` in a loop with a serial + VGA error message.

**Files:** `kernel/kernel.c`

---

### B-03 -- Backbuffer allocation fallback vulnerability (gui/gfx.c)
**Problem:** When `kmalloc` failed, `BACKBUFFER = FRAMEBUFFER`. The
`gfx_flip` 24-bpp path then called `fb_write_pixel` which read from and
wrote to the same memory, producing recursive corruption.

**Fix:** Replaced the pointer-aliasing fallback with an explicit boolean
`gfx_direct_mode`. When set, `gfx_flip` returns immediately (data is already
on-screen). The `BACKBUFFER` pointer is still set to `FRAMEBUFFER` as a safe
read target for `gfx_getpixel`, but `gfx_flip` never attempts a copy.

**Files:** `gui/gfx.c`

---

### B-04 -- Signed/unsigned coordinate cast (gui/gfx.c)
**Problem:** `gfx_setpixel` and `put_px` cast `i32` coordinates to `u32`
for bounds checking. A negative value like `-1` becomes `0xFFFFFFFF`, which
silently passes the `>= SCREEN_W` check and writes to address 0.

**Fix:** Added `if (x < 0 || y < 0) return;` before the cast in both
`gfx_setpixel` and `put_px`.

**Files:** `gui/gfx.c`

---

## Performance Optimizations

### P-01 -- SSE2 screen blit (gui/gfx.c)
When `__SSE2__` is defined (i.e. when compiled with `-msse2`), `gfx_flip`
uses `_mm_loadu_si128` / `_mm_storeu_si128` to move 16 bytes per instruction,
reducing the full-screen copy loop count by 4x. The backbuffer is now
allocated with 16-byte alignment. Falls back to `kmemcpy` on non-SSE2 builds.

**Files:** `gui/gfx.c`

---

### P-02 -- Dirty rectangle tracking (gui/gfx.c)
Every draw call (`gfx_hline`, `gfx_rect`, `gfx_char`, `gfx_circle`, etc.)
now records the bounding rectangle of its change via `gfx_dirty()`. `gfx_flip`
flushes only the union of dirty regions. A `dirty_full` flag is set on
`gfx_clear()` and on `gfx_init()` so the first frame still does a full blit.

The public `gfx_dirty(x, y, w, h)` function is declared in `gui.h` so that
the window manager or compositor can proactively mark regions dirty when
needed.

**Files:** `gui/gfx.c`, `gui/gui.h`

---

### P-03 -- Pre-expanded 8x8 font glyph cache (gui/gfx.c)
`gfx_char` previously expanded each 8x8 bitmask row bit-by-bit on every call.
A `glyph_cache[95][8][8]` table pre-expands all 95 printable ASCII glyphs
into ready-to-write `u32` pixel values for a given `fg`/`bg` pair.
The cache is rebuilt via `gfx_char_cache_build(fg, bg)` whenever the color
pair changes. Transparent-bg rendering still uses the original bit-loop path
since caching per unique `fg` color would be too expensive.

**Files:** `gui/gfx.c`

---

## Mouse Improvements

### M-01 -- Sub-pixel fixed-point accumulator (gui/mouse.c)
The cursor position is now tracked in Q8 fixed-point (256 units = 1 pixel).
Raw PS/2 deltas are converted to Q8 by the acceleration function and
accumulated in `sub_x_q8`/`sub_y_q8`. Whole pixels are carried to `cur_x/y`
each frame; the fractional remainder is preserved. This eliminates the
quantized "jumpiness" at low physical speeds.

**Files:** `gui/mouse.c`

---

### M-02 -- Reduced interrupt-to-screen latency (gui/mouse.c)
The delta accumulators (`accum_dx_q8`, `accum_dy_q8`) are now updated inside
`mouse_irq` (the ISR) rather than being deferred to the main loop.
`mouse_update` reads them atomically with `cli`/`sti` bracketing, clearing
the accumulators in one operation. This reduces worst-case cursor lag from
one full render-frame to less than one PIT tick (~10 ms).

**Files:** `gui/mouse.c`

---

### M-03 -- Two-zone acceleration curve (gui/mouse.c)
`accel_q8(raw)` implements a two-zone curve:
- **Precision zone** (`|raw| <= 4`): 1:1 mapping, ideal for fine cursor work.
- **Speed zone** (`|raw| > 4`): 2.5x amplification, allowing fast travel.

The scale factors are applied in Q8 fixed-point arithmetic with no
floating-point instructions. The thresholds and multipliers can be adjusted
by changing the constants in `accel_q8()`.

**Files:** `gui/mouse.c`

---

## Files Changed Summary

| File | Changes |
|------|---------|
| `gui/gfx.c` | B-01, B-03, B-04, P-01, P-02, P-03 |
| `gui/mouse.c` | M-01, M-02, M-03 |
| `kernel/kernel.c` | B-01 (color mask parse), B-02 (magic halt) |
| `gui/gui.h` | New declarations: `gfx_set_pixel_format`, `gfx_dirty` |
| `CHANGES_v5.md` | This file |
