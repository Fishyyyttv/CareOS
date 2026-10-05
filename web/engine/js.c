/* web/engine/js.c -- JavaScript (QuickJS-ng) and the DOM bindings.
 *
 * One runtime per page. Every DOM node handed to JS gets one wrapper object
 * (class "Node"), cached in node->user and kept alive until the page dies,
 * so identity (a === b) and expando properties (listeners, onclick, ...)
 * are stable. C provides the primitives on Node.prototype and a small
 * `__weave` host object; the richer API surface (events, classList, style,
 * dataset, location, storage, observers) lives in the JS prelude.
 *
 * Any DOM mutation sets js->dirty; the page restyles and relayouts after
 * the script / handler / timer that caused it returns. */
#include "engine.h"
#include "js_prelude.h"
#include "../third_party/quickjs/quickjs.h"
#include <lexbor/html/serialize.h>
#include <lexbor/selectors/selectors.h>
#include <stdio.h>

#define JS_MEM_LIMIT     (64u << 20)
#define JS_STACK_LIMIT   (1u << 20)
#define JS_LOAD_BUDGET   4000        /* ms for all scripts at load */
#define JS_EVENT_BUDGET  300         /* ms per handler / timer batch */
#define JS_MAX_SCRIPTS   48
#define JS_MAX_TIMERS    256

typedef struct {
    int id; uint64_t due; int interval; bool used;
    JSValue fn;
} jtimer_t;

typedef struct jsctx jsctx_t;

struct jsctx {
    JSRuntime *rt;
    JSContext *ctx;
    page_t    *pg;
    JSClassID  node_class;
    JSValue    node_proto;
    lxb_css_parser_t *sel_parser;
    lxb_selectors_t  *selectors;
    JSValue   *wrappers; size_t nwrap, capwrap;
    jtimer_t   timers[JS_MAX_TIMERS];
    int        next_timer;
    uint64_t   deadline;
    lxb_dom_node_t *current_script;
    char       nav[2048];
    int        scroll_req;   /* -1 none */
};

static uint64_t now_ms(void) { return g_web.now_ms ? g_web.now_ms(g_web.ctx) : 0; }

static int interrupt_cb(JSRuntime *rt, void *opaque) {
    (void)rt;
    jsctx_t *j = (jsctx_t *)opaque;
    return j->deadline && now_ms() > j->deadline;
}

static jsctx_t *J(JSContext *ctx) { return (jsctx_t *)JS_GetContextOpaque(ctx); }

/* ------------------------------------------------------------------ */
/*  Node wrappers                                                      */
/* ------------------------------------------------------------------ */

static JSValue wrap(JSContext *ctx, lxb_dom_node_t *n) {
    if (!n) return JS_NULL;
    jsctx_t *j = J(ctx);
    if (n->user) return JS_DupValue(ctx, *(JSValue *)n->user);
    JSValue o = JS_NewObjectProtoClass(ctx, j->node_proto, j->node_class);
    if (JS_IsException(o)) return o;
    JS_SetOpaque(o, n);
    if (j->nwrap == j->capwrap) {
        size_t nc = j->capwrap ? j->capwrap * 2 : 256;
        JSValue *nw = (JSValue *)realloc(j->wrappers, nc * sizeof(JSValue));
        if (!nw) { JS_FreeValue(ctx, o); return JS_ThrowOutOfMemory(ctx); }
        /* re-point node->user after realloc */
        for (size_t i = 0; i < j->nwrap; i++) {
            lxb_dom_node_t *m = (lxb_dom_node_t *)JS_GetOpaque(nw[i], j->node_class);
            if (m) m->user = &nw[i];
        }
        j->wrappers = nw; j->capwrap = nc;
    }
    j->wrappers[j->nwrap] = JS_DupValue(ctx, o);
    n->user = &j->wrappers[j->nwrap];
    j->nwrap++;
    return o;
}

static lxb_dom_node_t *unwrap(JSContext *ctx, JSValueConst v) {
    return (lxb_dom_node_t *)JS_GetOpaque(v, J(ctx)->node_class);
}

#define THIS_NODE() lxb_dom_node_t *n = unwrap(ctx, this_val); if (!n) return JS_ThrowTypeError(ctx, "not a Node")
#define THIS_EL() THIS_NODE(); if (n->type != LXB_DOM_NODE_TYPE_ELEMENT) return JS_UNDEFINED; \
                  lxb_dom_element_t *el = lxb_dom_interface_element(n)

static void dirty(JSContext *ctx) { J(ctx)->pg->js_dirty = 1; }

static JSValue str_val(JSContext *ctx, const char *s, size_t n) { return JS_NewStringLen(ctx, s ? s : "", s ? n : 0); }

/* Argument as a C string (caller frees with JS_FreeCString). */
static const char *arg_str(JSContext *ctx, int argc, JSValueConst *argv, int i, size_t *len) {
    if (i >= argc) { if (len) *len = 0; return NULL; }
    return JS_ToCStringLen(ctx, len, argv[i]);
}

/* ---- tree navigation ---- */

static JSValue g_nodeType(JSContext *ctx, JSValueConst this_val) { THIS_NODE(); return JS_NewInt32(ctx, (int)n->type); }

static JSValue g_nodeName(JSContext *ctx, JSValueConst this_val) {
    THIS_NODE();
    if (n->type == LXB_DOM_NODE_TYPE_TEXT) return JS_NewString(ctx, "#text");
    if (n->type == LXB_DOM_NODE_TYPE_DOCUMENT) return JS_NewString(ctx, "#document");
    if (n->type == LXB_DOM_NODE_TYPE_COMMENT) return JS_NewString(ctx, "#comment");
    if (n->type == LXB_DOM_NODE_TYPE_DOCUMENT_FRAGMENT) return JS_NewString(ctx, "#document-fragment");
    if (n->type != LXB_DOM_NODE_TYPE_ELEMENT) return JS_NewString(ctx, "");
    size_t l = 0;
    const lxb_char_t *q = lxb_dom_element_qualified_name(lxb_dom_interface_element(n), &l);
    char buf[128]; size_t k = 0;
    for (; k < l && k < sizeof buf - 1; k++) { char c = (char)q[k]; buf[k] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c; }
    return JS_NewStringLen(ctx, buf, k);
}

static JSValue g_localName(JSContext *ctx, JSValueConst this_val) {
    THIS_EL();
    size_t l = 0; const lxb_char_t *q = lxb_dom_element_local_name(el, &l);
    return str_val(ctx, (const char *)q, l);
}

static JSValue g_parentNode(JSContext *ctx, JSValueConst this_val) { THIS_NODE(); return wrap(ctx, n->parent); }
static JSValue g_parentElement(JSContext *ctx, JSValueConst this_val) {
    THIS_NODE();
    return (n->parent && n->parent->type == LXB_DOM_NODE_TYPE_ELEMENT) ? wrap(ctx, n->parent) : JS_NULL;
}
static JSValue g_firstChild(JSContext *ctx, JSValueConst this_val) { THIS_NODE(); return wrap(ctx, n->first_child); }
static JSValue g_lastChild(JSContext *ctx, JSValueConst this_val) { THIS_NODE(); return wrap(ctx, n->last_child); }
static JSValue g_nextSibling(JSContext *ctx, JSValueConst this_val) { THIS_NODE(); return wrap(ctx, n->next); }
static JSValue g_prevSibling(JSContext *ctx, JSValueConst this_val) { THIS_NODE(); return wrap(ctx, n->prev); }

