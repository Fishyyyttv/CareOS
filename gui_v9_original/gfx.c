/* =============================================================================
 * CareOS gui/gfx.c  --  buffered framebuffer renderer
 * Draw into a 32-bpp backbuffer, then copy once per frame to the real FB.
 * This removes visible scan/flicker from direct-to-FB drawing.
 *
 * Changes (v5):
 *   B-01 -- Pixel format no longer hardcoded. gfx_set_pixel_format() stores
 *           the per-channel shifts read from the MB2 framebuffer tag so that
 *           any RGB/BGR/RGBA layout is handled correctly.
 *   B-03 -- gfx_direct_mode flag replaces the fragile BACKBUFFER==FRAMEBUFFER
 *           pointer-equality test, preventing the 24-bpp aliasing corruption.
 *   B-04 -- Explicit x<0||y<0 guard added before the signed-to-unsigned cast.
 *   P-01 -- SSE2 fast path in gfx_flip for 32-bpp 16-byte-aligned blits.
 *   P-02 -- Dirty-rectangle tracking: only changed regions are flushed.
 *   P-03 -- Pre-expanded 32-bit glyph row cache; gfx_char uses row copies.
 * ============================================================================= */
#include "kernel.h"
#include "gui.h"
#define COL_PRIMARY_DK  rgb(0x30,0x50,0xcc)
#define GFX_TRANSPARENT 0xFFFFFFFF  /* internal skip-pixel sentinel */

u32  SCREEN_W     = 800;
u32  SCREEN_H     = 600;
u32  SCREEN_PITCH = 800*4;
u32 *FRAMEBUFFER  = (u32*)0;
static u32 FB_BPP        = 32;
static u32 *BACKBUFFER   = (u32*)0;
static bool gfx_direct_mode = false; /* B-03: true when kmalloc failed */

/* -- B-01: Per-channel bit shifts for the hardware pixel format ----------- */
static u8 FB_R_SHIFT = 16;  /* defaults: 0x00RRGGBB (our internal packing) */
static u8 FB_G_SHIFT =  8;
static u8 FB_B_SHIFT =  0;

/* Called by kernel_main after reading MB2 color mask fields.
 * Pass red_field_pos, green_field_pos, blue_field_pos from mb2_tag_fb_t. */
void gfx_set_pixel_format(u8 r_shift, u8 g_shift, u8 b_shift) {
    FB_R_SHIFT = r_shift;
    FB_G_SHIFT = g_shift;
    FB_B_SHIFT = b_shift;
}

/* Convert internal 0x00RRGGBB to the hardware byte order. */
static inline u32 gfx_pack_color(u32 color) {
    u8 r = (u8)((color >> 16) & 0xFF);
    u8 g = (u8)((color >>  8) & 0xFF);
    u8 b = (u8)( color        & 0xFF);
    return ((u32)r << FB_R_SHIFT) | ((u32)g << FB_G_SHIFT) | ((u32)b << FB_B_SHIFT);
}

static inline void fb_write_pixel(u32 x, u32 y, u32 color) {
    if (!FRAMEBUFFER) return;
    if (x >= SCREEN_W || y >= SCREEN_H) return;
    u32 packed = gfx_pack_color(color);
    u8 *base = (u8*)FRAMEBUFFER + y * SCREEN_PITCH + x * (FB_BPP / 8);
    base[0] = (u8)( packed        & 0xFF);
    base[1] = (u8)((packed >>  8) & 0xFF);
    base[2] = (u8)((packed >> 16) & 0xFF);
    if (FB_BPP == 32) base[3] = 0xFF;
}

/* -- P-02: Dirty-rectangle tracker ---------------------------------------- */
#define MAX_DIRTY 32
typedef struct { i32 x, y, w, h; } dirty_rect_t;
static dirty_rect_t dirty_rects[MAX_DIRTY];
static u32          dirty_count = 0;
static bool         dirty_full  = false;

static void dirty_reset(void) { dirty_count = 0; dirty_full = false; }

void gfx_dirty(i32 x, i32 y, i32 w, i32 h) {
    if (dirty_full) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (w <= 0 || h <= 0) return;
    if ((u32)(x + w) > SCREEN_W) w = (i32)SCREEN_W - x;
    if ((u32)(y + h) > SCREEN_H) h = (i32)SCREEN_H - y;
    /* Check containment to avoid redundant entries */
    for (u32 i = 0; i < dirty_count; i++) {
        dirty_rect_t *d = &dirty_rects[i];
        if (x >= d->x && y >= d->y &&
            x+w <= d->x+d->w && y+h <= d->y+d->h) return;
    }
    if (dirty_count >= MAX_DIRTY) { dirty_full = true; return; }
    dirty_rects[dirty_count++] = (dirty_rect_t){x, y, w, h};
}

