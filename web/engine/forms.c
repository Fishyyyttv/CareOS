/* web/engine/forms.c -- form controls: focus, typing, toggles, submission.
 *
 * Live values are written back to the DOM (`value` / `checked` / `selected`
 * attributes) so submission -- and, later, scripts -- see what the user
 * typed. Submission is always a GET: the CareOS network stack has no POST,
 * and the forms that matter here (search boxes) accept GET. */
#include "engine.h"
#include <stdio.h>

enum { RK_IMG = 0, RK_INPUT, RK_CHECK, RK_RADIO, RK_SELECT, RK_TEXTAREA };

static const char *attr(lxb_dom_element_t *el, const char *name, size_t *len) {
    size_t l = 0;
    const lxb_char_t *v = lxb_dom_element_get_attribute(el, (const lxb_char_t *)name, strlen(name), &l);
    if (len) *len = l;
    return (const char *)v;
}

static bool has_attr(lxb_dom_element_t *el, const char *name) {
    return lxb_dom_element_has_attribute(el, (const lxb_char_t *)name, strlen(name));
}

static void set_attr(lxb_dom_element_t *el, const char *name, const char *v, size_t n) {
    lxb_dom_element_set_attribute(el, (const lxb_char_t *)name, strlen(name), (const lxb_char_t *)v, n);
}

static void del_attr(lxb_dom_element_t *el, const char *name) {
    lxb_dom_element_remove_attribute(el, (const lxb_char_t *)name, strlen(name));
}

static bool type_is(lxb_dom_element_t *el, const char *t) {
    size_t l; const char *v = attr(el, "type", &l);
    if (!v || !l) return w_ieq(t, "text");
    return w_ieqn(v, l, t);
}

static bool is_text_input(lxb_dom_element_t *el) {
    uintptr_t tag = lxb_dom_element_tag_id(el);
    if (tag == LXB_TAG_TEXTAREA) return true;
    if (tag != LXB_TAG_INPUT) return false;
    static const char *t[] = { "text", "search", "email", "url", "tel", "password", "number", NULL };
    size_t l; const char *v = attr(el, "type", &l);
    if (!v || !l) return true;
    for (int i = 0; t[i]; i++) if (w_ieqn(v, l, t[i])) return true;
    return false;
}

/* The box that renders element el, or NULL. */
static box_t *box_for_el(box_t *b, lxb_dom_element_t *el, int depth) {
    if (!b || depth > 500) return NULL;
    if (b->el == el && b->kind != K_TEXT) return b;
    for (box_t *c = b->first; c; c = c->next) {
        box_t *r = box_for_el(c, el, depth + 1);
        if (r) return r;
    }
    return NULL;
}

static lxb_dom_element_t *closest(lxb_dom_element_t *el, uintptr_t tag) {
    for (lxb_dom_node_t *n = lxb_dom_interface_node(el); n; n = n->parent)
        if (n->type == LXB_DOM_NODE_TYPE_ELEMENT && lxb_dom_node_tag_id(n) == tag)
            return lxb_dom_interface_element(n);
    return NULL;
}

/* ---- control value <-> box display ---- */

static char *dup_arena(page_t *pg, const char *s, size_t n) {
    char *d = (char *)pg_alloc(pg, n + 1);
    if (d) { memcpy(d, s, n); d[n] = 0; }
    return d;
}

/* Current text of a text control (value attribute, else textarea content). */
static char *control_value(lxb_dom_element_t *el, size_t *len) {
    size_t l = 0;
    const char *v = attr(el, "value", &l);
    if (v || lxb_dom_element_tag_id(el) != LXB_TAG_TEXTAREA) {
        char *r = w_strndup(v ? v : "", v ? l : 0);
        if (len) *len = v ? l : 0;
        return r;
    }
    lxb_char_t *t = lxb_dom_node_text_content(lxb_dom_interface_node(el), &l);
    char *r = w_strndup(t ? (const char *)t : "", t ? l : 0);
    if (t) lxb_dom_document_destroy_text(lxb_dom_interface_node(el)->owner_document, t);
    if (len) *len = t ? l : 0;
    return r;
}