static lxb_dom_node_t *elem_from(lxb_dom_node_t *n, bool forward) {
    while (n && n->type != LXB_DOM_NODE_TYPE_ELEMENT) n = forward ? n->next : n->prev;
    return n;
}
static JSValue g_firstElementChild(JSContext *ctx, JSValueConst this_val) { THIS_NODE(); return wrap(ctx, elem_from(n->first_child, true)); }
static JSValue g_lastElementChild(JSContext *ctx, JSValueConst this_val) { THIS_NODE(); return wrap(ctx, elem_from(n->last_child, false)); }
static JSValue g_nextElementSibling(JSContext *ctx, JSValueConst this_val) { THIS_NODE(); return wrap(ctx, elem_from(n->next, true)); }
static JSValue g_prevElementSibling(JSContext *ctx, JSValueConst this_val) { THIS_NODE(); return wrap(ctx, elem_from(n->prev, false)); }
static JSValue g_ownerDocument(JSContext *ctx, JSValueConst this_val) { THIS_NODE(); return wrap(ctx, lxb_dom_interface_node(n->owner_document)); }

static JSValue g_isConnected(JSContext *ctx, JSValueConst this_val) {
    THIS_NODE();
    while (n->parent) n = n->parent;
    return JS_NewBool(ctx, n->type == LXB_DOM_NODE_TYPE_DOCUMENT);
}

static JSValue m_childNodes(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)argc; (void)argv; THIS_NODE();
    JSValue a = JS_NewArray(ctx);
    uint32_t i = 0;
    for (lxb_dom_node_t *c = n->first_child; c; c = c->next) JS_SetPropertyUint32(ctx, a, i++, wrap(ctx, c));
    return a;
}

static JSValue m_children(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)argc; (void)argv; THIS_NODE();
    JSValue a = JS_NewArray(ctx);
    uint32_t i = 0;
    for (lxb_dom_node_t *c = n->first_child; c; c = c->next)
        if (c->type == LXB_DOM_NODE_TYPE_ELEMENT) JS_SetPropertyUint32(ctx, a, i++, wrap(ctx, c));
    return a;
}

/* ---- text / html ---- */

static JSValue g_textContent(JSContext *ctx, JSValueConst this_val) {
    THIS_NODE();
    if (n->type == LXB_DOM_NODE_TYPE_DOCUMENT) return JS_NULL;
    size_t l = 0;
    lxb_char_t *t = lxb_dom_node_text_content(n, &l);
    JSValue r = str_val(ctx, (const char *)t, l);
    if (t) lxb_dom_document_destroy_text(n->owner_document, t);
    return r;
}

static JSValue s_textContent(JSContext *ctx, JSValueConst this_val, JSValueConst v) {
    THIS_NODE();
    size_t l = 0;
    const char *s = JS_IsNull(v) || JS_IsUndefined(v) ? NULL : JS_ToCStringLen(ctx, &l, v);
    /* Keep detached nodes alive for wrappers and the previous layout. Their
     * storage belongs to the document arena and is reclaimed with the page. */
    if (n->type == LXB_DOM_NODE_TYPE_ELEMENT || n->type == LXB_DOM_NODE_TYPE_DOCUMENT_FRAGMENT)
        while (n->first_child) lxb_dom_node_remove(n->first_child);
    lxb_dom_node_text_content_set(n, (const lxb_char_t *)(s ? s : ""), s ? l : 0);
    if (s) JS_FreeCString(ctx, s);
    dirty(ctx);
    return JS_UNDEFINED;
}

typedef struct { char *s; size_t n, cap; } jbuf_t;
static lxb_status_t ser_cb(const lxb_char_t *data, size_t len, void *ctx) {
    jbuf_t *b = (jbuf_t *)ctx;
    if (b->n + len + 1 > b->cap) {
        size_t nc = (b->cap + len + 256) * 2;
        char *ns = (char *)realloc(b->s, nc);
        if (!ns) return LXB_STATUS_ERROR_MEMORY_ALLOCATION;
        b->s = ns; b->cap = nc;
    }
    memcpy(b->s + b->n, data, len); b->n += len;
    return LXB_STATUS_OK;
}

static JSValue g_innerHTML(JSContext *ctx, JSValueConst this_val) {
    THIS_NODE();
    jbuf_t b = {0};
    lxb_html_serialize_deep_cb(n, ser_cb, &b);
    JSValue r = str_val(ctx, b.s, b.n);
    free(b.s);
    return r;
}

static JSValue g_outerHTML(JSContext *ctx, JSValueConst this_val) {
    THIS_NODE();
    jbuf_t b = {0};
    lxb_html_serialize_tree_cb(n, ser_cb, &b);
    JSValue r = str_val(ctx, b.s, b.n);
    free(b.s);
    return r;
}

static JSValue s_innerHTML(JSContext *ctx, JSValueConst this_val, JSValueConst v) {
    THIS_EL();
    size_t l = 0;
    const char *s = JS_ToCStringLen(ctx, &l, v);
    if (!s) return JS_EXCEPTION;
    while (n->first_child) lxb_dom_node_remove(n->first_child);
    lxb_html_element_inner_html_set(lxb_html_interface_element(el), (const lxb_char_t *)s, l);
    JS_FreeCString(ctx, s);
    dirty(ctx);
    return JS_UNDEFINED;
}

static JSValue g_data(JSContext *ctx, JSValueConst this_val) {
    THIS_NODE();
    if (n->type != LXB_DOM_NODE_TYPE_TEXT && n->type != LXB_DOM_NODE_TYPE_COMMENT) return JS_NULL;
    lxb_dom_character_data_t *cd = lxb_dom_interface_character_data(n);
    return str_val(ctx, (const char *)cd->data.data, cd->data.length);
}

static JSValue s_data(JSContext *ctx, JSValueConst this_val, JSValueConst v) {
    THIS_NODE();
    if (n->type != LXB_DOM_NODE_TYPE_TEXT && n->type != LXB_DOM_NODE_TYPE_COMMENT) return JS_UNDEFINED;
    size_t l = 0; const char *s = JS_ToCStringLen(ctx, &l, v);
    if (!s) return JS_EXCEPTION;
    lxb_dom_node_text_content_set(n, (const lxb_char_t *)s, l);
    JS_FreeCString(ctx, s);
    dirty(ctx);
    return JS_UNDEFINED;
}

/* ---- attributes ---- */

