# CareOS 1080p Resolution Independence & UX Improvements

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the 7 checklist items to make CareOS comfortable and correct at 1920×1080 resolution.

**Architecture:** All changes are confined to four files: `gui/gui.h`, `gui/gfx.c`, `gui/wm.c`, and `apps/app_paint.c`. No new files are created. Runtime-computed values replace hardcoded constants where resolution matters. Font scaling uses a single `GFX_FONT_SCALE` global (set in `gfx_init()`, exposed via `gui.h`) applied across every text-rendering function.

**Tech Stack:** Bare-metal C, 32-bpp linear framebuffer, x86, cross-compiled GCC.

---

### Task 1: Increase MAX_WINDOWS to 16

**Files:**
- Modify: `gui/gui.h:121`
- Modify: `gui/wm.c:139`

- [ ] **Step 1: Change MAX_WINDOWS in gui.h**

In `gui/gui.h`, line 121, replace:
```c
#define MAX_WINDOWS  8
```
with:
```c
#define MAX_WINDOWS  16
```

- [ ] **Step 2: Fix cascade modulo in wm.c**

In `gui/wm.c`, line 139, replace:
```c
        i32 cascade = (i32)(open_count % 8) * 20;
```
with:
```c
        i32 cascade = (i32)(open_count % MAX_WINDOWS) * 20;
```

- [ ] **Step 3: Compile**

```bash
make
```
Expected: zero errors. All `MAX_WINDOWS` loops already use the constant.

- [ ] **Step 4: Commit**

```bash
git add gui/gui.h gui/wm.c
git commit -m "feat: increase MAX_WINDOWS from 8 to 16"
```

---

### Task 2: Font Scaling via GFX_FONT_SCALE

**Files:**
- Modify: `gui/gfx.c` (multiple sites)
- Modify: `gui/gui.h` (one line)

- [ ] **Step 1: Add GFX_FONT_SCALE global in gfx.c**

In `gui/gfx.c`, after line 24 (`u32 *FRAMEBUFFER  = (u32*)0;`), insert:
```c
u32  GFX_FONT_SCALE = 1;  /* 1 at <900px height, 2 at >=900px; set by gfx_init */
```

- [ ] **Step 2: Set scale in gfx_init**

In `gui/gfx.c`, inside `gfx_init()`, after the line `SCREEN_PITCH = pitch;` (line 104), insert:
```c
    GFX_FONT_SCALE = (h >= 900) ? 2 : 1;
```

- [ ] **Step 3: Update gfx_char to draw scaled glyphs**

In `gui/gfx.c`, replace the entire `gfx_char` function (lines 401–430) with:
```c
void gfx_char(i32 x, i32 y, char c, u32 fg, u32 bg) {
    if (c < 32 || c > 126) c = '?';
    bool skip_bg = (bg == COL_TRANSPARENT);
    i32 sc = (i32)GFX_FONT_SCALE;
    const u8 *g = font8[(u8)c - 32];
    for (i32 row = 0; row < 8; row++) {
        u8 bits = g[row];
        for (i32 col = 0; col < 8; col++) {
            bool lit = (bits >> col) & 1;
            if (skip_bg && !lit) continue;
            u32 color = lit ? fg : bg;
            for (i32 sy = 0; sy < sc; sy++)
                for (i32 sx = 0; sx < sc; sx++)
                    gfx_setpixel(x + col * sc + sx, y + row * sc + sy, color);
        }
    }
    gfx_dirty(x, y, 8 * sc, 8 * sc);
}
```

- [ ] **Step 4: Update gfx_str_ex to apply GFX_FONT_SCALE as base multiplier**

In `gui/gfx.c`, replace the entire `gfx_str_ex` function (lines 432–464) with:
```c
void gfx_str_ex(i32 x, i32 y, const char *s, u32 fg, u32 bg, font_size_t size) {
    i32 cx = x;
    i32 sc = (i32)GFX_FONT_SCALE;
    if      (size == FONT_H1)                   sc *= 3;
    else if (size == FONT_H2 || size == FONT_H3) sc *= 2;

    while (*s) {
        if (*s == '\n') { cx = x; y += FONT_H * sc; s++; continue; }
        if (*s >= 32 && *s <= 126) {
            const u8 *g = font8[(u8)*s - 32];
            for (i32 row = 0; row < 8; row++) {
                u8 bits = g[row];
                for (i32 col = 0; col < 8; col++) {
                    if ((bits >> col) & 1) {
                        for (i32 sy = 0; sy < sc; sy++)
                            for (i32 sx = 0; sx < sc; sx++)
                                gfx_setpixel(cx + col*sc + sx, y + row*sc + sy, fg);
                    } else if (bg != COL_TRANSPARENT) {
                        for (i32 sy = 0; sy < sc; sy++)
                            for (i32 sx = 0; sx < sc; sx++)
                                gfx_setpixel(cx + col*sc + sx, y + row*sc + sy, bg);
                    }
                }
            }
        }
        cx += FONT_W * sc; s++;
    }
}
```

