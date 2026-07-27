/* =============================================================================
 * CareOS gui/gfx.c  --  buffered framebuffer renderer
 * Draw into a 32-bpp backbuffer, then copy once per frame to the real FB.
 * ============================================================================= */
#include "kernel.h"
#include "gui.h"
#include "font.h"
#define GFX_TRANSPARENT 0xFFFFFFFF  /* internal skip-pixel sentinel */

u32  SCREEN_W     = 1920;
u32  SCREEN_H     = 1080;
u32  SCREEN_PITCH = 1920*4;
u32 *FRAMEBUFFER  = (u32*)0;
u32  GFX_FONT_SCALE = 1;
u32  GFX_FONT_W = 8;
u32  GFX_FONT_H = 13;
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
    if (dirty_full || dirty_count == 0) {
        __m128i *src = (__m128i*)BACKBUFFER;
        __m128i *dst = (__m128i*)FRAMEBUFFER;
        size_t n = (SCREEN_W * SCREEN_H * 4) / 16;
        for (size_t i = 0; i < n; i++) _mm_storeu_si128(dst + i, _mm_loadu_si128(src + i));
    } else {
        for (u32 i = 0; i < dirty_count; i++) {
            dirty_rect_t *d = &dirty_rects[i];
            for (i32 y = d->y; y < d->y + d->h; y++) {
                __m128i *src = (__m128i*)&BACKBUFFER[y * SCREEN_W + d->x];
                __m128i *dst = (__m128i*)&FRAMEBUFFER[y * SCREEN_W + d->x];
                i32 nw = d->w / 4;
                for (i32 x = 0; x < nw; x++) _mm_storeu_si128(dst + x, _mm_loadu_si128(src + x));
            }
        }
    }
}
#endif

void gfx_flip(void) {
    if (!FRAMEBUFFER || !BACKBUFFER) return;
#ifdef __SSE2__
    gfx_flip_sse2();
#else
    if (dirty_full || dirty_count == 0) {
        kmemcpy(FRAMEBUFFER, BACKBUFFER, SCREEN_W * SCREEN_H * 4);
    } else {
        for (u32 i = 0; i < dirty_count; i++) {
            dirty_rect_t *d = &dirty_rects[i];
            for (i32 y = d->y; y < d->y + d->h; y++) {
                kmemcpy(&FRAMEBUFFER[y * SCREEN_W + d->x], &BACKBUFFER[y * SCREEN_W + d->x], d->w * 4);
            }
        }
    }
#endif
    dirty_reset();
}

/* -- Wallpaper cache ------------------------------------------------------
 * The desktop wallpaper is the same every frame until the theme, accent, or
 * resolution changes -- yet the old renderer paid for it in full on every
 * frame: a cover-fitted full-screen image scale (gfx_draw_image_cover), or the
 * procedural fallback with several 800x600 alpha-blend passes. On a software-
 * emulated GPU that per-frame cost is exactly what made the pointer lag while
 * moving, because every mouse packet forced a from-scratch recomposite.
 *
 * Compose the wallpaper once, keep the pixels, and blit them back at the top of
 * each frame (one memcpy) instead of re-rendering. Invalidated on theme/accent
 * change; reallocated by gfx_init() on a resolution change. If the cache buffer
 * cannot be allocated, blit reports failure and the caller falls back to the
 * original per-frame draw, so this is a pure fast-path with a safe fallback. */
static u32 *WALLPAPER_CACHE     = (u32*)0;
static bool wallpaper_cache_ok  = false;

void gfx_wallpaper_cache_invalidate(void) { wallpaper_cache_ok = false; }

bool gfx_wallpaper_cache_blit(void) {
    if (!wallpaper_cache_ok || !WALLPAPER_CACHE || !BACKBUFFER) return false;
    kmemcpy(BACKBUFFER, WALLPAPER_CACHE, (size_t)SCREEN_W * SCREEN_H * 4);
    dirty_full = true;
    return true;
}

void gfx_wallpaper_cache_capture(void) {
    if (!WALLPAPER_CACHE || !BACKBUFFER) return;
    kmemcpy(WALLPAPER_CACHE, BACKBUFFER, (size_t)SCREEN_W * SCREEN_H * 4);
    wallpaper_cache_ok = true;
}