static JSValue m_getAttribute(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    THIS_EL();
    size_t nl; const char *name = arg_str(ctx, argc, argv, 0, &nl);
    if (!name) return JS_NULL;
    size_t vl = 0;
    const lxb_char_t *v = lxb_dom_element_get_attribute(el, (const lxb_char_t *)name, nl, &vl);
    JS_FreeCString(ctx, name);
    return v ? str_val(ctx, (const char *)v, vl) : JS_NULL;
}

static JSValue m_setAttribute(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    THIS_EL();
    size_t nl, vl;
    const char *name = arg_str(ctx, argc, argv, 0, &nl), *val = arg_str(ctx, argc, argv, 1, &vl);
    if (name) lxb_dom_element_set_attribute(el, (const lxb_char_t *)name, nl, (const lxb_char_t *)(val ? val : ""), val ? vl : 0);
    if (name) JS_FreeCString(ctx, name);
    if (val) JS_FreeCString(ctx, val);
    dirty(ctx);
    return JS_UNDEFINED;
}

static JSValue m_removeAttribute(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    THIS_EL();
    size_t nl; const char *name = arg_str(ctx, argc, argv, 0, &nl);
    if (name) { lxb_dom_element_remove_attribute(el, (const lxb_char_t *)name, nl); JS_FreeCString(ctx, name); }
    dirty(ctx);
    return JS_UNDEFINED;
}

static JSValue m_hasAttribute(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    THIS_EL();
    size_t nl; const char *name = arg_str(ctx, argc, argv, 0, &nl);
    bool r = name && lxb_dom_element_has_attribute(el, (const lxb_char_t *)name, nl);
    if (name) JS_FreeCString(ctx, name);
    return JS_NewBool(ctx, r);
}

static JSValue m_getAttributeNames(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)argc; (void)argv; THIS_EL();
    JSValue a = JS_NewArray(ctx);
    uint32_t i = 0;
    for (lxb_dom_attr_t *at = lxb_dom_element_first_attribute(el); at; at = lxb_dom_element_next_attribute(at)) {
        size_t l = 0; const lxb_char_t *q = lxb_dom_attr_qualified_name(at, &l);
        JS_SetPropertyUint32(ctx, a, i++, str_val(ctx, (const char *)q, l));
    }
    return a;
}

/* ---- tree mutation ---- */

static lxb_dom_node_t *arg_node(JSContext *ctx, int argc, JSValueConst *argv, int i) {
    return i < argc ? unwrap(ctx, argv[i]) : NULL;
}

/* Fragments insert their children. */
static void insert_before(lxb_dom_node_t *parent, lxb_dom_node_t *node, lxb_dom_node_t *ref) {
    if (node == ref) return;
    if (node->type == LXB_DOM_NODE_TYPE_DOCUMENT_FRAGMENT) {
        lxb_dom_node_t *c = node->first_child;
        while (c) { lxb_dom_node_t *nx = c->next; lxb_dom_node_remove(c); insert_before(parent, c, ref); c = nx; }
        return;
    }
    if (node->parent) lxb_dom_node_remove(node);
    if (ref && ref->parent == parent) lxb_dom_node_insert_before(ref, node);
    else lxb_dom_node_insert_child(parent, node);
}

static bool valid_insert(lxb_dom_node_t *parent, lxb_dom_node_t *node) {
    if (!node || (parent->type != LXB_DOM_NODE_TYPE_ELEMENT &&
        parent->type != LXB_DOM_NODE_TYPE_DOCUMENT_FRAGMENT &&
        parent->type != LXB_DOM_NODE_TYPE_DOCUMENT)) return false;
    if (node->type == LXB_DOM_NODE_TYPE_DOCUMENT) return false;
    for (lxb_dom_node_t *p = parent; p; p = p->parent) if (p == node) return false;
    return true;
}

static JSValue m_appendChild(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    THIS_NODE();
    lxb_dom_node_t *c = arg_node(ctx, argc, argv, 0);
    if (!valid_insert(n, c)) return JS_ThrowTypeError(ctx, "appendChild: invalid hierarchy");
    insert_before(n, c, NULL);
    dirty(ctx);
    return JS_DupValue(ctx, argv[0]);
}

static JSValue m_insertBefore(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    THIS_NODE();
    lxb_dom_node_t *c = arg_node(ctx, argc, argv, 0), *ref = arg_node(ctx, argc, argv, 1);
    if (!valid_insert(n, c) || (ref && ref->parent != n)) return JS_ThrowTypeError(ctx, "insertBefore: invalid hierarchy");
    insert_before(n, c, ref);
    dirty(ctx);
    return JS_DupValue(ctx, argv[0]);
}

static JSValue m_removeChild(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    THIS_NODE();
    lxb_dom_node_t *c = arg_node(ctx, argc, argv, 0);
    if (c && c->parent == n) { lxb_dom_node_remove(c); dirty(ctx); }
    return argc ? JS_DupValue(ctx, argv[0]) : JS_UNDEFINED;
}

static JSValue m_replaceChild(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    THIS_NODE();
    lxb_dom_node_t *nw = arg_node(ctx, argc, argv, 0), *old = arg_node(ctx, argc, argv, 1);
    if (!valid_insert(n, nw) || !old || old->parent != n) return JS_ThrowTypeError(ctx, "replaceChild: bad arguments");
    if (nw == old) return JS_DupValue(ctx, argv[1]);
    insert_before(n, nw, old);
    lxb_dom_node_remove(old);
    dirty(ctx);
    return JS_DupValue(ctx, argv[1]);
}

static JSValue m_remove(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)argc; (void)argv; THIS_NODE();
    if (n->parent) { lxb_dom_node_remove(n); dirty(ctx); }
    return JS_UNDEFINED;
}

static JSValue m_cloneNode(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    THIS_NODE();
    bool deep = argc > 0 && JS_ToBool(ctx, argv[0]);
    lxb_dom_node_t *c = lxb_dom_node_clone(n, deep);
    /* Lexbor copies user; clones need fresh wrapper identities. */
    for (lxb_dom_node_t *p = c; p;) {
        p->user = NULL;
        if (p->first_child) { p = p->first_child; continue; }
        while (p != c && !p->next) p = p->parent;
        if (p == c) break;
        p = p->next;
    }
    return wrap(ctx, c);
}

/* Detached elements can outlive the CSS arena used by a previous layout. */
void js_clear_detached_styles(jsctx_t *j) {
    for (size_t i = 0; i < j->nwrap; i++) {
        lxb_dom_node_t *root = (lxb_dom_node_t *)JS_GetOpaque(j->wrappers[i], j->node_class);
        if (!root) continue;
        while (root->parent) root = root->parent;
        if (root->type == LXB_DOM_NODE_TYPE_DOCUMENT) continue;
        for (lxb_dom_node_t *p = root; p;) {
            if (p->type == LXB_DOM_NODE_TYPE_ELEMENT) {
                lxb_dom_element_t *el = lxb_dom_interface_element(p);
                el->style = NULL; el->list = NULL;
            }
            if (p->first_child) { p = p->first_child; continue; }
            while (p != root && !p->next) p = p->parent;
            if (p == root) break;
            p = p->next;
        }
    }
}

