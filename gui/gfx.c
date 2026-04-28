/* =============================================================================
 * CareOS gui/gfx.c  --  buffered framebuffer renderer
 * Draw into a 32-bpp backbuffer, then copy once per frame to the real FB.
 * ============================================================================= */
#include "kernel.h"
#include "gui.h"
#define GFX_TRANSPARENT 0xFFFFFFFF  /* internal skip-pixel sentinel */

u32  SCREEN_W     = 800;
u32  SCREEN_H     = 600;
u32  SCREEN_PITCH = 800*4;
u32 *FRAMEBUFFER  = (u32*)0;
u32  GFX_FONT_SCALE = 1;
static u32 FB_BPP        = 32;
static u32 *BACKBUFFER   = (u32*)0;
static bool gfx_direct_mode = false;

/* -- B-01: Per-channel bit shifts for the hardware pixel format ----------- */
static u8 FB_R_SHIFT = 16;
static u8 FB_G_SHIFT =  8;
static u8 FB_B_SHIFT =  0;

static i32  clip_x = 0, clip_y = 0, clip_w = 0, clip_h = 0;
static bool clip_active = false;

void gfx_set_pixel_format(u8 r_shift, u8 g_shift, u8 b_shift) {
    FB_R_SHIFT = r_shift;
    FB_G_SHIFT = g_shift;
    FB_B_SHIFT = b_shift;
}

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

/* -- Dirty-rectangle tracker ---------------------------------------------- */
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
    for (u32 i = 0; i < dirty_count; i++) {
        dirty_rect_t *d = &dirty_rects[i];
        if (x >= d->x && y >= d->y && x+w <= d->x+d->w && y+h <= d->y+d->h) return;
    }
    if (dirty_count >= MAX_DIRTY) { dirty_full = true; return; }
    dirty_rects[dirty_count++] = (dirty_rect_t){x, y, w, h};
}

#ifdef __SSE2__
#include <immintrin.h>
static void gfx_flip_sse2(void) {
    __m128i *src = (__m128i*)BACKBUFFER;
    __m128i *dst = (__m128i*)FRAMEBUFFER;
    size_t n = (SCREEN_W * SCREEN_H * 4) / 16;
    for (size_t i = 0; i < n; i++) _mm_storeu_si128(dst + i, _mm_loadu_si128(src + i));
}
#endif

static u32 *SCREEN_FB;
static u32  SCREEN_W_VAL, SCREEN_H_VAL, SCREEN_P;

static gfx_buffer_t g_screen_buf;
static gfx_buffer_t *g_target = &g_screen_buf;

void gfx_set_target(gfx_buffer_t *target) {
    if (!target) g_target = &g_screen_buf;
    else g_target = target;
}

void gfx_init(u32 *fb, u32 w, u32 h, u32 pitch) {
    SCREEN_FB = fb; SCREEN_W_VAL = w; SCREEN_H_VAL = h; SCREEN_P = pitch;
    SCREEN_W = w; SCREEN_H = h; SCREEN_PITCH = pitch; FRAMEBUFFER = fb;
    GFX_FONT_SCALE = (h >= 900) ? 2 : 1;
    
    g_screen_buf.pixels = fb;
    g_screen_buf.w = w;
    g_screen_buf.h = h;
    g_screen_buf.pitch = pitch;
    g_target = &g_screen_buf;

    u32 bytes_per_px = pitch / w;
    FB_BPP = (bytes_per_px >= 4) ? 32 : 24;
    size_t sz = (size_t)(w * h * sizeof(u32));
    sz = (sz + 15) & ~15u;
    BACKBUFFER = (u32*)kmalloc(sz);
    if (!BACKBUFFER) {
        gfx_direct_mode = true;
        BACKBUFFER = FRAMEBUFFER;
    } else {
        g_screen_buf.pixels = BACKBUFFER;
        g_screen_buf.pitch  = SCREEN_W * 4;
    }
    dirty_reset();
    dirty_full = true;
    gfx_clear(0);
    gfx_flip();
}

void gfx_flip(void) {
    if (!FRAMEBUFFER || !BACKBUFFER) return;
    if (gfx_direct_mode) { dirty_reset(); return; }
    if (FB_BPP == 32) {
#ifdef __SSE2__
        if (!dirty_full && dirty_count == 0) { dirty_reset(); return; }
        if (dirty_full) { gfx_flip_sse2(); } else {
            for (u32 d = 0; d < dirty_count; d++) {
                dirty_rect_t *dr = &dirty_rects[d];
                for (i32 row = 0; row < dr->h; row++) {
                    u32 *src = BACKBUFFER  + ((u32)(dr->y + row)) * SCREEN_W + (u32)dr->x;
                    u8  *dst = (u8*)FRAMEBUFFER + ((u32)(dr->y + row)) * SCREEN_PITCH + (u32)dr->x * 4;
                    kmemcpy(dst, src, (size_t)dr->w * 4);
                }
            }
        }
#else
        if (dirty_full) { kmemcpy(FRAMEBUFFER, BACKBUFFER, (size_t)(SCREEN_W * SCREEN_H * sizeof(u32))); } else {
            for (u32 d = 0; d < dirty_count; d++) {
                dirty_rect_t *dr = &dirty_rects[d];
                for (i32 row = 0; row < dr->h; row++) {
                    u32 *src = BACKBUFFER  + ((u32)(dr->y + row)) * SCREEN_W + (u32)dr->x;
                    u8  *dst = (u8*)FRAMEBUFFER + ((u32)(dr->y + row)) * SCREEN_PITCH + (u32)dr->x * 4;
                    kmemcpy(dst, src, (size_t)dr->w * 4);
                }
            }
        }
#endif
        dirty_reset(); return;
    }
    for (u32 y = 0; y < SCREEN_H; y++) {
        u32 *src = BACKBUFFER + y * SCREEN_W;
        for (u32 x = 0; x < SCREEN_W; x++) fb_write_pixel(x, y, src[x]);
    }
    dirty_reset();
}

