/* web/engine/page.c -- page lifecycle: load, layout, paint, hit testing. */
#include "engine.h"
#include <stdio.h>

void web_init(const web_platform_t *p) { g_web = *p; font_preload(); }

static void request_bg_images(page_t *pg, box_t *b, int depth) {
    if (depth > 500) return;
    if (b->kind == K_ELEM && b->st->bg_image) img_request(pg, b->st->bg_image);
    for (box_t *c = b->first; c; c = c->next) request_bg_images(pg, c, depth + 1);
}

static void read_title(page_t *pg) {
    size_t len = 0;
    const lxb_char_t *t = lxb_html_document_title(pg->doc, &len);
    pg->title[0] = 0;
    if (t && len) {
        size_t n = len < sizeof pg->title - 1 ? len : sizeof pg->title - 1;
        memcpy(pg->title, t, n); pg->title[n] = 0;
    }
}

static void pick_canvas_bg(page_t *pg) {
    pg->bg = 0xFFFFFFFFu;
    if (!pg->root) return;
    if (pg->root->st->bg_color >> 24) { pg->bg = pg->root->st->bg_color; return; }
    for (box_t *c = pg->root->first; c; c = c->next)
        if (c->el && lxb_dom_element_tag_id(c->el) == LXB_TAG_BODY) {
            if (c->st->bg_color >> 24) pg->bg = c->st->bg_color;
            break;
        }
}

/* Any <script> that would run (no type, or a JavaScript type)? */
static bool doc_has_scripts(lxb_dom_node_t *n, int depth) {
    if (depth > 400) return false;
    for (lxb_dom_node_t *c = n->first_child; c; c = c->next) {
        if (c->type != LXB_DOM_NODE_TYPE_ELEMENT) continue;
        if (lxb_dom_node_tag_id(c) == LXB_TAG_SCRIPT) {
            size_t l = 0;
            const char *t = (const char *)lxb_dom_element_get_attribute(lxb_dom_interface_element(c), (const lxb_char_t *)"type", 4, &l);
            if (!t || !l || w_istr(t, l, "javascript") || w_istr(t, l, "ecmascript")) return true;
            continue;
        }
        if (doc_has_scripts(c, depth + 1)) return true;
    }
    return false;
}

bool web_page_has_scripts(web_page_t *pg) { return pg && pg->has_scripts && !pg->js; }

web_page_t *web_page_load(const char *url, const char *html, size_t len, int vw, int vh) {
    return web_page_load_ex(url, html, len, vw, vh, 0);
}

web_page_t *web_page_load_ex(const char *url, const char *html, size_t len, int vw, int vh, unsigned flags) {
    page_t *pg = (page_t *)calloc(1, sizeof(page_t));
    if (!pg) return NULL;
    snprintf(pg->url, sizeof pg->url, "%s", url ? url : "about:blank");
    pg->vw = vw > 0 ? vw : 800; pg->vh = vh > 0 ? vh : 600;
    pg->doc = lxb_html_document_create();
    if (!pg->doc) { free(pg); return NULL; }
    lxb_html_document_parse(pg->doc, (const lxb_char_t *)(html ? html : ""), html ? len : 0);
    /* <base href> */
    lxb_dom_collection_t *col = lxb_dom_collection_make(lxb_dom_interface_document(pg->doc), 4);
    if (col) {
        lxb_dom_element_t *head = lxb_dom_interface_element(pg->doc->head);
        if (head && lxb_dom_elements_by_tag_name(head, col, (const lxb_char_t *)"base", 4) == LXB_STATUS_OK &&
            lxb_dom_collection_length(col) > 0) {
            size_t hl = 0;
            const lxb_char_t *h = lxb_dom_element_get_attribute(lxb_dom_collection_element(col, 0), (const lxb_char_t *)"href", 4, &hl);
            if (h && hl && hl < 900) {
                char ref[1024], abs_[1024];
                snprintf(ref, sizeof ref, "%.*s", (int)hl, (const char *)h);
                url_resolve(pg->url, ref, abs_, sizeof abs_);
                snprintf(pg->url, sizeof pg->url, "%s", abs_);
            }
        }
        lxb_dom_collection_destroy(col, true);
    }
    read_title(pg);
    pg->has_scripts = doc_has_scripts(lxb_dom_interface_node(pg->doc), 0);
    if (!(flags & WEB_LOAD_NO_SCRIPTS) && pg->has_scripts) {
        pg->js = js_create(pg);
        if (pg->js) js_run_scripts(pg->js);
    }
    uint64_t t0 = g_web.now_ms ? g_web.now_ms(g_web.ctx) : 0;
    css_load_document_styles(pg);
    uint64_t t1 = g_web.now_ms ? g_web.now_ms(g_web.ctx) : 0;
    pg->root = box_build(pg);
    uint64_t t2 = g_web.now_ms ? g_web.now_ms(g_web.ctx) : 0;
    if (pg->root) request_bg_images(pg, pg->root, 0);
    pick_canvas_bg(pg);
    layout_document(pg);
    pg->js_dirty = 0;
    if (g_web.now_ms) {
        uint64_t t3 = g_web.now_ms(g_web.ctx);
        web_log("load %s: styles %ums, boxes %ums, layout %ums (%d sheets)", pg->url,
                (unsigned)(t1 - t0), (unsigned)(t2 - t1), (unsigned)(t3 - t2), pg->nsheets);
    }
    pg->need_layout = 0;
    if (url) snprintf(pg->url, sizeof pg->url, "%s", url);
    return pg;
}

