/* web/engine/util.c -- strings, URLs, UTF-8, logging, page arena. */
#include "engine.h"
#include <stdarg.h>
#include <stdio.h>

web_platform_t g_web;

void web_log(const char *fmt, ...) {
    if (!g_web.log) return;
    char buf[512];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    g_web.log(g_web.ctx, buf);
}

char *w_strndup(const char *s, size_t n) {
    char *d = (char *)malloc(n + 1);
    if (!d) return NULL;
    memcpy(d, s, n); d[n] = 0;
    return d;
}
char *w_strdup(const char *s) { return w_strndup(s, strlen(s)); }

static inline char lc(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

bool w_ieq(const char *a, const char *b) {
    while (*a && *b) { if (lc(*a) != lc(*b)) return false; a++; b++; }
    return *a == *b;
}
bool w_ieqn(const char *a, size_t an, const char *b) {
    size_t i = 0;
    for (; i < an && b[i]; i++) if (lc(a[i]) != lc(b[i])) return false;
    return i == an && b[i] == 0;
}
const char *w_istr(const char *hay, size_t hlen, const char *needle) {
    size_t nl = strlen(needle);
    if (nl == 0) return hay;
    for (size_t i = 0; i + nl <= hlen; i++) {
        size_t j = 0;
        while (j < nl && lc(hay[i + j]) == lc(needle[j])) j++;
        if (j == nl) return hay + i;
    }
    return NULL;
}

/* ---- arena ---- */
void *pg_alloc(page_t *pg, size_t n) {
    n = (n + 15) & ~(size_t)15;
    if (pg->arena_used + n > pg->arena_cap) {
        size_t cap = n > 256 * 1024 ? n : 256 * 1024;
        char *blk = (char *)malloc(cap);
        if (!blk) return NULL;
        if (pg->nblocks == pg->capblocks) {
            size_t nc = pg->capblocks ? pg->capblocks * 2 : 32;
            void **nb = (void **)realloc(pg->blocks, nc * sizeof(void *));
            if (!nb) { free(blk); return NULL; }
            pg->blocks = nb; pg->capblocks = nc;
        }
        pg->blocks[pg->nblocks++] = blk;
        pg->arena = blk; pg->arena_cap = cap; pg->arena_used = 0;
    }
    void *p = pg->arena + pg->arena_used;
    pg->arena_used += n;
    memset(p, 0, n);
    return p;
}

/* ---- UTF-8 ---- */
u32 utf8_next(const char **pp, const char *end) {
    const u8 *p = (const u8 *)*pp;
    u32 c = *p++;
    if (c >= 0x80) {
        int extra = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : (c >= 0xC0) ? 1 : 0;
        if (extra == 0) { *pp = (const char *)p; return 0xFFFD; }
        c &= (0x3F >> extra);
        for (int i = 0; i < extra; i++) {
            if ((const char *)p >= end || (*p & 0xC0) != 0x80) { c = 0xFFFD; break; }
            c = (c << 6) | (*p++ & 0x3F);
        }
    }
    *pp = (const char *)p;
    return c;
}

/* ---- URLs ---- */
static size_t scheme_len(const char *s) {
    size_t i = 0;
    while ((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z') ||
           (i && ((s[i] >= '0' && s[i] <= '9') || s[i] == '+' || s[i] == '-' || s[i] == '.'))) i++;
    return (i > 0 && s[i] == ':') ? i : 0;
}

static void copy_trim(char *out, size_t cap, const char *s, size_t n) {
    while (n && (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')) { s++; n--; }
    while (n && (s[n-1] == ' ' || s[n-1] == '\t' || s[n-1] == '\n' || s[n-1] == '\r')) n--;
    size_t o = 0;
    for (size_t i = 0; i < n && o + 1 < cap; i++) {
        if (s[i] == '\n' || s[i] == '\r' || s[i] == '\t') continue;
        out[o++] = s[i];
    }
    out[o] = 0;
}

/* Remove "." and ".." segments from a path in place. */
static void normalize_path(char *path) {
    char out[1024]; size_t o = 0;
    const char *p = path;
    char *q = strpbrk(path, "?#");
    char tail[1024] = "";
    if (q) { snprintf(tail, sizeof tail, "%s", q); *q = 0; }
    while (*p) {
        const char *seg = p; while (*p && *p != '/') p++;
        size_t sl = (size_t)(p - seg);
        bool slash = (*p == '/'); if (slash) p++;
        if (sl == 1 && seg[0] == '.') { if (!slash && o && out[o-1] != '/') out[o++] = '/'; continue; }
        if (sl == 2 && seg[0] == '.' && seg[1] == '.') {
            if (o > 1) { o--; while (o > 0 && out[o-1] != '/') o--; }
            continue;
        }
        if (o + sl + 2 >= sizeof out) break;
        memcpy(out + o, seg, sl); o += sl;
        if (slash) out[o++] = '/';
    }
    out[o] = 0;
    snprintf(path, 1024, "%s%s", out, tail);
}

void url_resolve(const char *base, const char *ref_in, char *out, size_t cap) {
    char ref[1024];
    copy_trim(ref, sizeof ref, ref_in, strlen(ref_in));
    if (scheme_len(ref)) { snprintf(out, cap, "%s", ref); return; }
    size_t bs = scheme_len(base);
    if (!bs) { snprintf(out, cap, "%s", ref); return; }
    if (ref[0] == '/' && ref[1] == '/') { snprintf(out, cap, "%.*s:%s", (int)bs, base, ref); return; }
    /* authority end */
    const char *auth = base + bs + 1;
    const char *path = auth;
    if (auth[0] == '/' && auth[1] == '/') { path = auth + 2; while (*path && *path != '/' && *path != '?' && *path != '#') path++; }
    size_t origin_len = (size_t)(path - base);
    char buf[1024];
    if (ref[0] == 0) { snprintf(out, cap, "%s", base); char *h = strchr(out, '#'); if (h) *h = 0; return; }
    if (ref[0] == '#') {
        const char *h = strchr(base, '#');
        size_t bl = h ? (size_t)(h - base) : strlen(base);
        snprintf(out, cap, "%.*s%s", (int)bl, base, ref); return;
    }
    if (ref[0] == '?') {
        const char *q = strpbrk(path, "?#");
        size_t bl = q ? (size_t)(q - base) : strlen(base);
        snprintf(out, cap, "%.*s%s", (int)bl, base, ref); return;
    }
    if (ref[0] == '/') {
        snprintf(buf, sizeof buf, "%s", ref);
    } else {
        const char *q = strpbrk(path, "?#");
        size_t pl = q ? (size_t)(q - path) : strlen(path);
        size_t dir = pl;
        while (dir > 0 && path[dir-1] != '/') dir--;
        if (dir == 0) snprintf(buf, sizeof buf, "/%s", ref);
        else snprintf(buf, sizeof buf, "%.*s%s", (int)dir, path, ref);
    }
    normalize_path(buf);
    snprintf(out, cap, "%.*s%s", (int)origin_len, base, buf);
}

/* ---- data: URLs ---- */
static int b64v(int c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+' || c == '-') return 62;
    if (c == '/' || c == '_') return 63;
    return -1;
}
static int hexv(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

unsigned char *data_url_decode(const char *url, size_t *len) {
    if (strncmp(url, "data:", 5) != 0) return NULL;
    const char *comma = strchr(url, ',');
    if (!comma) return NULL;
    bool b64 = w_istr(url, (size_t)(comma - url), ";base64") != NULL;
    const char *s = comma + 1;
    size_t n = strlen(s);
    unsigned char *out = (unsigned char *)malloc(n + 1);
    if (!out) return NULL;
    size_t o = 0;
    if (b64) {
        u32 acc = 0; int bits = 0;
        for (size_t i = 0; i < n; i++) {
            int v = b64v((unsigned char)s[i]);
            if (v < 0) continue;
            acc = (acc << 6) | (u32)v; bits += 6;
            if (bits >= 8) { bits -= 8; out[o++] = (unsigned char)(acc >> bits); }
        }
    } else {
        for (size_t i = 0; i < n; i++) {
            if (s[i] == '%' && i + 2 < n && hexv(s[i+1]) >= 0 && hexv(s[i+2]) >= 0) {
                out[o++] = (unsigned char)(hexv(s[i+1]) * 16 + hexv(s[i+2])); i += 2;
            } else out[o++] = (unsigned char)s[i];
        }
    }
    *len = o;
    return out;
}
