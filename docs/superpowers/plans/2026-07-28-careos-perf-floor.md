# CareOS ≥30 fps Performance Floor — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Guarantee the CareOS desktop sustains ≥30 fps in the worst realistic case (Terminal + Files + remote Browser open, dragging a window while the browser image updates).

**Architecture:** Measurement-driven. The backdrop cache already reached ~100 fps with windows re-rendering every frame, so this plan first removes the one hotspot the remote browser introduces (a per-pixel image blit), adds frame pacing, then *measures* the worst case. A heavier per-window surface cache is included only as a **contingency task** to be executed if the measurement falls below 30 fps.

**Tech Stack:** C (freestanding), the existing `gfx.c` backbuffer/dirty-rect/target infrastructure, the rdtsc+timer fps harness used during backdrop-cache work.

## Global Constraints

- Target: sustained ≥30 fps (≤33 ms/frame) at 1920×1080 in the worst-case scenario above, verified with the fps harness.
- Do not regress the backdrop cache (`gfx_desktop_cache_*` in `gui/gfx.c`) or its correctness.
- Language: C only.
- Build/measure via WSL + QEMU headless (`-vga std -display none -serial stdio`), the same harness used previously; login-gate bypass is a temporary edit reverted before commit.
- Prereq: the remote browser (plan `2026-07-28-careos-remote-browser.md`) is implemented, since its image blit is the primary target.

## File Structure

| File | Create/Modify | Responsibility |
|------|---------------|----------------|
| `gui/gfx.c`, `gui/gui.h` | MODIFY | `gfx_blit_argb()` fast image blit |
| `apps/app_browser.c` | MODIFY | Use `gfx_blit_argb` instead of per-pixel loop |
| `gui/gui.c` | MODIFY | Frame pacing cap; (contingency) per-window surface compositing |
| `gui/wm.c`, `gui/gui.h` | MODIFY (contingency) | Per-window surface buffers + `content_dirty` |

---

### Task 1: Fast image blit primitive + use it in the browser

**Files:**
- Modify: `gui/gfx.c`, `gui/gui.h` (declare)
- Modify: `apps/app_browser.c` (replace per-pixel loop)

**Interfaces:**
- Produces: `void gfx_blit_argb(i32 dx, i32 dy, const u32 *src, i32 sw, i32 sh, i32 clip_x, i32 clip_y, i32 clip_w, i32 clip_h);` — copies `src` (row-major, `sw` wide) into the current target at `(dx,dy)`, clipped to both the target and the given clip rect, one `kmemcpy` per visible row. Marks the covered area dirty via `gfx_dirty`.

- [ ] **Step 1: Declare in gui.h**

Near the other `gfx_*` blit declarations:
```c
void gfx_blit_argb(i32 dx, i32 dy, const u32 *src, i32 sw, i32 sh,
                   i32 clip_x, i32 clip_y, i32 clip_w, i32 clip_h);
```

- [ ] **Step 2: Implement in gfx.c**

```c
void gfx_blit_argb(i32 dx, i32 dy, const u32 *src, i32 sw, i32 sh,
                   i32 clip_x, i32 clip_y, i32 clip_w, i32 clip_h){
    if(!g_target || !g_target->pixels || !src) return;
    i32 tw=(i32)g_target->w, th=(i32)g_target->h, st=(i32)(g_target->pitch/4);
    /* Intersect destination rect with clip rect and target bounds. */
    i32 x0=dx, y0=dy, x1=dx+sw, y1=dy+sh;
    if(x0<clip_x) x0=clip_x; if(y0<clip_y) y0=clip_y;
    if(x1>clip_x+clip_w) x1=clip_x+clip_w; if(y1>clip_y+clip_h) y1=clip_y+clip_h;
    if(x0<0) x0=0; if(y0<0) y0=0; if(x1>tw) x1=tw; if(y1>th) y1=th;
    if(x1<=x0||y1<=y0) return;
    i32 roww=x1-x0;
    for(i32 y=y0;y<y1;y++){
        const u32 *s=&src[(u32)(y-dy)*(u32)sw + (u32)(x0-dx)];
        u32 *d=&g_target->pixels[(u32)y*(u32)st + (u32)x0];
        kmemcpy(d, s, (u32)roww*4u);
    }
    gfx_dirty(x0,y0,roww,y1-y0);
}
```