static void free_box_arrays(box_t *b, int depth);

static box_t *find_focus(box_t *b, lxb_dom_element_t *el, int depth) {
    if (!b || !el || depth > 500) return NULL;
    if (b->el == el && b->kind == K_REPLACED) return b;
    for (box_t *c = b->first; c; c = c->next) {
        box_t *r = find_focus(c, el, depth + 1);
        if (r) return r;
    }
    return NULL;
}

void web_page_restyle_and_relayout(page_t *pg) {
    if (!pg || !pg->doc) return;
    lxb_dom_element_t *focused = pg->focus ? pg->focus->el : NULL;
    if (pg->root) free_box_arrays(pg->root, 0);
    pg->root = NULL;
    pg->root_style = NULL;
    pg->focus = NULL;
    for (size_t i = 0; i < pg->nblocks; i++) free(pg->blocks[i]);
    free(pg->blocks);
    pg->blocks = NULL;
    pg->nblocks = pg->capblocks = 0;
    pg->arena = NULL;
    pg->arena_used = pg->arena_cap = 0;

    css_restyle(pg);
    pg->root = box_build(pg);
    pg->focus = find_focus(pg->root, focused, 0);
    if (pg->root) request_bg_images(pg, pg->root, 0);
    pick_canvas_bg(pg);
    layout_document(pg);
    pg->generation++;
    pg->need_layout = 0;
}

bool web_page_step(web_page_t *pg) {
    if (!pg || !pg->js) return false;
    int ran = js_run_timers(pg->js);
    if (pg->js_dirty) {
        pg->js_dirty = 0;
        web_page_restyle_and_relayout(pg);
        return true;
    }
    return ran > 0;
}

int web_page_take_scroll(web_page_t *pg) {
    return pg && pg->js ? js_take_scroll(pg->js) : -1;
}

bool web_page_take_navigation(web_page_t *pg, char *url, size_t cap) {
    if (!pg || !pg->js) return false;
    return js_take_navigation(pg->js, url, cap);
}

int web_page_load_images(web_page_t *pg, int max_images) {
    int done = 0, pending = 0;
    for (web_img_t *im = pg->imgs; im; im = im->next) {
        if (im->state != 0) continue;
        if (max_images > 0 && done >= max_images) { pending++; continue; }
        img_load(pg, im);
        done++;
        pg->need_layout = 1;
    }
    return pending;
}

bool web_page_relayout_if_needed(web_page_t *pg, int vw, int vh) {
    if (!pg) return false;
    if (vw <= 0) vw = pg->vw;
    if (vh <= 0) vh = pg->vh;
    if (pg->js_dirty) {
        pg->vw = vw; pg->vh = vh;
        pg->js_dirty = 0;
        web_page_restyle_and_relayout(pg);
        return true;
    }
    if (!pg->need_layout && vw == pg->vw && vh == pg->vh) return false;
    pg->vw = vw; pg->vh = vh;
    pg->need_layout = 0;
    layout_document(pg);
    pg->generation++;
    return true;
}

int web_page_height(web_page_t *pg) { return pg ? (int)(pg->doc_h + 0.5f) : 0; }
int web_page_width(web_page_t *pg) { return pg ? (int)(pg->doc_w + 0.5f) : 0; }
const char *web_page_title(web_page_t *pg) { return pg ? pg->title : ""; }
const char *web_page_url(web_page_t *pg) { return pg ? pg->url : ""; }

