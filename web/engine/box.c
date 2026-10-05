/* web/engine/box.c -- DOM + computed styles -> box tree.
 *
 * Produces element boxes, text boxes, <br>, replaced boxes (img, form
 * controls, media placeholders) and list markers, then repairs the tree so
 * every block container holds either only block-level or only inline-level
 * children (anonymous block wrappers), and flex/grid items are blockified. */
#include "engine.h"
#include <stdio.h>

enum { RK_IMG = 0, RK_INPUT, RK_CHECK, RK_RADIO, RK_SELECT, RK_TEXTAREA, RK_MEDIA, RK_SVG };

static box_t *new_box(page_t *pg, u8 kind, style_t *st, lxb_dom_element_t *el) {
    box_t *b = (box_t *)pg_alloc(pg, sizeof(box_t));
    if (!b) return NULL;
    b->kind = kind; b->st = st; b->el = el;
    return b;
}

static void append(box_t *parent, box_t *c) {
    c->parent = parent; c->next = NULL; c->prev = parent->last;
    if (parent->last) parent->last->next = c; else parent->first = c;
    parent->last = c;
}

static void unlink_box(box_t *c) {
    box_t *p = c->parent;
    if (c->prev) c->prev->next = c->next; else if (p) p->first = c->next;
    if (c->next) c->next->prev = c->prev; else if (p) p->last = c->prev;
    c->next = c->prev = NULL; c->parent = NULL;
}

static bool display_inline_level(u8 d) {
    return d == D_INLINE || d == D_INLINE_BLOCK || d == D_INLINE_FLEX || d == D_INLINE_GRID ||
           d == D_INLINE_TABLE;
}

static u8 inner_of(u8 d) {
    switch (d) {
    case D_FLEX: case D_INLINE_FLEX: return IN_FLEX;
    case D_GRID: case D_INLINE_GRID: return IN_GRID;
    case D_TABLE: case D_INLINE_TABLE: return IN_TABLE;
    case D_TABLE_ROW: return IN_ROW;
    case D_TABLE_CELL: return IN_CELL;
    case D_TABLE_ROW_GROUP: return IN_ROWGROUP;
    case D_INLINE_BLOCK: case D_FLOW_ROOT: case D_TABLE_CAPTION: return IN_FLOW_ROOT;
    default: return IN_FLOW;
    }
}

static const char *attr(lxb_dom_element_t *el, const char *name, size_t *len) {
    size_t l = 0;
    const lxb_char_t *v = lxb_dom_element_get_attribute(el, (const lxb_char_t *)name, strlen(name), &l);
    if (len) *len = l;
    return (const char *)v;
}

static char *arena_str(page_t *pg, const char *s, size_t n) {
    char *d = (char *)pg_alloc(pg, n + 1);
    if (d) { memcpy(d, s, n); d[n] = 0; }
    return d;
}

static void apply_transform(char *s, u32 n, u8 tt, bool *word_start) {
    for (u32 i = 0; i < n; i++) {
        char c = s[i];
        if (tt == TT_UPPER || (tt == TT_CAP && *word_start)) { if (c >= 'a' && c <= 'z') s[i] = (char)(c - 32); }
        else if (tt == TT_LOWER) { if (c >= 'A' && c <= 'Z') s[i] = (char)(c + 32); }
        *word_start = (c == ' ' || c == '\n' || c == '\t' || c == '-');
    }
}

/* ---- list markers ---- */
static void roman(int n, bool upper, char *out, size_t cap) {
    static const int v[] = {1000,900,500,400,100,90,50,40,10,9,5,4,1};
    static const char *s[] = {"m","cm","d","cd","c","xc","l","xl","x","ix","v","iv","i"};
    size_t o = 0; out[0] = 0;
    for (int i = 0; i < 13 && n > 0; i++)
        while (n >= v[i] && o + 3 < cap) {
            for (const char *p = s[i]; *p; p++) out[o++] = upper ? (char)(*p - 32) : *p;
            n -= v[i];
        }
    out[o] = 0;
}

