/* web/engine/image.c -- image requests, decoding (stb_image) and scaling. */
#include "engine.h"

#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_PSD
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_NO_TGA
#define STBI_ASSERT(x) ((void)0)
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include "../third_party/stb_image.h"

#define IMG_MAX_PIXELS (4096 * 4096)
#define IMG_MAX_COUNT  160

web_img_t *img_request(page_t *pg, const char *url) {
    if (!url || !*url) return NULL;
    int n = 0;
    for (web_img_t *im = pg->imgs; im; im = im->next, n++)
        if (strcmp(im->url, url) == 0) return im;
    if (n >= IMG_MAX_COUNT) return NULL;
    /* formats we cannot decode */
    size_t ul = strlen(url);
    const char *q = strchr(url, '?');
    size_t pl = q ? (size_t)(q - url) : ul;
    if ((pl > 4 && w_ieqn(url + pl - 4, 4, ".svg")) || strncmp(url, "data:image/svg", 14) == 0) return NULL;
    web_img_t *im = (web_img_t *)calloc(1, sizeof(web_img_t));
    if (!im) return NULL;
    im->url = w_strdup(url);
    if (!im->url) { free(im); return NULL; }
    /* append (keeps document order for loading) */
    web_img_t **pp = &pg->imgs;
    while (*pp) pp = &(*pp)->next;
    *pp = im;
    pg->nimg_pending++;
    return im;
}

bool img_load(page_t *pg, web_img_t *im) {
    (void)pg;
    if (im->state != 0) return im->state == 1;
    size_t len = 0;
    unsigned char *data = NULL;
    if (strncmp(im->url, "data:", 5) == 0) data = data_url_decode(im->url, &len);
    else if (g_web.fetch) data = g_web.fetch(g_web.ctx, im->url, &len, NULL, 0);
    if (!data || len == 0) { free(data); im->state = 2; return false; }
    int w, h, comp;
    if (!stbi_info_from_memory(data, (int)len, &w, &h, &comp) || (size_t)w * h > IMG_MAX_PIXELS) {
        free(data); im->state = 2; return false;
    }
    unsigned char *rgba = stbi_load_from_memory(data, (int)len, &w, &h, &comp, 4);
    free(data);
    if (!rgba) { im->state = 2; return false; }
    u32 *px = (u32 *)malloc((size_t)w * h * 4);
    if (!px) { stbi_image_free(rgba); im->state = 2; return false; }
    for (size_t i = 0, n = (size_t)w * h; i < n; i++) {
        const unsigned char *p = rgba + i * 4;
        px[i] = ((u32)p[3] << 24) | ((u32)p[0] << 16) | ((u32)p[1] << 8) | p[2];
    }
    stbi_image_free(rgba);
    im->w = w; im->h = h; im->px = px;
    __sync_synchronize();
    im->state = 1;
    return true;
}

/* Area-averaging (down) / bilinear-ish nearest (up) scale, cached. */
const u32 *img_scaled(web_img_t *im, int w, int h) {
    if (im->state != 1 || w <= 0 || h <= 0) return NULL;
    if (w == im->w && h == im->h) return im->px;
    if (im->spx && im->sw == w && im->sh == h) return im->spx;
    u32 *out = (u32 *)malloc((size_t)w * h * 4);
    if (!out) return NULL;
    float sx = (float)im->w / w, sy = (float)im->h / h;
    for (int y = 0; y < h; y++) {
        float fy0 = y * sy, fy1 = fy0 + sy;
        int y0 = (int)fy0, y1 = (int)ceilf(fy1);
        if (y1 > im->h) y1 = im->h;
        if (y1 <= y0) y1 = y0 + 1;
        if (y0 >= im->h) y0 = im->h - 1;
        for (int x = 0; x < w; x++) {
            float fx0 = x * sx, fx1 = fx0 + sx;
            int x0 = (int)fx0, x1 = (int)ceilf(fx1);
            if (x1 > im->w) x1 = im->w;
            if (x1 <= x0) x1 = x0 + 1;
            if (x0 >= im->w) x0 = im->w - 1;
            if (sx <= 1 && sy <= 1) { out[(size_t)y * w + x] = im->px[(size_t)y0 * im->w + x0]; continue; }
            /* average (premultiplied) over the source rect, sampling at most 4x4 */
            int stepx = (x1 - x0 + 3) / 4, stepy = (y1 - y0 + 3) / 4;
            if (stepx < 1) stepx = 1;
            if (stepy < 1) stepy = 1;
            u32 a = 0, r = 0, g = 0, bsum = 0, n = 0;
            for (int yy = y0; yy < y1; yy += stepy)
                for (int xx = x0; xx < x1; xx += stepx) {
                    u32 p = im->px[(size_t)yy * im->w + xx];
                    u32 pa = p >> 24;
                    a += pa; r += ((p >> 16) & 0xFF) * pa; g += ((p >> 8) & 0xFF) * pa; bsum += (p & 0xFF) * pa; n++;
                }
            u32 oa = n ? a / n : 0;
            u32 orr = a ? r / a : 0, og = a ? g / a : 0, ob = a ? bsum / a : 0;
            out[(size_t)y * w + x] = (oa << 24) | (orr << 16) | (og << 8) | ob;
        }
    }
    free(im->spx);
    im->spx = out; im->sw = w; im->sh = h;
    return out;
}

void img_free_all(page_t *pg) {
    web_img_t *im = pg->imgs;
    while (im) {
        web_img_t *nx = im->next;
        free(im->url); free(im->px); free(im->spx); free(im);
        im = nx;
    }
    pg->imgs = NULL;
}
