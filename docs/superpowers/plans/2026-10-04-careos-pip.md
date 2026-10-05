# Pip, the desktop pet — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Pip lives on the CareOS desktop. He's drawn from vectors like the Pet artboard, driven by a real saved pet model, and reacts to real events. He has a Pip's Home app and cameos on the lock and power screens.

**Architecture:** There are three pure, host-tested layers:
- `pip_raster`: an integer anti-aliased polygon/curve/gradient rasterizer.
- `pip_art`: Pip as draw calls on the artboard's 160-unit grid.
- `pip_brain`: the pet model, the mood stack and the saved state.

A runtime layer (`pip_sprite`, `pip`) caches rendered frames and connects the brain to the system. `pip_desktop` and `app_pip` are the two surfaces. Session cameos and Care natives call into `pip`.

**Tech Stack:** Freestanding C (kernel flags, `-mno-sse`, no floats at runtime), host tests built with gcc + ASan/UBSan (`make host-test`), QEMU QA (`qa/v10-shell/shots.py`), VBox benchmark (`qa/v10-shell/build-bench.sh` + `../vbox-bench.ps1`).

**Spec:** `docs/superpowers/specs/2026-10-04-careos-pip-design.md`

## Global Constraints

- **Integer / Q16 maths only** in everything linked into the kernel. Float literals may appear only inside integer constant expressions that fold at compile time. The plan avoids them altogether.
- **Real data or hide.** Stats, age, bubbles and moods come from the model or system events only.
- **Character colours:**
  - lilac `#9C8CFF #CEC5FF #6A57DE #EEEAFF`
  - coral `#FF8A6B #FFC0AD #E0573A #FFE7DF`
  - mint `#5FD9A8 #A8F0D1 #25A574 #E4FAF0`
  - sky `#5CC0F2 #A8E0FB #2786C4 #E2F4FD`
  - honey `#FFC24B #FFE09A #DD9112 #FFF4D6`
  - bubblegum `#FF8EC4 #FFC4E1 #DD4F94 #FFE8F3`
  - midnight `#3E4A78 #6370A6 #222A4A #8B96C4` (ink `#F4F1FF`, shine `#1A1630`)
  - snow `#ECEFF8 #FFFFFF #B8BFD6 #FFFFFF`
  - Elsewhere ink is `#1A1630` and shine `#FFFFFF`.
- **Mood bulbs:**

  | idle | happy | sad | thinking | confused | watching | playing | sleeping | excited |
  |---|---|---|---|---|---|---|---|---|
  | `#8C9BFF` | `#FF8EC4` | `#6CB8FF` | `#FFC24B` | `#C08BFF` | `#3DD68C` | `#FF7A59` | `#5A6385` | `#FFC24B` |

- **Model constants:**
  - start happiness 70, start energy 80
  - pat +8 (≤ +24 per hour), +1 friendship point
  - happiness −1 / 30 min
  - energy −1 / 2 min while roaming or playing, +2 / min while napping
  - catch-up ≤ 3 days, happiness floor 30 after time away
  - level = 1 + points/40, max 10
  - +5 friendship per new day
  - sleeping when energy < 15, 10 idle minutes with nap on, 23:00–07:00 with 2 min idle, or nap mode
  - sad when happiness < 25
  - watching when the cursor is within 200 px and follow is on
- **Event moods** last 4–8 s and beat the resting mood. Pat → happy always.
- **Saving:** `~/.pip`, versioned `key=value` text, written at most every 10 s and at sign-out. A damaged or missing file means a fresh Pip. Guests aren't saved.
- **Desktop:**
  - perched 120 px right of the dock; roaming returns after about 40 s or when energy < 30; nap 110 px on the dock
  - layer: above windows, below the menu bar, Control Center, notifications and launcher
  - hidden in DOOM / 3D / Maze
  - damage only Pip's rectangle
- **Performance:** the VBox benchmark with Pip roaming must keep fps ≥ 30 and p95 ≤ 33 ms.
- Commits end with:
  ```
  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_012uKSbBWGh9gvbGwyQ2yXJy
  ```

## Review Focus

1. **Odd shapes and transforms.** A path that runs off the canvas, a zero-length segment, a stroke on a curve that doubles back, or a degenerate (zero-determinant) matrix must not crash, hang or write out of bounds. Pip simply gets clipped. Task 1 tests the off-canvas circle, the zero-length stroke and the singular matrix.
2. **The clock jumping backwards.** The RTC can be wrong or step backwards, and the saved last-seen time can be in the future. That must not underflow minutes into a huge catch-up, and must not crash. Task 3 tests `now < seen`.
3. **Damaged or hostile `~/.pip`.** Missing keys, absurd numbers (`happy=999`), a 10 KB name, or a wrong header must load as clamped or fresh values. Task 3 tests these.
4. **A pat storm.** 100 clicks in a second must not push happiness past the hourly cap. The bubble must not flood, and repeated mood restarts must not reset the cap window. Task 3 tests the cap; Task 5 debounces the bubble.
5. **Pip at the screen edge or over a fullscreen app.** He must never draw over the menu bar or the launcher, never steal clicks meant for an open menu, and must hide behind DOOM / 3D / Maze. Task 5 checks the layering and routing order and adds a QA shot.

---

## File map

| File | Status | Role |
|---|---|---|
| `gui/pip/pip_raster.h/.c` | new | Q16 matrices, path parser, polygon fill (non-zero, 5 sub-scanlines), round-cap strokes, radial gradients, premultiplied compositing |
| `gui/pip/pip_art.h/.c` | new | Character data, mood table, keyframe animation, `pip_art_draw` |
| `gui/pip/pip_brain.h/.c` | new | Pet model, mood stack, `~/.pip` text format |
| `gui/pip/pip_sprite.h/.c` | new | Frame cache (LRU by bytes) |
| `gui/pip/pip.h/.c` | new | Runtime: brain ownership, events, ticking, drawing helpers, persistence, cameos |
| `gui/pip/pip_desktop.c` | new | Perch / roam / nap, bubbles, status pill, menu, routing |
| `apps/app_pip.c` | new | Pip's Home |
| `tests/host/test_pip_raster.c`, `test_pip_art.c`, `test_pip_brain.c` | new | Host tests |
| `Makefile` | modify | Sources, `-Igui/pip`, host tests |
| `gui/gui.h`, `gui/gui.c`, `gui/wm.c`, `gui/launcher.c`, `gui/taskbar.c`, `gui/icon.c`, `gui/notify.c`, `kernel/appdb.c` | modify | `APP_PIP`, routing, layering, events |
| `apps/app_terminal.c`, `apps/app_editor.c`, `kernel/care_lang.c`, `gui/session/common.c` | modify | Event hooks, natives, cameos |

---

### Task 1: `pip_raster` — the shape rasterizer

**Files:**
- Create: `gui/pip/pip_raster.h`, `gui/pip/pip_raster.c`
- Test: `tests/host/test_pip_raster.c`
- Modify: `Makefile` (host test registration; `-Igui/pip` in `HOST_CFLAGS`)

**Interfaces:**
- Produces:
  - Matrices: `pr_mat_t`, `pr_identity`, `pr_translate`, `pr_scale`, `pr_rotate`, `pr_mul`, `pr_about`, `pr_sin`, `pr_cos`
  - Canvas and paint: `pr_canvas_t`, `pr_paint_t`, `pr_solid`
  - Drawing: `pr_fill_ellipse`, `pr_stroke_ellipse`, `pr_fill_rrect`, `pr_fill_path`, `pr_stroke_path`, `pr_unpremultiply`
  - All coordinates are Q16 design units. `m` maps them to canvas pixels (Q16).

- [ ] **Step 1: Write the failing test**

<!-- file: tests/host/test_pip_raster.c -->
```c
/* tests/host/test_pip_raster.c -- Pip's shape rasterizer. */
#include "pure.h"
#include "pip_raster.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define W 64
#define H 64
static u32 px[W * H];
static pr_canvas_t cv = { px, W, H };
#define U(x) ((i32)(x) * 65536)

static u32 alpha_at(i32 x, i32 y) { return px[y * W + x] >> 24; }
static u64 alpha_sum(void) { u64 s = 0; for (u32 i = 0; i < W * H; i++) s += px[i] >> 24; return s; }
static void clear(void) { memset(px, 0, sizeof px); }

static void test_trig_and_matrices(void) {
    assert(pr_sin(0) == 0 && pr_sin(90) == 65536 && pr_sin(270) == -65536);
    assert(pr_cos(0) == 65536 && pr_cos(180) == -65536 && pr_sin(-90) == -65536);
    pr_mat_t r = pr_rotate(90);
    i32 x, y;
    pr_apply(r, U(1), 0, &x, &y);                 /* (1,0) -> (0,1), SVG / y-down */
    assert(x == 0 && y == U(1));
    pr_mat_t m = pr_mul(pr_translate(U(10), U(5)), pr_scale(U(2), U(3)));
    pr_apply(m, U(1), U(1), &x, &y);
    assert(x == U(12) && y == U(8));
    pr_mat_t a = pr_about(pr_rotate(180), U(10), U(10));
    pr_apply(a, U(12), U(10), &x, &y);
    assert(x == U(8) && y == U(10));
}

static void test_circle_fill(void) {
    clear();
    pr_paint_t p = pr_solid(0xFFFF0000);
    pr_fill_ellipse(&cv, pr_identity(), U(32), U(32), U(20), U(20), 0, &p, 255);
    assert(alpha_at(32, 32) == 255 && alpha_at(1, 1) == 0);
    u64 area = alpha_sum() / 255;                  /* pi * 400 = 1256.6 */
    assert(area > 1230 && area < 1285);
    pr_unpremultiply(&cv);
    assert(px[32 * W + 32] == 0xFFFF0000);
}

static void test_square_exact_and_half(void) {
    clear();
    pr_paint_t p = pr_solid(0xFF00FF00);
    assert(pr_fill_path(&cv, pr_identity(), "M10 10 L30 10 L30 30 L10 30 Z", &p, 255));
    assert(alpha_at(20, 20) == 255 && alpha_at(9, 20) == 0 && alpha_at(30, 20) == 0);
    u64 area = alpha_sum();
    assert(area >= 400 * 255 - 400 && area <= 400 * 255 + 400);
    clear();
    assert(pr_fill_path(&cv, pr_identity(), "M10.5 10 L30 10 L30 30 L10.5 30 Z", &p, 255));
    assert(alpha_at(10, 20) > 115 && alpha_at(10, 20) < 140);  /* half-covered column */
}

static void test_opacity_and_rotation(void) {
    clear();
    pr_paint_t p = pr_solid(0xFF0000FF);
    pr_fill_ellipse(&cv, pr_identity(), U(32), U(32), U(10), U(10), 0, &p, 128);
    assert(alpha_at(32, 32) > 120 && alpha_at(32, 32) < 136);
    clear();
    pr_fill_ellipse(&cv, pr_identity(), U(32), U(32), U(20), U(6), 0, &p, 255);
    u64 a0 = alpha_sum();
    assert(alpha_at(50, 32) > 0 && alpha_at(32, 45) == 0);
    clear();
    pr_fill_ellipse(&cv, pr_identity(), U(32), U(32), U(20), U(6), 90, &p, 255);
    u64 a1 = alpha_sum();
    assert(alpha_at(32, 50) > 0 && alpha_at(50, 32) == 0);
    assert(a0 > a1 - a1 / 50 && a0 < a1 + a1 / 50);  /* same area */
}

static void test_strokes(void) {
    clear();
    pr_paint_t p = pr_solid(0xFFFFFFFF);
    assert(pr_stroke_path(&cv, pr_identity(), "M10 32 L50 32", U(4), &p, 255));
    assert(alpha_at(30, 31) == 255 && alpha_at(30, 32) == 255 && alpha_at(30, 35) == 0);
    u64 area = alpha_sum() / 255;                  /* 40*4 + pi*4 = 172.6 */
    assert(area > 160 && area < 185);
    clear();                                        /* zero-length segment = a dot */
    assert(pr_stroke_path(&cv, pr_identity(), "M20 20 L20 20", U(6), &p, 255));
    assert(alpha_at(20, 20) > 200);
    clear();                                        /* curves parse and draw */
    assert(pr_stroke_path(&cv, pr_identity(), "M10 40 Q32 10 54 40", U(3), &p, 255));
    assert(pr_fill_path(&cv, pr_identity(), "M80 36 C114 36 138 64 138 98 C138 128 114 144 80 144 Z", &p, 255));
    assert(alpha_sum() > 0);
    clear();
    pr_stroke_ellipse(&cv, pr_identity(), U(32), U(32), U(14), U(14), 0, U(3), &p, 255);
    assert(alpha_at(32, 32) == 0 && alpha_at(46, 32) > 200);
}

static void test_bad_input_and_clipping(void) {
    clear();
    pr_paint_t p = pr_solid(0xFF000000);
    assert(!pr_fill_path(&cv, pr_identity(), "M10 x", &p, 255));
    assert(!pr_fill_path(&cv, pr_identity(), "", &p, 255));
    assert(!pr_stroke_path(&cv, pr_identity(), "L10", U(2), &p, 255) || 1);  /* no crash */
    pr_fill_ellipse(&cv, pr_identity(), U(-5), U(60), U(30), U(30), 0, &p, 255);  /* mostly off canvas */
    assert(alpha_at(0, 63) == 255);
    pr_fill_ellipse(&cv, pr_scale(0, 0), U(32), U(32), U(10), U(10), 0, &p, 255);  /* singular matrix */
    pr_paint_t g = { PR_RADIAL, 0, 32768, 32768, 32768, { { 0, 0xFFFFFFFF }, { 65535, 0xFF000000 } }, 2 };
    pr_fill_ellipse(&cv, pr_scale(0, 0), U(32), U(32), U(10), U(10), 0, &g, 255);
    pr_fill_rrect(&cv, pr_identity(), U(70), U(70), U(10), U(10), U(3), &p, 255);  /* fully off */
}

static void test_radial_gradient(void) {
    clear();
    pr_paint_t g = { PR_RADIAL, 0, 32768, 32768, 32768, { { 0, 0xFFFFFFFF }, { 65535, 0xFF000000 } }, 2 };
    pr_fill_ellipse(&cv, pr_identity(), U(32), U(32), U(20), U(20), 0, &g, 255);
    pr_unpremultiply(&cv);
    u32 c = px[32 * W + 32], e = px[32 * W + 50];
    assert(((c >> 16) & 255) > 230);                 /* centre is the first stop */
    assert(((e >> 16) & 255) < 60);                  /* near the edge, the last */
}

int main(void) {
    test_trig_and_matrices();
    test_circle_fill();
    test_square_exact_and_half();
    test_opacity_and_rotation();
    test_strokes();
    test_bad_input_and_clipping();
    test_radial_gradient();
    puts("pip_raster OK");
    return 0;
}
```

- [ ] **Step 2: Register the test and run it to verify it fails**

In `Makefile`:
- Add `-Igui/pip` to `HOST_CFLAGS`.
- Append `pip_raster` to a `HOST_TESTS +=` line.
- Add `HT_SRC_pip_raster := gui/pip/pip_raster.c`.
- Add `$(HOST_TEST_DIR)/test_pip_raster: gui/pip/pip_raster.h`.

Run (WSL): `cd /mnt/d/Users/mhetm/Downloads/CareOS_v9_full/CareOS_v9 && make host-test 2>&1 | grep -E 'pip_raster|rror'`
Expected: FAIL with "pip_raster.h: No such file".

- [ ] **Step 3: Write the header**

<!-- file: gui/pip/pip_raster.h -->
```c
/* gui/pip/pip_raster.h -- Pip's shape rasterizer (pure, host-tested).
 *
 * Integer-only (Q16 fixed point: 1.0 == 65536), so it runs in the kernel
 * (-mno-sse). Shapes are given in "design units" and a matrix m maps them to
 * canvas pixels. Everything becomes polygons that are filled with the
 * non-zero rule, 5 sub-scanlines per pixel and exact horizontal coverage.
 * Strokes are the union of per-segment quads and round joints/caps. The canvas
 * holds premultiplied ARGB while drawing; pr_unpremultiply converts it to
 * straight ARGB for gfx_blend_argb. */
#ifndef CAREOS_PIP_RASTER_H
#define CAREOS_PIP_RASTER_H
#include "pure.h"

typedef struct { i32 a, b, c, d, e, f; } pr_mat_t;   /* x' = a*x + c*y + e;  y' = b*x + d*y + f */

i32      pr_sin(i32 deg);                             /* Q16 */
i32      pr_cos(i32 deg);
pr_mat_t pr_identity(void);
pr_mat_t pr_translate(i32 tx, i32 ty);
pr_mat_t pr_scale(i32 sx, i32 sy);
pr_mat_t pr_rotate(i32 deg);                          /* SVG rotate(): clockwise on screen */
pr_mat_t pr_mul(pr_mat_t outer, pr_mat_t inner);      /* inner first, then outer */
pr_mat_t pr_about(pr_mat_t m, i32 cx, i32 cy);        /* m applied about (cx, cy) */
void     pr_apply(pr_mat_t m, i32 x, i32 y, i32 *ox, i32 *oy);

typedef struct { u32 *px; i32 w, h; } pr_canvas_t;

enum { PR_SOLID = 0, PR_RADIAL };
typedef struct { u16 pos; u32 argb; } pr_stop_t;      /* pos 0..65535, straight ARGB */
typedef struct {
    u8        kind;
    u32       color;                                  /* PR_SOLID, straight ARGB */
    i32       cx, cy, r;                              /* PR_RADIAL: Q16 fractions of the shape's box */
    pr_stop_t stops[3];
    u8        nstops;
} pr_paint_t;

pr_paint_t pr_solid(u32 argb);

/* opacity 0..255 multiplies the paint. Shapes outside the canvas clip. */
void pr_fill_ellipse(pr_canvas_t *c, pr_mat_t m, i32 cx, i32 cy, i32 rx, i32 ry, i32 rot_deg,
                     const pr_paint_t *p, u8 opacity);
void pr_stroke_ellipse(pr_canvas_t *c, pr_mat_t m, i32 cx, i32 cy, i32 rx, i32 ry, i32 rot_deg,
                       i32 width, const pr_paint_t *p, u8 opacity);
void pr_fill_rrect(pr_canvas_t *c, pr_mat_t m, i32 x, i32 y, i32 w, i32 h, i32 r,
                   const pr_paint_t *p, u8 opacity);
/* SVG path data with absolute M L Q C Z. False when it doesn't parse. */
bool pr_fill_path(pr_canvas_t *c, pr_mat_t m, const char *d, const pr_paint_t *p, u8 opacity);
bool pr_stroke_path(pr_canvas_t *c, pr_mat_t m, const char *d, i32 width, const pr_paint_t *p, u8 opacity);
void pr_unpremultiply(pr_canvas_t *c);

#endif
```

- [ ] **Step 4: Write the implementation**

