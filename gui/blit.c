/* gui/blit.c -- clip math and backbuffer->framebuffer row copies (pure). */
#include "blit.h"

static i64 max64(i64 a, i64 b) { return a > b ? a : b; }
static i64 min64(i64 a, i64 b) { return a < b ? a : b; }

bool blit_clip(i32 dx, i32 dy, i32 sw, i32 sh, i32 cx, i32 cy, i32 cw, i32 ch,
               i32 tw, i32 th, blit_box_t *o) {
    if (sw <= 0 || sh <= 0 || cw <= 0 || ch <= 0 || tw <= 0 || th <= 0) return false;
    i64 x0 = max64(max64(dx, cx), 0);
    i64 y0 = max64(max64(dy, cy), 0);
    i64 x1 = min64(min64((i64)dx + sw, (i64)cx + cw), tw);
    i64 y1 = min64(min64((i64)dy + sh, (i64)cy + ch), th);
    if (x1 <= x0 || y1 <= y0) return false;
    o->x0 = (i32)x0; o->y0 = (i32)y0; o->x1 = (i32)x1; o->y1 = (i32)y1;
    return true;
}

void blit_copy_rows(u8 *dst, u32 dst_pitch, u32 dst_bpp, const u32 *src, u32 src_w,
                    i32 x, i32 y, i32 w, i32 h, u8 rs, u8 gs, u8 bs) {
    if (w <= 0 || h <= 0) return;
    bool native = (dst_bpp == 32 && rs == 16 && gs == 8 && bs == 0);
    u32 bpp = dst_bpp / 8u;
    for (i32 r = y; r < y + h; r++) {
        const u32 *s = src + (u64)(u32)r * src_w + (u32)x;
        u8 *d = dst + (u64)(u32)r * dst_pitch + (u64)(u32)x * bpp;
        if (native) { kmemcpy(d, s, (size_t)w * 4u); continue; }
        for (i32 i = 0; i < w; i++) {
            u32 c = s[i];
            u32 p = (((c >> 16) & 0xFFu) << rs) | (((c >> 8) & 0xFFu) << gs) | ((c & 0xFFu) << bs);
            d[0] = (u8)p; d[1] = (u8)(p >> 8); d[2] = (u8)(p >> 16);
            if (bpp == 4) d[3] = 0;
            d += bpp;
        }
    }
}
