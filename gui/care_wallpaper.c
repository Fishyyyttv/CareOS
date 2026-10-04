/* gui/care_wallpaper.c -- procedural "Care Night" / "Care Day" backgrounds.
 *
 * The Desktop artboard (1440x900) paints the ground colour, three CSS
 * `filter: blur()` discs and a 90 px ring at 6 % opacity. A Gaussian-blurred
 * disc of radius R and std-dev S has, to a very good approximation for R >> S,
 * the profile Phi((R - d) / S), so each blob is one lookup per pixel:
 * d^2 -> coverage through a per-blob table, which keeps sqrt out of the loop.
 * Channels accumulate at 8.8 fixed point and an ordered dither on the way out
 * stops the long dark gradients from banding. */
#include "care_wallpaper.h"

#define ART_W 1440
#define ART_H 900
#define MAX_W 4096
#define LUT_N 2048

/* 65535 * Phi(t) for t = -3 .. +3 in 257 steps. */
static const u16 PHI[257] = {
88, 96, 103, 111, 120, 129, 139, 150, 161, 173, 186, 200, 215, 230, 247, 265,
284, 304, 326, 348, 372, 398, 425, 454, 485, 517, 551, 587, 626, 666, 709, 754,
801, 851, 904, 959, 1018, 1079, 1144, 1212, 1283, 1358, 1437, 1519, 1605, 1695, 1790, 1889,
1992, 2100, 2213, 2331, 2453, 2581, 2715, 2854, 2998, 3149, 3305, 3468, 3637, 3812, 3994, 4183,
4378, 4581, 4790, 5007, 5231, 5463, 5702, 5950, 6205, 6468, 6739, 7018, 7305, 7601, 7905, 8218,
8539, 8869, 9207, 9554, 9910, 10274, 10647, 11029, 11420, 11819, 12227, 12643, 13068, 13501, 13943, 14394,
14852, 15319, 15793, 16276, 16766, 17264, 17769, 18282, 18801, 19328, 19861, 20401, 20947, 21499, 22056, 22620,
23188, 23762, 24340, 24923, 25510, 26101, 26695, 27293, 27894, 28497, 29103, 29711, 30320, 30931, 31542, 32155,
32768, 33380, 33993, 34604, 35215, 35824, 36432, 37038, 37641, 38242, 38840, 39434, 40025, 40612, 41195, 41773,
42347, 42915, 43479, 44036, 44588, 45134, 45674, 46207, 46734, 47253, 47766, 48271, 48769, 49259, 49742, 50216,
50683, 51141, 51592, 52034, 52467, 52892, 53308, 53716, 54115, 54506, 54888, 55261, 55625, 55981, 56328, 56666,
56996, 57317, 57630, 57934, 58230, 58517, 58796, 59067, 59330, 59585, 59833, 60072, 60304, 60528, 60745, 60954,
61157, 61352, 61541, 61723, 61898, 62067, 62230, 62386, 62537, 62681, 62820, 62954, 63082, 63204, 63322, 63435,
63543, 63646, 63745, 63840, 63930, 64016, 64098, 64177, 64252, 64323, 64391, 64456, 64517, 64576, 64631, 64684,
64734, 64781, 64826, 64869, 64909, 64948, 64984, 65018, 65050, 65081, 65110, 65137, 65163, 65187, 65209, 65231,
65251, 65270, 65288, 65305, 65320, 65335, 65349, 65362, 65374, 65385, 65396, 65406, 65415, 65424, 65432, 65439,
65447
};

/* Artboard geometry: centre, radius and blur in 1440x900 units; alpha 0..65535. */
typedef struct { i32 cx, cy, r, blur; u32 color; u32 alpha; } blob_t;