/* -- P-01: SSE2 blit helper (32-bpp, 16-byte aligned) -------------------- */
#ifdef __SSE2__
#include <immintrin.h>
static void gfx_flip_sse2(void) {
    /* Backbuffer is allocated 16-byte aligned; framebuffer assumed aligned */
    __m128i *src = (__m128i*)BACKBUFFER;
    __m128i *dst = (__m128i*)FRAMEBUFFER;
    size_t n = (SCREEN_W * SCREEN_H * 4) / 16;
    for (size_t i = 0; i < n; i++)
        _mm_storeu_si128(dst + i, _mm_loadu_si128(src + i));
}
#endif

void gfx_init(u32 *fb, u32 w, u32 h, u32 pitch) {
    FRAMEBUFFER  = fb;
    SCREEN_W     = w;
    SCREEN_H     = h;
    SCREEN_PITCH = pitch;

    u32 bytes_per_px = pitch / w;
    FB_BPP = (bytes_per_px >= 4) ? 32 : 24;

    /* 16-byte aligned allocation for SSE2 (P-01) */
    size_t sz = (size_t)(w * h * sizeof(u32));
    sz = (sz + 15) & ~15u;
    BACKBUFFER = (u32*)kmalloc(sz);
    if (!BACKBUFFER) {
        /* B-03: use explicit flag, do NOT alias the framebuffer pointer */
        gfx_direct_mode = true;
        BACKBUFFER = FRAMEBUFFER;
        serial_write("  [gfx] WARN: backbuffer alloc failed, direct mode\n");
    }

    dirty_reset();
    dirty_full = true; /* first frame: flush everything */
    gfx_clear(0);
    gfx_flip();
}

void gfx_flip(void) {
    if (!FRAMEBUFFER || !BACKBUFFER) return;

    /* B-03: in direct mode the data is already on-screen, nothing to copy */
    if (gfx_direct_mode) { dirty_reset(); return; }

    /* P-01 + P-02: SSE2 path for 32-bpp; dirty-rect path for 24-bpp */
    if (FB_BPP == 32) {
#ifdef __SSE2__
        if (!dirty_full && dirty_count == 0) { dirty_reset(); return; }
        if (dirty_full) {
            gfx_flip_sse2();
        } else {
            /* Copy only dirty regions via SSE2 row copies */
            for (u32 d = 0; d < dirty_count; d++) {
                dirty_rect_t *dr = &dirty_rects[d];
                for (i32 row = 0; row < dr->h; row++) {
                    u32 *src = BACKBUFFER  + ((u32)(dr->y + row)) * SCREEN_W + (u32)dr->x;
                    u8  *dst = (u8*)FRAMEBUFFER + ((u32)(dr->y + row)) * SCREEN_PITCH
                                                 + (u32)dr->x * 4;
                    kmemcpy(dst, src, (size_t)dr->w * 4);
                }
            }
        }
#else
        if (dirty_full) {
            kmemcpy(FRAMEBUFFER, BACKBUFFER, (size_t)(SCREEN_W * SCREEN_H * sizeof(u32)));
        } else {
            for (u32 d = 0; d < dirty_count; d++) {
                dirty_rect_t *dr = &dirty_rects[d];
                for (i32 row = 0; row < dr->h; row++) {
                    u32 *src = BACKBUFFER  + ((u32)(dr->y + row)) * SCREEN_W + (u32)dr->x;
                    u8  *dst = (u8*)FRAMEBUFFER + ((u32)(dr->y + row)) * SCREEN_PITCH
                                                 + (u32)dr->x * 4;
                    kmemcpy(dst, src, (size_t)dr->w * 4);
                }
            }
        }
#endif
        dirty_reset();
        return;
    }

    /* 24-bpp fallback: always full flush via fb_write_pixel */
    for (u32 y = 0; y < SCREEN_H; y++) {
        u32 *src = BACKBUFFER + y * SCREEN_W;
        for (u32 x = 0; x < SCREEN_W; x++)
            fb_write_pixel(x, y, src[x]);
    }
    dirty_reset();
}

void gfx_clear(u32 color) {
    if (!BACKBUFFER) return;
    u32 count = SCREEN_W * SCREEN_H;
    for (u32 i = 0; i < count; i++) BACKBUFFER[i] = color;
    dirty_full = true;
}

/* -- Primitives ----------------------------------------------------------- */
/* B-04: explicit negative guard before cast */
void gfx_setpixel(i32 x, i32 y, u32 color) {
    if (!BACKBUFFER) return;
    if (x < 0 || y < 0) return;                      /* B-04 */
    if ((u32)x >= SCREEN_W || (u32)y >= SCREEN_H) return;
    BACKBUFFER[(u32)y * SCREEN_W + (u32)x] = color;
}

void gfx_hline(i32 x, i32 y, i32 len, u32 color) {
    if (!BACKBUFFER) return;
    if (y < 0 || (u32)y >= SCREEN_H || len <= 0) return;
    i32 x1 = x < 0 ? 0 : x;
    i32 x2 = (x+len) > (i32)SCREEN_W ? (i32)SCREEN_W : (x+len);
    if (x1 >= x2) return;
    u32 *row = BACKBUFFER + (u32)y * SCREEN_W + (u32)x1;
    i32 n = x2 - x1; while (n-- > 0) *row++ = color;
    gfx_dirty(x1, y, x2 - x1, 1);
}

