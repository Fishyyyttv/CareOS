/* web/engine/css.c -- stylesheet loading, CSS rewriting and computed style.
 *
 * lexbor does selector matching and stores the cascade, but drops @media
 * blocks and any declaration it cannot type (var(), calc(), background, gap,
 * border-radius, ...). So every declaration is rewritten before lexbor sees it:
 *
 *     color: var(--fg) !important   ==>   --x-color: 1000123 var(--fg) !important
 *
 * Custom properties keep any value, so lexbor stores all of them verbatim; the
 * number is a global source-order sequence (UA sheet < author sheets). Here we
 * walk each element's matched declarations, pick winners by
 * (important, origin, specificity, order), substitute var() and parse values. */
#include "engine.h"
#include "ua_css.h"
#include <stdio.h>
#include <strings.h>

#define AUTHOR_SEQ  1000000u
#define CUR_COLOR   0x00FFFFFEu      /* sentinel: currentColor, resolved late */

static uint64_t t_fetch, t_rewrite, t_attach, t_parse;
static uint64_t ms_now(void) { return g_web.now_ms ? g_web.now_ms(g_web.ctx) : 0; }
/* Stylesheets are shared across pages of a site; keep recent ones in memory
 * (most-recent first, bounded total size). Returns a malloc'd copy. */
typedef struct css_cache { char *url; unsigned char *body; size_t len; struct css_cache *next; } css_cache_t;
static css_cache_t *g_css_cache;
#define CSS_CACHE_BYTES (8u << 20)

static unsigned char *dup_bytes(const unsigned char *b, size_t n) {
    unsigned char *d = (unsigned char *)malloc(n + 1);
    if (d) { memcpy(d, b, n); d[n] = 0; }
    return d;
}

static unsigned char *css_fetch(const char *url, size_t *len) {
    css_cache_t **pp = &g_css_cache;
    for (css_cache_t *c = g_css_cache; c; pp = &c->next, c = c->next) {
        if (strcmp(c->url, url) == 0) {
            *pp = c->next; c->next = g_css_cache; g_css_cache = c;   /* move to front */
            *len = c->len;
            return dup_bytes(c->body, c->len);
        }
    }
    uint64_t t = ms_now();
    unsigned char *b = g_web.fetch(g_web.ctx, url, len, NULL, 0);
    t_fetch += ms_now() - t;
    if (!b || *len == 0 || *len > CSS_CACHE_BYTES / 2) return b;
    css_cache_t *c = (css_cache_t *)malloc(sizeof *c);
    if (!c) return b;
    c->url = w_strdup(url); c->body = dup_bytes(b, *len); c->len = *len;
    if (!c->url || !c->body) { free(c->url); free(c->body); free(c); return b; }
    c->next = g_css_cache; g_css_cache = c;
    /* evict from the tail beyond the budget */
    size_t total = 0;
    for (css_cache_t **q = &g_css_cache; *q; ) {
        total += (*q)->len;
        if (total > CSS_CACHE_BYTES) {
            css_cache_t *dead = *q; *q = dead->next;
            free(dead->url); free(dead->body); free(dead);
        } else q = &(*q)->next;
    }
    return b;
}

/* =================================================================== */
/*  Small text scanning helpers                                         */
/* =================================================================== */

typedef struct { char *s; size_t n, cap; } sbuf_t;

static void sb_put(sbuf_t *b, const char *s, size_t n) {
    if (b->n + n + 1 > b->cap) {
        size_t nc = b->cap ? b->cap * 2 : 4096;
        while (nc < b->n + n + 1) nc *= 2;
        char *ns = (char *)realloc(b->s, nc);
        if (!ns) return;
        b->s = ns; b->cap = nc;
    }
    memcpy(b->s + b->n, s, n); b->n += n; b->s[b->n] = 0;
}
static void sb_puts(sbuf_t *b, const char *s) { sb_put(b, s, strlen(s)); }

static const char *skip_ws_comments(const char *p, const char *e) {
    for (;;) {
        while (p < e && (*p == ' ' || *p == '\n' || *p == '\t' || *p == '\r' || *p == '\f')) p++;
        if (p + 1 < e && p[0] == '/' && p[1] == '*') {
            const char *q = p + 2;
            while (q + 1 < e && !(q[0] == '*' && q[1] == '/')) q++;
            p = (q + 1 < e) ? q + 2 : e;
            continue;
        }
        if (p + 3 < e && p[0] == '<' && p[1] == '!' && p[2] == '-' && p[3] == '-') { p += 4; continue; }
        if (p + 2 < e && p[0] == '-' && p[1] == '-' && p[2] == '>') { p += 3; continue; }
        return p;
    }
}

/* Advance past a string literal starting at *p (quote char). */
static const char *skip_string(const char *p, const char *e) {
    char q = *p++;
    while (p < e && *p != q) { if (*p == '\\' && p + 1 < e) p++; p++; }
    return p < e ? p + 1 : e;
}

/* Find the first top-level char in `stops` (depth 0 in () and []), honoring
 * strings and comments. Returns e if none. */
static const char *find_top(const char *p, const char *e, const char *stops) {
    int depth = 0;
    while (p < e) {
        char c = *p;
        if (c == '"' || c == '\'') { p = skip_string(p, e); continue; }
        if (c == '/' && p + 1 < e && p[1] == '*') { p = skip_ws_comments(p, e); continue; }
        if (c == '(' || c == '[') depth++;
        else if ((c == ')' || c == ']') && depth > 0) depth--;
        else if (depth == 0 && strchr(stops, c)) return p;
        p++;
    }
    return e;
}

/* p points at '{'; returns pointer just past the matching '}'. */
static const char *skip_block(const char *p, const char *e) {
    int depth = 0;
    while (p < e) {
        char c = *p;
        if (c == '"' || c == '\'') { p = skip_string(p, e); continue; }
        if (c == '/' && p + 1 < e && p[1] == '*') { p = skip_ws_comments(p, e); continue; }
        if (c == '{') depth++;
        else if (c == '}') { depth--; if (depth == 0) return p + 1; }
        p++;
    }
    return e;
}

static void trim(const char **s, const char **e) {
    while (*s < *e && (unsigned char)**s <= ' ') (*s)++;
    while (*e > *s && (unsigned char)(*e)[-1] <= ' ') (*e)--;
}

/* =================================================================== */
/*  Media queries                                                       */
/* =================================================================== */

typedef struct { int vw, vh; } media_env_t;

static float mq_len(const char *s, const char *e) {
    float v = strtof(s, NULL);
    const char *u = s;
    while (u < e && ((*u >= '0' && *u <= '9') || *u == '.' || *u == '-' || *u == '+')) u++;
    if (u + 2 <= e && (strncmp(u, "em", 2) == 0 || strncmp(u, "rem", 3) == 0)) v *= 16;
    return v;
}

static bool mq_feature(const char *s, const char *e, const media_env_t *env) {
    trim(&s, &e);
    const char *colon = memchr(s, ':', (size_t)(e - s));
    /* range syntax: (width >= 600px) / (400px <= width <= 700px) */
    const char *op = NULL;
    for (const char *q = s; q < e; q++) if (*q == '<' || *q == '>') { op = q; break; }
    if (!colon && op) {
        const char *ns = s, *ne = op; trim(&ns, &ne);
        bool ge = op[0] == '>', eq = (op + 1 < e && op[1] == '=');
        const char *vs = op + (eq ? 2 : 1), *ve = e;
        const char *op2 = NULL;
        for (const char *q = vs; q < e; q++) if (*q == '<' || *q == '>') { op2 = q; break; }
        if (op2) ve = op2;
        trim(&vs, &ve);
        bool name_first = (*ns >= 'a' && *ns <= 'z');
        const char *name = name_first ? ns : vs; const char *nend = name_first ? ne : ve;
        float val = name_first ? mq_len(vs, ve) : mq_len(ns, ne);
        if (!name_first) ge = !ge;  /* "600px <= width" == "width >= 600px" */
        float cur = 0;
        if (w_ieqn(name, (size_t)(nend - name), "width")) cur = (float)env->vw;
        else if (w_ieqn(name, (size_t)(nend - name), "height")) cur = (float)env->vh;
        else return false;
        bool ok = ge ? (eq ? cur >= val : cur > val) : (eq ? cur <= val : cur < val);
        if (op2 && ok) {
            bool ge2 = op2[0] == '>', eq2 = (op2 + 1 < e && op2[1] == '=');
            const char *v2 = op2 + (eq2 ? 2 : 1), *v2e = e; trim(&v2, &v2e);
            float val2 = mq_len(v2, v2e);
            ok = ge2 ? (eq2 ? cur >= val2 : cur > val2) : (eq2 ? cur <= val2 : cur < val2);
        }
        return ok;
    }
    if (!colon) {
        size_t n = (size_t)(e - s);
        if (w_ieqn(s, n, "color") || w_ieqn(s, n, "hover") || w_ieqn(s, n, "pointer") ||
            w_ieqn(s, n, "any-hover") || w_ieqn(s, n, "any-pointer")) return true;
        return false;
    }
    const char *ns = s, *ne = colon; trim(&ns, &ne);
    const char *vs = colon + 1, *ve = e; trim(&vs, &ve);
    size_t nn = (size_t)(ne - ns), vn = (size_t)(ve - vs);
    if (w_ieqn(ns, nn, "min-width")) return env->vw >= mq_len(vs, ve);
    if (w_ieqn(ns, nn, "max-width")) return env->vw <= mq_len(vs, ve);
    if (w_ieqn(ns, nn, "min-height")) return env->vh >= mq_len(vs, ve);
    if (w_ieqn(ns, nn, "max-height")) return env->vh <= mq_len(vs, ve);
    if (w_ieqn(ns, nn, "min-device-width")) return env->vw >= mq_len(vs, ve);
    if (w_ieqn(ns, nn, "max-device-width")) return env->vw <= mq_len(vs, ve);
    if (w_ieqn(ns, nn, "orientation")) return w_ieqn(vs, vn, env->vw >= env->vh ? "landscape" : "portrait");
    if (w_ieqn(ns, nn, "prefers-color-scheme")) return w_ieqn(vs, vn, "light");
    if (w_ieqn(ns, nn, "prefers-reduced-motion")) return w_ieqn(vs, vn, "no-preference");
    if (w_ieqn(ns, nn, "prefers-contrast")) return w_ieqn(vs, vn, "no-preference");
    if (w_ieqn(ns, nn, "hover") || w_ieqn(ns, nn, "any-hover")) return w_ieqn(vs, vn, "hover");
    if (w_ieqn(ns, nn, "pointer") || w_ieqn(ns, nn, "any-pointer")) return w_ieqn(vs, vn, "fine");
    if (w_ieqn(ns, nn, "min-resolution") || w_ieqn(ns, nn, "-webkit-min-device-pixel-ratio") ||
        w_ieqn(ns, nn, "min--moz-device-pixel-ratio")) {
        float v = strtof(vs, NULL);
        return v <= 1.0f || (v <= 96.0f && w_istr(vs, vn, "dpi"));
    }
    if (w_ieqn(ns, nn, "max-resolution") || w_ieqn(ns, nn, "-webkit-max-device-pixel-ratio")) return true;
    if (w_ieqn(ns, nn, "display-mode")) return w_ieqn(vs, vn, "browser");
    if (w_ieqn(ns, nn, "scripting")) return w_ieqn(vs, vn, "enabled");
    return false;
}

/* One comma-free media query: [not|only] [type] [and (feature)]* */
static bool mq_one(const char *s, const char *e, const media_env_t *env) {
    trim(&s, &e);
    if (s >= e) return true;
    bool neg = false, result = true;
    const char *p = s;
    while (p < e) {
        p = skip_ws_comments(p, e);
        if (p >= e) break;
        if (*p == '(') {
            int d = 0; const char *q = p;
            while (q < e) { if (*q == '(') d++; else if (*q == ')') { d--; if (!d) break; } q++; }
            const char *inner = p + 1, *inner_e = q;
            /* nested "(a) or (b)" / "not (a)" */
            if (inner < inner_e && *skip_ws_comments(inner, inner_e) == '(')
                result = result && mq_one(inner, inner_e, env);
            else
                result = result && mq_feature(inner, inner_e, env);
            p = q < e ? q + 1 : e;
            continue;
        }
        const char *w = p;
        while (p < e && *p != ' ' && *p != '(' && *p != '\n' && *p != '\t') p++;
        size_t n = (size_t)(p - w);
        if (w_ieqn(w, n, "not")) neg = true;
        else if (w_ieqn(w, n, "only") || w_ieqn(w, n, "and")) {}
        else if (w_ieqn(w, n, "or")) { if (result) return !neg; result = true; }
        else if (w_ieqn(w, n, "screen") || w_ieqn(w, n, "all")) {}
        else result = false;   /* print, speech, tv, unknown */
    }
    return neg ? !result : result;
}

static bool media_matches(const char *s, const char *e, const media_env_t *env) {
    trim(&s, &e);
    if (s >= e) return true;
    while (s < e) {
        const char *c = find_top(s, e, ",");
        if (mq_one(s, c, env)) return true;
        s = c < e ? c + 1 : e;
    }
    return false;
}

static bool supports_matches(const char *s, const char *e) {
    /* Optimistic: true unless it is a negation or a selector() test. */
    trim(&s, &e);
    if (w_istr(s, (size_t)(e - s), "not ") || w_istr(s, (size_t)(e - s), "not(")) return false;
    if (w_istr(s, (size_t)(e - s), "selector(")) return false;
    if (w_istr(s, (size_t)(e - s), "subgrid") || w_istr(s, (size_t)(e - s), "contain-intrinsic")) return false;
    return true;
}

/* =================================================================== */
/*  Rewriter                                                            */
/* =================================================================== */

typedef struct {
    page_t *pg;
    media_env_t env;
    u32   *seq;
    sbuf_t out;
    int    depth;
    const char *base_url;
} rw_t;

static bool selector_part_ok(const char *s, const char *e) {
    size_t n = (size_t)(e - s);
    static const char *bad[] = {
        "::", ":before", ":after", ":first-line", ":first-letter", ":hover",
        ":focus", ":active", ":visited", ":target", ":checked", ":placeholder",
        ":-webkit", ":-moz", ":-ms-", ":selection", ":marker", ":backdrop",
        ":autofill", ":invalid", ":valid", ":user-", ":indeterminate", ":open",
        ":popover", ":modal", ":fullscreen", ":picture-in-picture", ":playing",
        ":paused", ":defined", ":state(", "&", NULL };
    for (int i = 0; bad[i]; i++) if (w_istr(s, n, bad[i])) return false;
    return n > 0;
}

/* Remove pseudo-classes that are always true in a static, unfocused page
 * (":not(:hover)" etc.), so rules like skip-link hiding still apply. Returns
 * the cleaned length, 0 if it does not fit. */
static size_t clean_selector(const char *s, const char *e, char *out, size_t cap) {
    static const char *always[] = { ":not(:focus)", ":not(:focus-visible)", ":not(:focus-within)",
        ":not(:hover)", ":not(:active)", ":not(:target)", ":not(:visited)", ":not(:checked)",
        ":not(:placeholder-shown)", ":not(:disabled)", ":enabled", ":link", ":any-link", NULL };
    size_t o = 0;
    while (s < e) {
        bool skipped = false;
        if (*s == ':') {
            for (int i = 0; always[i]; i++) {
                size_t l = strlen(always[i]);
                if ((size_t)(e - s) >= l && w_ieqn(s, l, always[i])) {
                    /* keep ":link" etc. from swallowing a longer name */
                    char nx = (size_t)(e - s) > l ? s[l] : 0;
                    if ((nx >= 'a' && nx <= 'z') || nx == '-') continue;
                    s += l; skipped = true; break;
                }
            }
        }
        if (skipped) continue;
        if (o + 1 >= cap) return 0;
        out[o++] = *s++;
    }
    out[o] = 0;
    return o;
}

static uint64_t name_hash(char kind, const char *s, size_t n) {
    uint64_t h = 1469598103934665603ull ^ (u8)kind;
    for (size_t i = 0; i < n; i++) { h ^= (u8)s[i]; h *= 1099511628211ull; }
    return h ? h : 1;
}

static void set_add(page_t *pg, uint64_t h) {
    if (!pg->sel_cap) return;
    u32 m = pg->sel_cap - 1, i = (u32)h & m;
    while (pg->sel_set[i] && pg->sel_set[i] != h) i = (i + 1) & m;
    pg->sel_set[i] = h;
}

static bool set_has(page_t *pg, uint64_t h) {
    if (!pg->sel_cap) return true;
    u32 m = pg->sel_cap - 1, i = (u32)h & m;
    while (pg->sel_set[i]) { if (pg->sel_set[i] == h) return true; i = (i + 1) & m; }
    return false;
}