static JSValue m_contains(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    THIS_NODE();
    lxb_dom_node_t *o = arg_node(ctx, argc, argv, 0);
    for (; o; o = o->parent) if (o == n) return JS_TRUE;
    return JS_FALSE;
}

static JSValue m_hasChildNodes(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)argc; (void)argv; THIS_NODE();
    return JS_NewBool(ctx, n->first_child != NULL);
}

static JSValue m_insertAdjacentHTML(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    THIS_EL(); (void)el;
    size_t pl, hl;
    const char *pos = arg_str(ctx, argc, argv, 0, &pl), *html = arg_str(ctx, argc, argv, 1, &hl);
    if (pos && html) {
        /* parse into a scratch element, then move the children */
        lxb_dom_element_t *tmp = lxb_dom_document_create_element(n->owner_document, (const lxb_char_t *)"div", 3, NULL);
        if (tmp) {
            lxb_html_element_inner_html_set(lxb_html_interface_element(tmp), (const lxb_char_t *)html, hl);
            lxb_dom_node_t *parent, *ref;
            if (w_ieq(pos, "beforebegin")) { parent = n->parent; ref = n; }
            else if (w_ieq(pos, "afterbegin")) { parent = n; ref = n->first_child; }
            else if (w_ieq(pos, "afterend")) { parent = n->parent; ref = n->next; }
            else { parent = n; ref = NULL; }
            if (parent) {
                lxb_dom_node_t *c = lxb_dom_interface_node(tmp)->first_child;
                while (c) { lxb_dom_node_t *nx = c->next; lxb_dom_node_remove(c); insert_before(parent, c, ref); c = nx; }
            }
            dirty(ctx);
        }
    }
    if (pos) JS_FreeCString(ctx, pos);
    if (html) JS_FreeCString(ctx, html);
    return JS_UNDEFINED;
}

/* ---- selectors ---- */

/* lxb_css_selectors_parse() leaves the list's memory pool installed in the
 * parser; detach it before freeing so the next parse creates a fresh one. */
static void free_selectors(jsctx_t *j, lxb_css_selector_list_t *list) {
    if (!list) return;
    if (j->sel_parser->memory == list->memory) j->sel_parser->memory = NULL;
    lxb_css_selector_list_destroy_memory(list);
}

typedef struct { JSContext *ctx; JSValue arr; uint32_t n; bool first_only; lxb_dom_node_t *root; } selq_t;

static lxb_status_t sel_cb(lxb_dom_node_t *node, lxb_css_selector_specificity_t spec, void *c) {
    (void)spec;
    selq_t *q = (selq_t *)c;
    if (node == q->root) return LXB_STATUS_OK;
    JS_SetPropertyUint32(q->ctx, q->arr, q->n++, wrap(q->ctx, node));
    return q->first_only ? LXB_STATUS_STOP : LXB_STATUS_OK;
}

static JSValue query(JSContext *ctx, lxb_dom_node_t *root, JSValueConst sel, bool first) {
    jsctx_t *j = J(ctx);
    size_t l; const char *s = JS_ToCStringLen(ctx, &l, sel);
    if (!s) return JS_EXCEPTION;
    lxb_css_selector_list_t *list = lxb_css_selectors_parse(j->sel_parser, (const lxb_char_t *)s, l);
    JS_FreeCString(ctx, s);
    if (!list) return first ? JS_NULL : JS_NewArray(ctx);
    selq_t q = { ctx, JS_NewArray(ctx), 0, first, root };
    lxb_selectors_find(j->selectors, root, list, sel_cb, &q);
    free_selectors(j, list);
    if (!first) return q.arr;
    JSValue r = q.n ? JS_GetPropertyUint32(ctx, q.arr, 0) : JS_NULL;
    JS_FreeValue(ctx, q.arr);
    return r;
}

static JSValue m_querySelector(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    THIS_NODE(); if (!argc) return JS_NULL;
    return query(ctx, n, argv[0], true);
}
static JSValue m_querySelectorAll(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    THIS_NODE(); if (!argc) return JS_NewArray(ctx);
    return query(ctx, n, argv[0], false);
}

typedef struct { bool hit; } matchq_t;
static lxb_status_t match_cb(lxb_dom_node_t *node, lxb_css_selector_specificity_t spec, void *c) {
    (void)node; (void)spec; ((matchq_t *)c)->hit = true; return LXB_STATUS_STOP;
}

static bool node_matches(JSContext *ctx, lxb_dom_node_t *n, const char *s, size_t l) {
    jsctx_t *j = J(ctx);
    lxb_css_selector_list_t *list = lxb_css_selectors_parse(j->sel_parser, (const lxb_char_t *)s, l);
    if (!list) return false;
    matchq_t m = { false };
    lxb_selectors_match_node(j->selectors, n, list, match_cb, &m);
    free_selectors(j, list);
    return m.hit;
}

static JSValue m_matches(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    THIS_EL(); (void)el;
    size_t l; const char *s = arg_str(ctx, argc, argv, 0, &l);
    if (!s) return JS_FALSE;
    bool r = node_matches(ctx, n, s, l);
    JS_FreeCString(ctx, s);
    return JS_NewBool(ctx, r);
}

static JSValue m_closest(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    THIS_NODE();
    size_t l; const char *s = arg_str(ctx, argc, argv, 0, &l);
    if (!s) return JS_NULL;
    JSValue r = JS_NULL;
    for (lxb_dom_node_t *p = n; p; p = p->parent)
        if (p->type == LXB_DOM_NODE_TYPE_ELEMENT && node_matches(ctx, p, s, l)) { r = wrap(ctx, p); break; }
    JS_FreeCString(ctx, s);
    return r;
}

/* ---- document ---- */

static lxb_dom_document_t *doc_of(lxb_dom_node_t *n) {
    return n->type == LXB_DOM_NODE_TYPE_DOCUMENT ? lxb_dom_interface_document(n) : n->owner_document;
}

static JSValue m_createElement(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    THIS_NODE();
    size_t l; const char *name = arg_str(ctx, argc, argv, 0, &l);
    if (!name) return JS_ThrowTypeError(ctx, "createElement: name required");
    char low[128]; size_t k = 0;
    for (; k < l && k < sizeof low - 1; k++) { char c = name[k]; low[k] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
    JS_FreeCString(ctx, name);
    lxb_dom_element_t *el = lxb_dom_document_create_element(doc_of(n), (const lxb_char_t *)low, k, NULL);
    return el ? wrap(ctx, lxb_dom_interface_node(el)) : JS_NULL;
}

static JSValue m_createTextNode(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    THIS_NODE();
    size_t l = 0; const char *s = arg_str(ctx, argc, argv, 0, &l);
    lxb_dom_text_t *t = lxb_dom_document_create_text_node(doc_of(n), (const lxb_char_t *)(s ? s : ""), s ? l : 0);
    if (s) JS_FreeCString(ctx, s);
    return t ? wrap(ctx, lxb_dom_interface_node(t)) : JS_NULL;
}

static JSValue m_createDocumentFragment(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)argc; (void)argv; THIS_NODE();
    lxb_dom_document_fragment_t *f = lxb_dom_document_create_document_fragment(doc_of(n));
    return f ? wrap(ctx, lxb_dom_interface_node(f)) : JS_NULL;
}