static void marker_text(u8 ls, int idx, char *out, size_t cap) {
    switch (ls) {
    case LS_DISC:   snprintf(out, cap, "\xE2\x80\xA2"); break;      /* • */
    case LS_CIRCLE: snprintf(out, cap, "\xE2\x97\xA6"); break;      /* ◦ */
    case LS_SQUARE: snprintf(out, cap, "\xE2\x96\xAA"); break;      /* ▪ */
    case LS_DECIMAL: snprintf(out, cap, "%d.", idx); break;
    case LS_LOWER_ALPHA: case LS_UPPER_ALPHA: {
        char b[8]; int n = idx, o = 0;
        while (n > 0 && o < 6) { n--; b[o++] = (char)((ls == LS_LOWER_ALPHA ? 'a' : 'A') + n % 26); n /= 26; }
        char r[8]; for (int i = 0; i < o; i++) r[i] = b[o - 1 - i]; r[o] = 0;
        snprintf(out, cap, "%s.", r); break; }
    case LS_LOWER_ROMAN: case LS_UPPER_ROMAN: {
        char r[32]; roman(idx, ls == LS_UPPER_ROMAN, r, sizeof r);
        snprintf(out, cap, "%s.", r); break; }
    default: out[0] = 0;
    }
}

/* ---- replaced elements ---- */
static float attr_px(lxb_dom_element_t *el, const char *name) {
    size_t l; const char *v = attr(el, name, &l);
    if (!v || !l) return -1;
    char buf[32]; snprintf(buf, sizeof buf, "%.*s", (int)(l < 31 ? l : 31), v);
    if (strchr(buf, '%')) return -1;
    float f = strtof(buf, NULL);
    return f > 0 ? f : -1;
}

static const char *pick_src(lxb_dom_element_t *el, size_t *len) {
    size_t l; const char *v;
    /* lazy-loading conventions first, when src is a placeholder */
    const char *src = attr(el, "src", &l);
    bool placeholder = !src || !l || (l > 5 && strncmp(src, "data:", 5) == 0 && l < 200);
    if (placeholder) {
        static const char *lazy[] = {"data-src", "data-lazy-src", "data-original", "data-url", "data-hi-res-src", NULL};
        for (int i = 0; lazy[i]; i++) if ((v = attr(el, lazy[i], len)) && *len) return v;
        if ((v = attr(el, "srcset", &l)) && l) {
            const char *e = v; while (e < v + l && *e != ' ' && *e != ',') e++;
            *len = (size_t)(e - v); return v;
        }
        if ((v = attr(el, "data-srcset", &l)) && l) {
            const char *e = v; while (e < v + l && *e != ' ' && *e != ',') e++;
            *len = (size_t)(e - v); return v;
        }
    }
    *len = l;
    return src;
}

