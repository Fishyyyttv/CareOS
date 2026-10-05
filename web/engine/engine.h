/* web/engine/engine.h -- internal engine types shared by the engine units. */
#ifndef WEB_ENGINE_H
#define WEB_ENGINE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include <lexbor/html/html.h>
#include <lexbor/css/css.h>
#include <lexbor/style/style.h>

#include "web.h"

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int32_t  i32;
typedef int16_t  i16;

extern web_platform_t g_web;
void web_log(const char *fmt, ...);

/* ---------- small utils (util.c) ---------- */
char  *w_strndup(const char *s, size_t n);
char  *w_strdup(const char *s);
bool   w_ieq(const char *a, const char *b);                 /* ASCII case-insens */
bool   w_ieqn(const char *a, size_t an, const char *b);     /* a has length an */
const char *w_istr(const char *hay, size_t hlen, const char *needle);
void   url_resolve(const char *base, const char *ref, char *out, size_t cap);
u32    utf8_next(const char **p, const char *end);
unsigned char *data_url_decode(const char *url, size_t *len);

/* ---------- lengths ---------- */
enum { L_AUTO = 0, L_PX, L_PCT, L_NONE, L_FR, L_EM_UNRESOLVED };
typedef struct { float v; float pc_px; u8 u; } len_t;   /* L_PCT: v% + pc_px */

static inline float len_px(len_t l, float pct_base, float auto_val) {
    if (l.u == L_PX)  return l.v;
    if (l.u == L_PCT) return pct_base >= 0 ? l.v * pct_base / 100.0f + l.pc_px : auto_val;
    return auto_val;
}

/* ---------- computed style (css.c) ---------- */
enum { D_NONE = 0, D_BLOCK, D_INLINE, D_INLINE_BLOCK, D_LIST_ITEM, D_FLEX,
       D_INLINE_FLEX, D_GRID, D_INLINE_GRID, D_TABLE, D_INLINE_TABLE,
       D_TABLE_ROW, D_TABLE_CELL, D_TABLE_ROW_GROUP, D_TABLE_CAPTION,
       D_TABLE_COLUMN, D_CONTENTS, D_FLOW_ROOT };
enum { POS_STATIC = 0, POS_RELATIVE, POS_ABSOLUTE, POS_FIXED, POS_STICKY };
enum { FL_NONE = 0, FL_LEFT, FL_RIGHT };
enum { CL_NONE = 0, CL_LEFT, CL_RIGHT, CL_BOTH };
enum { WS_NORMAL = 0, WS_NOWRAP, WS_PRE, WS_PRE_WRAP, WS_PRE_LINE };
enum { TA_LEFT = 0, TA_RIGHT, TA_CENTER, TA_JUSTIFY };
enum { TT_NONE = 0, TT_UPPER, TT_LOWER, TT_CAP };
enum { FF_SANS = 0, FF_SERIF, FF_MONO };
enum { DECO_UNDER = 1, DECO_OVER = 2, DECO_THROUGH = 4 };
enum { BS_NONE = 0, BS_SOLID, BS_DASHED, BS_DOTTED, BS_DOUBLE, BS_OTHER };
enum { OV_VISIBLE = 0, OV_HIDDEN, OV_SCROLL };
enum { VA_BASELINE = 0, VA_MIDDLE, VA_TOP, VA_BOTTOM, VA_SUB, VA_SUPER,
       VA_TEXT_TOP, VA_TEXT_BOTTOM };
enum { LS_NONE = 0, LS_DISC, LS_CIRCLE, LS_SQUARE, LS_DECIMAL, LS_LOWER_ALPHA,
       LS_UPPER_ALPHA, LS_LOWER_ROMAN, LS_UPPER_ROMAN };
enum { JC_START = 0, JC_END, JC_CENTER, JC_BETWEEN, JC_AROUND, JC_EVENLY,
       JC_STRETCH, JC_BASELINE };   /* shared by justify-* and align-* */
enum { BOX_CONTENT = 0, BOX_BORDER };
enum { BGR_REPEAT = 0, BGR_NO_REPEAT, BGR_REPEAT_X, BGR_REPEAT_Y };
enum { BGS_AUTO = 0, BGS_COVER, BGS_CONTAIN, BGS_LEN };

