/* tests/host/test_pip_raster.c -- Pip's shape rasterizer. */
#include "pure.h"
#include "pip_raster.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define W 64
#define H 64
static u32 px[W * H];
static pr_canvas_t cv = { px, W, H };
#define U(x) ((i32)(x) * 65536)

static u32 alpha_at(i32 x, i32 y) { return px[y * W + x] >> 24; }
static u64 alpha_sum(void) { u64 s = 0; for (u32 i = 0; i < W * H; i++) s += px[i] >> 24; return s; }
static void clear(void) { memset(px, 0, sizeof px); }

static void test_trig_and_matrices(void) {
    assert(pr_sin(0) == 0 && pr_sin(90) == 65536 && pr_sin(270) == -65536);
    assert(pr_cos(0) == 65536 && pr_cos(180) == -65536 && pr_sin(-90) == -65536);
    pr_mat_t r = pr_rotate(90);
    i32 x, y;
    pr_apply(r, U(1), 0, &x, &y);                 /* (1,0) -> (0,1), SVG / y-down */
    assert(x == 0 && y == U(1));
    pr_mat_t m = pr_mul(pr_translate(U(10), U(5)), pr_scale(U(2), U(3)));
    pr_apply(m, U(1), U(1), &x, &y);
    assert(x == U(12) && y == U(8));
    pr_mat_t a = pr_about(pr_rotate(180), U(10), U(10));
    pr_apply(a, U(12), U(10), &x, &y);
    assert(x == U(8) && y == U(10));
}

static void test_circle_fill(void) {
    clear();
    pr_paint_t p = pr_solid(0xFFFF0000);
    pr_fill_ellipse(&cv, pr_identity(), U(32), U(32), U(20), U(20), 0, &p, 255);
    assert(alpha_at(32, 32) == 255 && alpha_at(1, 1) == 0);
    u64 area = alpha_sum() / 255;                  /* pi * 400 = 1256.6 */
    assert(area > 1230 && area < 1285);
    pr_unpremultiply(&cv);
    assert(px[32 * W + 32] == 0xFFFF0000);
}

static void test_square_exact_and_half(void) {
    clear();
    pr_paint_t p = pr_solid(0xFF00FF00);
    assert(pr_fill_path(&cv, pr_identity(), "M10 10 L30 10 L30 30 L10 30 Z", &p, 255));
    assert(alpha_at(20, 20) == 255 && alpha_at(9, 20) == 0 && alpha_at(30, 20) == 0);
    u64 area = alpha_sum();
    assert(area >= 400 * 255 - 400 && area <= 400 * 255 + 400);
    clear();
    assert(pr_fill_path(&cv, pr_identity(), "M10.5 10 L30 10 L30 30 L10.5 30 Z", &p, 255));
    assert(alpha_at(10, 20) > 115 && alpha_at(10, 20) < 140);  /* half-covered column */
}

static void test_opacity_and_rotation(void) {
    clear();
    pr_paint_t p = pr_solid(0xFF0000FF);
    pr_fill_ellipse(&cv, pr_identity(), U(32), U(32), U(10), U(10), 0, &p, 128);
    assert(alpha_at(32, 32) > 120 && alpha_at(32, 32) < 136);
    clear();
    pr_fill_ellipse(&cv, pr_identity(), U(32), U(32), U(20), U(6), 0, &p, 255);
    u64 a0 = alpha_sum();
    assert(alpha_at(50, 32) > 0 && alpha_at(32, 45) == 0);
    clear();
    pr_fill_ellipse(&cv, pr_identity(), U(32), U(32), U(20), U(6), 90, &p, 255);
    u64 a1 = alpha_sum();
    assert(alpha_at(32, 50) > 0 && alpha_at(50, 32) == 0);
    assert(a0 > a1 - a1 / 50 && a0 < a1 + a1 / 50);  /* same area */
}

static void test_strokes(void) {
    clear();
    pr_paint_t p = pr_solid(0xFFFFFFFF);
    assert(pr_stroke_path(&cv, pr_identity(), "M10 32 L50 32", U(4), &p, 255));
    assert(alpha_at(30, 31) == 255 && alpha_at(30, 32) == 255 && alpha_at(30, 35) == 0);
    u64 area = alpha_sum() / 255;                  /* 40*4 + pi*4 = 172.6 */
    assert(area > 160 && area < 185);
    clear();                                        /* zero-length segment = a dot */
    assert(pr_stroke_path(&cv, pr_identity(), "M20 20 L20 20", U(6), &p, 255));
    assert(alpha_at(20, 20) > 200);
    clear();                                        /* curves parse and draw */
    assert(pr_stroke_path(&cv, pr_identity(), "M10 40 Q32 10 54 40", U(3), &p, 255));
    assert(pr_fill_path(&cv, pr_identity(), "M80 36 C114 36 138 64 138 98 C138 128 114 144 80 144 Z", &p, 255));
    assert(alpha_sum() > 0);
    clear();
    pr_stroke_ellipse(&cv, pr_identity(), U(32), U(32), U(14), U(14), 0, U(3), &p, 255);
    assert(alpha_at(32, 32) == 0 && alpha_at(46, 32) > 200);
}

static void test_bad_input_and_clipping(void) {
    clear();
    pr_paint_t p = pr_solid(0xFF000000);
    assert(!pr_fill_path(&cv, pr_identity(), "M10 x", &p, 255));
    assert(!pr_fill_path(&cv, pr_identity(), "", &p, 255));
    assert(!pr_stroke_path(&cv, pr_identity(), "L10", U(2), &p, 255) || 1);  /* no crash */
    pr_fill_ellipse(&cv, pr_identity(), U(-5), U(60), U(30), U(30), 0, &p, 255);  /* mostly off canvas */
    assert(alpha_at(0, 63) == 255);
    pr_fill_ellipse(&cv, pr_scale(0, 0), U(32), U(32), U(10), U(10), 0, &p, 255);  /* singular matrix */
    pr_paint_t g = { PR_RADIAL, 0, 32768, 32768, 32768, { { 0, 0xFFFFFFFF }, { 65535, 0xFF000000 } }, 2 };
    pr_fill_ellipse(&cv, pr_scale(0, 0), U(32), U(32), U(10), U(10), 0, &g, 255);
    pr_fill_rrect(&cv, pr_identity(), U(70), U(70), U(10), U(10), U(3), &p, 255);  /* fully off */
}

static void test_radial_gradient(void) {
    clear();
    pr_paint_t g = { PR_RADIAL, 0, 32768, 32768, 32768, { { 0, 0xFFFFFFFF }, { 65535, 0xFF000000 } }, 2 };
    pr_fill_ellipse(&cv, pr_identity(), U(32), U(32), U(20), U(20), 0, &g, 255);
    pr_unpremultiply(&cv);
    u32 c = px[32 * W + 32], e = px[32 * W + 50];
    assert(((c >> 16) & 255) > 230);                 /* centre is the first stop */
    assert(((e >> 16) & 255) < 60);                  /* near the edge, the last */
}

int main(void) {
    test_trig_and_matrices();
    test_circle_fill();
    test_square_exact_and_half();
    test_opacity_and_rotation();
    test_strokes();
    test_bad_input_and_clipping();
    test_radial_gradient();
    puts("pip_raster OK");
    return 0;
}