static box_t *make_replaced(page_t *pg, lxb_dom_element_t *el, style_t *st, uintptr_t tag) {
    box_t *b = new_box(pg, K_REPLACED, st, el);
    if (!b) return NULL;
    b->iw = attr_px(el, "width"); b->ih = attr_px(el, "height");
    size_t l; const char *v;
    if (tag == LXB_TAG_IMG || (tag == LXB_TAG_INPUT && (v = attr(el, "type", &l)) && w_ieqn(v, l, "image"))) {
        b->rkind = RK_IMG;
        const char *src = pick_src(el, &l);
        if (src && l && l < 4000) {
            char *ref = (char *)malloc(l + 1), url[4096];
            if (ref) {
                memcpy(ref, src, l); ref[l] = 0;
                if (strncmp(ref, "data:", 5) == 0) snprintf(url, sizeof url, "%s", ref);
                else url_resolve(pg->url, ref, url, sizeof url);
                free(ref);
                b->img = img_request(pg, url);
            }
        }
        if ((v = attr(el, "alt", &l)) && l) b->alt = arena_str(pg, v, l < 200 ? l : 200);
        return b;
    }
    if (tag == LXB_TAG_INPUT) {
        const char *type = attr(el, "type", &l);
        size_t tl = l;
        if (type && (w_ieqn(type, tl, "checkbox"))) { b->rkind = RK_CHECK; b->iw = b->ih = 13;
            if (lxb_dom_element_has_attribute(el, (const lxb_char_t *)"checked", 7)) b->alt = "x";
            return b; }
        if (type && (w_ieqn(type, tl, "radio"))) { b->rkind = RK_RADIO; b->iw = b->ih = 13;
            if (lxb_dom_element_has_attribute(el, (const lxb_char_t *)"checked", 7)) b->alt = "x";
            return b; }
        b->rkind = RK_INPUT;
        const char *val = attr(el, "value", &l);
        bool pw = type && w_ieqn(type, tl, "password");
        if (val && l) {
            if (pw) { char *s = (char *)pg_alloc(pg, l + 1); if (s) { memset(s, '*', l); } b->alt = s; }
            else b->alt = arena_str(pg, val, l);
        } else if ((v = attr(el, "placeholder", &l)) && l) { b->alt = arena_str(pg, v, l); b->colspan = 1; /* placeholder flag */ }
        float sz = attr_px(el, "size");
        b->iw = (sz > 0 ? sz : 20) * st->font_size * 0.5f + 8;
        if (b->iw > 600) b->iw = 600;
        b->ih = -1;
        return b;
    }
    if (tag == LXB_TAG_TEXTAREA) {
        b->rkind = RK_TEXTAREA;
        size_t tl = 0;
        lxb_char_t *t = lxb_dom_node_text_content(lxb_dom_interface_node(el), &tl);
        if (t) { b->alt = arena_str(pg, (const char *)t, tl); lxb_dom_document_destroy_text(lxb_dom_interface_node(el)->owner_document, t); }
        if (!b->alt || !*b->alt) { if ((v = attr(el, "placeholder", &l)) && l) { b->alt = arena_str(pg, v, l); b->colspan = 1; } }
        float cols = attr_px(el, "cols"), rows = attr_px(el, "rows");
        b->iw = (cols > 0 ? cols : 20) * st->font_size * 0.55f + 8;
        b->ih = (rows > 0 ? rows : 2) * st->font_size * 1.25f + 6;
        return b;
    }
    if (tag == LXB_TAG_SELECT) {
        b->rkind = RK_SELECT;
        /* selected option, else first */
        lxb_dom_node_t *first = NULL, *sel = NULL;
        lxb_dom_node_t *n = lxb_dom_interface_node(el)->first_child;
        while (n) {
            if (lxb_dom_node_tag_id(n) == LXB_TAG_OPTION) {
                if (!first) first = n;
                if (lxb_dom_element_has_attribute(lxb_dom_interface_element(n), (const lxb_char_t *)"selected", 8)) { sel = n; break; }
            }
            /* descend into optgroup */
            if (n->first_child && lxb_dom_node_tag_id(n) == LXB_TAG_OPTGROUP) { n = n->first_child; continue; }
            while (n && !n->next && n->parent != lxb_dom_interface_node(el)) n = n->parent;
            n = n ? n->next : NULL;
        }
        if (!sel) sel = first;
        if (sel) {
            size_t tl = 0;
            lxb_char_t *t = lxb_dom_node_text_content(sel, &tl);
            if (t) { b->alt = arena_str(pg, (const char *)t, tl < 120 ? tl : 120); lxb_dom_document_destroy_text(sel->owner_document, t); }
        }
        b->iw = -1; b->ih = -1;
        return b;
    }
    if (tag == LXB_TAG_SVG) {
        b->rkind = RK_SVG;
        if (b->iw < 0 || b->ih < 0) {
            const char *vb = attr(el, "viewBox", &l);
            float vw = 0, vh = 0;
            if (vb && l) {
                char tmp[64]; snprintf(tmp, sizeof tmp, "%.*s", (int)(l < 63 ? l : 63), vb);
                for (char *q = tmp; *q; q++) if (*q == ',') *q = ' ';
                char *p2 = tmp;
                strtof(p2, &p2); strtof(p2, &p2);
                vw = strtof(p2, &p2); vh = strtof(p2, NULL);
            }
            bool vbok = vw > 0 && vh > 0;
            if (b->iw < 0 && b->ih > 0 && vbok) b->iw = b->ih * vw / vh;
            else if (b->ih < 0 && b->iw > 0 && vbok) b->ih = b->iw * vh / vw;
            else if (b->iw < 0 && b->ih < 0) {
                if (vbok) { b->ih = st->font_size; b->iw = st->font_size * vw / vh; }
                else { b->iw = 300; b->ih = 150; }
            }
        }
        return b;
    }
    /* iframe, video, canvas, embed, object, audio */
    b->rkind = RK_MEDIA;
    if (b->iw < 0) b->iw = 300;
    if (b->ih < 0) b->ih = 150;
    if (tag == LXB_TAG_VIDEO && (v = attr(el, "poster", &l)) && l && l < 2000) {
        char ref[2048], url[2048]; snprintf(ref, sizeof ref, "%.*s", (int)l, v);
        url_resolve(pg->url, ref, url, sizeof url);
        b->img = img_request(pg, url);
    }
    if (tag == LXB_TAG_IFRAME && (v = attr(el, "src", &l)) && l) b->alt = arena_str(pg, v, l < 200 ? l : 200);
    if (tag == LXB_TAG_AUDIO) { b->iw = 300; b->ih = 40; }
    return b;
}