/* False when the selector requires a class or id that no element has.
 * Only top-level compounds are checked (:not()/:is() contents are skipped). */
static bool selector_present(page_t *pg, const char *s, const char *e) {
    if (!pg->sel_cap) return true;
    int depth = 0;
    while (s < e) {
        char c = *s;
        if (c == '"' || c == '\'') { s = skip_string(s, e); continue; }
        if (c == '(' || c == '[') { depth++; s++; continue; }
        if (c == ')' || c == ']') { if (depth) depth--; s++; continue; }
        if (depth == 0 && (c == '.' || c == '#')) {
            char name[256]; size_t n = 0; bool unsure = false;
            const char *p = s + 1;
            while (p < e) {
                char d = *p;
                if (d == '\\' && p + 1 < e) {
                    char x = p[1];
                    if ((x >= '0' && x <= '9') || (x >= 'a' && x <= 'f') || (x >= 'A' && x <= 'F')) unsure = true;
                    if (n < sizeof name) name[n++] = x;
                    p += 2; continue;
                }
                if ((d >= 'a' && d <= 'z') || (d >= 'A' && d <= 'Z') || (d >= '0' && d <= '9') ||
                    d == '-' || d == '_' || (u8)d >= 0x80) { if (n < sizeof name) name[n++] = d; p++; continue; }
                break;
            }
            if (n && !unsure && n < sizeof name && !set_has(pg, name_hash(c, name, n))) return false;
            s = p;
            continue;
        }
        s++;
    }
    return true;
}

static void collect_names(page_t *pg, lxb_dom_node_t *n, int depth) {
    if (depth > 600) return;
    for (lxb_dom_node_t *c = n->first_child; c; c = c->next) {
        if (c->type != LXB_DOM_NODE_TYPE_ELEMENT) continue;
        lxb_dom_element_t *el = lxb_dom_interface_element(c);
        size_t l = 0;
        const char *v = (const char *)lxb_dom_element_get_attribute(el, (const lxb_char_t *)"class", 5, &l);
        if (v) {
            const char *p = v, *ve = v + l;
            while (p < ve) {
                while (p < ve && (u8)*p <= ' ') p++;
                const char *w = p;
                while (p < ve && (u8)*p > ' ') p++;
                if (p > w) set_add(pg, name_hash('.', w, (size_t)(p - w)));
            }
        }
        v = (const char *)lxb_dom_element_get_attribute(el, (const lxb_char_t *)"id", 2, &l);
        if (v && l) set_add(pg, name_hash('#', v, l));
        collect_names(pg, c, depth + 1);
    }
}

static void build_name_set(page_t *pg) {
    pg->sel_cap = 1u << 15;            /* 32768 slots; ~16k names before saturation */
    pg->sel_set = (uint64_t *)calloc(pg->sel_cap, sizeof(uint64_t));
    if (!pg->sel_set) { pg->sel_cap = 0; return; }
    collect_names(pg, lxb_dom_interface_node(pg->doc), 0);
    /* nearly full: filtering would be slow and imprecise -- disable */
    u32 used = 0;
    for (u32 i = 0; i < pg->sel_cap; i++) used += pg->sel_set[i] != 0;
    if (used > pg->sel_cap / 2) { free(pg->sel_set); pg->sel_set = NULL; pg->sel_cap = 0; }
}

/* Rewrite a declaration block body (between braces) into rw->out. */
static void rewrite_decls(rw_t *rw, const char *p, const char *e, u32 *seq, char kind) {
    while (p < e) {
        p = skip_ws_comments(p, e);
        if (p >= e) break;
        const char *semi = find_top(p, e, ";{");
        if (semi < e && *semi == '{') {           /* nested rule: skip it */
            const char *q = skip_block(semi, e);
            p = q; continue;
        }
        const char *colon = find_top(p, semi, ":");
        if (colon < semi) {
            const char *ns = p, *ne = colon; trim(&ns, &ne);
            const char *vs = colon + 1, *ve = semi; trim(&vs, &ve);
            bool imp = false;
            const char *bang = find_top(vs, ve, "!");
            if (bang < ve && w_istr(bang, (size_t)(ve - bang), "important")) { imp = true; ve = bang; trim(&vs, &ve); }
            if (ne > ns && ve > vs && (size_t)(ne - ns) < 64) {
                char hdr[128];
                int hl = snprintf(hdr, sizeof hdr, "--%c-%.*s:%u ", kind, (int)(ne - ns), ns, (*seq)++);
                for (int i = 4; i < hl && hdr[i] != ':'; i++)
                    if (hdr[i] >= 'A' && hdr[i] <= 'Z') hdr[i] = (char)(hdr[i] + 32);
                sb_put(&rw->out, hdr, (size_t)hl);
                sb_put(&rw->out, vs, (size_t)(ve - vs));
                if (imp) sb_puts(&rw->out, " !important");
                sb_puts(&rw->out, ";");
            }
        }
        p = semi < e ? semi + 1 : e;
    }
}

static void rewrite_sheet(rw_t *rw, const char *p, const char *e);

static void do_import(rw_t *rw, const char *s, const char *e) {
    if (rw->depth >= 3 || !g_web.fetch) return;
    /* @import url("x") media;  |  @import "x" media; */
    trim(&s, &e);
    char ref[1024]; const char *rest = e;
    if (w_istr(s, (size_t)(e - s), "url(") == s) {
        const char *a = s + 4, *b = find_top(a, e, ")");
        trim(&a, &b);
        if (a < b && (*a == '"' || *a == '\'')) { a++; b--; }
        snprintf(ref, sizeof ref, "%.*s", (int)(b - a), a);
        rest = b < e ? b + 1 : e;
    } else if (*s == '"' || *s == '\'') {
        const char *b = skip_string(s, e);
        snprintf(ref, sizeof ref, "%.*s", (int)(b - s - 2), s + 1);
        rest = b;
    } else return;
    /* layer()/supports() conditions are ignored; plain media list honoured */
    const char *ms = rest, *me = e; trim(&ms, &me);
    if (ms < me && !w_istr(ms, (size_t)(me - ms), "layer") && !w_istr(ms, (size_t)(me - ms), "supports")
        && !media_matches(ms, me, &rw->env)) return;
    char url[1024];
    url_resolve(rw->base_url, ref, url, sizeof url);
    size_t len = 0;
    unsigned char *body = css_fetch(url, &len);
    if (!body) return;
    const char *saved = rw->base_url;
    rw->base_url = url;
    rw->depth++;
    rewrite_sheet(rw, (const char *)body, (const char *)body + len);
    rw->depth--;
    rw->base_url = saved;
    free(body);
}

/* Rewrite url(...) references inside declaration values to absolute URLs, so
 * a style computed later resolves against the stylesheet, not the document. */
static void absolutize_urls(rw_t *rw, const char *s, const char *e, sbuf_t *dst) {
    while (s < e) {
        const char *u = w_istr(s, (size_t)(e - s), "url(");
        if (!u) { sb_put(dst, s, (size_t)(e - s)); return; }
        sb_put(dst, s, (size_t)(u - s) + 4);
        const char *a = u + 4, *b = find_top(a, e, ")");
        const char *ta = a, *tb = b; trim(&ta, &tb);
        if (ta < tb && (*ta == '"' || *ta == '\'')) { ta++; tb--; }
        if (tb - ta > 0 && tb - ta < 900 && strncmp(ta, "data:", 5) != 0) {
            char ref[1024], abs_[1024];
            snprintf(ref, sizeof ref, "%.*s", (int)(tb - ta), ta);
            url_resolve(rw->base_url, ref, abs_, sizeof abs_);
            sb_puts(dst, "\""); sb_puts(dst, abs_); sb_puts(dst, "\"");
        } else {
            sb_put(dst, a, (size_t)(b - a));
        }
        s = b;
    }
}

static void rewrite_sheet(rw_t *rw, const char *p, const char *e) {
    while (p < e) {
        p = skip_ws_comments(p, e);
        if (p >= e) break;
        if (*p == '}' || *p == ';') { p++; continue; }
        if (*p == '@') {
            const char *kw = p + 1, *k = kw;
            while (k < e && ((*k >= 'a' && *k <= 'z') || (*k >= 'A' && *k <= 'Z') || *k == '-')) k++;
            size_t kn = (size_t)(k - kw);
            const char *stop = find_top(k, e, "{;");
            if (stop >= e) return;
            if (*stop == ';') {
                if (w_ieqn(kw, kn, "import")) do_import(rw, k, stop);
                p = stop + 1;
                continue;
            }
            const char *bend = skip_block(stop, e);
            const char *inner = stop + 1, *inner_e = bend > inner ? bend - 1 : inner;
            if (w_ieqn(kw, kn, "media")) {
                if (media_matches(k, stop, &rw->env)) rewrite_sheet(rw, inner, inner_e);
            } else if (w_ieqn(kw, kn, "supports")) {
                if (supports_matches(k, stop)) rewrite_sheet(rw, inner, inner_e);
            } else if (w_ieqn(kw, kn, "layer") || w_ieqn(kw, kn, "document") ||
                       w_ieqn(kw, kn, "-moz-document") || w_ieqn(kw, kn, "scope")) {
                rewrite_sheet(rw, inner, inner_e);
            }
            /* font-face, keyframes, page, property, counter-style, ... dropped */
            p = bend;
            continue;
        }
        /* qualified rule */
        const char *brace = find_top(p, e, "{");
        if (brace >= e) return;
        const char *bend = skip_block(brace, e);
        const char *inner = brace + 1, *inner_e = bend > inner ? bend - 1 : inner;
        /* filter the selector list; ::before / ::after parts become their own
         * rules whose declarations carry the --b- / --a- prefix */
        sbuf_t sel[3] = {{0}};             /* element, before, after */
        static const char kinds[3] = { 'x', 'b', 'a' };
        const char *s = p;
        while (s < brace) {
            const char *c = find_top(s, brace, ",");
            const char *a = s, *b = c; trim(&a, &b);
            char clean[1024];
            size_t cl = clean_selector(a, b, clean, sizeof clean);
            int which = 0;
            static const char *pseudo[4] = { "::before", ":before", "::after", ":after" };
            for (int q = 0; q < 4; q++) {
                size_t pl = strlen(pseudo[q]);
                if (cl > pl && w_ieqn(clean + cl - pl, pl, pseudo[q])) { cl -= pl; which = q < 2 ? 1 : 2; break; }
            }
            if (which && (cl == 0 || clean[cl - 1] == ' ' || clean[cl - 1] == '>')) {
                /* "::before" alone or after a combinator means "*::before" */
                if (cl + 1 < sizeof clean) clean[cl++] = '*';
            }
            clean[cl] = 0;
            if (cl && selector_part_ok(clean, clean + cl) && selector_present(rw->pg, clean, clean + cl)) {
                if (sel[which].n) sb_puts(&sel[which], ",");
                sb_put(&sel[which], clean, cl);
            }
            s = c < brace ? c + 1 : brace;
        }
        if (sel[0].n || sel[1].n || sel[2].n) {
            sbuf_t decl = {0};
            absolutize_urls(rw, inner, inner_e, &decl);
            for (int w = 0; w < 3; w++) {
                if (!sel[w].n) continue;
                sb_put(&rw->out, sel[w].s, sel[w].n);
                sb_puts(&rw->out, "{");
                if (decl.s) rewrite_decls(rw, decl.s, decl.s + decl.n, rw->seq, kinds[w]);
                sb_puts(&rw->out, "}\n");
            }
            free(decl.s);
        }
        for (int w = 0; w < 3; w++) free(sel[w].s);
        p = bend;
    }
}

static void attach_sheet_text(page_t *pg, const char *css, size_t len, const char *base,
                              u32 *seq) {
    if (pg->nsheets >= (int)(sizeof pg->sheets / sizeof pg->sheets[0])) return;
    rw_t rw = { .pg = pg, .env = { pg->vw, pg->vh }, .seq = seq, .base_url = base };
    uint64_t t0 = ms_now();
    rewrite_sheet(&rw, css, css + len);
    uint64_t t1 = ms_now();
    t_rewrite += t1 - t0;
    if (!rw.out.s) return;
    lxb_css_stylesheet_t *sst = lxb_css_stylesheet_create(NULL);
    if (sst && lxb_css_stylesheet_parse(sst, pg->css_parser, (const lxb_char_t *)rw.out.s, rw.out.n) == LXB_STATUS_OK) {
        uint64_t t2 = ms_now();
        t_rewrite += 0;
        lxb_html_document_stylesheet_attach(pg->doc, sst);
        t_attach += ms_now() - t2;
        t_parse += t2 - t1;
        pg->sheets[pg->nsheets++] = sst;
    } else if (sst) {
        lxb_css_stylesheet_destroy(sst, true);
    }
    free(rw.out.s);
}

static const char *attr(lxb_dom_element_t *el, const char *name, size_t *len) {
    size_t l = 0;
    const lxb_char_t *v = lxb_dom_element_get_attribute(el, (const lxb_char_t *)name, strlen(name), &l);
    if (len) *len = l;
    return (const char *)v;
}

static void collect_sheets(page_t *pg, lxb_dom_node_t *n, u32 *seq, int *budget) {
    for (lxb_dom_node_t *c = n->first_child; c; c = c->next) {
        if (c->type != LXB_DOM_NODE_TYPE_ELEMENT) continue;
        lxb_dom_element_t *el = lxb_dom_interface_element(c);
        uintptr_t tag = lxb_dom_node_tag_id(c);
        if (tag == LXB_TAG_STYLE) {
            size_t ml; const char *media = attr(el, "media", &ml);
            media_env_t env = { pg->vw, pg->vh };
            if (media && !media_matches(media, media + ml, &env)) continue;
            size_t tl = 0;
            lxb_char_t *txt = lxb_dom_node_text_content(c, &tl);
            if (txt) { attach_sheet_text(pg, (const char *)txt, tl, pg->url, seq);
                       lxb_dom_document_destroy_text(c->owner_document, txt); }
            continue;
        }
        if (tag == LXB_TAG_LINK) {
            size_t rl, hl, ml;
            const char *rel = attr(el, "rel", &rl), *href = attr(el, "href", &hl);
            const char *media = attr(el, "media", &ml);
            if (!rel || !href || !w_istr(rel, rl, "stylesheet") || w_istr(rel, rl, "alternate")) continue;
            media_env_t env = { pg->vw, pg->vh };
            if (media && !media_matches(media, media + ml, &env)) continue;
            if (*budget <= 0 || !g_web.fetch) continue;
            (*budget)--;
            char ref[1024], url[1024];
            snprintf(ref, sizeof ref, "%.*s", (int)hl, href);
            url_resolve(pg->url, ref, url, sizeof url);
            size_t len = 0;
            unsigned char *body = css_fetch(url, &len);
            if (body) { attach_sheet_text(pg, (const char *)body, len, url, seq); free(body); }
            continue;
        }
        if (tag == LXB_TAG_TEMPLATE || tag == LXB_TAG_SVG) continue;
        collect_sheets(pg, c, seq, budget);
    }
}

static void apply_inline_styles(page_t *pg, lxb_dom_node_t *n, u32 *seq) {
    for (lxb_dom_node_t *c = n->first_child; c; c = c->next) {
        if (c->type != LXB_DOM_NODE_TYPE_ELEMENT) continue;
        lxb_dom_element_t *el = lxb_dom_interface_element(c);
        size_t sl = 0;
        const char *s = attr(el, "style", &sl);
        if (s && sl) {
            rw_t rw = { .pg = pg, .env = { pg->vw, pg->vh }, .seq = seq, .base_url = pg->url };
            sbuf_t abs_ = {0};
            absolutize_urls(&rw, s, s + sl, &abs_);
            if (abs_.s) rewrite_decls(&rw, abs_.s, abs_.s + abs_.n, seq, 'x');
            if (rw.out.s) lxb_dom_element_style_parse(el, (const lxb_char_t *)rw.out.s, rw.out.n);
            free(rw.out.s); free(abs_.s);
        }
        apply_inline_styles(pg, c, seq);
    }
}