void gfx_clear(u32 color) {
    if (!BACKBUFFER) return;
    u32 count = SCREEN_W * SCREEN_H;
    u8 r = (color >> 16) & 0xFF;
    u8 g = (color >> 8) & 0xFF;
    u8 b = color & 0xFF;
    if (r == g && g == b) {
        kmemset(BACKBUFFER, r, count * 4);
    } else {
        for (u32 i = 0; i < count; i++) BACKBUFFER[i] = color;
    }
    dirty_full = true;
}

static inline u32 color_blend(u32 bg, u32 fg, u8 alpha) {
    if (alpha >= 250) return fg;
    if (alpha <= 5)   return bg;
    u32 a = (u32)alpha;
    u32 inv_a = 256 - a;
    u32 r = (((bg >> 16) & 0xFF) * inv_a + ((fg >> 16) & 0xFF) * a) >> 8;
    u32 g = (((bg >> 8) & 0xFF) * inv_a + ((fg >> 8) & 0xFF) * a) >> 8;
    u32 b = ((bg & 0xFF) * inv_a + (fg & 0xFF) * a) >> 8;
    return (r << 16) | (g << 8) | b;
}

void gfx_setpixel(i32 x, i32 y, u32 col) {
    if (!g_target) return;
    if (x < 0 || x >= (i32)g_target->w || y < 0 || y >= (i32)g_target->h) return;
    if (clip_active && (x < clip_x || x >= clip_x + clip_w || y < clip_y || y >= clip_y + clip_h)) return;
    g_target->pixels[y * (g_target->pitch / 4) + x] = col;
}

static inline void gfx_setpixel_blend(i32 x, i32 y, u32 col, u8 alpha) {
    if (!g_target) return;
    if (x < 0 || x >= (i32)g_target->w || y < 0 || y >= (i32)g_target->h) return;
    if (clip_active && (x < clip_x || x >= clip_x + clip_w || y < clip_y || y >= clip_y + clip_h)) return;
    u32 *p = &g_target->pixels[y * (g_target->pitch / 4) + x];
    *p = color_blend(*p, col, alpha);
}

static inline void gfx_hline_raw(i32 x, i32 y, i32 len, u32 color) {
    if (!g_target || !g_target->pixels) return;
    if (y < 0 || (u32)y >= g_target->h || len <= 0) return;
    i32 x1 = x < 0 ? 0 : x;
    i32 x2 = (x+len) > (i32)g_target->w ? (i32)g_target->w : (x+len);
    if (x1 >= x2) return;
    u32 *row = g_target->pixels + (u32)y * (g_target->pitch / 4) + (u32)x1;
    i32 n = x2 - x1; while (n-- > 0) *row++ = color;
}

void gfx_hline(i32 x, i32 y, i32 len, u32 color) {
    gfx_hline_raw(x, y, len, color);
    i32 x1 = x < 0 ? 0 : x;
    i32 x2 = (x+len) > (i32)SCREEN_W ? (i32)SCREEN_W : (x+len);
    if (x1 < x2 && y >= 0 && (u32)y < SCREEN_H) gfx_dirty(x1, y, x2 - x1, 1);
}

void gfx_vline(i32 x, i32 y, i32 len, u32 color) {
    for (i32 i = 0; i < len; i++) gfx_setpixel(x, y+i, color);
    gfx_dirty(x, y, 1, len);
}

void gfx_rect(i32 x, i32 y, i32 w, i32 h, u32 color) {
    for (i32 i = 0; i < h; i++) gfx_hline_raw(x, y+i, w, color);
    gfx_dirty(x, y, w, h);
}

void gfx_rect_blend(i32 x, i32 y, i32 w, i32 h, u32 color, u8 alpha) {
    if (alpha <= 2) return;
    if (alpha >= 253) { gfx_rect(x, y, w, h, color); return; }
    if (!g_target || !g_target->pixels) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (i32)g_target->w) w = (i32)g_target->w - x;
    if (y + h > (i32)g_target->h) h = (i32)g_target->h - y;
    if (w <= 0 || h <= 0) return;

    u32 a = (u32)alpha;
    u32 inv_a = 256 - a;
    u32 r_fg = (color >> 16) & 0xFF;
    u32 g_fg = (color >> 8) & 0xFF;
    u32 b_fg = color & 0xFF;
    u32 stride = g_target->pitch / 4;

    for (i32 i = y; i < y + h; i++) {
        u32 *row = &g_target->pixels[i * stride + x];
        for (i32 j = 0; j < w; j++) {
            u32 bg = row[j];
            u32 r = (((bg >> 16) & 0xFF) * inv_a + r_fg * a) >> 8;
            u32 g = (((bg >> 8) & 0xFF) * inv_a + g_fg * a) >> 8;
            u32 b = ((bg & 0xFF) * inv_a + b_fg * a) >> 8;
            row[j] = (r << 16) | (g << 8) | b;
        }
    }
    gfx_dirty(x, y, w, h);
}