/* ---- builder ---- */
typedef struct { page_t *pg; int depth; } bctx_t;

static void build_children(bctx_t *bc, lxb_dom_node_t *n, box_t *parent);
static box_t *make_pseudo(bctx_t *bc, style_t *ps, const char *txt);


static void add_marker(bctx_t *bc, box_t *li, int idx) {
    if (li->st->list_style == LS_NONE) return;
    char txt[40];
    marker_text(li->st->list_style, idx, txt, sizeof txt);
    if (!txt[0]) return;
    style_t *ms = css_anon_style(bc->pg, li->st, D_INLINE);
    ms->deco = 0; ms->text_align = TA_LEFT; ms->text_indent = 0;
    box_t *m = new_box(bc->pg, K_MARKER, ms, NULL);
    if (!m) return;
    size_t tl = strlen(txt);
    m->text = arena_str(bc->pg, txt, tl + 1);
    if (m->text && li->st->list_inside) { m->text[tl] = ' '; m->tlen = (u32)tl + 1; }
    else m->tlen = (u32)tl;
    m->outer_inline = (u8)li->st->list_inside;
    m->list_index = idx;
    append(li, m);
}

static box_t *build_element(bctx_t *bc, lxb_dom_element_t *el, const style_t *ps) {
    uintptr_t tag = lxb_dom_element_tag_id(el);
    if (tag == LXB_TAG_SCRIPT || tag == LXB_TAG_STYLE || tag == LXB_TAG_HEAD ||
        tag == LXB_TAG_TEMPLATE || tag == LXB_TAG_NOSCRIPT || tag == LXB_TAG_TITLE ||
        tag == LXB_TAG_META || tag == LXB_TAG_LINK) return NULL;
    style_t *st = css_compute(bc->pg, el, ps);
    if (!st || st->display == D_NONE) return NULL;
    if (tag == LXB_TAG_BR) {
        box_t *b = new_box(bc->pg, K_BR, st, el);
        if (b) b->outer_inline = 1;
        return b;
    }
    bool replaced = tag == LXB_TAG_IMG || tag == LXB_TAG_INPUT || tag == LXB_TAG_TEXTAREA ||
                    tag == LXB_TAG_SELECT || tag == LXB_TAG_IFRAME || tag == LXB_TAG_VIDEO ||
                    tag == LXB_TAG_CANVAS || tag == LXB_TAG_EMBED || tag == LXB_TAG_OBJECT ||
                    tag == LXB_TAG_SVG || tag == LXB_TAG_AUDIO;
    if (tag == LXB_TAG_INPUT) {
        size_t l; const char *t = attr(el, "type", &l);
        if (t && (w_ieqn(t, l, "submit") || w_ieqn(t, l, "button") || w_ieqn(t, l, "reset"))) {
            /* a button with its value as a text child */
            box_t *b = new_box(bc->pg, K_ELEM, st, el);
            if (!b) return NULL;
            if (st->display == D_INLINE) st->display = D_INLINE_BLOCK;
            b->inner = IN_FLOW_ROOT; b->outer_inline = display_inline_level(st->display);
            size_t vl; const char *v = attr(el, "value", &vl);
            if (!v || !vl) { v = w_ieqn(t, l, "reset") ? "Reset" : w_ieqn(t, l, "submit") ? "Submit" : ""; vl = strlen(v); }
            box_t *tx = new_box(bc->pg, K_TEXT, css_anon_style(bc->pg, st, D_INLINE), NULL);
            if (tx) { tx->text = arena_str(bc->pg, v, vl); tx->tlen = (u32)vl; tx->outer_inline = 1; append(b, tx); }
            return b;
        }
    }
    if (tag == LXB_TAG_AUDIO && !lxb_dom_element_has_attribute(el, (const lxb_char_t *)"controls", 8)) return NULL;
    box_t *b;
    if (replaced) {
        b = make_replaced(bc->pg, el, st, tag);
        if (!b) return NULL;
        if (st->display == D_INLINE) st->display = D_INLINE_BLOCK;
        b->outer_inline = display_inline_level(st->display) && st->float_ == FL_NONE &&
                          st->position != POS_ABSOLUTE && st->position != POS_FIXED;
        b->is_float = st->float_ != FL_NONE;
        b->is_abs = st->position == POS_ABSOLUTE || st->position == POS_FIXED;
        b->is_fixed = st->position == POS_FIXED;
        return b;
    }
    b = new_box(bc->pg, K_ELEM, st, el);
    if (!b) return NULL;
    b->inner = inner_of(st->display);
    b->outer_inline = display_inline_level(st->display);
    b->is_float = st->float_ != FL_NONE;
    b->is_abs = st->position == POS_ABSOLUTE || st->position == POS_FIXED;
    b->is_fixed = st->position == POS_FIXED;
    if (b->is_float || b->is_abs) b->outer_inline = 0;
    if (tag == LXB_TAG_TD || tag == LXB_TAG_TH) {
        size_t l; const char *v;
        b->colspan = (v = attr(el, "colspan", &l)) ? (i32)strtol(v, NULL, 10) : 1;
        b->rowspan = (v = attr(el, "rowspan", &l)) ? (i32)strtol(v, NULL, 10) : 1;
        if (b->colspan < 1) b->colspan = 1;
        if (b->colspan > 64) b->colspan = 64;
    }
    if (st->display == D_LIST_ITEM) {
        int idx = 1;
        lxb_dom_node_t *pn = lxb_dom_interface_node(el)->parent;
        if (pn && lxb_dom_node_tag_id(pn) == LXB_TAG_OL) {
            size_t l; const char *v = attr(lxb_dom_interface_element(pn), "start", &l);
            idx = v ? (int)strtol(v, NULL, 10) : 1;
            bool rev = lxb_dom_element_has_attribute(lxb_dom_interface_element(pn), (const lxb_char_t *)"reversed", 8);
            for (lxb_dom_node_t *s = pn->first_child; s && s != lxb_dom_interface_node(el); s = s->next)
                if (lxb_dom_node_tag_id(s) == LXB_TAG_LI) idx += rev ? -1 : 1;
        } else if (pn) {
            for (lxb_dom_node_t *s = pn->first_child; s && s != lxb_dom_interface_node(el); s = s->next)
                if (lxb_dom_node_tag_id(s) == LXB_TAG_LI) idx++;
        }
        size_t l; const char *v = attr(el, "value", &l);
        if (v) idx = (int)strtol(v, NULL, 10);
        add_marker(bc, b, idx);
    }
    if (bc->depth < 400) {
        bc->depth++;
        build_children(bc, lxb_dom_interface_node(el), b);
        bc->depth--;
    }
    /* generated content */
    for (int k = 0; k < 2; k++) {
        if (!st->pseudo[k]) continue;
        box_t *pb = make_pseudo(bc, st->pseudo[k], st->pseudo_text[k]);
        if (!pb) continue;
        if (k == 1) { append(b, pb); continue; }
        /* ::before goes first, after a list marker */
        box_t *after = (b->first && b->first->kind == K_MARKER) ? b->first : NULL;
        pb->parent = b;
        pb->prev = after;
        pb->next = after ? after->next : b->first;
        if (pb->next) pb->next->prev = pb; else b->last = pb;
        if (after) after->next = pb; else b->first = pb;
    }
    return b;
}

