/* web/engine/paint.c -- software painter: boxes, text, images, stacking. */
#include "engine.h"

/* ------------------------------------------------------------------ */
/*  Canvas primitives (all coordinates in document space)              */
/* ------------------------------------------------------------------ */

static inline u32 blend_px(u32 dst, u32 src, u32 a) {
    if (a >= 255) return src | 0xFF000000u;
    if (a == 0) return dst;
    u32 ia = 255 - a;
    u32 rb = (((src & 0xFF00FF) * a + (dst & 0xFF00FF) * ia) >> 8) & 0xFF00FF;
    u32 g  = (((src & 0x00FF00) * a + (dst & 0x00FF00) * ia) >> 8) & 0x00FF00;
    return 0xFF000000u | rb | g;
}

static inline u32 color_alpha(canvas_t *c, u32 color) {
    return (u32)(((color >> 24) & 0xFF) * c->alpha + 0.5f);
}

void cv_fill(canvas_t *c, float fx, float fy, float fw, float fh, u32 color) {
    u32 a = color_alpha(c, color);
    if (!a || fw <= 0 || fh <= 0) return;
    int x0 = (int)floorf(fx - c->ox + 0.5f), y0 = (int)floorf(fy - c->oy + 0.5f);
    int x1 = (int)floorf(fx + fw - c->ox + 0.5f), y1 = (int)floorf(fy + fh - c->oy + 0.5f);
    if (x1 == x0 && fw > 0.3f) x1 = x0 + 1;
    if (y1 == y0 && fh > 0.3f) y1 = y0 + 1;
    if (x0 < c->clip[0]) x0 = c->clip[0];
    if (y0 < c->clip[1]) y0 = c->clip[1];
    if (x1 > c->clip[2]) x1 = c->clip[2];
    if (y1 > c->clip[3]) y1 = c->clip[3];
    if (x0 >= x1 || y0 >= y1) return;
    u32 col = color & 0xFFFFFF;
    for (int y = y0; y < y1; y++) {
        u32 *row = c->px + (size_t)y * c->stride;
        if (a >= 255) { u32 v = col | 0xFF000000u; for (int x = x0; x < x1; x++) row[x] = v; }
        else for (int x = x0; x < x1; x++) row[x] = blend_px(row[x], col, a);
    }
}

/* Horizontal extent of a rounded rect at row center py (doc coords). */
static bool rr_span(float x, float y, float w, float h, const float r[4], float py, float *l, float *rr) {
    if (py < y || py >= y + h) return false;
    *l = x; *rr = x + w;
    float tl = r[0], tr = r[1], br = r[2], bl = r[3];
    if (tl > 0 && py < y + tl) { float dy = y + tl - py; float d = tl * tl - dy * dy; *l = x + tl - (d > 0 ? sqrtf(d) : 0); }
    if (bl > 0 && py > y + h - bl) { float dy = py - (y + h - bl); float d = bl * bl - dy * dy; *l = x + bl - (d > 0 ? sqrtf(d) : 0); }
    if (tr > 0 && py < y + tr) { float dy = y + tr - py; float d = tr * tr - dy * dy; *rr = x + w - tr + (d > 0 ? sqrtf(d) : 0); }
    if (br > 0 && py > y + h - br) { float dy = py - (y + h - br); float d = br * br - dy * dy; *rr = x + w - br + (d > 0 ? sqrtf(d) : 0); }
    return *rr > *l;
}

static void span_fill(canvas_t *c, u32 *row, float l, float r, u32 col, u32 a) {
    /* l, r in canvas x coords (float) with AA on the ends */
    int il = (int)floorf(l), ir = (int)floorf(r);
    for (int x = il; x <= ir; x++) {
        if (x < c->clip[0] || x >= c->clip[2]) continue;
        float cov = 1.0f;
        if (x == il) cov = (float)(il + 1) - l;
        if (x == ir) cov = (x == il) ? r - l : r - (float)ir;
        if (cov <= 0) continue;
        if (cov > 1) cov = 1;
        row[x] = blend_px(row[x], col, (u32)(a * cov));
    }
}