- [ ] **Step 5: Update gfx_str_centered_ex to use scaled width for centering**

In `gui/gfx.c`, replace the entire `gfx_str_centered_ex` function (lines 470–476) with:
```c
void gfx_str_centered_ex(i32 x, i32 y, i32 w, const char *s, u32 fg, u32 bg, font_size_t size) {
    i32 sc = (i32)GFX_FONT_SCALE;
    if      (size == FONT_H1)                        sc *= 3;
    else if (size == FONT_H2 || size == FONT_H3)     sc *= 2;
    i32 len = (i32)kstrlen(s);
    gfx_str_ex(x + (w - len * FONT_W * sc) / 2, y, s, fg, bg, size);
}
```

- [ ] **Step 6: Update gfx_str_bg_none to advance by scaled width**

In `gui/gfx.c`, replace the entire `gfx_str_bg_none` function (lines 482–496) with:
```c
void gfx_str_bg_none(i32 x, i32 y, const char *s, u32 fg) {
    i32 cx = x;
    i32 sc = (i32)GFX_FONT_SCALE;
    while (*s) {
        if (*s == '\n') { cx = x; y += FONT_H * sc; s++; continue; }
        if (*s >= 32 && *s <= 126) {
            const u8 *g = font8[(u8)*s - 32];
            for (i32 row = 0; row < 8; row++) {
                u8 bits = g[row];
                for (i32 col = 0; col < 8; col++)
                    if ((bits >> col) & 1)
                        for (i32 sy = 0; sy < sc; sy++)
                            for (i32 sx = 0; sx < sc; sx++)
                                gfx_setpixel(cx + col*sc + sx, y + row*sc + sy, fg);
            }
        }
        cx += FONT_W * sc; s++;
    }
}
```

- [ ] **Step 7: Scale gfx_str_width and gfx_str_clipped**

In `gui/gfx.c`, replace line 562:
```c
i32 gfx_str_width(const char *s){ return (i32)(kstrlen(s)*FONT_W); }
```
with:
```c
i32 gfx_str_width(const char *s){ return (i32)(kstrlen(s) * FONT_W * GFX_FONT_SCALE); }
```

Replace lines 569–572:
```c
void gfx_str_clipped(i32 x,i32 y,i32 maxw,const char *s,u32 fg,u32 bg){
    i32 px=x; int maxchars=maxw/FONT_W;
    for(int i=0;s[i]&&i<maxchars;i++){ gfx_char(px,y,s[i],fg,bg); px+=FONT_W; }
}
```
with:
```c
void gfx_str_clipped(i32 x,i32 y,i32 maxw,const char *s,u32 fg,u32 bg){
    i32 px = x;
    i32 cw = (i32)(FONT_W * GFX_FONT_SCALE);
    int maxchars = maxw / cw;
    for(int i=0;s[i]&&i<maxchars;i++){ gfx_char(px,y,s[i],fg,bg); px+=cw; }
}
```

- [ ] **Step 8: Expose GFX_FONT_SCALE in gui.h**

In `gui/gui.h`, after line 15 (`extern u32 *FRAMEBUFFER;`), insert:
```c
extern u32 GFX_FONT_SCALE;   /* 1 for <900px height, 2 for >=900px (set by gfx_init) */
```

- [ ] **Step 9: Compile**

```bash
make
```
Expected: zero errors. At 1080p all text renders at 16×16px per glyph.

- [ ] **Step 10: Commit**

```bash
git add gui/gfx.c gui/gui.h
git commit -m "feat: GFX_FONT_SCALE for 1080p 2x font scaling"
```

---

### Task 3: Ease-Out Cubic Animation

**Files:**
- Modify: `gui/wm.c:346–364`

