#include "pure.h"
#include "care_wallpaper.h"
#include <assert.h>

static u32 *buf;

static int ch(u32 px, int shift) { return (int)((px >> shift) & 0xFF); }

static void near_rgb(u32 px, int r, int g, int b, int tol) {
    int dr = ch(px, 16) - r, dg = ch(px, 8) - g, db = ch(px, 0) - b;
    if (dr < -tol || dr > tol || dg < -tol || dg > tol || db < -tol || db > tol) {
        fprintf(stderr, "got %06x want %02x%02x%02x\n", px, r, g, b);
        assert(0);
    }
}

static void dump(const char *path, u32 w, u32 h) {
    const char *dir = getenv("CARE_WALLPAPER_PPM");
    if (!dir) return;
    char name[512];
    snprintf(name, sizeof(name), "%s/%s", dir, path);
    FILE *f = fopen(name, "wb");
    if (!f) return;
    fprintf(f, "P6\n%u %u\n255\n", w, h);
    for (u32 i = 0; i < w * h; i++) {
        unsigned char c[3] = { (unsigned char)ch(buf[i], 16), (unsigned char)ch(buf[i], 8), (unsigned char)ch(buf[i], 0) };
        fwrite(c, 1, 3, f);
    }
    fclose(f);
}

int main(void) {
    u32 w = 1440, h = 900;
    buf = malloc((size_t)w * h * 4);

    care_wallpaper_render(buf, w, h, w, true);
    /* Blob 1 centre: ground 0B1020 under #2C3FA8 at 55 %. */
    near_rgb(buf[670 * w + 250], 0x1D, 0x2A, 0x6B, 2);
    /* Top-left corner is beyond every blob's 3-sigma reach: pure ground. */
    near_rgb(buf[0], 0x0B, 0x10, 0x20, 1);
    /* Inside the ring band (centre 1110,810; radius ~645) far from blobs:
     * ground + 6 % of #8C9BFF. */
    {
        /* At x=1110 the ring band spans y 120..210. Its 6 % #8C9BFF lifts blue
         * above the smooth blob gradient on either side of it. */
        int in = ch(buf[165 * w + 1110], 0);
        int above = ch(buf[100 * w + 1110], 0), below = ch(buf[230 * w + 1110], 0);
        assert(in >= (above + below) / 2 + 3);
    }
    dump("care-night.ppm", w, h);

    care_wallpaper_render(buf, w, h, w, false);
    near_rgb(buf[710 * w + 210], 0xC9, 0xD2, 0xFF, 2);
    near_rgb(buf[w - 1 + (h - 1) * w], 0xE9, 0xEC, 0xF7, 6);
    dump("care-day.ppm", w, h);

    /* Odd sizes and a stride wider than the width must not write past rows. */
    u32 sw = 1366, sh = 768, stride = 1400;
    u32 *big = malloc((size_t)stride * sh * 4);
    for (u32 i = 0; i < stride * sh; i++) big[i] = 0xDEADBEEF;
    care_wallpaper_render(big, sw, sh, stride, true);
    for (u32 y = 0; y < sh; y++)
        for (u32 x = sw; x < stride; x++) assert(big[y * stride + x] == 0xDEADBEEF);
    free(big);
    free(buf);
    puts("care_wallpaper OK");
    return 0;
}