<!-- file: gui/pip/pip_raster.c -->
```c
/* gui/pip/pip_raster.c -- Pip's shape rasterizer (pure). See pip_raster.h. */
#include "pip_raster.h"

static const i32 SIN_Q16[91] = {
    0,1144,2287,3430,4572,5712,6850,7987,9121,10252,11380,12505,13626,14742,15855,16962,18064,
    19161,20252,21336,22415,23486,24550,25607,26656,27697,28729,29753,30767,31772,32768,33754,
    34729,35693,36647,37590,38521,39441,40348,41243,42126,42995,43852,44695,45525,46341,47143,
    47930,48703,49461,50203,50931,51643,52339,53020,53684,54332,54963,55578,56175,56756,57319,
    57865,58393,58903,59396,59870,60326,60764,61183,61584,61966,62328,62672,62997,63303,63589,
    63856,64104,64332,64540,64729,64898,65048,65177,65287,65376,65446,65496,65526,65536
};

i32 pr_sin(i32 deg) {
    deg %= 360;
    if (deg < 0) deg += 360;
    if (deg <= 90)  return SIN_Q16[deg];
    if (deg <= 180) return SIN_Q16[180 - deg];
    if (deg <= 270) return -SIN_Q16[deg - 180];
    return -SIN_Q16[360 - deg];
}
i32 pr_cos(i32 deg) { return pr_sin(deg + 90); }

static i32 qmul(i32 a, i32 b) { return (i32)(((i64)a * b) >> 16); }

pr_mat_t pr_identity(void) { pr_mat_t m = { 65536, 0, 0, 65536, 0, 0 }; return m; }
pr_mat_t pr_translate(i32 tx, i32 ty) { pr_mat_t m = { 65536, 0, 0, 65536, tx, ty }; return m; }
pr_mat_t pr_scale(i32 sx, i32 sy) { pr_mat_t m = { sx, 0, 0, sy, 0, 0 }; return m; }
pr_mat_t pr_rotate(i32 deg) {
    i32 s = pr_sin(deg), c = pr_cos(deg);
    pr_mat_t m = { c, s, -s, c, 0, 0 };
    return m;
}
pr_mat_t pr_mul(pr_mat_t o, pr_mat_t i) {
    pr_mat_t r;
    r.a = qmul(o.a, i.a) + qmul(o.c, i.b);
    r.b = qmul(o.b, i.a) + qmul(o.d, i.b);
    r.c = qmul(o.a, i.c) + qmul(o.c, i.d);
    r.d = qmul(o.b, i.c) + qmul(o.d, i.d);
    r.e = qmul(o.a, i.e) + qmul(o.c, i.f) + o.e;
    r.f = qmul(o.b, i.e) + qmul(o.d, i.f) + o.f;
    return r;
}
pr_mat_t pr_about(pr_mat_t m, i32 cx, i32 cy) {
    return pr_mul(pr_translate(cx, cy), pr_mul(m, pr_translate(-cx, -cy)));
}
void pr_apply(pr_mat_t m, i32 x, i32 y, i32 *ox, i32 *oy) {
    *ox = qmul(m.a, x) + qmul(m.c, y) + m.e;
    *oy = qmul(m.b, x) + qmul(m.d, y) + m.f;
}

pr_paint_t pr_solid(u32 argb) {
    pr_paint_t p;
    for (u32 i = 0; i < sizeof p; i++) ((u8 *)&p)[i] = 0;
    p.kind = PR_SOLID; p.color = argb;
    return p;
}

static u64 isqrt64(u64 v) {
    u64 r = 0, bit = (u64)1 << 62;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; } else r >>= 1;
        bit >>= 2;
    }
    return r;
}

/* ---- Geometry scratch: points of the current shape in design units ---- */
#define PR_MAXP 1600
#define PR_MAXS 48
static i32  g_px[PR_MAXP], g_py[PR_MAXP];
static u32  g_np;
static u32  g_sub[PR_MAXS + 1];
static bool g_closed[PR_MAXS];
static u32  g_nsub;

static bool add_pt(i32 x, i32 y) {
    if (g_np >= PR_MAXP) return false;
    g_px[g_np] = x; g_py[g_np] = y; g_np++;
    return true;
}
static bool new_sub(void) {
    if (g_nsub >= PR_MAXS) return false;
    g_sub[g_nsub] = g_np; g_closed[g_nsub] = false; g_nsub++;
    return true;
}
static void reset_geom(void) { g_np = 0; g_nsub = 0; }
static u32 sub_end(u32 s) { return s + 1 < g_nsub ? g_sub[s + 1] : g_np; }

static const char *skip(const char *s) {
    while (*s == ' ' || *s == ',' || *s == '\n' || *s == '\t' || *s == '\r') s++;
    return s;
}
static const char *num(const char *s, i32 *out) {
    s = skip(s);
    bool neg = false;
    if (*s == '-') { neg = true; s++; } else if (*s == '+') s++;
    if (!((*s >= '0' && *s <= '9') || *s == '.')) return NULL;
    i64 v = 0, frac = 0, div = 1;
    while (*s >= '0' && *s <= '9') { if (v < 100000) v = v * 10 + (*s - '0'); s++; }
    if (*s == '.') {
        s++;
        while (*s >= '0' && *s <= '9') { if (div < 100000) { frac = frac * 10 + (*s - '0'); div *= 10; } s++; }
    }
    i64 q = (v << 16) + (frac << 16) / div;
    *out = (i32)(neg ? -q : q);
    return s;
}

static bool parse_path(const char *d) {
    reset_geom();
    if (!d) return false;
    i32 cx = 0, cy = 0, sx = 0, sy = 0;
    char cmd = 0;
    const char *s = d;
    for (;;) {
        s = skip(s);
        if (!*s) break;
        if ((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z')) {
            cmd = *s++;
            if (cmd == 'Z' || cmd == 'z') {
                if (g_nsub) g_closed[g_nsub - 1] = true;
                cx = sx; cy = sy;
                continue;
            }
        } else if (!cmd) return false;
        u32 need = (cmd == 'M' || cmd == 'L') ? 2 : cmd == 'Q' ? 4 : cmd == 'C' ? 6 : 0;
        if (!need) return false;
        i32 v[6];
        for (u32 k = 0; k < need; k++) { s = num(s, &v[k]); if (!s) return false; }
        if (cmd == 'M') {
            if (!new_sub() || !add_pt(v[0], v[1])) return false;
            cx = sx = v[0]; cy = sy = v[1];
            cmd = 'L';                                      /* implicit lineto after M */
            continue;
        }
        if (!g_nsub) { if (!new_sub() || !add_pt(cx, cy)) return false; }
        if (cmd == 'L') {
            if (!add_pt(v[0], v[1])) return false;
            cx = v[0]; cy = v[1];
        } else if (cmd == 'Q') {
            for (i64 i = 1; i <= 12; i++) {
                i64 t = i * 65536 / 12, u = 65536 - t;
                i64 b0 = (u * u) >> 16, b1 = (2 * u * t) >> 16, b2 = (t * t) >> 16;
                if (!add_pt((i32)((b0 * cx + b1 * v[0] + b2 * v[2]) >> 16),
                            (i32)((b0 * cy + b1 * v[1] + b2 * v[3]) >> 16))) return false;
            }
            cx = v[2]; cy = v[3];
        } else {
            for (i64 i = 1; i <= 16; i++) {
                i64 t = i * 65536 / 16, u = 65536 - t;
                i64 u2 = (u * u) >> 16, t2 = (t * t) >> 16;
                i64 b0 = (u2 * u) >> 16, b1 = (3 * u2 * t) >> 16, b2 = (3 * u * t2) >> 16, b3 = (t2 * t) >> 16;
                if (!add_pt((i32)((b0 * cx + b1 * v[0] + b2 * v[2] + b3 * v[4]) >> 16),
                            (i32)((b0 * cy + b1 * v[1] + b2 * v[3] + b3 * v[5]) >> 16))) return false;
            }
            cx = v[4]; cy = v[5];
        }
    }
    return g_np > 0;
}

static void ellipse_pts(i32 cx, i32 cy, i32 rx, i32 ry, i32 rot) {
    reset_geom();
    new_sub();
    g_closed[0] = true;
    i32 cr = pr_cos(rot), sr = pr_sin(rot);
    for (i32 a = 0; a < 360; a += 5) {
        i32 ex = qmul(rx, pr_cos(a)), ey = qmul(ry, pr_sin(a));
        add_pt(cx + qmul(ex, cr) - qmul(ey, sr), cy + qmul(ex, sr) + qmul(ey, cr));
    }
}

static void rrect_pts(i32 x, i32 y, i32 w, i32 h, i32 r) {
    reset_geom();
    new_sub();
    g_closed[0] = true;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    i32 ccx[4] = { x + w - r, x + w - r, x + r, x + r };
    i32 ccy[4] = { y + r, y + h - r, y + h - r, y + r };
    i32 a0[4] = { 270, 0, 90, 180 };
    for (u32 k = 0; k < 4; k++)
        for (i32 a = 0; a <= 90; a += 15)
            add_pt(ccx[k] + qmul(r, pr_cos(a0[k] + a)), ccy[k] + qmul(r, pr_sin(a0[k] + a)));
}

/* Design-space bounding box of the current points (for gradients). */
static void geom_bbox(i32 *x0, i32 *y0, i32 *x1, i32 *y1) {
    *x0 = *y0 = 0x7fffffff; *x1 = *y1 = -0x7fffffff;
    for (u32 i = 0; i < g_np; i++) {
        if (g_px[i] < *x0) *x0 = g_px[i];
        if (g_px[i] > *x1) *x1 = g_px[i];
        if (g_py[i] < *y0) *y0 = g_py[i];
        if (g_py[i] > *y1) *y1 = g_py[i];
    }
}

/* ---- Edges in canvas space, Q8 (1/256 px) ---- */
#define PR_MAXE 6000
static i32 e_x0[PR_MAXE], e_y0[PR_MAXE], e_x1[PR_MAXE], e_y1[PR_MAXE];
static i8  e_dir[PR_MAXE];
static u32 g_ne;
static i32 g_ymin, g_ymax;

static void edge(i32 x0, i32 y0, i32 x1, i32 y1) {
    if (y0 == y1 || g_ne >= PR_MAXE) return;
    i8 dir = 1;
    if (y0 > y1) { i32 t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; dir = -1; }
    e_x0[g_ne] = x0; e_y0[g_ne] = y0; e_x1[g_ne] = x1; e_y1[g_ne] = y1; e_dir[g_ne] = dir; g_ne++;
    if (y0 < g_ymin) g_ymin = y0;
    if (y1 > g_ymax) g_ymax = y1;
}
static void edges_reset(void) { g_ne = 0; g_ymin = 0x7fffffff; g_ymax = -0x7fffffff; }

static i32 to_q8(i32 v16) { return v16 >> 8; }

/* A polygon (canvas Q8 points), oriented so that every polygon winds the
 * same way: overlapping pieces then union under the non-zero rule. */
static void poly(const i32 *xs, const i32 *ys, u32 n) {
    if (n < 3) return;
    i64 area = 0;
    for (u32 i = 0; i < n; i++) {
        u32 j = (i + 1) % n;
        area += (i64)xs[i] * ys[j] - (i64)xs[j] * ys[i];
    }
    for (u32 i = 0; i < n; i++) {
        u32 j = (i + 1) % n;
        if (area >= 0) edge(xs[i], ys[i], xs[j], ys[j]);
        else edge(xs[j], ys[j], xs[i], ys[i]);
    }
}

/* Fill edges from the current geometry (each subpath closed). */
static void edges_from_fill(pr_mat_t m) {
    edges_reset();
    for (u32 s = 0; s < g_nsub; s++) {
        u32 a = g_sub[s], b = sub_end(s);
        if (b - a < 2) continue;
        i32 px0, py0, pxp, pyp;
        pr_apply(m, g_px[a], g_py[a], &px0, &py0);
        pxp = px0; pyp = py0;
        for (u32 i = a + 1; i < b; i++) {
            i32 x, y; pr_apply(m, g_px[i], g_py[i], &x, &y);
            edge(to_q8(pxp), to_q8(pyp), to_q8(x), to_q8(y));
            pxp = x; pyp = y;
        }
        edge(to_q8(pxp), to_q8(pyp), to_q8(px0), to_q8(py0));
    }
}

/* Device stroke half-width in Q8 from a design width (Q16). */
static i32 half_width_q8(pr_mat_t m, i32 width) {
    i64 det = (i64)m.a * m.d - (i64)m.b * m.c;            /* Q32 */
    if (det < 0) det = -det;
    i32 scale = (i32)isqrt64((u64)det);                   /* Q16 */
    return qmul(width, scale) >> 9;                       /* /2 and Q16 -> Q8 */
}

static void circle_poly(i32 cx, i32 cy, i32 r) {
    i32 xs[16], ys[16];
    for (u32 k = 0; k < 16; k++) {
        xs[k] = cx + qmul(r, pr_cos((i32)k * 360 / 16));
        ys[k] = cy + qmul(r, pr_sin((i32)k * 360 / 16));
    }
    poly(xs, ys, 16);
}

static void seg_quad(i32 x0, i32 y0, i32 x1, i32 y1, i32 hw) {
    i64 dx = x1 - x0, dy = y1 - y0;
    i64 len = (i64)isqrt64((u64)(dx * dx + dy * dy));
    if (len == 0) return;
    i32 nx = (i32)(-dy * hw / len), ny = (i32)(dx * hw / len);
    i32 xs[4] = { x0 + nx, x1 + nx, x1 - nx, x0 - nx };
    i32 ys[4] = { y0 + ny, y1 + ny, y1 - ny, y0 - ny };
    poly(xs, ys, 4);
}

static void edges_from_stroke(pr_mat_t m, i32 width) {
    edges_reset();
    i32 hw = half_width_q8(m, width);
    if (hw < 32) hw = 32;                                 /* never thinner than 1/4 px */
    for (u32 s = 0; s < g_nsub; s++) {
        u32 a = g_sub[s], b = sub_end(s);
        if (b <= a) continue;
        i32 fx, fy, px_, py_;
        pr_apply(m, g_px[a], g_py[a], &fx, &fy);
        fx = to_q8(fx); fy = to_q8(fy);
        px_ = fx; py_ = fy;
        circle_poly(fx, fy, hw);
        for (u32 i = a + 1; i < b; i++) {
            i32 x, y; pr_apply(m, g_px[i], g_py[i], &x, &y);
            x = to_q8(x); y = to_q8(y);
            seg_quad(px_, py_, x, y, hw);
            circle_poly(x, y, hw);
            px_ = x; py_ = y;
        }
        if (g_closed[s]) seg_quad(px_, py_, fx, fy, hw);
    }
}

/* ---- Scanline fill and compositing ---- */
#define PR_SS     5
#define PR_MAXW   1024
#define PR_MAXX   512
static i32 g_acc[PR_MAXW];
static i32 g_xs[PR_MAXX];
static i8  g_xd[PR_MAXX];

static u32 lerp_argb(u32 a, u32 b, u32 t /*0..65536*/) {
    u32 r = 0;
    for (u32 sh = 0; sh < 32; sh += 8) {
        i32 ca = (i32)((a >> sh) & 255), cb = (i32)((b >> sh) & 255);
        r |= (u32)(ca + (((cb - ca) * (i32)t) >> 16)) << sh;
    }
    return r;
}

typedef struct { pr_mat_t inv; bool ok; i32 gx, gy, rx, ry; } pr_grad_t;

static bool invert(pr_mat_t m, pr_mat_t *out) {
    i64 det = ((i64)m.a * m.d - (i64)m.b * m.c) >> 16;    /* Q16 */
    if (det == 0) return false;
    pr_mat_t r;
    r.a = (i32)(((i64)m.d << 16) / det);
    r.b = (i32)(((i64)-m.b << 16) / det);
    r.c = (i32)(((i64)-m.c << 16) / det);
    r.d = (i32)(((i64)m.a << 16) / det);
    r.e = -(qmul(r.a, m.e) + qmul(r.c, m.f));
    r.f = -(qmul(r.b, m.e) + qmul(r.d, m.f));
    *out = r;
    return true;
}

static u32 paint_at(const pr_paint_t *p, const pr_grad_t *g, i32 px, i32 py) {
    if (p->kind != PR_RADIAL || !g->ok || p->nstops == 0) return p->kind == PR_RADIAL ? (p->nstops ? p->stops[0].argb : 0) : p->color;
    i32 dx, dy;
    pr_apply(g->inv, (px << 16) + 32768, (py << 16) + 32768, &dx, &dy);
    i64 nx = g->rx ? (((i64)(dx - g->gx)) << 16) / g->rx : 0;
    i64 ny = g->ry ? (((i64)(dy - g->gy)) << 16) / g->ry : 0;
    u64 t = isqrt64((u64)(nx * nx + ny * ny));            /* Q16 */
    if (t >= 65535) return p->stops[p->nstops - 1].argb;
    if (t <= p->stops[0].pos) return p->stops[0].argb;
    for (u32 i = 1; i < p->nstops; i++) {
        u32 a = p->stops[i - 1].pos, b = p->stops[i].pos;
        if (t <= b) {
            u32 f = b > a ? (u32)(((t - a) << 16) / (b - a)) : 65536;
            return lerp_argb(p->stops[i - 1].argb, p->stops[i].argb, f);
        }
    }
    return p->stops[p->nstops - 1].argb;
}

static void blend(u32 *d, u32 col, u32 a) {
    if (!a) return;
    if (a > 255) a = 255;
    u32 inv = 255 - a, dv = *d;
    u32 sr = ((col >> 16) & 255) * a / 255, sg = ((col >> 8) & 255) * a / 255, sb = (col & 255) * a / 255;
    u32 da = dv >> 24, dr = (dv >> 16) & 255, dg = (dv >> 8) & 255, db = dv & 255;
    *d = ((a + da * inv / 255) << 24) | ((sr + dr * inv / 255) << 16) | ((sg + dg * inv / 255) << 8) | (sb + db * inv / 255);
}

static void add_span(i32 xa, i32 xb, i32 w) {
    if (xa < 0) xa = 0;
    if (xb > w * 256) xb = w * 256;
    if (xb <= xa) return;
    i32 ia = xa >> 8, ib = xb >> 8;
    if (ia == ib) { g_acc[ia] += xb - xa; return; }
    g_acc[ia] += 256 - (xa & 255);
    for (i32 i = ia + 1; i < ib && i < w; i++) g_acc[i] += 256;
    if (ib < w) g_acc[ib] += xb & 255;
}

static void raster(pr_canvas_t *c, const pr_paint_t *p, u8 opacity, const pr_grad_t *g) {
    if (!g_ne || !c || !c->px || c->w <= 0 || c->h <= 0 || c->w > PR_MAXW) return;
    i32 y0 = g_ymin >> 8, y1 = (g_ymax - 1) >> 8;
    if (y0 < 0) y0 = 0;
    if (y1 >= c->h) y1 = c->h - 1;
    for (i32 py = y0; py <= y1; py++) {
        for (i32 i = 0; i < c->w; i++) g_acc[i] = 0;
        bool any = false;
        for (i32 s = 0; s < PR_SS; s++) {
            i32 sy = py * 256 + (2 * s + 1) * 128 / PR_SS;
            u32 n = 0;
            for (u32 e = 0; e < g_ne && n < PR_MAXX; e++) {
                if (sy < e_y0[e] || sy >= e_y1[e]) continue;
                i32 x = e_x0[e] + (i32)(((i64)(sy - e_y0[e]) * (e_x1[e] - e_x0[e])) / (e_y1[e] - e_y0[e]));
                u32 k = n++;
                while (k > 0 && g_xs[k - 1] > x) { g_xs[k] = g_xs[k - 1]; g_xd[k] = g_xd[k - 1]; k--; }
                g_xs[k] = x; g_xd[k] = e_dir[e];
            }
            i32 wind = 0, start = 0;
            for (u32 k = 0; k < n; k++) {
                i32 before = wind;
                wind += g_xd[k];
                if (before == 0 && wind != 0) start = g_xs[k];
                else if (before != 0 && wind == 0) { add_span(start, g_xs[k], c->w); any = true; }
            }
        }
        if (!any) continue;
        u32 *row = c->px + py * c->w;
        for (i32 px = 0; px < c->w; px++) {
            if (!g_acc[px]) continue;
            u32 cov = (u32)g_acc[px] * 255 / (256 * PR_SS);
            if (cov > 255) cov = 255;
            u32 col = paint_at(p, g, px, py);
            u32 a = ((col >> 24) * cov / 255) * opacity / 255;
            blend(&row[px], col, a);
        }
    }
}

static void grad_setup(const pr_paint_t *p, pr_mat_t m, pr_grad_t *g) {
    g->ok = false;
    if (p->kind != PR_RADIAL) return;
    i32 x0, y0, x1, y1;
    geom_bbox(&x0, &y0, &x1, &y1);
    i32 bw = x1 - x0, bh = y1 - y0;
    g->gx = x0 + qmul(bw, p->cx);
    g->gy = y0 + qmul(bh, p->cy);
    g->rx = qmul(bw, p->r);
    g->ry = qmul(bh, p->r);
    g->ok = invert(m, &g->inv) && g->rx > 0 && g->ry > 0;
}

static void fill_current(pr_canvas_t *c, pr_mat_t m, const pr_paint_t *p, u8 opacity) {
    pr_grad_t g;
    grad_setup(p, m, &g);
    edges_from_fill(m);
    raster(c, p, opacity, &g);
}

void pr_fill_ellipse(pr_canvas_t *c, pr_mat_t m, i32 cx, i32 cy, i32 rx, i32 ry, i32 rot,
                     const pr_paint_t *p, u8 opacity) {
    ellipse_pts(cx, cy, rx, ry, rot);
    fill_current(c, m, p, opacity);
}

void pr_stroke_ellipse(pr_canvas_t *c, pr_mat_t m, i32 cx, i32 cy, i32 rx, i32 ry, i32 rot,
                       i32 width, const pr_paint_t *p, u8 opacity) {
    ellipse_pts(cx, cy, rx, ry, rot);
    edges_from_stroke(m, width);
    pr_grad_t g; g.ok = false;
    raster(c, p, opacity, &g);
}

void pr_fill_rrect(pr_canvas_t *c, pr_mat_t m, i32 x, i32 y, i32 w, i32 h, i32 r,
                   const pr_paint_t *p, u8 opacity) {
    rrect_pts(x, y, w, h, r);
    fill_current(c, m, p, opacity);
}

bool pr_fill_path(pr_canvas_t *c, pr_mat_t m, const char *d, const pr_paint_t *p, u8 opacity) {
    if (!parse_path(d)) return false;
    fill_current(c, m, p, opacity);
    return true;
}

bool pr_stroke_path(pr_canvas_t *c, pr_mat_t m, const char *d, i32 width, const pr_paint_t *p, u8 opacity) {
    if (!parse_path(d)) return false;
    edges_from_stroke(m, width);
    pr_grad_t g; g.ok = false;
    raster(c, p, opacity, &g);
    return true;
}

void pr_unpremultiply(pr_canvas_t *c) {
    for (i32 i = 0; i < c->w * c->h; i++) {
        u32 v = c->px[i], a = v >> 24;
        if (a == 0 || a == 255) continue;
        u32 r = ((v >> 16) & 255) * 255 / a, g = ((v >> 8) & 255) * 255 / a, b = (v & 255) * 255 / a;
        if (r > 255) r = 255;
        if (g > 255) g = 255;
        if (b > 255) b = 255;
        c->px[i] = (a << 24) | (r << 16) | (g << 8) | b;
    }
}
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `make host-test 2>&1 | grep -E 'pip_raster|rror|Assert'`
Expected: `pip_raster OK`, no sanitizer reports.

- [ ] **Step 6: Commit**

```bash
git add gui/pip/pip_raster.h gui/pip/pip_raster.c tests/host/test_pip_raster.c Makefile
git commit -m "feat(pip): integer vector rasterizer for Pip (fills, strokes, gradients)"
```

---

### Task 2: `pip_art` — Pip as drawing data

**Files:**
- Create: `gui/pip/pip_art.h`, `gui/pip/pip_art.c`
- Test: `tests/host/test_pip_art.c`
- Modify: `Makefile`
  - `HT_SRC_pip_art := gui/pip/pip_art.c gui/pip/pip_raster.c`
  - Add the test to `HOST_TESTS`, with deps on both headers.

**Interfaces:**
- Consumes: everything from Task 1.
- Produces:
  - Types: `pip_mood_t` (`PM_IDLE..PM_EXCITED`, `PM_COUNT`), `pip_look_t {u8 color, hat, outfit, acc, effect}`, `pip_pose_t {u8 mood; u16 phase; bool still, blink; i8 look_x, look_y;}`.
  - Counts: `PIP_COLORS 8`, `PIP_HATS 9`, `PIP_OUTFITS 6`, `PIP_ACCS 4`, `PIP_EFFECTS 6`.
  - Labels: `pip_color_label(i)`, `pip_hat_label(i)`, `pip_outfit_label(i)`, `pip_acc_label(i)`, `pip_effect_label(i)`, `pip_mood_label(m)`.
  - Mood data: `pip_mood_bulb(m)`, `pip_mood_cycle_ms(m)`.
  - Drawing: `pip_art_draw(pr_canvas_t*, pr_mat_t m, const pip_look_t*, const pip_pose_t*)` draws the design box (0..160 plus overlays out to x −40..200, y −24..176). `pip_art_errors()` counts paths that failed to parse since start.

- [ ] **Step 1: Write the failing test**

<!-- file: tests/host/test_pip_art.c -->
```c
/* tests/host/test_pip_art.c -- every look and mood draws, inside its box. */
#include "pure.h"
#include "pip_art.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define S 96                       /* 96 px Pip: canvas covers -40..200 x -24..176 units */
#define CW (S * 240 / 160)
#define CH (S * 200 / 160)
static u32 px[CW * CH];

