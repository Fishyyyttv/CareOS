/* gui/ui_glyph_raster.c -- anti-aliased stroke/fill rasteriser for the v10
 * system glyphs (polylines from tools/gen-ui-glyphs.py).
 *
 * Work is in Q8 viewbox units. A stroke's coverage at a pixel is how far its
 * centre lies inside the 2-unit-wide capsule around the nearest segment,
 * converted to pixels and clamped to one pixel of ramp; that is a box-filtered
 * edge, the same look as a browser's 2 px SVG stroke. Fills use 4x4
 * supersampling with the even-odd rule. */
#include "ui_glyph_raster.h"
#include "ui_glyph_data.h"

#define HALF_W 256            /* stroke half-width: 1 viewbox unit, Q8 */

u32 ui_glyph_raster_count(void) { return UI_GLYPH_DATA_COUNT; }

static u32 isqrt64(u64 v) {
    u64 r = 0, bit = 1ull << 62;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    return (u32)r;
}

/* Squared distance (Q16) from p to segment a-b, all Q8. */
static u64 seg_d2(i64 px, i64 py, i64 ax, i64 ay, i64 bx, i64 by) {
    i64 dx = bx - ax, dy = by - ay;
    i64 len2 = dx * dx + dy * dy;
    i64 ex = px - ax, ey = py - ay;
    if (len2 > 0) {
        i64 t = ex * dx + ey * dy;
        if (t > 0) {
            if (t >= len2) { ex = px - bx; ey = py - by; }
            else {
                /* foot of the perpendicular: a + d * t / len2 */
                i64 fx = ax + dx * t / len2, fy = ay + dy * t / len2;
                ex = px - fx; ey = py - fy;
            }
        }
    }
    return (u64)(ex * ex + ey * ey);
}

static i64 pt(u32 i, u32 c) { return (i64)UI_GLYPH_PTS[i * 2 + c] * 16; }   /* Q4 -> Q8 */

static bool inside_even_odd(const u16 *def, i64 px, i64 py) {
    /* def[0] = first path, def[1] = path count */
    bool in = false;
    for (u32 p = def[0]; p < (u32)def[0] + def[1]; p++) {
        u32 s = UI_GLYPH_PATHS[p][0], n = UI_GLYPH_PATHS[p][1];
        for (u32 k = 0; k < n; k++) {
            u32 j = (k + 1) % n;
            i64 ax = pt(s + k, 0), ay = pt(s + k, 1), bx = pt(s + j, 0), by = pt(s + j, 1);
            if ((ay > py) != (by > py)) {
                i64 xi = ax + (bx - ax) * (py - ay) / (by - ay);
                if (px < xi) in = !in;
            }
        }
    }
    return in;
}

static void raster_def(const u16 *def, bool fill, u32 size, u8 *mask) {
    i64 S = (i64)size;

    for (u32 y = 0; y < size; y++) {
        for (u32 x = 0; x < size; x++) {
            u32 cov = 0;
            if (fill) {
                u32 hits = 0;
                for (u32 sy = 0; sy < 4; sy++)
                    for (u32 sx = 0; sx < 4; sx++) {
                        i64 px = ((i64)(x * 8 + sx * 2 + 1) * 24 * 256) / (8 * S);
                        i64 py = ((i64)(y * 8 + sy * 2 + 1) * 24 * 256) / (8 * S);
                        if (inside_even_odd(def, px, py)) hits++;
                    }
                cov = hits * 255u / 16u;
            } else {
                i64 px = ((i64)(2 * x + 1) * 24 * 256) / (2 * S);
                i64 py = ((i64)(2 * y + 1) * 24 * 256) / (2 * S);
                u64 best = ~0ull;
                for (u32 p = def[0]; p < (u32)def[0] + def[1]; p++) {
                    u32 s = UI_GLYPH_PATHS[p][0], n = UI_GLYPH_PATHS[p][1];
                    u32 segs = UI_GLYPH_PATHS[p][2] ? n : n - 1;
                    if (n == 1) segs = 1;
                    for (u32 k = 0; k < segs; k++) {
                        u32 j = (n == 1) ? k : (k + 1) % n;
                        u64 d2 = seg_d2(px, py, pt(s + k, 0), pt(s + k, 1), pt(s + j, 0), pt(s + j, 1));
                        if (d2 < best) best = d2;
                    }
                }
                /* (HALF_W - d) in Q8 units -> pixels, centred on a 1 px ramp */
                i64 d = (i64)isqrt64(best);
                i64 c = 128 + ((HALF_W - d) * S) / 24;
                cov = c <= 0 ? 0u : c >= 255 ? 255u : (u32)c;
            }
            mask[y * size + x] = (u8)cov;
        }
    }
}

