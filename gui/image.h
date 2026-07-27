#ifndef CAREOS_IMAGE_H
#define CAREOS_IMAGE_H

/* =============================================================================
 * CareOS gui/image.h  --  raster images and the blitter
 *
 * Everything in CareOS is drawn into a 32-bpp buffer holding 0x00RRGGBB, so an
 * image is just a second such buffer plus an alpha byte. There is no colour
 * conversion at draw time and no palette: decoders normalise into one canonical
 * in-memory pixel, 0xAARRGGBB, and gfx_draw_image() src-over blends it onto
 * whatever g_target currently points at (screen backbuffer or a window buffer).
 *
 * Three container formats are understood, chosen by sniffing the first bytes:
 *
 *   .cri  CareOS Raster Image  -- native. Its BGRA32 payload IS the in-memory
 *         pixel layout on little-endian x86, so image_load() can BORROW the
 *         VFS buffer with no allocation and no decode pass. This is what the
 *         baked icon theme ships as, which is why 300 icons cost zero heap.
 *   .bmp  Windows BITMAPINFOHEADER, 24/32-bpp uncompressed and BI_BITFIELDS.
 *         Bottom-up rows are flipped during decode. What every art tool exports.
 *   .tga  Truevision, 24/32-bpp uncompressed (type 2) and RLE (type 10).
 *         Smallest decoder of the three; handy for hand-rolled assets.
 *
 * Nothing here allocates unless it has to. A decoder that can hand back a view
 * of the caller's bytes does so and clears IMG_OWNS_PIXELS; image_free() then
 * frees only the header.
 * ============================================================================= */

#include "kernel.h"
#include "gui.h"

/* -- Pixel formats --------------------------------------------------------
 * Both are 32-bit 0xAARRGGBB. XRGB32 promises every alpha byte is 0xFF, which
 * lets the blitter take the memcpy path without scanning the image first. */
#define IMG_FMT_ARGB32   0u
#define IMG_FMT_XRGB32   1u

/* -- image_t flags -------------------------------------------------------- */
#define IMG_OWNS_PIXELS  0x01u   /* image_free() kfree()s pixels               */
#define IMG_HAS_ALPHA    0x02u   /* at least one pixel is not fully opaque     */
#define IMG_CACHED       0x04u   /* owned by resource_cache; do not free direct */

typedef struct {
    u32  width;
    u32  height;
    u32  format;      /* IMG_FMT_*                                            */
    u32 *pixels;      /* width*height, top-down, no padding, 0xAARRGGBB       */
    u32  flags;       /* IMG_* bits                                           */
    i32  refs;        /* resource_cache reference count; 0 for loose images   */
} image_t;

/* Largest image the decoders will accept. A 4096x4096 ARGB32 image is 64 MiB
 * against a 192 MiB kernel heap, which is already beyond reason for a desktop
 * that ships 48px icons -- the cap exists so a corrupt header cannot ask for a
 * 4 GiB allocation. */
#define IMAGE_MAX_DIM     4096u
#define IMAGE_MAX_PIXELS  (4096u * 4096u)

/* -- Loading ---------------------------------------------------------------
 * image_load() resolves a VFS path, sniffs the container and decodes. Returns
 * NULL for a missing file, an unknown container or a header that fails the
 * sanity checks. The returned image is owned by the caller: image_free() it.
 *
 * image_load_mem() is the same decode step against a buffer the caller already
 * has. `data` must outlive the image when the decoder borrows it -- pass
 * copy_always = true if it will not. */
image_t *image_load(const char *path);
image_t *image_load_mem(const u8 *data, u32 len, bool copy_always);

/* Blank image, IMG_FMT_ARGB32, pixels zeroed (fully transparent). */
image_t *image_create(u32 w, u32 h);

/* Nearest-neighbour resample into a new image. NULL on bad size or OOM.
 * Nearest, not bilinear: icons are baked at their display size by
 * tools/gen-icons.py, so this only runs for off-theme sizes, and a sharp
 * result beats a blurred one on a 48px glyph. */
image_t *image_scaled(const image_t *src, u32 w, u32 h);

