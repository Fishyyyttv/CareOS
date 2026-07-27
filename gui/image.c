/* =============================================================================
 * CareOS gui/image.c  --  image lifetime, the .cri decoder and the blitter
 *
 * The blitter is the only place in CareOS that knows about alpha. gfx.c blends
 * with a single alpha for a whole rectangle (gfx_rect_blend); an image carries
 * one per pixel, so it gets its own inner loops rather than 2304 calls to
 * gfx_setpixel_blend() per 48px icon.
 *
 * Every draw path funnels through blit_begin(), which resolves g_target and the
 * active clip rect ONCE into a bounds box. After that the loops clamp against
 * plain integers instead of re-testing the clip per pixel, which is what makes
 * a full-screen wallpaper blit affordable at 1080p.
 * ============================================================================= */

#include "kernel.h"
#include "gui.h"
#include "image.h"

/* -- Destination binding ---------------------------------------------------
 * cx0/cy0/cx1/cy1 is the half-open intersection of the target surface and the
 * active clip rect, in target coordinates. Empty => nothing to draw. */
typedef struct {
    u32 *px;
    i32  stride;                 /* pixels per row, not bytes */
    i32  cx0, cy0, cx1, cy1;
} blit_dst_t;

static bool blit_begin(blit_dst_t *d) {
    if (!g_target || !g_target->pixels) return false;

    d->px     = g_target->pixels;
    d->stride = (i32)(g_target->pitch / 4);
    d->cx0    = 0;
    d->cy0    = 0;
    d->cx1    = (i32)g_target->w;
    d->cy1    = (i32)g_target->h;

    rect_t clip;
    if (gfx_get_clip(&clip)) {
        if (clip.x > d->cx0) d->cx0 = clip.x;
        if (clip.y > d->cy0) d->cy0 = clip.y;
        if (clip.x + clip.w < d->cx1) d->cx1 = clip.x + clip.w;
        if (clip.y + clip.h < d->cy1) d->cy1 = clip.y + clip.h;
    }
    return d->cx0 < d->cx1 && d->cy0 < d->cy1;
}

/* Exact (v*a)/255 with two shifts and two adds -- no divide in the inner loop.
 * t is at most 255*255, so the (t + (t>>8)) >> 8 fixup rounds correctly. */
static inline u32 blend_over(u32 dst, u32 src, u32 a) {
    u32 ia = 255u - a;
    u32 r  = ((src >> 16) & 0xFFu) * a + ((dst >> 16) & 0xFFu) * ia + 128u;
    u32 g  = ((src >>  8) & 0xFFu) * a + ((dst >>  8) & 0xFFu) * ia + 128u;
    u32 b  = ( src        & 0xFFu) * a + ( dst        & 0xFFu) * ia + 128u;
    r = (r + (r >> 8)) >> 8;
    g = (g + (g >> 8)) >> 8;
    b = (b + (b >> 8)) >> 8;
    return (r << 16) | (g << 8) | b;
}

/* One source pixel onto one destination slot, honouring src alpha. */
static inline void put_argb(u32 *slot, u32 src) {
    u32 a = src >> 24;
    if (a == 0)    return;
    if (a == 255) { *slot = src & 0x00FFFFFFu; return; }
    *slot = blend_over(*slot, src, a);
}

/* -- Allocation ------------------------------------------------------------ */

static image_t *image_alloc_header(u32 w, u32 h, u32 format) {
    image_t *img = (image_t *)kmalloc(sizeof(image_t));
    if (!img) return NULL;
    kmemset(img, 0, sizeof(*img));
    img->width  = w;
    img->height = h;
    img->format = format;
    return img;
}

static bool image_dim_ok(u32 w, u32 h) {
    if (w == 0 || h == 0) return false;
    if (w > IMAGE_MAX_DIM || h > IMAGE_MAX_DIM) return false;
    return (u64)w * (u64)h <= (u64)IMAGE_MAX_PIXELS;
}

image_t *image_create(u32 w, u32 h) {
    if (!image_dim_ok(w, h)) return NULL;
    image_t *img = image_alloc_header(w, h, IMG_FMT_ARGB32);
    if (!img) return NULL;

    size_t bytes = (size_t)w * (size_t)h * 4u;
    img->pixels = (u32 *)kmalloc(bytes);
    if (!img->pixels) { kfree(img); return NULL; }
    kmemset(img->pixels, 0, bytes);
    img->flags = IMG_OWNS_PIXELS | IMG_HAS_ALPHA;
    return img;
}

