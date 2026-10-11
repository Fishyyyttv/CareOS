/* web/engine/font.c -- TTF faces via stb_truetype, metrics + glyph cache. */
#include "engine.h"

#define STBTT_STATIC
#define STBTT_assert(x) ((void)0)
#define STB_TRUETYPE_IMPLEMENTATION
#include "../third_party/stb_truetype.h"

#define ADV_CACHE_CP 0x3000          /* codepoints with cached glyph/advance */

typedef struct {
    bool  tried, ok;
    stbtt_fontinfo info;
    int   ascent, descent, linegap, upem;
    i32  *gidx;                       /* ADV_CACHE_CP entries, -1 unknown */
    i16  *adv;                        /* unscaled advance */
} face_t;

static face_t g_faces[WEB_FONT_COUNT];

struct font {
    int   face;
    float px, scale;
    float asc, desc, lh;
};

#define FONT_SLOTS 1024
static font_t g_fonts[FONT_SLOTS];
static int    g_nfonts;

static face_t *face_load(int fi) {
    face_t *f = &g_faces[fi];
    if (f->tried) return f->ok ? f : NULL;
    f->tried = true;
    size_t len = 0;
    const unsigned char *data = g_web.font_data ? g_web.font_data(g_web.ctx, fi, &len) : NULL;
    if (!data || !stbtt_InitFont(&f->info, data, stbtt_GetFontOffsetForIndex(data, 0))) return NULL;
    int a, d, g;
    if (!stbtt_GetFontVMetricsOS2(&f->info, &a, &d, &g)) stbtt_GetFontVMetrics(&f->info, &a, &d, &g);
    f->ascent = a; f->descent = d; f->linegap = g;
    f->upem = (int)(1.0f / stbtt_ScaleForMappingEmToPixels(&f->info, 1.0f) + 0.5f);
    f->gidx = (i32 *)malloc(sizeof(i32) * ADV_CACHE_CP);
    f->adv  = (i16 *)malloc(sizeof(i16) * ADV_CACHE_CP);
    if (!f->gidx || !f->adv) return NULL;
    for (int i = 0; i < ADV_CACHE_CP; i++) f->gidx[i] = -1;
    f->ok = true;
    return f;
}

static int pick_face(u8 family, u16 weight) {
    bool bold = weight >= 600;
    int want = (family == FF_MONO) ? (bold ? WEB_FONT_MONO_BOLD : WEB_FONT_MONO)
                                   : (bold ? WEB_FONT_SANS_BOLD : WEB_FONT_SANS);
    if (face_load(want)) return want;
    if (face_load(WEB_FONT_SANS)) return WEB_FONT_SANS;
    for (int i = 0; i < WEB_FONT_COUNT; i++) if (face_load(i)) return i;
    return -1;
}

font_t *font_get(u8 family, u16 weight, u8 italic, float px) {
    (void)italic;
    if (px < 1) px = 1;
    if (px > 400) px = 400;
    px = floorf(px * 4.0f + 0.5f) / 4.0f;
    int fi = pick_face(family, weight);
    for (int i = 0; i < g_nfonts; i++)
        if (g_fonts[i].face == fi && g_fonts[i].px == px) return &g_fonts[i];
    /* The page-building task and the UI task both call this: fill the entry
     * completely, then publish it by bumping the count. Slots are never
     * reused (1024 distinct sizes is far beyond any page). */
    if (g_nfonts >= FONT_SLOTS) return &g_fonts[0];
    font_t *f = &g_fonts[g_nfonts];
    f->face = fi; f->px = px;
    if (fi < 0) { f->scale = 0; f->asc = px * 0.8f; f->desc = px * 0.2f; f->lh = px * 1.2f; }
    else {
        face_t *fc = &g_faces[fi];
        f->scale = stbtt_ScaleForMappingEmToPixels(&fc->info, px);
        f->asc  = fc->ascent * f->scale;
        f->desc = -fc->descent * f->scale;
        f->lh   = (fc->ascent - fc->descent + fc->linegap) * f->scale;
    }
    __sync_synchronize();
    g_nfonts++;
    return f;
}

float font_ascent(font_t *f)    { return f->asc; }
float font_descent(font_t *f)   { return f->desc; }
float font_normal_lh(font_t *f) { return f->lh; }
float font_size(font_t *f)      { return f->px; }

static int glyph_of(face_t *fc, u32 cp, int *adv_unscaled) {
    if (cp < ADV_CACHE_CP) {
        if (fc->gidx[cp] < 0) {
            int g = stbtt_FindGlyphIndex(&fc->info, (int)cp);
            int a, lsb; stbtt_GetGlyphHMetrics(&fc->info, g, &a, &lsb);
            fc->adv[cp] = (i16)a;               /* advance first: gidx publishes */
            __sync_synchronize();
            fc->gidx[cp] = g;
        }
        *adv_unscaled = fc->adv[cp];
        return fc->gidx[cp];
    }
    int g = stbtt_FindGlyphIndex(&fc->info, (int)cp);
    int a, lsb; stbtt_GetGlyphHMetrics(&fc->info, g, &a, &lsb);
    *adv_unscaled = a;
    return g;
}

