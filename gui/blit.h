/* gui/blit.h -- clip math and backbuffer->framebuffer row copies (pure). */
#ifndef CAREOS_BLIT_H
#define CAREOS_BLIT_H
#include "pure.h"

typedef struct { i32 x0, y0, x1, y1; } blit_box_t;

/* Intersect the destination rect (dx,dy,sw,sh) with the clip rect and the
 * target bounds (0,0,tw,th). Half-open box in *out; false when empty or when
 * any size is non-positive. 64-bit math: huge inputs cannot wrap. */
bool blit_clip(i32 dx, i32 dy, i32 sw, i32 sh, i32 cx, i32 cy, i32 cw, i32 ch,
               i32 tw, i32 th, blit_box_t *out);

/* Copy the rect (x,y,w,h) of a 0x00RRGGBB backbuffer (src_w pixels per row)
 * into a framebuffer with any pitch, 24 or 32 bpp, and any channel shifts.
 * Rows are plain memcpy when the format is native (32 bpp, shifts 16/8/0).
 * The caller guarantees the rect lies inside both buffers. */
void blit_copy_rows(u8 *dst, u32 dst_pitch, u32 dst_bpp, const u32 *src, u32 src_w,
                    i32 x, i32 y, i32 w, i32 h, u8 rs, u8 gs, u8 bs);

#endif