static box_t *make_pseudo(bctx_t *bc, style_t *ps, const char *txt) {
    box_t *b = new_box(bc->pg, K_ELEM, ps, NULL);
    if (!b) return NULL;
    b->inner = inner_of(ps->display);
    b->outer_inline = display_inline_level(ps->display);
    b->is_float = ps->float_ != FL_NONE;
    b->is_abs = ps->position == POS_ABSOLUTE || ps->position == POS_FIXED;
    b->is_fixed = ps->position == POS_FIXED;
    if (b->is_float || b->is_abs) b->outer_inline = 0;
    size_t n = txt ? strlen(txt) : 0;
    if (n) {
        box_t *t = new_box(bc->pg, K_TEXT, ps, NULL);
        if (t) {
            t->text = arena_str(bc->pg, txt, n);
            t->tlen = (u32)n;
            bool ws = true;
            if (ps->text_transform && t->text) apply_transform(t->text, t->tlen, ps->text_transform, &ws);
            t->outer_inline = 1;
            append(b, t);
        }
    }
    return b;
}

static void build_children(bctx_t *bc, lxb_dom_node_t *n, box_t *parent) {
    const style_t *ps = parent->st;
    bool word_start = true;
    for (lxb_dom_node_t *c = n->first_child; c; c = c->next) {
        if (c->type == LXB_DOM_NODE_TYPE_TEXT) {
            lxb_dom_text_t *tn = lxb_dom_interface_text(c);
            const char *s = (const char *)tn->char_data.data.data;
            size_t tl = tn->char_data.data.length;
            if (!s || !tl) continue;
            box_t *tb = new_box(bc->pg, K_TEXT, (style_t *)ps, NULL);
            if (!tb) continue;
            tb->text = arena_str(bc->pg, s, tl);
            tb->tlen = (u32)tl;
            if (ps->text_transform && tb->text) apply_transform(tb->text, tb->tlen, ps->text_transform, &word_start);
            tb->outer_inline = 1;
            append(parent, tb);
            continue;
        }
        if (c->type != LXB_DOM_NODE_TYPE_ELEMENT) continue;
        lxb_dom_element_t *el = lxb_dom_interface_element(c);
        box_t *cb = build_element(bc, el, ps);
        if (!cb) continue;
        if (cb->st->display == D_CONTENTS && cb->kind == K_ELEM) {
            /* splice the children in place of the element */
            box_t *k = cb->first;
            while (k) { box_t *nx = k->next; unlink_box(k); append(parent, k); k = nx; }
            continue;
        }
        append(parent, cb);
    }
}