static void sync_box(page_t *pg, box_t *b) {
    lxb_dom_element_t *el = b->el;
    if (!el) return;
    if (b->rkind == RK_CHECK || b->rkind == RK_RADIO) {
        b->alt = has_attr(el, "checked") ? "x" : NULL;
        return;
    }
    size_t l = 0;
    char *v = control_value(el, &l);
    if (!v) return;
    if (l) {
        if (lxb_dom_element_tag_id(el) == LXB_TAG_INPUT && type_is(el, "password")) {
            char *s = (char *)pg_alloc(pg, l + 1);
            if (s) memset(s, '*', l);
            b->alt = s;
        } else b->alt = dup_arena(pg, v, l);
        b->colspan = 0;
    } else {
        size_t pl; const char *ph = attr(el, "placeholder", &pl);
        b->alt = ph && pl ? dup_arena(pg, ph, pl) : NULL;
        b->colspan = ph && pl ? 1 : 0;
    }
    free(v);
}

/* ---- submission ---- */

typedef struct { char *s; size_t n, cap; } qbuf_t;

static void q_put(qbuf_t *q, const char *s, size_t n) {
    if (q->n + n * 3 + 2 > q->cap) {
        size_t nc = (q->cap + n * 3 + 64) * 2;
        char *ns = (char *)realloc(q->s, nc);
        if (!ns) return;
        q->s = ns; q->cap = nc;
    }
    memcpy(q->s + q->n, s, n); q->n += n; q->s[q->n] = 0;
}

static void q_enc(qbuf_t *q, const char *s, size_t n) {
    static const char hex[] = "0123456789ABCDEF";
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        char t[3];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '*') { t[0] = (char)c; q_put(q, t, 1); }
        else if (c == ' ') q_put(q, "+", 1);
        else { t[0] = '%'; t[1] = hex[c >> 4]; t[2] = hex[c & 15]; q_put(q, t, 3); }
    }
}

static void q_pair(qbuf_t *q, const char *k, size_t kn, const char *v, size_t vn) {
    if (q->n) q_put(q, "&", 1);
    q_enc(q, k, kn);
    q_put(q, "=", 1);
    q_enc(q, v, vn);
}

static void collect_fields(lxb_dom_node_t *n, lxb_dom_element_t *submitter, qbuf_t *q, int depth) {
    if (depth > 300) return;
    for (lxb_dom_node_t *c = n->first_child; c; c = c->next) {
        if (c->type != LXB_DOM_NODE_TYPE_ELEMENT) continue;
        lxb_dom_element_t *el = lxb_dom_interface_element(c);
        uintptr_t tag = lxb_dom_node_tag_id(c);
        size_t nl; const char *name = attr(el, "name", &nl);
        bool named = name && nl && !has_attr(el, "disabled");
        if (tag == LXB_TAG_INPUT && named) {
            size_t vl; const char *v = attr(el, "value", &vl);
            if (type_is(el, "checkbox") || type_is(el, "radio")) {
                if (has_attr(el, "checked")) q_pair(q, name, nl, v ? v : "on", v ? vl : 2);
            } else if (type_is(el, "submit") || type_is(el, "image") || type_is(el, "button") || type_is(el, "reset")) {
                if (el == submitter) q_pair(q, name, nl, v ? v : "", v ? vl : 0);
            } else if (!type_is(el, "file")) {
                q_pair(q, name, nl, v ? v : "", v ? vl : 0);
            }
        } else if (tag == LXB_TAG_TEXTAREA && named) {
            size_t vl; char *v = control_value(el, &vl);
            q_pair(q, name, nl, v ? v : "", v ? vl : 0);
            free(v);
        } else if (tag == LXB_TAG_SELECT && named) {
            lxb_dom_element_t *pick = NULL, *first = NULL;
            for (lxb_dom_node_t *o = c->first_child; o; o = o->next) {
                lxb_dom_node_t *opts[2] = { o, NULL };
                if (lxb_dom_node_tag_id(o) == LXB_TAG_OPTGROUP) opts[0] = o->first_child;
                for (lxb_dom_node_t *p = opts[0]; p; p = (o == opts[0]) ? NULL : p->next) {
                    if (lxb_dom_node_tag_id(p) != LXB_TAG_OPTION) continue;
                    lxb_dom_element_t *oe = lxb_dom_interface_element(p);
                    if (!first) first = oe;
                    if (has_attr(oe, "selected")) pick = oe;
                }
            }
            if (!pick) pick = first;
            if (pick) {
                size_t vl; const char *v = attr(pick, "value", &vl);
                if (v) q_pair(q, name, nl, v, vl);
                else {
                    size_t tl = 0;
                    lxb_char_t *t = lxb_dom_node_text_content(lxb_dom_interface_node(pick), &tl);
                    q_pair(q, name, nl, t ? (const char *)t : "", t ? tl : 0);
                    if (t) lxb_dom_document_destroy_text(lxb_dom_interface_node(pick)->owner_document, t);
                }
            }
        } else if (tag == LXB_TAG_BUTTON && named && el == submitter) {
            size_t vl; const char *v = attr(el, "value", &vl);
            q_pair(q, name, nl, v ? v : "", v ? vl : 0);
        }
        if (c->first_child && tag != LXB_TAG_SELECT && tag != LXB_TAG_TEXTAREA)
            collect_fields(c, submitter, q, depth + 1);
    }
}