void cv_fill_rrect(canvas_t *c, float x, float y, float w, float h, const float r[4], u32 color) {
    if (r[0] <= 0 && r[1] <= 0 && r[2] <= 0 && r[3] <= 0) { cv_fill(c, x, y, w, h, color); return; }
    u32 a = color_alpha(c, color);
    if (!a || w <= 0 || h <= 0) return;
    u32 col = color & 0xFFFFFF;
    int y0 = (int)floorf(y - c->oy), y1 = (int)ceilf(y + h - c->oy);
    if (y0 < c->clip[1]) y0 = c->clip[1];
    if (y1 > c->clip[3]) y1 = c->clip[3];
    for (int yy = y0; yy < y1; yy++) {
        float l, rr;
        if (!rr_span(x, y, w, h, r, yy + c->oy + 0.5f, &l, &rr)) continue;
        span_fill(c, c->px + (size_t)yy * c->stride, l - c->ox, rr - c->ox, col, a);
    }
}

/* Ring between an outer rounded rect and an inner one (borders). */
static void cv_ring(canvas_t *c, float x, float y, float w, float h, const float r[4],
                    const float bw[4], u32 color) {
    u32 a = color_alpha(c, color);
    if (!a) return;
    u32 col = color & 0xFFFFFF;
    float ix = x + bw[3], iy = y + bw[0], iw = w - bw[1] - bw[3], ih = h - bw[0] - bw[2];
    float ir[4] = { r[0] - (bw[0] > bw[3] ? bw[0] : bw[3]), r[1] - (bw[0] > bw[1] ? bw[0] : bw[1]),
                    r[2] - (bw[2] > bw[1] ? bw[2] : bw[1]), r[3] - (bw[2] > bw[3] ? bw[2] : bw[3]) };
    for (int i = 0; i < 4; i++) if (ir[i] < 0) ir[i] = 0;
    int y0 = (int)floorf(y - c->oy), y1 = (int)ceilf(y + h - c->oy);
    if (y0 < c->clip[1]) y0 = c->clip[1];
    if (y1 > c->clip[3]) y1 = c->clip[3];
    for (int yy = y0; yy < y1; yy++) {
        float py = yy + c->oy + 0.5f;
        float l, rr, il, irr;
        if (!rr_span(x, y, w, h, r, py, &l, &rr)) continue;
        u32 *row = c->px + (size_t)yy * c->stride;
        if (iw > 0 && ih > 0 && rr_span(ix, iy, iw, ih, ir, py, &il, &irr)) {
            span_fill(c, row, l - c->ox, il - c->ox, col, a);
            span_fill(c, row, irr - c->ox, rr - c->ox, col, a);
        } else span_fill(c, row, l - c->ox, rr - c->ox, col, a);
    }
}