static JSValue m_getElementById(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    THIS_NODE();
    size_t l; const char *id = arg_str(ctx, argc, argv, 0, &l);
    if (!id) return JS_NULL;
    lxb_dom_collection_t *col = lxb_dom_collection_make(doc_of(n), 4);
    JSValue r = JS_NULL;
    lxb_dom_node_t *root = lxb_dom_interface_node(lxb_dom_document_element(doc_of(n)));
    if (col && root) {
        lxb_dom_elements_by_attr(lxb_dom_interface_element(root), col, (const lxb_char_t *)"id", 2,
                                 (const lxb_char_t *)id, l, false);
        if (lxb_dom_collection_length(col)) r = wrap(ctx, lxb_dom_interface_node(lxb_dom_collection_element(col, 0)));
    }
    if (col) lxb_dom_collection_destroy(col, true);
    JS_FreeCString(ctx, id);
    return r;
}

static JSValue g_documentElement(JSContext *ctx, JSValueConst this_val) {
    THIS_NODE();
    return wrap(ctx, lxb_dom_interface_node(lxb_dom_document_element(doc_of(n))));
}
static JSValue g_body(JSContext *ctx, JSValueConst this_val) {
    THIS_NODE();
    lxb_html_document_t *d = lxb_html_interface_document(doc_of(n));
    return wrap(ctx, d->body ? lxb_dom_interface_node(d->body) : NULL);
}
static JSValue g_head(JSContext *ctx, JSValueConst this_val) {
    THIS_NODE();
    lxb_html_document_t *d = lxb_html_interface_document(doc_of(n));
    return wrap(ctx, d->head ? lxb_dom_interface_node(d->head) : NULL);
}

static JSValue g_title(JSContext *ctx, JSValueConst this_val) {
    THIS_NODE();
    if (n->type != LXB_DOM_NODE_TYPE_DOCUMENT) {
        size_t vl = 0;
        const lxb_char_t *v = n->type == LXB_DOM_NODE_TYPE_ELEMENT ?
            lxb_dom_element_get_attribute(lxb_dom_interface_element(n), (const lxb_char_t *)"title", 5, &vl) : NULL;
        return str_val(ctx, (const char *)v, vl);
    }
    return JS_NewString(ctx, J(ctx)->pg->title);
}

static JSValue s_title(JSContext *ctx, JSValueConst this_val, JSValueConst v) {
    THIS_NODE();
    size_t l; const char *s = JS_ToCStringLen(ctx, &l, v);
    if (!s) return JS_EXCEPTION;
    if (n->type == LXB_DOM_NODE_TYPE_DOCUMENT) snprintf(J(ctx)->pg->title, sizeof J(ctx)->pg->title, "%s", s);
    else if (n->type == LXB_DOM_NODE_TYPE_ELEMENT)
        lxb_dom_element_set_attribute(lxb_dom_interface_element(n), (const lxb_char_t *)"title", 5, (const lxb_char_t *)s, l);
    JS_FreeCString(ctx, s);
    return JS_UNDEFINED;
}

/* document.write: parse and insert after the running script */
static JSValue m_write(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    THIS_NODE();
    jsctx_t *j = J(ctx);
    lxb_dom_node_t *ref = j->current_script;
    lxb_html_document_t *d = lxb_html_interface_document(doc_of(n));
    lxb_dom_node_t *parent = ref ? ref->parent : (d->body ? lxb_dom_interface_node(d->body) : NULL);
    if (!parent) return JS_UNDEFINED;
    for (int i = 0; i < argc; i++) {
        size_t l; const char *s = JS_ToCStringLen(ctx, &l, argv[i]);
        if (!s) continue;
        lxb_dom_element_t *tmp = lxb_dom_document_create_element(doc_of(n), (const lxb_char_t *)"div", 3, NULL);
        if (tmp) {
            lxb_html_element_inner_html_set(lxb_html_interface_element(tmp), (const lxb_char_t *)s, l);
            lxb_dom_node_t *after = ref ? ref->next : NULL;
            lxb_dom_node_t *c = lxb_dom_interface_node(tmp)->first_child;
            while (c) { lxb_dom_node_t *nx = c->next; lxb_dom_node_remove(c); insert_before(parent, c, after); c = nx; }
        }
        JS_FreeCString(ctx, s);
    }
    dirty(ctx);
    return JS_UNDEFINED;
}

/* ---- geometry ---- */

static box_t *box_of(box_t *b, lxb_dom_element_t *el, float x, float y, float *ox, float *oy, int depth) {
    if (!b || depth > 400) return NULL;
    if (b->el == el && b->kind != K_TEXT) { *ox = x; *oy = y; return b; }
    for (box_t *c = b->first; c; c = c->next) {
        box_t *r = box_of(c, el, x + c->x + c->rel_x, y + c->y + c->rel_y, ox, oy, depth + 1);
        if (r) return r;
    }
    return NULL;
}

static JSValue m_rect(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)argc; (void)argv; THIS_EL();
    page_t *pg = J(ctx)->pg;
    float x = 0, y = 0;
    box_t *b = pg->root ? box_of(pg->root, el, pg->root->x, pg->root->y, &x, &y, 0) : NULL;
    JSValue o = JS_NewObject(ctx);
    float w = b ? b->w : 0, h = b ? b->h : 0;
    y -= (float)pg->scroll_y;
    JS_SetPropertyStr(ctx, o, "x", JS_NewFloat64(ctx, x));
    JS_SetPropertyStr(ctx, o, "y", JS_NewFloat64(ctx, y));
    JS_SetPropertyStr(ctx, o, "left", JS_NewFloat64(ctx, x));
    JS_SetPropertyStr(ctx, o, "top", JS_NewFloat64(ctx, y));
    JS_SetPropertyStr(ctx, o, "width", JS_NewFloat64(ctx, w));
    JS_SetPropertyStr(ctx, o, "height", JS_NewFloat64(ctx, h));
    JS_SetPropertyStr(ctx, o, "right", JS_NewFloat64(ctx, x + w));
    JS_SetPropertyStr(ctx, o, "bottom", JS_NewFloat64(ctx, y + h));
    return o;
}