The current animation accumulates incrementally (`rect += delta * elapsed/15`) which drifts. The new version uses `restore_rect` (already set by `wm_snap_focused` as the start position) for a true parametric ease-out cubic.

- [ ] **Step 1: Replace wm_animate_all**

In `gui/wm.c`, replace the entire `wm_animate_all` function (lines 346–364) with:
```c
void wm_animate_all(void) {
    u32 now = timer_get_ticks();
    for (int i = 0; i < MAX_WINDOWS; i++) {
        window_t *w = &windows[i];
        if (!w->active || !w->animating) continue;

        u32 elapsed = now - w->anim_start_tick;
        if (elapsed >= 15) {
            w->rect      = w->target_rect;
            w->animating = false;
        } else {
            /* Ease-out cubic: t = 1-(1-p)^3, integer arithmetic
             * p in [0..256]. restore_rect = start position set by wm_snap_focused. */
            i32 p    = (i32)elapsed * 256 / 15;
            i32 inv  = 256 - p;
            i32 inv2 = inv * inv / 256;   /* inv^2 / 256 */
            i32 inv3 = inv2 * inv / 256;  /* inv^3 / 256^2 */
            i32 t    = 256 - inv3;        /* ease-out factor [0..256] */

            w->rect.x = w->restore_rect.x + (w->target_rect.x - w->restore_rect.x) * t / 256;
            w->rect.y = w->restore_rect.y + (w->target_rect.y - w->restore_rect.y) * t / 256;
            w->rect.w = w->restore_rect.w + (w->target_rect.w - w->restore_rect.w) * t / 256;
            w->rect.h = w->restore_rect.h + (w->target_rect.h - w->restore_rect.h) * t / 256;
        }
    }
}
```

- [ ] **Step 2: Compile**

```bash
make
```

- [ ] **Step 3: Commit**

```bash
git add gui/wm.c
git commit -m "feat: ease-out cubic animation in wm_animate_all"
```

---

### Task 4: Dynamic Taskbar Slot Width + Show Desktop Constant

**Files:**
- Modify: `gui/wm.c` (constants block ~line 51, `taskbar_build_layout` ~lines 803–812)

- [ ] **Step 1: Add TB_SHOWDESK_W constant**

In `gui/wm.c`, after line 55 (`#define TB_PAD          4`), insert:
```c
#define TB_SHOWDESK_W  20   /* "show desktop" nub at far-right edge */
```

- [ ] **Step 2: Replace fixed slot-width logic in taskbar_build_layout**

In `gui/wm.c`, inside `taskbar_build_layout()`, replace this block:
```c
    i32 slots_start = TB_LAUNCHER_W + TB_PAD * 2;
    i32 slots_end   = sw - TB_TRAY_W - TB_PAD;
    u32 max_visible = (u32)((slots_end - slots_start) / (TB_SLOT_W + TB_PAD));
    if (max_visible > layout->app_count) max_visible = layout->app_count;
    if (max_visible > TASKBAR_SLOT_MAX)  max_visible = TASKBAR_SLOT_MAX;
    layout->app_count = max_visible;

    for (u32 i = 0; i < max_visible; i++) {
        i32 x = slots_start + (i32)i * (TB_SLOT_W + TB_PAD);
        layout->app_rects[i] = rect_make(x, ty + TB_PAD, TB_SLOT_W, TB_SLOT_H);
    }
```
with:
```c
    i32 slots_start = TB_LAUNCHER_W + TB_PAD * 2;
    i32 slots_end   = sw - TB_TRAY_W - TB_PAD - TB_SHOWDESK_W;
    i32 slots_avail = slots_end - slots_start;

    /* Dynamic slot width: fill available space equally, clamped 80..180px */
    i32 slot_count  = (i32)layout->app_count;
    i32 dyn_slot_w  = TB_SLOT_W;
    if (slot_count > 0) {
        dyn_slot_w = (slots_avail - (slot_count - 1) * TB_PAD) / slot_count;
        if (dyn_slot_w < 80)  dyn_slot_w = 80;
        if (dyn_slot_w > 180) dyn_slot_w = 180;
    }

    u32 max_visible = (u32)((slots_avail + TB_PAD) / (dyn_slot_w + TB_PAD));
    if (max_visible > layout->app_count) max_visible = layout->app_count;
    if (max_visible > TASKBAR_SLOT_MAX)  max_visible = TASKBAR_SLOT_MAX;
    layout->app_count = max_visible;

    for (u32 i = 0; i < max_visible; i++) {
        i32 x = slots_start + (i32)i * (dyn_slot_w + TB_PAD);
        layout->app_rects[i] = rect_make(x, ty + TB_PAD, dyn_slot_w, TB_SLOT_H);
    }
```