void css_load_document_styles(page_t *pg) {
    pg->css_parser = lxb_css_parser_create();
    lxb_css_parser_init(pg->css_parser, NULL);
    lxb_html_document_css_init(pg->doc, false);
    build_name_set(pg);
    u32 seq = 1;
    attach_sheet_text(pg, UA_CSS, sizeof UA_CSS - 1, pg->url, &seq);
    if (lxb_dom_interface_document(pg->doc)->compat_mode == LXB_DOM_DOCUMENT_CMODE_QUIRKS)
        attach_sheet_text(pg, UA_QUIRKS_CSS, sizeof UA_QUIRKS_CSS - 1, pg->url, &seq);
    seq = AUTHOR_SEQ;
    int budget = 24;
    collect_sheets(pg, lxb_dom_interface_node(pg->doc), &seq, &budget);
    uint64_t ti = ms_now();
    apply_inline_styles(pg, lxb_dom_interface_node(pg->doc), &seq);
    web_log("css: fetch %ums rewrite %ums parse %ums attach %ums inline %ums", (unsigned)t_fetch,
            (unsigned)(t_rewrite - t_fetch > t_rewrite ? 0 : t_rewrite), (unsigned)t_parse, (unsigned)t_attach, (unsigned)(ms_now() - ti));
    t_fetch = t_rewrite = t_attach = t_parse = 0;
}

/* =================================================================== */
/*  Value parsing                                                       */
/* =================================================================== */

typedef struct {
    page_t *pg;
    style_t *st;
    const style_t *parent;
    lxb_dom_element_t *el;
    float em, rem;
} cctx_t;

/* Split the next top-level whitespace-separated token (commas and slashes are
 * their own tokens). Returns false at end. */