static const blob_t NIGHT[] = {
    {  250, 670, 450,  90, 0x2C3FA8, 36044 },   /* .55 */
    { 1250, 130, 310, 100, 0xFF7A59, 19661 },   /* .30 */
    {  940, 920, 260,  90, 0x7B61FF, 22938 },   /* .35 */
};
static const blob_t DAY[] = {
    {  210, 710, 450,  90, 0xC9D2FF, 65535 },
    { 1280, 120, 320,  90, 0xFFD0C0, 65535 },
};
/* The ring is a 1200 px content-box circle with a 90 px border at (420, 120):
 * centre (1110, 810), radii 600..690, 6 % opacity. */
#define RING_CX 1110
#define RING_CY 810
#define RING_R0 600
#define RING_R1 690
#define RING_ALPHA 3932

static const u8 BAYER[4][4] = {
    {   8, 136,  40, 168 }, { 200,  72, 232, 104 },
    {  56, 184,  24, 152 }, { 248, 120, 216,  88 },
};

static u64 isqrt64(u64 v) {
    u64 r = 0, bit = 1ull << 62;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    return r;
}

static u32 acc_r[MAX_W], acc_g[MAX_W], acc_b[MAX_W];

static inline void mix_px(u32 x, u32 color, u32 a16) {
    i64 a = (i64)a16;
    acc_r[x] = (u32)((i64)acc_r[x] + ((((i64)((color >> 16) & 0xFF) << 8) - (i64)acc_r[x]) * a >> 16));
    acc_g[x] = (u32)((i64)acc_g[x] + ((((i64)((color >>  8) & 0xFF) << 8) - (i64)acc_g[x]) * a >> 16));
    acc_b[x] = (u32)((i64)acc_b[x] + ((((i64)( color        & 0xFF) << 8) - (i64)acc_b[x]) * a >> 16));
}

typedef struct {
    i32 cx, cy;
    u32 in2, out2, shift;   /* coverage is 1 below in2, 0 above out2 */
    u32 color, alpha;
    u16 table[LUT_N + 1];
} blob_px_t;

/* Scaled blob plus its d^2 -> coverage table. */
static void blob_prepare(blob_px_t *b, const blob_t *src, u32 sx, u32 sy, u32 sr) {
    b->cx = (i32)(((i64)src->cx * sx) >> 16);
    b->cy = (i32)(((i64)src->cy * sy) >> 16);
    i32 R = (i32)(((i64)src->r * sr) >> 16);
    i32 S = (i32)(((i64)src->blur * sr) >> 16);
    if (S < 1) S = 1;
    i32 in = R - 3 * S, out = R + 3 * S;
    if (in < 0) in = 0;
    b->in2 = (u32)(in * in);
    b->out2 = (u32)(out * out);
    b->color = src->color;
    b->alpha = src->alpha;
    u32 span = b->out2 - b->in2, shift = 0;
    while ((span >> shift) > LUT_N) shift++;
    b->shift = shift;
    for (u32 k = 0; k <= LUT_N; k++) {
        u32 d2 = b->in2 + (k << shift);
        /* d in 1/16 px; t index = 128 + (R - d) * 128 / (3S), clamped */
        i64 d16 = (i64)isqrt64((u64)d2 << 8);
        i64 idx = 128 + (((i64)R * 16 - d16) * 128) / ((i64)3 * S * 16);
        if (idx < 0) idx = 0;
        if (idx > 256) idx = 256;
        b->table[k] = PHI[idx];
    }
}

static blob_px_t g_blobs[3];