/* ---- computed style subset ---- */
static JSValue m_computed(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)argc; (void)argv; THIS_EL();
    page_t *pg = J(ctx)->pg;
    float x, y;
    box_t *b = pg->root ? box_of(pg->root, el, 0, 0, &x, &y, 0) : NULL;
    JSValue o = JS_NewObject(ctx);
    static const char *disp[] = { "none", "block", "inline", "inline-block", "list-item", "flex", "inline-flex",
        "grid", "inline-grid", "table", "inline-table", "table-row", "table-cell", "table-row-group",
        "table-caption", "table-column", "contents", "flow-root" };
    if (b) {
        const style_t *st = b->st;
        char buf[64];
        JS_SetPropertyStr(ctx, o, "display", JS_NewString(ctx, st->display < 18 ? disp[st->display] : "block"));
        JS_SetPropertyStr(ctx, o, "visibility", JS_NewString(ctx, st->visibility ? "hidden" : "visible"));
        JS_SetPropertyStr(ctx, o, "position", JS_NewString(ctx, (const char *[]){"static","relative","absolute","fixed","sticky"}[st->position]));
        snprintf(buf, sizeof buf, "%gpx", st->font_size); JS_SetPropertyStr(ctx, o, "fontSize", JS_NewString(ctx, buf));
        snprintf(buf, sizeof buf, "rgb(%u, %u, %u)", (st->color >> 16) & 255, (st->color >> 8) & 255, st->color & 255);
        JS_SetPropertyStr(ctx, o, "color", JS_NewString(ctx, buf));
        snprintf(buf, sizeof buf, "%gpx", b->w); JS_SetPropertyStr(ctx, o, "width", JS_NewString(ctx, buf));
        snprintf(buf, sizeof buf, "%gpx", b->h); JS_SetPropertyStr(ctx, o, "height", JS_NewString(ctx, buf));
        snprintf(buf, sizeof buf, "%g", st->opacity); JS_SetPropertyStr(ctx, o, "opacity", JS_NewString(ctx, buf));
    } else {
        JS_SetPropertyStr(ctx, o, "display", JS_NewString(ctx, "none"));
    }
    return o;
}

/* ------------------------------------------------------------------ */
/*  __weave host object                                                */
/* ------------------------------------------------------------------ */

static JSValue h_log(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val;
    char line[512]; size_t o = 0;
    for (int i = 0; i < argc && o < sizeof line - 2; i++) {
        const char *s = JS_ToCString(ctx, argv[i]);
        if (!s) continue;
        o += (size_t)snprintf(line + o, sizeof line - o, "%s%s", i ? " " : "", s);
        JS_FreeCString(ctx, s);
    }
    if (o >= sizeof line) o = sizeof line - 1;
    line[o] = 0;
    web_log("js: %s", line);
    return JS_UNDEFINED;
}

static JSValue h_setTimer(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val;
    jsctx_t *j = J(ctx);
    if (argc < 1 || !JS_IsFunction(ctx, argv[0])) return JS_NewInt32(ctx, 0);
    int32_t ms = 0, rep = 0;
    if (argc > 1) JS_ToInt32(ctx, &ms, argv[1]);
    if (argc > 2) JS_ToInt32(ctx, &rep, argv[2]);
    if (ms < 0) ms = 0;
    for (int i = 0; i < JS_MAX_TIMERS; i++) {
        jtimer_t *t = &j->timers[i];
        if (t->used) continue;
        t->used = true; t->id = ++j->next_timer;
        t->due = now_ms() + (uint64_t)ms;
        t->interval = rep ? (ms < 10 ? 10 : ms) : 0;
        t->fn = JS_DupValue(ctx, argv[0]);
        return JS_NewInt32(ctx, t->id);
    }
    return JS_NewInt32(ctx, 0);
}

static JSValue h_clearTimer(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val;
    jsctx_t *j = J(ctx);
    int32_t id = 0;
    if (argc) JS_ToInt32(ctx, &id, argv[0]);
    for (int i = 0; i < JS_MAX_TIMERS; i++)
        if (j->timers[i].used && j->timers[i].id == id) {
            JS_FreeValue(ctx, j->timers[i].fn);
            j->timers[i].used = false;
        }
    return JS_UNDEFINED;
}

static JSValue h_navigate(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val;
    jsctx_t *j = J(ctx);
    const char *s = argc ? JS_ToCString(ctx, argv[0]) : NULL;
    if (s) { url_resolve(j->pg->url, s, j->nav, sizeof j->nav); JS_FreeCString(ctx, s); }
    return JS_UNDEFINED;
}

static JSValue h_now(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc; (void)argv;
    return JS_NewFloat64(ctx, (double)now_ms());
}

static JSValue h_media(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val;
    const char *s = argc ? JS_ToCString(ctx, argv[0]) : NULL;
    bool r = s && css_media_matches(J(ctx)->pg, s);
    if (s) JS_FreeCString(ctx, s);
    return JS_NewBool(ctx, r);
}

static JSValue h_scrollTo(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val;
    int32_t y = 0;
    if (argc > 1) JS_ToInt32(ctx, &y, argv[1]);
    J(ctx)->scroll_req = y < 0 ? 0 : y;
    return JS_UNDEFINED;
}

static const JSCFunctionListEntry host_funcs[] = {
    JS_CFUNC_DEF("log", 1, h_log),
    JS_CFUNC_DEF("setTimer", 3, h_setTimer),
    JS_CFUNC_DEF("clearTimer", 1, h_clearTimer),
    JS_CFUNC_DEF("navigate", 1, h_navigate),
    JS_CFUNC_DEF("now", 0, h_now),
    JS_CFUNC_DEF("mediaMatches", 1, h_media),
    JS_CFUNC_DEF("scrollTo", 2, h_scrollTo),
};