void gfx_vline(i32 x, i32 y, i32 len, u32 color) {
    for (i32 i = 0; i < len; i++) gfx_setpixel(x, y+i, color);
    gfx_dirty(x, y, 1, len);
}

void gfx_rect(i32 x, i32 y, i32 w, i32 h, u32 color) {
    for (i32 i = 0; i < h; i++) gfx_hline(x, y+i, w, color);
    gfx_dirty(x, y, w, h);
}

void gfx_rect_outline(i32 x, i32 y, i32 w, i32 h, u32 color) {
    gfx_hline(x,     y,     w, color);
    gfx_hline(x,     y+h-1, w, color);
    gfx_vline(x,     y,     h, color);
    gfx_vline(x+w-1, y,     h, color);
    gfx_dirty(x, y, w, h);
}

void gfx_rect_rounded(i32 x, i32 y, i32 w, i32 h, i32 r, u32 color) {
    if (r <= 0) { gfx_rect(x,y,w,h,color); return; }
    gfx_rect(x+r, y,   w-2*r, h,   color);
    gfx_rect(x,   y+r, r,   h-2*r, color);
    gfx_rect(x+w-r, y+r, r, h-2*r, color);
    gfx_circle_fill(x+r,     y+r,     r, color);
    gfx_circle_fill(x+w-r-1, y+r,     r, color);
    gfx_circle_fill(x+r,     y+h-r-1, r, color);
    gfx_circle_fill(x+w-r-1, y+h-r-1, r, color);
    gfx_dirty(x, y, w, h);
}



void gfx_circle(i32 cx, i32 cy, i32 r, u32 color) {
    i32 x=r,y=0,err=0;
    while(x>=y){
        gfx_setpixel(cx+x,cy+y,color);gfx_setpixel(cx+y,cy+x,color);
        gfx_setpixel(cx-y,cy+x,color);gfx_setpixel(cx-x,cy+y,color);
        gfx_setpixel(cx-x,cy-y,color);gfx_setpixel(cx-y,cy-x,color);
        gfx_setpixel(cx+y,cy-x,color);gfx_setpixel(cx+x,cy-y,color);
        if(err<=0){y++;err+=2*y+1;}else{x--;err-=2*x+1;}
    }
    gfx_dirty(cx-r, cy-r, 2*r+1, 2*r+1);
}

void gfx_circle_fill(i32 cx, i32 cy, i32 r, u32 color) {
    i32 x=r,y=0,err=0;
    while(x>=y){
        gfx_hline(cx-x,cy+y,2*x+1,color);gfx_hline(cx-x,cy-y,2*x+1,color);
        gfx_hline(cx-y,cy+x,2*y+1,color);gfx_hline(cx-y,cy-x,2*y+1,color);
        if(err<=0){y++;err+=2*y+1;}else{x--;err-=2*x+1;}
    }
    gfx_dirty(cx-r, cy-r, 2*r+1, 2*r+1);
}