static bool next_tok(const char **pp, const char *e, const char **ts, const char **te) {
    const char *p = *pp;
    while (p < e && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    if (p >= e) { *pp = p; return false; }
    *ts = p;
    if (*p == ',' || *p == '/') { *te = p + 1; *pp = p + 1; return true; }
    int depth = 0;
    while (p < e) {
        char c = *p;
        if (c == '"' || c == '\'') { p = skip_string(p, e); continue; }
        if (c == '(') depth++;
        else if (c == ')') { if (depth) depth--; }
        else if (depth == 0 && (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ',' || c == '/')) break;
        p++;
    }
    *te = p; *pp = p;
    return true;
}

static bool tok_is(const char *s, const char *e, const char *kw) { return w_ieqn(s, (size_t)(e - s), kw); }

static int hexd(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

typedef struct { const char *name; u32 rgb; } named_color_t;
static const named_color_t NAMED[] = {
 {"aliceblue",0xf0f8ff},{"antiquewhite",0xfaebd7},{"aqua",0x00ffff},{"aquamarine",0x7fffd4},
 {"azure",0xf0ffff},{"beige",0xf5f5dc},{"bisque",0xffe4c4},{"black",0x000000},
 {"blanchedalmond",0xffebcd},{"blue",0x0000ff},{"blueviolet",0x8a2be2},{"brown",0xa52a2a},
 {"burlywood",0xdeb887},{"cadetblue",0x5f9ea0},{"chartreuse",0x7fff00},{"chocolate",0xd2691e},
 {"coral",0xff7f50},{"cornflowerblue",0x6495ed},{"cornsilk",0xfff8dc},{"crimson",0xdc143c},
 {"cyan",0x00ffff},{"darkblue",0x00008b},{"darkcyan",0x008b8b},{"darkgoldenrod",0xb8860b},
 {"darkgray",0xa9a9a9},{"darkgreen",0x006400},{"darkgrey",0xa9a9a9},{"darkkhaki",0xbdb76b},
 {"darkmagenta",0x8b008b},{"darkolivegreen",0x556b2f},{"darkorange",0xff8c00},{"darkorchid",0x9932cc},
 {"darkred",0x8b0000},{"darksalmon",0xe9967a},{"darkseagreen",0x8fbc8f},{"darkslateblue",0x483d8b},
 {"darkslategray",0x2f4f4f},{"darkslategrey",0x2f4f4f},{"darkturquoise",0x00ced1},{"darkviolet",0x9400d3},
 {"deeppink",0xff1493},{"deepskyblue",0x00bfff},{"dimgray",0x696969},{"dimgrey",0x696969},
 {"dodgerblue",0x1e90ff},{"firebrick",0xb22222},{"floralwhite",0xfffaf0},{"forestgreen",0x228b22},
 {"fuchsia",0xff00ff},{"gainsboro",0xdcdcdc},{"ghostwhite",0xf8f8ff},{"gold",0xffd700},
 {"goldenrod",0xdaa520},{"gray",0x808080},{"green",0x008000},{"greenyellow",0xadff2f},
 {"grey",0x808080},{"honeydew",0xf0fff0},{"hotpink",0xff69b4},{"indianred",0xcd5c5c},
 {"indigo",0x4b0082},{"ivory",0xfffff0},{"khaki",0xf0e68c},{"lavender",0xe6e6fa},
 {"lavenderblush",0xfff0f5},{"lawngreen",0x7cfc00},{"lemonchiffon",0xfffacd},{"lightblue",0xadd8e6},
 {"lightcoral",0xf08080},{"lightcyan",0xe0ffff},{"lightgoldenrodyellow",0xfafad2},{"lightgray",0xd3d3d3},
 {"lightgreen",0x90ee90},{"lightgrey",0xd3d3d3},{"lightpink",0xffb6c1},{"lightsalmon",0xffa07a},
 {"lightseagreen",0x20b2aa},{"lightskyblue",0x87cefa},{"lightslategray",0x778899},{"lightslategrey",0x778899},
 {"lightsteelblue",0xb0c4de},{"lightyellow",0xffffe0},{"lime",0x00ff00},{"limegreen",0x32cd32},
 {"linen",0xfaf0e6},{"magenta",0xff00ff},{"maroon",0x800000},{"mediumaquamarine",0x66cdaa},
 {"mediumblue",0x0000cd},{"mediumorchid",0xba55d3},{"mediumpurple",0x9370db},{"mediumseagreen",0x3cb371},
 {"mediumslateblue",0x7b68ee},{"mediumspringgreen",0x00fa9a},{"mediumturquoise",0x48d1cc},{"mediumvioletred",0xc71585},
 {"midnightblue",0x191970},{"mintcream",0xf5fffa},{"mistyrose",0xffe4e1},{"moccasin",0xffe4b5},
 {"navajowhite",0xffdead},{"navy",0x000080},{"oldlace",0xfdf5e6},{"olive",0x808000},
 {"olivedrab",0x6b8e23},{"orange",0xffa500},{"orangered",0xff4500},{"orchid",0xda70d6},
 {"palegoldenrod",0xeee8aa},{"palegreen",0x98fb98},{"paleturquoise",0xafeeee},{"palevioletred",0xdb7093},
 {"papayawhip",0xffefd5},{"peachpuff",0xffdab9},{"peru",0xcd853f},{"pink",0xffc0cb},
 {"plum",0xdda0dd},{"powderblue",0xb0e0e6},{"purple",0x800080},{"rebeccapurple",0x663399},
 {"red",0xff0000},{"rosybrown",0xbc8f8f},{"royalblue",0x4169e1},{"saddlebrown",0x8b4513},
 {"salmon",0xfa8072},{"sandybrown",0xf4a460},{"seagreen",0x2e8b57},{"seashell",0xfff5ee},
 {"sienna",0xa0522d},{"silver",0xc0c0c0},{"skyblue",0x87ceeb},{"slateblue",0x6a5acd},
 {"slategray",0x708090},{"slategrey",0x708090},{"snow",0xfffafa},{"springgreen",0x00ff7f},
 {"steelblue",0x4682b4},{"tan",0xd2b48c},{"teal",0x008080},{"thistle",0xd8bfd8},
 {"tomato",0xff6347},{"turquoise",0x40e0d0},{"violet",0xee82ee},{"wheat",0xf5deb3},
 {"white",0xffffff},{"whitesmoke",0xf5f5f5},{"yellow",0xffff00},{"yellowgreen",0x9acd32},
 {"canvas",0xffffff},{"canvastext",0x000000},{"buttonface",0xefefef},{"buttontext",0x000000},
 {"field",0xffffff},{"fieldtext",0x000000},{"linktext",0x0000ee},{"graytext",0x808080},
 {"highlight",0x3390ff},{"highlighttext",0xffffff},{"windowtext",0x000000},{"window",0xffffff},
 {NULL,0}};

/* Parse a number with optional percent; returns 0..1 for percent when pct_scale */
static float num_or_pct(const char *s, const char *e, float pct_full) {
    float v = strtof(s, NULL);
    if (e > s && e[-1] == '%') return v * pct_full / 100.0f;
    return v;
}

static float hue2rgb(float p, float q, float t) {
    if (t < 0) t += 1;
    if (t > 1) t -= 1;
    if (t < 1.0f/6) return p + (q - p) * 6 * t;
    if (t < 0.5f) return q;
    if (t < 2.0f/3) return p + (q - p) * (2.0f/3 - t) * 6;
    return p;
}

static u8 clamp255(float v) { return v < 0 ? 0 : v > 255 ? 255 : (u8)(v + 0.5f); }

bool css_parse_color(const char *s, size_t n, u32 *out, u32 current) {
    const char *e = s + n;
    trim(&s, &e);
    n = (size_t)(e - s);
    if (!n) return false;
    if (*s == '#') {
        s++; n--;
        int d[8];
        for (size_t i = 0; i < n && i < 8; i++) { d[i] = hexd(s[i]); if (d[i] < 0) return false; }
        u32 r, g, b, a = 255;
        if (n == 3 || n == 4) { r = d[0]*17; g = d[1]*17; b = d[2]*17; if (n == 4) a = d[3]*17; }
        else if (n == 6 || n == 8) { r = d[0]*16+d[1]; g = d[2]*16+d[3]; b = d[4]*16+d[5]; if (n == 8) a = d[6]*16+d[7]; }
        else return false;
        *out = (a << 24) | (r << 16) | (g << 8) | b;
        return true;
    }
    if (w_ieqn(s, n, "transparent")) { *out = 0; return true; }
    if (w_ieqn(s, n, "currentcolor")) { *out = current; return true; }
    const char *paren = memchr(s, '(', n);
    if (paren) {
        size_t fn = (size_t)(paren - s);
        const char *a = paren + 1, *b = e;
        if (b > a && b[-1] == ')') b--;
        float v[4] = {0, 0, 0, 1}; int nv = 0;
        const char *p = a, *ts, *te;
        bool rgb = w_ieqn(s, fn, "rgb") || w_ieqn(s, fn, "rgba");
        bool hsl = w_ieqn(s, fn, "hsl") || w_ieqn(s, fn, "hsla");
        if (!rgb && !hsl) {
            /* color-mix(), oklch(), lab(), etc.: take the first color-ish token */
            if (w_ieqn(s, fn, "var")) return false;
            while (next_tok(&p, b, &ts, &te)) {
                u32 c;
                if (te - ts > 1 && !(ts[0] >= '0' && ts[0] <= '9') && css_parse_color(ts, (size_t)(te - ts), &c, current)) { *out = c; return true; }
            }
            if (w_ieqn(s, fn, "oklch") || w_ieqn(s, fn, "lch") || w_ieqn(s, fn, "oklab") || w_ieqn(s, fn, "lab")) {
                p = a;
                if (next_tok(&p, b, &ts, &te)) {
                    float L = num_or_pct(ts, te, w_ieqn(s, fn, "oklch") || w_ieqn(s, fn, "oklab") ? 1.0f : 100.0f);
                    if (w_ieqn(s, fn, "lch") || w_ieqn(s, fn, "lab")) L /= 100.0f;
                    u8 g = clamp255(L * 255);
                    *out = 0xFF000000u | ((u32)g << 16) | ((u32)g << 8) | g;
                    return true;
                }
            }
            return false;
        }
        while (nv < 4 && next_tok(&p, b, &ts, &te)) {
            if (*ts == ',' || *ts == '/') continue;
            if (hsl && nv == 0) v[nv++] = strtof(ts, NULL);
            else if (nv == 3) v[nv++] = num_or_pct(ts, te, 1.0f);
            else if (hsl) v[nv++] = num_or_pct(ts, te, 1.0f) / ((te > ts && te[-1] == '%') ? 1.0f : 100.0f);
            else v[nv++] = num_or_pct(ts, te, 255.0f);
        }
        if (nv < 3) return false;
        u8 r, g, bl;
        if (hsl) {
            float h = fmodf(v[0], 360.0f) / 360.0f; if (h < 0) h += 1;
            float sat = v[1], l = v[2];
            if (sat <= 0) { r = g = bl = clamp255(l * 255); }
            else {
                float q = l < 0.5f ? l * (1 + sat) : l + sat - l * sat, pp = 2 * l - q;
                r = clamp255(hue2rgb(pp, q, h + 1.0f/3) * 255);
                g = clamp255(hue2rgb(pp, q, h) * 255);
                bl = clamp255(hue2rgb(pp, q, h - 1.0f/3) * 255);
            }
        } else { r = clamp255(v[0]); g = clamp255(v[1]); bl = clamp255(v[2]); }
        *out = ((u32)clamp255(v[3] * 255) << 24) | ((u32)r << 16) | ((u32)g << 8) | bl;
        return true;
    }
    for (int i = 0; NAMED[i].name; i++)
        if (w_ieqn(s, n, NAMED[i].name)) { *out = 0xFF000000u | NAMED[i].rgb; return true; }
    return false;
}

/* --- calc() --- value = px + pct% ; returns false on unsupported input */
typedef struct { float px, pct; bool num_only; } cval_t;

static bool unit_px(cctx_t *c, float v, const char *u, size_t un, float *out) {
    if (un == 0) { *out = v; return true; }
    if (w_ieqn(u, un, "px")) { *out = v; return true; }
    if (w_ieqn(u, un, "em")) { *out = v * c->em; return true; }
    if (w_ieqn(u, un, "rem")) { *out = v * c->rem; return true; }
    if (w_ieqn(u, un, "pt")) { *out = v * 96.0f / 72.0f; return true; }
    if (w_ieqn(u, un, "pc")) { *out = v * 16.0f; return true; }
    if (w_ieqn(u, un, "in")) { *out = v * 96.0f; return true; }
    if (w_ieqn(u, un, "cm")) { *out = v * 96.0f / 2.54f; return true; }
    if (w_ieqn(u, un, "mm")) { *out = v * 96.0f / 25.4f; return true; }
    if (w_ieqn(u, un, "q")) { *out = v * 96.0f / 101.6f; return true; }
    if (w_ieqn(u, un, "vw") || w_ieqn(u, un, "svw") || w_ieqn(u, un, "lvw") || w_ieqn(u, un, "dvw")) { *out = v * c->pg->vw / 100.0f; return true; }
    if (w_ieqn(u, un, "vh") || w_ieqn(u, un, "svh") || w_ieqn(u, un, "lvh") || w_ieqn(u, un, "dvh")) { *out = v * c->pg->vh / 100.0f; return true; }
    if (w_ieqn(u, un, "vmin")) { *out = v * (c->pg->vw < c->pg->vh ? c->pg->vw : c->pg->vh) / 100.0f; return true; }
    if (w_ieqn(u, un, "vmax")) { *out = v * (c->pg->vw > c->pg->vh ? c->pg->vw : c->pg->vh) / 100.0f; return true; }
    if (w_ieqn(u, un, "ex")) { *out = v * c->em * 0.5f; return true; }
    if (w_ieqn(u, un, "ch")) { *out = v * c->em * 0.55f; return true; }
    if (w_ieqn(u, un, "lh")) { *out = v * c->em * 1.2f; return true; }
    if (w_ieqn(u, un, "cqw") || w_ieqn(u, un, "cqi")) { *out = v * c->pg->vw / 100.0f; return true; }
    return false;
}

static bool calc_expr(cctx_t *c, const char **pp, const char *e, cval_t *out);

static bool calc_term_atom(cctx_t *c, const char **pp, const char *e, cval_t *out) {
    const char *p = *pp;
    while (p < e && *p == ' ') p++;
    if (p < e && *p == '(') {
        p++;
        if (!calc_expr(c, &p, e, out)) return false;
        while (p < e && *p == ' ') p++;
        if (p < e && *p == ')') p++;
        *pp = p; return true;
    }
    /* nested functions */
    if (e - p > 5 && (w_ieqn(p, 5, "calc(") )) { p += 4; *pp = p; return calc_term_atom(c, pp, e, out); }
    if (e - p > 4 && (w_ieqn(p, 4, "min(") || w_ieqn(p, 4, "max("))) {
        bool is_min = (p[1] == 'i' || p[1] == 'I');
        p += 4;
        cval_t best = {0}; bool have = false;
        for (;;) {
            cval_t v;
            if (!calc_expr(c, &p, e, &v)) return false;
            /* compare using px part with % as 10px per % (heuristic when mixed) */
            float score = v.px + v.pct * (c->pg->vw / 100.0f);
            float bscore = best.px + best.pct * (c->pg->vw / 100.0f);
            if (!have || (is_min ? score < bscore : score > bscore)) { best = v; have = true; }
            while (p < e && *p == ' ') p++;
            if (p < e && *p == ',') { p++; continue; }
            if (p < e && *p == ')') p++;
            break;
        }
        *out = best; *pp = p; return have;
    }
    if (e - p > 6 && w_ieqn(p, 6, "clamp(")) {
        p += 6;
        cval_t v[3]; int i = 0;
        for (; i < 3; i++) {
            if (!calc_expr(c, &p, e, &v[i])) return false;
            while (p < e && *p == ' ') p++;
            if (p < e && *p == ',') { p++; continue; }
            if (p < e && *p == ')') { p++; }
            break;
        }
        if (i < 2) return false;
        float vw = c->pg->vw / 100.0f;
        float lo = v[0].px + v[0].pct * vw, mid = v[1].px + v[1].pct * vw, hi = v[2].px + v[2].pct * vw;
        *out = mid < lo ? v[0] : mid > hi ? v[2] : v[1];
        (void)lo;
        *pp = p; return true;
    }
    if (e - p > 4 && w_ieqn(p, 4, "var(")) return false;
    char *end;
    float v = strtof(p, &end);
    if (end == p) return false;
    p = end;
    const char *u = p;
    while (p < e && ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || *p == '%')) p++;
    size_t un = (size_t)(p - u);
    out->px = 0; out->pct = 0; out->num_only = false;
    if (un == 1 && *u == '%') out->pct = v;
    else if (un == 0) { out->px = v; out->num_only = true; }
    else if (!unit_px(c, v, u, un, &out->px)) return false;
    *pp = p;
    return true;
}

static bool calc_term(cctx_t *c, const char **pp, const char *e, cval_t *out) {
    if (!calc_term_atom(c, pp, e, out)) return false;
    for (;;) {
        const char *p = *pp;
        while (p < e && *p == ' ') p++;
        if (p < e && (*p == '*' || *p == '/')) {
            char op = *p++;
            cval_t r;
            if (!calc_term_atom(c, &p, e, &r)) return false;
            if (op == '*') {
                if (r.num_only) { out->px *= r.px; out->pct *= r.px; }
                else if (out->num_only) { float k = out->px; *out = r; out->px *= k; out->pct *= k; }
                else return false;
            } else {
                if (!r.num_only || r.px == 0) return false;
                out->px /= r.px; out->pct /= r.px;
            }
            *pp = p;
        } else return true;
    }
}

static bool calc_expr(cctx_t *c, const char **pp, const char *e, cval_t *out) {
    if (!calc_term(c, pp, e, out)) return false;
    for (;;) {
        const char *p = *pp;
        while (p < e && *p == ' ') p++;
        if (p < e && (*p == '+' || *p == '-') && p + 1 < e && p[1] == ' ') {
            char op = *p++;
            cval_t r;
            if (!calc_term(c, &p, e, &r)) return false;
            if (op == '+') { out->px += r.px; out->pct += r.pct; }
            else { out->px -= r.px; out->pct -= r.pct; }
            out->num_only = out->num_only && r.num_only;
            *pp = p;
        } else return true;
    }
}

/* Parse a length/percentage token. allow_neg: margins & co. */
static bool parse_len(cctx_t *c, const char *s, const char *e, len_t *out) {
    trim(&s, &e);
    if (s >= e) return false;
    size_t n = (size_t)(e - s);
    if (w_ieqn(s, n, "auto") || w_ieqn(s, n, "fit-content") || w_ieqn(s, n, "max-content") ||
        w_ieqn(s, n, "min-content") || w_ieqn(s, n, "initial") || w_ieqn(s, n, "unset") ||
        w_ieqn(s, n, "revert") || w_ieqn(s, n, "stretch") || w_ieqn(s, n, "-webkit-fill-available")) {
        out->u = L_AUTO; out->v = 0; return true;
    }
    if (w_ieqn(s, n, "none")) { out->u = L_NONE; out->v = 0; return true; }
    if (n > 5 && (w_istr(s, n, "calc(") == s || w_istr(s, n, "min(") == s ||
                  w_istr(s, n, "max(") == s || w_istr(s, n, "clamp(") == s)) {
        const char *p = s;
        cval_t v;
        if (!calc_term_atom(c, &p, e, &v)) return false;
        if (v.pct != 0 && v.px != 0) { out->u = L_PCT; out->v = v.pct; out->pc_px = v.px; return true; }
        if (v.pct != 0) { out->u = L_PCT; out->v = v.pct; out->pc_px = 0; return true; }
        out->u = L_PX; out->v = v.px; return true;
    }
    char *end;
    float v = strtof(s, &end);
    if (end == s) return false;
    const char *u = end;
    size_t un = (size_t)(e - u);
    if (un == 1 && *u == '%') { out->u = L_PCT; out->v = v; out->pc_px = 0; return true; }
    if (un == 2 && w_ieqn(u, 2, "fr")) { out->u = L_FR; out->v = v; return true; }
    float px;
    if (!unit_px(c, v, u, un, &px)) return false;
    out->u = L_PX; out->v = px;
    return true;
}

static bool parse_px(cctx_t *c, const char *s, const char *e, float *out) {
    len_t l;
    if (!parse_len(c, s, e, &l) || l.u != L_PX) return false;
    *out = l.v; return true;
}

/* =================================================================== */
/*  var() substitution                                                  */
/* =================================================================== */

/* Custom property blob: sequence of "name\0value\0" pairs ending with "\0". */
static const char *custom_lookup(const char *blob, const char *name, size_t nl) {
    if (!blob) return NULL;
    const char *p = blob;
    while (*p) {
        size_t l = strlen(p);
        const char *v = p + l + 1;
        if (l == nl && memcmp(p, name, nl) == 0) return v;
        p = v + strlen(v) + 1;
    }
    return NULL;
}

/* Expand var() references into a malloc'd string, or NULL if no var() used. */
static char *expand_vars(const char *blob, const char *s, size_t n, int depth) {
    const char *e = s + n;
    if (!w_istr(s, n, "var(")) return NULL;
    sbuf_t out = {0};
    const char *p = s;
    while (p < e) {
        const char *v = w_istr(p, (size_t)(e - p), "var(");
        if (!v) { sb_put(&out, p, (size_t)(e - p)); break; }
        sb_put(&out, p, (size_t)(v - p));
        const char *a = v + 4;
        const char *close = find_top(a, e, ")");
        const char *comma = find_top(a, close, ",");
        const char *ns = a, *ne = comma; trim(&ns, &ne);
        const char *val = custom_lookup(blob, ns, (size_t)(ne - ns));
        const char *rep; size_t rl;
        if (val) { rep = val; rl = strlen(val); }
        else if (comma < close) { rep = comma + 1; rl = (size_t)(close - comma - 1); }
        else { rep = ""; rl = 0; }
        if (depth < 8) {
            char *sub = expand_vars(blob, rep, rl, depth + 1);
            if (sub) { sb_puts(&out, sub); free(sub); }
            else sb_put(&out, rep, rl);
        }
        p = close < e ? close + 1 : e;
    }
    if (!out.s) out.s = w_strdup("");
    return out.s;
}

/* =================================================================== */
/*  Property application                                                */
/* =================================================================== */

static u8 parse_border_style(const char *s, const char *e) {
    if (tok_is(s, e, "none") || tok_is(s, e, "hidden")) return BS_NONE;
    if (tok_is(s, e, "solid")) return BS_SOLID;
    if (tok_is(s, e, "dashed")) return BS_DASHED;
    if (tok_is(s, e, "dotted")) return BS_DOTTED;
    if (tok_is(s, e, "double")) return BS_DOUBLE;
    if (tok_is(s, e, "groove") || tok_is(s, e, "ridge") || tok_is(s, e, "inset") || tok_is(s, e, "outset")) return BS_OTHER;
    return 255;
}

static bool parse_border_width(cctx_t *c, const char *s, const char *e, float *w) {
    if (tok_is(s, e, "thin")) { *w = 1; return true; }
    if (tok_is(s, e, "medium")) { *w = 3; return true; }
    if (tok_is(s, e, "thick")) { *w = 5; return true; }
    return parse_px(c, s, e, w);
}

/* border shorthand on the sides in mask (bit0 top..bit3 left) */
static void apply_border(cctx_t *c, const char *s, const char *e, int mask) {
    float w = 3; u8 bs = BS_NONE; u32 col = CUR_COLOR;
    const char *p = s, *ts, *te;
    while (next_tok(&p, e, &ts, &te)) {
        u8 b = parse_border_style(ts, te);
        float ww; u32 cc;
        if (b != 255) bs = b;
        else if (parse_border_width(c, ts, te, &ww)) w = ww;
        else if (css_parse_color(ts, (size_t)(te - ts), &cc, CUR_COLOR)) col = cc;
    }
    for (int i = 0; i < 4; i++) if (mask & (1 << i)) {
        c->st->border_w[i] = w; c->st->border_style[i] = bs; c->st->border_color[i] = col;
    }
}

/* 1-4 value box shorthands */
static int split4(const char *s, const char *e, const char *ts[4], const char *te[4]) {
    const char *p = s; int n = 0;
    const char *a, *b;
    while (n < 4 && next_tok(&p, e, &a, &b)) {
        if (*a == '/' || *a == ',') break;
        ts[n] = a; te[n] = b; n++;
    }
    return n;
}
static void expand4(int n, int idx[4]) {
    static const int map[5][4] = {{0,0,0,0},{0,0,0,0},{0,1,0,1},{0,1,2,1},{0,1,2,3}};
    for (int i = 0; i < 4; i++) idx[i] = map[n][i];
}

static void apply_len4(cctx_t *c, const char *s, const char *e, len_t out[4]) {
    const char *ts[4], *te[4];
    int n = split4(s, e, ts, te);
    if (!n) return;
    len_t v[4];
    for (int i = 0; i < n; i++) if (!parse_len(c, ts[i], te[i], &v[i])) return;
    int idx[4]; expand4(n, idx);
    for (int i = 0; i < 4; i++) out[i] = v[idx[i]];
}

static u8 parse_display(const char *s, const char *e) {
    size_t n = (size_t)(e - s);
    /* two-value syntax: "inline flex", "block flow-root" ... */
    bool inl = w_istr(s, n, "inline") != NULL && !w_istr(s, n, "inline-");
    if (w_ieqn(s, n, "none")) return D_NONE;
    if (w_ieqn(s, n, "block") || w_ieqn(s, n, "block flow")) return D_BLOCK;
    if (w_ieqn(s, n, "inline") || w_ieqn(s, n, "inline flow")) return D_INLINE;
    if (w_ieqn(s, n, "inline-block") || w_ieqn(s, n, "inline flow-root")) return D_INLINE_BLOCK;
    if (w_ieqn(s, n, "list-item") || w_istr(s, n, "list-item")) return D_LIST_ITEM;
    if (w_ieqn(s, n, "flex") || w_ieqn(s, n, "-webkit-box") || w_ieqn(s, n, "-webkit-flex") || w_ieqn(s, n, "-ms-flexbox") || w_ieqn(s, n, "block flex")) return D_FLEX;
    if (w_ieqn(s, n, "inline-flex") || w_ieqn(s, n, "-webkit-inline-box") || w_ieqn(s, n, "-webkit-inline-flex") || (inl && w_istr(s, n, "flex"))) return D_INLINE_FLEX;
    if (w_ieqn(s, n, "grid") || w_ieqn(s, n, "-ms-grid") || w_ieqn(s, n, "block grid")) return D_GRID;
    if (w_ieqn(s, n, "inline-grid") || (inl && w_istr(s, n, "grid"))) return D_INLINE_GRID;
    if (w_ieqn(s, n, "table")) return D_TABLE;
    if (w_ieqn(s, n, "inline-table")) return D_INLINE_TABLE;
    if (w_ieqn(s, n, "table-row")) return D_TABLE_ROW;
    if (w_ieqn(s, n, "table-cell")) return D_TABLE_CELL;
    if (w_ieqn(s, n, "table-row-group") || w_ieqn(s, n, "table-header-group") || w_ieqn(s, n, "table-footer-group")) return D_TABLE_ROW_GROUP;
    if (w_ieqn(s, n, "table-caption")) return D_TABLE_CAPTION;
    if (w_ieqn(s, n, "table-column") || w_ieqn(s, n, "table-column-group")) return D_TABLE_COLUMN;
    if (w_ieqn(s, n, "contents")) return D_CONTENTS;
    if (w_ieqn(s, n, "flow-root")) return D_FLOW_ROOT;
    if (w_ieqn(s, n, "run-in") || w_ieqn(s, n, "ruby") || w_ieqn(s, n, "ruby-text")) return D_INLINE;
    return 255;
}

static u8 parse_align(const char *s, const char *e) {
    const char *p = s, *ts, *te; u8 r = 255;
    while (next_tok(&p, e, &ts, &te)) {
        if (tok_is(ts, te, "safe") || tok_is(ts, te, "unsafe") || tok_is(ts, te, "first") || tok_is(ts, te, "last")) continue;
        if (tok_is(ts, te, "flex-start") || tok_is(ts, te, "start") || tok_is(ts, te, "self-start") || tok_is(ts, te, "left") || tok_is(ts, te, "top")) r = JC_START;
        else if (tok_is(ts, te, "flex-end") || tok_is(ts, te, "end") || tok_is(ts, te, "self-end") || tok_is(ts, te, "right") || tok_is(ts, te, "bottom")) r = JC_END;
        else if (tok_is(ts, te, "center")) r = JC_CENTER;
        else if (tok_is(ts, te, "space-between")) r = JC_BETWEEN;
        else if (tok_is(ts, te, "space-around")) r = JC_AROUND;
        else if (tok_is(ts, te, "space-evenly")) r = JC_EVENLY;
        else if (tok_is(ts, te, "stretch") || tok_is(ts, te, "normal")) r = JC_STRETCH;
        else if (tok_is(ts, te, "baseline")) r = JC_BASELINE;
        else if (tok_is(ts, te, "auto")) r = 254;
        break;
    }
    return r;
}

static float font_size_keyword(const char *s, const char *e, float parent) {
    if (tok_is(s, e, "xx-small")) return 9;
    if (tok_is(s, e, "x-small")) return 10;
    if (tok_is(s, e, "small")) return 13;
    if (tok_is(s, e, "medium")) return 16;
    if (tok_is(s, e, "large")) return 18;
    if (tok_is(s, e, "x-large")) return 24;
    if (tok_is(s, e, "xx-large")) return 32;
    if (tok_is(s, e, "xxx-large")) return 48;
    if (tok_is(s, e, "smaller")) return parent / 1.2f;
    if (tok_is(s, e, "larger")) return parent * 1.2f;
    return -1;
}

static bool apply_font_size(cctx_t *c, const char *s, const char *e) {
    float pf = c->parent ? c->parent->font_size : 16;
    float k = font_size_keyword(s, e, pf);
    if (k > 0) { c->st->font_size = k; c->em = k; return true; }
    cctx_t pc = *c; pc.em = pf;            /* em in font-size = parent's size */
    len_t l;
    if (!parse_len(&pc, s, e, &l)) return false;
    float v = l.u == L_PCT ? pf * l.v / 100.0f + l.pc_px : l.u == L_PX ? l.v : -1;
    if (v <= 0) return false;
    c->st->font_size = v; c->em = v;
    return true;
}

static void apply_font_family(cctx_t *c, const char *s, const char *e) {
    const char *p = s;
    while (p < e) {
        const char *comma = find_top(p, e, ",");
        const char *a = p, *b = comma; trim(&a, &b);
        if (a < b && (*a == '"' || *a == '\'')) { a++; if (b > a) b--; }
        size_t n = (size_t)(b - a);
        if (w_istr(a, n, "mono") || w_istr(a, n, "courier") || w_istr(a, n, "consol") ||
            w_istr(a, n, "menlo") || w_istr(a, n, "monaco") || w_istr(a, n, "code") ||
            w_ieqn(a, n, "ui-monospace") || w_istr(a, n, "lucida console")) { c->st->font_family = FF_MONO; return; }
        if (w_ieqn(a, n, "serif") || w_istr(a, n, "times") || w_istr(a, n, "georgia") ||
            w_istr(a, n, "garamond") || w_ieqn(a, n, "ui-serif")) { c->st->font_family = FF_SERIF; return; }
        if (w_istr(a, n, "sans") || w_istr(a, n, "arial") || w_istr(a, n, "helvetica") ||
            w_istr(a, n, "system") || w_istr(a, n, "segoe") || w_istr(a, n, "roboto") ||
            w_istr(a, n, "verdana") || w_istr(a, n, "inter") || w_istr(a, n, "apple")) { c->st->font_family = FF_SANS; return; }
        p = comma < e ? comma + 1 : e;
    }
    c->st->font_family = FF_SANS;
}

static bool apply_font_weight(cctx_t *c, const char *s, const char *e) {
    u16 pw = c->parent ? c->parent->font_weight : 400;
    if (tok_is(s, e, "normal")) c->st->font_weight = 400;
    else if (tok_is(s, e, "bold")) c->st->font_weight = 700;
    else if (tok_is(s, e, "bolder")) c->st->font_weight = pw >= 600 ? 900 : 700;
    else if (tok_is(s, e, "lighter")) c->st->font_weight = pw >= 600 ? 400 : 100;
    else if (*s >= '0' && *s <= '9') c->st->font_weight = (u16)strtol(s, NULL, 10);
    else return false;
    return true;
}

static bool apply_line_height(cctx_t *c, const char *s, const char *e) {
    if (tok_is(s, e, "normal")) { c->st->line_height = -1; c->st->lh_factor = 0; return true; }
    char *end; float v = strtof(s, &end);
    if (end != s && end == e) { c->st->lh_factor = v; c->st->line_height = v * c->st->font_size; return true; }
    len_t l;
    if (!parse_len(c, s, e, &l)) return false;
    if (l.u == L_PCT) { c->st->line_height = l.v * c->st->font_size / 100.0f; c->st->lh_factor = 0; return true; }
    if (l.u == L_PX) { c->st->line_height = l.v; c->st->lh_factor = 0; return true; }
    return false;
}

static void apply_font(cctx_t *c, const char *s, const char *e) {
    /* [style] [variant] [weight] size[/lh] family */
    const char *p = s, *ts, *te;
    if (tok_is(s, e, "caption") || tok_is(s, e, "menu") || tok_is(s, e, "message-box") ||
        tok_is(s, e, "status-bar") || tok_is(s, e, "icon") || tok_is(s, e, "small-caption")) {
        c->st->font_size = 13; c->em = 13; c->st->font_family = FF_SANS; return;
    }
    c->st->italic = 0; c->st->font_weight = 400; c->st->line_height = -1; c->st->lh_factor = 0;
    while (next_tok(&p, e, &ts, &te)) {
        if (tok_is(ts, te, "italic") || tok_is(ts, te, "oblique")) { c->st->italic = 1; continue; }
        if (tok_is(ts, te, "normal") || tok_is(ts, te, "small-caps")) continue;
        if (apply_font_weight(c, ts, te) && !(te > ts && (te[-1] == 'x' || te[-1] == '%' || te[-1] == 'm'))) continue;
        if (apply_font_size(c, ts, te)) {
            const char *save = p;
            if (next_tok(&p, e, &ts, &te) && *ts == '/') {
                if (next_tok(&p, e, &ts, &te)) apply_line_height(c, ts, te);
            } else p = save;
            apply_font_family(c, p, e);
            return;
        }
    }
}

static void apply_background(cctx_t *c, const char *s, const char *e) {
    style_t *st = c->st;
    /* use the last layer (it carries the color) for color; first url wins */
    st->bg_color = 0; st->bg_image = NULL; st->bg_grad = 0;
    st->bg_repeat = BGR_REPEAT; st->bg_size_mode = BGS_AUTO;
    st->bg_pos[0].u = L_PCT; st->bg_pos[0].v = 0; st->bg_pos[1].u = L_PCT; st->bg_pos[1].v = 0;
    const char *p = s, *ts, *te;
    int npos = 0; bool after_slash = false;
    while (next_tok(&p, e, &ts, &te)) {
        size_t n = (size_t)(te - ts);
        u32 col;
        if (*ts == ',') { npos = 0; continue; }
        if (*ts == '/') { after_slash = true; continue; }
        if (w_istr(ts, n, "url(") == ts) {
            if (!st->bg_image) {
                const char *a = ts + 4, *b = te - 1; trim(&a, &b);
                if (a < b && (*a == '"' || *a == '\'')) { a++; b--; }
                char ref[1024], url[1024];
                snprintf(ref, sizeof ref, "%.*s", (int)(b - a), a);
                url_resolve(c->pg->url, ref, url, sizeof url);
                st->bg_image = (char *)pg_alloc(c->pg, strlen(url) + 1);
                if (st->bg_image) strcpy(st->bg_image, url);
            }
            continue;
        }
        if (w_istr(ts, n, "linear-gradient(") == ts || w_istr(ts, n, "-webkit-linear-gradient(") == ts ||
            w_istr(ts, n, "radial-gradient(") == ts || w_istr(ts, n, "repeating-linear-gradient(") == ts) {
            const char *a = memchr(ts, '(', n) + 1, *b = te - 1;
            const char *q = a, *xs, *xe;
            u32 cols[8]; int nc = 0; u8 dir = 0;
            while (next_tok(&q, b, &xs, &xe)) {
                if (tok_is(xs, xe, "right") || tok_is(xs, xe, "left") || (xe > xs && (xe[-1] == 'g') && (strtof(xs, NULL) == 90 || strtof(xs, NULL) == 270))) dir = 1;
                u32 cc;
                if (nc < 8 && css_parse_color(xs, (size_t)(xe - xs), &cc, st->color)) cols[nc++] = cc;
            }
            if (nc >= 1) {
                st->bg_grad = 1; st->grad_dir = dir;
                st->grad_from = cols[0]; st->grad_to = cols[nc - 1];
            }
            continue;
        }
        if (tok_is(ts, te, "no-repeat")) { st->bg_repeat = BGR_NO_REPEAT; continue; }
        if (tok_is(ts, te, "repeat-x")) { st->bg_repeat = BGR_REPEAT_X; continue; }
        if (tok_is(ts, te, "repeat-y")) { st->bg_repeat = BGR_REPEAT_Y; continue; }
        if (tok_is(ts, te, "repeat") || tok_is(ts, te, "space") || tok_is(ts, te, "round")) continue;
        if (after_slash) {
            if (tok_is(ts, te, "cover")) st->bg_size_mode = BGS_COVER;
            else if (tok_is(ts, te, "contain")) st->bg_size_mode = BGS_CONTAIN;
            continue;
        }
        if (tok_is(ts, te, "center")) { if (npos < 2) { st->bg_pos[npos].u = L_PCT; st->bg_pos[npos].v = 50; npos++; } continue; }
        if (tok_is(ts, te, "left") || tok_is(ts, te, "top")) { if (npos < 2) { st->bg_pos[npos].u = L_PCT; st->bg_pos[npos].v = 0; npos++; } continue; }
        if (tok_is(ts, te, "right") || tok_is(ts, te, "bottom")) { if (npos < 2) { st->bg_pos[npos].u = L_PCT; st->bg_pos[npos].v = 100; npos++; } continue; }
        if (css_parse_color(ts, n, &col, st->color)) { st->bg_color = col; continue; }
        len_t l;
        if (npos < 2 && parse_len(c, ts, te, &l) && (l.u == L_PX || l.u == L_PCT)) { st->bg_pos[npos++] = l; continue; }
    }
    if (npos == 1) { st->bg_pos[1].u = L_PCT; st->bg_pos[1].v = 50; }
}

static void parse_grid_template(cctx_t *c, const char *s, const char *e, grid_tracks_t *g) {
    memset(g, 0, sizeof *g);
    const char *p = s, *ts, *te;
    while (next_tok(&p, e, &ts, &te)) {
        size_t n = (size_t)(te - ts);
        if (*ts == '[') continue;                         /* line names */
        if (w_istr(ts, n, "repeat(") == ts) {
            const char *a = ts + 7, *b = te - 1;
            const char *comma = find_top(a, b, ",");
            const char *cs = a, *ce = comma; trim(&cs, &ce);
            const char *body = comma < b ? comma + 1 : b;
            if (tok_is(cs, ce, "auto-fill") || tok_is(cs, ce, "auto-fit")) {
                g->auto_fill = tok_is(cs, ce, "auto-fit") ? 2 : 1;
                const char *mm = w_istr(body, (size_t)(b - body), "minmax(");
                len_t l;
                const char *xs = body, *xe = b;
                if (mm) { xs = mm + 7; xe = find_top(xs, b, ","); }
                trim(&xs, &xe);
                if (parse_len(c, xs, xe, &l) && l.u == L_PX) g->fill_min = l.v;
                else if (l.u == L_PCT) g->fill_min = l.v * c->pg->vw / 100.0f;
                else g->fill_min = 200;
                if (g->fill_min < 1) g->fill_min = 1;
                continue;
            }
            int count = (int)strtol(cs, NULL, 10);
            if (count < 1) count = 1;
            for (int k = 0; k < count; k++) {
                const char *q = body, *xs, *xe;
                while (next_tok(&q, b, &xs, &xe) && g->n < GRID_MAX_TRACKS) {
                    if (*xs == '[') continue;
                    grid_tracks_t tmp = {0};
                    parse_grid_template(c, xs, xe, &tmp);
                    if (tmp.n) { g->t[g->n] = tmp.t[0]; g->tmin[g->n] = tmp.tmin[0]; g->n++; }
                }
            }
            continue;
        }
        if (g->n >= GRID_MAX_TRACKS) break;
        len_t l = { 0, 0, L_AUTO };
        float mn = 0;
        if (w_istr(ts, n, "minmax(") == ts) {
            const char *a = ts + 7, *b = te - 1;
            const char *comma = find_top(a, b, ",");
            len_t lo, hi;
            if (parse_len(c, a, comma, &lo) && lo.u == L_PX) mn = lo.v;
            if (comma < b && parse_len(c, comma + 1, b, &hi)) l = hi;
            if (l.u == L_AUTO && mn > 0) { l.u = L_PX; l.v = mn; }
        } else if (w_istr(ts, n, "fit-content(") == ts) {
            l.u = L_AUTO;
        } else {
            parse_len(c, ts, te, &l);
        }
        g->t[g->n] = l; g->tmin[g->n] = mn; g->n++;
    }
}

/* "a a" "b c" -> "a a|b c" (arena), or NULL for none. */
static const char *parse_areas(page_t *pg, const char *s, const char *e) {
    if (tok_is(s, e, "none")) return NULL;
    char *out = (char *)pg_alloc(pg, (size_t)(e - s) + 1);
    if (!out) return NULL;
    size_t o = 0;
    const char *p = s;
    while (p < e) {
        if (*p != '"' && *p != '\'') { p++; continue; }
        const char *q = skip_string(p, e);
        const char *a = p + 1, *b = q - 1;
        if (o) out[o++] = '|';
        bool sp = true;
        for (; a < b; a++) {
            if ((u8)*a <= ' ') { if (!sp) { out[o++] = ' '; sp = true; } continue; }
            out[o++] = *a; sp = false;
        }
        if (o && out[o - 1] == ' ') o--;
        p = q;
    }
    out[o] = 0;
    return o ? out : NULL;
}

static void parse_grid_line(const char *s, const char *e, i32 *start, i32 *span) {
    /* "span 2" | "1 / 3" | "1 / -1" | "2" | "auto" */
    const char *slash = find_top(s, e, "/");
    const char *a = s, *b = slash; trim(&a, &b);
    i32 st = 0, sp = 1;
    if (w_istr(a, (size_t)(b - a), "span") == a) sp = (i32)strtol(a + 4, NULL, 10);
    else if (a < b && (*a == '-' || (*a >= '0' && *a <= '9'))) st = (i32)strtol(a, NULL, 10);
    if (slash < e) {
        const char *c2 = slash + 1, *d = e; trim(&c2, &d);
        if (w_istr(c2, (size_t)(d - c2), "span") == c2) sp = (i32)strtol(c2 + 4, NULL, 10);
        else if (c2 < d) {
            i32 en = (i32)strtol(c2, NULL, 10);
            if (en == -1) sp = 999;              /* to the end */
            else if (st > 0 && en > st) sp = en - st;
        }
    }
    if (sp < 1) sp = 1;
    *start = st; *span = sp;
}

static void apply_box_shadow(cctx_t *c, const char *s, const char *e) {
    style_t *st = c->st;
    st->shadow_color = 0;
    if (tok_is(s, e, "none")) return;
    const char *first_end = find_top(s, e, ",");
    const char *p = s, *ts, *te;
    float nums[4]; int nn = 0; u32 col = 0x40000000u; bool inset = false;
    while (next_tok(&p, first_end, &ts, &te)) {
        u32 cc; float v;
        if (tok_is(ts, te, "inset")) { inset = true; continue; }
        if (css_parse_color(ts, (size_t)(te - ts), &cc, st->color)) { col = cc; continue; }
        if (nn < 4 && parse_px(c, ts, te, &v)) nums[nn++] = v;
    }
    if (inset || nn < 2) return;
    st->shadow_x = nums[0]; st->shadow_y = nums[1];
    st->shadow_blur = nn > 2 ? nums[2] : 0;
    st->shadow_color = col;
}

static void apply_flex(cctx_t *c, const char *s, const char *e) {
    style_t *st = c->st;
    if (tok_is(s, e, "none")) { st->grow = 0; st->shrink = 0; st->basis.u = L_AUTO; return; }
    if (tok_is(s, e, "auto")) { st->grow = 1; st->shrink = 1; st->basis.u = L_AUTO; return; }
    if (tok_is(s, e, "initial")) { st->grow = 0; st->shrink = 1; st->basis.u = L_AUTO; return; }
    const char *p = s, *ts, *te;
    int nnum = 0; bool basis_set = false;
    while (next_tok(&p, e, &ts, &te)) {
        char *end; float v = strtof(ts, &end);
        if (end == te && end != ts) {
            if (nnum == 0) st->grow = v; else if (nnum == 1) st->shrink = v;
            nnum++;
            continue;
        }
        len_t l;
        if (parse_len(c, ts, te, &l)) { st->basis = l; basis_set = true; }
        if (tok_is(ts, te, "content")) { st->basis.u = L_AUTO; basis_set = true; }
    }
    if (nnum == 1) st->shrink = 1;
    if (!basis_set && nnum > 0) { st->basis.u = L_PX; st->basis.v = 0; }
}

/* Apply one declaration; returns false if unhandled/invalid. */
static void apply_prop(cctx_t *c, const char *name, size_t nl, const char *s, const char *e) {
    style_t *st = c->st;
    const style_t *ps = c->parent;
    trim(&s, &e);
    if (s >= e) return;
    size_t n = (size_t)(e - s);
    /* CSS-wide keywords */
    if (w_ieqn(s, n, "inherit")) {
        if (!ps) return;
        if (w_ieqn(name, nl, "color")) st->color = ps->color;
        else if (w_ieqn(name, nl, "background-color")) st->bg_color = ps->bg_color;
        else if (w_ieqn(name, nl, "font-size")) { st->font_size = ps->font_size; c->em = ps->font_size; }
        else if (w_ieqn(name, nl, "height")) st->height = ps->height;
        else if (w_ieqn(name, nl, "width")) st->width = ps->width;
        else if (w_ieqn(name, nl, "display")) st->display = ps->display;
        else if (w_ieqn(name, nl, "text-decoration")) st->deco = ps->deco;
        else if (w_ieqn(name, nl, "vertical-align")) st->vertical_align = ps->vertical_align;
        else if (w_ieqn(name, nl, "border-color")) for (int i = 0; i < 4; i++) st->border_color[i] = ps->border_color[i];
        else if (w_ieqn(name, nl, "overflow")) st->overflow = ps->overflow;
        return;
    }
    if (w_ieqn(s, n, "initial") || w_ieqn(s, n, "unset") || w_ieqn(s, n, "revert") || w_ieqn(s, n, "revert-layer")) {
        if (w_ieqn(name, nl, "display")) st->display = D_INLINE;
        else if (w_ieqn(name, nl, "color") && ps) st->color = ps->color;
        else if (w_ieqn(name, nl, "background-color") || w_ieqn(name, nl, "background")) { st->bg_color = 0; st->bg_image = NULL; st->bg_grad = 0; }
        else if (w_ieqn(name, nl, "width")) st->width.u = L_AUTO;
        else if (w_ieqn(name, nl, "height")) st->height.u = L_AUTO;
        else if (w_ieqn(name, nl, "max-width")) st->max_w.u = L_NONE;
        else if (w_ieqn(name, nl, "margin")) for (int i = 0; i < 4; i++) { st->margin[i].u = L_PX; st->margin[i].v = 0; }
        else if (w_ieqn(name, nl, "padding")) for (int i = 0; i < 4; i++) { st->padding[i].u = L_PX; st->padding[i].v = 0; }
        else if (w_ieqn(name, nl, "border")) for (int i = 0; i < 4; i++) st->border_style[i] = BS_NONE;
        else if (w_ieqn(name, nl, "position")) st->position = POS_STATIC;
        else if (w_ieqn(name, nl, "float")) st->float_ = FL_NONE;
        return;
    }
    u32 col; len_t l; float f;
#define IS(x) w_ieqn(name, nl, x)
    switch (name[0]) {
    case '-':
        if (IS("-webkit-mask") || IS("-webkit-mask-image") || IS("-webkit-mask-box-image"))
            st->masked = (w_istr(s, n, "url(") || w_istr(s, n, "gradient(")) ? 1 : 0;
        break;
    case 'a':
        if (IS("align-items")) { u8 a = parse_align(s, e); if (a < 254) st->align_items = a; }
        else if (IS("align-self")) { u8 a = parse_align(s, e); st->align_self = a == 254 ? 255 : a; }
        else if (IS("align-content")) { u8 a = parse_align(s, e); if (a < 254) st->align_content = a; }
        else if (IS("aspect-ratio")) {
            const char *slash = find_top(s, e, "/");
            float a = strtof(s, NULL), b = slash < e ? strtof(slash + 1, NULL) : 1;
            if (tok_is(s, e, "auto")) a = 0;
            st->aspect = (a > 0 && b > 0) ? a / b : 0;
        }
        break;
    case 'b':
        if (IS("background")) apply_background(c, s, e);
        else if (IS("background-color")) { if (css_parse_color(s, n, &col, st->color)) st->bg_color = col; }
        else if (IS("background-image")) {
            u32 saved = st->bg_color; u8 rep = st->bg_repeat, sm = st->bg_size_mode; len_t p0 = st->bg_pos[0], p1 = st->bg_pos[1];
            apply_background(c, s, e);
            st->bg_color = saved; st->bg_repeat = rep; st->bg_size_mode = sm; st->bg_pos[0] = p0; st->bg_pos[1] = p1;
        }
        else if (IS("background-repeat")) {
            if (w_istr(s, n, "no-repeat")) st->bg_repeat = BGR_NO_REPEAT;
            else if (w_istr(s, n, "repeat-x")) st->bg_repeat = BGR_REPEAT_X;
            else if (w_istr(s, n, "repeat-y")) st->bg_repeat = BGR_REPEAT_Y;
            else st->bg_repeat = BGR_REPEAT;
        }
        else if (IS("background-size")) {
            if (w_istr(s, n, "cover")) st->bg_size_mode = BGS_COVER;
            else if (w_istr(s, n, "contain")) st->bg_size_mode = BGS_CONTAIN;
            else {
                const char *p = s, *ts, *te; int k = 0;
                st->bg_size_mode = BGS_LEN;
                st->bg_size[0].u = L_AUTO; st->bg_size[1].u = L_AUTO;
                while (k < 2 && next_tok(&p, e, &ts, &te)) { if (*ts == ',') break; parse_len(c, ts, te, &st->bg_size[k]); k++; }
            }
        }
        else if (IS("background-position")) {
            const char *p = s, *ts, *te; int k = 0;
            while (k < 2 && next_tok(&p, e, &ts, &te)) {
                if (*ts == ',') break;
                if (tok_is(ts, te, "center")) { st->bg_pos[k].u = L_PCT; st->bg_pos[k].v = 50; }
                else if (tok_is(ts, te, "left") || tok_is(ts, te, "top")) { st->bg_pos[k].u = L_PCT; st->bg_pos[k].v = 0; }
                else if (tok_is(ts, te, "right") || tok_is(ts, te, "bottom")) { st->bg_pos[k].u = L_PCT; st->bg_pos[k].v = 100; }
                else parse_len(c, ts, te, &st->bg_pos[k]);
                k++;
            }
            if (k == 1) { st->bg_pos[1].u = L_PCT; st->bg_pos[1].v = 50; }
        }
        else if (IS("border")) apply_border(c, s, e, 15);
        else if (IS("border-top")) apply_border(c, s, e, 1);
        else if (IS("border-right")) apply_border(c, s, e, 2);
        else if (IS("border-bottom")) apply_border(c, s, e, 4);
        else if (IS("border-left")) apply_border(c, s, e, 8);
        else if (IS("border-block")) apply_border(c, s, e, 5);
        else if (IS("border-inline")) apply_border(c, s, e, 10);
        else if (IS("border-block-start")) apply_border(c, s, e, 1);
        else if (IS("border-block-end")) apply_border(c, s, e, 4);
        else if (IS("border-inline-start")) apply_border(c, s, e, 8);
        else if (IS("border-inline-end")) apply_border(c, s, e, 2);
        else if (IS("border-width")) {
            const char *ts[4], *te[4]; int k = split4(s, e, ts, te), idx[4]; float v[4];
            for (int i = 0; i < k; i++) if (!parse_border_width(c, ts[i], te[i], &v[i])) return;
            if (!k) return;
            expand4(k, idx); for (int i = 0; i < 4; i++) st->border_w[i] = v[idx[i]];
        }
        else if (IS("border-style")) {
            const char *ts[4], *te[4]; int k = split4(s, e, ts, te), idx[4]; u8 v[4];
            for (int i = 0; i < k; i++) { v[i] = parse_border_style(ts[i], te[i]); if (v[i] == 255) return; }
            if (!k) return;
            expand4(k, idx); for (int i = 0; i < 4; i++) st->border_style[i] = v[idx[i]];
        }
        else if (IS("border-color")) {
            const char *ts[4], *te[4]; int k = split4(s, e, ts, te), idx[4]; u32 v[4];
            for (int i = 0; i < k; i++) if (!css_parse_color(ts[i], (size_t)(te[i] - ts[i]), &v[i], CUR_COLOR)) return;
            if (!k) return;
            expand4(k, idx); for (int i = 0; i < 4; i++) st->border_color[i] = v[idx[i]];
        }
        else if (nl > 7 && w_ieqn(name, 7, "border-") ) {
            /* border-<side>-<width|style|color>, border-radius family */
            static const char *sides[4] = {"top", "right", "bottom", "left"};
            static const char *lsides[4] = {"block-start", "inline-end", "block-end", "inline-start"};
            if (IS("border-radius")) {
                const char *slash = find_top(s, e, "/");
                const char *ts[4], *te[4]; int k = split4(s, slash, ts, te), idx[4]; float v[4];
                for (int i = 0; i < k; i++) {
                    len_t rl;
                    if (!parse_len(c, ts[i], te[i], &rl)) return;
                    v[i] = rl.u == L_PX ? rl.v : rl.u == L_PCT ? -rl.v : 0;   /* negative = percent */
                }
                if (!k) return;
                expand4(k, idx); for (int i = 0; i < 4; i++) st->radius[i] = v[idx[i]];
                return;
            }
            static const char *corners[4] = {"border-top-left-radius", "border-top-right-radius",
                                             "border-bottom-right-radius", "border-bottom-left-radius"};
            for (int i = 0; i < 4; i++) if (IS(corners[i])) {
                const char *ts, *te, *p = s;
                if (next_tok(&p, e, &ts, &te) && parse_len(c, ts, te, &l))
                    st->radius[i] = l.u == L_PX ? l.v : l.u == L_PCT ? -l.v : 0;
                return;
            }
            const char *rest = name + 7; size_t rl = nl - 7;
            for (int i = 0; i < 4; i++) {
                const char *sd[2] = { sides[i], lsides[i] };
                for (int j = 0; j < 2; j++) {
                    size_t sl = strlen(sd[j]);
                    if (rl > sl + 1 && strncasecmp(rest, sd[j], sl) == 0 && rest[sl] == '-') {
                        const char *what = rest + sl + 1; size_t wl = rl - sl - 1;
                        if (w_ieqn(what, wl, "width")) { if (parse_border_width(c, s, e, &f)) st->border_w[i] = f; }
                        else if (w_ieqn(what, wl, "style")) { u8 b = parse_border_style(s, e); if (b != 255) st->border_style[i] = b; }
                        else if (w_ieqn(what, wl, "color")) { if (css_parse_color(s, n, &col, CUR_COLOR)) st->border_color[i] = col; }
                        return;
                    }
                }
            }
        }
        else if (IS("box-sizing")) st->box_sizing = tok_is(s, e, "border-box") ? BOX_BORDER : BOX_CONTENT;
        else if (IS("bottom")) parse_len(c, s, e, &st->inset[2]);
        else if (IS("box-shadow")) apply_box_shadow(c, s, e);
        else if (IS("border-collapse")) st->border_collapse = tok_is(s, e, "collapse");
        else if (IS("border-spacing")) { if (parse_px(c, s, find_top(s, e, " "), &f)) st->border_spacing = f; }
        break;
    case 'c':
        if (IS("color")) { if (css_parse_color(s, n, &col, ps ? ps->color : 0xFF000000u)) st->color = col; }
        else if (IS("content")) { char *v = (char *)pg_alloc(c->pg, n + 1); if (v) { memcpy(v, s, n); st->content = v; } }
        else if (IS("clear")) st->clear = tok_is(s, e, "left") ? CL_LEFT : tok_is(s, e, "right") ? CL_RIGHT : (tok_is(s, e, "both") || tok_is(s, e, "inline-start") || tok_is(s, e, "inline-end")) ? CL_BOTH : CL_NONE;
        else if (IS("column-gap") || IS("grid-column-gap")) { if (parse_len(c, s, e, &l)) st->col_gap = l.u == L_PX ? l.v : 0; }
        else if (IS("cursor")) st->cursor_pointer = tok_is(s, e, "pointer");
        break;
    case 'd':
        if (IS("display")) { u8 d = parse_display(s, e); if (d != 255) st->display = d; }
        break;
    case 'f':
        if (IS("font-size")) apply_font_size(c, s, e);
        else if (IS("font-weight")) apply_font_weight(c, s, e);
        else if (IS("font-style")) st->italic = (w_istr(s, n, "italic") || w_istr(s, n, "oblique")) ? 1 : 0;
        else if (IS("font-family")) apply_font_family(c, s, e);
        else if (IS("font")) apply_font(c, s, e);
        else if (IS("float")) st->float_ = (tok_is(s, e, "left") || tok_is(s, e, "inline-start")) ? FL_LEFT : (tok_is(s, e, "right") || tok_is(s, e, "inline-end")) ? FL_RIGHT : FL_NONE;
        else if (IS("flex")) apply_flex(c, s, e);
        else if (IS("flex-grow")) st->grow = strtof(s, NULL);
        else if (IS("flex-shrink")) st->shrink = strtof(s, NULL);
        else if (IS("flex-basis")) { if (tok_is(s, e, "content")) st->basis.u = L_AUTO; else parse_len(c, s, e, &st->basis); }
        else if (IS("flex-direction")) st->flex_dir = tok_is(s, e, "row") ? 0 : tok_is(s, e, "row-reverse") ? 1 : tok_is(s, e, "column") ? 2 : 3;
        else if (IS("flex-wrap")) st->flex_wrap = tok_is(s, e, "nowrap") ? 0 : tok_is(s, e, "wrap-reverse") ? 2 : 1;
        else if (IS("flex-flow")) {
            const char *p = s, *ts, *te;
            while (next_tok(&p, e, &ts, &te)) {
                if (tok_is(ts, te, "row")) st->flex_dir = 0; else if (tok_is(ts, te, "row-reverse")) st->flex_dir = 1;
                else if (tok_is(ts, te, "column")) st->flex_dir = 2; else if (tok_is(ts, te, "column-reverse")) st->flex_dir = 3;
                else if (tok_is(ts, te, "wrap")) st->flex_wrap = 1; else if (tok_is(ts, te, "nowrap")) st->flex_wrap = 0;
                else if (tok_is(ts, te, "wrap-reverse")) st->flex_wrap = 2;
            }
        }
        break;
    case 'g':
        if (IS("gap") || IS("grid-gap")) {
            const char *p = s, *ts, *te; float g[2]; int k = 0;
            while (k < 2 && next_tok(&p, e, &ts, &te)) { if (parse_len(c, ts, te, &l)) g[k++] = l.u == L_PX ? l.v : 0; }
            if (k == 1) g[1] = g[0];
            if (k) { st->row_gap = g[0]; st->col_gap = g[1]; }
        }
        else if (IS("grid-row-gap")) { if (parse_px(c, s, e, &f)) st->row_gap = f; }
        else if (IS("grid-template-columns")) parse_grid_template(c, s, e, &st->grid_cols);
        else if (IS("grid-template-areas")) st->grid_areas = parse_areas(c->pg, s, e);
        else if (IS("grid-template") || IS("grid")) {
            const char *sl = find_top(s, e, "/");
            if (memchr(s, '"', n) || memchr(s, '\'', n)) st->grid_areas = parse_areas(c->pg, s, sl);
            if (sl < e && !w_istr(sl, (size_t)(e - sl), "auto-flow")) parse_grid_template(c, sl + 1, e, &st->grid_cols);
        }
        else if (IS("grid-row")) parse_grid_line(s, e, &st->grid_row_start, &st->grid_row_span);
        else if (IS("grid-row-start")) { i32 sp; parse_grid_line(s, e, &st->grid_row_start, &sp); if (w_istr(s, n, "span")) st->grid_row_span = sp; }
        else if (IS("grid-row-end")) { if (w_istr(s, n, "span")) st->grid_row_span = (i32)strtol(s + 4, NULL, 10); else { i32 en = (i32)strtol(s, NULL, 10); if (st->grid_row_start > 0 && en > st->grid_row_start) st->grid_row_span = en - st->grid_row_start; } }
        else if (IS("grid-column")) parse_grid_line(s, e, &st->grid_col_start, &st->grid_col_span);
        else if (IS("grid-column-start")) { i32 sp; parse_grid_line(s, e, &st->grid_col_start, &sp); if (w_istr(s, n, "span")) st->grid_col_span = sp; }
        else if (IS("grid-column-end")) { if (w_istr(s, n, "span")) st->grid_col_span = (i32)strtol(s + 4, NULL, 10); else { i32 en = (i32)strtol(s, NULL, 10); if (en == -1) st->grid_col_span = 999; else if (st->grid_col_start > 0 && en > st->grid_col_start) st->grid_col_span = en - st->grid_col_start; } }
        else if (IS("grid-area")) {
            if ((*s >= '0' && *s <= '9') || *s == '-' || w_istr(s, n, "span") == s || memchr(s, '/', n)) {
                /* row-start / column-start / row-end / column-end */
                i32 v[4] = {0, 0, 0, 0}; int k = 0;
                const char *p = s;
                while (k < 4 && p < e) {
                    const char *q = find_top(p, e, "/");
                    const char *a = p, *b2 = q; trim(&a, &b2);
                    v[k++] = (a < b2 && w_istr(a, (size_t)(b2 - a), "span") != a) ? (i32)strtol(a, NULL, 10) : 0;
                    p = q < e ? q + 1 : e;
                }
                st->grid_row_start = v[0]; st->grid_col_start = v[1];
                st->grid_row_span = (v[2] > v[0] && v[0] > 0) ? v[2] - v[0] : 1;
                st->grid_col_span = (v[3] > v[1] && v[1] > 0) ? v[3] - v[1] : (v[3] == -1 ? 999 : 1);
            } else if (!tok_is(s, e, "auto")) {
                char *nm = (char *)pg_alloc(c->pg, n + 1);
                if (nm) { memcpy(nm, s, n); st->grid_area = nm; }
            }
        }
        break;
    case 'h':
        if (IS("height")) parse_len(c, s, e, &st->height);
        break;
    case 'i':
        if (IS("inset")) apply_len4(c, s, e, st->inset);
        else if (IS("inset-inline-start")) parse_len(c, s, e, &st->inset[3]);
        else if (IS("inset-inline-end")) parse_len(c, s, e, &st->inset[1]);
        else if (IS("inset-block-start")) parse_len(c, s, e, &st->inset[0]);
        else if (IS("inset-block-end")) parse_len(c, s, e, &st->inset[2]);
        else if (IS("inline-size")) parse_len(c, s, e, &st->width);
        break;
    case 'j':
        if (IS("justify-content")) { u8 a = parse_align(s, e); if (a < 254) st->justify = a; }
        else if (IS("justify-items") || IS("justify-self")) {}
        break;
    case 'l':
        if (IS("line-height")) apply_line_height(c, s, e);
        else if (IS("left")) parse_len(c, s, e, &st->inset[3]);
        else if (IS("letter-spacing")) { if (tok_is(s, e, "normal")) st->letter_spacing = 0; else if (parse_px(c, s, e, &f)) st->letter_spacing = f; }
        else if (IS("list-style") || IS("list-style-type")) {
            const char *p = s, *ts, *te;
            while (next_tok(&p, e, &ts, &te)) {
                if (tok_is(ts, te, "none")) st->list_style = LS_NONE;
                else if (tok_is(ts, te, "disc")) st->list_style = LS_DISC;
                else if (tok_is(ts, te, "circle")) st->list_style = LS_CIRCLE;
                else if (tok_is(ts, te, "square")) st->list_style = LS_SQUARE;
                else if (tok_is(ts, te, "decimal") || tok_is(ts, te, "decimal-leading-zero")) st->list_style = LS_DECIMAL;
                else if (tok_is(ts, te, "lower-alpha") || tok_is(ts, te, "lower-latin")) st->list_style = LS_LOWER_ALPHA;
                else if (tok_is(ts, te, "upper-alpha") || tok_is(ts, te, "upper-latin")) st->list_style = LS_UPPER_ALPHA;
                else if (tok_is(ts, te, "lower-roman")) st->list_style = LS_LOWER_ROMAN;
                else if (tok_is(ts, te, "upper-roman")) st->list_style = LS_UPPER_ROMAN;
                else if (tok_is(ts, te, "inside")) st->list_inside = 1;
                else if (tok_is(ts, te, "outside")) st->list_inside = 0;
            }
        }
        else if (IS("list-style-position")) st->list_inside = tok_is(s, e, "inside");
        break;
    case 'm':
        if (IS("mask") || IS("mask-image")) st->masked = (w_istr(s, n, "url(") || w_istr(s, n, "gradient(")) ? 1 : 0;
        else if (IS("margin")) apply_len4(c, s, e, st->margin);
        else if (IS("margin-top") || IS("margin-block-start")) parse_len(c, s, e, &st->margin[0]);
        else if (IS("margin-right") || IS("margin-inline-end")) parse_len(c, s, e, &st->margin[1]);
        else if (IS("margin-bottom") || IS("margin-block-end")) parse_len(c, s, e, &st->margin[2]);
        else if (IS("margin-left") || IS("margin-inline-start")) parse_len(c, s, e, &st->margin[3]);
        else if (IS("margin-inline")) { len_t v[4]; apply_len4(c, s, e, v); const char *ts[4], *te[4]; int k = split4(s, e, ts, te); if (k) { parse_len(c, ts[0], te[0], &st->margin[3]); parse_len(c, ts[k > 1], te[k > 1], &st->margin[1]); } }
        else if (IS("margin-block")) { const char *ts[4], *te[4]; int k = split4(s, e, ts, te); if (k) { parse_len(c, ts[0], te[0], &st->margin[0]); parse_len(c, ts[k > 1], te[k > 1], &st->margin[2]); } }
        else if (IS("min-width") || IS("min-inline-size")) parse_len(c, s, e, &st->min_w);
        else if (IS("min-height") || IS("min-block-size")) parse_len(c, s, e, &st->min_h);
        else if (IS("max-width") || IS("max-inline-size")) parse_len(c, s, e, &st->max_w);
        else if (IS("max-height") || IS("max-block-size")) parse_len(c, s, e, &st->max_h);
        break;
    case 'o':
        if (IS("overflow") || IS("overflow-x") || IS("overflow-y")) {
            u8 o = (w_istr(s, n, "hidden") || w_istr(s, n, "clip")) ? OV_HIDDEN :
                   (w_istr(s, n, "auto") || w_istr(s, n, "scroll")) ? OV_SCROLL : OV_VISIBLE;
            if (IS("overflow") || o > st->overflow) st->overflow = o;
        }
        else if (IS("opacity")) { float o = num_or_pct(s, e, 1.0f); st->opacity = o < 0 ? 0 : o > 1 ? 1 : o; }
        else if (IS("order")) st->order = (i32)strtol(s, NULL, 10);
        else if (IS("object-fit")) st->object_fit = tok_is(s, e, "cover") ? 1 : tok_is(s, e, "contain") ? 2 : 0;
        break;
    case 'p':
        if (IS("padding")) apply_len4(c, s, e, st->padding);
        else if (IS("padding-top") || IS("padding-block-start")) parse_len(c, s, e, &st->padding[0]);
        else if (IS("padding-right") || IS("padding-inline-end")) parse_len(c, s, e, &st->padding[1]);
        else if (IS("padding-bottom") || IS("padding-block-end")) parse_len(c, s, e, &st->padding[2]);
        else if (IS("padding-left") || IS("padding-inline-start")) parse_len(c, s, e, &st->padding[3]);
        else if (IS("padding-inline")) { const char *ts[4], *te[4]; int k = split4(s, e, ts, te); if (k) { parse_len(c, ts[0], te[0], &st->padding[3]); parse_len(c, ts[k > 1], te[k > 1], &st->padding[1]); } }
        else if (IS("padding-block")) { const char *ts[4], *te[4]; int k = split4(s, e, ts, te); if (k) { parse_len(c, ts[0], te[0], &st->padding[0]); parse_len(c, ts[k > 1], te[k > 1], &st->padding[2]); } }
        else if (IS("position")) st->position = tok_is(s, e, "relative") ? POS_RELATIVE : tok_is(s, e, "absolute") ? POS_ABSOLUTE : tok_is(s, e, "fixed") ? POS_FIXED : (tok_is(s, e, "sticky") || tok_is(s, e, "-webkit-sticky")) ? POS_STICKY : POS_STATIC;
        else if (IS("place-items")) { u8 a = parse_align(s, e); if (a < 254) st->align_items = a; }
        else if (IS("place-content")) { u8 a = parse_align(s, e); if (a < 254) { st->align_content = a; st->justify = a; } }
        else if (IS("place-self")) { u8 a = parse_align(s, e); st->align_self = a >= 254 ? 255 : a; }
        break;
    case 'r':
        if (IS("right")) parse_len(c, s, e, &st->inset[1]);
        else if (IS("row-gap")) { if (parse_len(c, s, e, &l)) st->row_gap = l.u == L_PX ? l.v : 0; }
        break;
    case 't':
        if (IS("top")) parse_len(c, s, e, &st->inset[0]);
        else if (IS("text-align")) st->text_align = (tok_is(s, e, "right") || tok_is(s, e, "end")) ? TA_RIGHT : (tok_is(s, e, "center") || tok_is(s, e, "-webkit-center") || tok_is(s, e, "-moz-center")) ? TA_CENTER : tok_is(s, e, "justify") ? TA_JUSTIFY : TA_LEFT;
        else if (IS("text-decoration") || IS("text-decoration-line")) {
            u8 d = 0; const char *p = s, *ts, *te;
            while (next_tok(&p, e, &ts, &te)) {
                if (tok_is(ts, te, "underline")) d |= DECO_UNDER;
                else if (tok_is(ts, te, "overline")) d |= DECO_OVER;
                else if (tok_is(ts, te, "line-through")) d |= DECO_THROUGH;
                else if (css_parse_color(ts, (size_t)(te - ts), &col, CUR_COLOR) && IS("text-decoration")) st->deco_color = col;
            }
            st->deco = d;
            st->deco_owner = 1;
        }
        else if (IS("text-decoration-color")) { if (css_parse_color(s, n, &col, CUR_COLOR)) st->deco_color = col; }
        else if (IS("text-transform")) st->text_transform = tok_is(s, e, "uppercase") ? TT_UPPER : tok_is(s, e, "lowercase") ? TT_LOWER : tok_is(s, e, "capitalize") ? TT_CAP : TT_NONE;
        else if (IS("text-indent")) { if (parse_len(c, s, e, &l)) st->text_indent = l.u == L_PX ? l.v : 0; }
        else if (IS("text-overflow")) st->nowrap_ellipsis = tok_is(s, e, "ellipsis");
        else if (IS("text-wrap") || IS("text-wrap-mode")) { if (tok_is(s, e, "nowrap")) st->white_space = WS_NOWRAP; }
        break;
    case 'v':
        if (IS("visibility")) st->visibility = (tok_is(s, e, "hidden") || tok_is(s, e, "collapse")) ? 1 : 0;
        else if (IS("vertical-align")) {
            st->vertical_align = tok_is(s, e, "middle") ? VA_MIDDLE : tok_is(s, e, "top") ? VA_TOP :
                tok_is(s, e, "bottom") ? VA_BOTTOM : tok_is(s, e, "sub") ? VA_SUB : tok_is(s, e, "super") ? VA_SUPER :
                tok_is(s, e, "text-top") ? VA_TEXT_TOP : tok_is(s, e, "text-bottom") ? VA_TEXT_BOTTOM : VA_BASELINE;
        }
        break;
    case 'w':
        if (IS("width")) parse_len(c, s, e, &st->width);
        else if (IS("white-space") || IS("white-space-collapse")) {
            st->white_space = tok_is(s, e, "nowrap") ? WS_NOWRAP : tok_is(s, e, "pre") ? WS_PRE :
                (tok_is(s, e, "pre-wrap") || tok_is(s, e, "break-spaces") || tok_is(s, e, "preserve")) ? WS_PRE_WRAP : tok_is(s, e, "pre-line") ? WS_PRE_LINE : WS_NORMAL;
        }
        else if (IS("word-spacing")) { if (parse_px(c, s, e, &f)) st->word_spacing = f; }
        else if (IS("word-break") || IS("overflow-wrap") || IS("word-wrap")) { st->break_word = (tok_is(s, e, "break-all") || tok_is(s, e, "break-word") || tok_is(s, e, "anywhere")); }
        else if (IS("width")) {}
        break;
    case 'z':
        if (IS("z-index")) { if (tok_is(s, e, "auto")) st->z_auto = true; else { st->z_auto = false; st->z_index = (i32)strtol(s, NULL, 10); } }
        break;
    }
#undef IS
}

/* =================================================================== */
/*  Cascade collection                                                  */
/* =================================================================== */

typedef struct {
    const char *name; size_t nl;
    const char *val;  size_t vl;
    u32 sp; u32 seq;
} decl_t;

typedef struct {
    decl_t *d; int n, cap;
} dlist_t;

static lxb_status_t walk_cb(lxb_dom_element_t *el, const lxb_css_rule_declaration_t *declr,
                            void *ctx, lxb_css_selector_specificity_t spec, bool is_weak) {
    (void)el; (void)is_weak;
    dlist_t *lists = (dlist_t *)ctx;     /* [0] element, [1] ::before, [2] ::after */
    if (declr->type != LXB_CSS_PROPERTY__CUSTOM) return LXB_STATUS_OK;
    const lexbor_str_t *nm = &declr->u.custom->name, *vv = &declr->u.custom->value;
    if (nm->length < 5 || nm->data[0] != '-' || nm->data[1] != '-' || nm->data[3] != '-') return LXB_STATUS_OK;
    char kind = (char)nm->data[2];
    dlist_t *dl = kind == 'x' ? &lists[0] : kind == 'b' ? &lists[1] : kind == 'a' ? &lists[2] : NULL;
    if (!dl) return LXB_STATUS_OK;
    const char *v = (const char *)vv->data, *ve = v + vv->length;
    while (v < ve && *v == ' ') v++;
    char *endp;
    unsigned long seq = strtoul(v, &endp, 10);
    if (endp == v) return LXB_STATUS_OK;
    v = endp;
    if (dl->n == dl->cap) {
        int nc = dl->cap ? dl->cap * 2 : 32;
        decl_t *nd = (decl_t *)realloc(dl->d, (size_t)nc * sizeof(decl_t));
        if (!nd) return LXB_STATUS_OK;
        dl->d = nd; dl->cap = nc;
    }
    decl_t *d = &dl->d[dl->n++];
    d->name = (const char *)nm->data + 4; d->nl = nm->length - 4;
    d->val = v; d->vl = (size_t)(ve - v);
    d->sp = spec; d->seq = (u32)seq;
    return LXB_STATUS_OK;
}

/* priority: important > origin(author) > specificity > order */
static int decl_cmp(const void *a, const void *b) {
    const decl_t *x = (const decl_t *)a, *y = (const decl_t *)b;
    u32 xi = lxb_css_selector_sp_i(x->sp), yi = lxb_css_selector_sp_i(y->sp);
    if (xi != yi) return xi < yi ? -1 : 1;
    u32 xo = x->seq >= AUTHOR_SEQ, yo = y->seq >= AUTHOR_SEQ;
    if (xo != yo) return xo < yo ? -1 : 1;
    u32 xs = x->sp & 0x0FFFFFFFu, ys = y->sp & 0x0FFFFFFFu;
    if (xs != ys) return xs < ys ? -1 : 1;
    if (x->seq != y->seq) return x->seq < y->seq ? -1 : 1;
    return 0;
}

/* Inherited properties copied from the parent. */
static void inherit(style_t *st, const style_t *ps) {
    st->color = ps->color;
    st->font_size = ps->font_size; st->font_weight = ps->font_weight;
    st->font_family = ps->font_family; st->italic = ps->italic;
    st->lh_factor = ps->lh_factor;
    st->line_height = ps->lh_factor > 0 ? -2 : ps->line_height;  /* -2: recompute */
    st->letter_spacing = ps->letter_spacing; st->word_spacing = ps->word_spacing;
    st->text_align = ps->text_align; st->text_transform = ps->text_transform;
    st->text_indent = ps->text_indent; st->white_space = ps->white_space;
    st->visibility = ps->visibility; st->list_style = ps->list_style;
    st->list_inside = ps->list_inside; st->cursor_pointer = ps->cursor_pointer;
    st->border_collapse = ps->border_collapse; st->border_spacing = ps->border_spacing;
    st->break_word = ps->break_word;
    st->custom_props = ps->custom_props;
    st->is_link = ps->is_link;
    /* text-decoration propagates to descendants' text */
    st->deco = ps->deco; st->deco_color = ps->deco_color;
}

static void set_initial(style_t *st) {
    st->display = D_INLINE;
    st->opacity = 1; st->z_auto = true;
    st->shrink = 1; st->basis.u = L_AUTO;
    st->max_w.u = L_NONE; st->max_h.u = L_NONE;
    st->align_items = JC_STRETCH; st->align_self = 255; st->align_content = JC_STRETCH;
    for (int i = 0; i < 4; i++) {
        st->margin[i].u = L_PX; st->padding[i].u = L_PX;
        st->border_w[i] = 3; st->border_style[i] = BS_NONE; st->border_color[i] = CUR_COLOR;
    }
    st->bg_pos[0].u = L_PCT; st->bg_pos[1].u = L_PCT;
    st->grid_col_span = 1; st->grid_row_span = 1;
}

style_t *css_initial_root(page_t *pg) {
    style_t *st = (style_t *)pg_alloc(pg, sizeof(style_t));
    set_initial(st);
    st->display = D_BLOCK;
    st->color = 0xFF000000u; st->font_size = 16; st->font_weight = 400;
    st->line_height = -1;
    return st;
}

style_t *css_anon_style(page_t *pg, const style_t *ps, u8 display) {
    style_t *st = (style_t *)pg_alloc(pg, sizeof(style_t));
    set_initial(st);
    inherit(st, ps);
    if (st->line_height == -2) st->line_height = st->lh_factor * st->font_size;
    st->display = display;
    return st;
}

/* Presentational HTML attributes, applied as lowest-priority author style. */
static void apply_presentational(cctx_t *c) {
    lxb_dom_element_t *el = c->el;
    uintptr_t tag = lxb_dom_element_tag_id(el);
    size_t l; const char *v;
    char buf[64];
#define ATTR(nm) (v = attr(el, nm, &l))
    if (ATTR("bgcolor")) apply_prop(c, "background-color", 16, v, v + l);
    if ((tag == LXB_TAG_TABLE || tag == LXB_TAG_TD || tag == LXB_TAG_TH || tag == LXB_TAG_IMG ||
         tag == LXB_TAG_COL || tag == LXB_TAG_IFRAME || tag == LXB_TAG_HR || tag == LXB_TAG_CANVAS ||
         tag == LXB_TAG_VIDEO || tag == LXB_TAG_EMBED || tag == LXB_TAG_OBJECT || tag == LXB_TAG_SVG ||
         tag == LXB_TAG_INPUT) && ATTR("width")) {
        snprintf(buf, sizeof buf, "%.*s%s", (int)(l < 40 ? l : 40), v, (l && v[l-1] != '%' && v[l-1] != 'x') ? "px" : "");
        if (strtof(buf, NULL) > 0) apply_prop(c, "width", 5, buf, buf + strlen(buf));
    }
    if ((tag == LXB_TAG_TABLE || tag == LXB_TAG_TD || tag == LXB_TAG_TH || tag == LXB_TAG_TR || tag == LXB_TAG_IMG ||
         tag == LXB_TAG_IFRAME || tag == LXB_TAG_CANVAS || tag == LXB_TAG_VIDEO || tag == LXB_TAG_EMBED ||
         tag == LXB_TAG_OBJECT || tag == LXB_TAG_SVG) && ATTR("height")) {
        snprintf(buf, sizeof buf, "%.*s%s", (int)(l < 40 ? l : 40), v, (l && v[l-1] != '%' && v[l-1] != 'x') ? "px" : "");
        if (strtof(buf, NULL) > 0) apply_prop(c, "height", 6, buf, buf + strlen(buf));
    }
    if (ATTR("align")) {
        if (tag == LXB_TAG_IMG) {
            if (w_ieqn(v, l, "left")) c->st->float_ = FL_LEFT;
            else if (w_ieqn(v, l, "right")) c->st->float_ = FL_RIGHT;
            else if (w_ieqn(v, l, "middle") || w_ieqn(v, l, "absmiddle")) c->st->vertical_align = VA_MIDDLE;
        } else if (tag == LXB_TAG_TABLE) {
            if (w_ieqn(v, l, "center")) { c->st->margin[1].u = L_AUTO; c->st->margin[3].u = L_AUTO; }
            else if (w_ieqn(v, l, "left")) c->st->float_ = FL_LEFT;
            else if (w_ieqn(v, l, "right")) c->st->float_ = FL_RIGHT;
        } else {
            if (w_ieqn(v, l, "center") || w_ieqn(v, l, "middle")) c->st->text_align = TA_CENTER;
            else if (w_ieqn(v, l, "right")) c->st->text_align = TA_RIGHT;
            else if (w_ieqn(v, l, "left")) c->st->text_align = TA_LEFT;
            else if (w_ieqn(v, l, "justify")) c->st->text_align = TA_JUSTIFY;
        }
    }
    if (ATTR("valign")) {
        c->st->vertical_align = w_ieqn(v, l, "top") ? VA_TOP : w_ieqn(v, l, "bottom") ? VA_BOTTOM : VA_MIDDLE;
        c->st->valign_set = 1;
    }
    if (tag == LXB_TAG_FONT) {
        if (ATTR("color")) apply_prop(c, "color", 5, v, v + l);
        if (ATTR("face")) apply_font_family(c, v, v + l);
        if (ATTR("size") && l) {
            int sz = (int)strtol(v, NULL, 10);
            if (*v == '+' || *v == '-') sz += 3;
            static const float sizes[8] = {10, 10, 13, 16, 18, 24, 32, 48};
            if (sz < 1) sz = 1;
            if (sz > 7) sz = 7;
            c->st->font_size = sizes[sz]; c->em = sizes[sz];
        }
    }
    if (tag == LXB_TAG_BODY) {
        if (ATTR("text")) apply_prop(c, "color", 5, v, v + l);
        if (ATTR("marginwidth") || ATTR("leftmargin")) { float m = strtof(v, NULL); c->st->margin[1].v = c->st->margin[3].v = m; }
        if (ATTR("marginheight") || ATTR("topmargin")) { float m = strtof(v, NULL); c->st->margin[0].v = c->st->margin[2].v = m; }
    }
    if (tag == LXB_TAG_TABLE) {
        if (ATTR("border")) {
            float b = l ? strtof(v, NULL) : 1;
            if (b > 0) for (int i = 0; i < 4; i++) { c->st->border_w[i] = b; c->st->border_style[i] = BS_OTHER; c->st->border_color[i] = 0xFF808080u; }
        }
        if (ATTR("cellspacing")) c->st->border_spacing = strtof(v, NULL);
    }
    if ((tag == LXB_TAG_TD || tag == LXB_TAG_TH)) {
        if (ATTR("nowrap")) c->st->white_space = WS_NOWRAP;
        /* cellpadding / border from the owning table */
        lxb_dom_node_t *t = lxb_dom_interface_node(el)->parent;
        while (t && lxb_dom_node_tag_id(t) != LXB_TAG_TABLE) t = t->parent;
        if (t) {
            lxb_dom_element_t *te = lxb_dom_interface_element(t);
            if ((v = attr(te, "cellpadding", &l))) {
                float pd = strtof(v, NULL);
                for (int i = 0; i < 4; i++) { c->st->padding[i].u = L_PX; c->st->padding[i].v = pd; }
            }
            if ((v = attr(te, "border", &l)) && (l == 0 || strtof(v, NULL) > 0))
                for (int i = 0; i < 4; i++) { c->st->border_w[i] = 1; c->st->border_style[i] = BS_OTHER; c->st->border_color[i] = 0xFF808080u; }
        }
    }
    if (tag == LXB_TAG_IMG && ATTR("border")) {
        float b = strtof(v, NULL);
        for (int i = 0; i < 4; i++) { c->st->border_w[i] = b; c->st->border_style[i] = b > 0 ? BS_SOLID : BS_NONE; }
    }
    if ((tag == LXB_TAG_IMG) && (ATTR("hspace"))) { float m = strtof(v, NULL); c->st->margin[1].v = c->st->margin[3].v = m; }
    if ((tag == LXB_TAG_IMG) && (ATTR("vspace"))) { float m = strtof(v, NULL); c->st->margin[0].v = c->st->margin[2].v = m; }
    if (tag == LXB_TAG_HR) {
        if (ATTR("noshade")) for (int i = 0; i < 4; i++) c->st->border_style[i] = BS_SOLID;
        if (ATTR("size")) { float h = strtof(v, NULL); if (h > 2) { c->st->height.u = L_PX; c->st->height.v = h - 2; } }
        if (ATTR("color")) { u32 cc; if (css_parse_color(v, l, &cc, 0)) { c->st->bg_color = cc; for (int i = 0; i < 4; i++) c->st->border_color[i] = cc; } }
    }
    if (tag == LXB_TAG_TEXTAREA || tag == LXB_TAG_SELECT || tag == LXB_TAG_INPUT || tag == LXB_TAG_BUTTON) {}
    if ((tag == LXB_TAG_UL || tag == LXB_TAG_OL) && ATTR("type")) {
        if (l == 1 && *v == '1') c->st->list_style = LS_DECIMAL;
        else if (l == 1 && *v == 'a') c->st->list_style = LS_LOWER_ALPHA;
        else if (l == 1 && *v == 'A') c->st->list_style = LS_UPPER_ALPHA;
        else if (l == 1 && *v == 'i') c->st->list_style = LS_LOWER_ROMAN;
        else if (l == 1 && *v == 'I') c->st->list_style = LS_UPPER_ROMAN;
        else if (w_ieqn(v, l, "disc")) c->st->list_style = LS_DISC;
        else if (w_ieqn(v, l, "circle")) c->st->list_style = LS_CIRCLE;
        else if (w_ieqn(v, l, "square")) c->st->list_style = LS_SQUARE;
    }
#undef ATTR
}

/* Build the custom-property blob for this element from declared --vars. */
static const char *build_customs(page_t *pg, const char *parent_blob, decl_t *d, int n) {
    int count = 0;
    for (int i = 0; i < n; i++) if (d[i].nl > 2 && d[i].name[0] == '-' && d[i].name[1] == '-') count++;
    if (!count) return parent_blob;
    sbuf_t b = {0};
    /* winners are later in the sorted array; emit highest priority first so
     * lookups find them first */
    for (int i = n - 1; i >= 0; i--) {
        if (!(d[i].nl > 2 && d[i].name[0] == '-' && d[i].name[1] == '-')) continue;
        bool dup = false;
        for (int j = n - 1; j > i; j--)
            if (d[j].nl == d[i].nl && memcmp(d[j].name, d[i].name, d[i].nl) == 0) { dup = true; break; }
        if (dup) continue;
        /* values may themselves reference vars (resolved against the parent) */
        char *exp = expand_vars(parent_blob, d[i].val, d[i].vl, 0);
        sb_put(&b, d[i].name, d[i].nl); sb_put(&b, "", 1);
        if (b.s) b.s[b.n - 1] = 0;
        const char *vv = exp ? exp : d[i].val; size_t vl = exp ? strlen(exp) : d[i].vl;
        while (vl && (*vv == ' ')) { vv++; vl--; }
        sb_put(&b, vv, vl); sb_put(&b, "", 1);
        if (b.s) b.s[b.n - 1] = 0;
        free(exp);
    }
    /* append parent entries not overridden */
    if (parent_blob) {
        const char *p = parent_blob;
        while (*p) {
            size_t l = strlen(p);
            const char *v = p + l + 1;
            size_t vl = strlen(v);
            if (!custom_lookup(b.s, p, l)) { sb_put(&b, p, l + 1); if (b.s) b.s[b.n - 1] = 0; sb_put(&b, v, vl + 1); if (b.s) b.s[b.n - 1] = 0; }
            p = v + vl + 1;
        }
    }
    sb_put(&b, "", 1);
    if (!b.s) return parent_blob;
    b.s[b.n - 1] = 0;
    char *out = (char *)pg_alloc(pg, b.n + 1);
    if (out) memcpy(out, b.s, b.n);
    free(b.s);
    return out ? out : parent_blob;
}

static bool is_font_prop(const decl_t *d) {
    return w_ieqn(d->name, d->nl, "font-size") || w_ieqn(d->name, d->nl, "font") ||
           w_ieqn(d->name, d->nl, "color");
}

/* Apply sorted declarations (lowest priority first) to c->st. */
static void apply_decls(cctx_t *c, decl_t *d0, int n) {
    style_t *st = c->st;
    /* pass 1: font-size/font/color (lengths depend on em; colors on currentColor) */
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < n; i++) {
            decl_t *d = &d0[i];
            if (d->nl > 2 && d->name[0] == '-' && d->name[1] == '-') continue;
            if (pass == 0 && !is_font_prop(d)) continue;
            if (pass == 1 && is_font_prop(d) && !w_ieqn(d->name, d->nl, "font")) continue;
            char *exp = expand_vars(st->custom_props, d->val, d->vl, 0);
            if (exp) { apply_prop(c, d->name, d->nl, exp, exp + strlen(exp)); free(exp); }
            else apply_prop(c, d->name, d->nl, d->val, d->val + d->vl);
        }
    }
}