- [ ] **Step 3: Use it in app_browser_draw**

Replace the per-pixel loop from the browser plan's Task 5 Step 5:
```c
    if (w->browser_remote && !w->browser_remote_error && w->browser_img_px) {
        rect_t cr = wm_client_rect(w);
        gfx_blit_argb(cr.x, cr.y + BROW_BAR_H, w->browser_img_px,
                      (i32)w->browser_img_w, (i32)w->browser_img_h,
                      cr.x, cr.y + BROW_BAR_H, cr.w, cr.h - BROW_BAR_H - BROW_STAT_H);
        return;
    }
```

- [ ] **Step 4: Build, verify clean**

Run: `make` → Expected: clean.

- [ ] **Step 5: Commit**

```bash
git add gui/gfx.c gui/gui.h apps/app_browser.c
git commit -m "perf(gfx): row-blit for images; browser uses it instead of per-pixel"
```

---

### Task 2: Frame pacing cap

**Files:**
- Modify: `gui/gui.c` (main loop tail)

**Interfaces:**
- Consumes: `timer_get_ticks()`. Produces: bounded loop rate (~60 fps ceiling) so it never spin-burns a core; no behavior change other than yielding when a frame finished early and nothing is pending.

- [ ] **Step 1: Add a per-iteration frame floor**

After `gfx_flip(); needs_redraw=false;` in the main loop, add:
```c
                /* Frame pacing: hold ~60 fps ceiling so we never spin a core. */
                {
                    static u32 last_frame_ms = 0;
                    u32 fnow = timer_get_ticks();
                    u32 spent = fnow - last_frame_ms;
                    if (spent < 16) timer_wait(16 - spent);
                    last_frame_ms = timer_get_ticks();
                }
```

- [ ] **Step 2: Build + boot sanity (QEMU headless)**

Run `make`; boot headless; confirm the desktop reaches the main loop and stays responsive (serial shows `[gui_run] entering main loop`, no stall).

- [ ] **Step 3: Commit**

```bash
git add gui/gui.c
git commit -m "perf(gui): cap main loop to ~60 fps to stop spin-burn"
```

---

### Task 3: Worst-case fps measurement (acceptance test)

**Files:**
- Modify (temporary, reverted before commit): `gui/gui.c` (login bypass + auto-open windows + fps counter)

**Interfaces:** none produced; this task produces a **measurement result** recorded in the commit message / plan checkbox.

- [ ] **Step 1: Add temporary harness**

In `gui/gui.c`: (a) early `return true;` in `run_login_flow`; (b) after the Terminal `wm_open`, also `wm_open(APP_FILES,…)` and `wm_open(APP_BROWSER,…)`; (c) in `app_browser_init` temporarily auto-navigate to a real site so an image is present; (d) re-add the 60-frame fps serial counter in the draw block, and force `needs_redraw=true` each frame to measure the sustained ceiling.

- [ ] **Step 2: Build + measure**

Run headless (QEMU, `-vga std -display none -serial stdio`), grep `[PERF]`. To emulate dragging, temporarily jitter `mouse.x/mouse.y` a few px each frame in the harness so the compositor takes the worst-case (over-chrome rebuild) path part of the time.
Record fps. **Acceptance: sustained ≥30 fps.**

- [ ] **Step 3: Decide**

- If ≥30 fps: revert all temporary harness edits, `make`, confirm clean, and record the number. Skip Task 4.
- If <30 fps: revert the auto-open/auto-nav but keep a note of the measured number; proceed to Task 4 (per-window surface cache), then re-measure.