#define GRID_MAX_TRACKS 24
typedef struct {
    u8    n;                 /* explicit tracks */
    u8    auto_fill;         /* repeat(auto-fill/auto-fit, minmax(min,..)) */
    float fill_min;          /* px for auto-fill */
    len_t t[GRID_MAX_TRACKS];/* L_PX / L_PCT / L_FR / L_AUTO */
    float tmin[GRID_MAX_TRACKS]; /* minmax lower bound (px) */
} grid_tracks_t;

typedef struct style {
    u8    display, position, float_, clear, box_sizing, visibility;
    u8    overflow, white_space, text_align, text_transform, font_family;
    u8    italic, deco, vertical_align, list_style, list_inside;
    u8    border_style[4];
    u8    flex_dir, flex_wrap, justify, align_items, align_self, align_content;
    u8    bg_repeat, bg_size_mode, bg_grad, nowrap_ellipsis, border_collapse;
    u8    is_link, cursor_pointer, deco_owner, valign_set, break_word, object_fit;
    u16   font_weight;
    i32   z_index;  bool z_auto;
    i32   order;
    float font_size, line_height;          /* line_height < 0 => normal */
    float lh_factor;                        /* >0: unitless line-height factor */
    float aspect;                           /* aspect-ratio w/h, 0 = none */
    float letter_spacing, word_spacing, text_indent;
    float border_w[4];
    float radius[4];                        /* tl tr br bl */
    float opacity;
    float grow, shrink, row_gap, col_gap, border_spacing;
    len_t width, height, min_w, min_h, max_w, max_h, basis;
    len_t margin[4], padding[4], inset[4];  /* top right bottom left */
    len_t bg_pos[2], bg_size[2];
    u32   color, bg_color, deco_color;
    u32   border_color[4];
    u32   grad_from, grad_to; u8 grad_dir;  /* 0 = to bottom, 1 = to right */
    u32   shadow_color; float shadow_blur, shadow_x, shadow_y;
    char *bg_image;                         /* resolved URL or NULL */
    grid_tracks_t grid_cols;
    i32   grid_col_span, grid_col_start, grid_row_span;
    const char *custom_props;               /* "--a:..;--b:.." inherited blob */
} style_t;

/* ---------- images (image.c) ---------- */
typedef struct web_img {
    char  *url;
    volatile int state;          /* 0 pending, 1 loaded, 2 failed */
    int    w, h;                 /* natural size */
    u32   *px;                   /* 0xAARRGGBB, w*h */
    /* cached scaled copy for the last display size */
    int    sw, sh; u32 *spx;
    struct web_img *next;
} web_img_t;

/* ---------- boxes ---------- */
enum { K_ELEM = 0, K_TEXT, K_BR, K_REPLACED, K_MARKER };
enum { IN_FLOW = 0, IN_FLOW_ROOT, IN_FLEX, IN_GRID, IN_TABLE, IN_ROW, IN_CELL,
       IN_ROWGROUP };

typedef struct box box_t;

/* A fragment of an inline formatting context, positioned relative to the
 * block container that owns the lines. */
typedef struct {
    float x, y, w, h;            /* y = top of the fragment's own box */
    float base;                  /* baseline offset from y */
    box_t *box;                  /* text box / atomic box / br */
    const char *s; u32 n;        /* text slice (text fragments) */
    u8    atomic, hyphen;
} frag_t;

typedef struct {                 /* inline element background/border per line */
    box_t *box; float x0, x1, y, h;
    u8 first, last;
} ideco_t;

struct box {
    box_t *parent, *first, *last, *next, *prev;
    style_t *st;
    lxb_dom_element_t *el;       /* NULL for text/anonymous */
    u8   kind, inner, outer_inline, anon;
    u8   is_float, is_abs, is_fixed, has_inline_kids;
    /* text */
    char *text; u32 tlen;
    /* replaced */
    web_img_t *img; float iw, ih; u8 rkind; char *alt;
    /* layout output (relative to parent's border-box origin) */
    float x, y, w, h;            /* border box */
    float m[4], b[4], p[4];      /* used margin / border / padding */
    float rel_x, rel_y;          /* position:relative offset */
    float baseline;              /* first line baseline from border-box top */
    float min_c, max_c; u8 intr_ok;
    /* inline content */
    frag_t  *frags; u32 nfrag, capfrag;
    ideco_t *decos; u32 ndeco, capdeco;
    /* table */
    i32 colspan, rowspan;
    /* list */
    i32 list_index;
    float scroll_h;              /* bottom of descendant overflow (for page h) */
};

