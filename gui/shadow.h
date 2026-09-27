/* gui/shadow.h -- single-pass soft drop shadow geometry (pure; host-tested).
 *
 * The CDL shadow is L stacked rounded rects ("rings"), ring i (1..L) grown by
 * s_i = blur*i/L, each blended at a small alpha. Blending them one after the
 * other touched most shadow pixels L times. Here each pixel is blended once:
 * a row is split into segments by the innermost ring covering them, and every
 * segment uses the alpha the stacked rings would have added up to. */
#ifndef CAREOS_SHADOW_H
#define CAREOS_SHADOW_H
#include "pure.h"

typedef struct { i32 x0, x1; i32 level; } shadow_seg_t;   /* level: innermost ring, 1..L */

/* Row span [*x0, *x1) of rounded rect (x,y,w,h,r) at screen row yy; false if
 * the rect does not cover that row. r is clamped to fit. */
bool rr_row_span(i32 x, i32 y, i32 w, i32 h, i32 r, i32 yy, i32 *x0, i32 *x1);
bool rr_contains(i32 x, i32 y, i32 w, i32 h, i32 r, i32 px, i32 py);

/* Segments of screen row yy for the shadow of body (x,y,w,h,r). With hollow,
 * pixels inside the body are skipped (the opaque body covers them). */
u32 shadow_row(i32 x, i32 y, i32 w, i32 h, i32 r, i32 blur, i32 dy, i32 L, bool hollow,
               i32 yy, shadow_seg_t *out, u32 max);

/* Alpha (0..256) equal to blending rings level..L one after another. */
u32 shadow_combined_alpha(u32 alpha, u32 L, u32 level);

#endif