static void cv_image(canvas_t *c, const u32 *src, int sw, int sh, float dx, float dy,
                     int clipx0, int clipy0, int clipx1, int clipy1) {
    int x0 = (int)floorf(dx - c->ox + 0.5f), y0 = (int)floorf(dy - c->oy + 0.5f);
    int cx0 = clipx0 > c->clip[0] ? clipx0 : c->clip[0], cy0 = clipy0 > c->clip[1] ? clipy0 : c->clip[1];
    int cx1 = clipx1 < c->clip[2] ? clipx1 : c->clip[2], cy1 = clipy1 < c->clip[3] ? clipy1 : c->clip[3];
    u32 ga = (u32)(c->alpha * 255 + 0.5f);
    for (int j = 0; j < sh; j++) {
        int y = y0 + j;
        if (y < cy0 || y >= cy1) continue;
        u32 *row = c->px + (size_t)y * c->stride;
        const u32 *s = src + (size_t)j * sw;
        for (int i = 0; i < sw; i++) {
            int x = x0 + i;
            if (x < cx0 || x >= cx1) continue;
            u32 p = s[i];
            u32 a = (p >> 24) * ga / 255;
            if (a) row[x] = blend_px(row[x], p, a);
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Box painting                                                       */
/* ------------------------------------------------------------------ */

static void radii(const box_t *b, float r[4]) {
    float m = (b->w < b->h ? b->w : b->h) / 2;
    for (int i = 0; i < 4; i++) {
        float v = b->st->radius[i];
        if (v < 0) v = -v * (i % 2 == 0 ? b->w : b->w) / 100.0f;   /* percent */
        r[i] = v > m ? m : v;
    }
}

static void canvas_clip_rect(canvas_t *c, float x, float y, float w, float h, int out[4]) {
    out[0] = (int)floorf(x - c->ox); out[1] = (int)floorf(y - c->oy);
    out[2] = (int)ceilf(x + w - c->ox); out[3] = (int)ceilf(y + h - c->oy);
}

static void paint_bg_image(page_t *pg, canvas_t *c, const style_t *st, float x, float y, float w, float h) {
    (void)pg;
    web_img_t *im = NULL;
    if (!st->bg_image) return;
    for (web_img_t *q = pg->imgs; q; q = q->next) if (strcmp(q->url, st->bg_image) == 0) { im = q; break; }
    if (!im || im->state != 1 || im->w <= 0 || im->h <= 0) return;
    float iw = (float)im->w, ih = (float)im->h;
    if (st->bg_size_mode == BGS_COVER || st->bg_size_mode == BGS_CONTAIN) {
        float sx = w / iw, sy = h / ih;
        float s = st->bg_size_mode == BGS_COVER ? (sx > sy ? sx : sy) : (sx < sy ? sx : sy);
        iw *= s; ih *= s;
    } else if (st->bg_size_mode == BGS_LEN) {
        float a = len_px(st->bg_size[0], w, -1), b2 = len_px(st->bg_size[1], h, -1);
        if (a > 0 && b2 > 0) { iw = a; ih = b2; }
        else if (a > 0) { ih = ih * a / iw; iw = a; }
        else if (b2 > 0) { iw = iw * b2 / ih; ih = b2; }
    }
    if (iw < 1 || ih < 1 || iw > 8192 || ih > 8192) return;
    int siw = (int)(iw + 0.5f), sih = (int)(ih + 0.5f);
    const u32 *px = img_scaled(im, siw, sih);
    if (!px) return;
    float px0 = x + len_px(st->bg_pos[0], w - iw, 0) ;
    float py0 = y + len_px(st->bg_pos[1], h - ih, 0);
    if (st->bg_pos[0].u == L_PX) px0 = x + st->bg_pos[0].v;
    if (st->bg_pos[1].u == L_PX) py0 = y + st->bg_pos[1].v;
    int clip[4]; canvas_clip_rect(c, x, y, w, h, clip);
    bool rx = st->bg_repeat == BGR_REPEAT || st->bg_repeat == BGR_REPEAT_X;
    bool ry = st->bg_repeat == BGR_REPEAT || st->bg_repeat == BGR_REPEAT_Y;
    float sx0 = px0, sy0 = py0;
    if (rx) while (sx0 > x) sx0 -= siw;
    if (ry) while (sy0 > y) sy0 -= sih;
    int guard = 0;
    for (float yy = sy0; yy < y + h && guard < 4000; yy += sih) {
        if (yy + sih >= c->oy + c->clip[1] && yy <= c->oy + c->clip[3])
            for (float xx = sx0; xx < x + w && guard < 4000; xx += siw) {
                cv_image(c, px, siw, sih, xx, yy, clip[0], clip[1], clip[2], clip[3]);
                guard++;
                if (!rx) break;
            }
        if (!ry) break;
    }
}

static u32 lerp_color(u32 a, u32 b, float t) {
    u32 r = 0;
    for (int s = 0; s < 32; s += 8) {
        float ca = (float)((a >> s) & 0xFF), cb = (float)((b >> s) & 0xFF);
        r |= ((u32)(ca + (cb - ca) * t + 0.5f) & 0xFF) << s;
    }
    return r;
}

static void paint_box_decor(page_t *pg, canvas_t *c, box_t *b, float x, float y) {
    const style_t *st = b->st;
    if (st->visibility) return;
    /* masked boxes are icon glyphs cut out of their background by an (SVG)
     * mask we cannot decode: painting the bare background gives a black blob */
    if (st->masked) return;
    float r[4]; radii(b, r);
    float w = b->w, h = b->h;
    if (w <= 0 || h <= 0) return;
    /* shadow */
    if (st->shadow_color >> 24) {
        float blur = st->shadow_blur;
        int steps = blur > 8 ? 8 : (int)blur;
        u32 base_a = st->shadow_color >> 24;
        if (steps <= 0) {
            cv_fill_rrect(c, x + st->shadow_x, y + st->shadow_y, w, h, r, st->shadow_color);
        } else {
            for (int i = steps; i >= 1; i--) {
                float grow = blur * i / steps / 2;
                u32 a = base_a / (u32)(steps + 1);
                float rr[4]; for (int k = 0; k < 4; k++) rr[k] = r[k] + grow;
                cv_fill_rrect(c, x + st->shadow_x - grow, y + st->shadow_y - grow, w + 2 * grow, h + 2 * grow, rr,
                              (st->shadow_color & 0xFFFFFF) | (a << 24));
            }
        }
    }
    /* background */
    if (st->bg_color >> 24) cv_fill_rrect(c, x, y, w, h, r, st->bg_color);
    if (st->bg_grad) {
        if (st->grad_dir == 0) {
            int n = (int)h; if (n > 2000) n = 2000;
            for (int i = 0; i < n; i++) {
                float t = n > 1 ? (float)i / (n - 1) : 0;
                float sy = y + h * i / n;
                if (sy - c->oy > c->clip[3] || sy + h / n - c->oy < c->clip[1]) continue;
                cv_fill(c, x, sy, w, h / n + 0.5f, lerp_color(st->grad_from, st->grad_to, t));
            }
        } else {
            int n = (int)w; if (n > 2000) n = 2000;
            for (int i = 0; i < n; i++) {
                float t = n > 1 ? (float)i / (n - 1) : 0;
                cv_fill(c, x + w * i / n, y, w / n + 0.5f, h, lerp_color(st->grad_from, st->grad_to, t));
            }
        }
    }
    if (st->bg_image) paint_bg_image(pg, c, st, x, y, w, h);
    /* borders */
    float bw[4] = { b->b[0], b->b[1], b->b[2], b->b[3] };
    if (bw[0] + bw[1] + bw[2] + bw[3] <= 0) return;
    bool same = st->border_color[0] == st->border_color[1] && st->border_color[1] == st->border_color[2] &&
                st->border_color[2] == st->border_color[3];
    bool rounded = r[0] > 0 || r[1] > 0 || r[2] > 0 || r[3] > 0;
    if (rounded && same) { cv_ring(c, x, y, w, h, r, bw, st->border_color[0]); return; }
    u32 col[4];
    for (int i = 0; i < 4; i++) {
        col[i] = st->border_color[i];
        if (st->border_style[i] == BS_OTHER) {
            /* inset/outset/groove: darken top-left or bottom-right */
            u32 cc = col[i];
            bool dark = (i == 0 || i == 3);
            u32 rr = (cc >> 16) & 0xFF, gg = (cc >> 8) & 0xFF, bb = cc & 0xFF;
            if (dark) { rr = rr * 2 / 3; gg = gg * 2 / 3; bb = bb * 2 / 3; }
            col[i] = (cc & 0xFF000000u) | (rr << 16) | (gg << 8) | bb;
        }
    }
    if (bw[0] > 0) cv_fill(c, x, y, w, bw[0], col[0]);
    if (bw[2] > 0) cv_fill(c, x, y + h - bw[2], w, bw[2], col[2]);
    if (bw[3] > 0) cv_fill(c, x, y + bw[0], bw[3], h - bw[0] - bw[2], col[3]);
    if (bw[1] > 0) cv_fill(c, x + w - bw[1], y + bw[0], bw[1], h - bw[0] - bw[2], col[1]);
}

static void draw_text(canvas_t *c, const style_t *st, float x, float base, const char *s, u32 n, float w) {
    font_t *f = style_font(st);
    int clip[4] = { c->clip[0], c->clip[1], c->clip[2], c->clip[3] };
    u32 color = st->color;
    if (n) font_draw(f, c->px, c->w, c->h, c->stride, clip, x - c->ox, base - c->oy, s, n, color,
                     st->letter_spacing, st->italic, c->alpha);
    if (st->deco) {
        u32 dc = st->deco_color ? st->deco_color : color;
        float th = font_size(f) / 14.0f; if (th < 1) th = 1;
        if (st->deco & DECO_UNDER) cv_fill(c, x, base + font_size(f) * 0.12f, w, th, dc);
        if (st->deco & DECO_THROUGH) cv_fill(c, x, base - font_ascent(f) * 0.32f, w, th, dc);
        if (st->deco & DECO_OVER) cv_fill(c, x, base - font_ascent(f), w, th, dc);
    }
}

static void paint_replaced(page_t *pg, canvas_t *c, box_t *b, float x, float y) {
    (void)pg;
    const style_t *st = b->st;
    if (st->visibility) return;
    float cx = x + b->b[3] + b->p[3], cy = y + b->b[0] + b->p[0];
    float cw = b->w - b->b[1] - b->b[3] - b->p[1] - b->p[3];
    float ch = b->h - b->b[0] - b->b[2] - b->p[0] - b->p[2];
    if (cw <= 0 || ch <= 0) return;
    int clip[4]; canvas_clip_rect(c, cx, cy, cw, ch, clip);
    web_img_t *im = b->img;
    switch (b->rkind) {
    case 0: case 6: /* img / media */
        if (im && im->state == 1 && im->w > 0) {
            float dw = cw, dh = ch, dx = cx, dy = cy;
            if (st->object_fit == 1 || st->object_fit == 2) {
                float sx = cw / im->w, sy = ch / im->h;
                float s = st->object_fit == 1 ? (sx > sy ? sx : sy) : (sx < sy ? sx : sy);
                dw = im->w * s; dh = im->h * s;
                dx = cx + (cw - dw) / 2; dy = cy + (ch - dh) / 2;
            }
            int iw = (int)(dw + 0.5f), ih = (int)(dh + 0.5f);
            if (iw > 0 && ih > 0 && iw <= 8192 && ih <= 8192) {
                const u32 *px = img_scaled(im, iw, ih);
                if (px) cv_image(c, px, iw, ih, dx, dy, clip[0], clip[1], clip[2], clip[3]);
            }
        } else if (b->rkind == 6) {
            cv_fill(c, cx, cy, cw, ch, 0xFF202124u);
            /* play triangle */
            float s = (cw < ch ? cw : ch) * 0.18f;
            for (int i = 0; i < (int)s; i++)
                cv_fill(c, cx + cw / 2 - s / 3 + i * 0.9f, cy + ch / 2 - (s - i) / 2, 1, s - i, 0xC0FFFFFFu);
        } else if (b->alt && ch > 8) {
            style_t tmp = *st; tmp.deco = 0; tmp.color = 0xFF606060u;
            font_t *f = style_font(&tmp);
            int saved[4]; memcpy(saved, c->clip, sizeof saved);
            for (int i = 0; i < 4; i++) c->clip[i] = (i < 2) ? (clip[i] > saved[i] ? clip[i] : saved[i]) : (clip[i] < saved[i] ? clip[i] : saved[i]);
            draw_text(c, &tmp, cx + 2, cy + font_ascent(f), b->alt, (u32)strlen(b->alt), 0);
            memcpy(c->clip, saved, sizeof saved);
        }
        break;
    case 1: case 4: case 5: { /* input / select / textarea */
        style_t tmp = *st; tmp.deco = 0;
        if (b->colspan == 1) tmp.color = 0xFF757575u;          /* placeholder */
        font_t *f = style_font(&tmp);
        int saved[4]; memcpy(saved, c->clip, sizeof saved);
        for (int i = 0; i < 4; i++) c->clip[i] = (i < 2) ? (clip[i] > saved[i] ? clip[i] : saved[i]) : (clip[i] < saved[i] ? clip[i] : saved[i]);
        if (b->alt) {
            if (b->rkind == 5) {
                const char *s = b->alt; float ly = cy + font_ascent(f);
                while (*s && ly < cy + ch + font_ascent(f)) {
                    const char *e = strchr(s, '\n'); size_t l = e ? (size_t)(e - s) : strlen(s);
                    draw_text(c, &tmp, cx, ly, s, (u32)l, 0);
                    ly += font_normal_lh(f);
                    s += l + (e ? 1 : 0);
                }
            } else {
                float lh = font_ascent(f) + font_descent(f);
                draw_text(c, &tmp, cx, cy + (ch - lh) / 2 + font_ascent(f), b->alt, (u32)strlen(b->alt), 0);
            }
        }
        memcpy(c->clip, saved, sizeof saved);
        if (b->rkind == 4) {
            /* dropdown chevron */
            float ax = x + b->w - b->b[1] - 13, ay = y + b->h / 2 - 2;
            for (int i = 0; i < 4; i++) cv_fill(c, ax + i, ay + i, 8 - 2 * i, 1, 0xFF333333u);
        }
        break; }
    case 2: case 3: { /* checkbox / radio */
        float rr[4] = { b->rkind == 3 ? cw / 2 : 2, b->rkind == 3 ? cw / 2 : 2, b->rkind == 3 ? cw / 2 : 2, b->rkind == 3 ? cw / 2 : 2 };
        float bw[4] = { 1, 1, 1, 1 };
        cv_fill_rrect(c, cx, cy, cw, ch, rr, 0xFFFFFFFFu);
        cv_ring(c, cx, cy, cw, ch, rr, bw, 0xFF767676u);
        if (b->alt) {
            if (b->rkind == 3) { float ir[4] = { cw / 4, cw / 4, cw / 4, cw / 4 }; cv_fill_rrect(c, cx + cw / 4, cy + ch / 4, cw / 2, ch / 2, ir, 0xFF0B57D0u); }
            else { cv_fill_rrect(c, cx, cy, cw, ch, rr, 0xFF0B57D0u);
                   for (int i = 0; i < 3; i++) cv_fill(c, cx + 3 + i, cy + ch / 2 + i - 1, 1.5f, 1.5f, 0xFFFFFFFFu);
                   for (int i = 0; i < 6; i++) cv_fill(c, cx + 5 + i, cy + ch / 2 + 1 - i, 1.5f, 1.5f, 0xFFFFFFFFu); }
        }
        break; }
    default: break;
    }
}

/* ------------------------------------------------------------------ */
/*  Tree walk with stacking                                            */
/* ------------------------------------------------------------------ */

typedef struct { box_t *b; float x, y; int z; int order; float alpha; int clip[4]; } stack_ent_t;
typedef struct { stack_ent_t *v; int n, cap; } stack_list_t;

static void sl_push(stack_list_t *l, stack_ent_t e) {
    if (l->n == l->cap) { int nc = l->cap ? l->cap * 2 : 16; stack_ent_t *nv = (stack_ent_t *)realloc(l->v, (size_t)nc * sizeof(stack_ent_t)); if (!nv) return; l->v = nv; l->cap = nc; }
    l->v[l->n++] = e;
}

static bool is_positioned(const box_t *b) {
    return b->st->position != POS_STATIC && b->st->position != POS_STICKY ? true : (b->st->position == POS_STICKY);
}

static void paint_box(page_t *pg, canvas_t *c, box_t *b, float x, float y, stack_list_t *sl, int depth);
static void paint_context(page_t *pg, canvas_t *c, box_t *b, float x, float y, int depth);

static void paint_frags(page_t *pg, canvas_t *c, box_t *b, float x, float y, stack_list_t *sl, int depth) {
    /* inline element backgrounds first */
    for (u32 i = 0; i < b->ndeco; i++) {
        ideco_t *d = &b->decos[i];
        box_t *ib = d->box;
        const style_t *st = ib->st;
        if (st->visibility) continue;
        float dx = x + d->x0 - (d->first ? ib->b[3] + ib->p[3] : 0);
        float dw = d->x1 - d->x0 + (d->first ? ib->b[3] + ib->p[3] : 0);
        float r[4] = {0, 0, 0, 0};
        float rad = st->radius[0] > 0 ? st->radius[0] : 0;
        if (rad > 0) { if (d->first) { r[0] = r[3] = rad; } if (d->last) { r[1] = r[2] = rad; } }
        if (st->bg_color >> 24) cv_fill_rrect(c, dx, y + d->y, dw, d->h, r, st->bg_color);
        if (st->bg_grad) cv_fill_rrect(c, dx, y + d->y, dw, d->h, r, st->grad_from);
        float bw[4] = { ib->b[0], d->last ? ib->b[1] : 0, ib->b[2], d->first ? ib->b[3] : 0 };
        if (bw[0] > 0) cv_fill(c, dx, y + d->y, dw, bw[0], st->border_color[0]);
        if (bw[2] > 0) cv_fill(c, dx, y + d->y + d->h - bw[2], dw, bw[2], st->border_color[2]);
        if (bw[3] > 0) cv_fill(c, dx, y + d->y, bw[3], d->h, st->border_color[3]);
        if (bw[1] > 0) cv_fill(c, dx + dw - bw[1], y + d->y, bw[1], d->h, st->border_color[1]);
    }
    float top = c->oy + c->clip[1], bottom = c->oy + c->clip[3];
    for (u32 i = 0; i < b->nfrag; i++) {
        frag_t *f = &b->frags[i];
        if (f->atomic) {
            box_t *a = f->box;
            float ax = x + a->x + a->rel_x, ay = y + a->y + a->rel_y;
            if (is_positioned(a) && sl) {
                stack_ent_t e = { a, ax, ay, a->st->z_auto ? 0 : a->st->z_index, sl->n, c->alpha, {c->clip[0], c->clip[1], c->clip[2], c->clip[3]} };
                sl_push(sl, e);
                continue;
            }
            paint_box(pg, c, a, ax, ay, sl, depth + 1);
            continue;
        }
        float fy = y + f->y;
        if (fy > bottom || fy + f->h < top) continue;
        const style_t *st = f->box->st;
        if (st->visibility) continue;
        float rx = 0, ry = 0;
        /* relative offsets of inline ancestors */
        for (box_t *p = f->box->parent; p && p != b && p->kind == K_ELEM && p->outer_inline && p->st->display == D_INLINE; p = p->parent)
            if (p->kind == K_ELEM && p->st->position == POS_RELATIVE) { rx += p->rel_x; ry += p->rel_y; }
        draw_text(c, st, x + f->x + rx, fy + f->base + ry, f->s, f->n, f->w);
    }
}

static void paint_children(page_t *pg, canvas_t *c, box_t *b, float x, float y, stack_list_t *sl, int depth) {
    /* in-flow blocks, then floats, then inline content */
    for (int phase = 0; phase < 2; phase++) {
        for (box_t *k = b->first; k; k = k->next) {
            if (k->kind != K_ELEM && k->kind != K_REPLACED) continue;
            if (k->outer_inline && b->has_inline_kids) continue;   /* painted via fragments */
            if (b->has_inline_kids && !k->is_float && !k->is_abs) continue;
            if ((phase == 0) == (k->is_float != 0)) continue;
            float kx = x + k->x + k->rel_x, ky = y + k->y + k->rel_y;
            if (is_positioned(k) && sl) {
                stack_ent_t e = { k, kx, ky, k->st->z_auto ? 0 : k->st->z_index, sl->n, c->alpha, {c->clip[0], c->clip[1], c->clip[2], c->clip[3]} };
                sl_push(sl, e);
                continue;
            }
            paint_box(pg, c, k, kx, ky, sl, depth + 1);
        }
    }
    if (b->nfrag || b->ndeco) paint_frags(pg, c, b, x, y, sl, depth);
    /* inline elements can own floats/abs children positioned against b */
    if (b->has_inline_kids) {
        for (box_t *k = b->first; k; k = k->next) {
            if (k->kind != K_ELEM || !k->outer_inline || k->st->display != D_INLINE) continue;
            /* descend for floats & positioned boxes inside inline elements */
            for (box_t *q = k->first; q; q = q->next) {
                if (!(q->is_float || q->is_abs)) continue;
                float qx = x + q->x + q->rel_x, qy = y + q->y + q->rel_y;
                if (is_positioned(q) && sl) {
                    stack_ent_t e = { q, qx, qy, q->st->z_auto ? 0 : q->st->z_index, sl->n, c->alpha, {c->clip[0], c->clip[1], c->clip[2], c->clip[3]} };
                    sl_push(sl, e);
                } else paint_box(pg, c, q, qx, qy, sl, depth + 1);
            }
        }
    }
}

static void paint_box(page_t *pg, canvas_t *c, box_t *b, float x, float y, stack_list_t *sl, int depth) {
    if (depth > 400) return;
    float saved_alpha = c->alpha;
    if (b->st->opacity < 1) c->alpha *= b->st->opacity;
    if (c->alpha <= 0.01f) { c->alpha = saved_alpha; return; }
    if (b->kind == K_ELEM || b->kind == K_REPLACED) paint_box_decor(pg, c, b, x, y);
    if (b->kind == K_REPLACED) { paint_replaced(pg, c, b, x, y); c->alpha = saved_alpha; return; }
    int saved_clip[4];
    bool clipped = b->st->overflow != OV_VISIBLE && b->parent;
    if (clipped) {
        memcpy(saved_clip, c->clip, sizeof saved_clip);
        int r[4]; canvas_clip_rect(c, x + b->b[3], y + b->b[0], b->w - b->b[1] - b->b[3], b->h - b->b[0] - b->b[2], r);
        if (r[0] > c->clip[0]) c->clip[0] = r[0];
        if (r[1] > c->clip[1]) c->clip[1] = r[1];
        if (r[2] < c->clip[2]) c->clip[2] = r[2];
        if (r[3] < c->clip[3]) c->clip[3] = r[3];
    }
    if (c->clip[0] < c->clip[2] && c->clip[1] < c->clip[3])
        paint_children(pg, c, b, x, y, sl, depth);
    if (clipped) memcpy(c->clip, saved_clip, sizeof saved_clip);
    c->alpha = saved_alpha;
}

static int cmp_stack(const void *a, const void *b) {
    const stack_ent_t *x = (const stack_ent_t *)a, *y = (const stack_ent_t *)b;
    if (x->z != y->z) return x->z < y->z ? -1 : 1;
    return x->order < y->order ? -1 : x->order > y->order;
}

/* Paint b as a stacking context: own content, then positioned descendants by z. */
static void paint_context(page_t *pg, canvas_t *c, box_t *b, float x, float y, int depth) {
    if (depth > 100) return;
    stack_list_t sl = {0};
    float saved_alpha = c->alpha;
    paint_box(pg, c, b, x, y, &sl, depth);
    if (b->st->opacity < 1) c->alpha *= b->st->opacity;
    if (sl.n > 1) w_sort(sl.v, (size_t)sl.n, sizeof(stack_ent_t), cmp_stack);
    for (int i = 0; i < sl.n; i++) {
        stack_ent_t *e = &sl.v[i];
        int saved[4]; memcpy(saved, c->clip, sizeof saved);
        float sa = c->alpha;
        /* absolutely positioned boxes escape overflow clips of non-positioned ancestors;
         * relative ones keep the clip they were found under */
        if (e->b->st->position == POS_RELATIVE || e->b->st->position == POS_STICKY) memcpy(c->clip, e->clip, sizeof saved);
        c->alpha = e->alpha;
        paint_context(pg, c, e->b, e->x, e->y, depth + 1);
        memcpy(c->clip, saved, sizeof saved);
        c->alpha = sa;
    }
    c->alpha = saved_alpha;
    free(sl.v);
}

void paint_page(page_t *pg, canvas_t *c) {
    /* canvas background */
    u32 bg = pg->bg | 0xFF000000u;
    for (int yy = 0; yy < c->h; yy++) {
        u32 *row = c->px + (size_t)yy * c->stride;
        for (int xx = 0; xx < c->w; xx++) row[xx] = bg;
    }
    if (!pg->root) return;
    box_t *root = pg->root;
    paint_context(pg, c, root, root->x, root->y, 0);
}