void image_free(image_t *img) {
    if (!img) return;
    /* Cached images belong to resource_cache.c; releasing one here would leave
     * the cache holding a freed pointer. Callers use res_release() instead. */
    if (img->flags & IMG_CACHED) return;
    if ((img->flags & IMG_OWNS_PIXELS) && img->pixels) kfree(img->pixels);
    img->pixels = NULL;
    kfree(img);
}

u32 image_pixel(const image_t *img, u32 x, u32 y) {
    if (!img || !img->pixels || x >= img->width || y >= img->height) return 0;
    return img->pixels[y * img->width + x];
}

/* -- Alpha survey ----------------------------------------------------------
 * One pass over the decoded pixels that answers two questions at once.
 *
 * Is the image really translucent? If every alpha byte is 0xFF the blitter can
 * take the straight-copy path forever after, so it is worth finding out once.
 *
 * Is the alpha channel meaningful at all? Plenty of tools emit 32-bpp BMP and
 * TGA with the fourth byte left at zero. Honouring that literally renders the
 * whole image invisible, which is the single most common way an image loader
 * "silently does nothing". An all-zero alpha channel is therefore read as
 * "no alpha channel" and forced opaque. A genuinely all-transparent image is
 * indistinguishable from that and equally pointless to draw. */
void image_classify_alpha(image_t *img) {
    u32  n         = img->width * img->height;
    bool any_set   = false;
    bool any_clear = false;

    for (u32 i = 0; i < n; i++) {
        u32 a = img->pixels[i] >> 24;
        if (a != 0u)    any_set   = true;
        if (a != 0xFFu) any_clear = true;
        if (any_set && any_clear) break;
    }

    if (!any_set) {
        for (u32 i = 0; i < n; i++) img->pixels[i] |= 0xFF000000u;
        any_clear = false;
    }

    if (any_clear) {
        img->format = IMG_FMT_ARGB32;
        img->flags |= IMG_HAS_ALPHA;
    } else {
        img->format = IMG_FMT_XRGB32;
        img->flags &= ~IMG_HAS_ALPHA;
    }
}

/* -- .cri decoder ----------------------------------------------------------
 * The native format, and the only one that can be drawn without a decode pass:
 * its BGRA byte order read as a u32 on little-endian x86 is already 0xAARRGGBB.
 *
 * `borrow` is honoured only for the uncompressed encoding and only when the
 * payload happens to be 4-byte aligned. The archive baker aligns every entry,
 * so the icon theme always takes this path; a stray .cri written by hand may
 * not, and silently copying beats a misaligned u32 load. */
image_t *image_decode_cri(const u8 *data, u32 len, bool borrow) {
    if (!data || len < CRI_HEADER_BYTES) return NULL;
    if (data[0] != CRI_MAGIC0 || data[1] != CRI_MAGIC1 ||
        data[2] != CRI_MAGIC2 || data[3] != CRI_MAGIC3) return NULL;

    u32 w    = (u32)data[4] | ((u32)data[5] << 8);
    u32 h    = (u32)data[6] | ((u32)data[7] << 8);
    u32 enc  = data[8];
    u32 fl   = data[9];
    u32 plen = (u32)data[12] | ((u32)data[13] << 8) |
               ((u32)data[14] << 16) | ((u32)data[15] << 24);

    if (!image_dim_ok(w, h)) return NULL;
    if (plen > len - CRI_HEADER_BYTES) return NULL;

    const u8 *payload = data + CRI_HEADER_BYTES;
    u32 npx = w * h;
    u32 fmt = (fl & CRI_FLAG_HAS_ALPHA) ? IMG_FMT_ARGB32 : IMG_FMT_XRGB32;

    if (enc == CRI_ENC_BGRA32) {
        if (plen < npx * 4u) return NULL;

        image_t *img = image_alloc_header(w, h, fmt);
        if (!img) return NULL;
        if (fl & CRI_FLAG_HAS_ALPHA) img->flags |= IMG_HAS_ALPHA;

        if (borrow && (((uintptr_t)payload & 3u) == 0)) {
            /* Zero-copy: the image is a window onto the caller's bytes. */
            img->pixels = (u32 *)(void *)payload;
            return img;
        }
        img->pixels = (u32 *)kmalloc((size_t)npx * 4u);
        if (!img->pixels) { kfree(img); return NULL; }
        kmemcpy(img->pixels, payload, (size_t)npx * 4u);
        img->flags |= IMG_OWNS_PIXELS;
        return img;
    }

    if (enc == CRI_ENC_RLE_BGRA32) {
        image_t *img = image_alloc_header(w, h, fmt);
        if (!img) return NULL;
        if (fl & CRI_FLAG_HAS_ALPHA) img->flags |= IMG_HAS_ALPHA;
        img->pixels = (u32 *)kmalloc((size_t)npx * 4u);
        if (!img->pixels) { kfree(img); return NULL; }
        img->flags |= IMG_OWNS_PIXELS;

        u32 out = 0, in = 0;
        while (out < npx && in < plen) {
            u8 ctl = payload[in++];
            u32 run = (u32)(ctl & 0x7Fu) + 1u;
            if (run > npx - out) run = npx - out;

            if (ctl & 0x80u) {                       /* repeated quad */
                if (in + 4u > plen) break;
                u32 px = (u32)payload[in] | ((u32)payload[in+1] << 8) |
                         ((u32)payload[in+2] << 16) | ((u32)payload[in+3] << 24);
                in += 4u;
                for (u32 i = 0; i < run; i++) img->pixels[out++] = px;
            } else {                                 /* literal quads */
                if (in + run * 4u > plen) break;
                for (u32 i = 0; i < run; i++, in += 4u)
                    img->pixels[out++] = (u32)payload[in] | ((u32)payload[in+1] << 8) |
                                         ((u32)payload[in+2] << 16) | ((u32)payload[in+3] << 24);
            }
        }
        /* A truncated stream yields a partially decoded image rather than a
         * failure: a half-drawn icon is a better desktop than a missing one. */
        while (out < npx) img->pixels[out++] = 0;
        return img;
    }

    return NULL;   /* unknown encoding */
}