- [ ] **Step 3: Compile**

```bash
make
```

- [ ] **Step 4: Commit**

```bash
git add gui/wm.c
git commit -m "feat: dynamic taskbar slot width 80-180px, reserve space for show-desktop nub"
```

---

### Task 5: Increase Hitboxes — Titlebar Buttons + Paint Palette

**Files:**
- Modify: `gui/wm.c:473–476`
- Modify: `apps/app_paint.c:20–25, 48–51`

- [ ] **Step 1: Increase hit_btn radius from 10px to 14px**

In `gui/wm.c`, replace lines 473–476:
```c
static bool hit_btn(i32 mx, i32 my, i32 bx, i32 bmy) {
    i32 dx = mx - bx, dy = my - bmy;
    return (dx*dx + dy*dy) <= 100;  /* 10px radius */
}
```
with:
```c
static bool hit_btn(i32 mx, i32 my, i32 bx, i32 bmy) {
    i32 dx = mx - bx, dy = my - bmy;
    return (dx*dx + dy*dy) <= 196;  /* 14px radius */
}
```

- [ ] **Step 2: Increase paint palette circle radius and spacing in app_paint_draw**

In `apps/app_paint.c`, replace lines 20–25:
```c
    u32 palette[]={0x000000,0xffffff,0xff0000,0x00cc44,0x4a6fff,0xfbba00,0x22d3ee,0xa78bfa,0xf87171,0xfb923c};
    for(int i=0;i<10;i++){
        i32 px=cr.x+cr.w-140+i*14;
        gfx_circle_fill(px,cr.y+14,5,palette[i]);
        if((u32)i==w->paint_color) gfx_circle(px,cr.y+14,6,COL_WHITE);
    }
```
with:
```c
    u32 palette[]={0x000000,0xffffff,0xff0000,0x00cc44,0x4a6fff,0xfbba00,0x22d3ee,0xa78bfa,0xf87171,0xfb923c};
    for(int i=0;i<10;i++){
        i32 px=cr.x+cr.w-208+i*20;  /* 8px radius, 20px spacing, total 200px */
        gfx_circle_fill(px,cr.y+14,8,palette[i]);
        if((u32)i==w->paint_color) gfx_circle(px,cr.y+14,10,COL_WHITE);
    }
```

- [ ] **Step 3: Update palette hit detection to match new positions/radius**

In `apps/app_paint.c`, inside `app_paint_click`, replace:
```c
        for(int i=0;i<10;i++){
            i32 px=cr.x+cr.w-140+i*14;
            if(x>=px-6&&x<px+6) w->paint_color=(u32)i;
        }
```
with:
```c
        for(int i=0;i<10;i++){
            i32 px=cr.x+cr.w-208+i*20;
            if(x>=px-10&&x<px+10) w->paint_color=(u32)i;
        }
```

- [ ] **Step 4: Compile**

```bash
make
```

- [ ] **Step 5: Commit**

```bash
git add gui/wm.c apps/app_paint.c
git commit -m "feat: 14px titlebar button hitboxes, larger paint palette circles"
```

---

### Task 6: Show Desktop Nub in Taskbar

**Files:**
- Modify: `gui/wm.c` (new function + taskbar_draw + taskbar_handle_mouse)
- Modify: `gui/gui.h` (declare wm_minimize_all)

- [ ] **Step 1: Add wm_minimize_all function**

In `gui/wm.c`, directly before the `taskbar_draw` function (~line 815), insert:
```c
void wm_minimize_all(void) {
    for (int i = 0; i < MAX_WINDOWS; i++)
        if (windows[i].active && !windows[i].minimized)
            windows[i].minimized = true;
}
```

- [ ] **Step 2: Shift the tray left to make room for the nub**

In `gui/wm.c`, inside `taskbar_draw()`, find:
```c
    i32 tray_x = sw - TB_TRAY_W;
```
Replace with:
```c
    i32 tray_x = sw - TB_TRAY_W - TB_SHOWDESK_W;
```

- [ ] **Step 3: Draw the Show Desktop nub**

