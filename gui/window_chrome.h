/* Window chrome geometry shared by painting and input. No framebuffer or
 * window-manager dependency, so boundary cases can be tested on the host. */
#ifndef CAREOS_WINDOW_CHROME_H
#define CAREOS_WINDOW_CHROME_H
#include "pure.h"

#define WM_CHROME_RADIUS 22
#define WM_CONTROL_LEFT 14
#define WM_CONTROL_SIZE 14
#define WM_CONTROL_STEP 22
#define WM_TITLE_LEFT 96
#define WM_SNAP_WIDTH 180
#define WM_SNAP_HEIGHT 120

enum { WM_CONTROL_NONE = -1, WM_CONTROL_CLOSE, WM_CONTROL_MIN, WM_CONTROL_MAX };

static inline i32 wm_control_x(i32 wx, i32 control) {
    return wx + WM_CONTROL_LEFT + WM_CONTROL_SIZE / 2 + control * WM_CONTROL_STEP;
}

/* Non-overlapping 22x28 targets, centred on the visible 14px lights. */
static inline i32 wm_control_hit(i32 wx, i32 wy, i32 title_h, i32 mx, i32 my) {
    i32 cy = wy + title_h / 2;
    if (my < cy - 14 || my >= cy + 14) return WM_CONTROL_NONE;
    i32 left = wm_control_x(wx, WM_CONTROL_CLOSE) - WM_CONTROL_STEP / 2;
    if (mx < left || mx >= left + 3 * WM_CONTROL_STEP) return WM_CONTROL_NONE;
    return (mx - left) / WM_CONTROL_STEP;
}

/* Anchor below the green light and keep the flyout on screen, even when the
 * window is partly off screen. */
static inline i32 wm_snap_x(i32 wx, i32 screen_w) {
    i32 x = wx + WM_CONTROL_LEFT;
    i32 last = screen_w - WM_SNAP_WIDTH - 4;
    if (x > last) x = last;
    return x < 4 ? 4 : x;
}

typedef struct {
    u32 body, bar, border, divider, title, muted, inactive;
    u32 close, minimize, maximize, glyph;
} wm_chrome_palette_t;

/* Palette lives with the other theme colours, not in the renderer. */
const wm_chrome_palette_t *theme_window_chrome(void);
#endif