static const JSCFunctionListEntry node_funcs[] = {
    JS_CGETSET_DEF("nodeType", g_nodeType, NULL),
    JS_CGETSET_DEF("nodeName", g_nodeName, NULL),
    JS_CGETSET_DEF("tagName", g_nodeName, NULL),
    JS_CGETSET_DEF("localName", g_localName, NULL),
    JS_CGETSET_DEF("parentNode", g_parentNode, NULL),
    JS_CGETSET_DEF("parentElement", g_parentElement, NULL),
    JS_CGETSET_DEF("firstChild", g_firstChild, NULL),
    JS_CGETSET_DEF("lastChild", g_lastChild, NULL),
    JS_CGETSET_DEF("nextSibling", g_nextSibling, NULL),
    JS_CGETSET_DEF("previousSibling", g_prevSibling, NULL),
    JS_CGETSET_DEF("firstElementChild", g_firstElementChild, NULL),
    JS_CGETSET_DEF("lastElementChild", g_lastElementChild, NULL),
    JS_CGETSET_DEF("nextElementSibling", g_nextElementSibling, NULL),
    JS_CGETSET_DEF("previousElementSibling", g_prevElementSibling, NULL),
    JS_CGETSET_DEF("ownerDocument", g_ownerDocument, NULL),
    JS_CGETSET_DEF("isConnected", g_isConnected, NULL),
    JS_CGETSET_DEF("textContent", g_textContent, s_textContent),
    JS_CGETSET_DEF("innerText", g_textContent, s_textContent),
    JS_CGETSET_DEF("innerHTML", g_innerHTML, s_innerHTML),
    JS_CGETSET_DEF("outerHTML", g_outerHTML, NULL),
    JS_CGETSET_DEF("data", g_data, s_data),
    JS_CGETSET_DEF("nodeValue", g_data, s_data),
    JS_CGETSET_DEF("documentElement", g_documentElement, NULL),
    JS_CGETSET_DEF("body", g_body, NULL),
    JS_CGETSET_DEF("head", g_head, NULL),
    JS_CGETSET_DEF("title", g_title, s_title),
    JS_CFUNC_DEF("_childNodes", 0, m_childNodes),
    JS_CFUNC_DEF("_children", 0, m_children),
    JS_CFUNC_DEF("getAttribute", 1, m_getAttribute),
    JS_CFUNC_DEF("setAttribute", 2, m_setAttribute),
    JS_CFUNC_DEF("removeAttribute", 1, m_removeAttribute),
    JS_CFUNC_DEF("hasAttribute", 1, m_hasAttribute),
    JS_CFUNC_DEF("getAttributeNames", 0, m_getAttributeNames),
    JS_CFUNC_DEF("appendChild", 1, m_appendChild),
    JS_CFUNC_DEF("insertBefore", 2, m_insertBefore),
    JS_CFUNC_DEF("removeChild", 1, m_removeChild),
    JS_CFUNC_DEF("replaceChild", 2, m_replaceChild),
    JS_CFUNC_DEF("remove", 0, m_remove),
    JS_CFUNC_DEF("cloneNode", 1, m_cloneNode),
    JS_CFUNC_DEF("contains", 1, m_contains),
    JS_CFUNC_DEF("hasChildNodes", 0, m_hasChildNodes),
    JS_CFUNC_DEF("insertAdjacentHTML", 2, m_insertAdjacentHTML),
    JS_CFUNC_DEF("querySelector", 1, m_querySelector),
    JS_CFUNC_DEF("querySelectorAll", 1, m_querySelectorAll),
    JS_CFUNC_DEF("matches", 1, m_matches),
    JS_CFUNC_DEF("closest", 1, m_closest),
    JS_CFUNC_DEF("createElement", 1, m_createElement),
    JS_CFUNC_DEF("createTextNode", 1, m_createTextNode),
    JS_CFUNC_DEF("createDocumentFragment", 0, m_createDocumentFragment),
    JS_CFUNC_DEF("getElementById", 1, m_getElementById),
    JS_CFUNC_DEF("write", 1, m_write),
    JS_CFUNC_DEF("writeln", 1, m_write),
    JS_CFUNC_DEF("getBoundingClientRect", 0, m_rect),
    JS_CFUNC_DEF("_computed", 0, m_computed),
};

/* ------------------------------------------------------------------ */
/*  Lifecycle                                                          */
/* ------------------------------------------------------------------ */

static void report_exception(JSContext *ctx, const char *where) {
    JSValue e = JS_GetException(ctx);
    const char *s = JS_ToCString(ctx, e);
    JSValue stack = JS_IsObject(e) ? JS_GetPropertyStr(ctx, e, "stack") : JS_UNDEFINED;
    const char *st = JS_IsString(stack) ? JS_ToCString(ctx, stack) : NULL;
    web_log("js error (%s): %s %s", where, s ? s : "?", st ? st : "");
    if (st) JS_FreeCString(ctx, st);
    if (s) JS_FreeCString(ctx, s);
    JS_FreeValue(ctx, stack);
    JS_FreeValue(ctx, e);
}

static void run_jobs(jsctx_t *j) {
    JSContext *c;
    for (int i = 0; i < 1000; i++) {
        if (interrupt_cb(j->rt, j)) break;
        int r = JS_ExecutePendingJob(j->rt, &c);
        if (r <= 0) { if (r < 0) report_exception(c, "job"); break; }
    }
}

static bool eval(jsctx_t *j, const char *src, size_t len, const char *name) {
    JSValue r = JS_Eval(j->ctx, src, len, name, JS_EVAL_TYPE_GLOBAL);
    bool ok = !JS_IsException(r);
    if (!ok) report_exception(j->ctx, name);
    JS_FreeValue(j->ctx, r);
    run_jobs(j);
    return ok;
}

jsctx_t *js_create(page_t *pg) {
    if (!g_web.now_ms) return NULL; /* Require a clock to bound untrusted code. */
    jsctx_t *j = (jsctx_t *)calloc(1, sizeof *j);
    if (!j) return NULL;
    j->pg = pg;
    j->scroll_req = -1;
    j->rt = JS_NewRuntime();
    if (!j->rt) { free(j); return NULL; }
    JS_SetMemoryLimit(j->rt, JS_MEM_LIMIT);
    JS_SetMaxStackSize(j->rt, JS_STACK_LIMIT);
    JS_SetInterruptHandler(j->rt, interrupt_cb, j);
    j->ctx = JS_NewContext(j->rt);
    if (!j->ctx) { JS_FreeRuntime(j->rt); free(j); return NULL; }
    JS_SetContextOpaque(j->ctx, j);
    JS_NewClassID(j->rt, &j->node_class);
    JSClassDef def = { .class_name = "Node" };
    if (JS_NewClass(j->rt, j->node_class, &def) < 0) { js_destroy(j); return NULL; }
    j->node_proto = JS_NewObject(j->ctx);
    JS_SetPropertyFunctionList(j->ctx, j->node_proto, node_funcs, (int)(sizeof node_funcs / sizeof node_funcs[0]));
    JS_SetClassProto(j->ctx, j->node_class, JS_DupValue(j->ctx, j->node_proto));
    j->sel_parser = lxb_css_parser_create();
    if (!j->sel_parser || lxb_css_parser_init(j->sel_parser, NULL) != LXB_STATUS_OK) { js_destroy(j); return NULL; }
    j->selectors = lxb_selectors_create();
    if (!j->selectors || lxb_selectors_init(j->selectors) != LXB_STATUS_OK) { js_destroy(j); return NULL; }

    JSValue global = JS_GetGlobalObject(j->ctx);
    JSValue host = JS_NewObject(j->ctx);
    JS_SetPropertyFunctionList(j->ctx, host, host_funcs, (int)(sizeof host_funcs / sizeof host_funcs[0]));
    JS_SetPropertyStr(j->ctx, host, "url", JS_NewString(j->ctx, pg->url));
    JS_SetPropertyStr(j->ctx, host, "width", JS_NewInt32(j->ctx, pg->vw));
    JS_SetPropertyStr(j->ctx, host, "height", JS_NewInt32(j->ctx, pg->vh));
    JS_SetPropertyStr(j->ctx, host, "NodeProto", JS_DupValue(j->ctx, j->node_proto));
    JS_SetPropertyStr(j->ctx, host, "document", wrap(j->ctx, lxb_dom_interface_node(pg->doc)));
    JS_SetPropertyStr(j->ctx, global, "__weave", host);
    JS_FreeValue(j->ctx, global);
    j->deadline = now_ms() + 2000;
    if (!eval(j, JS_PRELUDE, sizeof JS_PRELUDE - 1, "<prelude>")) { js_destroy(j); return NULL; }
    j->deadline = 0;
    return j;
}

