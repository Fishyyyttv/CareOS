#include "pure.h"
#include "glass.h"
#include <assert.h>

#define W 120
#define H 40

static u32 src[W * H], dst[W * H];

int main(void) {
    /* sized for the largest case below (127 x 37) */
    u32 *scratch = malloc(glass_scratch_words(127, H) * 4);

    /* A flat colour stays exactly that colour. */
    for (u32 i = 0; i < W * H; i++) src[i] = 0x336699;
    glass_blur(src, W, W, H, 24, dst, scratch);
    for (u32 i = 0; i < W * H; i++) assert(dst[i] == 0x336699);

    /* A black|white step turns into a monotonic ramp; far edges keep their side. */
    for (u32 y = 0; y < H; y++)
        for (u32 x = 0; x < W; x++) src[y * W + x] = x < W / 2 ? 0x000000 : 0xFFFFFF;
    glass_blur(src, W, W, H, 24, dst, scratch);
    for (u32 y = 0; y < H; y++)
        for (u32 x = 1; x < W; x++) assert((dst[y * W + x] & 0xFF) >= (dst[y * W + x - 1] & 0xFF));
    assert((dst[20 * W + 0] & 0xFF) < 40);
    assert((dst[20 * W + W - 1] & 0xFF) > 215);
    u32 mid = dst[20 * W + W / 2] & 0xFF;
    assert(mid > 90 && mid < 170);

    /* Odd sizes and a wider source stride. */
    static u32 big[131 * 37];
    for (u32 i = 0; i < 131 * 37; i++) big[i] = 0x808080;
    glass_blur(big, 131, 127, 37, 30, dst, scratch);
    assert(dst[126 + 36 * 127] == 0x808080);

    /* Signature: stable for the same pixels, changes when a sampled one does. */
    u32 a = glass_signature(src, W, W, H);
    assert(a == glass_signature(src, W, W, H));
    src[3 * W + 8] ^= 0x010101;              /* row 3, column 8: sampled */
    assert(a != glass_signature(src, W, W, H));
    src[3 * W + 8] ^= 0x010101;
    src[(H - 1) * W + 30] ^= 0x000100;       /* last row: always sampled */
    assert(a != glass_signature(src, W, W, H));

    /* Guard: three slow frames switch to tint-only; 1 s of fast frames restores. */
    glass_guard_t g = {0};
    glass_guard_frame(&g, 40, 0);
    glass_guard_frame(&g, 40, 40);
    assert(!g.tint_only);
    glass_guard_frame(&g, 40, 80);
    assert(g.tint_only);
    glass_guard_frame(&g, 15, 500);
    glass_guard_frame(&g, 25, 900);           /* a 25 ms frame restarts the calm period */
    glass_guard_frame(&g, 15, 1800);
    assert(g.tint_only);
    glass_guard_frame(&g, 15, 1950);
    assert(!g.tint_only);
    /* A single slow frame between fast ones never trips it. */
    glass_guard_frame(&g, 40, 2000);
    glass_guard_frame(&g, 10, 2020);
    glass_guard_frame(&g, 40, 2060);
    glass_guard_frame(&g, 40, 2100);
    assert(!g.tint_only);

    free(scratch);
    puts("glass OK");
    return 0;
}