In `gui/wm.c`, inside `taskbar_draw()`, after the clock `gfx_str(...)` call (the last draw call before the closing brace of taskbar_draw), insert:
```c
    /* Show Desktop nub */
    gfx_rect(sw - TB_SHOWDESK_W + 1, ty + 2, TB_SHOWDESK_W - 2, TASKBAR_H - 4, COL_SURFACE3);
    gfx_vline(sw - TB_SHOWDESK_W, ty + 2, TASKBAR_H - 4, COL_BORDER);
```

- [ ] **Step 4: Handle nub click in taskbar_handle_mouse**

In `gui/wm.c`, inside `taskbar_handle_mouse()`, after the early-return guard `if (m->y < (i32)SCREEN_H - TASKBAR_H) return;` and before `taskbar_build_layout(&layout);`, insert:
```c
    /* Show Desktop nub */
    if (m->x >= (i32)SCREEN_W - TB_SHOWDESK_W) {
        wm_minimize_all();
        return;
    }
```

- [ ] **Step 5: Declare wm_minimize_all in gui.h**

In `gui/gui.h`, in the WM API block (after `void wm_snap_focused(int mode);`, around line 334), insert:
```c
void      wm_minimize_all(void);
```

- [ ] **Step 6: Compile**

```bash
make
```

- [ ] **Step 7: Commit**

```bash
git add gui/wm.c gui/gui.h
git commit -m "feat: Show Desktop nub at taskbar far-right (wm_minimize_all)"
```

---

### Task 7: Fix layout_icons + Scale Desktop Icon Size

**Files:**
- Modify: `gui/wm.c:46–48, 62–75, 91–98`

- [ ] **Step 1: Convert ICON_W and ICON_H from defines to runtime variables**

In `gui/wm.c`, replace lines 46–48:
```c
#define ICON_W       72
#define ICON_H       80
#define ICON_MARGIN  16
```
with:
```c
static i32 ICON_W     = 72;
static i32 ICON_H     = 80;
#define    ICON_MARGIN 16
```

- [ ] **Step 2: Set icon size in wm_init based on SCREEN_H**

In `gui/wm.c`, inside `wm_init()` (line 91), before the `layout_icons()` call, insert:
```c
    ICON_W = ((i32)SCREEN_H >= 900) ? 96 : 72;
    ICON_H = ((i32)SCREEN_H >= 900) ? 104 : 80;
```

- [ ] **Step 3: Fix layout_icons — remove the column-reset bug**

In `gui/wm.c`, replace the entire `layout_icons` function (lines 62–75) with:
```c
static void layout_icons(void) {
    i32 max_y = (i32)SCREEN_H - (i32)TASKBAR_H - ICON_MARGIN;
    i32 x = ICON_MARGIN, y = ICON_MARGIN;
    for (int i = 0; i < ICON_COUNT; i++) {
        if (y + ICON_H > max_y) {
            y  = ICON_MARGIN;
            x += ICON_W + ICON_MARGIN;
            /* No column reset: icons in extra columns render off-screen rather
             * than overwriting column 1. At 1080p all 14 icons fit in 2 columns. */
        }
        icons[i].x = x;
        icons[i].y = y;
        y += ICON_H + ICON_MARGIN;
    }
}
```

- [ ] **Step 4: Compile**

```bash
make
```
Expected: zero errors. `static i32 ICON_W` and `ICON_H` are compatible with all existing arithmetic and comparisons.

- [ ] **Step 5: Commit**

```bash
git add gui/wm.c
git commit -m "feat: scale desktop icons 96x104 at 1080p, fix layout_icons column-reset bug"
```

---

## Out of Scope (separate plans needed)

- **Paint canvas 64×64 → 128×128**: requires a buffer larger than `text_buf[4096]`; adding a static canvas is a dedicated change.
- **Settings layout robustness**: the `cr.y + 80 + i*42` tab positions work today but need a layout-engine refactor to survive font scaling; deferred.
- **Terminal scrollback**: `text_buf[4096]` is already the output ring; expanding it requires window struct changes.
- **Resize boundary clamping**: cosmetic edge case; one-liner addition to `wm_handle_mouse` resize block.
- **Pre-existing paint coordinate bug**: `app_paint_click` mixes relative `x/y` with absolute `cr.x`/`cr.y` positions. Not introduced by this plan; fix separately.