/* -- Container sniffing ---------------------------------------------------- */

image_t *image_load_mem(const u8 *data, u32 len, bool copy_always) {
    if (!data || len < 16) return NULL;

    image_t *img = image_decode_cri(data, len, !copy_always);
    if (img) return img;

    if (data[0] == 'B' && data[1] == 'M') {
        img = image_decode_bmp(data, len);
        if (img) return img;
    }

    /* TGA has no magic at offset 0. Try it last so a malformed BMP/CRI is
     * reported as such instead of being misread as a 2-byte TGA header. */
    return image_decode_tga(data, len);
}

image_t *image_load(const char *path) {
    if (!path || !path[0]) return NULL;

    fs_node_t *node = vfs_resolve_path(path);
    if (!node || node->type != FS_FILE || node->size == 0) return NULL;

    const u8 *bytes = (const u8 *)vfs_file_str(node);
    if (!bytes) return NULL;

    /* Borrowing is safe only against memory the VFS does not own -- today that
     * means the icon archive linked into the kernel image, which is immortal.
     * A heap-backed node can be rewritten or deleted under us, so copy those.
     * See gui/resource_boot.c for where the borrowed nodes come from. */
    bool copy_always = node->data_owned;

    image_t *img = image_load_mem(bytes, node->size, copy_always);
    if (!img) {
        serial_write("[image] decode failed: ");
        serial_write(path);
        serial_write("\n");
    }
    return img;
}

/* -- Scaling ---------------------------------------------------------------
 * Fixed-point 16.16 source stepping: one add per output pixel, no divide in
 * the loop, and it cannot drift off the last row the way an accumulating
 * float would. */
image_t *image_scaled(const image_t *src, u32 w, u32 h) {
    if (!src || !src->pixels || !image_dim_ok(w, h)) return NULL;
    if (w == src->width && h == src->height) {
        image_t *copy = image_create(w, h);
        if (!copy) return NULL;
        kmemcpy(copy->pixels, src->pixels, (size_t)w * h * 4u);
        copy->format = src->format;
        copy->flags  = IMG_OWNS_PIXELS | (src->flags & IMG_HAS_ALPHA);
        return copy;
    }

    image_t *dst = image_create(w, h);
    if (!dst) return NULL;

    u32 xstep = (src->width  << 16) / w;
    u32 ystep = (src->height << 16) / h;

    u32 sy = 0;
    for (u32 y = 0; y < h; y++, sy += ystep) {
        u32 row = sy >> 16;
        if (row >= src->height) row = src->height - 1;
        const u32 *srow = src->pixels + (size_t)row * src->width;
        u32       *drow = dst->pixels + (size_t)y * w;

        u32 sx = 0;
        for (u32 x = 0; x < w; x++, sx += xstep) {
            u32 col = sx >> 16;
            if (col >= src->width) col = src->width - 1;
            drow[x] = srow[col];
        }
    }
    dst->format = src->format;
    dst->flags  = IMG_OWNS_PIXELS | (src->flags & IMG_HAS_ALPHA);
    return dst;
}

/* -- Draw ------------------------------------------------------------------ */