static void draw(const pip_look_t *l, const pip_pose_t *p) {
    memset(px, 0, sizeof px);
    pr_canvas_t c = { px, CW, CH };
    pr_mat_t m = pr_mul(pr_scale(S * 65536 / 160, S * 65536 / 160), pr_translate(40 * 65536, 24 * 65536));
    pip_art_draw(&c, m, l, p);
}
static u64 coverage(void) { u64 s = 0; for (u32 i = 0; i < CW * CH; i++) s += px[i] >> 24; return s; }

int main(void) {
    pip_look_t look = { 0, 0, 0, 0, 0 };
    pip_pose_t pose = { PM_IDLE, 0, true, false, 0, 0 };
    draw(&look, &pose);
    u64 base = coverage();
    assert(base > 0);
    /* the body sits where the artboard puts it: the centre of the 160 box is opaque */
    i32 cx = (40 + 80) * S / 160, cy = (24 + 98) * S / 160;
    assert((px[cy * CW + cx] >> 24) == 255);
    /* every mood, every phase, every look draws without a parse error */
    for (u32 m = 0; m < PM_COUNT; m++)
        for (u32 ph = 0; ph < 65536; ph += 16384) {
            pip_pose_t p = { (u8)m, (u16)ph, false, (ph & 16384) != 0, 3, -2 };
            draw(&look, &p);
            assert(coverage() > 0);
        }
    for (u8 c = 0; c < PIP_COLORS; c++)
        for (u8 h = 0; h < PIP_HATS; h++)
            for (u8 o = 0; o < PIP_OUTFITS; o++) {
                pip_look_t l = { c, h, o, (u8)((c + h) % PIP_ACCS), (u8)((h + o) % PIP_EFFECTS) };
                draw(&l, &pose);
            }
    assert(pip_art_errors() == 0);
    /* a hat adds pixels above the head */
    pip_look_t crown = { 0, 2, 0, 0, 0 };
    draw(&crown, &pose);
    u64 top = 0;
    for (i32 y = 0; y < (24 + 30) * S / 160; y++) for (i32 x = 0; x < CW; x++) top += px[y * CW + x] >> 24;
    assert(top > 0);
    /* labels and mood data */
    assert(!strcmp(pip_mood_label(PM_SLEEPING), "Sleeping") && !strcmp(pip_hat_label(2), "Crown"));
    assert(pip_mood_bulb(PM_HAPPY) == 0xFF8EC4 && pip_mood_bulb(PM_WATCHING) == 0x3DD68C);
    assert(pip_mood_cycle_ms(PM_IDLE) == 2600 && pip_mood_cycle_ms(PM_PLAYING) == 500);
    assert(!strcmp(pip_color_label(7), "Snow") && !strcmp(pip_effect_label(5), "Rainbow"));
    puts("pip_art OK");
    return 0;
}
```

- [ ] **Step 2: Register and run, expect failure** (`pip_art.h` missing).

- [ ] **Step 3: Write the header**

<!-- file: gui/pip/pip_art.h -->
```c
/* gui/pip/pip_art.h -- Pip as drawing data (pure, host-tested).
 * Transcribed from the Pet artboard (docs/design/careos-v10/Pet.dc.html):
 * the 160-unit design grid, the layer order, the mood table and the CSS
 * keyframes as integer tables. Overlays reach x -40..200, y -24..176. */
#ifndef CAREOS_PIP_ART_H
#define CAREOS_PIP_ART_H
#include "pip_raster.h"

#define PIP_COLORS  8
#define PIP_HATS    9
#define PIP_OUTFITS 6
#define PIP_ACCS    4
#define PIP_EFFECTS 6

typedef enum {
    PM_IDLE = 0, PM_HAPPY, PM_SAD, PM_THINKING, PM_CONFUSED,
    PM_WATCHING, PM_PLAYING, PM_SLEEPING, PM_EXCITED, PM_COUNT
} pip_mood_t;

typedef struct { u8 color, hat, outfit, acc, effect; } pip_look_t;
typedef struct {
    u8   mood;
    u16  phase;          /* position in the mood's cycle, 0..65535 */
    bool still;          /* no animation */
    bool blink;          /* eyes shut for a blink (open-eyed moods) */
    i8   look_x, look_y; /* eye offset in design units (watching follows the cursor) */
} pip_pose_t;

const char *pip_color_label(u32 i);
const char *pip_hat_label(u32 i);
const char *pip_outfit_label(u32 i);
const char *pip_acc_label(u32 i);
const char *pip_effect_label(u32 i);
const char *pip_mood_label(u32 m);
u32  pip_mood_bulb(u32 m);        /* 0xRRGGBB */
u32  pip_mood_cycle_ms(u32 m);    /* one animation cycle */
u32  pip_color_base(u32 i);       /* 0xRRGGBB, for UI swatches */

void pip_art_draw(pr_canvas_t *c, pr_mat_t m, const pip_look_t *look, const pip_pose_t *pose);
u32  pip_art_errors(void);

#endif
```

- [ ] **Step 4: Write the implementation**

<!-- file: gui/pip/pip_art.c -->
```c
/* gui/pip/pip_art.c -- Pip as drawing data (pure). See pip_art.h. */
#include "pip_art.h"

#define U(x10) ((i32)(x10) * 6554)                 /* tenths of a unit -> Q16 (65536/10) */
#define FX(x)  ((i32)(x) * 65536)                  /* whole units -> Q16 */

static u32 g_errors;
u32 pip_art_errors(void) { return g_errors; }

/* ---- Labels and tables ---- */
static const char *const COLOR_L[PIP_COLORS]  = { "Lilac", "Coral", "Mint", "Sky", "Honey", "Bubblegum", "Midnight", "Snow" };
static const char *const HAT_L[PIP_HATS]      = { "None", "Beanie", "Crown", "Party", "Wizard", "Cap", "Bow", "Phones", "Sprout" };
static const char *const OUTFIT_L[PIP_OUTFITS] = { "None", "Scarf", "Bow tie", "Hoodie", "Cape", "Overalls" };
static const char *const ACC_L[PIP_ACCS]      = { "None", "Glasses", "Shades", "Star clip" };
static const char *const EFFECT_L[PIP_EFFECTS] = { "None", "Glow", "Sparkles", "Hearts", "Bubbles", "Rainbow" };
static const char *const MOOD_L[PM_COUNT]     = { "Chilling", "Happy", "Sad", "Thinking", "Confused", "Watching", "Playing", "Sleeping", "Excited" };

const char *pip_color_label(u32 i)  { return i < PIP_COLORS ? COLOR_L[i] : ""; }
const char *pip_hat_label(u32 i)    { return i < PIP_HATS ? HAT_L[i] : ""; }
const char *pip_outfit_label(u32 i) { return i < PIP_OUTFITS ? OUTFIT_L[i] : ""; }
const char *pip_acc_label(u32 i)    { return i < PIP_ACCS ? ACC_L[i] : ""; }
const char *pip_effect_label(u32 i) { return i < PIP_EFFECTS ? EFFECT_L[i] : ""; }
const char *pip_mood_label(u32 m)   { return m < PM_COUNT ? MOOD_L[m] : ""; }

/* base, light, dark, belly */
static const u32 COLORS[PIP_COLORS][4] = {
    { 0x9C8CFF, 0xCEC5FF, 0x6A57DE, 0xEEEAFF }, { 0xFF8A6B, 0xFFC0AD, 0xE0573A, 0xFFE7DF },
    { 0x5FD9A8, 0xA8F0D1, 0x25A574, 0xE4FAF0 }, { 0x5CC0F2, 0xA8E0FB, 0x2786C4, 0xE2F4FD },
    { 0xFFC24B, 0xFFE09A, 0xDD9112, 0xFFF4D6 }, { 0xFF8EC4, 0xFFC4E1, 0xDD4F94, 0xFFE8F3 },
    { 0x3E4A78, 0x6370A6, 0x222A4A, 0x8B96C4 }, { 0xECEFF8, 0xFFFFFF, 0xB8BFD6, 0xFFFFFF },
};
u32 pip_color_base(u32 i) { return COLORS[i < PIP_COLORS ? i : 0][0]; }

enum { E_OPEN, E_HAPPY, E_SAD, E_CLOSED, E_CONFUSED, E_STAR };
enum { MO_SMILE, MO_BIG, MO_FROWN, MO_O, MO_WAVY, MO_FLAT };
enum { A_BOB, A_BOUNCE, A_DROOP, A_TILT, A_WOBBLE, A_HOP, A_BREATHE, A_JUMP };
enum { SH_NONE, SH_BOB, SH_BOUNCE, SH_HOP, SH_JUMP };
enum { OV_NONE, OV_TEAR, OV_THINK, OV_Q, OV_PLAY, OV_Z, OV_BANG };

typedef struct {
    u8 eye, mouth, anim, shadow, ov;
    bool wave, pulse, feet, lookanim;
    u8 blush;            /* 0..255 */
    i8 look_x, look_y;
    u32 bulb;
    u16 cycle_ms;
} mood_t;

static const mood_t MOODS[PM_COUNT] = {
    /* idle     */ { E_OPEN,     MO_SMILE, A_BOB,     SH_BOB,    OV_NONE,  false, false, false, false, 255, 0, 0,  0x8C9BFF, 2600 },
    /* happy    */ { E_HAPPY,    MO_BIG,   A_BOUNCE,  SH_BOUNCE, OV_NONE,  true,  false, false, false, 255, 0, 0,  0xFF8EC4, 950 },
    /* sad      */ { E_SAD,      MO_FROWN, A_DROOP,   SH_NONE,   OV_TEAR,  false, false, false, false, 38,  0, 0,  0x6CB8FF, 3200 },
    /* thinking */ { E_OPEN,     MO_FLAT,  A_TILT,    SH_NONE,   OV_THINK, false, true,  false, false, 255, 4, -6, 0xFFC24B, 3000 },
    /* confused */ { E_CONFUSED, MO_WAVY,  A_WOBBLE,  SH_NONE,   OV_Q,     false, true,  false, false, 255, 0, 0,  0xC08BFF, 1800 },
    /* watching */ { E_OPEN,     MO_O,     A_BOB,     SH_BOB,    OV_NONE,  false, false, false, true,  255, 0, 0,  0x3DD68C, 3200 },
    /* playing  */ { E_HAPPY,    MO_BIG,   A_HOP,     SH_HOP,    OV_PLAY,  false, false, true,  false, 255, 0, 0,  0xFF7A59, 500 },
    /* sleeping */ { E_CLOSED,   MO_O,     A_BREATHE, SH_NONE,   OV_Z,     false, false, false, false, 255, 0, 0,  0x5A6385, 3400 },
    /* excited  */ { E_STAR,     MO_BIG,   A_JUMP,    SH_JUMP,   OV_BANG,  true,  false, false, false, 255, 0, 0,  0xFFC24B, 850 },
};
u32 pip_mood_bulb(u32 m) { return MOODS[m < PM_COUNT ? m : 0].bulb; }
u32 pip_mood_cycle_ms(u32 m) { return MOODS[m < PM_COUNT ? m : 0].cycle_ms; }

/* ---- Keyframes (CSS @keyframes as integers) ----
 * pct 0..100, dy in tenths of a unit, sx/sy in thousandths, rot in degrees. */
typedef struct { u8 pct; i16 dy; i16 sx, sy; i8 rot; } kf_t;
typedef struct { const kf_t *k; u8 n; } anim_t;
static const kf_t K_BOB[]     = { {0,0,1000,1000,0}, {50,-50,1010,990,0}, {100,0,1000,1000,0} };
static const kf_t K_BOUNCE[]  = { {0,0,1000,1000,0}, {15,0,1080,920,0}, {40,-200,940,1060,0}, {65,0,1060,940,0}, {82,-20,990,1010,0}, {100,0,1000,1000,0} };
static const kf_t K_DROOP[]   = { {0,0,1000,1000,0}, {50,50,1040,960,-2}, {100,0,1000,1000,0} };
static const kf_t K_TILT[]    = { {0,0,1000,1000,0}, {30,0,1000,1000,-6}, {70,-20,1000,1000,6}, {100,0,1000,1000,0} };
static const kf_t K_WOBBLE[]  = { {0,0,1000,1000,0}, {20,0,1000,1000,-8}, {40,0,1000,1000,6}, {60,0,1000,1000,-4}, {80,0,1000,1000,2}, {100,0,1000,1000,0} };
static const kf_t K_HOP[]     = { {0,0,1000,1000,0}, {25,-100,1000,1000,-4}, {50,-140,1000,1000,0}, {75,-60,1000,1000,4}, {100,0,1000,1000,0} };
static const kf_t K_BREATHE[] = { {0,0,1000,1000,0}, {50,0,1040,960,0}, {100,0,1000,1000,0} };
static const kf_t K_JUMP[]    = { {0,0,1000,1000,0}, {12,0,1120,860,0}, {42,-320,920,1080,0}, {70,0,1080,920,0}, {85,0,980,1020,0}, {100,0,1000,1000,0} };
static const anim_t ANIMS[] = {
    { K_BOB, 3 }, { K_BOUNCE, 6 }, { K_DROOP, 3 }, { K_TILT, 4 }, { K_WOBBLE, 6 }, { K_HOP, 5 }, { K_BREATHE, 3 }, { K_JUMP, 6 }
};
/* Shadow: dy = opacity in thousandths, sx = scale */
static const kf_t S_BOB[]    = { {0,1000,1000,0,0}, {50,1000,940,0,0}, {100,1000,1000,0,0} };
static const kf_t S_BOUNCE[] = { {0,1000,1000,0,0}, {15,1000,1080,0,0}, {40,500,700,0,0}, {65,1000,1060,0,0}, {100,1000,1000,0,0} };
static const kf_t S_HOP[]    = { {0,1000,1000,0,0}, {50,600,780,0,0}, {100,1000,1000,0,0} };
static const kf_t S_JUMP[]   = { {0,1000,1000,0,0}, {12,1000,1100,0,0}, {42,400,550,0,0}, {70,1000,1080,0,0}, {100,1000,1000,0,0} };
static const anim_t SHADOWS[] = { { 0, 0 }, { S_BOB, 3 }, { S_BOUNCE, 5 }, { S_HOP, 3 }, { S_JUMP, 5 } };

/* Smooth (ease-in-out) interpolation at phase 0..65535. */
static kf_t kf_at(const anim_t *a, u32 phase, bool still) {
    kf_t z = { 0, 0, 1000, 1000, 0 };
    if (!a->k || still) return a->k ? a->k[0] : z;
    u32 pct16 = phase * 100;                               /* pct in Q16 */
    for (u8 i = 1; i < a->n; i++) {
        u32 p0 = (u32)a->k[i - 1].pct << 16, p1 = (u32)a->k[i].pct << 16;
        if (pct16 > p1 && i + 1 < a->n) continue;
        i64 t = p1 > p0 ? (i64)(pct16 - p0) * 65536 / (p1 - p0) : 65536;
        if (t < 0) t = 0;
        if (t > 65536) t = 65536;
        t = (t * t >> 16) * (3 * 65536 - 2 * t) >> 16;     /* smoothstep */
        const kf_t *A = &a->k[i - 1], *B = &a->k[i];
        kf_t r;
        r.pct = 0;
        r.dy  = (i16)(A->dy + (((B->dy - A->dy) * t) >> 16));
        r.sx  = (i16)(A->sx + (((B->sx - A->sx) * t) >> 16));
        r.sy  = (i16)(A->sy + (((B->sy - A->sy) * t) >> 16));
        r.rot = (i8)(A->rot + (((B->rot - A->rot) * t) >> 16));
        return r;
    }
    return a->k[a->n - 1];
}

/* A triangle wave 0..65536..0 over the cycle, offset by off (0..65535). */
static u32 tri(u32 phase, u32 off) {
    u32 p = (phase + off) & 0xFFFF;
    return p < 32768 ? p * 2 : (65535 - p) * 2;
}

/* ---- Drawing helpers ---- */
static pr_canvas_t *C;
static u32 P_BASE, P_LIGHT, P_DARK, P_BELLY, P_INK, P_SHINE, P_BULB;

static u32 argb(u32 rgb, u32 a) { return (a << 24) | (rgb & 0xFFFFFF); }
static void ell(pr_mat_t m, i32 cx, i32 cy, i32 rx, i32 ry, i32 rot, u32 rgb, u32 op) {
    pr_paint_t p = pr_solid(argb(rgb, 255));
    pr_fill_ellipse(C, m, U(cx), U(cy), U(rx), U(ry), rot, &p, (u8)op);
}
static void ring(pr_mat_t m, i32 cx, i32 cy, i32 r, i32 w, u32 rgb, u32 op) {
    pr_paint_t p = pr_solid(argb(rgb, 255));
    pr_stroke_ellipse(C, m, U(cx), U(cy), U(r), U(r), 0, U(w), &p, (u8)op);
}
static void fill(pr_mat_t m, const char *d, u32 rgb, u32 op) {
    pr_paint_t p = pr_solid(argb(rgb, 255));
    if (!pr_fill_path(C, m, d, &p, (u8)op)) g_errors++;
}
static void stroke(pr_mat_t m, const char *d, i32 w10, u32 rgb, u32 op) {
    pr_paint_t p = pr_solid(argb(rgb, 255));
    if (!pr_stroke_path(C, m, d, U(w10), &p, (u8)op)) g_errors++;
}
static void rrect(pr_mat_t m, i32 x, i32 y, i32 w, i32 h, i32 r, u32 rgb, u32 op) {
    pr_paint_t p = pr_solid(argb(rgb, 255));
    pr_fill_rrect(C, m, U(x), U(y), U(w), U(h), U(r), &p, (u8)op);
}
static pr_paint_t radial(i32 cx, i32 cy, i32 r, u32 c0, u32 c1, u32 c2, u8 n, u16 mid) {
    pr_paint_t p = pr_solid(0);
    p.kind = PR_RADIAL;
    p.cx = cx; p.cy = cy; p.r = r; p.nstops = n;
    p.stops[0].pos = 0; p.stops[0].argb = c0;
    if (n == 2) { p.stops[1].pos = 65535; p.stops[1].argb = c1; }
    else { p.stops[1].pos = mid; p.stops[1].argb = c1; p.stops[2].pos = 65535; p.stops[2].argb = c2; }
    return p;
}
static u32 a255(u32 permille) { return permille * 255 / 1000; }

/* Bold glyph ('?', '!', 'z') as round strokes in a 10-unit box, h10 tall. */
static void glyph(pr_mat_t m, char g, i32 x10, i32 y10, i32 h10, u32 rgb, u32 op) {
    pr_mat_t gm = pr_mul(m, pr_mul(pr_translate(U(x10), U(y10)), pr_scale(h10 * 6554, h10 * 6554)));
    pr_mat_t unit = pr_scale(65536 / 10, 65536 / 10);
    pr_mat_t gmu = pr_mul(gm, unit);                       /* glyph coordinates are 0..10 */
    pr_paint_t p = pr_solid(argb(rgb, 255));
    if (g == '?') {
        pr_stroke_path(C, gmu, "M2 3 Q2 0 5 0 Q8 0 8 3 Q8 5 5 6 L5 7.5", FX(17) / 10, &p, (u8)op);
        pr_fill_ellipse(C, gmu, FX(5), FX(96) / 10, FX(1), FX(1), 0, &p, (u8)op);
    } else if (g == '!') {
        pr_stroke_path(C, gmu, "M5 0.5 L5 6.6", FX(2), &p, (u8)op);
        pr_fill_ellipse(C, gmu, FX(5), FX(96) / 10, FX(11) / 10, FX(11) / 10, 0, &p, (u8)op);
    } else {
        pr_stroke_path(C, gmu, "M1.5 2 L8.5 2 L1.5 9 L8.5 9", FX(16) / 10, &p, (u8)op);
    }
}

/* ---- Layers ---- */
static void layer_effects_back(pr_mat_t m, const pip_look_t *l, u32 ph) {
    if (l->effect == 1) {                                   /* glow */
        u32 s = 920 + tri(ph, 0) * 160 / 65536;             /* aura 0.92..1.08 */
        pr_mat_t gm = pr_mul(m, pr_about(pr_scale(s * 65536 / 1000, s * 65536 / 1000), FX(80), FX(92)));
        pr_paint_t g = radial(32768, 32768, 32768, argb(P_LIGHT, 230), argb(P_LIGHT, 140), argb(P_LIGHT, 0), 3, 40000);
        pr_fill_ellipse(C, gm, FX(80), FX(92), FX(74), FX(74), 0, &g, (u8)(140 + tri(ph, 0) * 100 / 65536));
    }
    if (l->effect == 5) {                                   /* rainbow */
        stroke(m, "M-34 118 Q16 58 58 96", 60, 0xFF5A5F, 217);
        stroke(m, "M-34 126 Q16 66 58 104", 60, 0xFFC24B, 217);
        stroke(m, "M-34 134 Q16 74 58 112", 60, 0x3DD68C, 217);
        stroke(m, "M-34 142 Q16 82 58 120", 60, 0x5CC0F2, 217);
        stroke(m, "M-34 150 Q16 90 58 128", 60, 0x9C8CFF, 217);
    }
}