static u32 map_cp(u32 cp) {
    if (cp == 0xA0) return ' ';          /* nbsp */
    if (cp == 0xAD) return 0;            /* soft hyphen: invisible */
    if (cp == 0x200B || cp == 0x200C || cp == 0x200D || cp == 0xFEFF) return 0;
    return cp;
}

float font_text_width(font_t *f, const char *s, u32 n, float ls) {
    if (f->face < 0) return n * f->px * 0.5f;
    face_t *fc = &g_faces[f->face];
    const char *p = s, *end = s + n;
    float w = 0;
    while (p < end) {
        u32 cp = map_cp(utf8_next(&p, end));
        if (!cp) continue;
        int a; glyph_of(fc, cp, &a);
        w += a * f->scale + ls;
    }
    return w;
}

/* ---- glyph bitmap cache ---- */
typedef struct {
    int face; float px; int glyph;
    int w, h, xo, yo;
    u8 *bmp;
} gcache_t;

#define GC_SIZE 4096
static gcache_t g_gc[GC_SIZE];

static gcache_t *glyph_bitmap(font_t *f, int glyph) {
    u32 h = ((u32)glyph * 2654435761u) ^ ((u32)(f->px * 4) * 40503u) ^ ((u32)f->face * 97u);
    gcache_t *e = &g_gc[h & (GC_SIZE - 1)];
    if (e->bmp && e->face == f->face && e->px == f->px && e->glyph == glyph) return e;
    if (e->bmp == (u8 *)1 && e->face == f->face && e->px == f->px && e->glyph == glyph) return e;
    if (e->bmp && e->bmp != (u8 *)1) free(e->bmp);
    face_t *fc = &g_faces[f->face];
    int x0, y0, x1, y1;
    stbtt_GetGlyphBitmapBox(&fc->info, glyph, f->scale, f->scale, &x0, &y0, &x1, &y1);
    e->face = f->face; e->px = f->px; e->glyph = glyph;
    e->w = x1 - x0; e->h = y1 - y0; e->xo = x0; e->yo = y0;
    if (e->w <= 0 || e->h <= 0 || e->w > 1024 || e->h > 1024) { e->bmp = (u8 *)1; e->w = e->h = 0; return e; }
    e->bmp = (u8 *)malloc((size_t)e->w * e->h);
    if (!e->bmp) { e->bmp = (u8 *)1; e->w = e->h = 0; return e; }
    stbtt_MakeGlyphBitmap(&fc->info, e->bmp, e->w, e->h, e->w, f->scale, f->scale, glyph);
    return e;
}

static inline u32 blend(u32 dst, u32 src, u32 a) {
    if (a >= 255) return src | 0xFF000000u;
    u32 ia = 255 - a;
    u32 rb = (((src & 0xFF00FF) * a + (dst & 0xFF00FF) * ia) >> 8) & 0xFF00FF;
    u32 g  = (((src & 0x00FF00) * a + (dst & 0x00FF00) * ia) >> 8) & 0x00FF00;
    return 0xFF000000u | rb | g;
}

void font_draw(font_t *f, u32 *cv, int cw, int ch, int stride, const int clip[4],
               float x, float by, const char *s, u32 n, u32 color, float ls,
               u8 italic, float alpha) {
    (void)cw; (void)ch;
    if (f->face < 0) return;
    u32 ca = (u32)(((color >> 24) & 0xFF) * alpha);
    if (ca == 0) return;
    color &= 0xFFFFFF;
    face_t *fc = &g_faces[f->face];
    const char *p = s, *end = s + n;
    int iy = (int)floorf(by + 0.5f);
    float pen = x;
    while (p < end) {
        u32 cp = map_cp(utf8_next(&p, end));
        if (!cp) continue;
        int a; int g = glyph_of(fc, cp, &a);
        if (cp != ' ') {
            gcache_t *e = glyph_bitmap(f, g);
            if (e->w > 0) {
                int gx = (int)floorf(pen + 0.5f) + e->xo;
                int gy = iy + e->yo;
                for (int r = 0; r < e->h; r++) {
                    int yy = gy + r;
                    if (yy < clip[1] || yy >= clip[3]) continue;
                    int sh = italic ? (int)((float)(-e->yo - r) * 0.2f) : 0;
                    u32 *row = cv + (size_t)yy * stride;
                    const u8 *src = e->bmp + (size_t)r * e->w;
                    for (int c = 0; c < e->w; c++) {
                        u32 cov = src[c];
                        if (!cov) continue;
                        int xx = gx + c + sh;
                        if (xx < clip[0] || xx >= clip[2]) continue;
                        row[xx] = blend(row[xx], color, (cov * ca) / 255);
                    }
                }
            }
        }
        pen += a * f->scale + ls;
    }
}

font_t *style_font(const style_t *st) {
    return font_get(st->font_family, st->font_weight, st->italic, st->font_size);
}

/* Load every face up front (from web_init) so lazy loading never races
 * between the page-building task and the UI task. */
void font_preload(void) {
    for (int i = 0; i < WEB_FONT_COUNT; i++) face_load(i);
}