/* ---- tree repair ---- */
static bool is_ws_text(box_t *b) {
    if (b->kind != K_TEXT) return false;
    for (u32 i = 0; i < b->tlen; i++) if (!is_ws((u8)b->text[i])) return false;
    return true;
}

static bool is_block_level(box_t *b) {
    if (b->is_float || b->is_abs) return true;
    if (b->kind == K_MARKER) return false;
    return !b->outer_inline;
}

static void fix_tree(page_t *pg, box_t *b, int depth);

/* Inline element that contains block-level children: make it a block. */
static bool inline_contains_block(box_t *b) {
    for (box_t *c = b->first; c; c = c->next)
        if (c->kind != K_MARKER && !c->is_float && !c->is_abs && !c->outer_inline) return true;
    return false;
}

static void blockify(box_t *c) {
    style_t *st = c->st;
    if (c->kind == K_TEXT || c->kind == K_BR) return;
    if (st->display == D_INLINE || st->display == D_INLINE_BLOCK) st->display = D_BLOCK;
    else if (st->display == D_INLINE_FLEX) st->display = D_FLEX;
    else if (st->display == D_INLINE_GRID) st->display = D_GRID;
    else if (st->display == D_INLINE_TABLE) st->display = D_TABLE;
    c->outer_inline = 0;
    if (c->kind == K_ELEM) c->inner = inner_of(st->display);
    if (c->inner == IN_FLOW) c->inner = IN_FLOW_ROOT;
}

static void wrap_inline_runs(page_t *pg, box_t *b) {
    box_t *c = b->first;
    while (c) {
        if (is_block_level(c)) { c = c->next; continue; }
        /* collect a run of inline-level children */
        box_t *start = c, *end = c;
        while (end->next && !is_block_level(end->next)) end = end->next;
        box_t *after = end->next;
        /* whitespace-only runs between blocks vanish (unless pre) */
        bool all_ws = true;
        for (box_t *k = start; ; k = k->next) { if (!is_ws_text(k) || k->st->white_space >= WS_PRE) { all_ws = false; } if (k == end) break; }
        if (all_ws) {
            box_t *k = start;
            while (k != after) { box_t *nx = k->next; unlink_box(k); k = nx; }
            c = after; continue;
        }
        style_t *as = css_anon_style(pg, b->st, D_BLOCK);
        box_t *anon = new_box(pg, K_ELEM, as, NULL);
        if (!anon) return;
        anon->anon = 1; anon->inner = IN_FLOW;
        /* insert anon before start */
        anon->parent = b; anon->prev = start->prev; anon->next = after;
        if (start->prev) start->prev->next = anon; else b->first = anon;
        if (after) after->prev = anon; else b->last = anon;
        box_t *k = start;
        while (k != after) {
            box_t *nx = k->next;
            k->parent = anon; k->prev = anon->last; k->next = NULL;
            if (anon->last) anon->last->next = k; else anon->first = k;
            anon->last = k;
            k = nx;
        }
        c = after;
    }
}