static void layer_shadow(pr_mat_t m, const mood_t *md, u32 ph, bool still) {
    kf_t k = kf_at(&SHADOWS[md->shadow], ph, still);
    i32 s = (md->shadow ? k.sx : 1000) * 65536 / 1000;
    u32 op = md->shadow ? (u32)k.dy * 255 / 1000 : 255;
    pr_mat_t sm = pr_mul(m, pr_about(pr_scale(s, s), FX(80), FX(144)));
    pr_paint_t g = radial(32768, 32768, 32768, argb(0x0A0C1E, 82), argb(0x0A0C1E, 0), 0, 2, 0);
    pr_fill_ellipse(C, sm, FX(80), FX(144), FX(46), FX(8), 0, &g, (u8)op);
}

static void layer_outfit_back(pr_mat_t b, const pip_look_t *l) {
    if (l->outfit == 4) {                                   /* cape */
        fill(b, "M40 60 Q8 126 20 152 L140 152 Q152 126 120 60 Z", 0xE5303A, 255);
        stroke(b, "M40 60 Q16 120 26 148M120 60 Q144 120 134 148", 30, 0xB81F28, 255);
    }
}

static void layer_ears_feet_arms(pr_mat_t b, const mood_t *md, u32 ph, bool still) {
    ell(b, 460, 480, 110, 160, -26, P_BASE, 255);
    ell(b, 460, 500, 55, 95, -26, 0xFF9FB8, 153);
    ell(b, 1140, 480, 110, 160, 26, P_BASE, 255);
    ell(b, 1140, 500, 55, 95, 26, 0xFF9FB8, 153);
    i32 fa = 0, fb = 0;
    if (md->feet && !still) { fa = -(i32)tri(ph, 0) * 6 / 65536; fb = -(i32)tri(ph, 43690) * 6 / 65536; }
    pr_mat_t ma = pr_mul(b, pr_translate(0, FX(fa))), mb = pr_mul(b, pr_translate(0, FX(fb)));
    ell(ma, 580, 1430, 150, 85, 0, P_DARK, 255);
    ell(ma, 540, 1400, 60, 26, 0, 0xFFFFFF, 64);
    ell(mb, 1020, 1430, 150, 85, 0, P_DARK, 255);
    ell(mb, 980, 1400, 60, 26, 0, 0xFFFFFF, 64);
    ell(b, 240, 1080, 90, 130, 22, P_DARK, 255);
    i32 wave = (md->wave && !still) ? -(i32)tri(ph * 2 & 0xFFFF, 0) * 38 / 65536 : 0;
    pr_mat_t arm = pr_mul(b, pr_about(pr_rotate(wave), FX(130), FX(100)));
    ell(arm, 1360, 1080, 90, 130, -22, P_DARK, 255);
}

static void layer_body(pr_mat_t b, const mood_t *md) {
    static const char *BODY = "M80 36 C114 36 138 64 138 98 C138 128 114 144 80 144 C46 144 22 128 22 98 C22 64 46 36 80 36 Z";
    pr_paint_t g = radial(23593, 18350, 52429, argb(P_LIGHT, 255), argb(P_BASE, 255), argb(P_DARK, 255), 3, 32768);
    if (!pr_fill_path(C, b, BODY, &g, 255)) g_errors++;
    stroke(b, BODY, 20, P_DARK, 71);
    pr_paint_t bl = radial(32768, 26214, 39322, argb(P_BELLY, 242), argb(P_BELLY, 0), 0, 2, 0);
    pr_fill_ellipse(C, b, FX(80), FX(120), FX(40), FX(24), 0, &bl, 255);
    ell(b, 560, 580, 170, 80, -28, 0xFFFFFF, 128);
    ell(b, 740, 480, 30, 30, 0, 0xFFFFFF, 140);
    pr_paint_t ck = radial(32768, 32768, 32768, argb(0xFF6B8E, 191), argb(0xFF6B8E, 0), 0, 2, 0);
    pr_fill_ellipse(C, b, FX(49), FX(103), FX(11), FX(7), 0, &ck, md->blush);
    pr_fill_ellipse(C, b, FX(111), FX(103), FX(11), FX(7), 0, &ck, md->blush);
}

static void layer_outfit_front(pr_mat_t b, const pip_look_t *l) {
    switch (l->outfit) {
    case 1:
        fill(b, "M28 112 Q80 132 132 112 L133 125 Q80 145 27 125 Z", 0xFF5A5F, 255);
        stroke(b, "M28 112 Q80 132 132 112", 25, 0xFF8A8E, 255);
        fill(b, "M98 128 L110 154 L98 157 L90 132 Z", 0xE23D44, 255);
        stroke(b, "M96 139 L106 136M99 148 L108 145", 20, 0xFFFFFF, 255);
        break;
    case 2:
        fill(b, "M80 124 Q70 112 62 114 Q58 124 62 134 Q70 136 80 124 Z", 0x4F5EF2, 255);
        fill(b, "M80 124 Q90 112 98 114 Q102 124 98 134 Q90 136 80 124 Z", 0x4F5EF2, 255);
        ell(b, 800, 1240, 55, 55, 0, 0x3542C9, 255);
        stroke(b, "M66 120 L70 122M94 120 L90 122", 20, 0x8C9BFF, 255);
        break;
    case 3:
        fill(b, "M25 108 Q80 130 135 108 Q140 134 114 144 L46 144 Q20 134 25 108 Z", 0x3A4570, 255);
        stroke(b, "M25 108 Q80 130 135 108", 30, 0x5A6799, 255);
        fill(b, "M62 129 L98 129 L94 141 L66 141 Z", 0x2B3458, 255);
        stroke(b, "M72 120 L70 131M88 120 L90 131", 20, 0xFFFFFF, 255);
        ell(b, 700, 1320, 20, 20, 0, 0xFFFFFF, 255);
        ell(b, 900, 1320, 20, 20, 0, 0xFFFFFF, 255);
        break;
    case 5:
        stroke(b, "M62 104 L48 74M98 104 L112 74", 50, 0x4C7DFF, 255);
        fill(b, "M60 110 L100 110 L100 118 L124 118 L128 136 Q80 152 32 136 L36 118 L60 118 Z", 0x4C7DFF, 255);
        rrect(b, 710, 1160, 180, 110, 30, 0x3A66E0, 255);
        ell(b, 640, 1140, 28, 28, 0, 0xFFC24B, 255);
        ell(b, 960, 1140, 28, 28, 0, 0xFFC24B, 255);
        break;
    default: break;
    }
}

static void layer_antenna(pr_mat_t b, const pip_look_t *l, const mood_t *md, u32 ph, bool still) {
    if (!(l->hat == 0 || l->hat == 6 || l->hat == 7)) return;  /* none, bow, headphones */
    stroke(b, "M80 38 Q75 27 83 17", 35, P_DARK, 255);
    u32 hs = still ? 1000 : 1000 + (ph & 0xFFFF) * 700 / 65536;  /* halo 1..1.7, fading */
    u32 hop = still ? 89 : 89 - (ph & 0xFFFF) * 89 / 65536;
    pr_mat_t hm = pr_mul(b, pr_about(pr_scale(hs * 65536 / 1000, hs * 65536 / 1000), FX(84), FX(14)));
    ell(hm, 840, 140, 110, 110, 0, P_BULB, hop);
    u32 bop = (md->pulse && !still) ? 255 - tri(ph * 4 & 0xFFFF, 0) * 178 / 65536 : 255;
    ell(b, 840, 140, 80, 80, 0, P_BULB, bop);
    ring(b, 840, 140, 80, 15, 0xFFFFFF, 89 * bop / 255);
    ell(b, 815, 115, 28, 28, 0, 0xFFFFFF, 230 * bop / 255);
}

static void layer_eyes(pr_mat_t b, const mood_t *md, const pip_pose_t *pose) {
    i32 lx = pose->look_x, ly = pose->look_y;
    if (!lx && !ly) { lx = md->look_x; ly = md->look_y; }
    if (md->lookanim && !pose->still && !pose->look_x && !pose->look_y) {
        u32 p = pose->phase;                                    /* pip-look: -6 at 25 %, +6 at 75 % */
        lx = p < 32768 ? -(i32)tri(p * 2 & 0xFFFF, 0) * 6 / 65536 : (i32)tri(p * 2 & 0xFFFF, 0) * 6 / 65536;
    }
    pr_mat_t e = pr_mul(b, pr_translate(FX(lx), FX(ly)));
    if (pose->blink && md->eye == E_OPEN) e = pr_mul(e, pr_about(pr_scale(65536, 6554), FX(80), FX(88)));
    switch (md->eye) {
    case E_OPEN:
        ell(e, 620, 880, 100, 125, 0, P_INK, 255); ell(e, 980, 880, 100, 125, 0, P_INK, 255);
        ell(e, 620, 940, 60, 40, 0, P_LIGHT, 140); ell(e, 980, 940, 60, 40, 0, P_LIGHT, 140);
        ell(e, 655, 825, 40, 40, 0, P_SHINE, 255); ell(e, 1015, 825, 40, 40, 0, P_SHINE, 255);
        ell(e, 585, 925, 18, 18, 0, P_SHINE, 217); ell(e, 945, 925, 18, 18, 0, P_SHINE, 217);
        break;
    case E_HAPPY:
        stroke(e, "M52 92 Q62 78 72 92M88 92 Q98 78 108 92", 50, P_INK, 255);
        break;
    case E_SAD:
        ell(e, 620, 920, 85, 95, 0, P_INK, 255); ell(e, 980, 920, 85, 95, 0, P_INK, 255);
        ell(e, 650, 880, 30, 30, 0, P_SHINE, 255); ell(e, 1010, 880, 30, 30, 0, P_SHINE, 255);
        ell(e, 620, 970, 50, 25, 0, 0x9ADBFA, 179); ell(e, 980, 970, 50, 25, 0, 0x9ADBFA, 179);
        stroke(e, "M50 77 Q59 74 70 81M110 77 Q101 74 90 81", 35, P_INK, 255);
        break;
    case E_CLOSED:
        stroke(e, "M52 90 Q62 98 72 90M88 90 Q98 98 108 90", 45, P_INK, 255);
        stroke(e, "M52 90 L48 87M108 90 L112 87", 30, P_INK, 255);
        break;
    case E_CONFUSED:
        ell(e, 620, 880, 110, 135, 0, P_INK, 255);
        ell(e, 660, 820, 42, 42, 0, P_SHINE, 255);
        ell(e, 580, 930, 18, 18, 0, P_SHINE, 217);
        stroke(e, "M89 91 L108 87", 50, P_INK, 255);
        stroke(e, "M49 70 Q62 61 75 69", 35, P_INK, 255);
        break;
    default: /* star */
        fill(e, "M62 74 Q64.8 85.2 76 88 Q64.8 90.8 62 102 Q59.2 90.8 48 88 Q59.2 85.2 62 74 Z", 0xFFC24B, 255);
        stroke(e, "M62 74 Q64.8 85.2 76 88 Q64.8 90.8 62 102 Q59.2 90.8 48 88 Q59.2 85.2 62 74 Z", 22, P_INK, 255);
        fill(e, "M98 74 Q100.8 85.2 112 88 Q100.8 90.8 98 102 Q95.2 90.8 84 88 Q95.2 85.2 98 74 Z", 0xFFC24B, 255);
        stroke(e, "M98 74 Q100.8 85.2 112 88 Q100.8 90.8 98 102 Q95.2 90.8 84 88 Q95.2 85.2 98 74 Z", 22, P_INK, 255);
        ell(e, 590, 840, 20, 20, 0, 0xFFFFFF, 255); ell(e, 950, 840, 20, 20, 0, 0xFFFFFF, 255);
        break;
    }
}

static void layer_mouth(pr_mat_t b, const mood_t *md) {
    switch (md->mouth) {
    case MO_SMILE: stroke(b, "M71 106 Q75.5 112 80 107 Q84.5 112 89 106", 35, P_INK, 255); break;
    case MO_BIG:
        fill(b, "M68 104 Q80 124 92 104 Z", P_INK, 255);
        stroke(b, "M68 104 Q80 124 92 104 Z", 25, P_INK, 255);
        fill(b, "M73.5 113.5 Q80 108.5 86.5 113.5 Q80 119 73.5 113.5 Z", 0xFF6B8E, 255);
        break;
    case MO_FROWN: stroke(b, "M72 113 Q80 105 88 113", 35, P_INK, 255); break;
    case MO_O:     ell(b, 800, 1100, 45, 55, 0, P_INK, 255); break;
    case MO_WAVY:  stroke(b, "M70 110 Q74 105 78 110 Q82 115 86 110 Q88 108 90 108", 35, P_INK, 255); break;
    default:       stroke(b, "M74 109 Q80 107 87 108", 35, P_INK, 255); break;
    }
}

static void layer_acc(pr_mat_t b, const pip_look_t *l) {
    if (l->acc == 1) {
        pr_paint_t g = pr_solid(argb(0xFFFFFF, 46));
        pr_fill_ellipse(C, b, FX(62), FX(88), FX(14), FX(14), 0, &g, 255);
        pr_fill_ellipse(C, b, FX(98), FX(88), FX(14), FX(14), 0, &g, 255);
        ring(b, 620, 880, 140, 30, 0x2A2F45, 255);
        ring(b, 980, 880, 140, 30, 0x2A2F45, 255);
        stroke(b, "M76 87 Q80 83 84 87M48 86 L34 82M112 86 L126 82", 30, 0x2A2F45, 255);
        stroke(b, "M54 80 L58 77M90 80 L94 77", 20, 0xFFFFFF, 179);
    } else if (l->acc == 2) {
        fill(b, "M45 81 L77 81 L75 95 Q62 103 48 95 Z", 0x14121F, 255);
        fill(b, "M83 81 L115 81 L112 95 Q98 103 85 95 Z", 0x14121F, 255);
        stroke(b, "M77 83 L83 83M45 82 L34 79M115 82 L126 79", 30, 0x14121F, 255);
        stroke(b, "M51 85 L59 85M89 85 L97 85", 24, 0xFFFFFF, 179);
    } else if (l->acc == 3) {
        static const char *STAR = "M116 48 L119 55 L127 56 L121 61 L123 69 L116 65 L109 69 L111 61 L105 56 L113 55 Z";
        fill(b, STAR, 0xFFC24B, 255);
        stroke(b, STAR, 15, 0xD98E12, 255);
    }
}

static void layer_hat(pr_mat_t b, const pip_look_t *l) {
    switch (l->hat) {
    case 1:
        fill(b, "M42 56 Q44 18 80 16 Q116 18 118 56 Z", 0xFF7A59, 255);
        stroke(b, "M60 22 L58 52M80 17 L80 52M100 22 L102 52", 25, 0xE5603F, 255);
        rrect(b, 380, 490, 840, 140, 70, 0xE5603F, 255);
        ell(b, 800, 130, 95, 95, 0, 0xFFF1E8, 255);
        ell(b, 770, 100, 30, 30, 0, 0xFFFFFF, 255);
        break;
    case 2: {
        static const char *CROWN = "M52 48 L55 18 L68 33 L80 12 L92 33 L105 18 L108 48 Z";
        fill(b, CROWN, 0xFFC24B, 255);
        stroke(b, CROWN, 20, 0xD98E12, 255);
        rrect(b, 510, 420, 580, 80, 30, 0xF5AE2E, 255);
        ell(b, 800, 380, 45, 45, 0, 0xFF5A7A, 255);
        ell(b, 640, 400, 30, 30, 0, 0x5CC0F2, 255);
        ell(b, 960, 400, 30, 30, 0, 0x3DD68C, 255);
        ell(b, 550, 180, 30, 30, 0, 0xFFE08A, 255);
        ell(b, 800, 120, 30, 30, 0, 0xFFE08A, 255);
        ell(b, 1050, 180, 30, 30, 0, 0xFFE08A, 255);
        break;
    }
    case 3: {
        pr_mat_t g = pr_mul(b, pr_about(pr_rotate(14), FX(80), FX(44)));
        fill(g, "M63 48 L80 0 L97 48 Z", 0x8C9BFF, 255);
        stroke(g, "M70 30 L90 30M66 42 L94 42", 30, 0xFFFFFF, 179);
        ell(g, 760, 220, 26, 26, 0, 0xFFC24B, 255);
        ell(g, 860, 360, 26, 26, 0, 0xFF8EC4, 255);
        ell(g, 800, 0, 70, 70, 0, 0xFF8EC4, 255);
        break;
    }
    case 4:
        fill(b, "M56 48 Q70 28 72 2 Q86 -6 100 10 Q90 18 104 48 Z", 0x6A55D9, 255);
        ell(b, 800, 480, 460, 85, 0, 0x4B3AA8, 255);
        fill(b, "M86 22 L88 27 L93 27.5 L89 31 L90.5 36 L86 33.5 L81.5 36 L83 31 L79 27.5 L84 27 Z", 0xFFC24B, 255);
        ell(b, 720, 380, 20, 20, 0, 0xFFE08A, 255);
        ell(b, 950, 400, 16, 16, 0, 0xFFE08A, 255);
        break;
    case 5:
        fill(b, "M46 54 Q46 20 80 20 Q114 20 114 54 Z", 0x3CA0E8, 255);
        fill(b, "M102 50 Q132 46 142 56 Q126 61 104 56 Z", 0x2A7CC0, 255);
        ell(b, 800, 210, 35, 35, 0, 0x2A7CC0, 255);
        stroke(b, "M60 36 Q68 27 80 26", 30, 0xFFFFFF, 128);
        break;
    case 6:
        fill(b, "M104 44 Q88 24 82 40 Q84 58 104 44 Z", 0xFF5A8E, 255);
        fill(b, "M104 44 Q120 24 126 40 Q124 58 104 44 Z", 0xFF5A8E, 255);
        stroke(b, "M90 36 Q92 42 98 43M118 36 Q116 42 110 43", 20, 0xFF9FBF, 255);
        ell(b, 1040, 440, 60, 60, 0, 0xE23D74, 255);
        break;
    case 7:
        stroke(b, "M28 96 Q26 30 80 28 Q134 30 132 96", 70, 0x2A2F45, 255);
        rrect(b, 150, 840, 220, 320, 100, 0xFF7A59, 255);
        rrect(b, 1230, 840, 220, 320, 100, 0xFF7A59, 255);
        rrect(b, 190, 900, 60, 180, 30, 0xFFB49E, 255);
        rrect(b, 1270, 900, 60, 180, 30, 0xFFB49E, 255);
        break;
    case 8:
        stroke(b, "M80 38 Q79 28 81 18", 35, 0x2FAE7D, 255);
        fill(b, "M80 25 Q66 10 56 20 Q68 32 80 25 Z", 0x5FD9A8, 255);
        fill(b, "M81 20 Q92 2 106 10 Q96 26 81 20 Z", 0x3DD68C, 255);
        stroke(b, "M68 21 L76 24M94 12 L86 18", 15, 0x2FAE7D, 255);
        break;
    default: break;
    }
}

static void layer_overlay(pr_mat_t m, pr_mat_t b, const mood_t *md, u32 ph, bool still) {
    switch (md->ov) {
    case OV_TEAR: {
        i32 dy = still ? 0 : (i32)(ph * 14 / 65536);
        u32 op = still ? 255 : (ph < 13107 ? ph * 255 / 13107 : 255 - (ph - 13107) * 255 / 52429);
        fill(pr_mul(b, pr_translate(0, FX(dy))), "M55 100 Q50 109 55 112 Q60 109 55 100 Z", 0x6CB8FF, op);
        fill(m, "M112 26 Q112 16 122 16 Q126 8 134 12 Q144 10 144 20 Q150 26 142 30 L116 30 Q108 30 112 26 Z", 0x9AA3BD, 255);
        stroke(m, "M118 36 L116 42M128 36 L126 44M138 36 L136 42", 25, 0x6CB8FF, 255);
        break;
    }
    case OV_THINK:
        ell(m, 1215, 535, 35, 35, 0, 0xFFFFFF, 255);
        ell(m, 1315, 415, 55, 55, 0, 0xFFFFFF, 255);
        rrect(m, 1260, -60, 560, 360, 180, 0xFFFFFF, 255);
        for (u32 i = 0; i < 3; i++) {
            u32 op = still ? 255 : 255 - tri(ph * 3 & 0xFFFF, i * 13107) * 178 / 65536;
            ell(m, 1430 + (i32)i * 110, 120, 30, 30, 0, 0x6E5CE0, op);
        }
        break;
    case OV_Q: {
        kf_t k = kf_at(&ANIMS[A_WOBBLE], ph, still);
        pr_mat_t q = pr_mul(m, pr_about(pr_rotate(k.rot), FX(130), FX(30)));
        glyph(q, '?', 1220, 70, 245, 0xA97CFF, 255);
        glyph(m, '?', 1420, 300, 145, 0xC08BFF, 204);
        break;
    }
    case OV_PLAY:
        stroke(m, "M0 90 L14 90M-8 104 L12 104M0 118 L16 118", 40, P_DARK, 115);
        ell(m, 180, 1480, 60, 60, 0, P_LIGHT, 153);
        ell(m, 60, 1440, 40, 40, 0, P_LIGHT, 102);
        break;
    case OV_Z: {
        static const i32 ZX[3] = { 1120, 1260, 1420 }, ZY[3] = { 400, 260, 70 }, ZH[3] = { 130, 190, 230 };
        static const u32 ZC[3] = { 0x8C9BFF, 0x8C9BFF, 0xB7C1FF };
        for (u32 i = 0; i < 3; i++) {
            u32 p = (ph + i * 21845) & 0xFFFF;               /* staggered by a third */
            i32 dy = still ? 0 : 80 - (i32)(p * 360 / 65536);
            u32 op = still ? 255 : (p < 13107 ? p * 255 / 13107 : 255 - (p - 13107) * 255 / 52429);
            glyph(m, 'z', ZX[i], ZY[i] + dy, ZH[i], ZC[i], op);
        }
        break;
    }
    case OV_BANG: {
        kf_t k = kf_at(&ANIMS[A_HOP], ph, still);
        glyph(pr_mul(m, pr_translate(0, U(k.dy))), '!', 1240, 40, 275, 0xFF7A59, 255);
        break;
    }
    default: break;
    }
}