static void put_utf8(sbuf_t *b, u32 cp) {
    char u[4]; size_t n;
    if (cp < 0x80) { u[0] = (char)cp; n = 1; }
    else if (cp < 0x800) { u[0] = (char)(0xC0 | (cp >> 6)); u[1] = (char)(0x80 | (cp & 0x3F)); n = 2; }
    else if (cp < 0x10000) { u[0] = (char)(0xE0 | (cp >> 12)); u[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); u[2] = (char)(0x80 | (cp & 0x3F)); n = 3; }
    else { u[0] = (char)(0xF0 | (cp >> 18)); u[1] = (char)(0x80 | ((cp >> 12) & 0x3F)); u[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); u[3] = (char)(0x80 | (cp & 0x3F)); n = 4; }
    sb_put(b, u, n);
}

/* Evaluate a `content` value into text: strings (with CSS escapes), attr(),
 * quotes. Returns NULL for none/normal or anything we cannot produce. */
static const char *content_text(page_t *pg, lxb_dom_element_t *el, const char *s) {
    if (!s) return NULL;
    const char *e = s + strlen(s);
    trim(&s, &e);
    if (s >= e || tok_is(s, e, "none") || tok_is(s, e, "normal")) return NULL;
    sbuf_t out = {0};
    bool any = false;
    const char *p = s, *ts, *te;
    while (next_tok(&p, e, &ts, &te)) {
        if (*ts == '"' || *ts == '\'') {
            any = true;
            for (const char *q = ts + 1; q < te - 1; q++) {
                if (*q == '\\' && q + 1 < te - 1) {
                    q++;
                    if (hexd(*q) >= 0) {
                        u32 cp = 0; int k = 0;
                        while (k < 6 && q < te - 1 && hexd(*q) >= 0) { cp = cp * 16 + (u32)hexd(*q); q++; k++; }
                        if (q < te - 1 && *q == ' ') q++;
                        q--;
                        if (cp == 0xA) cp = '\n';
                        put_utf8(&out, cp ? cp : 0xFFFD);
                    } else sb_put(&out, q, 1);
                    continue;
                }
                sb_put(&out, q, 1);
            }
        } else if (w_istr(ts, (size_t)(te - ts), "attr(") == ts) {
            const char *a = ts + 5, *b = te - 1; trim(&a, &b);
            size_t l = 0; char nm[64];
            snprintf(nm, sizeof nm, "%.*s", (int)(b - a), a);
            const lxb_char_t *v = lxb_dom_element_get_attribute(el, (const lxb_char_t *)nm, strlen(nm), &l);
            if (v && l) sb_put(&out, (const char *)v, l);
            any = true;
        } else if (tok_is(ts, te, "open-quote")) { sb_puts(&out, "\xE2\x80\x9C"); any = true; }
        else if (tok_is(ts, te, "close-quote")) { sb_puts(&out, "\xE2\x80\x9D"); any = true; }
        else if (tok_is(ts, te, "no-open-quote") || tok_is(ts, te, "no-close-quote")) any = true;
        else if (w_istr(ts, (size_t)(te - ts), "counter") == ts) any = true;   /* counters: unsupported, empty */
        else if (*ts == '/') break;                                           /* alt text follows */
    }
    if (!any) { free(out.s); return NULL; }
    char *r = (char *)pg_alloc(pg, out.n + 1);
    if (r && out.s) memcpy(r, out.s, out.n);
    free(out.s);
    return r;
}