/* Safe on NULL, on IMG_CACHED images (ignored -- the cache owns those) and on
 * borrowed pixels (frees the header only). */
void     image_free(image_t *img);

/* Single pixel, 0xAARRGGBB. Out-of-range reads return 0 (transparent). */
u32      image_pixel(const image_t *img, u32 x, u32 y);

/* -- Drawing ---------------------------------------------------------------
 * All of these honour the active gfx clip rect and g_target, and mark the
 * touched area dirty, exactly like the gfx_* primitives they sit beside. */

/* Src-over blend at 1:1. Fully-transparent pixels cost one compare. */
void gfx_draw_image(image_t *img, i32 x, i32 y);

/* Nearest-neighbour scale straight to the target -- no intermediate image.
 * Use this for one-off sizes; for an icon redrawn every frame, prefer
 * res_icon(), which caches the resampled copy. */
void gfx_draw_image_scaled(image_t *img, i32 x, i32 y, i32 w, i32 h);

/* As gfx_draw_image() with a global opacity multiplied into the per-pixel
 * alpha. opacity 255 is identical to gfx_draw_image(). */
void gfx_draw_image_alpha(image_t *img, i32 x, i32 y, u8 opacity);

/* Draw the image's alpha channel in a flat colour -- the monochrome/symbolic
 * icon path, so one baked glyph can follow the theme accent. RGB is ignored. */
void gfx_draw_image_tinted(image_t *img, i32 x, i32 y, u32 tint);

/* Blit a sub-rectangle. Used for sprite sheets and for wallpaper cropping. */
void gfx_draw_image_region(image_t *img, i32 sx, i32 sy, i32 sw, i32 sh,
                           i32 dx, i32 dy);

/* Cover-fit an image over a rectangle (aspect preserved, overflow cropped) --
 * how a wallpaper is painted onto a screen of a different aspect ratio. */
void gfx_draw_image_cover(image_t *img, i32 x, i32 y, i32 w, i32 h);

/* -- Decoders --------------------------------------------------------------
 * Exposed for the sniffer in image.c and for tests; call image_load() instead.
 * Each returns NULL when `data` is not its format. `borrow` asks the decoder to
 * alias `data` rather than copy when the format allows it (CRI only). */
image_t *image_decode_cri(const u8 *data, u32 len, bool borrow);
image_t *image_decode_bmp(const u8 *data, u32 len);
image_t *image_decode_tga(const u8 *data, u32 len);

/* Scan the decoded pixels, set format/IMG_HAS_ALPHA, and rescue 32-bpp sources
 * whose alpha channel was never written (all-zero => forced opaque). Called by
 * the BMP and TGA decoders; .cri carries the answer in its header. */
void image_classify_alpha(image_t *img);

/* -- .cri container --------------------------------------------------------
 * Header is 16 bytes so the payload stays 4-byte aligned, which is what makes
 * the borrow path legal. All fields little-endian.
 *
 *   0  4  magic "CRI1"
 *   4  2  width
 *   6  2  height
 *   8  1  format   (CRI_ENC_*)
 *   9  1  flags    (bit 0: image contains non-opaque pixels)
 *  10  2  reserved, must be 0
 *  12  4  payload length in bytes
 *  16  .. payload
 *
 * CRI_ENC_BGRA32 payload is width*height*4 bytes in B,G,R,A order, which on
 * little-endian x86 reads back as the u32 0xAARRGGBB the blitter wants.
 * CRI_ENC_RLE_BGRA32 is a byte stream of packets: a count byte, then either one
 * BGRA quad repeated count+1 times (count & 0x80) or count+1 literal quads. */
#define CRI_MAGIC0 'C'
#define CRI_MAGIC1 'R'
#define CRI_MAGIC2 'I'
#define CRI_MAGIC3 '1'

#define CRI_ENC_BGRA32      0u
#define CRI_ENC_RLE_BGRA32  1u

#define CRI_FLAG_HAS_ALPHA  0x01u
#define CRI_HEADER_BYTES    16u

#endif /* CAREOS_IMAGE_H */