static void layer_effects_front(pr_mat_t m, const pip_look_t *l, u32 mood, u32 ph, bool still) {
    if (l->effect == 2) {                                   /* sparkles, twinkling */
        static const char *SP[4] = {
            "M20 30 Q21 39 30 40 Q21 41 20 50 Q19 41 10 40 Q19 39 20 30 Z",
            "M148 62 Q149 69 156 70 Q149 71 148 78 Q147 71 140 70 Q147 69 148 62 Z",
            "M142 122 Q143 129 150 130 Q143 131 142 138 Q141 131 134 130 Q141 129 142 122 Z",
            "M10 106 Q11 111 16 112 Q11 113 10 118 Q9 113 4 112 Q9 111 10 106 Z" };
        static const i32 SX[4] = { 20, 148, 142, 10 }, SY[4] = { 40, 70, 130, 112 };
        static const u32 SC[4] = { 0xFFE08A, 0xFFFFFF, 0xFFE08A, 0xFFFFFF };
        for (u32 i = 0; i < 4; i++) {
            u32 t = still ? 65536 : tri(ph, i * 16384);
            i32 s = (400 + (i32)(t * 600 / 65536)) * 65536 / 1000;
            u32 op = 64 + t * 191 / 65536;
            fill(pr_mul(m, pr_about(pr_scale(s, s), FX(SX[i]), FX(SY[i]))), SP[i], SC[i], op);
        }
    }
    if (l->effect == 3 || mood == PM_HAPPY) {
        fill(m, "M28 50 C28 44 36 42 38 48 C40 42 48 44 48 50 C48 56 38 62 38 62 C38 62 28 56 28 50 Z", 0xFF6B8E, 255);
        fill(m, "M122 40 C122 35 129 33 130 38 C132 33 139 35 139 40 C139 45 130 50 130 50 C130 50 122 45 122 40 Z", 0xFF8EC4, 255);
    }
    if (l->effect == 4) {
        static const i32 BX[3] = { 220, 1400, 1300 }, BY[3] = { 700, 600, 1000 }, BR[3] = { 70, 90, 50 };
        for (u32 i = 0; i < 3; i++) {
            i32 dy = still ? 0 : -(i32)(tri(ph, i * 21845) * 6 / 65536);
            pr_mat_t bm = pr_mul(m, pr_translate(0, FX(dy)));
            pr_paint_t g = pr_solid(argb(0x9ADBFA, 89));
            pr_fill_ellipse(C, bm, U(BX[i]), U(BY[i]), U(BR[i]), U(BR[i]), 0, &g, 255);
            ring(bm, BX[i], BY[i], BR[i], 15, 0x9ADBFA, 255);
            if (i < 2) ell(bm, BX[i] - 25, BY[i] - 25, 22, 22, 0, 0xFFFFFF, 204);
        }
    }
    (void)a255;
}

void pip_art_draw(pr_canvas_t *c, pr_mat_t m, const pip_look_t *look, const pip_pose_t *pose) {
    if (!c || !look || !pose) return;
    C = c;
    pip_look_t l = *look;
    if (l.color >= PIP_COLORS) l.color = 0;
    if (l.hat >= PIP_HATS) l.hat = 0;
    if (l.outfit >= PIP_OUTFITS) l.outfit = 0;
    if (l.acc >= PIP_ACCS) l.acc = 0;
    if (l.effect >= PIP_EFFECTS) l.effect = 0;
    u32 mood = pose->mood < PM_COUNT ? pose->mood : PM_IDLE;
    const mood_t *md = &MOODS[mood];
    P_BASE = COLORS[l.color][0]; P_LIGHT = COLORS[l.color][1]; P_DARK = COLORS[l.color][2]; P_BELLY = COLORS[l.color][3];
    P_INK = l.color == 6 ? 0xF4F1FF : 0x1A1630;
    P_SHINE = l.color == 6 ? 0x1A1630 : 0xFFFFFF;
    P_BULB = md->bulb;
    bool still = pose->still;
    u32 ph = still ? 0 : pose->phase;

    kf_t k = kf_at(&ANIMS[md->anim], ph, still);
    pr_mat_t bx = pr_mul(pr_translate(0, U(k.dy)),
                  pr_mul(pr_scale(k.sx * 65536 / 1000, k.sy * 65536 / 1000), pr_rotate(k.rot)));
    pr_mat_t b = pr_mul(m, pr_about(bx, FX(80), FX(144)));

    layer_effects_back(m, &l, ph);
    if (md->ov == OV_PLAY) layer_overlay(m, b, md, ph, still);
    layer_shadow(m, md, ph, still);
    layer_outfit_back(b, &l);
    layer_ears_feet_arms(b, md, ph, still);
    layer_body(b, md);
    layer_outfit_front(b, &l);
    layer_antenna(b, &l, md, ph, still);
    layer_eyes(b, md, pose);
    layer_mouth(b, md);
    layer_acc(b, &l);
    layer_hat(b, &l);
    if (md->ov != OV_PLAY) layer_overlay(m, b, md, ph, still);
    layer_effects_front(m, &l, mood, ph, still);
}
```

- [ ] **Step 5: Run the tests to verify they pass** (`pip_art OK`).

- [ ] **Step 6: Commit** — `feat(pip): Pip character data, moods and keyframes from the Pet artboard`

---

### Task 3: `pip_brain` — the pet model

**Files:**
- Create: `gui/pip/pip_brain.h`, `gui/pip/pip_brain.c`
- Test: `tests/host/test_pip_brain.c`
- Modify: `Makefile` (`HT_SRC_pip_brain := gui/pip/pip_brain.c`)

**Interfaces:**
- Consumes: `pip_look_t`, `pip_mood_t` (`pip_art.h`, types only).
- Produces:
  - Event and mode enums: `pip_event_t` (`PE_PAT`, `PE_RUN_OK`, `PE_RUN_FAIL`, `PE_CONFUSED`, `PE_LOADING`, `PE_WELCOME_BACK`, `PE_SCRIPT`), `pip_mode_t` (`PIP_PERCH`, `PIP_ROAM`, `PIP_NAP`).
  - State: `pip_brain_t`.
  - Time helpers: `pip_day_number(yyyymmdd)`, `pip_minutes(yyyymmdd, h, m)`.
  - Lifecycle: `pip_brain_new`, `pip_brain_signin` (returns minutes away), `pip_brain_advance(b, now_min)`.
  - Events and queries: `pip_brain_pat`, `pip_brain_event(b, ev, now_ms, script_mood)`, `pip_brain_mood(b, ctx, now_ms)`, `pip_brain_level`, `pip_brain_age_days`.
  - Persistence: `pip_brain_save(b, out, cap)`, `pip_brain_load(b, text, len)`.

- [ ] **Step 1: Write the failing test**

<!-- file: tests/host/test_pip_brain.c -->
```c
/* tests/host/test_pip_brain.c -- Pip's pet model. */
#include "pure.h"
#include "pip_brain.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define DAY 20261004u

static pip_ctx_t ctx(u32 idle_ms, u8 hour, bool near) { pip_ctx_t c = { idle_ms, hour, near }; return c; }

int main(void) {
    assert(pip_day_number(20000101) == 0 && pip_day_number(20000102) == 1);
    assert(pip_day_number(20010101) == 366);                       /* 2000 was a leap year */
    assert(pip_minutes(20000102, 1, 30) == 1440 + 90);
    u32 now = pip_minutes(DAY, 12, 0);

    pip_brain_t b;
    pip_brain_new(&b, DAY, now);
    assert(b.happy == 70 && b.energy == 80 && pip_brain_level(&b) == 1 && !strcmp(b.name, "Pip"));
    assert(b.roam && b.follow && b.nap && b.mode == PIP_PERCH);
    assert(pip_brain_age_days(&b, DAY) == 0 && pip_brain_age_days(&b, 20261007) == 3);

    /* happiness: -1 per 30 minutes */
    pip_brain_advance(&b, now + 60);
    assert(b.happy == 68);
    /* roaming costs energy, napping restores it */
    b.mode = PIP_ROAM;
    pip_brain_advance(&b, now + 80);
    assert(b.energy == 70);
    b.mode = PIP_NAP;
    pip_brain_advance(&b, now + 85);
    assert(b.energy == 80);
    b.mode = PIP_PERCH;

    /* pats: +8, capped at +24 per hour; friendship always grows */
    b.happy = 40;
    u32 f0 = b.friend_pts;
    for (u32 i = 0; i < 100; i++) pip_brain_pat(&b, now + 90);
    assert(b.happy == 64 && b.friend_pts == f0 + 100);
    pip_brain_pat(&b, now + 90 + 61);                              /* a new hour */
    assert(b.happy == 72);
    pip_brain_t lv = b; lv.friend_pts = 1000;
    assert(pip_brain_level(&lv) == 10);

    /* moods: an event beats the resting mood, then expires */
    pip_brain_t m;
    pip_brain_new(&m, DAY, now);
    assert(pip_brain_mood(&m, &(pip_ctx_t){0, 12, false}, 0) == PM_IDLE);
    pip_brain_event(&m, PE_RUN_FAIL, 1000, 0);
    assert(pip_brain_mood(&m, &(pip_ctx_t){0, 12, false}, 2000) == PM_SAD);
    assert(pip_brain_mood(&m, &(pip_ctx_t){0, 12, false}, 20000) == PM_IDLE);
    pip_brain_event(&m, PE_PAT, 30000, 0);
    assert(pip_brain_mood(&m, &(pip_ctx_t){0, 12, false}, 31000) == PM_HAPPY);
    pip_brain_event(&m, PE_SCRIPT, 40000, PM_CONFUSED);
    assert(pip_brain_mood(&m, &(pip_ctx_t){0, 12, false}, 41000) == PM_CONFUSED);
    /* resting rules, in priority order */
    pip_ctx_t c = ctx(0, 12, true);
    assert(pip_brain_mood(&m, &c, 100000) == PM_WATCHING);
    m.follow = false;
    assert(pip_brain_mood(&m, &c, 100000) == PM_IDLE);
    m.mode = PIP_ROAM;
    assert(pip_brain_mood(&m, &c, 100000) == PM_PLAYING);
    m.happy = 20;
    assert(pip_brain_mood(&m, &c, 100000) == PM_SAD);
    c = ctx(11 * 60000, 12, false);
    assert(pip_brain_mood(&m, &c, 100000) == PM_SLEEPING);         /* ten idle minutes, nap on */
    m.nap = false;
    assert(pip_brain_mood(&m, &c, 100000) == PM_SAD);
    c = ctx(3 * 60000, 23, false);
    assert(pip_brain_mood(&m, &c, 100000) == PM_SLEEPING);         /* late, two idle minutes */
    m.happy = 70; m.energy = 10;
    c = ctx(0, 12, false);
    assert(pip_brain_mood(&m, &c, 100000) == PM_SLEEPING);         /* worn out */
    m.energy = 80; m.mode = PIP_NAP;
    assert(pip_brain_mood(&m, &c, 100000) == PM_SLEEPING);

    /* time away: catch-up capped, happiness floored at 30, energy restored */
    pip_brain_t a;
    pip_brain_new(&a, DAY, now);
    a.happy = 90; a.energy = 20;
    u32 away = pip_brain_signin(&a, 20261020, pip_minutes(20261020, 9, 0));
    assert(away == 3 * 1440);                                      /* capped at 3 days */
    assert(a.happy == 30 && a.energy == 100 && a.days == 2);
    /* a low-happiness Pip isn't pushed lower than where he was */
    a.happy = 20;
    pip_brain_signin(&a, 20261021, pip_minutes(20261021, 9, 0));
    assert(a.happy == 20);
    /* the clock stepping backwards: no underflow, nothing lost */
    pip_brain_t back;
    pip_brain_new(&back, DAY, now);
    assert(pip_brain_signin(&back, DAY, now - 500) == 0 && back.happy == 70);
    pip_brain_advance(&back, now - 1000);
    assert(back.happy == 70);

    /* save / load round-trip */
    pip_brain_t s;
    pip_brain_new(&s, DAY, now);
    strcpy(s.name, "Biscuit");
    s.look.color = 3; s.look.hat = 2; s.look.outfit = 1; s.look.acc = 2; s.look.effect = 4;
    s.happy = 55; s.energy = 33; s.friend_pts = 77; s.days = 9; s.roam = false; s.mode = PIP_NAP;
    char buf[512];
    u32 n = pip_brain_save(&s, buf, sizeof buf);
    assert(n > 0 && !strncmp(buf, "PIP 1\n", 6));
    pip_brain_t r;
    assert(pip_brain_load(&r, buf, n));
    assert(!strcmp(r.name, "Biscuit") && r.look.color == 3 && r.look.hat == 2 && r.look.outfit == 1);
    assert(r.look.acc == 2 && r.look.effect == 4 && r.happy == 55 && r.energy == 33);
    assert(r.friend_pts == 77 && r.days == 9 && !r.roam && r.mode == PIP_NAP);
    assert(r.born_day == s.born_day && r.seen_min == s.seen_min);
    /* damaged or hostile files: fresh or clamped, never trusted blindly */
    assert(!pip_brain_load(&r, "garbage", 7));
    assert(!pip_brain_load(&r, "", 0));
    const char *wild = "PIP 1\nhappy=999\nenergy=-5\ncolor=77\nhat=200\nname=Abcdefghijklmnopqrstuvwxyz0123456789\n";
    assert(pip_brain_load(&r, wild, (u32)strlen(wild)));
    assert(r.happy == 100 && r.energy == 0 && r.look.color == 0 && r.look.hat == 0);
    assert(strlen(r.name) <= 14);
    const char *empty_name = "PIP 1\nname=\n";
    assert(pip_brain_load(&r, empty_name, (u32)strlen(empty_name)) && !strcmp(r.name, "Pip"));
    puts("pip_brain OK");
    return 0;
}
```

- [ ] **Step 2: Register and run, expect failure.**

- [ ] **Step 3: Write the header**

<!-- file: gui/pip/pip_brain.h -->
```c
/* gui/pip/pip_brain.h -- Pip's pet model (pure, host-tested).
 * Stats, the mood stack, time-away catch-up and the ~/.pip text format.
 * Time is in minutes since 2000-01-01 (from the RTC); event moods use the
 * millisecond uptime. */
#ifndef CAREOS_PIP_BRAIN_H
#define CAREOS_PIP_BRAIN_H
#include "pip_art.h"

typedef enum { PE_PAT = 0, PE_RUN_OK, PE_RUN_FAIL, PE_CONFUSED, PE_LOADING, PE_WELCOME_BACK, PE_SCRIPT } pip_event_t;
typedef enum { PIP_PERCH = 0, PIP_ROAM, PIP_NAP } pip_mode_t;

typedef struct {
    char       name[15];
    pip_look_t look;
    u8         happy, energy;        /* 0..100 */
    u16        friend_pts;
    u32        born_day;             /* yyyymmdd */
    u32        last_day;             /* yyyymmdd of the last day counted */
    u16        days;                 /* days together */
    u32        seen_min;             /* minutes since 2000 at the last advance */
    u32        hour_start_min;       /* pat-cap window */
    u8         pat_gain_hour;        /* happiness from pats in that window */
    u16        happy_carry, energy_carry;   /* minutes toward the next point */
    bool       roam, follow, nap;
    u8         mode;                 /* pip_mode_t */
    /* not saved */
    u8         event_mood;
    u32        event_until_ms;
} pip_brain_t;

typedef struct { u32 idle_ms; u8 hour; bool cursor_near; } pip_ctx_t;

u32  pip_day_number(u32 yyyymmdd);
u32  pip_minutes(u32 yyyymmdd, u32 hour, u32 minute);

void pip_brain_new(pip_brain_t *b, u32 today, u32 now_min);
u32  pip_brain_signin(pip_brain_t *b, u32 today, u32 now_min);
void pip_brain_advance(pip_brain_t *b, u32 now_min);
void pip_brain_pat(pip_brain_t *b, u32 now_min);
void pip_brain_event(pip_brain_t *b, pip_event_t ev, u32 now_ms, u8 script_mood);
pip_mood_t pip_brain_mood(const pip_brain_t *b, const pip_ctx_t *c, u32 now_ms);
u32  pip_brain_level(const pip_brain_t *b);
u32  pip_brain_age_days(const pip_brain_t *b, u32 today);
u32  pip_brain_save(const pip_brain_t *b, char *out, u32 cap);
bool pip_brain_load(pip_brain_t *b, const char *text, u32 len);

#endif
```

- [ ] **Step 4: Write the implementation**

<!-- file: gui/pip/pip_brain.c -->
```c
/* gui/pip/pip_brain.c -- Pip's pet model (pure). See pip_brain.h. */
#include "pip_brain.h"

#define CATCHUP_MAX  (3u * 1440u)
#define HAPPY_FLOOR  30
#define PAT_GAIN     8
#define PAT_CAP      24

/* Days since 1970 for a civil date (Howard Hinnant's algorithm). */
static i32 days_civil(i32 y, u32 m, u32 d) {
    y -= m <= 2;
    i32 era = (y >= 0 ? y : y - 399) / 400;
    u32 yoe = (u32)(y - era * 400);
    u32 doy = (153 * (m + (m > 2 ? (u32)-3 : 9)) + 2) / 5 + d - 1;
    u32 doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (i32)doe - 719468;
}

u32 pip_day_number(u32 ymd) {
    i32 y = (i32)(ymd / 10000), m = (i32)(ymd / 100 % 100), d = (i32)(ymd % 100);
    if (m < 1 || m > 12 || d < 1 || d > 31) return 0;
    i32 n = days_civil(y, (u32)m, (u32)d) - days_civil(2000, 1, 1);
    return n < 0 ? 0 : (u32)n;
}
u32 pip_minutes(u32 ymd, u32 hour, u32 minute) { return pip_day_number(ymd) * 1440u + hour * 60u + minute; }

static void copy_name(char *dst, const char *src, u32 n) {
    u32 o = 0;
    for (u32 i = 0; i < n && o < 14; i++) {
        char c = src[i];
        if (c >= 32 && c < 127) dst[o++] = c;
    }
    dst[o] = '\0';
    if (!o) { dst[0] = 'P'; dst[1] = 'i'; dst[2] = 'p'; dst[3] = '\0'; }
}

void pip_brain_new(pip_brain_t *b, u32 today, u32 now_min) {
    for (u32 i = 0; i < sizeof *b; i++) ((u8 *)b)[i] = 0;
    copy_name(b->name, "Pip", 3);
    b->happy = 70; b->energy = 80;
    b->born_day = today; b->last_day = today; b->days = 1;
    b->seen_min = now_min; b->hour_start_min = now_min;
    b->roam = b->follow = b->nap = true;
    b->mode = PIP_PERCH;
}

static u8 clamp100(i32 v) { return (u8)(v < 0 ? 0 : v > 100 ? 100 : v); }

void pip_brain_advance(pip_brain_t *b, u32 now_min) {
    if (now_min <= b->seen_min) return;                  /* clock went backwards: ignore */
    u32 dt = now_min - b->seen_min;
    if (dt > CATCHUP_MAX) dt = CATCHUP_MAX;
    b->seen_min = now_min;
    u32 h = b->happy_carry + dt;
    b->happy = clamp100((i32)b->happy - (i32)(h / 30));
    b->happy_carry = (u16)(h % 30);
    if (b->mode == PIP_NAP) {
        b->energy = clamp100((i32)b->energy + 2 * (i32)dt);
        b->energy_carry = 0;
    } else if (b->mode == PIP_ROAM) {
        u32 e = b->energy_carry + dt;
        b->energy = clamp100((i32)b->energy - (i32)(e / 2));
        b->energy_carry = (u16)(e % 2);
    }
}

u32 pip_brain_signin(pip_brain_t *b, u32 today, u32 now_min) {
    u32 away = now_min > b->seen_min ? now_min - b->seen_min : 0;
    if (away > CATCHUP_MAX) away = CATCHUP_MAX;
    if (away) {
        i32 h = (i32)b->happy - (i32)(away / 30);
        if (b->happy >= HAPPY_FLOOR && h < HAPPY_FLOOR) h = HAPPY_FLOOR;   /* missed you, not miserable */
        if (b->happy < HAPPY_FLOOR) h = b->happy;
        b->happy = clamp100(h);
        b->energy = away >= 360 ? 100 : clamp100((i32)b->energy + 2 * (i32)away);
    }
    if (now_min > b->seen_min) b->seen_min = now_min;
    if (pip_day_number(today) > pip_day_number(b->last_day)) {
        b->days++;
        b->friend_pts = (u16)(b->friend_pts + 5);
        b->last_day = today;
    }
    b->mode = PIP_PERCH;
    return away;
}

