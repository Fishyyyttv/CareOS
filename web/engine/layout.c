/* web/engine/layout.c -- block & inline formatting, floats, positioning.
 *
 * Box geometry: b->x, b->y is the border-box origin relative to the parent
 * box's border-box origin. Boxes laid out inside an inline formatting
 * context (atomic inlines) are positioned relative to the block container
 * that owns the line boxes (`frags`). After layout, abs_pass() computes
 * document coordinates (ax, ay) and positions absolutely positioned boxes. */
#include "engine.h"

/* ------------------------------------------------------------------ */
/*  Floats / block formatting context                                  */
/* ------------------------------------------------------------------ */

typedef struct { float x, y, w, h; u8 side; } flt_t;
typedef struct bfc {
    flt_t *f; int n, cap;
} bfc_t;

static void bfc_add(bfc_t *bf, float x, float y, float w, float h, u8 side) {
    if (bf->n == bf->cap) {
        int nc = bf->cap ? bf->cap * 2 : 8;
        flt_t *nf = (flt_t *)realloc(bf->f, (size_t)nc * sizeof(flt_t));
        if (!nf) return;
        bf->f = nf; bf->cap = nc;
    }
    bf->f[bf->n++] = (flt_t){ x, y, w, h, side };
}

/* Horizontal space free of floats for the band [y, y+h) within [x0, x1]. */
static void bfc_span(const bfc_t *bf, float y, float h, float x0, float x1, float *l, float *r) {
    *l = x0; *r = x1;
    if (!bf) return;
    if (h < 1) h = 1;
    for (int i = 0; i < bf->n; i++) {
        const flt_t *f = &bf->f[i];
        if (f->y >= y + h || f->y + f->h <= y) continue;
        if (f->side == FL_LEFT) { if (f->x + f->w > *l) *l = f->x + f->w; }
        else { if (f->x < *r) *r = f->x; }
    }
}

static float bfc_clear_y(const bfc_t *bf, u8 clear) {
    float y = -1e9f;
    if (!bf) return y;
    for (int i = 0; i < bf->n; i++) {
        const flt_t *f = &bf->f[i];
        if ((clear == CL_BOTH) || (clear == CL_LEFT && f->side == FL_LEFT) || (clear == CL_RIGHT && f->side == FL_RIGHT))
            if (f->y + f->h > y) y = f->y + f->h;
    }
    return y;
}

static float bfc_bottom(const bfc_t *bf) { return bfc_clear_y(bf, CL_BOTH); }

/* Next y below `y` where some float ends (to retry placement). */
static float bfc_next_edge(const bfc_t *bf, float y) {
    float best = 1e9f;
    for (int i = 0; i < bf->n; i++) {
        float e = bf->f[i].y + bf->f[i].h;
        if (e > y + 0.01f && e < best) best = e;
    }
    return best;
}

/* ------------------------------------------------------------------ */
/*  Box edges and sizes                                                */
/* ------------------------------------------------------------------ */

void resolve_box_edges(box_t *b, float cbw) {
    style_t *st = b->st;
    float base = cbw >= 0 ? cbw : 0;
    for (int i = 0; i < 4; i++) {
        b->m[i] = st->margin[i].u == L_AUTO ? 0 : len_px(st->margin[i], base, 0);
        b->p[i] = len_px(st->padding[i], base, 0);
        if (b->p[i] < 0) b->p[i] = 0;
        b->b[i] = st->border_style[i] != BS_NONE ? st->border_w[i] : 0;
    }
    if (b->kind == K_TEXT || b->kind == K_BR || b->kind == K_MARKER) {
        memset(b->m, 0, sizeof b->m); memset(b->p, 0, sizeof b->p); memset(b->b, 0, sizeof b->b);
    }
}

static inline float hpb(const box_t *b) { return b->p[1] + b->p[3] + b->b[1] + b->b[3]; }
static inline float vpb(const box_t *b) { return b->p[0] + b->p[2] + b->b[0] + b->b[2]; }

/* Specified content width (px) or -1 when auto/unresolvable. */
static float spec_content_w(const box_t *b, float cbw) {
    const style_t *st = b->st;
    if (st->width.u != L_PX && !(st->width.u == L_PCT && cbw >= 0)) return -1;
    float w = len_px(st->width, cbw, -1);
    if (st->box_sizing == BOX_BORDER) w -= hpb(b);
    return w < 0 ? 0 : w;
}

static float spec_content_h(const box_t *b, float cbh) {
    const style_t *st = b->st;
    if (st->height.u != L_PX && !(st->height.u == L_PCT && cbh >= 0)) return -1;
    float h = len_px(st->height, cbh, -1);
    if (st->box_sizing == BOX_BORDER) h -= vpb(b);
    return h < 0 ? 0 : h;
}

static float clamp_w(const box_t *b, float w, float cbw) {
    const style_t *st = b->st;
    float adj = st->box_sizing == BOX_BORDER ? hpb(b) : 0;
    if (st->max_w.u == L_PX || (st->max_w.u == L_PCT && cbw >= 0)) {
        float mx = len_px(st->max_w, cbw, 1e9f) - adj;
        if (w > mx) w = mx;
    }
    if (st->min_w.u == L_PX || (st->min_w.u == L_PCT && cbw >= 0)) {
        float mn = len_px(st->min_w, cbw, 0) - adj;
        if (w < mn) w = mn;
    }
    return w < 0 ? 0 : w;
}

static float clamp_h(const box_t *b, float h, float cbh) {
    const style_t *st = b->st;
    float adj = st->box_sizing == BOX_BORDER ? vpb(b) : 0;
    if (st->max_h.u == L_PX || (st->max_h.u == L_PCT && cbh >= 0)) {
        float mx = len_px(st->max_h, cbh, 1e9f) - adj;
        if (h > mx) h = mx;
    }
    if (st->min_h.u == L_PX || (st->min_h.u == L_PCT && cbh >= 0)) {
        float mn = len_px(st->min_h, cbh, 0) - adj;
        if (h < mn) h = mn;
    }
    return h < 0 ? 0 : h;
}

float box_used_width(box_t *b, float cbw, bool *is_auto) {
    float w = spec_content_w(b, cbw);
    if (is_auto) *is_auto = w < 0;
    return w;
}
float box_used_height(box_t *b, float cbh, bool *is_auto) {
    float h = spec_content_h(b, cbh);
    if (is_auto) *is_auto = h < 0;
    return h;
}

static bool establishes_bfc(const box_t *b) {
    const style_t *st = b->st;
    return b->inner != IN_FLOW || b->is_float || b->is_abs || st->overflow != OV_VISIBLE ||
           b->parent == NULL || st->display == D_FLOW_ROOT || b->outer_inline;
}

/* ------------------------------------------------------------------ */
/*  Replaced elements                                                  */
/* ------------------------------------------------------------------ */