static int submit(page_t *pg, lxb_dom_element_t *form, lxb_dom_element_t *submitter, char *url, size_t cap) {
    if (!form) return WEB_ACT_NONE;
    size_t al; const char *action = attr(form, "action", &al);
    if (submitter) {
        size_t fl; const char *fa = attr(submitter, "formaction", &fl);
        if (fa && fl) { action = fa; al = fl; }
    }
    char ref[1024], base[1024];
    snprintf(ref, sizeof ref, "%.*s", (int)(action ? (al < 1023 ? al : 1023) : 0), action ? action : "");
    if (strncmp(ref, "javascript:", 11) == 0) return WEB_ACT_NONE;
    url_resolve(pg->url, ref, base, sizeof base);
    char *cut = strpbrk(base, "?#");
    if (cut) *cut = 0;
    qbuf_t q = {0};
    collect_fields(lxb_dom_interface_node(form), submitter, &q, 0);
    snprintf(url, cap, "%s?%s", base, q.s ? q.s : "");
    free(q.s);
    return WEB_ACT_NAVIGATE;
}

/* ---- focus ---- */

static int focus_box(page_t *pg, box_t *b) {
    if (pg->focus == b) return WEB_ACT_NONE;
    pg->focus = b;
    pg->generation++;
    return WEB_ACT_REPAINT;
}

bool web_page_focused(web_page_t *pg) { return pg && pg->focus; }

void web_page_blur(web_page_t *pg) {
    if (pg && pg->focus) { pg->focus = NULL; pg->generation++; }
}

static void collect_text_boxes(box_t *b, box_t **out, int *n, int max, int depth) {
    if (depth > 500 || *n >= max) return;
    if (b->kind == K_REPLACED && b->el && is_text_input(b->el) && !has_attr(b->el, "disabled")) out[(*n)++] = b;
    for (box_t *c = b->first; c; c = c->next) collect_text_boxes(c, out, n, max, depth + 1);
}

/* ---- public entry points ---- */

