/* gui/wcache.h -- per-window pixel cache policy (pure; host-tested).
 *
 * Re-rendering every window's chrome and app content each frame cost ~35 ms
 * of a ~80 ms frame with four windows open (QEMU). Unfocused, settled windows
 * rarely change, so they are drawn once, snapshotted, and blitted until
 * something that could change their pixels happens. */
#ifndef CAREOS_WCACHE_H
#define CAREOS_WCACHE_H
#include "pure.h"

#define WCACHE_REFRESH_MS 500u   /* background content still updates at ~2 Hz */

typedef struct {
    bool valid;
    i32  w, h;                   /* size at capture */
    u32  captured_ms;
    u32  theme_id, font_id;
} wcache_state_t;

typedef struct {
    bool focused, animating, hover_near, busy;
    i32  x, y, w, h;             /* window rect now */
    i32  screen_w, screen_h;
    u32  now_ms, theme_id, font_id;
    u32  stagger_ms;             /* per-window offset so refreshes don't coincide */
} wcache_query_t;

typedef enum { WC_DRAW = 0, WC_DRAW_AND_CAPTURE, WC_BLIT } wcache_action_t;

/* Decide how to draw this window this frame. May invalidate *st. */
wcache_action_t wcache_decide(wcache_state_t *st, const wcache_query_t *q);
/* Record that the window's pixels were just captured. */
void wcache_captured(wcache_state_t *st, const wcache_query_t *q);

#endif