static void replaced_natural(page_t *pg, box_t *b, float *nw, float *nh) {
    (void)pg;
    float w = b->iw, h = b->ih;
    web_img_t *im = b->img;
    bool has_img = im && im->state == 1 && im->w > 0 && im->h > 0;
    if (b->rkind == 1 /* input */) {
        font_t *f = style_font(b->st);
        if (h < 0) h = font_normal_lh(f);
        if (w < 0) w = 150;
    } else if (b->rkind == 4 /* select */) {
        font_t *f = style_font(b->st);
        if (h < 0) h = font_normal_lh(f);
        if (w < 0) w = (b->alt ? font_text_width(f, b->alt, (u32)strlen(b->alt), 0) : 40) + 4;
    } else if (has_img) {
        if (w < 0 && h < 0) { w = (float)im->w; h = (float)im->h; }
        else if (w < 0) w = h * im->w / im->h;
        else if (h < 0) h = w * im->h / im->w;
    } else {
        if (w < 0 && h < 0) {
            if (b->alt && b->rkind == 0) { font_t *f = style_font(b->st); w = font_text_width(f, b->alt, (u32)strlen(b->alt), 0) + 4; h = font_normal_lh(f); if (w > 300) w = 300; }
            else { w = 0; h = 0; }
        } else if (w < 0) w = h;
        else if (h < 0) h = b->rkind == 0 ? w * 0.75f : w;
    }
    *nw = w; *nh = h;
}

/* Used content size of a replaced box. */
static void layout_replaced(page_t *pg, box_t *b, float cbw, float cbh) {
    resolve_box_edges(b, cbw);
    float nw, nh;
    replaced_natural(pg, b, &nw, &nh);
    float ratio = (nw > 0 && nh > 0) ? nw / nh : (b->st->aspect > 0 ? b->st->aspect : 0);
    if (b->st->aspect > 0) ratio = b->st->aspect;
    float w = spec_content_w(b, cbw), h = spec_content_h(b, cbh);
    if (w < 0 && h < 0) { w = nw; h = nh; }
    else if (w < 0) w = ratio > 0 ? h * ratio : nw;
    else if (h < 0) h = ratio > 0 ? w / ratio : nh;
    float w2 = clamp_w(b, w, cbw);
    if (w2 != w && ratio > 0 && spec_content_h(b, cbh) < 0) h = w2 / ratio;
    w = w2;
    h = clamp_h(b, h, cbh);
    b->w = w + hpb(b);
    b->h = h + vpb(b);
    b->baseline = b->h;
    if (b->rkind == 1 || b->rkind == 4) {
        font_t *f = style_font(b->st);
        b->baseline = b->b[0] + b->p[0] + (h - font_normal_lh(f)) / 2 + font_ascent(f) +
                      (font_normal_lh(f) - font_ascent(f) - font_descent(f)) / 2;
    }
}

/* ------------------------------------------------------------------ */
/*  Intrinsic sizes                                                    */
/* ------------------------------------------------------------------ */

static void inline_intrinsic(page_t *pg, box_t *b, float *mn, float *mx, float *line, int depth);

static float margin_px(len_t l) { return l.u == L_PX ? l.v : 0; }

void box_intrinsic(page_t *pg, box_t *b, float *minw, float *maxw) {
    if (b->intr_ok) { *minw = b->min_c; *maxw = b->max_c; return; }
    float mn = 0, mx = 0;
    resolve_box_edges(b, -1);
    float pb = hpb(b);
    float sw = spec_content_w(b, -1);
    if (b->kind == K_REPLACED) {
        layout_replaced(pg, b, -1, -1);
        mn = mx = b->w;
        if (b->st->width.u == L_PCT || b->st->max_w.u == L_PCT) mn = 0;
    } else if (sw >= 0) {
        mn = mx = sw + pb;
    } else {
        if (b->inner == IN_FLEX) flex_intrinsic(pg, b, &mn, &mx);
        else if (b->inner == IN_TABLE) table_intrinsic(pg, b, &mn, &mx);
        else if (b->inner == IN_GRID) {
            int ncols = b->st->grid_cols.n > 0 ? b->st->grid_cols.n : 1;
            for (box_t *c = b->first; c; c = c->next) {
                if (c->is_abs) continue;
                float a, z; box_intrinsic(pg, c, &a, &z);
                float m = margin_px(c->st->margin[1]) + margin_px(c->st->margin[3]);
                if (a + m > mn) mn = a + m;
                if (z + m > mx) mx = z + m;
            }
            mx = mx * ncols + b->st->col_gap * (ncols - 1);
        } else if (b->has_inline_kids) {
            float line = 0;
            inline_intrinsic(pg, b, &mn, &mx, &line, 0);
            if (line > mx) mx = line;
        } else {
            float fl = 0;   /* floats side by side add up */
            for (box_t *c = b->first; c; c = c->next) {
                if (c->is_abs) continue;
                float a, z; box_intrinsic(pg, c, &a, &z);
                float m = margin_px(c->st->margin[1]) + margin_px(c->st->margin[3]);
                if (a + m > mn) mn = a + m;
                if (c->is_float) { fl += z + m; if (fl > mx) mx = fl; continue; }
                fl = 0;
                if (z + m > mx) mx = z + m;
            }
        }
        float w = clamp_w(b, mx, -1); mx = w;
        if (mn > mx) mn = mx;
        mn += pb; mx += pb;
    }
    if (b->st->max_w.u == L_PX) { float lim = b->st->max_w.v + (b->st->box_sizing == BOX_BORDER ? 0 : pb); if (mx > lim) mx = lim; if (mn > lim) mn = lim; }
    b->min_c = mn; b->max_c = mx; b->intr_ok = 1;
    *minw = mn; *maxw = mx;
}

static float space_w(font_t *f, const style_t *st) {
    return font_text_width(f, " ", 1, st->letter_spacing) + st->word_spacing;
}