static u32 *SCREEN_FB;
static u32  SCREEN_W_VAL, SCREEN_H_VAL, SCREEN_P;

gfx_buffer_t g_screen_buf;
gfx_buffer_t *g_target = &g_screen_buf;

void gfx_set_target(gfx_buffer_t *target) {
    if (!target) g_target = &g_screen_buf;
    else g_target = target;
}

void gfx_init(u32 *fb, u32 w, u32 h, u32 pitch) {
    font_init();
    SCREEN_FB = fb; SCREEN_W_VAL = w; SCREEN_H_VAL = h; SCREEN_P = pitch;
    SCREEN_W = w; SCREEN_H = h; SCREEN_PITCH = pitch; FRAMEBUFFER = fb;
    GFX_FONT_SCALE = (h >= 1800) ? 2 : 1;
    
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

    /* Full-screen wallpaper cache. Same size as the backbuffer; a NULL result
     * just disables the fast path (see gfx_wallpaper_cache_blit). */
    WALLPAPER_CACHE    = (u32*)kmalloc(sz);
    wallpaper_cache_ok = false;

    dirty_reset();
    dirty_full = true;
    gfx_clear(0);
    gfx_flip();
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

void gfx_rect_alpha(i32 x, i32 y, i32 w, i32 h, u32 color, u8 alpha) {
    gfx_rect_blend(x, y, w, h, color, alpha);
}

void gfx_rect_outline(i32 x, i32 y, i32 w, i32 h, u32 color) {
    gfx_hline(x, y, w, color); gfx_hline(x, y+h-1, w, color);
    gfx_vline(x, y, h, color); gfx_vline(x+w-1, y, h, color);
}

void gfx_shadow(i32 x, i32 y, i32 w, i32 h) {
    gfx_rect_blend(x + 4, y + 4, w, h, 0, 40);
}

void gfx_shadow_ext(i32 x, i32 y, i32 w, i32 h, u32 alpha) {
    gfx_rect_blend(x, y, w, h, 0, (u8)alpha);
}

/* ── CDL primitives ───────────────────────────────────────────────────────
 * The shared foundation the rest of the design language is built on: a soft
 * shadow, a real blur, and a glass panel, plus the rounded-shape blended fill
 * they all lean on. Kept together so the "floating surface" look is defined in
 * exactly one place. */

u32 gfx_isqrt(u32 v) {
    if (v == 0) return 0;
    u32 x = v, y = (x + 1) / 2;
    while (y < x) { x = y; y = (x + v / x) / 2; }
    return x;
}

/* One blended horizontal span, honouring the clip rect, no per-span dirty. */
static inline void blend_span(i32 x, i32 y, i32 len, u32 color, u32 a) {
    if (!g_target || !g_target->pixels || len <= 0) return;
    if (y < 0 || y >= (i32)g_target->h) return;
    i32 x1 = x < 0 ? 0 : x;
    i32 x2 = x + len; if (x2 > (i32)g_target->w) x2 = (i32)g_target->w;
    if (clip_active) {
        if (y < clip_y || y >= clip_y + clip_h) return;
        if (x1 < clip_x)          x1 = clip_x;
        if (x2 > clip_x + clip_w) x2 = clip_x + clip_w;
    }
    if (x1 >= x2) return;
    u32 inv = 256 - a;
    u32 rf = (color >> 16) & 0xFF, gf = (color >> 8) & 0xFF, bf = color & 0xFF;
    u32 *row = &g_target->pixels[y * (g_target->pitch / 4) + x1];
    for (i32 i = x1; i < x2; i++) {
        u32 bg = *row;
        u32 rr = (((bg >> 16) & 0xFF) * inv + rf * a) >> 8;
        u32 gg = (((bg >>  8) & 0xFF) * inv + gf * a) >> 8;
        u32 bb = (( bg        & 0xFF) * inv + bf * a) >> 8;
        *row++ = (rr << 16) | (gg << 8) | bb;
    }
}

void gfx_rect_rounded_blend(i32 x, i32 y, i32 w, i32 h, i32 r, u32 color, u8 alpha) {
    if (w <= 0 || h <= 0 || alpha == 0) return;
    if (r <= 0) { gfx_rect_blend(x, y, w, h, color, alpha); return; }
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    u32 a = alpha;
    for (i32 row = 0; row < h; row++) {
        i32 dy = -1;
        if (row < r)          dy = (r - 1) - row;
        else if (row >= h - r) dy = row - (h - r);
        i32 inset = 0;
        if (dy >= 0) inset = r - (i32)gfx_isqrt((u32)(r * r - dy * dy));
        blend_span(x + inset, y + row, w - 2 * inset, color, a);
    }
    gfx_dirty(x, y, w, h);
}

/* Separable box blur (horizontal then vertical) of the target in place. Cheap
 * enough for panel-sized regions; not meant for full-screen every frame. */
void gfx_blur_region(i32 x, i32 y, i32 w, i32 h, i32 rad) {
    if (rad < 1 || !g_target || !g_target->pixels) return;
    i32 W = (i32)g_target->w, H = (i32)g_target->h, st = (i32)(g_target->pitch / 4);
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > W) w = W - x;
    if (y + h > H) h = H - y;
    if (w <= 0 || h <= 0) return;
    static u32 line[4096];              /* IMAGE_MAX_DIM ceiling */
    if (w > 4096 || h > 4096) return;

    for (i32 j = 0; j < h; j++) {
        u32 *rowp = &g_target->pixels[(y + j) * st + x];
        for (i32 i = 0; i < w; i++) line[i] = rowp[i];
        for (i32 i = 0; i < w; i++) {
            i32 a = i - rad; if (a < 0) a = 0;
            i32 b = i + rad; if (b >= w) b = w - 1;
            u32 sr = 0, sg = 0, sb = 0, n = 0;
            for (i32 k = a; k <= b; k++) {
                u32 c = line[k]; sr += (c >> 16) & 0xFF; sg += (c >> 8) & 0xFF; sb += c & 0xFF; n++;
            }
            rowp[i] = ((sr / n) << 16) | ((sg / n) << 8) | (sb / n);
        }
    }
    for (i32 i = 0; i < w; i++) {
        for (i32 j = 0; j < h; j++) line[j] = g_target->pixels[(y + j) * st + x + i];
        for (i32 j = 0; j < h; j++) {
            i32 a = j - rad; if (a < 0) a = 0;
            i32 b = j + rad; if (b >= h) b = h - 1;
            u32 sr = 0, sg = 0, sb = 0, n = 0;
            for (i32 k = a; k <= b; k++) {
                u32 c = line[k]; sr += (c >> 16) & 0xFF; sg += (c >> 8) & 0xFF; sb += c & 0xFF; n++;
            }
            g_target->pixels[(y + j) * st + x + i] = ((sr / n) << 16) | ((sg / n) << 8) | (sb / n);
        }
    }
    gfx_dirty(x, y, w, h);
}

