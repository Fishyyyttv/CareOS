/* =============================================================================
 * CareOS gui/image_tga.c  --  Truevision TGA decoder
 *
 * Supports image types 2 (uncompressed true-colour) and 10 (RLE true-colour) at
 * 24 and 32 bits per pixel. Paletted (types 1/9) and greyscale (3/11) are
 * rejected -- they exist for scanner output from 1989, not for icons.
 *
 * TGA has no magic number at offset 0, so image_load_mem() tries it LAST and
 * this decoder has to be strict: every reserved-ish field is validated, because
 * the alternative is happily "decoding" a truncated PNG into noise. The 18-byte
 * header is:
 *
 *   0  id_len      1
 *   1  cmap_type   1   must be 0 (no colour map)
 *   2  image_type  1   2 = raw BGR(A), 10 = RLE BGR(A)
 *   3  cmap_spec   5   must be all zero when cmap_type is 0
 *   8  x_origin    2
 *  10  y_origin    2
 *  12  width       2
 *  14  height      2
 *  16  bpp         1   24 or 32
 *  17  descriptor  1   bits 0-3 alpha depth, bit 5 = rows are top-down
 *
 * Rows are bottom-up unless descriptor bit 5 is set, matching BMP.
 * ============================================================================= */

#include "kernel.h"
#include "gui.h"
#include "image.h"

#define TGA_HEADER_BYTES  18u

#define TGA_TYPE_RAW      2u
#define TGA_TYPE_RLE     10u

#define TGA_DESC_TOPDOWN  0x20u

image_t *image_decode_tga(const u8 *data, u32 len) {
    if (!data || len <= TGA_HEADER_BYTES) return NULL;

    u32 id_len     = data[0];
    u32 cmap_type  = data[1];
    u32 image_type = data[2];
    u32 w          = (u32)data[12] | ((u32)data[13] << 8);
    u32 h          = (u32)data[14] | ((u32)data[15] << 8);
    u32 bpp        = data[16];
    u32 descriptor = data[17];

    if (cmap_type != 0) return NULL;
    if (image_type != TGA_TYPE_RAW && image_type != TGA_TYPE_RLE) return NULL;
    if (bpp != 24 && bpp != 32) return NULL;
    if (w == 0 || h == 0 || w > IMAGE_MAX_DIM || h > IMAGE_MAX_DIM) return NULL;
    if ((u64)w * (u64)h > (u64)IMAGE_MAX_PIXELS) return NULL;
    /* With no colour map the whole 5-byte spec must be zero. This is the check
     * that stops a random file from being mistaken for a TGA. */
    for (u32 i = 3; i < 8; i++) if (data[i] != 0) return NULL;
    if (descriptor & 0xC0u) return NULL;   /* reserved bits, never set in practice */

    u32 pixel_off = TGA_HEADER_BYTES + id_len;
    if (pixel_off >= len) return NULL;

    u32 src_bpp = bpp / 8u;
    u32 npx     = w * h;
    if (image_type == TGA_TYPE_RAW && (u64)npx * src_bpp > (u64)(len - pixel_off))
        return NULL;

    image_t *img = image_create(w, h);
    if (!img) return NULL;

    bool top_down = (descriptor & TGA_DESC_TOPDOWN) != 0;

    /* Decode into a flat top-down buffer indexed by the file's row order, then
     * flip on the way out. Doing it this way means the RLE reader never has to
     * care which direction the rows run -- runs are allowed to straddle row
     * boundaries, so it cannot decode row-at-a-time anyway. */
    u32 *flat = img->pixels;

    if (image_type == TGA_TYPE_RAW) {
        const u8 *p = data + pixel_off;
        for (u32 i = 0; i < npx; i++, p += src_bpp) {
            u32 a = (src_bpp == 4) ? (u32)p[3] : 0xFFu;
            flat[i] = (a << 24) | ((u32)p[2] << 16) | ((u32)p[1] << 8) | (u32)p[0];
        }
    } else {
        u32 in = pixel_off, out = 0;
        while (out < npx && in < len) {
            u8  ctl = data[in++];
            u32 run = (u32)(ctl & 0x7Fu) + 1u;
            if (run > npx - out) run = npx - out;

            if (ctl & 0x80u) {                       /* run-length packet */
                if (in + src_bpp > len) break;
                const u8 *p = data + in;
                u32 a  = (src_bpp == 4) ? (u32)p[3] : 0xFFu;
                u32 px = (a << 24) | ((u32)p[2] << 16) | ((u32)p[1] << 8) | (u32)p[0];
                in += src_bpp;
                for (u32 i = 0; i < run; i++) flat[out++] = px;
            } else {                                 /* raw packet */
                if (in + run * src_bpp > len) break;
                for (u32 i = 0; i < run; i++, in += src_bpp) {
                    const u8 *p = data + in;
                    u32 a = (src_bpp == 4) ? (u32)p[3] : 0xFFu;
                    flat[out++] = (a << 24) | ((u32)p[2] << 16) |
                                  ((u32)p[1] << 8) | (u32)p[0];
                }
            }
        }
        /* A stream that ends early leaves the tail transparent rather than
         * failing the whole load. */
        while (out < npx) flat[out++] = 0;
    }

    if (!top_down) {
        /* In-place vertical flip, swapping row pairs from the outside in. */
        for (u32 y = 0; y < h / 2u; y++) {
            u32 *a = flat + (size_t)y * w;
            u32 *b = flat + (size_t)(h - 1u - y) * w;
            for (u32 x = 0; x < w; x++) { u32 t = a[x]; a[x] = b[x]; b[x] = t; }
        }
    }

    image_classify_alpha(img);
    return img;
}