void web_page_paint(web_page_t *pg, uint32_t *px, int cw, int ch, int stride, int sx, int sy) {
    if (pg) pg->scroll_y = sy;
    canvas_t c = { px, cw, ch, stride, sx, sy, { 0, 0, cw, ch }, 1.0f };
    paint_page(pg, &c);
}

/* ---- hit testing ---- */
typedef struct { float x, y; box_t *hit; } hit_t;

static bool inside(float px, float py, float x, float y, float w, float h) {
    return px >= x && py >= y && px < x + w && py < y + h;
}

static void hit_box(box_t *b, float x, float y, hit_t *h, int depth) {
    if (depth > 400) return;
    if (b->st->visibility == 0 && inside(h->x, h->y, x, y, b->w, b->h) && b->kind != K_TEXT) h->hit = b;
    if (b->st->overflow != OV_VISIBLE && b->parent && !inside(h->x, h->y, x, y, b->w, b->h)) return;
    for (box_t *c = b->first; c; c = c->next) {
        if (c->kind == K_TEXT || c->kind == K_BR || c->kind == K_MARKER) continue;
        if (b->has_inline_kids && c->outer_inline) continue;
        if (c->kind == K_ELEM && c->outer_inline && c->st->display == D_INLINE) {
            /* floats / abs inside inline elements */
            for (box_t *q = c->first; q; q = q->next)
                if (q->is_float || q->is_abs) hit_box(q, x + q->x + q->rel_x, y + q->y + q->rel_y, h, depth + 1);
            continue;
        }
        hit_box(c, x + c->x + c->rel_x, y + c->y + c->rel_y, h, depth + 1);
    }
    for (u32 i = 0; i < b->nfrag; i++) {
        frag_t *f = &b->frags[i];
        if (f->atomic) { hit_box(f->box, x + f->box->x + f->box->rel_x, y + f->box->y + f->box->rel_y, h, depth + 1); continue; }
        if (inside(h->x, h->y, x + f->x, y + f->y, f->w + 1, f->h)) h->hit = f->box;
    }
}

bool web_page_link_at(web_page_t *pg, int x, int y, char *href, size_t cap) {
    if (!pg || !pg->root) return false;
    hit_t h = { (float)x, (float)y, NULL };
    hit_box(pg->root, pg->root->x, pg->root->y, &h, 0);
    for (box_t *b = h.hit; b; b = b->parent) {
        if (!b->el) continue;
        if (lxb_dom_element_tag_id(b->el) == LXB_TAG_A) {
            size_t l = 0;
            const lxb_char_t *v = lxb_dom_element_get_attribute(b->el, (const lxb_char_t *)"href", 4, &l);
            if (!v || !l) return false;
            char ref[2048];
            snprintf(ref, sizeof ref, "%.*s", (int)(l < 2047 ? l : 2047), (const char *)v);
            if (strncmp(ref, "javascript:", 11) == 0) return false;
            url_resolve(pg->url, ref, href, cap);
            return true;
        }
    }
    return false;
}

/* ---- anchors ---- */
static bool find_el_y(box_t *b, lxb_dom_element_t *target, float x, float y, float *out, int depth) {
    if (depth > 400) return false;
    if (b->el == target) { *out = y; return true; }
    for (box_t *c = b->first; c; c = c->next) {
        if (c->kind == K_TEXT) continue;
        if (c->el == target && c->kind == K_ELEM && c->outer_inline && c->st->display == D_INLINE) {
            /* inline target: first fragment of its first text child */
            for (u32 i = 0; i < b->nfrag; i++) {
                box_t *fb = b->frags[i].box;
                for (box_t *p = fb; p; p = p->parent) if (p == c) { *out = y + b->frags[i].y; return true; }
            }
            *out = y; return true;
        }
        if (find_el_y(c, target, x + c->x, y + c->y, out, depth + 1)) return true;
    }
    return false;
}

static lxb_dom_element_t *find_by_id(lxb_dom_node_t *n, const char *id, size_t il, int depth) {
    if (depth > 500) return NULL;
    for (lxb_dom_node_t *c = n->first_child; c; c = c->next) {
        if (c->type != LXB_DOM_NODE_TYPE_ELEMENT) continue;
        lxb_dom_element_t *el = lxb_dom_interface_element(c);
        size_t l = 0;
        const lxb_char_t *v = lxb_dom_element_get_attribute(el, (const lxb_char_t *)"id", 2, &l);
        if (v && l == il && memcmp(v, id, il) == 0) return el;
        v = lxb_dom_element_get_attribute(el, (const lxb_char_t *)"name", 4, &l);
        if (v && l == il && memcmp(v, id, il) == 0 && lxb_dom_element_tag_id(el) == LXB_TAG_A) return el;
        lxb_dom_element_t *r = find_by_id(c, id, il, depth + 1);
        if (r) return r;
    }
    return NULL;
}