static void inline_intrinsic(page_t *pg, box_t *b, float *mn, float *mx, float *line, int depth) {
    if (depth > 200) return;
    for (box_t *c = b->first; c; c = c->next) {
        if (c->is_abs) continue;
        if (c->kind == K_TEXT || (c->kind == K_MARKER && c->outer_inline)) {
            const style_t *st = c->st;
            font_t *f = style_font(st);
            const char *s = c->text, *e = c->text + c->tlen;
            bool pre = st->white_space == WS_PRE || st->white_space == WS_PRE_WRAP;
            bool nowrap = st->white_space == WS_NOWRAP || st->white_space == WS_PRE;
            float word = 0;
            while (s < e) {
                const char *w0 = s;
                while (s < e && !is_ws((u8)*s)) s++;
                if (s > w0) {
                    float ww = font_text_width(f, w0, (u32)(s - w0), st->letter_spacing);
                    word += ww; *line += ww;
                    if (st->break_word) { if (ww / (float)(s - w0) * 4 > *mn) *mn = ww / (float)(s - w0) * 4; }
                    else if (!nowrap && word > *mn) *mn = word;
                }
                if (s < e) {
                    if (pre && *s == '\n') { if (*line > *mx) *mx = *line; *line = 0; word = 0; s++; continue; }
                    *line += space_w(f, st);
                    if (nowrap) word += space_w(f, st);
                    else word = 0;
                    s++;
                    if (!pre) while (s < e && is_ws((u8)*s)) s++;
                }
            }
            if (nowrap && *line > *mn) *mn = *line;
        } else if (c->kind == K_BR) {
            if (*line > *mx) *mx = *line;
            *line = 0;
        } else if (c->kind == K_ELEM && c->outer_inline && c->st->display == D_INLINE) {
            resolve_box_edges(c, -1);
            float l = c->m[3] + c->b[3] + c->p[3], r = c->m[1] + c->b[1] + c->p[1];
            *line += l;
            inline_intrinsic(pg, c, mn, mx, line, depth + 1);
            *line += r;
        } else if (c->kind == K_MARKER) {
            continue;
        } else {
            float a, z; box_intrinsic(pg, c, &a, &z);
            float m = margin_px(c->st->margin[1]) + margin_px(c->st->margin[3]);
            if (a + m > *mn) *mn = a + m;
            *line += z + m;
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Inline formatting                                                  */
/* ------------------------------------------------------------------ */

enum { IT_WORD = 0, IT_SPACE, IT_BREAK, IT_START, IT_END, IT_ATOMIC, IT_FLOAT };

typedef struct {
    u8 type; u8 nowrap; u8 hyph;
    box_t *box;              /* text box / inline elem / atomic */
    const char *s; u32 n;
    float w;                 /* advance (start/end: edge widths) */
    float asc, desc;         /* above/below baseline incl. half-leading */
    float base_shift;        /* vertical-align offset (+ = down) */
} item_t;

typedef struct { item_t *v; int n, cap; } items_t;

static item_t *it_push(items_t *it) {
    if (it->n == it->cap) {
        int nc = it->cap ? it->cap * 2 : 256;
        item_t *nv = (item_t *)realloc(it->v, (size_t)nc * sizeof(item_t));
        if (!nv) return NULL;
        it->v = nv; it->cap = nc;
    }
    item_t *r = &it->v[it->n++];
    memset(r, 0, sizeof *r);
    return r;
}

static void text_metrics(const style_t *st, float *asc, float *desc) {
    font_t *f = style_font(st);
    float a = font_ascent(f), d = font_descent(f);
    float lh = st->line_height > 0 ? st->line_height : font_normal_lh(f);
    float hl = (lh - (a + d)) / 2;
    *asc = a + hl; *desc = d + hl;
}

static float va_shift(const style_t *st, const style_t *parent) {
    float pf = parent ? parent->font_size : st->font_size;
    switch (st->vertical_align) {
    case VA_SUB: return pf * 0.25f;
    case VA_SUPER: return -pf * 0.4f;
    default: return 0;
    }
}

typedef struct {
    page_t *pg;
    items_t it;
    float avail;        /* content width (for atomic sizing) */
    bool  skip_space;   /* collapse: previous output ended with a space */
} ictx_t;

static void layout_atomic(page_t *pg, box_t *c, float avail);

static void collect_items(ictx_t *ic, box_t *b, float shift, int depth) {
    if (depth > 200) return;
    for (box_t *c = b->first; c; c = c->next) {
        if (c->is_abs) { c->x = 0; c->y = 0; continue; }
        if (c->kind == K_TEXT || (c->kind == K_MARKER && c->outer_inline)) {
            const style_t *st = c->st;
            font_t *f = style_font(st);
            float asc, desc; text_metrics(st, &asc, &desc);
            u8 ws = st->white_space;
            bool pre = ws == WS_PRE || ws == WS_PRE_WRAP;
            bool keep_nl = pre || ws == WS_PRE_LINE;
            bool nowrap = ws == WS_NOWRAP || ws == WS_PRE;
            const char *s = c->text, *e = c->text + c->tlen;
            float sw = space_w(f, st);
            while (s < e) {
                if (is_ws((u8)*s)) {
                    if (keep_nl && *s == '\n') {
                        item_t *x = it_push(&ic->it); if (!x) return;
                        x->type = IT_BREAK; x->box = c; x->asc = asc; x->desc = desc;
                        s++; ic->skip_space = !pre; continue;
                    }
                    if (pre) {
                        const char *s0 = s; float w = 0;
                        while (s < e && is_ws((u8)*s) && *s != '\n') { w += (*s == '\t') ? sw * 8 : sw; s++; }
                        item_t *x = it_push(&ic->it); if (!x) return;
                        x->type = IT_SPACE; x->box = c; x->s = s0; x->n = 0; x->w = w; x->asc = asc; x->desc = desc;
                        x->nowrap = nowrap; x->base_shift = shift;
                        continue;
                    }
                    while (s < e && is_ws((u8)*s) && !(keep_nl && *s == '\n')) s++;
                    if (!ic->skip_space) {
                        item_t *x = it_push(&ic->it); if (!x) return;
                        x->type = IT_SPACE; x->box = c; x->w = sw; x->asc = asc; x->desc = desc;
                        x->nowrap = nowrap; x->base_shift = shift;
                        ic->skip_space = true;
                    }
                    continue;
                }
                const char *w0 = s;
                while (s < e && !is_ws((u8)*s)) s++;
                /* long words (URLs, paths) may break after - / ? & = */
                const char *p = w0;
                bool splittable = !nowrap && (s - w0) > 16;
                while (p < s) {
                    const char *q = s;
                    if (splittable) {
                        q = p + 1;
                        while (q < s && !((q[-1] == '-' || q[-1] == '/' || q[-1] == '?' || q[-1] == '&' || q[-1] == '=') &&
                                          (q - p) > 2 && (s - q) > 2)) q++;
                    }
                    item_t *x = it_push(&ic->it); if (!x) return;
                    x->type = IT_WORD; x->box = c; x->s = p; x->n = (u32)(q - p);
                    x->w = font_text_width(f, p, x->n, st->letter_spacing);
                    x->asc = asc; x->desc = desc; x->nowrap = nowrap; x->base_shift = shift;
                    if (q < s) x->hyph = 1;          /* glued to the next part */
                    p = q;
                }
                ic->skip_space = false;
            }
            continue;
        }
        if (c->kind == K_BR) {
            item_t *x = it_push(&ic->it); if (!x) return;
            float asc, desc; text_metrics(c->st, &asc, &desc);
            x->type = IT_BREAK; x->box = c; x->asc = asc; x->desc = desc;
            ic->skip_space = true;
            continue;
        }
        if (c->kind == K_MARKER) continue;   /* outside markers handled by caller */
        if (c->is_float) {
            item_t *x = it_push(&ic->it); if (!x) return;
            x->type = IT_FLOAT; x->box = c;
            continue;
        }
        if (c->kind == K_ELEM && c->st->display == D_INLINE && c->inner == IN_FLOW) {
            resolve_box_edges(c, ic->avail);
            float asc, desc; text_metrics(c->st, &asc, &desc);
            float sh = shift + va_shift(c->st, b->st);
            item_t *x = it_push(&ic->it); if (!x) return;
            x->type = IT_START; x->box = c; x->w = c->m[3] + c->b[3] + c->p[3];
            x->asc = asc; x->desc = desc; x->base_shift = sh;
            if (x->w > 0) ic->skip_space = false;
            collect_items(ic, c, sh, depth + 1);
            item_t *y = it_push(&ic->it); if (!y) return;
            y->type = IT_END; y->box = c; y->w = c->m[1] + c->b[1] + c->p[1];
            y->asc = asc; y->desc = desc; y->base_shift = sh;
            if (y->w > 0) ic->skip_space = false;
            continue;
        }
        /* atomic inline */
        layout_atomic(ic->pg, c, ic->avail);
        item_t *x = it_push(&ic->it); if (!x) return;
        x->type = IT_ATOMIC; x->box = c;
        x->w = c->w + c->m[1] + c->m[3];
        float bl = c->baseline > 0 ? c->baseline : c->h;
        x->asc = c->m[0] + bl;
        x->desc = c->h - bl + c->m[2];
        x->base_shift = shift;
        if (c->st->vertical_align == VA_MIDDLE) {
            font_t *pf = style_font(b->st);
            float mid = font_size(pf) * 0.25f;        /* x-height/2 above baseline */
            float total = c->h + c->m[0] + c->m[2];
            x->asc = total / 2 + mid; x->desc = total / 2 - mid;
        } else if (c->st->vertical_align == VA_TOP || c->st->vertical_align == VA_TEXT_TOP) {
            font_t *pf = style_font(b->st);
            float total = c->h + c->m[0] + c->m[2];
            x->asc = font_ascent(pf); x->desc = total - font_ascent(pf);
        } else if (c->st->vertical_align == VA_BOTTOM || c->st->vertical_align == VA_TEXT_BOTTOM) {
            font_t *pf = style_font(b->st);
            float total = c->h + c->m[0] + c->m[2];
            x->desc = font_descent(pf); x->asc = total - font_descent(pf);
        } else x->base_shift += va_shift(c->st, b->st);
        ic->skip_space = false;
    }
}

/* Size an atomic inline (inline-block, replaced, inline-flex, inline-table). */
static void layout_atomic(page_t *pg, box_t *c, float avail) {
    if (c->kind == K_REPLACED) { layout_replaced(pg, c, avail, -1); return; }
    resolve_box_edges(c, avail);
    float w = spec_content_w(c, avail);
    if (w < 0) {
        float mn, mx; box_intrinsic(pg, c, &mn, &mx);
        float room = avail - c->m[1] - c->m[3];
        float bw = mx < room ? mx : room;
        if (bw < mn) bw = mn;
        w = bw - hpb(c);
        if (c->inner == IN_TABLE) w = -1;
    }
    if (c->inner == IN_TABLE) {
        layout_table(pg, c, avail - c->m[1] - c->m[3]);
    } else {
        c->w = clamp_w(c, w, avail) + hpb(c);
        layout_block_box(pg, c, avail, -1);
    }
}

typedef struct { box_t *box; float x0; } open_t;

static void add_frag(box_t *owner, frag_t *f) {
    if (owner->nfrag == owner->capfrag) {
        u32 nc = owner->capfrag ? owner->capfrag * 2 : 32;
        frag_t *nf = (frag_t *)realloc(owner->frags, nc * sizeof(frag_t));
        if (!nf) return;
        owner->frags = nf; owner->capfrag = nc;
    }
    owner->frags[owner->nfrag++] = *f;
}

static void add_deco(box_t *owner, box_t *ib, float x0, float x1, float base, bool first, bool last) {
    const style_t *st = ib->st;
    bool visible = (st->bg_color >> 24) || ib->b[0] > 0 || ib->b[2] > 0 || ib->b[1] > 0 || ib->b[3] > 0 || st->bg_image || st->bg_grad || st->shadow_color;
    if (!visible) return;
    font_t *f = style_font(st);
    float top = base - font_ascent(f) - ib->p[0] - ib->b[0];
    float h = font_ascent(f) + font_descent(f) + vpb(ib);
    if (owner->ndeco == owner->capdeco) {
        u32 nc = owner->capdeco ? owner->capdeco * 2 : 16;
        ideco_t *nd = (ideco_t *)realloc(owner->decos, nc * sizeof(ideco_t));
        if (!nd) return;
        owner->decos = nd; owner->capdeco = nc;
    }
    owner->decos[owner->ndeco++] = (ideco_t){ ib, x0, x1, top, h, (u8)first, (u8)last };
}

void layout_float_box(page_t *pg, box_t *c, float cbw, bfc_t *bf, float ox, float oy,
                      float cx, float cw, float y);

/* Lay out the inline content of block container b. (ox, oy) = BFC coords of
 * b's border-box origin. Returns the content height. */
static float layout_inline(page_t *pg, box_t *b, float cx, float cy, float cw, bfc_t *bf, float ox, float oy) {
    b->nfrag = 0; b->ndeco = 0;
    ictx_t ic = { pg, {0}, cw, true };
    collect_items(&ic, b, 0, 0);
    item_t *it = ic.it.v; int n = ic.it.n;
    const style_t *bst = b->st;
    float strut_a, strut_d; text_metrics(bst, &strut_a, &strut_d);
    float y = 0;
    int i = 0;
    open_t open[64]; int nopen = 0;
    bool first_line = true;
    b->baseline = -1;
    float last_base = -1;
    /* outside list marker */
    box_t *marker = NULL;
    if (b->first && b->first->kind == K_MARKER && !b->first->outer_inline) marker = b->first;

    while (i < n) {
        /* skip leading spaces on a line (collapsible ones) */
        while (i < n && it[i].type == IT_SPACE && it[i].box->st->white_space != WS_PRE && it[i].box->st->white_space != WS_PRE_WRAP) i++;
        if (i >= n) break;
        /* available span for this line */
        float lh_guess = strut_a + strut_d;
        float l = cx, r = cx + cw;
        if (bf && bf->n) {
            float fl, fr;
            bfc_span(bf, oy + cy + y, lh_guess, ox + cx, ox + cx + cw, &fl, &fr);
            l = fl - ox; r = fr - ox;
            /* not even room for one word: drop below the float */
            int guard = 0;
            while (r - l < 20 && guard++ < 50) {
                float ny = bfc_next_edge(bf, oy + cy + y);
                if (ny >= 1e8f) break;
                y = ny - oy - cy;
                bfc_span(bf, oy + cy + y, lh_guess, ox + cx, ox + cx + cw, &fl, &fr);
                l = fl - ox; r = fr - ox;
            }
        }
        float indent = first_line ? bst->text_indent : 0;
        float avail = r - l - indent;
        /* gather the line */
        int start = i;
        float w = 0;
        int last_break = -1; float w_at_break = 0;
        bool forced = false;
        while (i < n) {
            item_t *x = &it[i];
            if (x->type == IT_BREAK) { forced = true; i++; break; }
            if (x->type == IT_FLOAT) {
                /* place now, at the current line top */
                layout_float_box(pg, x->box, cw, bf, ox, oy, cx, cw, cy + y);
                if (bf) {
                    float fl, fr;
                    bfc_span(bf, oy + cy + y, lh_guess, ox + cx, ox + cx + cw, &fl, &fr);
                    l = fl - ox; r = fr - ox; avail = r - l - indent;
                }
                i++; continue;
            }
            float xw = x->w;
            if (x->type == IT_SPACE) {
                if (!x->nowrap) { last_break = i; w_at_break = w; }
                w += xw; i++; continue;
            }
            /* word / atomic / start / end */
            if (w + xw > avail + 0.01f && i > start && x->type != IT_END) {
                if (x->type == IT_WORD && i > 0 && it[i - 1].type == IT_WORD && it[i - 1].hyph) {
                    /* hyphen glue point: break here is allowed */
                    break;
                }
                if (last_break > start) { i = last_break; w = w_at_break; break; }
                /* no break opportunity: allow break between start/atomic items */
                if (it[i - 1].type == IT_SPACE || x->type == IT_ATOMIC || it[i - 1].type == IT_ATOMIC) break;
                if (x->nowrap) { w += xw; i++; continue; }
                break;
            }
            if (x->type == IT_ATOMIC && i > start && it[i - 1].type != IT_SPACE && it[i - 1].type != IT_START) {
                last_break = i; w_at_break = w;
            }
            w += xw; i++;
        }
        int end = i;
        /* trim trailing collapsible spaces */
        int vis_end = end;
        while (vis_end > start && (it[vis_end - 1].type == IT_SPACE || it[vis_end - 1].type == IT_BREAK || it[vis_end - 1].type == IT_FLOAT) &&
               (it[vis_end - 1].type != IT_SPACE || (it[vis_end - 1].box->st->white_space != WS_PRE && it[vis_end - 1].box->st->white_space != WS_PRE_WRAP))) {
            if (it[vis_end - 1].type == IT_SPACE) w -= it[vis_end - 1].w;
            vis_end--;
        }
        /* vertical metrics */
        float A = strut_a, D = strut_d;
        bool has_content = false;
        for (int k = start; k < vis_end; k++) {
            item_t *x = &it[k];
            if (x->type == IT_FLOAT) continue;
            bool content = x->type == IT_WORD || x->type == IT_ATOMIC ||
                           ((x->type == IT_START || x->type == IT_END) && x->w > 0);
            if (!content && x->type != IT_SPACE) continue;
            has_content = has_content || content;
            float a = x->asc - x->base_shift, d = x->desc + x->base_shift;
            if (a > A) A = a;
            if (d > D) D = d;
        }
        if (!has_content && !forced && vis_end == start) {
            /* nothing visible: an empty line produces no box */
            continue;
        }
        if (!has_content) {
            /* only spaces/empty inline: still occupy a line if forced by <br> */
            bool only_br = forced;
            if (!only_br) { A = strut_a; D = strut_d; }
        }
        float line_h = A + D;
        float base = y + A;
        /* horizontal alignment */
        float slack = avail - w;
        float x = l + indent;
        float just_extra = 0;
        u8 ta = bst->text_align;
        if (ta == TA_CENTER) x += slack > 0 ? slack / 2 : 0;
        else if (ta == TA_RIGHT) x += slack > 0 ? slack : 0;
        else if (ta == TA_JUSTIFY && !forced && i < n && slack > 0) {
            int spaces = 0;
            for (int k = start; k < vis_end; k++) if (it[k].type == IT_SPACE) spaces++;
            if (spaces) just_extra = slack / spaces;
        }
        /* reopen inline boxes continuing from the previous line */
        for (int k = 0; k < nopen; k++) open[k].x0 = x;
        bool line_first_flags[64] = {0};
        (void)line_first_flags;
        for (int k = start; k < end; k++) {
            item_t *xi = &it[k];
            frag_t f = {0};
            switch (xi->type) {
            case IT_START:
                if (nopen < 64) { open[nopen].box = xi->box; open[nopen].x0 = x + xi->box->m[3]; nopen++; }
                x += xi->w;
                break;
            case IT_END:
                for (int q = nopen - 1; q >= 0; q--) if (open[q].box == xi->box) {
                    add_deco(b, xi->box, open[q].x0, x + xi->box->b[1] + xi->box->p[1], cy + base + xi->base_shift, true, true);
                    for (int z = q; z < nopen - 1; z++) open[z] = open[z + 1];
                    nopen--; break;
                }
                x += xi->w;
                break;
            case IT_WORD: case IT_SPACE: {
                if (k >= vis_end) break;
                font_t *ft = style_font(xi->box->st);
                f.x = x; f.w = xi->w; f.box = xi->box;
                f.y = cy + base + xi->base_shift - font_ascent(ft);
                f.base = font_ascent(ft);
                f.h = font_ascent(ft) + font_descent(ft);
                f.s = xi->type == IT_WORD ? xi->s : NULL; f.n = xi->type == IT_WORD ? xi->n : 0;
                add_frag(b, &f);
                x += xi->w + (xi->type == IT_SPACE ? just_extra : 0);
                break; }
            case IT_ATOMIC: {
                box_t *c = xi->box;
                float top = cy + base + xi->base_shift - xi->asc;
                c->x = x + c->m[3];
                c->y = top + c->m[0];
                f.x = c->x; f.y = c->y; f.w = c->w; f.h = c->h; f.box = c; f.atomic = 1;
                add_frag(b, &f);
                x += xi->w;
                break; }
            default: break;
            }
        }
        /* inline boxes still open at line end get a deco segment */
        for (int k = 0; k < nopen; k++)
            add_deco(b, open[k].box, open[k].x0, x, cy + base + open[k].box->st->vertical_align * 0, false, false);
        if (first_line) {
            b->baseline = cy + base;
            if (marker) {
                font_t *mf = style_font(marker->st);
                float mw = font_text_width(mf, marker->text, marker->tlen, 0);
                frag_t f = {0};
                f.x = l - mw - font_size(mf) * 0.5f; f.w = mw; f.box = marker;
                f.y = cy + base - font_ascent(mf); f.base = font_ascent(mf); f.h = font_ascent(mf) + font_descent(mf);
                f.s = marker->text; f.n = marker->tlen;
                add_frag(b, &f);
            }
        }
        last_base = cy + base;
        first_line = false;
        y += line_h;
    }
    if (marker && b->nfrag == 0) {
        /* empty list item: still show the marker */
        font_t *mf = style_font(marker->st);
        float mw = font_text_width(mf, marker->text, marker->tlen, 0);
        frag_t f = {0};
        f.x = cx - mw - font_size(mf) * 0.5f; f.w = mw; f.box = marker;
        f.y = cy; f.base = font_ascent(mf); f.h = font_ascent(mf) + font_descent(mf);
        f.s = marker->text; f.n = marker->tlen;
        add_frag(b, &f);
    }
    b->scroll_h = last_base;
    free(ic.it.v);
    return y;
}

/* ------------------------------------------------------------------ */
/*  Block formatting                                                   */
/* ------------------------------------------------------------------ */

/* Collapsed top margin of b including its first child chain. */
static float top_margin_through(box_t *b, float cbw, int depth) {
    float mt = b->m[0];
    if (depth > 50 || establishes_bfc(b) || b->b[0] > 0 || b->p[0] > 0 || b->has_inline_kids || b->kind != K_ELEM) return mt;
    box_t *c = b->first;
    while (c && (c->is_abs || c->is_float)) c = c->next;
    if (!c || c->outer_inline) return mt;
    float inner_w = cbw - b->m[1] - b->m[3] - hpb(b);
    resolve_box_edges(c, inner_w);
    float cm = top_margin_through(c, inner_w, depth + 1);
    if (mt >= 0 && cm >= 0) return mt > cm ? mt : cm;
    if (mt < 0 && cm < 0) return mt < cm ? mt : cm;
    return mt + cm;
}

static float collapse(float a, float b) {
    if (a >= 0 && b >= 0) return a > b ? a : b;
    if (a < 0 && b < 0) return a < b ? a : b;
    return a + b;
}

static void layout_block_in(page_t *pg, box_t *b, float cbw, float cbh, bfc_t *bf, float ox, float oy);

void layout_float_box(page_t *pg, box_t *c, float cbw, bfc_t *bf, float ox, float oy,
                      float cx, float cw, float y) {
    /* size */
    if (c->kind == K_REPLACED) layout_replaced(pg, c, cbw, -1);
    else {
        resolve_box_edges(c, cbw);
        float w = spec_content_w(c, cbw);
        if (w < 0) {
            float mn, mx; box_intrinsic(pg, c, &mn, &mx);
            float room = cbw - c->m[1] - c->m[3];
            float bw = mx < room ? mx : room;
            if (bw < mn) bw = mn;
            w = bw - hpb(c);
        }
        if (c->inner == IN_TABLE) layout_table(pg, c, cbw - c->m[1] - c->m[3]);
        else { c->w = clamp_w(c, w, cbw) + hpb(c); layout_block_box(pg, c, cbw, -1); }
    }
    float mw = c->w + c->m[1] + c->m[3], mh = c->h + c->m[0] + c->m[2];
    if (!bf) { c->x = cx + c->m[3]; c->y = y + c->m[0]; return; }
    float by = oy + y;
    if (c->st->clear) { float cy2 = bfc_clear_y(bf, c->st->clear); if (cy2 > by) by = cy2; }
    /* floats may not be placed above earlier floats */
    for (int k = 0; k < bf->n; k++) if (bf->f[k].y > by) by = bf->f[k].y;
    float x0 = ox + cx, x1 = ox + cx + cw;
    int guard = 0;
    for (;;) {
        float l, r;
        bfc_span(bf, by, mh, x0, x1, &l, &r);
        if (r - l >= mw - 0.5f || guard++ > 100 || (l == x0 && r == x1)) {
            float fx = c->st->float_ == FL_LEFT ? l : r - mw;
            bfc_add(bf, fx, by, mw, mh, c->st->float_);
            c->x = fx - ox + c->m[3];
            c->y = by - oy + c->m[0];
            return;
        }
        float ny = bfc_next_edge(bf, by);
        if (ny >= 1e8f) ny = by + 1;
        by = ny;
    }
}

static void layout_block_children(page_t *pg, box_t *b, float cx, float cy, float cw, float ch_def,
                                  bfc_t *bf, float ox, float oy, float *out_h) {
    float y = 0;
    float pend = 0;               /* pending collapsed margin */
    bool first = true;
    bool collapse_top = !establishes_bfc(b) && b->b[0] == 0 && b->p[0] == 0;
    box_t *last_in_flow = NULL;
    for (box_t *c = b->first; c; c = c->next) {
        if (c->kind == K_MARKER) continue;
        if (c->is_abs) {
            resolve_box_edges(c, cw);
            c->x = cx + c->m[3]; c->y = cy + y + pend;
            continue;
        }
        if (c->is_float) {
            layout_float_box(pg, c, cw, bf, ox, oy, cx, cw, cy + y + (first && collapse_top ? 0 : pend));
            continue;
        }
        resolve_box_edges(c, cw);
        float mt = top_margin_through(c, cw, 0);
        float m;
        if (first && collapse_top) m = 0;          /* hoisted into b's margin */
        else m = collapse(pend, mt);
        float cyy = y + m;
        /* clearance */
        if (c->st->clear && bf) {
            float clr = bfc_clear_y(bf, c->st->clear) - oy - cy;
            if (clr > cyy) cyy = clr;
        }
        c->y = cy + cyy;
        /* horizontal placement / width */
        bool wauto;
        float w = spec_content_w(c, cw);
        wauto = w < 0;
        float avail_l = 0, avail_r = cw;
        if (bf && bf->n && establishes_bfc(c) && c->kind == K_ELEM) {
            float fl, fr;
            bfc_span(bf, oy + c->y, 10, ox + cx, ox + cx + cw, &fl, &fr);
            avail_l = fl - ox - cx; avail_r = fr - ox - cx;
        }
        float room = avail_r - avail_l;
        if (c->kind == K_REPLACED) {
            layout_replaced(pg, c, cw, ch_def);
            float slack = room - c->w - c->m[1] - c->m[3];
            float ml = c->m[3];
            if (c->st->margin[3].u == L_AUTO && c->st->margin[1].u == L_AUTO) ml = slack / 2 + c->m[3];
            else if (c->st->margin[3].u == L_AUTO) ml = slack + c->m[3];
            c->x = cx + avail_l + ml;
        } else if (c->inner == IN_TABLE) {
            layout_table(pg, c, room - c->m[1] - c->m[3]);
            float slack = room - c->w - c->m[1] - c->m[3];
            float ml = c->m[3];
            if (c->st->margin[3].u == L_AUTO && c->st->margin[1].u == L_AUTO) ml = (slack > 0 ? slack / 2 : 0);
            else if (c->st->margin[3].u == L_AUTO) ml = slack > 0 ? slack : 0;
            c->x = cx + avail_l + ml;
        } else {
            if (wauto) w = room - c->m[1] - c->m[3] - hpb(c);
            w = clamp_w(c, w, cw);
            float bw = w + hpb(c);
            float slack = room - bw - c->m[1] - c->m[3];
            float ml = c->m[3];
            bool al = c->st->margin[3].u == L_AUTO, ar = c->st->margin[1].u == L_AUTO;
            if (!wauto || bw < room - c->m[1] - c->m[3] - 0.5f) {
                if (al && ar) ml = slack > 0 ? slack / 2 : 0;
                else if (al) ml = slack > 0 ? slack : 0;
                else if (!ar && b->st->text_align == TA_CENTER && b->el && lxb_dom_element_tag_id(b->el) == LXB_TAG_CENTER) ml = slack > 0 ? slack / 2 : 0;
            }
            c->x = cx + avail_l + ml;
            c->w = bw;
            layout_block_in(pg, c, cw, ch_def, establishes_bfc(c) ? NULL : bf, ox + c->x, oy + c->y);
        }
        float h = c->h;
        bool empty = h == 0 && c->kind == K_ELEM && c->b[0] == 0 && c->b[2] == 0 && c->p[0] == 0 && c->p[2] == 0 && !c->first;
        if (empty) {
            pend = collapse(m, c->m[2]);
            if (first && collapse_top) pend = c->m[2];
            continue;
        }
        y = cyy + h;
        pend = c->m[2];
        first = false;
        last_in_flow = c;
    }
    (void)last_in_flow;
    /* bottom margin: escapes when nothing separates it from b's bottom */
    bool collapse_bottom = !establishes_bfc(b) && b->b[2] == 0 && b->p[2] == 0 && b->st->height.u == L_AUTO;
    if (collapse_bottom && !first) {
        b->m[2] = collapse(b->m[2], pend);
    } else if (!first) {
        y += pend;
    }
    *out_h = y;
}

/* b->w (border box) must be set by the caller; computes h and children. */
static void layout_block_in(page_t *pg, box_t *b, float cbw, float cbh, bfc_t *bf, float ox, float oy) {
    float cw = b->w - hpb(b);
    if (cw < 0) cw = 0;
    float cx = b->b[3] + b->p[3], cy = b->b[0] + b->p[0];
    float sh = spec_content_h(b, cbh);
    if (sh >= 0) sh = clamp_h(b, sh, cbh);
    float ch = 0;
    bfc_t own = {0};
    bfc_t *use = bf;
    bool root = establishes_bfc(b) || !bf;
    if (root) use = &own;
    b->baseline = -1;
    if (b->inner == IN_FLEX) {
        layout_flex(pg, b, cw, sh);
        ch = b->h;          /* layout_flex stores content height in h */
    } else if (b->inner == IN_GRID) {
        layout_grid(pg, b, cw);
        ch = b->h;
    } else if (b->inner == IN_TABLE) {
        layout_table(pg, b, b->w);
        return;
    } else if (b->has_inline_kids) {
        ch = layout_inline(pg, b, cx, cy, cw, use, root ? 0 : ox, root ? 0 : oy);
    } else {
        layout_block_children(pg, b, cx, cy, cw, sh, use, root ? 0 : ox, root ? 0 : oy, &ch);
        /* baseline: first in-flow child's baseline */
        for (box_t *c = b->first; c; c = c->next)
            if (!c->is_abs && !c->is_float && c->kind == K_ELEM && c->baseline >= 0) { b->baseline = c->y + c->baseline; break; }
    }
    if (root && own.n) {
        float fb = bfc_bottom(&own) - cy;
        if (fb > ch && (b->parent == NULL || establishes_bfc(b))) ch = fb;
    }
    free(own.f);
    float h = sh >= 0 ? sh : clamp_h(b, ch, cbh);
    b->h = h + vpb(b);
}

void layout_block_box(page_t *pg, box_t *b, float cbw, float cbh) {
    if (b->kind == K_REPLACED) { layout_replaced(pg, b, cbw, cbh); return; }
    layout_block_in(pg, b, cbw, cbh, NULL, 0, 0);
}

/* ------------------------------------------------------------------ */
/*  Document + absolute positioning                                    */
/* ------------------------------------------------------------------ */

static void abs_coords(box_t *b, float ax, float ay, page_t *pg, int depth);

typedef struct { box_t **v; int n, cap; } blist_t;
static void bl_push(blist_t *l, box_t *b) {
    if (l->n == l->cap) { int nc = l->cap ? l->cap * 2 : 32; box_t **nv = (box_t **)realloc(l->v, (size_t)nc * sizeof(box_t *)); if (!nv) return; l->v = nv; l->cap = nc; }
    l->v[l->n++] = b;
}

static void collect_abs(box_t *b, blist_t *l, int depth) {
    if (depth > 500) return;
    for (box_t *c = b->first; c; c = c->next) {
        if (c->is_abs) bl_push(l, c);
        collect_abs(c, l, depth + 1);
    }
}

/* Document coordinates of a box's border-box origin. */
typedef struct { float x, y; } pt_t;

static box_t *inline_owner(box_t *b) {
    /* the block container whose fragments position b */
    box_t *p = b->parent;
    while (p && p->kind == K_ELEM && p->outer_inline && p->st->display == D_INLINE) p = p->parent;
    return p;
}

static pt_t doc_pos(box_t *b) {
    pt_t r = { 0, 0 };
    int guard = 0;
    while (b && guard++ < 1000) {
        r.x += b->x + b->rel_x; r.y += b->y + b->rel_y;
        box_t *p = b->parent;
        if (p && p->kind == K_ELEM && p->outer_inline && p->st->display == D_INLINE) b = inline_owner(b);
        else b = p;
    }
    return r;
}

static void layout_abs_box(page_t *pg, box_t *a) {
    /* containing block */
    box_t *cb = a->parent;
    if (a->is_fixed) cb = NULL;
    else while (cb && cb->parent && cb->st->position == POS_STATIC) cb = cb->parent;
    float cbx, cby, cbw, cbh;
    if (!cb || !cb->parent) {
        cbx = 0; cby = 0; cbw = (float)pg->vw; cbh = (float)pg->vh;
        if (cb && !a->is_fixed && cb->h > cbh) cbh = (float)pg->vh;
    } else {
        pt_t p = doc_pos(cb);
        cbx = p.x + cb->b[3]; cby = p.y + cb->b[0];
        cbw = cb->w - cb->b[1] - cb->b[3]; cbh = cb->h - cb->b[0] - cb->b[2];
    }
    resolve_box_edges(a, cbw);
    const style_t *st = a->st;
    bool l_auto = st->inset[3].u == L_AUTO, r_auto = st->inset[1].u == L_AUTO;
    bool t_auto = st->inset[0].u == L_AUTO, b_auto = st->inset[2].u == L_AUTO;
    float L = len_px(st->inset[3], cbw, 0), R = len_px(st->inset[1], cbw, 0);
    float T = len_px(st->inset[0], cbh, 0), B = len_px(st->inset[2], cbh, 0);
    /* static position (doc coords) */
    pt_t sp = { 0, 0 };
    if (a->parent) { pt_t pp = doc_pos(a->parent); sp.x = pp.x + a->x - a->m[3]; sp.y = pp.y + a->y; }
    float w;
    if (a->kind == K_REPLACED) { layout_replaced(pg, a, cbw, cbh); w = a->w; }
    else {
        float sw = spec_content_w(a, cbw);
        if (sw < 0 && !l_auto && !r_auto) sw = cbw - L - R - a->m[1] - a->m[3] - hpb(a);
        if (sw < 0) {
            float mn, mx; box_intrinsic(pg, a, &mn, &mx);
            float room = cbw - (l_auto ? 0 : L) - (r_auto ? 0 : R) - a->m[1] - a->m[3];
            float bw = mx < room ? mx : room; if (bw < mn) bw = mn;
            sw = bw - hpb(a);
        }
        sw = clamp_w(a, sw, cbw);
        a->w = sw + hpb(a);
        if (a->inner == IN_TABLE) layout_table(pg, a, a->w);
        else {
            float sh = spec_content_h(a, cbh);
            if (sh < 0 && !t_auto && !b_auto) {
                /* stretch between top and bottom */
                float hh = cbh - T - B - a->m[0] - a->m[2] - vpb(a);
                a->st->height.u = L_PX; a->st->height.v = hh + (a->st->box_sizing == BOX_BORDER ? vpb(a) : 0);
                layout_block_in(pg, a, cbw, cbh, NULL, 0, 0);
                a->st->height.u = L_AUTO;
            } else layout_block_in(pg, a, cbw, cbh, NULL, 0, 0);
        }
        w = a->w;
    }
    float x, y;
    if (!l_auto) x = cbx + L + a->m[3];
    else if (!r_auto) x = cbx + cbw - R - w - a->m[1];
    else x = sp.x + a->m[3];
    if (!l_auto && !r_auto && st->margin[3].u == L_AUTO && st->margin[1].u == L_AUTO)
        x = cbx + L + (cbw - L - R - w) / 2;
    if (!t_auto) y = cby + T + a->m[0];
    else if (!b_auto) y = cby + cbh - B - a->h - a->m[2];
    else y = sp.y + a->m[0];
    if (!t_auto && !b_auto && st->margin[0].u == L_AUTO && st->margin[2].u == L_AUTO)
        y = cby + T + (cbh - T - B - a->h) / 2;
    /* convert to parent-relative */
    pt_t pp = { 0, 0 };
    if (a->parent) pp = doc_pos(a->parent);
    a->x = x - pp.x; a->y = y - pp.y;
}

static void rel_offsets(box_t *b, float cbw, float cbh, int depth) {
    if (depth > 500) return;
    for (box_t *c = b->first; c; c = c->next) {
        c->rel_x = c->rel_y = 0;
        if (c->st->position == POS_RELATIVE || c->st->position == POS_STICKY) {
            const style_t *st = c->st;
            if (st->inset[3].u != L_AUTO) c->rel_x = len_px(st->inset[3], cbw, 0);
            else if (st->inset[1].u != L_AUTO) c->rel_x = -len_px(st->inset[1], cbw, 0);
            if (c->st->position == POS_RELATIVE) {
                if (st->inset[0].u != L_AUTO) c->rel_y = len_px(st->inset[0], cbh, 0);
                else if (st->inset[2].u != L_AUTO) c->rel_y = -len_px(st->inset[2], cbh, 0);
            } else c->rel_x = 0;
        }
        rel_offsets(c, c->w, c->h, depth + 1);
    }
}

static void extent(box_t *b, float ax, float ay, float *maxy, float *maxx, int depth) {
    if (depth > 500) return;
    float bottom = ay + b->h, right = ax + b->w;
    if (b->st->overflow != OV_VISIBLE) { if (bottom > *maxy) *maxy = bottom; if (right > *maxx) *maxx = right; return; }
    if (bottom > *maxy) *maxy = bottom;
    if (right > *maxx) *maxx = right;
    for (u32 i = 0; i < b->nfrag; i++) {
        frag_t *f = &b->frags[i];
        float fb = ay + f->y + f->h;
        if (fb > *maxy) *maxy = fb;
    }
    for (box_t *c = b->first; c; c = c->next) {
        if (c->kind == K_TEXT || c->kind == K_BR || c->kind == K_MARKER) continue;
        box_t *p = c->parent;
        float bx = ax, by = ay;
        if (p->kind == K_ELEM && p->outer_inline && p->st->display == D_INLINE) {
            /* atomic inside inline: coords relative to the owner, i.e. b's owner */
            extent(c, bx + c->x + c->rel_x, by + c->y + c->rel_y, maxy, maxx, depth + 1);
            continue;
        }
        extent(c, bx + c->x + c->rel_x, by + c->y + c->rel_y, maxy, maxx, depth + 1);
    }
}

static void clear_intrinsic(box_t *b, int depth) {
    if (depth > 500) return;
    b->intr_ok = 0;
    for (box_t *c = b->first; c; c = c->next) clear_intrinsic(c, depth + 1);
}

void layout_document(page_t *pg) {
    box_t *root = pg->root;
    if (!root) return;
    clear_intrinsic(root, 0);
    float vw = (float)pg->vw, vh = (float)pg->vh;
    resolve_box_edges(root, vw);
    float w = spec_content_w(root, vw);
    if (w < 0) w = vw - root->m[1] - root->m[3] - hpb(root);
    root->w = clamp_w(root, w, vw) + hpb(root);
    root->x = root->m[3]; root->y = root->m[0];
    /* the root's first child (body) top margin does not escape the root */
    layout_block_in(pg, root, vw, vh, NULL, 0, 0);
    if (root->h < vh - root->m[0] - root->m[2]) {}
    rel_offsets(root, root->w, root->h, 0);
    blist_t abs_ = {0};
    collect_abs(root, &abs_, 0);
    for (int i = 0; i < abs_.n; i++) layout_abs_box(pg, abs_.v[i]);
    free(abs_.v);
    float maxy = 0, maxx = 0;
    extent(root, root->x, root->y, &maxy, &maxx, 0);
    pg->doc_h = maxy + root->m[2];
    pg->doc_w = maxx;
    if (pg->doc_h < vh) pg->doc_h = vh;
    (void)abs_coords;
}

static void abs_coords(box_t *b, float ax, float ay, page_t *pg, int depth) { (void)b; (void)ax; (void)ay; (void)pg; (void)depth; }

/* Lay out b at a given border-box width (flex items, grid items, cells). */
void layout_box_with_width(page_t *pg, box_t *b, float bw, float cbw, float cbh) {
    if (b->kind == K_REPLACED) {
        layout_replaced(pg, b, cbw, cbh);
        float ow = b->w - hpb(b), oh = b->h - vpb(b);
        float cw = bw - hpb(b);
        if (cw < 0) cw = 0;
        if (spec_content_h(b, cbh) < 0 && ow > 0) b->h = oh * cw / ow + vpb(b);
        b->w = bw;
        return;
    }
    resolve_box_edges(b, cbw);
    b->w = bw;
    if (b->inner == IN_TABLE) { layout_table(pg, b, bw); return; }
    layout_block_in(pg, b, cbw, cbh, NULL, 0, 0);
}

/* Move a laid-out box's contents down by dy (cell vertical alignment). */
void shift_contents(box_t *b, float dy) {
    if (dy == 0) return;
    if (b->has_inline_kids) {
        for (u32 i = 0; i < b->nfrag; i++) {
            b->frags[i].y += dy;
            if (b->frags[i].atomic) b->frags[i].box->y += dy;
        }
        for (u32 i = 0; i < b->ndeco; i++) b->decos[i].y += dy;
    } else {
        for (box_t *c = b->first; c; c = c->next) c->y += dy;
        for (u32 i = 0; i < b->nfrag; i++) b->frags[i].y += dy;
    }
    if (b->baseline >= 0) b->baseline += dy;
}