void gfx_shadow_soft_ex(i32 x, i32 y, i32 w, i32 h, i32 r, i32 blur, u8 alpha, i32 dy) {
    if (w <= 0 || h <= 0 || blur <= 0) return;
    const i32 L = 10;                   /* stacked rings → smooth falloff */
    u32 denom = (u32)(L * (L + 1) / 2);  /* Σ weights, so total ink ≈ alpha */
    for (i32 i = L; i >= 1; i--) {
        i32 s = blur * i / L;            /* outer rings reach further, fainter */
        u32 a = (u32)alpha * (u32)(L - i + 1) / denom;
        if (a == 0) a = 1;
        gfx_rect_rounded_blend(x - s, y - s + dy, w + 2 * s, h + 2 * s, r + s,
                               COL_SHADOW, (u8)a);
    }
}

void gfx_shadow_soft(i32 x, i32 y, i32 w, i32 h, i32 r) {
    gfx_shadow_soft_ex(x, y, w, h, r, CDL_SHADOW_BLUR, CDL_SHADOW_ALPHA, CDL_SHADOW_DY);
}

void gfx_glass_panel_ex(i32 x, i32 y, i32 w, i32 h, i32 r, u32 tint, u8 alpha, i32 blur) {
    gfx_blur_region(x, y, w, h, blur);
    gfx_rect_rounded_blend(x, y, w, h, r, tint, alpha);
    /* Top sheen sells the "frosted" read; brighter on dark themes. */
    gfx_rect_rounded_blend(x + 1, y + 1, w - 2, h / 3, r, COL_WHITE,
                           g_theme->is_dark ? 14 : 24);
    gfx_rect_rounded_outline(x, y, w, h, r, COL_BORDER);
}