void care_wallpaper_render(u32 *dst, u32 w, u32 h, u32 stride, bool dark) {
    if (!dst || w == 0 || h == 0) return;
    if (w > MAX_W) w = MAX_W;
    u32 sx = (u32)(((u64)w << 16) / ART_W);
    u32 sy = (u32)(((u64)h << 16) / ART_H);
    u32 sr = (sx + sy) / 2;

    const blob_t *set = dark ? NIGHT : DAY;
    u32 nb = dark ? 3u : 2u;
    for (u32 i = 0; i < nb; i++) blob_prepare(&g_blobs[i], &set[i], sx, sy, sr);

    u32 ground = dark ? 0x0B1020u : 0xE9ECF7u;
    u32 ring_col = dark ? 0x8C9BFFu : 0x4F5EF2u;
    i32 rcx = (i32)(((i64)RING_CX * sx) >> 16), rcy = (i32)(((i64)RING_CY * sy) >> 16);
    i32 r0 = (i32)(((i64)RING_R0 * sr) >> 16), r1 = (i32)(((i64)RING_R1 * sr) >> 16);
    u32 r0lo = (u32)((r0 - 1) * (r0 - 1)), r0hi = (u32)((r0 + 1) * (r0 + 1));
    u32 r1lo = (u32)((r1 - 1) * (r1 - 1)), r1hi = (u32)((r1 + 1) * (r1 + 1));

    for (u32 y = 0; y < h; y++) {
        for (u32 x = 0; x < w; x++) {
            acc_r[x] = ((ground >> 16) & 0xFF) << 8;
            acc_g[x] = ((ground >>  8) & 0xFF) << 8;
            acc_b[x] = ( ground        & 0xFF) << 8;
        }

        for (u32 i = 0; i < nb; i++) {
            const blob_px_t *b = &g_blobs[i];
            i32 dy = (i32)y - b->cy;
            u32 dy2 = (u32)(dy * dy);
            if (dy2 >= b->out2) continue;
            i32 half = (i32)isqrt64(b->out2 - dy2) + 1;
            i32 x0 = b->cx - half, x1 = b->cx + half;
            if (x0 < 0) x0 = 0;
            if (x1 > (i32)w) x1 = (i32)w;
            for (i32 x = x0; x < x1; x++) {
                i32 dx = x - b->cx;
                u32 d2 = (u32)(dx * dx) + dy2;
                u32 cov;
                if (d2 <= b->in2) cov = 65535;
                else if (d2 >= b->out2) continue;
                else cov = b->table[(d2 - b->in2) >> b->shift];
                mix_px((u32)x, b->color, (b->alpha * cov) >> 16);
            }
        }

        /* Ring: flat inside the band, 1 px anti-aliased edges. */
        i32 dy = (i32)y - rcy;
        u32 dy2 = (u32)(dy * dy);
        if (dy2 < r1hi) {
            i32 half = (i32)isqrt64(r1hi - dy2) + 1;
            i32 x0 = rcx - half, x1 = rcx + half;
            if (x0 < 0) x0 = 0;
            if (x1 > (i32)w) x1 = (i32)w;
            for (i32 x = x0; x < x1; x++) {
                i32 dx = x - rcx;
                u32 d2 = (u32)(dx * dx) + dy2;
                if (d2 < r0lo || d2 >= r1hi) continue;
                u32 a = RING_ALPHA;
                if (d2 < r0hi || d2 >= r1lo) {
                    /* edge: coverage from the distance in 1/256 px */
                    i32 d256 = (i32)isqrt64((u64)d2 << 16);
                    i32 c0 = d256 - (r0 * 256 - 128);     /* >0 past the inner edge */
                    i32 c1 = (r1 * 256 + 128) - d256;     /* >0 before the outer edge */
                    i32 c = c0 < c1 ? c0 : c1;
                    if (c <= 0) continue;
                    if (c < 256) a = (u32)(RING_ALPHA * (u32)c) >> 8;
                }
                mix_px((u32)x, ring_col, a);
            }
        }

        u32 *row = dst + (u64)y * stride;
        const u8 *bay = BAYER[y & 3];
        for (u32 x = 0; x < w; x++) {
            u32 t = bay[x & 3];
            u32 r = (acc_r[x] + t) >> 8, g = (acc_g[x] + t) >> 8, b = (acc_b[x] + t) >> 8;
            if (r > 255) r = 255;
            if (g > 255) g = 255;
            if (b > 255) b = 255;
            row[x] = (r << 16) | (g << 8) | b;
        }
    }
}