- [ ] **Step 4: Revert harness + commit the measurement note**

```bash
git add gui/gui.c
git commit -m "perf: measured worst-case desktop at <N> fps (>=30 floor)"
```

---

### Task 4 (CONTINGENCY — only if Task 3 measured <30 fps): Per-window surface cache

Execute this task **only** if Task 3's measurement was below 30 fps. It removes per-window content re-rendering from the hot path by giving each window its own surface and re-rendering content only when it changes.

**Files:**
- Modify: `gui/gui.h` (window_t: `u32 *surface; u32 surf_w, surf_h; bool content_dirty;`)
- Modify: `gui/wm.c` (allocate/free surface on open/close/resize; `wm_draw_all` renders dirty windows into their surface then blits)
- Modify: `gui/gfx.c` (target origin so absolute-coord app draws land in a window surface)

**Interfaces:**
- Consumes: `gfx_set_target`, `gfx_blit_argb` (Task 1).
- Produces: `void wm_mark_content_dirty(window_t *w);` — apps call it when their content changes (text appended, remote image swapped, edit). Move/raise/lower/focus do NOT call it.

- [ ] **Step 1: Add gfx target origin**

Add `i32 origin_x, origin_y;` to `gfx_buffer_t`; in the core pixel path subtract the origin so an app drawing at absolute `(x,y)` writes to `surface[(y-origin_y)*surf_w + (x-origin_x)]`, clipped to `0..surf_w/surf_h`. Set origin=0 for the screen buffer (unchanged behavior). Verify the existing desktop still renders identically (screenshot compare) before proceeding.

- [ ] **Step 2: window_t fields + alloc**

Add the surface fields; allocate `surface = kmalloc(w*h*4)` in `wm_open` and on resize; free in `wm_close`. `content_dirty=true` initially.

- [ ] **Step 3: Render-to-surface in wm_draw_all**

For each window in z-order:
```c
    if (w->content_dirty && w->surface) {
        gfx_buffer_t wb = { w->surface, w->surf_w, w->surf_h, w->surf_w*4, w->x, w->y };
        gfx_set_target(&wb);
        wm_draw_window_content(w);     /* existing per-app draw dispatch */
        gfx_set_target(0);             /* back to screen backbuffer */
        w->content_dirty = false;
    }
    gfx_blit_argb(w->x, w->y, w->surface, (i32)w->surf_w, (i32)w->surf_h,
                  w->x, w->y, (i32)w->surf_w, (i32)w->surf_h);
```

- [ ] **Step 4: Wire content_dirty triggers**

`wm_mark_content_dirty(w)` from: any `app_*_key`, any content-changing `app_*_click`, terminal output append, `browser_remote_request` image swap, editor/notes edits, resize. Add a low-rate safety: mark focused window dirty on the existing ~250 ms heartbeat so live content (e.g. terminal cursor blink) still updates.

- [ ] **Step 5: Re-measure (repeat Task 3 harness)**

Confirm the worst case is now ≥30 fps. Record the number.

- [ ] **Step 6: Commit**

```bash
git add gui/gui.h gui/wm.c gui/gfx.c apps/*.c
git commit -m "perf(wm): per-window surface cache; content re-render only when dirty"
```

---

## Self-Review Notes (coverage)
- Spec "per-window surface cache" → Task 4 (contingency, gated on measurement).
- Spec "dirty-rect flips" → `gfx_blit_argb` calls `gfx_dirty` (Task 1); the surface blit path (Task 4) feeds real rects instead of `dirty_full`.
- Spec "frame pacing" → Task 2. Spec "acceptance test ≥30 fps worst case" → Task 3 (+ re-measure in Task 4).
- Rationale for gating Task 4: the backdrop cache already measured ~100 fps with per-frame window redraw, so the refactor may be unnecessary; measure before paying its complexity/risk.