/* ---------- page ---------- */
struct web_page {
    char  url[1024];
    char  title[256];
    lxb_html_document_t *doc;
    box_t *root;
    style_t *root_style;
    int   vw, vh;                /* viewport the layout was made for */
    float doc_h, doc_w;
    volatile int need_layout;
    web_img_t *imgs; int nimg_pending;
    lxb_css_parser_t *css_parser;
    lxb_css_stylesheet_t *sheets[64]; int nsheets;
    /* arena for styles + boxes */
    void **blocks; size_t nblocks, capblocks;
    char  *arena; size_t arena_used, arena_cap;
    u32   bg;                    /* canvas background */
};
typedef struct web_page page_t;

void *pg_alloc(page_t *pg, size_t n);       /* zeroed, freed with the page */

/* css.c */
void   css_load_document_styles(page_t *pg);
style_t *css_compute(page_t *pg, lxb_dom_element_t *el, const style_t *parent);
style_t *css_anon_style(page_t *pg, const style_t *parent, u8 display);
style_t *css_initial_root(page_t *pg);
bool   css_parse_color(const char *s, size_t n, u32 *out, u32 current);

/* box.c */
box_t *box_build(page_t *pg);

/* layout.c */
void layout_document(page_t *pg);
void layout_block_box(page_t *pg, box_t *b, float cb_w, float cb_h);
void box_intrinsic(page_t *pg, box_t *b, float *minw, float *maxw);
void resolve_box_edges(box_t *b, float cb_w);
void layout_box_with_width(page_t *pg, box_t *b, float bw, float cbw, float cbh);
void shift_contents(box_t *b, float dy);
float box_used_width(box_t *b, float cb_w, bool *is_auto);
float box_used_height(box_t *b, float cb_h, bool *is_auto);

/* flex.c / grid.c / table.c */
void layout_flex(page_t *pg, box_t *b, float content_w, float content_h_or_neg);
void layout_grid(page_t *pg, box_t *b, float content_w);
void layout_table(page_t *pg, box_t *b, float avail_w);
void flex_intrinsic(page_t *pg, box_t *b, float *minw, float *maxw);
void table_intrinsic(page_t *pg, box_t *b, float *minw, float *maxw);

/* font.c */
typedef struct font font_t;
font_t *font_get(u8 family, u16 weight, u8 italic, float px);
float   font_text_width(font_t *f, const char *s, u32 n, float letter_sp);
float   font_ascent(font_t *f);
float   font_descent(font_t *f);         /* positive */
float   font_normal_lh(font_t *f);
float   font_size(font_t *f);
void    font_draw(font_t *f, u32 *canvas, int cw, int ch, int stride,
                  const int clip[4], float x, float baseline_y,
                  const char *s, u32 n, u32 color, float letter_sp, u8 italic,
                  float alpha);
font_t *style_font(const style_t *st);

/* paint.c */
typedef struct {
    u32 *px; int w, h, stride;
    int  ox, oy;                 /* document coord of canvas (0,0) */
    int  clip[4];                /* x0 y0 x1 y1 in canvas coords */
    float alpha;
} canvas_t;
void paint_page(page_t *pg, canvas_t *c);
void cv_fill(canvas_t *c, float x, float y, float w, float h, u32 color);
void cv_fill_rrect(canvas_t *c, float x, float y, float w, float h,
                   const float r[4], u32 color);

/* image.c */
web_img_t *img_request(page_t *pg, const char *url);
bool img_load(page_t *pg, web_img_t *im);
const u32 *img_scaled(web_img_t *im, int w, int h);
void img_free_all(page_t *pg);

/* text helpers */
static inline bool is_ws(u32 c) { return c == ' ' || c == '\n' || c == '\t' || c == '\r' || c == '\f'; }

#endif
