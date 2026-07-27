/* =============================================================================
 * CareOS gui/image_bmp.c  --  Windows BMP decoder
 *
 * Handles what art tools actually export and nothing else:
 *
 *   - BITMAPINFOHEADER (40 bytes) and the V4/V5 extensions, which are just a
 *     longer header with the same first 40 bytes -- bfOffBits tells us where the
 *     pixels start, so a longer header costs us nothing.
 *   - 24-bpp BI_RGB and 32-bpp BI_RGB / BI_BITFIELDS.
 *   - Bottom-up (positive biHeight, the normal case) and top-down (negative).
 *
 * Deliberately NOT handled: RLE4/RLE8, 1/4/8-bpp palettes, 16-bpp, OS/2
 * BITMAPCOREHEADER. Each would be another table and another loop for formats
 * that no icon in this OS is going to arrive in. A rejected BMP falls through
 * to the caller's placeholder, which is the correct outcome for an image the
 * kernel cannot draw.
 *
 * Note on 32-bpp BI_RGB: the format says the fourth byte is reserved and must
 * be zero, yet almost everything writes real alpha there. We read it as alpha
 * and let image_classify_alpha() fix up the all-zero case.
 * ============================================================================= */

#include "kernel.h"
#include "gui.h"
#include "image.h"

#define BMP_FILE_HEADER   14u
#define BMP_INFO_MIN      40u

#define BI_RGB             0u
#define BI_BITFIELDS       3u

static inline u32 rd16(const u8 *p) { return (u32)p[0] | ((u32)p[1] << 8); }
static inline u32 rd32(const u8 *p) {
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

/* Bit position of the lowest set bit, and the mask's width. A channel mask from
 * BI_BITFIELDS is always contiguous in every file anyone has ever shipped, so a
 * shift plus a scale is enough to normalise it to 8 bits. */
static void mask_decompose(u32 mask, u32 *shift, u32 *scale_mul, u32 *scale_shift) {
    if (mask == 0) { *shift = 0; *scale_mul = 0; *scale_shift = 0; return; }
    u32 s = 0;
    while (((mask >> s) & 1u) == 0u) s++;
    u32 bits = 0, m = mask >> s;
    while (m & 1u) { bits++; m >>= 1; }

    *shift = s;
    /* Expand a `bits`-wide value to 8 bits: v * 255 / (2^bits - 1). Done as a
     * multiply and a shift so the inner loop stays divide-free. */
    if (bits >= 8) { *scale_mul = 1; *scale_shift = bits - 8; }
    else           { *scale_mul = 255u / ((1u << bits) - 1u); *scale_shift = 0; }
}

/* mask_decompose() guarantees mul >= 1 for any non-zero mask, so this is one
 * shift, one multiply and one shift regardless of the channel width. */
static inline u32 chan(u32 px, u32 mask, u32 shift, u32 mul, u32 sh) {
    if (mask == 0) return 0;
    return (((px & mask) >> shift) * mul >> sh) & 0xFFu;
}

image_t *image_decode_bmp(const u8 *data, u32 len) {
    if (!data || len < BMP_FILE_HEADER + BMP_INFO_MIN) return NULL;
    if (data[0] != 'B' || data[1] != 'M') return NULL;

    u32 off_bits  = rd32(data + 10);
    u32 hdr_size  = rd32(data + 14);
    if (hdr_size < BMP_INFO_MIN) return NULL;
    if (len < BMP_FILE_HEADER + hdr_size) return NULL;

    const u8 *ih = data + BMP_FILE_HEADER;
    i32 bw        = (i32)rd32(ih + 4);
    i32 bh        = (i32)rd32(ih + 8);
    u32 planes    = rd16(ih + 12);
    u32 bpp       = rd16(ih + 14);
    u32 compress  = rd32(ih + 16);

    if (planes != 1) return NULL;
    if (bpp != 24 && bpp != 32) return NULL;
    if (compress != BI_RGB && compress != BI_BITFIELDS) return NULL;
    if (compress == BI_BITFIELDS && bpp != 32) return NULL;

    /* Negative height means the rows are stored top-down. */
    bool top_down = bh < 0;
    if (top_down) bh = -bh;
    if (bw <= 0 || bh <= 0) return NULL;

    u32 w = (u32)bw, h = (u32)bh;
    if (w > IMAGE_MAX_DIM || h > IMAGE_MAX_DIM) return NULL;
    if ((u64)w * (u64)h > (u64)IMAGE_MAX_PIXELS) return NULL;

    /* Channel masks. BI_RGB at 32 bpp is BGRA; at 24 bpp it is BGR. */
    u32 rmask = 0x00FF0000u, gmask = 0x0000FF00u, bmask = 0x000000FFu, amask = 0xFF000000u;
    if (compress == BI_BITFIELDS) {
        /* Masks sit either in the V4/V5 header or in the three DWORDs that
         * follow a plain BITMAPINFOHEADER. */
        const u8 *m = (hdr_size >= 56u) ? ih + 40 : ih + hdr_size;
        if ((u32)(m - data) + 12u > len) return NULL;
        rmask = rd32(m + 0);
        gmask = rd32(m + 4);
        bmask = rd32(m + 8);
        amask = (hdr_size >= 56u) ? rd32(ih + 52) : 0u;
        if (rmask == 0 || gmask == 0 || bmask == 0) return NULL;
    }

    u32 rs, rm, rsh, gs, gm, gsh, bs, bm, bsh, as = 0, am = 0, ash = 0;
    mask_decompose(rmask, &rs, &rm, &rsh);
    mask_decompose(gmask, &gs, &gm, &gsh);
    mask_decompose(bmask, &bs, &bm, &bsh);
    if (amask) mask_decompose(amask, &as, &am, &ash);

    /* Rows are padded to a 4-byte boundary. */
    u32 src_bpp   = bpp / 8u;
    u32 row_bytes = ((w * src_bpp) + 3u) & ~3u;

    if (off_bits < BMP_FILE_HEADER + hdr_size) off_bits = BMP_FILE_HEADER + hdr_size;
    if (off_bits >= len) return NULL;
    if ((u64)row_bytes * (u64)h > (u64)(len - off_bits)) return NULL;

    image_t *img = image_create(w, h);
    if (!img) return NULL;

    const u8 *src = data + off_bits;
    for (u32 y = 0; y < h; y++) {
        /* Bottom-up files store the last display row first. */
        const u8 *srow = src + (size_t)(top_down ? y : (h - 1u - y)) * row_bytes;
        u32      *drow = img->pixels + (size_t)y * w;

        if (bpp == 24) {
            for (u32 x = 0; x < w; x++) {
                const u8 *p = srow + (size_t)x * 3u;
                drow[x] = 0xFF000000u | ((u32)p[2] << 16) | ((u32)p[1] << 8) | (u32)p[0];
            }
        } else {
            for (u32 x = 0; x < w; x++) {
                u32 px = rd32(srow + (size_t)x * 4u);
                u32 a  = amask ? chan(px, amask, as, am, ash) : 0xFFu;
                drow[x] = (a << 24)
                        | (chan(px, rmask, rs, rm, rsh) << 16)
                        | (chan(px, gmask, gs, gm, gsh) <<  8)
                        |  chan(px, bmask, bs, bm, bsh);
            }
        }
    }

    image_classify_alpha(img);
    return img;
}