int web_page_click(web_page_t *pg, int x, int y, char *url, size_t cap) {
    if (!pg || !pg->root) return WEB_ACT_NONE;
    box_t *hit = page_hit_box(pg, (float)x, (float)y);
    int act = WEB_ACT_NONE;
    if (pg->focus && hit != pg->focus) { pg->focus = NULL; pg->generation++; act = WEB_ACT_REPAINT; }
    box_t *b = hit;
    while (b && !b->el) b = b->parent;
    if (!b) return act;
    for (lxb_dom_node_t *n = lxb_dom_interface_node(b->el); n; n = n->parent) {
        if (n->type != LXB_DOM_NODE_TYPE_ELEMENT) continue;
        lxb_dom_element_t *el = lxb_dom_interface_element(n);
        uintptr_t tag = lxb_dom_node_tag_id(n);
        if (tag == LXB_TAG_LABEL) {
            size_t fl; const char *f = attr(el, "for", &fl);
            lxb_dom_element_t *target = NULL;
            if (f && fl) {
                /* find by id */
                lxb_dom_collection_t *col = lxb_dom_collection_make(lxb_dom_interface_document(pg->doc), 4);
                if (col) {
                    lxb_dom_elements_by_attr(lxb_dom_interface_element(pg->doc->body), col,
                                             (const lxb_char_t *)"id", 2, (const lxb_char_t *)f, fl, true);
                    if (lxb_dom_collection_length(col)) target = lxb_dom_collection_element(col, 0);
                    lxb_dom_collection_destroy(col, true);
                }
            }
            if (target) { el = target; tag = lxb_dom_element_tag_id(el); }
            else continue;
        }
        if (has_attr(el, "disabled") && (tag == LXB_TAG_INPUT || tag == LXB_TAG_BUTTON ||
                                         tag == LXB_TAG_SELECT || tag == LXB_TAG_TEXTAREA)) return act;
        if (tag == LXB_TAG_INPUT) {
            if (type_is(el, "checkbox")) {
                if (has_attr(el, "checked")) del_attr(el, "checked"); else set_attr(el, "checked", "", 0);
                box_t *cb = box_for_el(pg->root, el, 0);
                if (cb) sync_box(pg, cb);
                pg->generation++;
                return WEB_ACT_REPAINT;
            }
            if (type_is(el, "radio")) {
                size_t nl; const char *name = attr(el, "name", &nl);
                lxb_dom_element_t *form = closest(el, LXB_TAG_FORM);
                if (name && nl) {
                    lxb_dom_collection_t *col = lxb_dom_collection_make(lxb_dom_interface_document(pg->doc), 8);
                    if (col) {
                        lxb_dom_element_t *scope = form ? form : lxb_dom_interface_element(pg->doc->body);
                        lxb_dom_elements_by_attr(scope, col, (const lxb_char_t *)"name", 4,
                                                 (const lxb_char_t *)name, nl, true);
                        for (size_t i = 0; i < lxb_dom_collection_length(col); i++) {
                            lxb_dom_element_t *o = lxb_dom_collection_element(col, i);
                            if (o != el && type_is(o, "radio") && has_attr(o, "checked")) {
                                del_attr(o, "checked");
                                box_t *ob = box_for_el(pg->root, o, 0);
                                if (ob) sync_box(pg, ob);
                            }
                        }
                        lxb_dom_collection_destroy(col, true);
                    }
                }
                set_attr(el, "checked", "", 0);
                box_t *rb = box_for_el(pg->root, el, 0);
                if (rb) sync_box(pg, rb);
                pg->generation++;
                return WEB_ACT_REPAINT;
            }
            if (type_is(el, "submit") || type_is(el, "image"))
                return submit(pg, closest(el, LXB_TAG_FORM), el, url, cap);
            if (type_is(el, "button") || type_is(el, "reset") || type_is(el, "hidden")) return act;
            box_t *tb = box_for_el(pg->root, el, 0);
            if (tb) return focus_box(pg, tb);
            return act;
        }
        if (tag == LXB_TAG_TEXTAREA) {
            box_t *tb = box_for_el(pg->root, el, 0);
            if (tb) return focus_box(pg, tb);
            return act;
        }
        if (tag == LXB_TAG_SELECT) {
            /* no dropdown yet: each click selects the next option */
            lxb_dom_element_t *opts[256]; int no = 0, cur = -1;
            for (lxb_dom_node_t *o = n->first_child; o && no < 256; o = o->next) {
                if (lxb_dom_node_tag_id(o) == LXB_TAG_OPTGROUP) {
                    for (lxb_dom_node_t *p = o->first_child; p && no < 256; p = p->next)
                        if (lxb_dom_node_tag_id(p) == LXB_TAG_OPTION) opts[no++] = lxb_dom_interface_element(p);
                } else if (lxb_dom_node_tag_id(o) == LXB_TAG_OPTION) opts[no++] = lxb_dom_interface_element(o);
            }
            for (int i = 0; i < no; i++) if (has_attr(opts[i], "selected")) cur = i;
            int shown = cur < 0 ? 0 : cur;          /* no `selected`: the first option shows */
            if (no > 1) {
                if (cur >= 0) del_attr(opts[cur], "selected");
                int nx = (shown + 1) % no;
                set_attr(opts[nx], "selected", "", 0);
                box_t *sb = box_for_el(pg->root, el, 0);
                if (sb) {
                    size_t tl = 0;
                    lxb_char_t *t = lxb_dom_node_text_content(lxb_dom_interface_node(opts[nx]), &tl);
                    if (t) { sb->alt = dup_arena(pg, (const char *)t, tl < 120 ? tl : 120);
                             lxb_dom_document_destroy_text(lxb_dom_interface_node(opts[nx])->owner_document, t); }
                }
                pg->generation++;
                return WEB_ACT_REPAINT;
            }
            return act;
        }
        if (tag == LXB_TAG_BUTTON) {
            size_t tl; const char *t = attr(el, "type", &tl);
            if (t && (w_ieqn(t, tl, "button") || w_ieqn(t, tl, "reset"))) return act;
            return submit(pg, closest(el, LXB_TAG_FORM), el, url, cap);
        }
        if (tag == LXB_TAG_A) {
            size_t l = 0;
            const lxb_char_t *v = lxb_dom_element_get_attribute(el, (const lxb_char_t *)"href", 4, &l);
            if (!v || !l) return act;
            char ref[2048];
            snprintf(ref, sizeof ref, "%.*s", (int)(l < 2047 ? l : 2047), (const char *)v);
            if (strncmp(ref, "javascript:", 11) == 0) return act;
            url_resolve(pg->url, ref, url, cap);
            return WEB_ACT_NAVIGATE;
        }
    }
    return act;
}