static void finish_style(style_t *st);

style_t *css_compute(page_t *pg, lxb_dom_element_t *el, const style_t *ps) {
    style_t *st = (style_t *)pg_alloc(pg, sizeof(style_t));
    if (!st) return NULL;
    set_initial(st);
    inherit(st, ps);
    cctx_t c = { pg, st, ps, el, ps->font_size, pg->root_style ? pg->root_style->font_size : 16 };

    dlist_t dl[3] = {{0}};
    lxb_dom_element_style_walk(el, walk_cb, dl, true);
    for (int k = 0; k < 3; k++)
        if (dl[k].n > 1) qsort(dl[k].d, (size_t)dl[k].n, sizeof(decl_t), decl_cmp);

    st->custom_props = build_customs(pg, ps->custom_props, dl[0].d, dl[0].n);

    /* presentational hints first: they lose against any author CSS */
    apply_presentational(&c);
    apply_decls(&c, dl[0].d, dl[0].n);
    finish_style(st);

    /* ::before / ::after */
    for (int k = 0; k < 2; k++) {
        if (!dl[k + 1].n || st->display == D_NONE) continue;
        style_t *ps2 = (style_t *)pg_alloc(pg, sizeof(style_t));
        if (!ps2) break;
        set_initial(ps2);
        inherit(ps2, st);
        ps2->custom_props = build_customs(pg, st->custom_props, dl[k + 1].d, dl[k + 1].n);
        cctx_t pc = { pg, ps2, st, el, st->font_size, c.rem };
        apply_decls(&pc, dl[k + 1].d, dl[k + 1].n);
        finish_style(ps2);
        const char *txt = content_text(pg, el, ps2->content);
        if (txt && ps2->display != D_NONE) { st->pseudo[k] = ps2; st->pseudo_text[k] = txt; }
    }
    for (int k = 0; k < 3; k++) free(dl[k].d);

    uintptr_t tag = lxb_dom_element_tag_id(el);
    if (tag == LXB_TAG_A && lxb_dom_element_has_attribute(el, (const lxb_char_t *)"href", 4)) st->is_link = 1;
    return st;
}