void gfx_draw_image(image_t *img, i32 x, i32 y) {
    if (!img || !img->pixels) return;

    blit_dst_t d;
    if (!blit_begin(&d)) return;

    i32 x0 = x < d.cx0 ? d.cx0 : x;
    i32 y0 = y < d.cy0 ? d.cy0 : y;
    i32 x1 = x + (i32)img->width;
    i32 y1 = y + (i32)img->height;
    if (x1 > d.cx1) x1 = d.cx1;
    if (y1 > d.cy1) y1 = d.cy1;
    if (x0 >= x1 || y0 >= y1) return;

    bool opaque = (img->format == IMG_FMT_XRGB32) && !(img->flags & IMG_HAS_ALPHA);

    for (i32 dy = y0; dy < y1; dy++) {
        const u32 *srow = img->pixels + (size_t)(dy - y) * img->width;
        u32       *drow = d.px + (size_t)dy * d.stride;
        if (opaque) {
            for (i32 dx = x0; dx < x1; dx++)
                drow[dx] = srow[dx - x] & 0x00FFFFFFu;
        } else {
            for (i32 dx = x0; dx < x1; dx++)
                put_argb(&drow[dx], srow[dx - x]);
        }
    }
    gfx_dirty(x0, y0, x1 - x0, y1 - y0);
}

void gfx_draw_image_alpha(image_t *img, i32 x, i32 y, u8 opacity) {
    if (!img || !img->pixels) return;
    if (opacity == 0) return;
    if (opacity == 255) { gfx_draw_image(img, x, y); return; }

    blit_dst_t d;
    if (!blit_begin(&d)) return;

    i32 x0 = x < d.cx0 ? d.cx0 : x;
    i32 y0 = y < d.cy0 ? d.cy0 : y;
    i32 x1 = x + (i32)img->width;
    i32 y1 = y + (i32)img->height;
    if (x1 > d.cx1) x1 = d.cx1;
    if (y1 > d.cy1) y1 = d.cy1;
    if (x0 >= x1 || y0 >= y1) return;

    u32 gop = (u32)opacity;
    for (i32 dy = y0; dy < y1; dy++) {
        const u32 *srow = img->pixels + (size_t)(dy - y) * img->width;
        u32       *drow = d.px + (size_t)dy * d.stride;
        for (i32 dx = x0; dx < x1; dx++) {
            u32 src = srow[dx - x];
            /* (alpha * opacity) / 255, same divide-free rounding as blend_over.
             * A real divide here would be the only one in any inner loop in
             * this file. */
            u32 t = (src >> 24) * gop + 128u;
            u32 a = (t + (t >> 8)) >> 8;
            if (a == 0) continue;
            drow[dx] = blend_over(drow[dx], src, a);
        }
    }
    gfx_dirty(x0, y0, x1 - x0, y1 - y0);
}

void gfx_draw_image_tinted(image_t *img, i32 x, i32 y, u32 tint) {
    if (!img || !img->pixels) return;

    blit_dst_t d;
    if (!blit_begin(&d)) return;

    i32 x0 = x < d.cx0 ? d.cx0 : x;
    i32 y0 = y < d.cy0 ? d.cy0 : y;
    i32 x1 = x + (i32)img->width;
    i32 y1 = y + (i32)img->height;
    if (x1 > d.cx1) x1 = d.cx1;
    if (y1 > d.cy1) y1 = d.cy1;
    if (x0 >= x1 || y0 >= y1) return;

    for (i32 dy = y0; dy < y1; dy++) {
        const u32 *srow = img->pixels + (size_t)(dy - y) * img->width;
        u32       *drow = d.px + (size_t)dy * d.stride;
        for (i32 dx = x0; dx < x1; dx++) {
            u32 a = srow[dx - x] >> 24;
            if (a == 0)    continue;
            if (a == 255) { drow[dx] = tint & 0x00FFFFFFu; continue; }
            drow[dx] = blend_over(drow[dx], tint, a);
        }
    }
    gfx_dirty(x0, y0, x1 - x0, y1 - y0);
}