int web_page_anchor_y(web_page_t *pg, const char *id) {
    if (!pg || !pg->root || !id || !*id) return -1;
    lxb_dom_element_t *el = find_by_id(lxb_dom_interface_node(pg->doc), id, strlen(id), 0);
    if (!el) return -1;
    float y;
    if (find_el_y(pg->root, el, pg->root->x, pg->root->y, &y, 0)) return (int)y;
    return -1;
}

/* ---- teardown ---- */
static void free_box_arrays(box_t *b, int depth) {
    if (depth > 600) return;
    free(b->frags); free(b->decos);
    for (box_t *c = b->first; c; c = c->next) free_box_arrays(c, depth + 1);
}

void web_page_free(web_page_t *pg) {
    if (!pg) return;
    if (pg->js) js_destroy(pg->js);
    if (pg->root) free_box_arrays(pg->root, 0);
    img_free_all(pg);
    free(pg->sel_set);
    for (int i = 0; i < pg->nsheets; i++) lxb_css_stylesheet_destroy(pg->sheets[i], true);
    if (pg->doc) { lxb_html_document_css_destroy(pg->doc); lxb_html_document_destroy(pg->doc); }
    if (pg->css_parser) lxb_css_parser_destroy(pg->css_parser, true);
    for (size_t i = 0; i < pg->nblocks; i++) free(pg->blocks[i]);
    free(pg->blocks);
    free(pg);
}

/* ---- debugging: box tree dump ---- */
static void dump_box(box_t *b, int depth, float ax, float ay, void (*out)(const char *)) {
    if (depth > 60) return;
    char line[512]; int o = 0;
    for (int i = 0; i < depth && o < 120; i++) line[o++] = ' ';
    const char *kind = b->kind == K_TEXT ? "#text" : b->kind == K_BR ? "br" : b->kind == K_MARKER ? "marker" : NULL;
    char tag[32] = "anon", cls[64] = "";
    if (b->el) {
        size_t l = 0;
        const lxb_char_t *t = lxb_dom_element_local_name(b->el, &l);
        snprintf(tag, sizeof tag, "%.*s", (int)l, (const char *)t);
        const lxb_char_t *c = lxb_dom_element_get_attribute(b->el, (const lxb_char_t *)"class", 5, &l);
        if (c) snprintf(cls, sizeof cls, ".%.*s", (int)(l < 50 ? l : 50), (const char *)c);
    }
    if (b->kind == K_TEXT) {
        snprintf(line + o, sizeof line - o, "%s \"%.*s\"", kind, (int)(b->tlen < 40 ? b->tlen : 40), b->text);
    } else {
        snprintf(line + o, sizeof line - o, "%s%s d=%d in=%d%s%s%s [%.0f,%.0f %.0fx%.0f] frags=%u",
                 kind ? kind : tag, cls, b->st->display, b->inner, b->outer_inline ? " inl" : "",
                 b->is_float ? " float" : "", b->is_abs ? " abs" : "", ax, ay, b->w, b->h, b->nfrag);
    }
    out(line);
    for (box_t *c = b->first; c; c = c->next) {
        bool via_owner = b->kind == K_ELEM && b->outer_inline && b->st->display == D_INLINE;
        dump_box(c, depth + 1, (via_owner ? ax : ax) + c->x + c->rel_x, (via_owner ? ay : ay) + c->y + c->rel_y, out);
    }
}

void web_page_dump(web_page_t *pg, void (*out)(const char *line)) {
    if (pg && pg->root) dump_box(pg->root, 0, pg->root->x, pg->root->y, out);
}

uint32_t web_page_generation(web_page_t *pg) { return pg ? pg->generation : 0; }

/* Deepest box under a document point (text boxes included), or NULL. */
box_t *page_hit_box(page_t *pg, float x, float y) {
    if (!pg || !pg->root) return NULL;
    hit_t h = { x, y, NULL };
    hit_box(pg->root, pg->root->x, pg->root->y, &h, 0);
    return h.hit;
}
