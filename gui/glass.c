/* gui/glass.c -- blur, backdrop signature and frame guard for UI glass. */
#include "glass.h"

u32 glass_scratch_words(u32 w, u32 h) {
    u32 hw = (w + 1) / 2, hh = (h + 1) / 2;
    return hw * hh + (hw > hh ? hw : hh);        /* half-res image + one line */
}

/* One box pass, horizontal or vertical, in place, sliding window. */
static void box_pass(u32 *px, u32 w, u32 h, u32 rad, bool vertical, u32 *line) {
    u32 outer = vertical ? w : h, n = vertical ? h : w;
    for (u32 o = 0; o < outer; o++) {
        for (u32 i = 0; i < n; i++) line[i] = vertical ? px[i * w + o] : px[o * w + i];
        u32 sr = 0, sg = 0, sb = 0, cnt = 0;
        for (u32 k = 0; k <= rad && k < n; k++) {
            u32 c = line[k];
            sr += (c >> 16) & 0xFF; sg += (c >> 8) & 0xFF; sb += c & 0xFF; cnt++;
        }
        for (u32 i = 0; i < n; i++) {
            u32 v = ((sr / cnt) << 16) | ((sg / cnt) << 8) | (sb / cnt);
            if (vertical) px[i * w + o] = v; else px[o * w + i] = v;
            if (i >= rad) {
                u32 c = line[i - rad];
                sr -= (c >> 16) & 0xFF; sg -= (c >> 8) & 0xFF; sb -= c & 0xFF; cnt--;
            }
            if (i + rad + 1 < n) {
                u32 c = line[i + rad + 1];
                sr += (c >> 16) & 0xFF; sg += (c >> 8) & 0xFF; sb += c & 0xFF; cnt++;
            }
        }
    }
}

static u32 avg4(u32 a, u32 b, u32 c, u32 d) {
    u32 r = (((a >> 16) & 0xFF) + ((b >> 16) & 0xFF) + ((c >> 16) & 0xFF) + ((d >> 16) & 0xFF) + 2) / 4;
    u32 g = (((a >> 8) & 0xFF) + ((b >> 8) & 0xFF) + ((c >> 8) & 0xFF) + ((d >> 8) & 0xFF) + 2) / 4;
    u32 l = ((a & 0xFF) + (b & 0xFF) + (c & 0xFF) + (d & 0xFF) + 2) / 4;
    return (r << 16) | (g << 8) | l;
}

static u32 lerp2(u32 a, u32 b, u32 t /* 0..256 */) {
    u32 r = (((a >> 16) & 0xFF) * (256 - t) + ((b >> 16) & 0xFF) * t) >> 8;
    u32 g = (((a >> 8) & 0xFF) * (256 - t) + ((b >> 8) & 0xFF) * t) >> 8;
    u32 l = ((a & 0xFF) * (256 - t) + (b & 0xFF) * t) >> 8;
    return (r << 16) | (g << 8) | l;
}

void glass_blur(const u32 *src, u32 stride, u32 w, u32 h, u32 blur, u32 *dst, u32 *scratch) {
    if (!src || !dst || !scratch || w == 0 || h == 0) return;
    u32 hw = (w + 1) / 2, hh = (h + 1) / 2;
    u32 *half = scratch, *line = scratch + hw * hh;

    for (u32 y = 0; y < hh; y++) {
        u32 y0 = y * 2, y1 = y0 + 1 < h ? y0 + 1 : y0;
        for (u32 x = 0; x < hw; x++) {
            u32 x0 = x * 2, x1 = x0 + 1 < w ? x0 + 1 : x0;
            half[y * hw + x] = avg4(src[y0 * stride + x0], src[y0 * stride + x1],
                                    src[y1 * stride + x0], src[y1 * stride + x1]);
        }
    }
    /* CSS blur(b) is a Gaussian with sigma b px. Three box passes of radius r
     * have variance r(r+1), so sigma ~ r; at half resolution sigma is b/2. */
    u32 rad = blur / 2;
    if (rad < 1) rad = 1;
    for (u32 p = 0; p < 3; p++) {
        box_pass(half, hw, hh, rad, false, line);
        box_pass(half, hw, hh, rad, true, line);
    }
    /* Bilinear upsample: dst pixel centre maps to (x + .5) / 2 - .5 in half
     * coordinates, i.e. x/2 - 1/4; kept in Q8. */
    for (u32 y = 0; y < h; y++) {
        i32 fy = (i32)(y * 128) - 64;
        if (fy < 0) fy = 0;
        u32 iy = (u32)fy >> 8, ty = (u32)fy & 255;
        u32 iy1 = iy + 1 < hh ? iy + 1 : iy;
        for (u32 x = 0; x < w; x++) {
            i32 fx = (i32)(x * 128) - 64;
            if (fx < 0) fx = 0;
            u32 ix = (u32)fx >> 8, tx = (u32)fx & 255;
            u32 ix1 = ix + 1 < hw ? ix + 1 : ix;
            u32 top = lerp2(half[iy * hw + ix], half[iy * hw + ix1], tx);
            u32 bot = lerp2(half[iy1 * hw + ix], half[iy1 * hw + ix1], tx);
            dst[y * w + x] = lerp2(top, bot, ty);
        }
    }
}

u32 glass_signature(const u32 *src, u32 stride, u32 w, u32 h) {
    u32 hsh = 2166136261u;
    if (!src || w == 0 || h == 0) return hsh;
    for (u32 y = 0; y < h; y += 3) {
        const u32 *row = src + y * stride;
        for (u32 x = 0; x < w; x += 4) hsh = (hsh ^ (row[x] & 0xFFFFFFu)) * 16777619u;
        hsh = (hsh ^ (row[w - 1] & 0xFFFFFFu)) * 16777619u;
    }
    const u32 *last = src + (h - 1) * stride;
    for (u32 x = 0; x < w; x += 2) hsh = (hsh ^ (last[x] & 0xFFFFFFu)) * 16777619u;
    return hsh;
}

void glass_guard_frame(glass_guard_t *g, u32 frame_ms, u32 now_ms) {
    if (!g) return;
    if (!g->tint_only) {
        g->slow_run = frame_ms > 33 ? g->slow_run + 1 : 0;
        if (g->slow_run >= 3) { g->tint_only = true; g->calm_since = now_ms; g->slow_run = 0; }
        return;
    }
    if (frame_ms > 20) g->calm_since = now_ms;
    else if (now_ms - g->calm_since >= 1000) g->tint_only = false;
}