void pip_brain_pat(pip_brain_t *b, u32 now_min) {
    if (now_min >= b->hour_start_min + 60 || now_min < b->hour_start_min) {
        b->hour_start_min = now_min;
        b->pat_gain_hour = 0;
    }
    u32 room = PAT_CAP > b->pat_gain_hour ? PAT_CAP - b->pat_gain_hour : 0;
    u32 gain = room < PAT_GAIN ? room : PAT_GAIN;
    b->happy = clamp100((i32)b->happy + (i32)gain);
    b->pat_gain_hour = (u8)(b->pat_gain_hour + gain);
    if (b->friend_pts < 60000) b->friend_pts++;
}

void pip_brain_event(pip_brain_t *b, pip_event_t ev, u32 now_ms, u8 script_mood) {
    u8 mood; u32 ms;
    switch (ev) {
    case PE_PAT:          mood = PM_HAPPY;    ms = 6000; break;
    case PE_RUN_OK:       mood = PM_EXCITED;  ms = 5000; break;
    case PE_RUN_FAIL:     mood = PM_SAD;      ms = 8000; break;
    case PE_CONFUSED:     mood = PM_CONFUSED; ms = 4000; break;
    case PE_LOADING:      mood = PM_THINKING; ms = 4000; break;
    case PE_WELCOME_BACK: mood = PM_EXCITED;  ms = 6000; break;
    default:              mood = script_mood < PM_COUNT ? script_mood : PM_IDLE; ms = 6000; break;
    }
    b->event_mood = mood;
    b->event_until_ms = now_ms + ms;
}

pip_mood_t pip_brain_mood(const pip_brain_t *b, const pip_ctx_t *c, u32 now_ms) {
    if (b->event_until_ms && now_ms < b->event_until_ms) return (pip_mood_t)b->event_mood;
    bool night = c->hour >= 23 || c->hour < 7;
    if (b->mode == PIP_NAP || b->energy < 15 || (b->nap && c->idle_ms >= 10u * 60000u) ||
        (night && c->idle_ms >= 2u * 60000u))
        return PM_SLEEPING;
    if (b->happy < 25) return PM_SAD;
    if (b->mode == PIP_ROAM) return PM_PLAYING;
    if (b->follow && c->cursor_near) return PM_WATCHING;
    return PM_IDLE;
}

u32 pip_brain_level(const pip_brain_t *b) { u32 l = 1 + b->friend_pts / 40u; return l > 10 ? 10 : l; }

u32 pip_brain_age_days(const pip_brain_t *b, u32 today) {
    u32 t = pip_day_number(today), s = pip_day_number(b->born_day);
    return t > s ? t - s : 0;
}

/* ---- ~/.pip ---- */
static u32 put(char *o, u32 cap, u32 n, const char *s) {
    while (*s && n + 1 < cap) o[n++] = *s++;
    if (n < cap) o[n] = '\0';
    return n;
}
static u32 put_kv(char *o, u32 cap, u32 n, const char *k, u32 v) {
    char d[12]; u32 i = 0;
    do { d[i++] = (char)('0' + v % 10); v /= 10; } while (v && i < 11);
    n = put(o, cap, n, k); n = put(o, cap, n, "=");
    while (i && n + 1 < cap) o[n++] = d[--i];
    return put(o, cap, n, "\n");
}

u32 pip_brain_save(const pip_brain_t *b, char *o, u32 cap) {
    if (!o || cap < 64) return 0;
    u32 n = put(o, cap, 0, "PIP 1\nname=");
    n = put(o, cap, n, b->name); n = put(o, cap, n, "\n");
    n = put_kv(o, cap, n, "color", b->look.color);
    n = put_kv(o, cap, n, "hat", b->look.hat);
    n = put_kv(o, cap, n, "outfit", b->look.outfit);
    n = put_kv(o, cap, n, "acc", b->look.acc);
    n = put_kv(o, cap, n, "effect", b->look.effect);
    n = put_kv(o, cap, n, "happy", b->happy);
    n = put_kv(o, cap, n, "energy", b->energy);
    n = put_kv(o, cap, n, "friend", b->friend_pts);
    n = put_kv(o, cap, n, "born", b->born_day);
    n = put_kv(o, cap, n, "lastday", b->last_day);
    n = put_kv(o, cap, n, "days", b->days);
    n = put_kv(o, cap, n, "seen", b->seen_min);
    n = put_kv(o, cap, n, "roam", b->roam);
    n = put_kv(o, cap, n, "follow", b->follow);
    n = put_kv(o, cap, n, "nap", b->nap);
    n = put_kv(o, cap, n, "mode", b->mode);
    return n;
}

static bool key_eq(const char *s, u32 n, const char *k) {
    u32 i = 0;
    for (; k[i]; i++) if (i >= n || s[i] != k[i]) return false;
    return i == n;
}
static i64 parse_int(const char *s, u32 n) {
    i64 v = 0; bool neg = false; u32 i = 0;
    if (n && s[0] == '-') { neg = true; i = 1; }
    for (; i < n && s[i] >= '0' && s[i] <= '9'; i++) if (v < 100000000) v = v * 10 + (s[i] - '0');
    return neg ? -v : v;
}
static u32 clampu(i64 v, u32 lo, u32 hi) { return v < (i64)lo ? lo : v > (i64)hi ? hi : (u32)v; }

bool pip_brain_load(pip_brain_t *b, const char *t, u32 len) {
    if (!t || len < 6 || t[0] != 'P' || t[1] != 'I' || t[2] != 'P' || t[3] != ' ' || t[4] != '1') return false;
    pip_brain_new(b, 20260101, 0);
    u32 i = 0;
    while (i < len && t[i] != '\n') i++;
    while (i < len) {
        i++;
        u32 ls = i;
        while (i < len && t[i] != '\n') i++;
        const char *l = t + ls; u32 n = i - ls, eq = 0;
        while (eq < n && l[eq] != '=') eq++;
        if (eq == n) continue;
        const char *v = l + eq + 1; u32 vn = n - eq - 1;
        i64 x = parse_int(v, vn);
        if (key_eq(l, eq, "name"))         copy_name(b->name, v, vn);
        else if (key_eq(l, eq, "color"))   b->look.color = (u8)(x >= 0 && x < PIP_COLORS ? x : 0);
        else if (key_eq(l, eq, "hat"))     b->look.hat = (u8)(x >= 0 && x < PIP_HATS ? x : 0);
        else if (key_eq(l, eq, "outfit"))  b->look.outfit = (u8)(x >= 0 && x < PIP_OUTFITS ? x : 0);
        else if (key_eq(l, eq, "acc"))     b->look.acc = (u8)(x >= 0 && x < PIP_ACCS ? x : 0);
        else if (key_eq(l, eq, "effect"))  b->look.effect = (u8)(x >= 0 && x < PIP_EFFECTS ? x : 0);
        else if (key_eq(l, eq, "happy"))   b->happy = (u8)clampu(x, 0, 100);
        else if (key_eq(l, eq, "energy"))  b->energy = (u8)clampu(x, 0, 100);
        else if (key_eq(l, eq, "friend"))  b->friend_pts = (u16)clampu(x, 0, 60000);
        else if (key_eq(l, eq, "born"))    b->born_day = clampu(x, 20000101, 99991231);
        else if (key_eq(l, eq, "lastday")) b->last_day = clampu(x, 20000101, 99991231);
        else if (key_eq(l, eq, "days"))    b->days = (u16)clampu(x, 1, 60000);
        else if (key_eq(l, eq, "seen"))    b->seen_min = clampu(x, 0, 0x7fffffff);
        else if (key_eq(l, eq, "roam"))    b->roam = x != 0;
        else if (key_eq(l, eq, "follow"))  b->follow = x != 0;
        else if (key_eq(l, eq, "nap"))     b->nap = x != 0;
        else if (key_eq(l, eq, "mode"))    b->mode = (u8)clampu(x, 0, 2);
    }
    b->hour_start_min = b->seen_min;
    return true;
}
```

- [ ] **Step 5: Run the tests to verify they pass** (`pip_brain OK`). Then run the whole suite: `make host-test 2>&1 | grep -c '  HOST'`, which should print 30.

- [ ] **Step 6: Commit** — `feat(pip): pet model -- stats, mood stack, catch-up, ~/.pip format`

---

### Task 4: Runtime — sprite cache, `pip.c`, kernel build

**Files:**
- Create: `gui/pip/pip_sprite.h`, `gui/pip/pip_sprite.c`, `gui/pip/pip.h`, `gui/pip/pip.c`
- Modify:
  - `Makefile`: add `gui/pip/pip_raster.c gui/pip/pip_art.c gui/pip/pip_brain.c gui/pip/pip_sprite.c gui/pip/pip.c gui/pip/pip_desktop.c` to the kernel sources after `gui/spring.c`; add `-Igui/pip` to the kernel `-I` list.
  - `gui/session/common.c`: replace the `session_pet_slot` stub with a call to `pip_cameo`.

**Interfaces:**
- Consumes:
  - From Tasks 1–3: `pip_art_draw`, `pip_mood_cycle_ms`, `pip_brain_*`.
  - Kernel: `kmalloc`, `kfree`, `kmemset`, `rtc_read`, `clock_read_local`, `timer_ms`, `vfs_*`, `app_home_path`, `user_current_uid`, `gfx_blend_argb`.
- Produces:
  - Sprites: `pip_sprite_get(const pip_look_t*, const pip_pose_t*, u32 size, pip_frame_t *out)`, `pip_sprite_flush()`.
  - Session lifecycle: `pip_session_start()`, `pip_session_end()`.
  - Per-frame: `pip_tick(i32 mx, i32 my, u32 idle_ms)`.
  - Events: `pip_event(pip_event_t)`, `pip_script_mood(const char*)`, `pip_say(const char*, u32 ms)`.
  - State: `pip_brain()`, `pip_mood()`, `pip_set_look(pip_look_t)`, `pip_set_mode(pip_mode_t)`, `pip_mark_dirty()`.
  - Drawing: `pip_draw(i32 x, i32 y, u32 size, u8 mood, bool still)`, where (x, y) is the design-box top-left.
  - Bubble: `pip_bubble_text()`, `pip_bubble_active()`.
  - Lock / power: `pip_cameo(rect_t r, u32 kind)`.

- [ ] **Step 1: Sprite cache**

<!-- file: gui/pip/pip_sprite.h -->
```c
/* gui/pip/pip_sprite.h -- cached Pip frames (kernel). */
#ifndef CAREOS_PIP_SPRITE_H
#define CAREOS_PIP_SPRITE_H
#include "kernel.h"
#include "pip_art.h"

#define PIP_FRAMES 12                       /* frames per mood cycle */

typedef struct { const u32 *px; i32 w, h; i32 ox, oy; } pip_frame_t;  /* ox,oy: design (0,0) in the sprite */

/* Straight-ARGB frame for this look/pose/size (size = width of the 160-unit box). */
bool pip_sprite_get(const pip_look_t *look, const pip_pose_t *pose, u32 size, pip_frame_t *out);
void pip_sprite_flush(void);

#endif
```

<!-- file: gui/pip/pip_sprite.c -->
```c
/* gui/pip/pip_sprite.c -- an LRU of rendered Pip frames, budgeted by bytes.
 * Key: look, mood, frame (phase / 12), blink, look offset, still, size. */
#include "pip_sprite.h"

#define PS_SLOTS  96
#define PS_BUDGET (6u * 1024u * 1024u)

typedef struct {
    u32 key_look, key_pose, size;
    u32 *px; i32 w, h;
    u32 used;
} ps_slot_t;

static ps_slot_t g_slots[PS_SLOTS];
static u32 g_bytes, g_clock;

static u32 look_key(const pip_look_t *l) {
    return (u32)l->color | ((u32)l->hat << 4) | ((u32)l->outfit << 8) | ((u32)l->acc << 12) | ((u32)l->effect << 16);
}
static u32 pose_key(const pip_pose_t *p) {
    u32 frame = p->still ? 0 : (u32)p->phase * PIP_FRAMES / 65536u;
    return (u32)p->mood | (frame << 4) | ((u32)p->blink << 9) | ((u32)p->still << 10) |
           ((u32)(u8)p->look_x << 12) | ((u32)(u8)p->look_y << 20);
}

static void evict_one(void) {
    u32 best = PS_SLOTS, oldest = 0xFFFFFFFFu;
    for (u32 i = 0; i < PS_SLOTS; i++)
        if (g_slots[i].px && g_slots[i].used < oldest) { oldest = g_slots[i].used; best = i; }
    if (best == PS_SLOTS) return;
    g_bytes -= (u32)(g_slots[best].w * g_slots[best].h * 4);
    kfree(g_slots[best].px);
    g_slots[best].px = NULL;
}

void pip_sprite_flush(void) {
    for (u32 i = 0; i < PS_SLOTS; i++) if (g_slots[i].px) { kfree(g_slots[i].px); g_slots[i].px = NULL; }
    g_bytes = 0;
}

bool pip_sprite_get(const pip_look_t *look, const pip_pose_t *pose, u32 size, pip_frame_t *out) {
    if (size < 16 || size > 400) return false;
    pip_pose_t q = *pose;
    if (!q.still) q.phase = (u16)(((u32)q.phase * PIP_FRAMES / 65536u) * 65536u / PIP_FRAMES);  /* snap */
    u32 lk = look_key(look), pk = pose_key(&q);
    i32 w = (i32)(size * 240 / 160), h = (i32)(size * 200 / 160);
    out->w = w; out->h = h; out->ox = (i32)(size * 40 / 160); out->oy = (i32)(size * 24 / 160);
    g_clock++;
    u32 free_slot = PS_SLOTS;
    for (u32 i = 0; i < PS_SLOTS; i++) {
        ps_slot_t *s = &g_slots[i];
        if (!s->px) { if (free_slot == PS_SLOTS) free_slot = i; continue; }
        if (s->key_look == lk && s->key_pose == pk && s->size == size) { s->used = g_clock; out->px = s->px; return true; }
    }
    u32 bytes = (u32)(w * h * 4);
    while (g_bytes + bytes > PS_BUDGET && g_bytes) evict_one();
    if (free_slot == PS_SLOTS) { evict_one(); for (u32 i = 0; i < PS_SLOTS; i++) if (!g_slots[i].px) { free_slot = i; break; } }
    if (free_slot == PS_SLOTS) return false;
    u32 *px = (u32 *)kmalloc(bytes);
    if (!px) return false;
    kmemset(px, 0, bytes);
    pr_canvas_t c = { px, w, h };
    i32 k = (i32)(size * 65536 / 160);
    pr_mat_t m = pr_mul(pr_scale(k, k), pr_translate(40 * 65536, 24 * 65536));
    pip_art_draw(&c, m, look, &q);
    pr_unpremultiply(&c);
    ps_slot_t *s = &g_slots[free_slot];
    s->key_look = lk; s->key_pose = pk; s->size = size; s->px = px; s->w = w; s->h = h; s->used = g_clock;
    g_bytes += bytes;
    out->px = px;
    return true;
}
```

- [ ] **Step 2: Runtime API**

<!-- file: gui/pip/pip.h -->
```c
/* gui/pip/pip.h -- Pip at runtime: the signed-in user's pet, events in,
 * frames out. One Pip per session; guests get an unsaved one. */
#ifndef CAREOS_PIP_H
#define CAREOS_PIP_H
#include "kernel.h"
#include "gui.h"
#include "pip_brain.h"

void        pip_session_start(void);          /* after sign-in / unlock */
void        pip_session_end(void);            /* sign-out: save */
void        pip_tick(i32 mx, i32 my, u32 idle_ms);   /* every frame */
void        pip_event(pip_event_t ev);
bool        pip_script_mood(const char *name);       /* Care: pip_mood("happy") */
void        pip_say(const char *text, u32 ms);       /* speech bubble */
const char *pip_bubble_text(void);                   /* NULL when none */
pip_brain_t *pip_brain(void);
pip_mood_t  pip_mood(void);
void        pip_set_look(pip_look_t look);
void        pip_set_mode(pip_mode_t mode);
void        pip_mark_dirty(void);
bool        pip_saved(void);                         /* false for guests */
u32         pip_today(void);                         /* yyyymmdd (local) */
/* Draw Pip with the 160-unit box's top-left at (x, y), size px wide.
 * mood PM_COUNT = his current mood. still = no animation. */
void        pip_draw(i32 x, i32 y, u32 size, u32 mood, bool still);
/* Lock (kind 0) / power (kind 1) cameo inside r. */
void        pip_cameo(rect_t r, u32 kind);
/* Cursor position used for "watching", set by pip_desktop. */
void        pip_set_anchor(i32 cx, i32 cy);

#endif
```

<!-- file: gui/pip/pip.c -->
```c
/* gui/pip/pip.c -- Pip at runtime. See pip.h and the spec
 * docs/superpowers/specs/2026-10-04-careos-pip-design.md. */
#include "pip.h"
#include "pip_sprite.h"

static pip_brain_t g_b;
static bool g_ready, g_saved, g_dirty;
static u32  g_last_save_ms, g_last_tick_min;
static pip_mood_t g_mood = PM_IDLE;
static u32  g_mood_start_ms;
static char g_bubble[64];
static u32  g_bubble_until;
static i32  g_anchor_x = -100000, g_anchor_y = -100000;
static i32  g_mx, g_my;
static u32  g_blink_at;

static void pip_path(char *out) { app_home_path(out); kstrcat(out, "/.pip"); }

u32 pip_today(void) {
    rtc_time_t t; clock_read_local(&t);
    return (u32)t.year * 10000u + (u32)t.month * 100u + t.day;
}
static u32 now_min(void) {
    rtc_time_t t; clock_read_local(&t);
    return pip_minutes((u32)t.year * 10000u + (u32)t.month * 100u + t.day, t.hour, t.minute);
}

static void save_now(void) {
    if (!g_saved) return;
    char path[80]; pip_path(path);
    char buf[512];
    u32 n = pip_brain_save(&g_b, buf, sizeof buf);
    fs_node_t *f = vfs_resolve_path(path);
    if (!f) {
        char home[64]; app_home_path(home);
        fs_node_t *h = vfs_resolve_path(home);
        if (h) f = vfs_mkfile(h, ".pip");
    }
    if (f) vfs_write(f, buf, n);
    g_dirty = false;
    g_last_save_ms = timer_ms();
}

void pip_session_start(void) {
    u32 today = pip_today(), nm = now_min();
    char home[64]; app_home_path(home);
    g_saved = user_current_uid() == 0 || vfs_resolve_path(home) != NULL;
    char path[80]; pip_path(path);
    fs_node_t *f = g_saved ? vfs_resolve_path(path) : NULL;
    if (!f || !pip_brain_load(&g_b, vfs_file_str(f), f->size)) pip_brain_new(&g_b, today, nm);
    u32 away = pip_brain_signin(&g_b, today, nm);
    g_ready = true;
    g_last_tick_min = nm;
    g_mood = PM_IDLE; g_mood_start_ms = timer_ms();
    pip_sprite_flush();
    if (away >= 60) {
        pip_brain_event(&g_b, PE_WELCOME_BACK, timer_ms(), 0);
        pip_say("You're back!", 3000);
    }
    g_dirty = true;
    save_now();
}

void pip_session_end(void) { if (g_ready) save_now(); g_ready = false; pip_sprite_flush(); }

pip_brain_t *pip_brain(void) { return &g_b; }
pip_mood_t pip_mood(void) { return g_mood; }
bool pip_saved(void) { return g_saved; }
void pip_mark_dirty(void) { g_dirty = true; }
void pip_set_anchor(i32 cx, i32 cy) { g_anchor_x = cx; g_anchor_y = cy; }

void pip_set_look(pip_look_t look) { g_b.look = look; g_dirty = true; }
void pip_set_mode(pip_mode_t mode) { g_b.mode = (u8)mode; g_dirty = true; }

void pip_say(const char *text, u32 ms) {
    if (!text) return;
    kstrncpy(g_bubble, text, sizeof g_bubble - 1);
    g_bubble[sizeof g_bubble - 1] = '\0';
    g_bubble_until = timer_ms() + ms;
}
const char *pip_bubble_text(void) { return (g_bubble[0] && timer_ms() < g_bubble_until) ? g_bubble : NULL; }

static const char *const EVENT_LINES[] = {
    "Hehe, that tickles!", "It worked!!", "Hmm, that didn't run.", "Huh? What's that?",
    "Hmm...", "You're back!", NULL
};

void pip_event(pip_event_t ev) {
    if (!g_ready) return;
    u32 now = timer_ms();
    if (ev == PE_PAT) { pip_brain_pat(&g_b, now_min()); g_dirty = true; }
    pip_brain_event(&g_b, ev, now, 0);
    if (ev < PE_SCRIPT && EVENT_LINES[ev] && (!pip_bubble_text() || ev != PE_PAT)) pip_say(EVENT_LINES[ev], 2600);
}

bool pip_script_mood(const char *name) {
    for (u32 m = 0; m < PM_COUNT; m++) {
        const char *l = pip_mood_label(m);
        bool eq = true;
        for (u32 i = 0; name[i] || l[i]; i++) {
            char a = name[i], b = l[i];
            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b) { eq = false; break; }
        }
        if (eq || (m == PM_IDLE && !kstrcmp(name, "idle"))) {
            pip_brain_event(&g_b, PE_SCRIPT, timer_ms(), (u8)m);
            return true;
        }
    }
    return false;
}

void pip_tick(i32 mx, i32 my, u32 idle_ms) {
    if (!g_ready) return;
    g_mx = mx; g_my = my;
    u32 nm = now_min();
    if (nm != g_last_tick_min) { pip_brain_advance(&g_b, nm); g_last_tick_min = nm; g_dirty = true; }
    rtc_time_t t; clock_read_local(&t);
    i32 dx = mx - g_anchor_x, dy = my - g_anchor_y;
    pip_ctx_t c = { idle_ms, t.hour, dx * dx + dy * dy < 200 * 200 };
    pip_mood_t m = pip_brain_mood(&g_b, &c, timer_ms());
    if (m != g_mood) { g_mood = m; g_mood_start_ms = timer_ms(); }
    if (g_dirty && timer_ms() - g_last_save_ms > 10000) save_now();
}