bool ui_glyph_raster(u32 glyph, u32 size, u8 *mask) {
    if (glyph >= UI_GLYPH_DATA_COUNT || size < 4 || size > UI_GLYPH_MAX_SIZE || !mask) return false;
    raster_def(UI_GLYPH_DEFS[glyph], UI_GLYPH_DEFS[glyph][2] != 0, size, mask);
    return true;
}

u32 ui_icon_raster_count(void) { return UI_ICON_DATA_COUNT; }

/* Coverage (0..256) of a rounded square of side s, radius r, at pixel (x,y):
 * distance from the pixel centre to the nearest corner circle, 1 px ramp. */
static u32 tile_cov(i32 x, i32 y, i32 s, i32 r) {
    if (x < 0 || y < 0 || x >= s || y >= s) return 0;
    i32 cx = x < r ? r : (x >= s - r ? s - r : -1);
    i32 cy = y < r ? r : (y >= s - r ? s - r : -1);
    if (cx < 0 || cy < 0) return 256;
    /* corner centre at (cx, cy) in pixel-edge units; pixel centre at x+.5 */
    i64 dx = (i64)(2 * x + 1 - 2 * cx) * 128, dy = (i64)(2 * y + 1 - 2 * cy) * 128;  /* Q8 */
    i64 d = (i64)isqrt64((u64)(dx * dx + dy * dy));
    i64 c = 128 + ((i64)r * 256 - d);
    return c <= 0 ? 0u : c >= 256 ? 256u : (u32)c;
}

static u32 lerp_rgb(u32 a, u32 b, u32 t /* 0..256 */) {
    u32 r = (((a >> 16) & 0xFF) * (256 - t) + ((b >> 16) & 0xFF) * t) >> 8;
    u32 g = (((a >>  8) & 0xFF) * (256 - t) + ((b >>  8) & 0xFF) * t) >> 8;
    u32 c = (( a        & 0xFF) * (256 - t) + ( b        & 0xFF) * t) >> 8;
    return (r << 16) | (g << 8) | c;
}

bool ui_icon_raster(u32 icon, u32 size, u32 *argb) {
    static u8 glyph[UI_GLYPH_MAX_SIZE * UI_GLYPH_MAX_SIZE];
    if (icon >= UI_ICON_DATA_COUNT || size < 12 || size > UI_ICON_MAX_SIZE || !argb) return false;
    i32 s = (i32)size, r = (s * 28 + 50) / 100;
    u32 c0 = UI_ICON_COLORS[icon][0], c1 = UI_ICON_COLORS[icon][1], fg = UI_ICON_COLORS[icon][2];

    /* glyph: 54 % of the tile (50 % for the launcher grid), centred */
    i32 g = (icon == UI_ICON_DATA_COUNT - 2) ? (s * 50 + 50) / 100 : (s * 54 + 50) / 100;
    if (g > UI_GLYPH_MAX_SIZE) g = UI_GLYPH_MAX_SIZE;
    if (g < 4) g = 4;
    raster_def(UI_ICON_DEFS[icon], false, (u32)g, glyph);
    i32 go = (s - g) / 2;

    /* linear-gradient(160deg): direction (sin 160, -cos 160) = (.342, .940) */
    i64 len = (i64)s * 1282;                     /* |dir.x| + |dir.y|, x1000 */
    for (i32 y = 0; y < s; y++) {
        for (i32 x = 0; x < s; x++) {
            u32 cov = tile_cov(x, y, s, r);
            if (!cov) { argb[y * s + x] = 0; continue; }
            i64 proj = (i64)(2 * x + 1 - s) * 342 + (i64)(2 * y + 1 - s) * 940;   /* x2 */
            i64 t = 128 + (proj * 128) / len;
            if (t < 0) t = 0;
            if (t > 256) t = 256;
            u32 col = lerp_rgb(c0, c1, (u32)t);
            /* inset 0 1px 0 white 30 %: the top 1 px band inside the shape */
            u32 up = tile_cov(x, y - 1, s, r);
            if (up < cov) col = lerp_rgb(col, 0xFFFFFF, (cov - up) * 77 / 256);
            /* inset 0 -2px 0 black 12 %: the bottom 2 px band */
            u32 dn = tile_cov(x, y + 2, s, r);
            if (dn < cov) col = lerp_rgb(col, 0x000000, (cov - dn) * 31 / 256);
            /* glyph */
            i32 gx = x - go, gy = y - go;
            if (gx >= 0 && gy >= 0 && gx < g && gy < g) {
                u32 a = glyph[gy * g + gx];
                if (a) col = lerp_rgb(col, fg, a + (a >> 7));
            }
            u32 alpha = cov >= 256 ? 255u : cov;
            argb[y * s + x] = (alpha << 24) | col;
        }
    }
    return true;
}
