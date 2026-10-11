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