void gfx_rect_outline(i32 x, i32 y, i32 w, i32 h, u32 color) {
    gfx_hline(x, y, w, color); gfx_hline(x, y+h-1, w, color);
    gfx_vline(x, y, h, color); gfx_vline(x+w-1, y, h, color);
}

void gfx_rect_rounded(i32 x, i32 y, i32 w, i32 h, i32 r, u32 color) {
    if (r <= 0) { gfx_rect(x,y,w,h,color); return; }
    gfx_rect(x+r, y,   w-2*r, h,   color);
    gfx_rect(x,   y+r, r,   h-2*r, color);
    gfx_rect(x+w-r, y+r, r, h-2*r, color);
    gfx_circle_fill(x+r, y+r, r, color);
    gfx_circle_fill(x+w-r-1, y+r, r, color);
    gfx_circle_fill(x+r, y+h-r-1, r, color);
    gfx_circle_fill(x+w-r-1, y+h-r-1, r, color);
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

/* -- New High-Fidelity 8x12 Sans-Serif Font -------------------------------- */
static const u8 font_data[95][12] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 32   */
    {0x00,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00,0x18,0x18,0x00}, /* 33 ! */
    {0x00,0x36,0x36,0x36,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 34 " */
    {0x00,0x36,0x36,0x7F,0x36,0x7F,0x36,0x36,0x36,0x00,0x00,0x00}, /* 35 # */
    {0x00,0x18,0x3E,0x60,0x3C,0x06,0x7C,0x18,0x00,0x00,0x00,0x00}, /* 36 $ */
    {0x00,0x63,0x63,0x06,0x0C,0x18,0x30,0x63,0x63,0x00,0x00,0x00}, /* 37 % */
    {0x00,0x38,0x6C,0x6C,0x38,0x76,0x6C,0x6C,0x3B,0x00,0x00,0x00}, /* 38 & */
    {0x00,0x18,0x18,0x0C,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 39 ' */
    {0x00,0x0C,0x18,0x30,0x30,0x30,0x30,0x30,0x30,0x18,0x0C,0x00}, /* 40 ( */
    {0x00,0x30,0x18,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x18,0x30,0x00}, /* 41 ) */
    {0x00,0x00,0x18,0x5A,0x3C,0x3C,0x5A,0x18,0x00,0x00,0x00,0x00}, /* 42 * */
    {0x00,0x00,0x18,0x18,0x18,0x7E,0x18,0x18,0x18,0x00,0x00,0x00}, /* 43 + */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x0C,0x00}, /* 44 , */
    {0x00,0x00,0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00,0x00,0x00}, /* 45 - */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00}, /* 46 . */
    {0x00,0x02,0x06,0x0C,0x18,0x30,0x60,0xC0,0x80,0x00,0x00,0x00}, /* 47 / */
    {0x00,0x3C,0x66,0x66,0x6E,0x76,0x66,0x66,0x3C,0x00,0x00,0x00}, /* 48 0 */
    {0x00,0x18,0x38,0x18,0x18,0x18,0x18,0x18,0x7E,0x00,0x00,0x00}, /* 49 1 */
    {0x00,0x3C,0x66,0x06,0x0C,0x18,0x30,0x60,0x7E,0x00,0x00,0x00}, /* 50 2 */
    {0x00,0x3C,0x66,0x06,0x1C,0x06,0x06,0x66,0x3C,0x00,0x00,0x00}, /* 51 3 */
    {0x00,0x0C,0x1C,0x3C,0x6C,0x6C,0x7F,0x0C,0x0C,0x00,0x00,0x00}, /* 52 4 */
    {0x00,0x7E,0x60,0x60,0x7C,0x06,0x06,0x66,0x3C,0x00,0x00,0x00}, /* 53 5 */
    {0x00,0x1C,0x30,0x60,0x7C,0x66,0x66,0x66,0x3C,0x00,0x00,0x00}, /* 54 6 */
    {0x00,0x7E,0x06,0x0C,0x0C,0x18,0x18,0x30,0x30,0x00,0x00,0x00}, /* 55 7 */
    {0x00,0x3C,0x66,0x66,0x3C,0x66,0x66,0x66,0x3C,0x00,0x00,0x00}, /* 56 8 */
    {0x00,0x3C,0x66,0x66,0x66,0x3E,0x06,0x0C,0x38,0x00,0x00,0x00}, /* 57 9 */
    {0x00,0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x00}, /* 58 : */
    {0x00,0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x0C,0x00,0x00,0x00}, /* 59 ; */
    {0x00,0x06,0x0C,0x18,0x30,0x60,0x30,0x18,0x0C,0x06,0x00,0x00}, /* 60 < */
    {0x00,0x00,0x00,0x7E,0x00,0x00,0x7E,0x00,0x00,0x00,0x00,0x00}, /* 61 = */
    {0x00,0x60,0x30,0x18,0x0C,0x06,0x0C,0x18,0x30,0x60,0x00,0x00}, /* 62 > */
    {0x00,0x3C,0x66,0x06,0x0C,0x18,0x18,0x00,0x18,0x18,0x00,0x00}, /* 63 ? */
    {0x00,0x3C,0x66,0x6E,0x6E,0x6E,0x60,0x62,0x3C,0x00,0x00,0x00}, /* 64 @ */
    {0x00,0x18,0x3C,0x66,0x66,0x7E,0x66,0x66,0x66,0x00,0x00,0x00}, /* 65 A */
    {0x00,0x7C,0x66,0x66,0x7C,0x66,0x66,0x66,0x7C,0x00,0x00,0x00}, /* 66 B */
    {0x00,0x3C,0x66,0x60,0x60,0x60,0x60,0x66,0x3C,0x00,0x00,0x00}, /* 67 C */
    {0x00,0x78,0x6C,0x66,0x66,0x66,0x66,0x6C,0x78,0x00,0x00,0x00}, /* 68 D */
    {0x00,0x7E,0x60,0x60,0x7C,0x60,0x60,0x60,0x7E,0x00,0x00,0x00}, /* 69 E */
    {0x00,0x7E,0x60,0x60,0x7C,0x60,0x60,0x60,0x60,0x00,0x00,0x00}, /* 70 F */
    {0x00,0x3C,0x66,0x60,0x60,0x6E,0x66,0x66,0x3E,0x00,0x00,0x00}, /* 71 G */
    {0x00,0x66,0x66,0x66,0x7E,0x66,0x66,0x66,0x66,0x00,0x00,0x00}, /* 72 H */
    {0x00,0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x7E,0x00,0x00,0x00}, /* 73 I */
    {0x00,0x1E,0x06,0x06,0x06,0x06,0x06,0x66,0x3C,0x00,0x00,0x00}, /* 74 J */
    {0x00,0x66,0x6C,0x78,0x70,0x78,0x6C,0x66,0x66,0x00,0x00,0x00}, /* 75 K */
    {0x00,0x60,0x60,0x60,0x60,0x60,0x60,0x60,0x7E,0x00,0x00,0x00}, /* 76 L */
    {0x00,0x63,0x77,0x7F,0x6B,0x63,0x63,0x63,0x63,0x00,0x00,0x00}, /* 77 M */
    {0x00,0x66,0x76,0x7E,0x7E,0x6E,0x66,0x66,0x66,0x00,0x00,0x00}, /* 78 N */
    {0x00,0x3C,0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0x00,0x00,0x00}, /* 79 O */
    {0x00,0x7C,0x66,0x66,0x7C,0x60,0x60,0x60,0x60,0x00,0x00,0x00}, /* 80 P */
    {0x00,0x3C,0x66,0x66,0x66,0x66,0x66,0x6E,0x3C,0x0E,0x00,0x00}, /* 81 Q */
    {0x00,0x7C,0x66,0x66,0x7C,0x78,0x6C,0x66,0x66,0x00,0x00,0x00}, /* 82 R */
    {0x00,0x3C,0x66,0x60,0x3C,0x06,0x06,0x66,0x3C,0x00,0x00,0x00}, /* 83 S */
    {0x00,0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00,0x00,0x00}, /* 84 T */
    {0x00,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0x00,0x00,0x00}, /* 85 U */
    {0x00,0x66,0x66,0x66,0x66,0x66,0x3C,0x18,0x18,0x00,0x00,0x00}, /* 86 V */
    {0x00,0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x63,0x00,0x00,0x00}, /* 87 W */
    {0x00,0x66,0x66,0x3C,0x18,0x18,0x3C,0x66,0x66,0x00,0x00,0x00}, /* 88 X */
    {0x00,0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0x18,0x00,0x00,0x00}, /* 89 Y */
    {0x00,0x7E,0x06,0x0C,0x18,0x30,0x60,0x60,0x7E,0x00,0x00,0x00}, /* 90 Z */
    {0x00,0x3C,0x30,0x30,0x30,0x30,0x30,0x30,0x3C,0x00,0x00,0x00}, /* 91 [ */
    {0x00,0x80,0x40,0x20,0x10,0x08,0x04,0x02,0x01,0x00,0x00,0x00}, /* 92 \ */
    {0x00,0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00,0x00,0x00}, /* 93 ] */
    {0x00,0x18,0x3C,0x66,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 94 ^ */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0x00}, /* 95 _ */
    {0x00,0x18,0x18,0x30,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 96 ` */
    {0x00,0x00,0x00,0x3C,0x06,0x3E,0x66,0x66,0x3B,0x00,0x00,0x00}, /* 97 a */
    {0x00,0x60,0x60,0x7C,0x66,0x66,0x66,0x66,0x7C,0x00,0x00,0x00}, /* 98 b */
    {0x00,0x00,0x00,0x3C,0x66,0x60,0x60,0x66,0x3C,0x00,0x00,0x00}, /* 99 c */
    {0x00,0x06,0x06,0x3E,0x66,0x66,0x66,0x66,0x3B,0x00,0x00,0x00}, /* 100 d */
    {0x00,0x00,0x00,0x3C,0x66,0x7E,0x60,0x66,0x3C,0x00,0x00,0x00}, /* 101 e */
    {0x00,0x1C,0x36,0x30,0x7C,0x30,0x30,0x30,0x30,0x00,0x00,0x00}, /* 102 f */
    {0x00,0x00,0x00,0x3B,0x66,0x66,0x3E,0x06,0x3C,0x00,0x00,0x00}, /* 103 g */
    {0x00,0x60,0x60,0x7C,0x66,0x66,0x66,0x66,0x66,0x00,0x00,0x00}, /* 104 h */
    {0x00,0x18,0x18,0x00,0x18,0x18,0x18,0x18,0x18,0x00,0x00,0x00}, /* 105 i */
    {0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x0C,0x0C,0x6C,0x38,0x00,0x00}, /* 106 j */
    {0x00,0x60,0x60,0x66,0x6C,0x78,0x6C,0x66,0x66,0x00,0x00,0x00}, /* 107 k */
    {0x00,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00,0x00,0x00}, /* 108 l */
    {0x00,0x00,0x00,0x6E,0x7F,0x6B,0x6B,0x6B,0x6B,0x00,0x00,0x00}, /* 109 m */
    {0x00,0x00,0x00,0x7C,0x66,0x66,0x66,0x66,0x66,0x00,0x00,0x00}, /* 110 n */
    {0x00,0x00,0x00,0x3C,0x66,0x66,0x66,0x66,0x3C,0x00,0x00,0x00}, /* 111 o */
    {0x00,0x00,0x00,0x7C,0x66,0x66,0x7C,0x60,0x60,0x60,0x00,0x00}, /* 112 p */
    {0x00,0x00,0x00,0x3E,0x66,0x66,0x3E,0x06,0x06,0x06,0x00,0x00}, /* 113 q */
    {0x00,0x00,0x00,0x7C,0x66,0x60,0x60,0x60,0x60,0x00,0x00,0x00}, /* 114 r */
    {0x00,0x00,0x00,0x3E,0x60,0x3C,0x06,0x7C,0x00,0x00,0x00,0x00}, /* 115 s */
    {0x00,0x30,0x30,0x7C,0x30,0x30,0x30,0x30,0x1C,0x00,0x00,0x00}, /* 116 t */
    {0x00,0x00,0x00,0x66,0x66,0x66,0x66,0x66,0x3B,0x00,0x00,0x00}, /* 117 u */
    {0x00,0x00,0x00,0x66,0x66,0x66,0x66,0x3C,0x18,0x00,0x00,0x00}, /* 118 v */
    {0x00,0x00,0x00,0x63,0x6B,0x6B,0x7F,0x63,0x63,0x00,0x00,0x00}, /* 119 w */
    {0x00,0x00,0x00,0x66,0x3C,0x18,0x3C,0x66,0x66,0x00,0x00,0x00}, /* 120 x */
    {0x00,0x00,0x00,0x66,0x66,0x66,0x3E,0x06,0x3C,0x00,0x00,0x00}, /* 121 y */
    {0x00,0x00,0x00,0x7E,0x0C,0x18,0x30,0x60,0x7E,0x00,0x00,0x00}, /* 122 z */
    {0x00,0x0C,0x18,0x18,0x30,0x18,0x18,0x0C,0x00,0x00,0x00,0x00}, /* 123 { */
    {0x00,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00,0x00,0x00}, /* 124 | */
    {0x00,0x30,0x18,0x18,0x0C,0x18,0x18,0x30,0x00,0x00,0x00,0x00}, /* 125 } */
    {0x00,0x36,0x5C,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 126 ~ */
};

void gfx_char(i32 x, i32 y, char c, u32 fg, u32 bg) {
    if (c < 32 || c > 126) c = '?';
    bool skip_bg = (bg == COL_TRANSPARENT);
    i32 sc = (i32)GFX_FONT_SCALE;
    const u8 *g = font_data[(u8)c - 32];
    for (i32 row = 0; row < 12; row++) {
        u8 bits = g[row];
        for (i32 col = 0; col < 8; col++) {
            bool lit = (bits >> (7 - col)) & 1;
            if (skip_bg && !lit) continue;
            u32 color = lit ? fg : bg;
            for (i32 sy = 0; sy < sc; sy++)
                for (i32 sx = 0; sx < sc; sx++)
                    gfx_setpixel(x + col * sc + sx, y + row * sc + sy, color);
        }
    }
    gfx_dirty(x, y, 8 * sc, 12 * sc);
}

void gfx_str_ex(i32 x, i32 y, const char *s, u32 fg, u32 bg, font_size_t size) {
    i32 cx = x;
    i32 sc = (i32)GFX_FONT_SCALE;
    /* Improved scaling: H1 is 2x larger, Body is 1x. Tripling (3x) was too blocky. */
    if      (size == FONT_H1) sc *= 2;
    else if (size == FONT_H2) sc = (sc * 3) / 2; /* 1.5x scale approx */

    i32 start_x = cx;
    while (*s) {
        if (*s == '\n') { cx = x; y += FONT_H * sc; s++; continue; }
        if (*s >= 32 && *s <= 126) {
            const u8 *g = font_data[(u8)*s - 32];
            for (i32 row = 0; row < 12; row++) {
                u8 bits = g[row];
                for (i32 col = 0; col < 8; col++) {
                    bool lit = (bits >> (7 - col)) & 1;
                    if (lit) {
                        for (i32 sy = 0; sy < sc; sy++)
                            for (i32 sx = 0; sx < sc; sx++)
                                gfx_setpixel(cx + col*sc + sx, y + row*sc + sy, fg);
                    } else if (bg != COL_TRANSPARENT) {
                        for (i32 sy = 0; sy < sc; sy++)
                            for (i32 sx = 0; sx < sc; sx++)
                                gfx_setpixel(cx + col*sc + sx, y + row*sc + sy, bg);
                    }
                }
            }
        }
        cx += FONT_W * sc; s++;
    }
    gfx_dirty(start_x, y, cx - start_x, 12 * sc);
}

void gfx_str(i32 x, i32 y, const char *s, u32 fg, u32 bg) {
    gfx_str_ex(x, y, s, fg, bg, FONT_BODY);
}

void gfx_str_centered_ex(i32 x, i32 y, i32 w, const char *s, u32 fg, u32 bg, font_size_t size) {
    i32 sc = (i32)GFX_FONT_SCALE;
    if (size == FONT_H1) sc *= 2;
    i32 len = (i32)kstrlen(s);
    gfx_str_ex(x + (w - len * FONT_W * sc) / 2, y, s, fg, bg, size);
}

void gfx_str_centered(i32 x, i32 y, i32 w, const char *s, u32 fg, u32 bg) {
    gfx_str_centered_ex(x, y, w, s, fg, bg, FONT_BODY);
}

void gfx_str_bg_none(i32 x, i32 y, const char *s, u32 fg) {
    gfx_str_ex(x, y, s, fg, COL_TRANSPARENT, FONT_BODY);
}

void gfx_set_clip(i32 x,i32 y,i32 w,i32 h){ clip_x=x;clip_y=y;clip_w=w;clip_h=h;clip_active=true; }
void gfx_clear_clip(void){ clip_active=false; }
static inline bool in_clip(i32 x,i32 y){ if(!clip_active) return true; return x>=clip_x&&x<clip_x+clip_w&&y>=clip_y&&y<clip_y+clip_h; }

static inline void put_px(i32 x, i32 y, u32 c){
    if(x<0||y<0||(u32)x>=(u32)SCREEN_W||(u32)y>=(u32)SCREEN_H||!in_clip(x,y)) return;
    BACKBUFFER[y*SCREEN_W+x]=c;
}

u32 gfx_getpixel(i32 x, i32 y){
    if(x<0||y<0||(u32)x>=(u32)SCREEN_W||(u32)y>=(u32)SCREEN_H) return 0;
    return BACKBUFFER[y*SCREEN_W+x];
}

void gfx_line(i32 x0, i32 y0, i32 x1, i32 y1, u32 c){
    int dx=x1-x0<0?x0-x1:x1-x0, dy=y1-y0<0?y0-y1:y1-y0;
    int sx=x0<x1?1:-1, sy=y0<y1?1:-1, err=dx-dy;
    while(1){ put_px(x0,y0,c); if(x0==x1&&y0==y1) break;
        int e2=2*err; if(e2>-dy){err-=dy;x0+=sx;} if(e2<dx){err+=dx;y0+=sy;}
    }
}

void gfx_triangle_fill(i32 x0, i32 y0, i32 x1, i32 y1, i32 x2, i32 y2, u32 c){
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

void gfx_gradient_rect(i32 x, i32 y, i32 w, i32 h, u32 top, u32 bot){
    for(i32 row=0;row<h;row++){
        u32 t=(u32)row*255/(u32)(h?h:1);
        u8 r=(u8)(((top>>16&0xff)*(255-t)+(bot>>16&0xff)*t)>>8);
        u8 g=(u8)(((top>>8&0xff)*(255-t)+(bot>>8&0xff)*t)>>8);
        u8 b=(u8)(((top&0xff)*(255-t)+(bot&0xff)*t)>>8);
        gfx_hline(x,y+row,w,((u32)r<<16)|((u32)g<<8)|b);
    }
}

void gfx_rect_rounded_outline(i32 x, i32 y, i32 w, i32 h, i32 r, u32 c){
    gfx_hline(x+r,y,w-2*r,c); gfx_hline(x+r,y+h-1,w-2*r,c);
    gfx_vline(x,y+r,h-2*r,c); gfx_vline(x+w-1,y+r,h-2*r,c);
}

i32 gfx_str_width(const char *s){ return (i32)(kstrlen(s) * FONT_W * GFX_FONT_SCALE); }
void gfx_str_right(i32 x, i32 y, i32 w, const char *s, u32 fg, u32 bg){ i32 sw=gfx_str_width(s); gfx_str(x+w-sw,y,s,fg,bg); }
void gfx_str_clipped(i32 x, i32 y, i32 maxw, const char *s, u32 fg, u32 bg){
    i32 px = x; i32 cw = (i32)(FONT_W * GFX_FONT_SCALE); int maxchars = maxw / cw;
    for(int i=0;s[i]&&i<maxchars;i++){ gfx_char(px,y,s[i],fg,bg); px+=cw; }
}

void gfx_bar(i32 x, i32 y, i32 w, i32 h, u32 bg, u32 fg, u32 pct){
    gfx_rect(x,y,w,h,bg); gfx_rect_outline(x,y,w,h,COL_BORDER);
    if(pct>100) pct=100; gfx_rect(x+1,y+1,(w-2)*pct/100,h-2,fg);
}

void gfx_draw_icon(app_id_t app, i32 x, i32 y, i32 size, u32 color) {
    i32 r = size / 2; i32 cx = x + r; i32 cy = y + r;
    switch (app) {
    case APP_TERMINAL:
        gfx_rect_rounded(x, y, size, size * 3/4, 4, COL_SURFACE2);
        gfx_rect_rounded_outline(x, y, size, size * 3/4, 4, color);
        gfx_str_ex(x+4, y+4, ">_", color, COL_TRANSPARENT, FONT_CAPTION);
        break;
    case APP_NOTES:
        gfx_rect_rounded(x, y, size * 7/8, size, 2, COL_YELLOW);
        gfx_rect(x, y, size * 7/8, size/6, COL_ORANGE);
        for (int l=0; l<4; l++) gfx_hline(x+4, y + size/4 + l*size/6, size/2, COL_SURFACE);
        break;
    case APP_FILES:
        gfx_rect_rounded(x, y + size/4, size, size * 3/4, 3, color);
        gfx_rect_rounded(x, y + size/8, size/2, size/4, 2, color);
        gfx_rect(x + 2, y + size/3, size/3, size/6, COL_SURFACE2);
        break;
    case APP_SYSMON:
        gfx_rect_rounded(x, y, size, size * 3/4, 4, COL_SURFACE2);
        gfx_rect_rounded_outline(x, y, size, size * 3/4, 4, color);
        for (int b=0; b<5; b++) { i32 bh = size/4 + (b*5) % (size/2); gfx_rect(x + 4 + b*size/6, y + size*2/3 - bh, size/8, bh, color); }
        break;
    case APP_SETTINGS:
        gfx_circle_fill(cx, cy, size/2, COL_SURFACE2);
        gfx_circle(cx, cy, size/2, color);
        gfx_circle_fill(cx, cy, size/5, color);
        break;
    case APP_BROWSER:
        gfx_rect_rounded(x, y, size, size * 3/4, 4, COL_SURFACE2);
        gfx_rect_rounded_outline(x, y, size, size * 3/4, 4, color);
        gfx_rect(x, y, size, size/5, COL_SURFACE3);
        gfx_circle_fill(x + size/10, y + size/10, 2, COL_RED);
        break;
    case APP_PAINT:
        gfx_circle_fill(cx, cy, size/2, COL_WHITE);
        gfx_circle_fill(cx - size/4, cy - size/4, size/4, COL_RED);
        gfx_circle_fill(cx + size/4, cy, size/5, COL_YELLOW);
        gfx_circle_fill(cx, cy + size/4, size/6, COL_PRIMARY);
        break;
    case APP_CALC:
        gfx_rect_rounded(x, y, size, size, 4, COL_SURFACE2);
        gfx_rect_rounded_outline(x, y, size, size, 4, color);
        gfx_rect(x + 4, y + 4, size - 8, size/4, COL_SURFACE3);
        gfx_circle_fill(x + size/3, y + size/2 + 2, 2, color);
        gfx_circle_fill(x + size*2/3, y + size/2 + 2, 2, color);
        gfx_circle_fill(x + size/3, y + size*3/4 + 2, 2, color);
        gfx_circle_fill(x + size*2/3, y + size*3/4 + 2, 2, color);
        break;
    case APP_NETMON:
        gfx_rect_rounded(x, y, size, size * 3/4, 4, COL_SURFACE2);
        gfx_rect_rounded_outline(x, y, size, size * 3/4, 4, color);
        gfx_line(x + 4, y + size/2, x + size/3, y + size/4, COL_GREEN);
        gfx_line(x + size/3, y + size/4, x + size*2/3, y + size/2, COL_GREEN);
        gfx_line(x + size*2/3, y + size/2, x + size - 4, y + size/8, COL_GREEN);
        break;
    case APP_CLOCK:
        gfx_circle_fill(cx, cy, size/2, COL_SURFACE2);
        gfx_circle(cx, cy, size/2, color);
        gfx_line(cx, cy, cx, cy - size/3, color);
        gfx_line(cx, cy, cx + size/4, cy + size/4, COL_ACCENT);
        break;
    case APP_USERS:
        gfx_circle_fill(cx, cy - size/6, size/4, COL_SURFACE2);
        gfx_circle(cx, cy - size/6, size/4, color);
        gfx_circle_fill(cx, cy + size/3, size/2, COL_SURFACE2);
        gfx_circle(cx, cy + size/3, size/2, color);
        break;
    default:
        gfx_circle_fill(cx, cy, size/2, COL_DIM);
        gfx_circle_fill(cx, cy, size/3, COL_SURFACE);
        break;
    }
    gfx_dirty(x, y, size, size);
}

void button_draw(const button_t *b){
    i32 x = b->rect.x, y = b->rect.y, w = b->rect.w, h = b->rect.h;
    u32 base = b->bg ? b->bg : COL_SURFACE2;
    u32 bg_col, border;
    if (b->pressed) { bg_col = g_theme->selection; border = COL_PRIMARY; }
    else if (b->active && b->bg) { bg_col = base; border = COL_PRIMARY; }
    else if (b->hover) { bg_col = COL_HOVER; border = COL_ACCENT; }
    else if (b->active) { bg_col = g_theme->surface3; border = COL_PRIMARY; }
    else { bg_col = base; border = COL_BORDER; }
    gfx_rect_rounded(x, y, w, h, 8, bg_col);
    gfx_rect_blend(x + 1, y + 1, w - 2, h / 3, COL_WHITE, b->hover ? 10 : 5);
    gfx_rect_rounded_outline(x, y, w, h, 8, border);
    if (b->active && !b->bg) gfx_rect_rounded(x + 8, y + h - 3, w - 16, 2, 1, COL_PRIMARY);
    i32 ty = y + (h - (i32)(FONT_H * GFX_FONT_SCALE)) / 2;
    u32 fg = b->fg ? b->fg : COL_TEXT;
    gfx_set_clip(x + 5, y + 2, w - 10, h - 4);
    gfx_str_centered(x, ty, w, b->label, fg, COL_TRANSPARENT);
    gfx_clear_clip();
}
bool button_hit(const button_t *b,i32 mx,i32 my){ return rect_contains(b->rect,mx,my); }
void button_update(button_t *b, const mouse_t *m){ if (!b || !m) return; b->hover = button_hit(b, m->x, m->y); if (!m->left) b->pressed = false; if (b->hover && m->left_clicked) b->pressed = true; }
bool button_take_click(button_t *b, const mouse_t *m){ button_update(b, m); return b && m && b->hover && m->left_clicked; }

void textinput_draw(const textinput_t *t){
    u32 bg = t->focused ? COL_INPUT_BG : g_theme->surface2;
    u32 border_col = t->focused ? COL_PRIMARY : (t->hover ? COL_ACCENT : COL_BORDER);
    i32 x = t->rect.x, y = t->rect.y, w = t->rect.w, h = t->rect.h;
    gfx_rect_rounded(x, y, w, h, 8, bg);
    if (t->focused) gfx_rect_blend(x + 1, y + 1, w - 2, h / 3, COL_WHITE, 8);
    gfx_rect_rounded_outline(x, y, w, h, 8, border_col);
    i32 pad = 10;
    i32 ty  = y + (h - (i32)(FONT_H * GFX_FONT_SCALE)) / 2;
    if (t->len > 0) { gfx_str_clipped(x + pad, ty, w - pad * 2, t->buf, COL_TEXT, COL_TRANSPARENT); }
    else { gfx_str_clipped(x + pad, ty, w - pad * 2, t->placeholder, COL_MUTED, COL_TRANSPARENT); }
    if (t->focused) { i32 cx = x + pad + (i32)(t->cursor * (i32)(FONT_W * GFX_FONT_SCALE)); gfx_vline(cx, y + 4, h - 8, COL_ACCENT); }
}

void textinput_key(textinput_t *t,char c){
    if(c=='\b'){if(t->len>0){t->len--;t->buf[t->len]='\0';if(t->cursor>0)t->cursor--;}}
    else if(c>=32&&c<127&&t->len<TEXTINPUT_MAX-1){ t->buf[t->len++]=c; t->buf[t->len]='\0'; t->cursor++; }
}
void textinput_handle_mouse(textinput_t *t, const mouse_t *m){ if (!t || !m || !m->left_clicked) return; t->focused = rect_contains(t->rect, m->x, m->y); t->hover = t->focused; }

static notification_t notifs[MAX_NOTIFICATIONS];
static bool notif_hover[MAX_NOTIFICATIONS];
void notify_push(const char *title, const char *body, u32 icon_color) {
    int slot = -1; u32 oldest_tick = 0xFFFFFFFF;
    for (int i = 0; i < MAX_NOTIFICATIONS; i++) { if (!notifs[i].active) { slot = i; break; } if (notifs[i].born_tick < oldest_tick) { oldest_tick = notifs[i].born_tick; slot = i; } }
    if (slot < 0) slot = 0;
    kstrncpy(notifs[slot].title, title, 31); kstrncpy(notifs[slot].body, body, 127);
    notifs[slot].icon_color = icon_color; notifs[slot].born_tick = timer_get_ticks(); notifs[slot].active = true; notif_hover[slot] = false;
}
void notify_tick(void) { u32 now = timer_get_ticks(); for (int i = 0; i < MAX_NOTIFICATIONS; i++) if (notifs[i].active && now - notifs[i].born_tick > 500) notifs[i].active = false; }
void notify_draw(void) {
    int slot = 0;
    for (int i = 0; i < MAX_NOTIFICATIONS; i++) {
        if (!notifs[i].active) continue;
        i32 nw=264, nh=62; i32 nx = (i32)SCREEN_W - nw - 12; i32 ny = 12 + slot*(nh+8);
        u32 now = timer_get_ticks(); u32 age = now - notifs[i].born_tick; u32 life = 500;
        u32 pct = (age < life) ? (100 * (life - age) / life) : 0;
        gfx_rect_rounded(nx, ny, nw, nh, 12, COL_SURFACE);
        gfx_rect_rounded_outline(nx, ny, nw, nh, 12, COL_BORDER);
        gfx_str_ex(nx+12, ny+12, notifs[i].title, COL_TEXT, COL_TRANSPARENT, FONT_H3);
        gfx_str_clipped(nx+12, ny+34, nw-24, notifs[i].body, COL_DIM, COL_TRANSPARENT);
        slot++;
    }
}

bool notify_handle_mouse(mouse_t *m) {
    (void)m;
    return false;
}

void gfx_shadow_ext(i32 x, i32 y, i32 w, i32 h, i32 depth) {
    for (i32 i = 0; i < depth; i++) {
        gfx_rect_blend(x + 2 + i, y + 2 + i, w, h, COL_BLACK, (u8)(20 - i * 2));
    }
}

void gfx_shadow(i32 x, i32 y, i32 w, i32 h) {
    gfx_shadow_ext(x, y, w, h, 5);
}

void gfx_blit(gfx_buffer_t *src, i32 dx, i32 dy) {
    if (!src || !src->pixels || !g_target) return;
    i32 sw = (i32)src->w;
    i32 sh = (i32)src->h;
    for (i32 y = 0; y < sh; y++) {
        i32 sy = dy + y;
        if (sy < 0 || sy >= (i32)g_target->h) continue;
        for (i32 x = 0; x < sw; x++) {
            i32 sx = dx + x;
            if (sx < 0 || sx >= (i32)g_target->w) continue;
            u32 col = src->pixels[y * (src->pitch / 4) + x];
            if (col == 0xFFFFFFFF) continue;
            g_target->pixels[sy * (g_target->pitch / 4) + sx] = col;
        }
    }
}


