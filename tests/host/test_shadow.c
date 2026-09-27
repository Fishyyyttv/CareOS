/* tests/host/test_shadow.c -- single-pass soft shadow: every pixel gets one
 * blend whose alpha equals the stacked rings it used to receive one by one. */
#include "pure.h"
#include "shadow.h"
#include <assert.h>

/* Brute force: innermost ring (1..L) covering pixel (px,yy), 0 if none, -1
 * if the pixel is inside the opaque body hole. */
static i32 brute_level(i32 x, i32 y, i32 w, i32 h, i32 r, i32 blur, i32 dy, i32 L,
                       bool hollow, i32 px, i32 yy) {
    if (hollow && rr_contains(x, y, w, h, r, px, yy)) return -1;
    i32 best = 0;
    for (i32 i = L; i >= 1; i--) {
        i32 s = blur * i / L;
        if (rr_contains(x - s, y - s + dy, w + 2 * s, h + 2 * s, r + s, px, yy)) best = i;
    }
    return best;
}

static void check_geometry(i32 x, i32 y, i32 w, i32 h, i32 r, i32 blur, i32 dy, bool hollow) {
    const i32 L = 6;
    shadow_seg_t seg[32];
    for (i32 yy = y - blur - 4 + dy; yy < y + h + blur + 4 + dy; yy++) {
        u32 n = shadow_row(x, y, w, h, r, blur, dy, L, hollow, yy, seg, 32);
        for (i32 px = x - blur - 4; px < x + w + blur + 4; px++) {
            i32 want = brute_level(x, y, w, h, r, blur, dy, L, hollow, px, yy);
            i32 got = 0;
            for (u32 k = 0; k < n; k++)
                if (px >= seg[k].x0 && px < seg[k].x1) { assert(got == 0); got = seg[k].level; }
            if (want == -1) want = 0;               /* hole: no blend */
            if (got != want) {
                printf("mismatch geom(%d,%d,%d,%d r%d b%d dy%d) px=%d yy=%d got=%d want=%d\n",
                       x, y, w, h, r, blur, dy, px, yy, got, want);
                assert(0);
            }
        }
    }
}

static void test_combined_alpha_matches_sequential(void) {
    /* Blend COL onto a background ring by ring (outer first, as the old code
     * did) and compare with one blend at the combined alpha. */
    const u32 L = 6, alpha = 110;
    for (u32 level = 1; level <= L; level++) {
        u32 bg = 200, col = 0, seq = bg;
        u32 denom = L * (L + 1) / 2;
        for (u32 i = L; i >= level; i--) {
            u32 a = alpha * (L - i + 1) / denom; if (a == 0) a = 1;
            seq = (seq * (256 - a) + col * a) >> 8;
        }
        u32 A = shadow_combined_alpha(alpha, L, level);
        u32 one = (bg * (256 - A) + col * A) >> 8;
        i32 d = (i32)one - (i32)seq; if (d < 0) d = -d;
        if (d > 2) { printf("level %u: seq=%u one=%u A=%u\n", level, seq, one, A); assert(0); }
    }
}

int main(void) {
    check_geometry(100, 100, 300, 200, 12, 24, 8, true);
    check_geometry(100, 100, 300, 200, 12, 18, 4, true);
    check_geometry(50, 60, 40, 30, 12, 24, 8, true);      /* tiny window, radius clamps */
    check_geometry(10, 10, 200, 120, 0, 12, 0, true);     /* square corners */
    check_geometry(100, 100, 300, 200, 12, 24, 8, false); /* full (transition) shadow */
    test_combined_alpha_matches_sequential();
    puts("shadow OK");
    return 0;
}