static size_t utf8_back(const char *s, size_t n) {
    if (!n) return 0;
    n--;
    while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80) n--;
    return n;
}

int web_page_key(web_page_t *pg, uint32_t key, char *url, size_t cap) {
    if (!pg || !pg->focus || !pg->focus->el) return WEB_ACT_NONE;
    box_t *f = pg->focus;
    lxb_dom_element_t *el = f->el;
    bool area = lxb_dom_element_tag_id(el) == LXB_TAG_TEXTAREA;
    if (key == WEB_KEY_ESCAPE) { web_page_blur(pg); return WEB_ACT_REPAINT; }
    if (key == WEB_KEY_TAB) {
        box_t *list[256]; int n = 0;
        collect_text_boxes(pg->root, list, &n, 256, 0);
        int at = 0;
        for (int i = 0; i < n; i++) if (list[i] == f) at = i;
        if (n) { pg->focus = list[(at + 1) % n]; pg->generation++; }
        return WEB_ACT_REPAINT;
    }
    if (key == WEB_KEY_ENTER && !area)
        return submit(pg, closest(el, LXB_TAG_FORM), NULL, url, cap);
    size_t len = 0;
    char *v = control_value(el, &len);
    if (!v) return WEB_ACT_NONE;
    char *nv = (char *)malloc(len + 8);
    if (!nv) { free(v); return WEB_ACT_NONE; }
    memcpy(nv, v, len);
    size_t nlen = len;
    if (key == WEB_KEY_BACKSPACE) nlen = utf8_back(nv, len);
    else if (key == WEB_KEY_ENTER) nv[nlen++] = '\n';
    else if (key >= 32 && key < 0x110000 && nlen < 4000) {
        if (key < 0x80) nv[nlen++] = (char)key;
        else if (key < 0x800) { nv[nlen++] = (char)(0xC0 | (key >> 6)); nv[nlen++] = (char)(0x80 | (key & 0x3F)); }
        else if (key < 0x10000) { nv[nlen++] = (char)(0xE0 | (key >> 12)); nv[nlen++] = (char)(0x80 | ((key >> 6) & 0x3F)); nv[nlen++] = (char)(0x80 | (key & 0x3F)); }
        else { nv[nlen++] = (char)(0xF0 | (key >> 18)); nv[nlen++] = (char)(0x80 | ((key >> 12) & 0x3F)); nv[nlen++] = (char)(0x80 | ((key >> 6) & 0x3F)); nv[nlen++] = (char)(0x80 | (key & 0x3F)); }
    } else { free(v); free(nv); return WEB_ACT_NONE; }
    size_t maxl; const char *ml = attr(el, "maxlength", &maxl);
    if (ml && maxl) { long m = strtol(ml, NULL, 10); if (m > 0 && (long)nlen > m) nlen = (size_t)m; }
    set_attr(el, "value", nv, nlen);
    free(v); free(nv);
    sync_box(pg, f);
    pg->generation++;
    return WEB_ACT_REPAINT;
}