void js_destroy(jsctx_t *j) {
    if (!j) return;
    for (int i = 0; i < JS_MAX_TIMERS; i++) if (j->timers[i].used) JS_FreeValue(j->ctx, j->timers[i].fn);
    for (size_t i = 0; i < j->nwrap; i++) {
        lxb_dom_node_t *n = (lxb_dom_node_t *)JS_GetOpaque(j->wrappers[i], j->node_class);
        if (n) n->user = NULL;
        JS_FreeValue(j->ctx, j->wrappers[i]);
    }
    free(j->wrappers);
    JS_FreeValue(j->ctx, j->node_proto);
    JS_FreeContext(j->ctx);
    JS_FreeRuntime(j->rt);
    if (j->selectors) lxb_selectors_destroy(j->selectors, true);
    if (j->sel_parser) lxb_css_parser_destroy(j->sel_parser, true);
    free(j);
}

static bool script_type_ok(lxb_dom_element_t *el) {
    size_t l = 0;
    const char *t = (const char *)lxb_dom_element_get_attribute(el, (const lxb_char_t *)"type", 4, &l);
    if (!t || !l) return true;
    return w_ieqn(t, l, "text/javascript") || w_ieqn(t, l, "application/javascript") ||
           w_ieqn(t, l, "text/ecmascript") || w_ieqn(t, l, "application/x-javascript");
}

static void collect_scripts(lxb_dom_node_t *n, lxb_dom_node_t **out, int *cnt, int depth) {
    if (depth > 400) return;
    for (lxb_dom_node_t *c = n->first_child; c && *cnt < JS_MAX_SCRIPTS; c = c->next) {
        if (c->type != LXB_DOM_NODE_TYPE_ELEMENT) continue;
        if (lxb_dom_node_tag_id(c) == LXB_TAG_SCRIPT) { out[(*cnt)++] = c; continue; }
        if (lxb_dom_node_tag_id(c) == LXB_TAG_TEMPLATE) continue;
        collect_scripts(c, out, cnt, depth + 1);
    }
}

/* Run the document's scripts in order (inline + external), then fire
 * DOMContentLoaded / load. Called before styles are computed. */
void js_run_scripts(jsctx_t *j) {
    page_t *pg = j->pg;
    lxb_dom_node_t *list[JS_MAX_SCRIPTS]; int cnt = 0;
    collect_scripts(lxb_dom_interface_node(pg->doc), list, &cnt, 0);
    uint64_t start = now_ms();
    JS_UpdateStackTop(j->rt);
    for (int i = 0; i < cnt; i++) {
        lxb_dom_element_t *el = lxb_dom_interface_element(list[i]);
        if (!script_type_ok(el)) continue;
        if (lxb_dom_element_has_attribute(el, (const lxb_char_t *)"nomodule", 8)) {}
        uint64_t t = now_ms();
        if (t - start > JS_LOAD_BUDGET) { web_log("js: load budget exhausted, %d scripts skipped", cnt - i); break; }
        j->deadline = start + JS_LOAD_BUDGET;
        j->current_script = list[i];
        size_t sl = 0;
        const char *src = (const char *)lxb_dom_element_get_attribute(el, (const lxb_char_t *)"src", 3, &sl);
        if (src && sl) {
            char ref[1024], url[1024];
            snprintf(ref, sizeof ref, "%.*s", (int)(sl < 1023 ? sl : 1023), src);
            url_resolve(pg->url, ref, url, sizeof url);
            size_t len = 0;
            unsigned char *body = g_web.fetch ? g_web.fetch(g_web.ctx, url, &len, NULL, 0) : NULL;
            if (body) { css_note_script_names(pg, (const char *)body, len); eval(j, (const char *)body, len, url); free(body); }
        } else {
            size_t tl = 0;
            lxb_char_t *txt = lxb_dom_node_text_content(list[i], &tl);
            if (txt) { css_note_script_names(pg, (const char *)txt, tl); eval(j, (const char *)txt, tl, "<inline>"); lxb_dom_document_destroy_text(list[i]->owner_document, txt); }
        }
    }
    j->current_script = NULL;
    j->deadline = now_ms() + 500;
    const char load[] = "__weave.fireLoad && __weave.fireLoad()";
    eval(j, load, sizeof load - 1, "<load>");
    j->deadline = 0;
    web_log("js: %d scripts in %ums", cnt, (unsigned)(now_ms() - start));
}

/* Dispatch a DOM event to node; true if a handler called preventDefault(). */
bool js_dispatch(jsctx_t *j, lxb_dom_node_t *node, const char *type) {
    JS_UpdateStackTop(j->rt);
    JSValue global = JS_GetGlobalObject(j->ctx);
    JSValue host = JS_GetPropertyStr(j->ctx, global, "__weave");
    JSValue fn = JS_GetPropertyStr(j->ctx, host, "dispatch");
    bool prevented = false;
    if (JS_IsFunction(j->ctx, fn)) {
        JSValue args[2] = { wrap(j->ctx, node), JS_NewString(j->ctx, type) };
        j->deadline = now_ms() + JS_EVENT_BUDGET;
        JSValue r = JS_Call(j->ctx, fn, host, 2, args);
        if (JS_IsException(r)) report_exception(j->ctx, type);
        else prevented = JS_ToBool(j->ctx, r);
        JS_FreeValue(j->ctx, r);
        JS_FreeValue(j->ctx, args[0]); JS_FreeValue(j->ctx, args[1]);
        run_jobs(j);
        j->deadline = 0;
    }
    JS_FreeValue(j->ctx, fn); JS_FreeValue(j->ctx, host); JS_FreeValue(j->ctx, global);
    return prevented;
}

/* Run due timers. Returns the number run. */
int js_run_timers(jsctx_t *j) {
    uint64_t t = now_ms();
    int ran = 0;
    JS_UpdateStackTop(j->rt);
    j->deadline = t + JS_EVENT_BUDGET;
    for (int i = 0; i < JS_MAX_TIMERS; i++) {
        jtimer_t *tm = &j->timers[i];
        if (!tm->used || tm->due > t) continue;
        JSValue fn = JS_DupValue(j->ctx, tm->fn);
        if (tm->interval) tm->due = t + (uint64_t)tm->interval;
        else { JS_FreeValue(j->ctx, tm->fn); tm->used = false; }
        JSValue r = JS_Call(j->ctx, fn, JS_UNDEFINED, 0, NULL);
        if (JS_IsException(r)) report_exception(j->ctx, "timer");
        JS_FreeValue(j->ctx, r);
        JS_FreeValue(j->ctx, fn);
        ran++;
        if (now_ms() > j->deadline) break;
    }
    if (JS_IsJobPending(j->rt)) { run_jobs(j); ran++; }
    j->deadline = 0;
    return ran;
}

bool js_has_timers(jsctx_t *j) {
    for (int i = 0; i < JS_MAX_TIMERS; i++) if (j->timers[i].used) return true;
    return false;
}

/* Navigation requested by script (location.href = ...). */
bool js_take_navigation(jsctx_t *j, char *url, size_t cap) {
    if (!j->nav[0]) return false;
    snprintf(url, cap, "%s", j->nav);
    j->nav[0] = 0;
    return true;
}

int js_take_scroll(jsctx_t *j) { int s = j->scroll_req; j->scroll_req = -1; return s; }