/* -- P-03: 8x8 font with pre-expanded glyph cache ------------------------ */
static const u8 font8[95][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 32   */
    {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00}, /* 33 ! */
    {0x36,0x36,0x00,0x00,0x00,0x00,0x00,0x00}, /* 34 " */
    {0x36,0x36,0x7F,0x36,0x7F,0x36,0x36,0x00}, /* 35 # */
    {0x0C,0x3E,0x03,0x1E,0x30,0x1F,0x0C,0x00}, /* 36 $ */
    {0x00,0x63,0x33,0x18,0x0C,0x66,0x63,0x00}, /* 37 % */
    {0x1C,0x36,0x1C,0x6E,0x3B,0x33,0x6E,0x00}, /* 38 & */
    {0x06,0x06,0x03,0x00,0x00,0x00,0x00,0x00}, /* 39 ' */
    {0x18,0x0C,0x06,0x06,0x06,0x0C,0x18,0x00}, /* 40 ( */
    {0x06,0x0C,0x18,0x18,0x18,0x0C,0x06,0x00}, /* 41 ) */
    {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00}, /* 42 * */
    {0x00,0x0C,0x0C,0x3F,0x0C,0x0C,0x00,0x00}, /* 43 + */
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x06}, /* 44 , */
    {0x00,0x00,0x00,0x3F,0x00,0x00,0x00,0x00}, /* 45 - */
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x00}, /* 46 . */
    {0x60,0x30,0x18,0x0C,0x06,0x03,0x01,0x00}, /* 47 / */
    {0x3E,0x63,0x73,0x7B,0x6F,0x67,0x3E,0x00}, /* 48 0 */
    {0x0C,0x0E,0x0C,0x0C,0x0C,0x0C,0x3F,0x00}, /* 49 1 */
    {0x1E,0x33,0x30,0x1C,0x06,0x33,0x3F,0x00}, /* 50 2 */
    {0x1E,0x33,0x30,0x1C,0x30,0x33,0x1E,0x00}, /* 51 3 */
    {0x38,0x3C,0x36,0x33,0x7F,0x30,0x78,0x00}, /* 52 4 */
    {0x3F,0x03,0x1F,0x30,0x30,0x33,0x1E,0x00}, /* 53 5 */
    {0x1C,0x06,0x03,0x1F,0x33,0x33,0x1E,0x00}, /* 54 6 */
    {0x3F,0x33,0x30,0x18,0x0C,0x0C,0x0C,0x00}, /* 55 7 */
    {0x1E,0x33,0x33,0x1E,0x33,0x33,0x1E,0x00}, /* 56 8 */
    {0x1E,0x33,0x33,0x3E,0x30,0x18,0x0E,0x00}, /* 57 9 */
    {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x00}, /* 58 : */
    {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x06}, /* 59 ; */
    {0x18,0x0C,0x06,0x03,0x06,0x0C,0x18,0x00}, /* 60 < */
    {0x00,0x00,0x3F,0x00,0x00,0x3F,0x00,0x00}, /* 61 = */
    {0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00}, /* 62 > */
    {0x1E,0x33,0x30,0x18,0x0C,0x00,0x0C,0x00}, /* 63 ? */
    {0x3E,0x63,0x7B,0x7B,0x7B,0x03,0x1E,0x00}, /* 64 @ */
    {0x0C,0x1E,0x33,0x33,0x3F,0x33,0x33,0x00}, /* 65 A */
    {0x3F,0x66,0x66,0x3E,0x66,0x66,0x3F,0x00}, /* 66 B */
    {0x3C,0x66,0x03,0x03,0x03,0x66,0x3C,0x00}, /* 67 C */
    {0x1F,0x36,0x66,0x66,0x66,0x36,0x1F,0x00}, /* 68 D */
    {0x7F,0x46,0x16,0x1E,0x16,0x46,0x7F,0x00}, /* 69 E */
    {0x7F,0x46,0x16,0x1E,0x16,0x06,0x0F,0x00}, /* 70 F */
    {0x3C,0x66,0x03,0x03,0x73,0x66,0x7C,0x00}, /* 71 G */
    {0x33,0x33,0x33,0x3F,0x33,0x33,0x33,0x00}, /* 72 H */
    {0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, /* 73 I */
    {0x78,0x30,0x30,0x30,0x33,0x33,0x1E,0x00}, /* 74 J */
    {0x67,0x66,0x36,0x1E,0x36,0x66,0x67,0x00}, /* 75 K */
    {0x0F,0x06,0x06,0x06,0x46,0x66,0x7F,0x00}, /* 76 L */
    {0x63,0x77,0x7F,0x7F,0x6B,0x63,0x63,0x00}, /* 77 M */
    {0x63,0x67,0x6F,0x7B,0x73,0x63,0x63,0x00}, /* 78 N */
    {0x1C,0x36,0x63,0x63,0x63,0x36,0x1C,0x00}, /* 79 O */
    {0x3F,0x66,0x66,0x3E,0x06,0x06,0x0F,0x00}, /* 80 P */
    {0x1E,0x33,0x33,0x33,0x3B,0x1E,0x38,0x00}, /* 81 Q */
    {0x3F,0x66,0x66,0x3E,0x36,0x66,0x67,0x00}, /* 82 R */
    {0x1E,0x33,0x07,0x0E,0x38,0x33,0x1E,0x00}, /* 83 S */
    {0x3F,0x2D,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, /* 84 T */
    {0x33,0x33,0x33,0x33,0x33,0x33,0x3F,0x00}, /* 85 U */
    {0x33,0x33,0x33,0x33,0x33,0x1E,0x0C,0x00}, /* 86 V */
    {0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00}, /* 87 W */
    {0x63,0x63,0x36,0x1C,0x1C,0x36,0x63,0x00}, /* 88 X */
    {0x33,0x33,0x33,0x1E,0x0C,0x0C,0x1E,0x00}, /* 89 Y */
    {0x7F,0x63,0x31,0x18,0x4C,0x66,0x7F,0x00}, /* 90 Z */
    {0x1E,0x06,0x06,0x06,0x06,0x06,0x1E,0x00}, /* 91 [ */
    {0x03,0x06,0x0C,0x18,0x30,0x60,0x40,0x00}, /* 92 \ */
    {0x1E,0x18,0x18,0x18,0x18,0x18,0x1E,0x00}, /* 93 ] */
    {0x08,0x1C,0x36,0x63,0x00,0x00,0x00,0x00}, /* 94 ^ */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF}, /* 95 _ */
    {0x0C,0x0C,0x18,0x00,0x00,0x00,0x00,0x00}, /* 96 ` */
    {0x00,0x00,0x1E,0x30,0x3E,0x33,0x6E,0x00}, /* 97 a */
    {0x07,0x06,0x06,0x3E,0x66,0x66,0x3B,0x00}, /* 98 b */
    {0x00,0x00,0x1E,0x33,0x03,0x33,0x1E,0x00}, /* 99 c */
    {0x38,0x30,0x30,0x3e,0x33,0x33,0x6E,0x00}, /* 100 d */
    {0x00,0x00,0x1E,0x33,0x3f,0x03,0x1E,0x00}, /* 101 e */
    {0x1C,0x36,0x06,0x0f,0x06,0x06,0x0F,0x00}, /* 102 f */
    {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x1F}, /* 103 g */
    {0x07,0x06,0x36,0x6E,0x66,0x66,0x67,0x00}, /* 104 h */
    {0x0C,0x00,0x0E,0x0C,0x0C,0x0C,0x1E,0x00}, /* 105 i */
    {0x30,0x00,0x30,0x30,0x30,0x33,0x33,0x1E}, /* 106 j */
    {0x07,0x06,0x66,0x36,0x1E,0x36,0x67,0x00}, /* 107 k */
    {0x0E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, /* 108 l */
    {0x00,0x00,0x33,0x7F,0x7F,0x6B,0x63,0x00}, /* 109 m */
    {0x00,0x00,0x1F,0x33,0x33,0x33,0x33,0x00}, /* 110 n */
    {0x00,0x00,0x1E,0x33,0x33,0x33,0x1E,0x00}, /* 111 o */
    {0x00,0x00,0x3B,0x66,0x66,0x3E,0x06,0x0F}, /* 112 p */
    {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x78}, /* 113 q */
    {0x00,0x00,0x3B,0x6E,0x66,0x06,0x0F,0x00}, /* 114 r */
    {0x00,0x00,0x3E,0x03,0x1E,0x30,0x1F,0x00}, /* 115 s */
    {0x08,0x0C,0x3E,0x0C,0x0C,0x2C,0x18,0x00}, /* 116 t */
    {0x00,0x00,0x33,0x33,0x33,0x33,0x6E,0x00}, /* 117 u */
    {0x00,0x00,0x33,0x33,0x33,0x1E,0x0C,0x00}, /* 118 v */
    {0x00,0x00,0x63,0x6B,0x7F,0x7F,0x36,0x00}, /* 119 w */
    {0x00,0x00,0x63,0x36,0x1C,0x36,0x63,0x00}, /* 120 x */
    {0x00,0x00,0x33,0x33,0x33,0x3E,0x30,0x1F}, /* 121 y */
    {0x00,0x00,0x3F,0x19,0x0C,0x26,0x3F,0x00}, /* 122 z */
    {0x38,0x0C,0x0C,0x07,0x0C,0x0C,0x38,0x00}, /* 123 { */
    {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00}, /* 124 | */
    {0x07,0x0C,0x0C,0x38,0x0C,0x0C,0x07,0x00}, /* 125 } */
    {0x6E,0x3B,0x00,0x00,0x00,0x00,0x00,0x00}, /* 126 ~ */
};

/* P-03: Per-glyph cache -- 95 glyphs x 8 rows x 8 pixels of pre-packed u32 colors.
 * Cache is rebuilt whenever fg/bg change via gfx_char_cache_build(). */
#define GLYPH_CACHE_ENTRIES 95
static u32 glyph_cache[GLYPH_CACHE_ENTRIES][8][8];
static u32 cache_fg = 0xFFFFFFFF; /* sentinel: force rebuild on first use */
static u32 cache_bg = 0xFFFFFFFF;

static void gfx_char_cache_build(u32 fg, u32 bg) {
    for (int ci = 0; ci < GLYPH_CACHE_ENTRIES; ci++) {
        for (int row = 0; row < 8; row++) {
            u8 bits = font8[ci][row];
            for (int col = 0; col < 8; col++)
                glyph_cache[ci][row][col] = ((bits >> col) & 1) ? fg : bg;
        }
    }
    cache_fg = fg;
    cache_bg = bg;
}

void gfx_char(i32 x, i32 y, char c, u32 fg, u32 bg) {
    if (c < 32 || c > 126) c = '?';
    bool skip_bg = (bg == COL_TRANSPARENT);

    if (!skip_bg) {
        /* Use pre-expanded cache for opaque rendering (P-03) */
        if (fg != cache_fg || bg != cache_bg)
            gfx_char_cache_build(fg, bg);
        int ci = (u8)c - 32;
        for (i32 row = 0; row < 8; row++) {
            i32 py = y + row;
            if (py < 0 || (u32)py >= SCREEN_H) continue;
            u32 *dst = BACKBUFFER + (u32)py * SCREEN_W;
            for (i32 col = 0; col < 8; col++) {
                i32 px = x + col;
                if (px < 0 || (u32)px >= SCREEN_W) continue;
                dst[(u32)px] = glyph_cache[ci][row][col];
            }
        }
    } else {
        /* Transparent bg: only draw foreground pixels */
        const u8 *g = font8[(u8)c - 32];
        for (i32 row = 0; row < 8; row++) {
            u8 bits = g[row];
            for (i32 col = 0; col < 8; col++)
                if ((bits >> col) & 1) gfx_setpixel(x+col, y+row, fg);
        }
    }
    gfx_dirty(x, y, 8, 8);
}

void gfx_str(i32 x, i32 y, const char *s, u32 fg, u32 bg) {
    i32 cx=x;
    while (*s) {
        if (*s=='\n'){cx=x;y+=FONT_H;s++;continue;}
        gfx_char(cx,y,*s,fg,bg);
        cx+=FONT_W; s++;
    }
}

void gfx_str_centered(i32 x, i32 y, i32 w, const char *s, u32 fg, u32 bg) {
    i32 len=(i32)kstrlen(s);
    gfx_str(x+(w-len*FONT_W)/2, y, s, fg, bg);
}

void gfx_str_bg_none(i32 x, i32 y, const char *s, u32 fg) {
    i32 cx = x;
    while (*s) {
        if (*s == '\n') { cx = x; y += FONT_H; s++; continue; }
        if (*s >= 32 && *s <= 126) {
            const u8 *g = font8[(u8)*s - 32];
            for (i32 row = 0; row < 8; row++) {
                u8 bits = g[row];
                for (i32 col = 0; col < 8; col++)
                    if ((bits >> col) & 1) gfx_setpixel(cx+col, y+row, fg);
            }
        }
        cx += FONT_W; s++;
    }
}

/* -- Clip region ---------------------------------------------------------- */
static i32 clip_x=0,clip_y=0,clip_w=0,clip_h=0;
static bool clip_active=false;
void gfx_set_clip(i32 x,i32 y,i32 w,i32 h){ clip_x=x;clip_y=y;clip_w=w;clip_h=h;clip_active=true; }
void gfx_clear_clip(void){ clip_active=false; }

static inline bool in_clip(i32 x,i32 y){
    if(!clip_active) return true;
    return x>=clip_x&&x<clip_x+clip_w&&y>=clip_y&&y<clip_y+clip_h;
}

/* B-04: negative guard before cast */
static inline void put_px(i32 x,i32 y,u32 c){
    if(x<0||y<0) return;                          /* B-04 */
    if((u32)x>=(u32)SCREEN_W||(u32)y>=(u32)SCREEN_H) return;
    if(!in_clip(x,y)) return;
    BACKBUFFER[y*SCREEN_W+x]=c;
}

u32 gfx_getpixel(i32 x,i32 y){
    if(x<0||y<0||(u32)x>=(u32)SCREEN_W||(u32)y>=(u32)SCREEN_H) return 0;
    return BACKBUFFER[y*SCREEN_W+x];
}

void gfx_line(i32 x0,i32 y0,i32 x1,i32 y1,u32 c){
    int dx=x1-x0<0?x0-x1:x1-x0, dy=y1-y0<0?y0-y1:y1-y0;
    int sx=x0<x1?1:-1, sy=y0<y1?1:-1, err=dx-dy;
    while(1){ put_px(x0,y0,c); if(x0==x1&&y0==y1) break;
        int e2=2*err;
        if(e2>-dy){err-=dy;x0+=sx;}
        if(e2<dx){err+=dx;y0+=sy;}
    }
}

void gfx_triangle_fill(i32 x0,i32 y0,i32 x1,i32 y1,i32 x2,i32 y2,u32 c){
    if(y0>y1){i32 t=y0;y0=y1;y1=t;t=x0;x0=x1;x1=t;}
    if(y0>y2){i32 t=y0;y0=y2;y2=t;t=x0;x0=x2;x2=t;}
    if(y1>y2){i32 t=y1;y1=y2;y2=t;t=x1;x1=x2;x2=t;}
    for(i32 y=y0;y<=y2;y++){
        i32 xa,xb;
        if(y<=y1&&y0!=y1) xa=x0+(x1-x0)*(y-y0)/(y1-y0); else if(y2!=y1) xa=x1+(x2-x1)*(y-y1)/(y2-y1); else xa=x1;
        if(y2!=y0) xb=x0+(x2-x0)*(y-y0)/(y2-y0); else xb=x0;
        if(xa>xb){i32 t=xa;xa=xb;xb=t;}
        gfx_hline(xa,y,xb-xa+1,c);
    }
}

void gfx_gradient_rect(i32 x,i32 y,i32 w,i32 h,u32 top,u32 bot){
    for(i32 row=0;row<h;row++){
        u32 t=(u32)row*255/(u32)(h?h:1);
        u8 r=(u8)(((top>>16&0xff)*(255-t)+(bot>>16&0xff)*t)>>8);
        u8 g=(u8)(((top>>8&0xff)*(255-t)+(bot>>8&0xff)*t)>>8);
        u8 b=(u8)(((top&0xff)*(255-t)+(bot&0xff)*t)>>8);
        gfx_hline(x,y+row,w,((u32)r<<16)|((u32)g<<8)|b);
    }
    gfx_dirty(x,y,w,h);
}

void gfx_rect_rounded_outline(i32 x,i32 y,i32 w,i32 h,i32 r,u32 c){
    gfx_hline(x+r,y,w-2*r,c); gfx_hline(x+r,y+h-1,w-2*r,c);
    gfx_vline(x,y+r,h-2*r,c); gfx_vline(x+w-1,y+r,h-2*r,c);
    gfx_dirty(x,y,w,h);
}

i32 gfx_str_width(const char *s){ return (i32)(kstrlen(s)*FONT_W); }

void gfx_str_right(i32 x,i32 y,i32 w,const char *s,u32 fg,u32 bg){
    i32 sw=gfx_str_width(s);
    gfx_str(x+w-sw,y,s,fg,bg);
}

void gfx_str_clipped(i32 x,i32 y,i32 maxw,const char *s,u32 fg,u32 bg){
    i32 px=x; int maxchars=maxw/FONT_W;
    for(int i=0;s[i]&&i<maxchars;i++){ gfx_char(px,y,s[i],fg,bg); px+=FONT_W; }
}

void gfx_str_transparent(i32 x,i32 y,const char *s,u32 fg){
    gfx_str(x,y,s,fg,COL_TRANSPARENT);
}



void gfx_bar(i32 x,i32 y,i32 w,i32 h,u32 bg,u32 fg,u32 pct){
    gfx_rect(x,y,w,h,bg);
    gfx_rect_outline(x,y,w,h,COL_BORDER);
    if(pct>100) pct=100;
    gfx_rect(x+1,y+1,(w-2)*pct/100,h-2,fg);
}

void gfx_blit(i32 dx,i32 dy,i32 dw,i32 dh,const u32 *src,i32 sw,i32 sh){
    for(i32 row=0;row<dh;row++){
        i32 sy=row*sh/dh;
        for(i32 col=0;col<dw;col++){
            i32 sx=col*sw/dw;
            u32 c=src[sy*sw+sx];
            if(c!=GFX_TRANSPARENT) put_px(dx+col,dy+row,c);
        }
    }
    gfx_dirty(dx,dy,dw,dh);
}

/* Widget drawing */
void button_draw(const button_t *b){
    u32 bg=b->pressed?COL_PRIMARY_DK:b->hover?COL_HOVER:b->bg;
    gfx_rect_rounded(b->rect.x,b->rect.y,b->rect.w,b->rect.h,4,bg);
    if(b->hover||b->active) gfx_rect_rounded_outline(b->rect.x,b->rect.y,b->rect.w,b->rect.h,4,COL_PRIMARY);
    gfx_str_centered(b->rect.x,b->rect.y+(b->rect.h-FONT_H)/2,b->rect.w,b->label,b->fg,bg);
}
bool button_hit(const button_t *b,i32 mx,i32 my){ return rect_contains(b->rect,mx,my); }

void textinput_draw(const textinput_t *t){
    u32 bg=t->focused?COL_INPUT_BG:COL_SURFACE2;
    gfx_rect(t->rect.x,t->rect.y,t->rect.w,t->rect.h,bg);
    u32 border_col=t->focused?COL_PRIMARY:COL_BORDER;
    gfx_rect_outline(t->rect.x,t->rect.y,t->rect.w,t->rect.h,border_col);
    if(t->len>0){
        gfx_str_clipped(t->rect.x+4,t->rect.y+(t->rect.h-FONT_H)/2,t->rect.w-8,t->buf,COL_TEXT,bg);
    } else {
        gfx_str_clipped(t->rect.x+4,t->rect.y+(t->rect.h-FONT_H)/2,t->rect.w-8,t->placeholder,COL_MUTED,bg);
    }
    if(t->focused){
        i32 cx=t->rect.x+4+(i32)(t->cursor*FONT_W);
        gfx_vline(cx,t->rect.y+3,t->rect.h-6,COL_ACCENT);
    }
}

void textinput_key(textinput_t *t,char c){
    if(c=='\b'){if(t->len>0){t->len--;t->buf[t->len]='\0';if(t->cursor>0)t->cursor--;}}
    else if(c>=32&&c<127&&t->len<TEXTINPUT_MAX-1){
        t->buf[t->len++]=c; t->buf[t->len]='\0'; t->cursor++;
    }
}

/* -- Notifications -------------------------------------------------------- */
static notification_t notifs[MAX_NOTIFICATIONS];
static bool notif_hover[MAX_NOTIFICATIONS];

void notify_push(const char *title, const char *body, u32 icon_color) {
    int slot = -1;
    u32 oldest_tick = 0xFFFFFFFF;
    for (int i = 0; i < MAX_NOTIFICATIONS; i++) {
        if (!notifs[i].active) { slot = i; break; }
        if (notifs[i].born_tick < oldest_tick) {
            oldest_tick = notifs[i].born_tick; slot = i;
        }
    }
    if (slot < 0) slot = 0;
    kstrncpy(notifs[slot].title, title, 31);
    kstrncpy(notifs[slot].body,  body,  127);
    notifs[slot].icon_color = icon_color;
    notifs[slot].born_tick  = timer_get_ticks();
    notifs[slot].active     = true;
    notif_hover[slot]       = false;
}

void notify_tick(void) {
    u32 now = timer_get_ticks();
    for (int i = 0; i < MAX_NOTIFICATIONS; i++)
        if (notifs[i].active && now - notifs[i].born_tick > 500)
            notifs[i].active = false;
}

void notify_draw(void) {
    int slot = 0;
    for (int i = 0; i < MAX_NOTIFICATIONS; i++) {
        if (!notifs[i].active) continue;
        i32 nw=264, nh=62;
        i32 nx = (i32)SCREEN_W - nw - 12;
        i32 ny = 12 + slot*(nh+8);

        u32 now  = timer_get_ticks();
        u32 age  = now - notifs[i].born_tick;
        u32 life = 500;
        u32 pct  = (age < life) ? (100 * (life - age) / life) : 0;

        gfx_shadow(nx, ny, nw, nh);
        gfx_rect_rounded(nx, ny, nw, nh, 7, COL_SURFACE);
        gfx_rect_rounded_outline(nx, ny, nw, nh, 7,
            notif_hover[i] ? COL_PRIMARY : COL_BORDER);

        gfx_rect_rounded(nx, ny, 4, nh, 2, notifs[i].icon_color);
        gfx_str(nx+14, ny+9, notifs[i].title, COL_TEXT, COL_TRANSPARENT);
        gfx_str_clipped(nx+14, ny+26, nw-44, notifs[i].body, COL_DIM, COL_TRANSPARENT);

        i32 bar_w = (nw-14) * (i32)pct / 100;
        if (bar_w > 0) {
            gfx_rect(nx+7, ny+nh-5, nw-14, 3, COL_SURFACE2);
            gfx_rect(nx+7, ny+nh-5, bar_w, 3, notifs[i].icon_color);
        }

        if (notif_hover[i]) {
            i32 xbx = nx+nw-16, xby = ny+6;
            gfx_rect_rounded(xbx, xby, 12, 12, 3, rgb(0x40,0x10,0x10));
            gfx_str(xbx+2, xby+1, "x", COL_RED, COL_TRANSPARENT);
        }
        slot++;
    }
}

bool notify_handle_mouse(mouse_t *m) {
    int slot = 0;
    for (int i = 0; i < MAX_NOTIFICATIONS; i++) {
        if (!notifs[i].active) continue;
        i32 nw=264, nh=62;
        i32 nx=(i32)SCREEN_W-nw-12;
        i32 ny=12+slot*(nh+8);
        rect_t nr = rect_make(nx, ny, nw, nh);
        notif_hover[i] = rect_contains(nr, m->x, m->y);

        if (notif_hover[i] && m->left_clicked) {
            i32 xbx=nx+nw-16, xby=ny+6;
            if (rect_contains(rect_make(xbx,xby,12,12), m->x, m->y)) {
                notifs[i].active = false;
                return true;
            }
        }
        slot++;
    }
    return false;
}

/* -- Drop shadow ---------------------------------------------------------- */
void gfx_shadow(i32 x, i32 y, i32 w, i32 h) {
    /* Simple 2px shadow offset in dark color */
    u32 sc = rgb(0x05,0x05,0x08);
    gfx_rect(x+2, y+h, w, 2, sc);
    gfx_rect(x+w, y+2, 2, h, sc);
    gfx_dirty(x, y, w+2, h+2);
}

/* Multi-layer shadow for focused/elevated windows */
void gfx_shadow_ext(i32 x, i32 y, i32 w, i32 h, i32 depth) {
    if (depth <= 0) return;
    for (i32 d = depth; d >= 1; d--) {
        /* Fade from pure black (0) at d=1 to desktop color (0x13,0x15,0x1F) at d=depth */
        u32 pct = (d * 100) / depth; 
        u8 r = (u8)((0x13 * pct) / 100);
        u8 g = (u8)((0x15 * pct) / 100);
        u8 b = (u8)((0x1F * pct) / 100);
        u32 sc = rgb(r, g, b);
        /* Bottom edge */
        gfx_rect(x + d, y + h + (d-1), w, 1, sc);
        /* Right edge */
        gfx_rect(x + w + (d-1), y + d, 1, h, sc);
    }
    gfx_dirty(x, y, w + depth, h + depth);
}

/* -- Bold text rendering (double-strike) ---------------------------------- */
void gfx_str_bold(i32 x, i32 y, const char *s, u32 fg, u32 bg) {
    gfx_str(x, y, s, fg, bg);
    gfx_str(x+1, y, s, fg, COL_TRANSPARENT);
}