void pip_draw(i32 x, i32 y, u32 size, u32 mood, bool still) {
    pip_pose_t p;
    p.mood = (u8)(mood < PM_COUNT ? mood : g_mood);
    u32 cyc = pip_mood_cycle_ms(p.mood);
    u32 t = timer_ms() - g_mood_start_ms;
    p.phase = (u16)((t % cyc) * 65536u / cyc);
    p.still = still;
    /* blink every 4.2 s for about 120 ms */
    u32 now = timer_ms();
    if (now - g_blink_at > 4200) g_blink_at = now;
    p.blink = !still && now - g_blink_at < 120;
    p.look_x = 0; p.look_y = 0;
    if (p.mood == PM_WATCHING && g_anchor_x > -100000) {
        i32 dx = g_mx - g_anchor_x, dy = g_my - g_anchor_y;
        p.look_x = (i8)(dx > 40 ? 6 : dx < -40 ? -6 : dx * 6 / 40);
        p.look_y = (i8)(dy > 40 ? 4 : dy < -40 ? -4 : dy * 4 / 40);
    }
    pip_frame_t f;
    if (!pip_sprite_get(&g_b.look, &p, size, &f)) return;
    gfx_blend_argb(x - f.ox, y - f.oy, f.px, f.w, f.h);
}

void pip_cameo(rect_t r, u32 kind) {
    if (!g_ready) {                                    /* the login screen: nobody's Pip */
        if (kind == 0) return;
        pip_brain_new(&g_b, pip_today(), now_min());
    }
    u32 size = (u32)(r.h < r.w ? r.h : r.w);
    if (size > 140) size = 140;
    i32 x = r.x + (r.w - (i32)size) / 2, y = r.y + (r.h - (i32)size) / 2;
    pip_draw(x, y, size, kind == 0 ? PM_SLEEPING : PM_CONFUSED, false);
}
```

- [ ] **Step 3: Cameo hook.** In `gui/session/common.c`, replace
```c
void session_pet_slot(rect_t r, u32 mood) { (void)r; (void)mood; }
```
with
```c
#include "pip/pip.h"
/* Pip's cameo: 0 lock (asleep), 1 power menu (worried). Phase 4. */
void session_pet_slot(rect_t r, u32 mood) { pip_cameo(r, mood); }
```

- [ ] **Step 4: Session hooks.** In `gui/gui.c`:
- Call `pip_session_start();` at the end of `enter_desktop()` (after `theme_set_glass(...)`).
- Call `pip_session_end();` where the session is left: the `SESSION_ACT_LOGOUT` path and before the lock screen. Find them with `grep -n "SESSION_ACT_LOGOUT\|SESSION_ACT_LOCK" gui/gui.c`.
- Add `#include "pip/pip.h"`.

- [ ] **Step 5: Mood-sheet QA mode.** Add this to `gui/gui.c`, behind `#ifdef CAREOS_QA_PIP_SHEET` (built with `make QA_PIP_SHEET=1`; add the Makefile flag the same way `QA_AUTOLOGIN` is defined). Right after `pip_session_start()` it draws all 9 moods in a 3×3 grid at 128 px on a `#F3F0FF` background, prints `[pip] sheet ready` to serial, and spins, so QEMU can screenshot it:

```c
#ifdef CAREOS_QA_PIP_SHEET
    {
        gfx_rect(0, 0, (i32)SCREEN_W, (i32)SCREEN_H, rgb(0xF3, 0xF0, 0xFF));
        for (u32 i = 0; i < PM_COUNT; i++) {
            i32 x = 120 + (i32)(i % 3) * 400, y = 80 + (i32)(i / 3) * 300;
            gfx_rect_rounded(x - 20, y - 30, 320, 250, 26, rgb(0xFF, 0xFF, 0xFF));
            pip_draw(x + 60, y, 160, i, true);
            gfx_str_role(x + 10, y + 185, pip_mood_label(i), rgb(0x1A, 0x16, 0x30), FONT_ROLE_DISPLAY, 1, FONT_W_BOLD);
        }
        gfx_flip();
        serial_write("[pip] sheet ready\n");
        for (;;) __asm__ volatile ("hlt");
    }
#endif
```
(`gfx_flip` is the function that presents the frame. Confirm the name with `grep -n "void gfx_flip\|void gfx_present" gui/gui.h` and use whichever exists.)

- [ ] **Step 6: Build and look**

Run (WSL):
```bash
make -j8 2>&1 | grep -E 'error|warning:' | grep -v 'GNU-stack\|Clock skew\|doomgeneric'
make host-test 2>&1 | grep -c '  HOST'
```
Expected: no errors; 30 host tests.

Then build the sheet ISO: `make -j8 QA_AUTOLOGIN=1 QA_PIP_SHEET=1`, copy it to `qa/v10-shell/careos-pip.iso`, and rebuild normally. Boot it with `shots.py` (add an ISO argument or copy it over `careos-qa.iso` temporarily), wait for `[pip] sheet ready`, and screenshot it.

Compare the screenshot with `docs/design/careos-v10/PetMoods.dc.html`. Check that the body, ears, feet, antenna and bulb colours are right, along with the eyes and mouth for each mood and the `?`, `z`, `!` and think-bubble overlays. Fix any coordinate typos in `pip_art.c` until it matches.

- [ ] **Step 7: Commit** — `feat(pip): sprite cache and runtime; lock/power cameos`

---

### Task 5: Pip on the desktop + real events

**Files:**
- Create: `gui/pip/pip_desktop.c` (declarations go in `pip.h`)
- Modify:
  - `gui/gui.c`: draw order, routing, tick.
  - `gui/wm.c`: app-open event, fullscreen query.
  - `apps/app_terminal.c`: unknown command → confused.
  - `apps/app_editor.c`: Run ok / fail.
  - `gui/notify.c`: error notification → confused.
  - `apps/app_browser.c`: page load → thinking.

**Interfaces:**
- Consumes: the Task 4 API, `taskbar_rect()` (or the dock rect accessor; find it with `grep -n "rect_t taskbar\|dock_rect" gui/*.h`), the `windows[]` iteration used by `wm_pointer_owner`, `spring.h` (`spring_t`, `spring_step`).
- Produces (add to `pip.h`):
```c
void pip_desktop_draw(void);                       /* after windows, before menu bar / overlays */
bool pip_desktop_mouse(mouse_t *m);                /* true = consumed */
bool pip_desktop_hidden(void);                     /* fullscreen app focused */
void pip_desktop_go_play(void);
void pip_desktop_come_back(void);
```

- [ ] **Step 1: Write `pip_desktop.c`**