/* Block container: wrap inline runs when mixed with block-level children. */
static void fix_container(page_t *pg, box_t *b) {
    bool any_block = false, any_inline = false;
    for (box_t *c = b->first; c; c = c->next) {
        if (is_block_level(c)) any_block = true; else any_inline = true;
    }
    if (any_block && any_inline) wrap_inline_runs(pg, b);
    if (!any_block && any_inline) b->has_inline_kids = 1;
    for (box_t *c = b->first; c; c = c->next)
        if (c->anon) c->has_inline_kids = 1;
}

static void fix_tree(page_t *pg, box_t *b, int depth) {
    if (depth > 500 || b->kind != K_ELEM) return;
    /* children first so inline->block promotion is known */
    for (box_t *c = b->first; c; c = c->next) fix_tree(pg, c, depth + 1);
    for (box_t *c = b->first; c; c = c->next)
        if (c->kind == K_ELEM && c->outer_inline && c->st->display == D_INLINE && inline_contains_block(c)) {
            c->st->display = D_BLOCK; c->outer_inline = 0; c->inner = IN_FLOW;
            fix_container(pg, c);
        }
    if (b->inner == IN_FLEX || b->inner == IN_GRID) {
        /* every in-flow child becomes a block-level item; text runs wrap */
        box_t *c = b->first;
        while (c) {
            box_t *nx = c->next;
            if (c->kind == K_TEXT) {
                if (is_ws_text(c)) { unlink_box(c); c = nx; continue; }
                style_t *as = css_anon_style(pg, b->st, D_BLOCK);
                box_t *anon = new_box(pg, K_ELEM, as, NULL);
                if (anon) {
                    anon->anon = 1; anon->inner = IN_FLOW;
                    anon->parent = b; anon->prev = c->prev; anon->next = c->next;
                    if (c->prev) c->prev->next = anon; else b->first = anon;
                    if (c->next) c->next->prev = anon; else b->last = anon;
                    c->parent = anon; c->prev = c->next = NULL; anon->first = anon->last = c;
                }
            } else if (!c->is_abs) {
                bool was_inline = c->kind == K_ELEM && c->outer_inline && c->st->display == D_INLINE;
                blockify(c);
                if (c->kind == K_ELEM && (c->inner == IN_FLOW_ROOT || c->inner == IN_FLOW) && was_inline)
                    fix_container(pg, c);
            }
            c = nx;
        }
        return;
    }
    if (b->inner == IN_TABLE || b->inner == IN_ROW || b->inner == IN_ROWGROUP) {
        /* drop whitespace text; table.c tolerates loose structure */
        box_t *c = b->first;
        while (c) { box_t *nx = c->next; if (is_ws_text(c)) unlink_box(c); c = nx; }
        return;
    }
    if (b->outer_inline && b->inner == IN_FLOW && b->st->display == D_INLINE) return;
    fix_container(pg, b);
}

box_t *box_build(page_t *pg) {
    bctx_t bc = { pg, 0 };
    lxb_dom_element_t *html = lxb_dom_document_element(lxb_dom_interface_document(pg->doc));
    if (!html) return NULL;
    style_t *init = css_initial_root(pg);
    pg->root_style = NULL;
    style_t *rs = css_compute(pg, html, init);
    if (!rs) return NULL;
    pg->root_style = rs;
    /* recompute with rem resolved against the root */
    rs->display = D_BLOCK;
    box_t *root = new_box(pg, K_ELEM, rs, html);
    if (!root) return NULL;
    root->inner = IN_FLOW_ROOT;
    build_children(&bc, lxb_dom_interface_node(html), root);
    fix_tree(pg, root, 0);
    return root;
}