void gfx_draw_image_scaled(image_t *img, i32 x, i32 y, i32 w, i32 h) {
    if (!img || !img->pixels || w <= 0 || h <= 0) return;

    blit_dst_t d;
    if (!blit_begin(&d)) return;

    i32 x0 = x < d.cx0 ? d.cx0 : x;
    i32 y0 = y < d.cy0 ? d.cy0 : y;
    i32 x1 = x + w, y1 = y + h;
    if (x1 > d.cx1) x1 = d.cx1;
    if (y1 > d.cy1) y1 = d.cy1;
    if (x0 >= x1 || y0 >= y1) return;

    u32 xstep = (img->width  << 16) / (u32)w;
    u32 ystep = (img->height << 16) / (u32)h;

    for (i32 dy = y0; dy < y1; dy++) {
        u32 row = ((u32)(dy - y) * ystep) >> 16;
        if (row >= img->height) row = img->height - 1;
        const u32 *srow = img->pixels + (size_t)row * img->width;
        u32       *drow = d.px + (size_t)dy * d.stride;

        for (i32 dx = x0; dx < x1; dx++) {
            u32 col = ((u32)(dx - x) * xstep) >> 16;
            if (col >= img->width) col = img->width - 1;
            put_argb(&drow[dx], srow[col]);
        }
    }
    gfx_dirty(x0, y0, x1 - x0, y1 - y0);
}

void gfx_draw_image_region(image_t *img, i32 sx, i32 sy, i32 sw, i32 sh,
                           i32 dx, i32 dy) {
    if (!img || !img->pixels || sw <= 0 || sh <= 0) return;

    /* Clamp the source window first so the destination maths below can assume
     * every sampled row and column exists. */
    if (sx < 0) { sw += sx; dx -= sx; sx = 0; }
    if (sy < 0) { sh += sy; dy -= sy; sy = 0; }
    if (sx + sw > (i32)img->width)  sw = (i32)img->width  - sx;
    if (sy + sh > (i32)img->height) sh = (i32)img->height - sy;
    if (sw <= 0 || sh <= 0) return;

    blit_dst_t d;
    if (!blit_begin(&d)) return;

    i32 x0 = dx < d.cx0 ? d.cx0 : dx;
    i32 y0 = dy < d.cy0 ? d.cy0 : dy;
    i32 x1 = dx + sw, y1 = dy + sh;
    if (x1 > d.cx1) x1 = d.cx1;
    if (y1 > d.cy1) y1 = d.cy1;
    if (x0 >= x1 || y0 >= y1) return;

    for (i32 py = y0; py < y1; py++) {
        const u32 *srow = img->pixels + (size_t)(sy + (py - dy)) * img->width + sx;
        u32       *drow = d.px + (size_t)py * d.stride;
        for (i32 px = x0; px < x1; px++)
            put_argb(&drow[px], srow[px - dx]);
    }
    gfx_dirty(x0, y0, x1 - x0, y1 - y0);
}

void gfx_draw_image_cover(image_t *img, i32 x, i32 y, i32 w, i32 h) {
    if (!img || !img->pixels || w <= 0 || h <= 0) return;

    /* Pick the axis that has to stretch further, scale both by it, then centre
     * the overflow -- CSS background-size: cover, in integer maths.
     *
     * The multiply back out MUST be 64-bit. Upscaling a small image to a large
     * rectangle produces a big 16.16 scale (a 100px image covering 1920px gives
     * ~1.26e9), and width * scale then overflows a u32 by two orders of
     * magnitude -- which showed up as a wallpaper drawn at a garbage size
     * rather than as anything that looked like an arithmetic bug. */
    u32 sx_scale = ((u32)w << 16) / img->width;
    u32 sy_scale = ((u32)h << 16) / img->height;
    u32 scale    = sx_scale > sy_scale ? sx_scale : sy_scale;
    if (scale == 0) scale = 1;

    i32 out_w = (i32)(((u64)img->width  * scale) >> 16);
    i32 out_h = (i32)(((u64)img->height * scale) >> 16);
    if (out_w < w) out_w = w;
    if (out_h < h) out_h = h;

    /* Intersect with whatever clip the caller already had rather than replacing
     * it -- a wallpaper drawn inside a settings preview pane must not escape
     * that pane just because it wants to crop itself. */
    rect_t saved;
    bool   had = gfx_get_clip(&saved);
    i32 cx0 = x, cy0 = y, cx1 = x + w, cy1 = y + h;
    if (had) {
        if (saved.x > cx0) cx0 = saved.x;
        if (saved.y > cy0) cy0 = saved.y;
        if (saved.x + saved.w < cx1) cx1 = saved.x + saved.w;
        if (saved.y + saved.h < cy1) cy1 = saved.y + saved.h;
    }
    if (cx0 < cx1 && cy0 < cy1) {
        gfx_set_clip(cx0, cy0, cx1 - cx0, cy1 - cy0);
        gfx_draw_image_scaled(img, x - (out_w - w) / 2, y - (out_h - h) / 2, out_w, out_h);
    }
    if (had) gfx_set_clip(saved.x, saved.y, saved.w, saved.h);
    else     gfx_clear_clip();
}