<!-- file: gui/pip/pip_desktop.c -->
```c
/* gui/pip/pip_desktop.c -- Pip on the desktop (spec §4): perched by the
 * dock, roaming along the dock edge and window tops, napping on the dock.
 * Click = pat; right-click = menu; while hovered a status pill shows mode
 * buttons. Only Pip's own rectangles are damaged. */
#include "pip.h"
#include "ui.h"
#include "spring.h"

#define PERCH_SIZE 120
#define NAP_SIZE   110

typedef enum { DM_NONE = 0, DM_PAT, DM_THINK, DM_PLAY, DM_DRESS } dmenu_t;

static i32  g_x, g_y;                 /* design-box top-left */
static i32  g_tx, g_ty;               /* walk target */
static u32  g_roam_until, g_next_hop;
static bool g_menu;
static i32  g_menu_x, g_menu_y;
static rect_t g_last;                 /* last drawn area, for damage */
static bool g_hover;
static u32  g_last_pat_ms;

extern bool taskbar_contains(i32 x, i32 y);

static rect_t dock_rect(void) {
    /* The dock is centred at the bottom: scan its extent from taskbar_contains. */
    i32 y = (i32)SCREEN_H - 40, x0 = (i32)SCREEN_W / 2, x1 = x0;
    while (x0 > 0 && taskbar_contains(x0 - 4, y)) x0 -= 4;
    while (x1 < (i32)SCREEN_W && taskbar_contains(x1 + 4, y)) x1 += 4;
    i32 top = y;
    while (top > 0 && taskbar_contains(x0 + 20, top - 2)) top -= 2;
    return rect_make(x0, top, x1 - x0, (i32)SCREEN_H - top);
}

static u32 cur_size(void) { return pip_brain()->mode == PIP_NAP ? NAP_SIZE : PERCH_SIZE; }

static void home_spot(i32 *x, i32 *y) {
    rect_t d = dock_rect();
    u32 s = cur_size();
    if (pip_brain()->mode == PIP_NAP) { *x = d.x + d.w - (i32)s - 10; *y = d.y - (i32)s * 120 / 160; }
    else { *x = d.x + d.w + 8; *y = d.y + d.h - (i32)s * 150 / 160; }
}

bool pip_desktop_hidden(void) {
    window_t *f = wm_focused();
    return f && (f->app == APP_DOOM || f->app == APP_3D || f->app == APP_MAZE) && f->maximized;
}

void pip_desktop_go_play(void) {
    pip_set_mode(PIP_ROAM);
    g_roam_until = timer_ms() + 40000;
    g_next_hop = 0;
}
void pip_desktop_come_back(void) { pip_set_mode(PIP_PERCH); }

/* Pick the next place to walk to: along the dock top, or onto a window top. */
static void pick_target(void) {
    rect_t d = dock_rect();
    u32 r = timer_ms() * 2654435761u;
    if ((r >> 28) & 1) {
        extern window_t windows[];
        for (u32 tries = 0; tries < 8; tries++) {
            window_t *w = &windows[(r >> 4) % MAX_WINDOWS];
            r = r * 1103515245u + 12345u;
            if (!w->active || w->minimized || w->rect.w < 200) continue;
            g_tx = w->rect.x + 20 + (i32)((r >> 8) % (u32)(w->rect.w - 160));
            g_ty = w->rect.y - (i32)PERCH_SIZE * 150 / 160 + 4;
            if (g_ty < 48) continue;
            return;
        }
    }
    g_tx = d.x + (i32)((r >> 8) % (u32)(d.w > 120 ? d.w - 120 : 1));
    g_ty = d.y - (i32)PERCH_SIZE * 150 / 160 + 6;
}

static void step_motion(void) {
    pip_brain_t *b = pip_brain();
    if (b->mode == PIP_ROAM) {
        if (timer_ms() > g_roam_until || b->energy < 30 || !b->roam) { pip_desktop_come_back(); return; }
        if (timer_ms() > g_next_hop) { pick_target(); g_next_hop = timer_ms() + 3500; }
        i32 dx = g_tx - g_x, dy = g_ty - g_y;
        g_x += dx > 3 ? 3 : dx < -3 ? -3 : dx;
        g_y += dy > 6 ? 6 : dy < -6 ? -6 : dy;
    } else {
        i32 hx, hy; home_spot(&hx, &hy);
        g_x += (hx - g_x) / 4 + ((hx - g_x) > 0) - ((hx - g_x) < 0);
        g_y += (hy - g_y) / 4 + ((hy - g_y) > 0) - ((hy - g_y) < 0);
    }
}

static rect_t pip_rect(void) {
    u32 s = cur_size();
    return rect_make(g_x + (i32)s * 20 / 160, g_y + (i32)s * 30 / 160, (i32)s * 120 / 160, (i32)s * 120 / 160);
}

static void draw_bubble(const char *t, i32 x, i32 y) {
    i32 w = gfx_str_role_width(t, FONT_ROLE_UI, 1, FONT_W_BOLD) + 28, h = 36;
    i32 bx = x - w / 2, by = y - h;
    if (bx < 8) bx = 8;
    if (bx + w > (i32)SCREEN_W - 8) bx = (i32)SCREEN_W - 8 - w;
    if (by < 48) by = 48;
    gfx_shadow_soft(bx, by, w, h, 18);
    gfx_rect_rounded(bx, by, w, h, 18, rgb(0xFF, 0xFF, 0xFF));
    gfx_str_role(bx + 14, by + (h - gfx_role_line_h(FONT_ROLE_UI, 1)) / 2, t, rgb(0x1A, 0x16, 0x30), FONT_ROLE_UI, 1, FONT_W_BOLD);
}

static const char *status_text(void) {
    switch (pip_mood()) {
    case PM_PLAYING:  return "Playing on your desktop";
    case PM_WATCHING: return "Watching you work";
    case PM_HAPPY:    return "Feeling loved";
    case PM_THINKING: return "Thinking";
    case PM_SLEEPING: return pip_brain()->mode == PIP_NAP ? "Napping on the dock" : "Sleeping";
    case PM_EXCITED:  return "Super excited";
    case PM_SAD:      return "A bit down";
    case PM_CONFUSED: return "Confused";
    default:          return "Chilling";
    }
}

static const char *const MENU_L[4] = { "Give a pat", "Ask what he's thinking", "Go play", "Dress up..." };
static rect_t menu_item(u32 i) { return rect_make(g_menu_x, g_menu_y + 8 + (i32)i * 38, 240, 38); }

void pip_desktop_draw(void) {
    if (!pip_brain() || pip_desktop_hidden()) return;
    if (!g_x && !g_y) home_spot(&g_x, &g_y);
    step_motion();
    rect_t pr = pip_rect();
    pip_set_anchor(pr.x + pr.w / 2, pr.y + pr.h / 3);
    u32 s = cur_size();
    pip_draw(g_x, g_y, s, PM_COUNT, false);
    rect_t area = rect_make(g_x - (i32)s * 40 / 160, g_y - (i32)s * 24 / 160 - 40, (i32)s * 240 / 160, (i32)s * 200 / 160 + 40);
    const char *bt = pip_bubble_text();
    if (bt) draw_bubble(bt, pr.x + pr.w / 2, pr.y - 6);
    if (g_hover && !g_menu) {                                 /* status pill */
        i32 pw = 300, ph = 64, px = pr.x + pr.w / 2 - pw / 2, py = pr.y + pr.h + 6;
        if (py + ph > (i32)SCREEN_H) py = pr.y - ph - 50;
        if (px < 8) px = 8;
        if (px + pw > (i32)SCREEN_W - 8) px = (i32)SCREEN_W - 8 - pw;
        ui_glass(0x9100, rect_make(px, py, pw, ph), 18, g_theme->panel_tint, g_theme->panel_alpha, 24, UI_GLASS_HAIRLINE | UI_GLASS_SHADOW_S);
        gfx_str_role(px + 14, py + 10, pip_brain()->name, g_theme->ink, FONT_ROLE_UI, 2, FONT_W_BOLD);
        gfx_circle_fill(px + 18, py + 42, 4, pip_mood_bulb(pip_mood()));
        gfx_str_role_fit(px + 28, py + 34, pw - 40, status_text(), g_theme->ink2, FONT_ROLE_UI, 0, FONT_W_REGULAR);
        damage_area:;
        area = rect_make(px < area.x ? px : area.x, area.y, (px + pw > area.x + area.w ? px + pw : area.x + area.w) - (px < area.x ? px : area.x), (py + ph > area.y + area.h ? py + ph : area.y + area.h) - area.y);
    }
    if (g_menu) {
        i32 mh = 8 + 4 * 38 + 8;
        gfx_shadow_soft(g_menu_x, g_menu_y, 240, mh, 16);
        gfx_rect_rounded(g_menu_x, g_menu_y, 240, mh, 16, g_theme->is_dark ? rgb(0x1C, 0x24, 0x40) : rgb(0xFF, 0xFF, 0xFF));
        for (u32 i = 0; i < 4; i++) {
            rect_t r = menu_item(i);
            const char *l = (i == 2 && pip_brain()->mode == PIP_ROAM) ? "Come back" : MENU_L[i];
            gfx_str_role(r.x + 16, r.y + (38 - gfx_role_line_h(FONT_ROLE_UI, 2)) / 2, l, g_theme->ink, FONT_ROLE_UI, 2, FONT_W_REGULAR);
        }
        ui_damage(rect_make(g_menu_x - 20, g_menu_y - 20, 280, mh + 40));
    }
    ui_damage(g_last);
    ui_damage(area);
    g_last = area;
}

static void think_line(void) {
    char t[64]; u32 n = 0;
    extern window_t windows[];
    for (u32 i = 0; i < MAX_WINDOWS; i++) if (windows[i].active && !windows[i].minimized) n++;
    pip_brain_t *b = pip_brain();
    u32 sel = timer_ms() / 1000 % 3;
    if (b->energy < 30) kstrcpy(t, "Energy's low... a nap soon?");
    else if (sel == 0) { char d[8]; kutoa(n, d, 10); kstrcpy(t, "You've got "); kstrcat(t, d); kstrcat(t, n == 1 ? " window open." : " windows open."); }
    else if (sel == 1) { char d[8]; kutoa(timer_ms() / 60000, d, 10); kstrcpy(t, "We've been up "); kstrcat(t, d); kstrcat(t, " minutes."); }
    else { char d[8]; kutoa(b->happy, d, 10); kstrcpy(t, "Happiness: "); kstrcat(t, d); kstrcat(t, "%. Pat me?"); }
    pip_say(t, 3500);
    pip_event(PE_LOADING);
}

bool pip_desktop_mouse(mouse_t *m) {
    if (!pip_brain() || pip_desktop_hidden()) { g_hover = false; return false; }
    rect_t pr = pip_rect();
    g_hover = rect_contains(pr, m->x, m->y);
    if (g_menu && (m->left_clicked || m->right_clicked)) {
        for (u32 i = 0; i < 4; i++) {
            if (!rect_contains(menu_item(i), m->x, m->y)) continue;
            g_menu = false;
            if (i == 0) pip_event(PE_PAT);
            else if (i == 1) think_line();
            else if (i == 2) { if (pip_brain()->mode == PIP_ROAM) pip_desktop_come_back(); else pip_desktop_go_play(); }
            else wm_open(APP_PIP, "Pip's Home", 120, 70, (i32)SCREEN_W * 70 / 100, (i32)SCREEN_H * 78 / 100);
            return true;
        }
        g_menu = false;
        return rect_contains(pr, m->x, m->y);
    }
    if (!g_hover) return false;
    if (m->right_clicked) { g_menu = true; g_menu_x = m->x; g_menu_y = m->y - 8 - 4 * 38 - 8; if (g_menu_y < 48) g_menu_y = m->y; return true; }
    if (m->left_clicked) {
        if (pip_brain()->mode == PIP_NAP) pip_set_mode(PIP_PERCH);   /* wake */
        if (timer_ms() - g_last_pat_ms > 250) pip_event(PE_PAT);      /* debounce a click storm */
        g_last_pat_ms = timer_ms();
        return true;
    }
    return m->left || m->left_released;                              /* own the press */
}
```
(Remove the `damage_area:;` label line if the compiler warns about an unused label. It's only there to mark the merge.)

- [ ] **Step 2: Wire it into `gui.c`**
- **Draw order:** call `pip_desktop_draw()` after windows are composited and before the menu bar, notifications, Control Center and launcher are drawn. Find the spot with `grep -n "wm_draw_all\|menubar_draw\|notify_draw\|launcher_draw" gui/gui.c` and insert the call between window drawing and `menubar_draw`.
- **Routing:** in the mouse routing chain (`wm_busy` → dock → owner → widgets → desktop), put `pip_desktop_mouse(&mouse)` right after the dock check and before `wm_pointer_owner`. Skip it when the launcher, Control Center or notification center is open:
  ```c
  if (!launcher_open && !s_control_center_open && !s_notif_center_open && pip_desktop_mouse(&mouse)) { /* consumed */ }
  else ...existing chain...
  ```
- **Tick:** once per frame, call `pip_tick(mouse.x, mouse.y, timer_ms() - g_last_activity_tick);`.
- **Nap:** a long idle sets nap mode only when `pip_brain()->nap`. The brain already returns `PM_SLEEPING`, so there's nothing else to add here.

- [ ] **Step 3: Real event hooks**
- `apps/app_terminal.c`: in `term_exec`'s final `else` branch (unknown command, both the suggestion and the plain case) add `pip_event(PE_CONFUSED);` and include `../gui/pip/pip.h`.
- `apps/app_editor.c`: in `editor_run`, after `s->ok` is known, add `pip_event(s->ok ? PE_RUN_OK : PE_RUN_FAIL);`.
- `gui/wm.c`: in `wm_open`, after a new window is created, add `if (app != APP_PIP) pip_event(PE_LOADING);`.
- `gui/notify.c`: in `notify_push_ex`, if `icon_color == g_theme->error`, add `pip_event(PE_CONFUSED);`.
- `apps/app_browser.c`: in `app_browser_navigate`, when a fetch is submitted, add `pip_event(PE_LOADING);`.

- [ ] **Step 4: Build, run the host tests, then QA**

Use the QA ISO and `shots.py`:
- Shot after 6 s: Pip perched by the dock.
- Move the pointer over him: the status pill.
- Click: a happy bounce, a heart and "Hehe, that tickles!".
- Right-click, then "Go play": he walks onto a window top. Take two shots 3 s apart.
- In the terminal, type `asdf`: Pip becomes confused.
- In the editor, press Ctrl+E on the sample: Pip becomes excited.
- Open DOOM, maximised: Pip is hidden.

Confirm he never draws over the menu bar (y < 48) and that an open launcher covers him.

- [ ] **Step 5: Commit** — `feat(pip): Pip on the desktop -- perch, roam, nap, bubbles, menu, real events`

---

### Task 6: Pip's Home app

**Files:**
- Create: `apps/app_pip.c`
- Modify:
  - `gui/gui.h`: add `APP_PIP` at the end of `app_id_t` and prototypes `app_pip_init/draw/click/key`.
  - `gui/wm.c`: init, draw, click and key cases; default size about 1180×760; minimum 900×600.
  - `gui/launcher.c`: entry `{ "pip", APP_PIP }` in the Fun category.
  - `gui/taskbar.c`: name "Pip".
  - `gui/icon.c` and `gui/notify.c`: names.
  - `gui/ui.c`: `ui_icon_for_app` maps `APP_PIP` to `UI_ICON_PET`.
  - `kernel/appdb.c`: builtin entry, so Pip appears in the launcher and can be pinned to the dock.

**Interfaces:**
- Consumes: `pip.h` (`pip_brain`, `pip_draw`, `pip_set_look`, `pip_set_mode`, `pip_event`, `pip_mark_dirty`, `pip_saved`, `pip_today`), `pip_art.h` labels, `pip_desktop_go_play`.
- Produces: `void app_pip_init(window_t*)`, `void app_pip_draw(window_t*)`, `void app_pip_click(window_t*, i32, i32)`, `void app_pip_key(window_t*, char)`.

- [ ] **Step 1: Write the app**

<!-- file: apps/app_pip.c -->
```c
/* CareOS v9 -- apps/app_pip.c -- Pip's Home (Phase 4 spec §5).
 * The room with a big live Pip, his name, real stats, a mood row, Pat /
 * Tuck in / Surprise me, "Let <name> out", and a dress-up aside whose tiles
 * preview each choice. Every change applies everywhere at once. */
#include "apps_common.h"
#include "ui.h"
#include "../gui/pip/pip.h"

enum { HA_NONE = 0, HA_NAME, HA_MOOD, HA_PAT, HA_TUCK, HA_SURPRISE, HA_OUT, HA_TAB, HA_OPT, HA_TOGGLE };
typedef struct { rect_t r; u8 act; i32 arg; } hhit_t;
typedef struct {
    hhit_t hits[80]; u32 nhits;
    u32 tab;                       /* 0 color, 1 hat, 2 outfit, 3 acc, 4 effect */
    bool name_focus;
    i32 preview_mood; u32 preview_until;
} hstate_t;

static hstate_t *hst(window_t *w) {
    if (!w->app_state) {
        hstate_t *s = (hstate_t *)kmalloc(sizeof(hstate_t));
        if (!s) return NULL;
        kmemset(s, 0, sizeof *s);
        s->tab = 1; s->preview_mood = -1;
        w->app_state = s;
    }
    return (hstate_t *)w->app_state;
}
static void hit(hstate_t *s, rect_t r, u8 a, i32 g) { if (s->nhits < 80) { s->hits[s->nhits].r = r; s->hits[s->nhits].act = a; s->hits[s->nhits].arg = g; s->nhits++; } }
static i32 lh(u32 slot) { return gfx_role_line_h(FONT_ROLE_UI, slot); }

static const char *const TAB_L[5] = { "Color", "Hats", "Outfits", "Extras", "Effects" };
static u32 tab_count(u32 t) { return t == 0 ? PIP_COLORS : t == 1 ? PIP_HATS : t == 2 ? PIP_OUTFITS : t == 3 ? PIP_ACCS : PIP_EFFECTS; }
static const char *opt_label(u32 t, u32 i) {
    return t == 0 ? pip_color_label(i) : t == 1 ? pip_hat_label(i) : t == 2 ? pip_outfit_label(i) : t == 3 ? pip_acc_label(i) : pip_effect_label(i);
}
static u8 *look_field(pip_look_t *l, u32 t) {
    return t == 0 ? &l->color : t == 1 ? &l->hat : t == 2 ? &l->outfit : t == 3 ? &l->acc : &l->effect;
}

void app_pip_init(window_t *w) { (void)hst(w); }

static void meter(i32 x, i32 y, i32 w, const char *label, const char *val, u32 pct, u32 col) {
    gfx_str_role(x, y, label, rgb(0xEE, 0xF1, 0xFA), FONT_ROLE_UI, 1, FONT_W_BOLD);
    i32 vw = gfx_str_role_width(val, FONT_ROLE_UI, 1, FONT_W_BOLD);
    gfx_str_role(x + w - vw, y, val, rgb(0xD5, 0xDA, 0xEA), FONT_ROLE_UI, 1, FONT_W_BOLD);
    gfx_rect_rounded(x, y + lh(1) + 6, w, 8, 4, rgb(0x2E, 0x37, 0x57));
    i32 fw = w * (i32)(pct > 100 ? 100 : pct) / 100;
    if (fw > 8) gfx_rect_rounded(x, y + lh(1) + 6, fw, 8, 4, col);
}

static i32 chip(hstate_t *s, i32 x, i32 y, const char *label, u32 dot, bool on, u8 act, i32 arg) {
    i32 w = gfx_str_role_width(label, FONT_ROLE_UI, 1, FONT_W_BOLD) + (dot ? 34 : 24);
    gfx_rect_rounded(x, y, w, 34, 17, on ? rgb(0xFF, 0xFF, 0xFF) : rgb(0x26, 0x2D, 0x4F));
    if (dot) gfx_circle_fill(x + 15, y + 17, 5, dot);
    gfx_str_role(x + (dot ? 26 : 12), y + (34 - lh(1)) / 2, label, on ? rgb(0x1A, 0x16, 0x30) : rgb(0xEE, 0xF1, 0xFA), FONT_ROLE_UI, 1, FONT_W_BOLD);
    hit(s, rect_make(x, y, w, 34), act, arg);
    return w;
}

void app_pip_draw(window_t *w) {
    rect_t cr = wm_client_rect(w);
    hstate_t *s = hst(w);
    if (!s) return;
    s->nhits = 0;
    pip_brain_t *b = pip_brain();
    i32 aw = cr.w >= 1100 ? 360 : 300;
    rect_t room = rect_make(cr.x, cr.y, cr.w - aw, cr.h);
    /* the room: sky gradient, window, floor */
    gfx_gradient_rect(room.x, room.y, room.w, room.h * 3 / 4, rgb(0x24, 0x2C, 0x5A), rgb(0x3B, 0x2F, 0x6E));
    gfx_rect(room.x, room.y + room.h * 3 / 4, room.w, room.h - room.h * 3 / 4, rgb(0x2A, 0x22, 0x4A));
    gfx_rect_rounded(room.x + room.w - 220, room.y + 40, 150, 120, 14, rgb(0x16, 0x1C, 0x40));
    gfx_rect_rounded_outline(room.x + room.w - 220, room.y + 40, 150, 120, 14, rgb(0x5A, 0x63, 0x95));
    gfx_circle_fill(room.x + room.w - 120, room.y + 80, 14, rgb(0xFF, 0xE0, 0x8A));
    /* name card */
    pip_draw(room.x + 24, room.y + 20, 70, PM_HAPPY, true);
    char age[48]; kutoa(pip_brain_age_days(b, pip_today()), age, 10);
    char line[64]; kstrcpy(line, b->name); kstrcat(line, ", age "); kstrcat(line, age); kstrcat(line, pip_brain_age_days(b, pip_today()) == 1 ? " day" : " days");
    gfx_str_role(room.x + 104, room.y + 44, line, rgb(0xEE, 0xF1, 0xFA), FONT_ROLE_DISPLAY, 1, FONT_W_BOLD);
    if (!pip_saved()) gfx_str_role(room.x + 104, room.y + 44 + gfx_role_line_h(FONT_ROLE_DISPLAY, 1), "Guest session -- Pip won't remember this visit", rgb(0xFF, 0xC2, 0x4B), FONT_ROLE_UI, 0, FONT_W_REGULAR);
    /* big Pip */
    u32 mood = (s->preview_mood >= 0 && timer_ms() < s->preview_until) ? (u32)s->preview_mood : PM_COUNT;
    pip_draw(room.x + room.w / 2 - 140, room.y + room.h * 3 / 4 - 260, 280, mood, false);
    /* stats panel */
    i32 px = room.x + 24, py = room.y + room.h - 240, pw = room.w - 48 < 520 ? room.w - 48 : 520;
    ui_glass(0x9200, rect_make(px, py, pw, 220), 22, rgb(0x15, 0x1B, 0x2E), 200, 24, UI_GLASS_HAIRLINE);
    gfx_str_role(px + 18, py + 16, "Name", rgb(0x9A, 0xA3, 0xBD), FONT_ROLE_UI, 0, FONT_W_BOLD);
    rect_t nf = rect_make(px + 70, py + 8, 200, 34);
    gfx_rect_rounded(nf.x, nf.y, nf.w, nf.h, 10, rgb(0x10, 0x15, 0x29));
    if (s->name_focus) gfx_rect_rounded_outline(nf.x, nf.y, nf.w, nf.h, 10, g_theme->care);
    gfx_str_role(nf.x + 10, nf.y + (34 - lh(2)) / 2, b->name, rgb(0xEE, 0xF1, 0xFA), FONT_ROLE_UI, 2, FONT_W_BOLD);
    hit(s, nf, HA_NAME, 0);
    char hv[12], ev[12], lv[12];
    kutoa(b->happy, hv, 10); kstrcat(hv, "%");
    kutoa(b->energy, ev, 10); kstrcat(ev, "%");
    kstrcpy(lv, "Lv "); { char d[4]; kutoa(pip_brain_level(b), d, 10); kstrcat(lv, d); }
    i32 mw = (pw - 36 - 24) / 3;
    meter(px + 18, py + 56, mw, "Happiness", hv, b->happy, rgb(0xFF, 0x8E, 0xC4));
    meter(px + 18 + mw + 12, py + 56, mw, "Energy", ev, b->energy, rgb(0xFF, 0xC2, 0x4B));
    meter(px + 18 + 2 * (mw + 12), py + 56, mw, "Friendship", lv, (b->friend_pts % 40) * 100 / 40, rgb(0x3D, 0xD6, 0x8C));
    /* mood row */
    i32 cx = px + 18, cy = py + 104;
    for (u32 m = 0; m < PM_COUNT && cx < px + pw - 60; m++) {
        cx += chip(s, cx, cy, pip_mood_label(m), pip_mood_bulb(m), (u32)s->preview_mood == m && timer_ms() < s->preview_until, HA_MOOD, (i32)m) + 6;
        if (cx > px + pw - 120 && m + 1 < PM_COUNT) { cx = px + 18; cy += 40; }
    }
    /* actions */
    i32 ay = py + 220 - 46, ax = px + 18;
    ax += chip(s, ax, ay, "Pat", 0xFF8EC4, false, HA_PAT, 0) + 8;
    ax += chip(s, ax, ay, "Tuck in", 0xB7C1FF, false, HA_TUCK, 0) + 8;
    ax += chip(s, ax, ay, "Surprise me", 0xFFC24B, false, HA_SURPRISE, 0) + 8;
    char out[32]; kstrcpy(out, "Let "); kstrcat(out, b->name); kstrcat(out, " out");
    i32 ow = gfx_str_role_width(out, FONT_ROLE_UI, 2, FONT_W_BOLD) + 32;
    gfx_rect_rounded(room.x + room.w - 24 - ow, room.y + room.h - 64, ow, 42, 21, g_theme->care);
    gfx_str_role(room.x + room.w - 24 - ow + 16, room.y + room.h - 64 + (42 - lh(2)) / 2, out, g_theme->on_care, FONT_ROLE_UI, 2, FONT_W_BOLD);
    hit(s, rect_make(room.x + room.w - 24 - ow, room.y + room.h - 64, ow, 42), HA_OUT, 0);

    /* aside: dress up */
    i32 ax0 = cr.x + cr.w - aw;
    gfx_rect(ax0, cr.y, aw, cr.h, g_theme->surface_alt);
    gfx_vline(ax0, cr.y, cr.h, g_theme->is_dark ? rgb(0x26, 0x2D, 0x48) : rgb(0xE1, 0xE4, 0xEE));
    char dt[40]; kstrcpy(dt, "Dress up "); kstrcat(dt, b->name);
    gfx_str_role(ax0 + 20, cr.y + 20, dt, g_theme->ink, FONT_ROLE_DISPLAY, 1, FONT_W_BOLD);
    gfx_str_role(ax0 + 20, cr.y + 20 + gfx_role_line_h(FONT_ROLE_DISPLAY, 1), "Changes show up right away, everywhere.", g_theme->muted, FONT_ROLE_UI, 0, FONT_W_REGULAR);
    i32 tx = ax0 + 16, ty = cr.y + 84;
    for (u32 t = 0; t < 5; t++) {
        i32 tw = gfx_str_role_width(TAB_L[t], FONT_ROLE_UI, 1, FONT_W_BOLD) + 20;
        if (s->tab == t) gfx_rect_rounded(tx, ty, tw, 32, 10, g_theme->is_dark ? rgb(0x2A, 0x33, 0x58) : rgb(0xFF, 0xFF, 0xFF));
        gfx_str_role(tx + 10, ty + (32 - lh(1)) / 2, TAB_L[t], s->tab == t ? g_theme->ink : g_theme->ink2, FONT_ROLE_UI, 1, FONT_W_BOLD);
        hit(s, rect_make(tx, ty, tw, 32), HA_TAB, (i32)t);
        tx += tw + 2;
    }
    i32 gy = ty + 44, tile = (aw - 32 - 16) / 3;
    for (u32 i = 0; i < tab_count(s->tab); i++) {
        i32 ox = ax0 + 16 + (i32)(i % 3) * (tile + 8), oy = gy + (i32)(i / 3) * (tile + 26);
        pip_look_t l = b->look;
        *look_field(&l, s->tab) = (u8)i;
        bool on = *look_field(&b->look, s->tab) == i;
        gfx_rect_rounded(ox, oy, tile, tile + 18, 14, g_theme->is_dark ? rgb(0x1A, 0x20, 0x38) : rgb(0xFF, 0xFF, 0xFF));
        if (on) gfx_rect_rounded_outline(ox, oy, tile, tile + 18, 14, g_theme->care);
        pip_brain_t saved = *b;
        b->look = l;                                   /* preview this choice */
        pip_draw(ox + (tile - 64) / 2, oy + 4, 64, PM_IDLE, true);
        b->look = saved.look;
        gfx_str_role_fit(ox + 8, oy + tile - 6, tile - 16, opt_label(s->tab, i), g_theme->ink, FONT_ROLE_UI, 0, FONT_W_BOLD);
        hit(s, rect_make(ox, oy, tile, tile + 18), HA_OPT, (i32)i);
    }
    /* toggles */
    i32 tgy = cr.y + cr.h - 3 * 50 - 50;
    char wo[40]; kstrcpy(wo, "When "); kstrcat(wo, b->name); kstrcat(wo, " is out");
    gfx_str_role(ax0 + 20, tgy, wo, g_theme->muted, FONT_ROLE_UI, 0, FONT_W_BOLD);
    static const char *const TL[3] = { "Roam around", "Follow my cursor", "Nap when I'm away" };
    bool vals[3] = { b->roam, b->follow, b->nap };
    for (u32 i = 0; i < 3; i++) {
        i32 y = tgy + 24 + (i32)i * 50;
        gfx_str_role(ax0 + 20, y + (UI_TOGGLE_H - lh(2)) / 2, TL[i], g_theme->ink, FONT_ROLE_UI, 2, FONT_W_REGULAR);
        ui_toggle(ax0 + aw - 20 - UI_TOGGLE_W, y, vals[i], UI_NORMAL);
        hit(s, rect_make(ax0 + 12, y - 8, aw - 24, UI_TOGGLE_H + 16), HA_TOGGLE, (i32)i);
    }
}

void app_pip_click(window_t *w, i32 x, i32 y) {
    hstate_t *s = hst(w);
    if (!s) return;
    pip_brain_t *b = pip_brain();
    for (u32 k = s->nhits; k-- > 0;) {
        const hhit_t *h = &s->hits[k];
        if (!rect_contains(h->r, x, y)) continue;
        if (h->act != HA_NAME) s->name_focus = false;
        switch (h->act) {
        case HA_NAME: s->name_focus = true; break;
        case HA_MOOD: s->preview_mood = h->arg; s->preview_until = timer_ms() + 5000; break;
        case HA_PAT: pip_event(PE_PAT); break;
        case HA_TUCK: pip_set_mode(PIP_NAP); pip_say("Night night...", 2500); break;
        case HA_SURPRISE: {
            u32 r = timer_ms() * 2654435761u;
            pip_look_t l = { (u8)(r % PIP_COLORS), (u8)((r >> 5) % PIP_HATS), (u8)((r >> 10) % PIP_OUTFITS), (u8)((r >> 15) % PIP_ACCS), (u8)((r >> 20) % PIP_EFFECTS) };
            pip_set_look(l);
            pip_event(PE_WELCOME_BACK);
            break;
        }
        case HA_OUT: pip_desktop_go_play(); wm_minimize(w); break;
        case HA_TAB: s->tab = (u32)h->arg; break;
        case HA_OPT: { pip_look_t l = b->look; *look_field(&l, s->tab) = (u8)h->arg; pip_set_look(l); break; }
        case HA_TOGGLE:
            if (h->arg == 0) b->roam = !b->roam; else if (h->arg == 1) b->follow = !b->follow; else b->nap = !b->nap;
            pip_mark_dirty();
            break;
        default: break;
        }
        return;
    }
    s->name_focus = false;
}

void app_pip_key(window_t *w, char c) {
    hstate_t *s = hst(w);
    if (!s || !s->name_focus) return;
    pip_brain_t *b = pip_brain();
    u32 n = (u32)kstrlen(b->name);
    if (c == '\n' || c == 0x1B) { s->name_focus = false; if (!b->name[0]) kstrcpy(b->name, "Pip"); pip_mark_dirty(); return; }
    if (c == '\b') { if (n) b->name[n - 1] = '\0'; pip_mark_dirty(); return; }
    if (c >= 32 && c < 127 && n < 14) { b->name[n] = c; b->name[n + 1] = '\0'; pip_mark_dirty(); }
}
```

- [ ] **Step 2: Register `APP_PIP`** at every site listed in Files. For each existing `case APP_DOOM:`-style switch in `wm.c`, add an `APP_PIP` case that calls the matching `app_pip_*` function. Find them with `grep -n "APP_DOOM" gui/*.c kernel/appdb.c`; each place that names DOOM needs a Pip line.

- [ ] **Step 3: Build, QA, compare with `PetHome`, commit**
- QA: open Pip's Home from the launcher ("pip"). Shoot the room. Click the Hats tab, then Crown; check that the desktop Pip wears it after closing. Click Pat; the Happiness % changes. Type a new name and press Enter. Click "Let Pip out"; he roams.
- Commit: `feat(pip): Pip's Home -- room, stats, moods, dress-up, toggles`

---

### Task 7: Care natives, Editor docs card, cameo polish

**Files:**
- Modify:
  - `kernel/care_lang.c`: two natives in `CL_NATIVES`.
  - `apps/app_editor.c`: the "Talk to Pip" docs card.
  - `gui/session/login.c` / `gui/session/power.c` only if a cameo needs layout room (it shouldn't).

- [ ] **Step 1: Natives.** In `kernel/care_lang.c`, add above `CL_NATIVES`:
```c
#include "../gui/pip/pip.h"
static cl_val_t nat_pip_mood(cl_val_t *args, u32 nargs) {
    return vnum(nargs >= 1 && args[0].is_str && pip_script_mood(args[0].str) ? 1 : 0);
}
static cl_val_t nat_pip_say(cl_val_t *args, u32 nargs) {
    if (nargs >= 1 && args[0].is_str) pip_say(args[0].str, 4000);
    return vnum(0);
}
```
and append these table rows:
```c
    { "pip_mood",          nat_pip_mood,          "pip_mood(\"happy\")" },
    { "pip_say",           nat_pip_say,           "pip_say(\"hi!\")" },
```
Check that `CL_MAX_FUNCS` (24) still has room: 13 natives plus user functions.

- [ ] **Step 2: Docs card.** In `apps/app_editor.c` `draw_docs`, make the static `cards` array 4 entries long. Add `{ "Talk to Pip", "pip_mood(\"happy\");\npip_say(\"hi!\");" }` and update the loop bounds (`c < 5`, built-ins as the last card).

- [ ] **Step 3: QA**
- In the Editor, run `pip_say("Hello from Care!"); pip_mood("excited");`. The bubble and excited mood show on the desktop Pip.
- Super+L: the lock screen shows a sleeping Pip.
- Open the power menu: a worried Pip above "Taking a break?".

- [ ] **Step 4: Commit** — `feat(pip): Care natives pip_mood/pip_say, Talk to Pip docs, cameos`

---

### Task 8: Performance and the phase write-up

**Files:**
- Create: `docs/design/careos-v10/phase4-pip-progress.md`
- Modify: the memory file `careos-v10-redesign.md` (outside the repo)

- [ ] **Step 1: VBox benchmark with Pip roaming.** Build the bench ISO (`qa/v10-shell/build-bench.sh`) with Pip forced to roam during the bench: in `pip_session_start`, `#ifdef CAREOS_PERF_BENCH` call `pip_desktop_go_play()` with a roam timeout of 600 s. Run `../vbox-bench.ps1` on the CareOS-perf VM. Pass: fps ≥ 30 and p95 ≤ 33 ms.
  - If it fails: check that the sprite cache hits (each mood is 12 frames, then 0 renders per frame), and that `ui_damage` only covers Pip's area. Fix and re-run.
- [ ] **Step 2: Write the progress doc.** Cover what was built, the deviations, and the QA screenshots list.
- [ ] **Step 3: Final checks.** `make host-test` shows 30 tests; normal ISO build; boot to the login gate (`drive.py start` with `normal.iso`); the login screen still has no Pip (nobody signed in), and the power menu from login shows the worried Pip.
- [ ] **Step 4: Commit** — `docs(v10): phase 4 Pip progress`