void gfx_glass_panel(i32 x, i32 y, i32 w, i32 h, i32 r) {
    gfx_glass_panel_ex(x, y, w, h, r, COL_GLASS_TINT, CDL_GLASS_ALPHA, CDL_GLASS_BLUR);
}

/* Ease-out cubic mapped to 0..256 fixed-point; saturates at duration. */
u32 cdl_ease_out(u32 elapsed_ms, u32 duration_ms) {
    if (duration_ms == 0 || elapsed_ms >= duration_ms) return 256;
    u32 t = elapsed_ms * 256 / duration_ms;   /* 0..256 */
    u32 inv = 256 - t;                          /* (1-t) */
    u32 cube = (inv * inv / 256) * inv / 256;   /* (1-t)^3 */
    return 256 - cube;                          /* 1-(1-t)^3 */
}

i32 cdl_lerp(i32 a, i32 b, u32 t256) {
    return a + (i32)(((i64)(b - a) * (i32)t256) / 256);
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


/* Blit one glyph. (x,y) is the TOP-LEFT of the text cell, never the baseline;
 * every existing caller depends on that. */
static void glyph_blit(const font_face_t *f, const glyph_t *g,
                       i32 x, i32 y, u32 fg, i32 sc) {
    i32 ox = x + (i32)g->bearing_x * sc;
    i32 oy = y + ((i32)f->baseline - (i32)g->bearing_y) * sc;
    for (i32 row = 0; row < (i32)g->h; row++) {
        const u8 *src = &f->coverage[g->offset + (u32)row * g->w];
        for (i32 col = 0; col < (i32)g->w; col++) {
            u8 a = src[col];
            if (!a) continue;
            for (i32 sy = 0; sy < sc; sy++)
                for (i32 sx = 0; sx < sc; sx++)
                    gfx_setpixel_blend(ox + col*sc + sx, oy + row*sc + sy, fg, a);
        }
    }
}

/* Opaque text background. gfx_rect() fills through gfx_hline_raw(), which
 * clamps to the target but does NOT honour the clip rectangle -- the old
 * renderer painted its background through gfx_setpixel(), which did. Intersect
 * here so clipped text (e.g. a long filename in Files->Rename) cannot paint a
 * band past its window edge. Kept local: gfx_hline_raw has too many callers to
 * change safely. */
static void text_bg_fill(i32 x, i32 y, i32 w, i32 h, u32 col) {
    if (w <= 0 || h <= 0) return;
    if (clip_active) {
        i32 x2 = x + w, y2 = y + h;
        if (x  < clip_x)          x  = clip_x;
        if (y  < clip_y)          y  = clip_y;
        if (x2 > clip_x + clip_w) x2 = clip_x + clip_w;
        if (y2 > clip_y + clip_h) y2 = clip_y + clip_h;
        if (x2 <= x || y2 <= y) return;
        w = x2 - x; h = y2 - y;
    }
    gfx_rect(x, y, w, h, col);
}

static void draw_text(i32 x, i32 y, const char *s, u32 fg, u32 bg,
                      font_size_t size, bool bold) {
    const font_face_t *f = bold ? font_bold_face_at((u32)size)
                                : font_face_at((u32)size);
    i32 sc = (i32)GFX_FONT_SCALE;
    i32 adv = (i32)f->advance * sc;
    i32 lh  = (i32)f->line_h  * sc;
    i32 cy = y, start_x = x, start_y = y, max_x = x;

    while (*s) {
        const char *p;
        i32 n = 0, right = 0;

        /* Measure the line: how many cells, and how far ink actually reaches.
         * Some faces have bearing_x + w > advance, so ink can outrun the cells. */
        for (p = s; *p && *p != '\n'; p++) {
            if (*p >= FONT_FIRST_CH && *p <= FONT_LAST_CH) {
                const glyph_t *g = &f->glyphs[(u8)*p - FONT_FIRST_CH];
                i32 r = n * adv + ((i32)g->bearing_x + (i32)g->w) * sc;
                if (r > right) right = r;
            }
            n++;
        }
        if (n * adv > right) right = n * adv;

        /* One background band for the whole line. Filling per character would
         * let cell N's background erase cell N-1's rightmost antialiased
         * column, and would emit two gfx_dirty calls per character -- enough
         * to overrun MAX_DIRTY after ~16 characters. */
        if (bg != COL_TRANSPARENT) text_bg_fill(x, cy, n * adv, lh, bg);

        i32 cx = x;
        for (p = s; *p && *p != '\n'; p++) {
            if (*p >= FONT_FIRST_CH && *p <= FONT_LAST_CH)
                glyph_blit(f, &f->glyphs[(u8)*p - FONT_FIRST_CH], cx, cy, fg, sc);
            cx += adv;
        }

        if (x + right > max_x) max_x = x + right;
        s = p;
        if (*s == '\n') { s++; cy += lh; }
    }
    gfx_dirty(start_x, start_y, max_x - start_x, (cy - start_y) + lh);
}

void gfx_char(i32 x, i32 y, char c, u32 fg, u32 bg) {
    if (c < FONT_FIRST_CH || c > FONT_LAST_CH) c = '?';
    char buf[2] = { c, '\0' };
    draw_text(x, y, buf, fg, bg, FONT_BODY, false);
}

void gfx_str_ex(i32 x, i32 y, const char *s, u32 fg, u32 bg, font_size_t size) {
    draw_text(x, y, s, fg, bg, size, false);
}

void gfx_str_bold(i32 x, i32 y, const char *s, u32 fg, u32 bg) {
    draw_text(x, y, s, fg, bg, FONT_BODY, true);
}

i32 gfx_str_width_ex(const char *s, font_size_t size) {
    return (i32)(kstrlen(s) * font_face_at((u32)size)->advance * GFX_FONT_SCALE);
}

i32 gfx_line_h_ex(font_size_t size) {
    return (i32)font_face_at((u32)size)->line_h * (i32)GFX_FONT_SCALE;
}

void gfx_str(i32 x, i32 y, const char *s, u32 fg, u32 bg) {
    gfx_str_ex(x, y, s, fg, bg, FONT_BODY);
}

void gfx_str_centered_ex(i32 x, i32 y, i32 w, const char *s, u32 fg, u32 bg, font_size_t size) {
    gfx_str_ex(x + (w - gfx_str_width_ex(s, size)) / 2, y, s, fg, bg, size);
}

void gfx_str_centered(i32 x, i32 y, i32 w, const char *s, u32 fg, u32 bg) {
    gfx_str_centered_ex(x, y, w, s, fg, bg, FONT_BODY);
}

void gfx_str_bg_none(i32 x, i32 y, const char *s, u32 fg) {
    gfx_str_ex(x, y, s, fg, COL_TRANSPARENT, FONT_BODY);
}

void gfx_set_clip(i32 x,i32 y,i32 w,i32 h){ clip_x=x;clip_y=y;clip_w=w;clip_h=h;clip_active=true; }
void gfx_clear_clip(void){ clip_active=false; }

/* Read back the active clip rect. The image blitter resolves the clip once per
 * draw into a bounds box instead of testing it per pixel, which is the whole
 * difference between an affordable and an unaffordable full-screen wallpaper.
 * Returns false (and leaves *out untouched) when no clip is set. */
bool gfx_get_clip(rect_t *out){
    if (!clip_active) return false;
    if (out) { out->x = clip_x; out->y = clip_y; out->w = clip_w; out->h = clip_h; }
    return true;
}
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
    if (w <= 0 || h <= 0) return;
    if (r <= 0) { gfx_rect_outline(x, y, w, h, c); return; }
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;

    gfx_hline(x + r, y, w - 2 * r, c);
    gfx_hline(x + r, y + h - 1, w - 2 * r, c);
    gfx_vline(x, y + r, h - 2 * r, c);
    gfx_vline(x + w - 1, y + r, h - 2 * r, c);

    i32 cx1 = x + r,         cy1 = y + r;
    i32 cx2 = x + w - r - 1, cy2 = y + r;
    i32 cx3 = x + r,         cy3 = y + h - r - 1;
    i32 cx4 = x + w - r - 1, cy4 = y + h - r - 1;
    i32 px = r, py = 0, err = 0;
    while (px >= py) {
        gfx_setpixel(cx1 - px, cy1 - py, c); gfx_setpixel(cx1 - py, cy1 - px, c);
        gfx_setpixel(cx2 + px, cy2 - py, c); gfx_setpixel(cx2 + py, cy2 - px, c);
        gfx_setpixel(cx3 - px, cy3 + py, c); gfx_setpixel(cx3 - py, cy3 + px, c);
        gfx_setpixel(cx4 + px, cy4 + py, c); gfx_setpixel(cx4 + py, cy4 + px, c);
        if (err <= 0) { py++; err += 2 * py + 1; }
        else { px--; err -= 2 * px + 1; }
    }
    gfx_dirty(x, y, w, h);
}

