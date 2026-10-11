/* gui/shadow.c -- single-pass soft drop shadow geometry (pure; host-tested). */
#include "shadow.h"

static u32 isqrt_u32(u32 v) {
    u32 x = v, y = (x + 1) / 2;
    if (v < 2) return v;
    while (y < x) { x = y; y = (x + v / x) / 2; }
    return x;
}

bool rr_row_span(i32 x, i32 y, i32 w, i32 h, i32 r, i32 yy, i32 *x0, i32 *x1) {
    if (w <= 0 || h <= 0 || yy < y || yy >= y + h) return false;
    if (r < 0) r = 0;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    i32 row = yy - y, dy = -1, inset = 0;
    if (row < r) dy = (r - 1) - row;
    else if (row >= h - r) dy = row - (h - r);
    if (r > 0 && dy >= 0) inset = r - (i32)isqrt_u32((u32)(r * r - dy * dy));
    if (w - 2 * inset <= 0) return false;
    *x0 = x + inset; *x1 = x + w - inset;
    return true;
}

bool rr_contains(i32 x, i32 y, i32 w, i32 h, i32 r, i32 px, i32 py) {
    i32 a, b;
    return rr_row_span(x, y, w, h, r, py, &a, &b) && px >= a && px < b;
}

u32 shadow_row(i32 x, i32 y, i32 w, i32 h, i32 r, i32 blur, i32 dy, i32 L, bool hollow,
               i32 yy, shadow_seg_t *out, u32 max) {
    if (L < 1 || L > 16 || blur <= 0 || max == 0) return 0;
    i32 rs0[17], rs1[17]; bool has[17];
    i32 bp[40]; u32 nb = 0;
    for (i32 i = 1; i <= L; i++) {
        i32 s = blur * i / L;
        has[i] = rr_row_span(x - s, y - s + dy, w + 2 * s, h + 2 * s, r + s, yy, &rs0[i], &rs1[i]);
        if (has[i]) { bp[nb++] = rs0[i]; bp[nb++] = rs1[i]; }
    }
    i32 h0 = 0, h1 = 0;
    bool hole = hollow && rr_row_span(x, y, w, h, r, yy, &h0, &h1);
    if (hole) { bp[nb++] = h0; bp[nb++] = h1; }
    if (nb == 0) return 0;
    for (u32 i = 1; i < nb; i++) {                       /* insertion sort */
        i32 v = bp[i]; u32 j = i;
        while (j > 0 && bp[j - 1] > v) { bp[j] = bp[j - 1]; j--; }
        bp[j] = v;
    }
    u32 n = 0;
    for (u32 k = 0; k + 1 < nb; k++) {
        i32 a = bp[k], b = bp[k + 1];
        if (b <= a) continue;
        i32 level = 0;
        if (!(hole && a >= h0 && a < h1)) {
            for (i32 i = 1; i <= L; i++)
                if (has[i] && a >= rs0[i] && a < rs1[i]) { level = i; break; }
        }
        if (level == 0) continue;
        if (n > 0 && out[n - 1].x1 == a && out[n - 1].level == level) { out[n - 1].x1 = b; continue; }
        if (n == max) break;
        out[n].x0 = a; out[n].x1 = b; out[n].level = level; n++;
    }
    return n;
}

u32 shadow_combined_alpha(u32 alpha, u32 L, u32 level) {
    u32 denom = L * (L + 1) / 2, t = 256;               /* transmittance, /256 */
    for (u32 i = L; i >= level && i >= 1; i--) {
        u32 a = alpha * (L - i + 1) / denom;
        if (a == 0) a = 1;
        t = t * (256 - a) / 256;
        if (i == 1) break;
    }
    return 256 - t;
}