/* Resolve late-bound values and blockify floats/positioned boxes. */
static void finish_style(style_t *st) {
    if (st->line_height == -2) st->line_height = st->lh_factor * st->font_size;
    for (int i = 0; i < 4; i++) {
        if (st->border_color[i] == CUR_COLOR) st->border_color[i] = st->color;
        if (st->border_style[i] == BS_NONE) st->border_w[i] = 0;
    }
    if (st->deco_owner && (st->deco_color == CUR_COLOR || st->deco_color == 0)) st->deco_color = st->color;
    if (st->bg_color == CUR_COLOR) st->bg_color = st->color;

    /* blockification */
    if (st->float_ != FL_NONE || st->position == POS_ABSOLUTE || st->position == POS_FIXED) {
        if (st->display == D_INLINE || st->display == D_INLINE_BLOCK || st->display == D_TABLE_CELL ||
            st->display == D_TABLE_ROW || st->display == D_TABLE_ROW_GROUP) st->display = D_BLOCK;
        else if (st->display == D_INLINE_FLEX) st->display = D_FLEX;
        else if (st->display == D_INLINE_GRID) st->display = D_GRID;
        else if (st->display == D_INLINE_TABLE) st->display = D_TABLE;
    }
    if (st->position == POS_ABSOLUTE || st->position == POS_FIXED) st->float_ = FL_NONE;
}