i32 gfx_str_width(const char *s){ return gfx_str_width_ex(s, FONT_BODY); }
void gfx_str_right(i32 x, i32 y, i32 w, const char *s, u32 fg, u32 bg){ i32 sw=gfx_str_width(s); gfx_str(x+w-sw,y,s,fg,bg); }
void gfx_str_clipped(i32 x, i32 y, i32 maxw, const char *s, u32 fg, u32 bg){
    i32 cw = (i32)font_face_at((u32)FONT_BODY)->advance * (i32)GFX_FONT_SCALE;
    if (cw <= 0) return;
    int maxchars = maxw / cw;
    i32 px = x;
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
    gfx_rect_rounded(x, y, w, h, CDL_R_BUTTON, bg_col);
    gfx_rect_blend(x + 1, y + 1, w - 2, h / 3, COL_WHITE, b->hover ? 10 : 5);
    gfx_rect_rounded_outline(x, y, w, h, CDL_R_BUTTON, border);
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
    gfx_rect_rounded(x, y, w, h, CDL_R_INPUT, bg);
    if (t->focused) gfx_rect_blend(x + 1, y + 1, w - 2, h / 3, COL_WHITE, 8);
    gfx_rect_rounded_outline(x, y, w, h, CDL_R_INPUT, border_col);
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
bool notify_active(void) { for (int i = 0; i < MAX_NOTIFICATIONS; i++) if (notifs[i].active) return true; return false; }
void notify_draw(void) {
    /* Toast geometry follows the live font: an H3 title over a BODY line.
     * Both the box height and the title/body split have to track those two
     * line heights, or the title runs into the body (JetBrains Mono H3 is 21px
     * against the 13px the old bitmap face used).
     *
     * The stack also starts below TOPBAR_H. gui_run() paints the menu bar
     * after wm_draw_all() -> notify_draw(), so anything drawn above that line
     * is covered by an opaque bar. */
    i32 pad      = 12;
    i32 title_lh = gfx_line_h_ex(FONT_H3);
    i32 body_lh  = gfx_line_h_ex(FONT_BODY);
    i32 nw = 264;
    i32 nh = pad + title_lh + 4 + body_lh + pad;
    int slot = 0;
    for (int i = 0; i < MAX_NOTIFICATIONS; i++) {
        if (!notifs[i].active) continue;
        i32 nx = (i32)SCREEN_W - nw - 12;
        i32 ny = TOPBAR_H + 12 + slot*(nh+8);
        u32 now = timer_get_ticks(); u32 age = now - notifs[i].born_tick; u32 life = 500;
        u32 pct = (age < life) ? (100 * (life - age) / life) : 0;
        /* Floating frosted card: soft shadow, blurred wallpaper, tint, hairline. */
        gfx_shadow_soft(nx, ny, nw, nh, CDL_R_CARD);
        gfx_glass_panel(nx, ny, nw, nh, CDL_R_CARD);
        /* Accent dot + title, body, and a thin accent life bar along the bottom. */
        gfx_circle_fill(nx + 18, ny + pad + title_lh / 2, 4,
                        notifs[i].icon_color ? notifs[i].icon_color : COL_ACCENT);
        gfx_str_ex(nx+32, ny+pad, notifs[i].title, COL_TEXT, COL_TRANSPARENT, FONT_H3);
        gfx_str_clipped(nx+32, ny+pad+title_lh+4, nw-44, notifs[i].body, COL_DIM, COL_TRANSPARENT);
        gfx_rect_rounded(nx + 12, ny + nh - 5, (nw - 24) * (i32)pct / 100, 2, 1, COL_ACCENT);
        slot++;
    }
}

bool notify_handle_mouse(mouse_t *m) {
    (void)m;
    return false;
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
