/* apps/http_parse.c -- HTTP/1.x response helpers for the browser (pure). */
#include "http_parse.h"

static char lc(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
static int hexv(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = lc(c);
    return (c >= 'a' && c <= 'f') ? c - 'a' + 10 : -1;
}
static bool starts(const char *s, const char *p) {
    while (*p) { if (*s++ != *p++) return false; }
    return true;
}

int http_status_code(const char *r, u32 len) {
    if (len < 12 || !starts(r, "HTTP/")) return 0;
    u32 i = 5;
    while (i < len && r[i] != ' ') i++;
    while (i < len && r[i] == ' ') i++;
    if (i + 3 > len) return 0;
    int code = 0;
    for (u32 k = 0; k < 3; k++) {
        if (r[i + k] < '0' || r[i + k] > '9') return 0;
        code = code * 10 + (r[i + k] - '0');
    }
    return code >= 100 ? code : 0;
}

u32 http_body_offset(const char *r, u32 len) {
    for (u32 i = 0; i + 3 < len; i++)
        if (r[i] == '\r' && r[i + 1] == '\n' && r[i + 2] == '\r' && r[i + 3] == '\n') return i + 4;
    for (u32 i = 0; i + 1 < len; i++)
        if (r[i] == '\n' && r[i + 1] == '\n') return i + 2;
    return 0;
}

bool http_header(const char *r, u32 len, const char *name, char *out, u32 out_max) {
    if (!out_max) return false;
    u32 end = http_body_offset(r, len);
    if (!end) end = len;
    u32 nl = (u32)kstrlen(name);
    u32 i = 0;
    while (i < end && r[i] != '\n') i++;                 /* skip the status line */
    while (++i < end) {
        u32 ls = i;
        while (i < end && r[i] != '\n') i++;
        if (i - ls > nl && r[ls + nl] == ':') {
            u32 k = 0;
            while (k < nl && lc(r[ls + k]) == lc(name[k])) k++;
            if (k == nl) {
                u32 v = ls + nl + 1;
                while (v < i && (r[v] == ' ' || r[v] == '\t')) v++;
                u32 ve = i;
                while (ve > v && (r[ve - 1] == '\r' || r[ve - 1] == ' ')) ve--;
                u32 n = ve - v;
                if (n >= out_max) n = out_max - 1;
                kmemcpy(out, r + v, n); out[n] = '\0';
                return true;
            }
        }
    }
    return false;
}

bool http_is_chunked(const char *r, u32 len) {
    char v[64];
    if (!http_header(r, len, "Transfer-Encoding", v, sizeof v)) return false;
    for (u32 i = 0; v[i]; i++) v[i] = lc(v[i]);
    for (u32 i = 0; v[i]; i++) if (starts(v + i, "chunked")) return true;
    return false;
}

u32 http_dechunk(char *b, u32 len) {
    u32 in = 0, out = 0;
    while (in < len) {
        u64 sz = 0; int digits = 0;
        while (in < len && hexv(b[in]) >= 0) {
            if (sz < 0x100000000ULL) sz = sz * 16u + (u64)hexv(b[in]);
            in++; digits++;
        }
        if (!digits) break;
        while (in < len && b[in] != '\n') in++;          /* chunk extensions + CR */
        if (in < len) in++;
        if (sz == 0) break;
        if (sz > (u64)(len - in)) sz = len - in;         /* truncated / absurd final chunk */
        for (u64 i = 0; i < sz; i++) b[out++] = b[in++];
        if (in < len && b[in] == '\r') in++;
        if (in < len && b[in] == '\n') in++;
    }
    b[out] = '\0';
    return out;
}

/* Bounded string builder: appends while there is room, always terminated. */
typedef struct { char *p; u32 n, max; } sb_t;
static void sb_add(sb_t *s, const char *t, u32 tl) {
    for (u32 i = 0; i < tl && s->n + 1 < s->max; i++) s->p[s->n++] = t[i];
    s->p[s->n] = '\0';
}
static void sb_str(sb_t *s, const char *t) { sb_add(s, t, (u32)kstrlen(t)); }

void http_url_resolve(const char *base, const char *href, char *out, u32 max) {
    if (!max) return;
    sb_t s = { out, 0, max };
    out[0] = '\0';
    if (!href || !*href) { sb_str(&s, base); return; }
    if (starts(href, "http://") || starts(href, "https://")) { sb_str(&s, href); return; }

    const char *scheme = starts(base, "https://") ? "https" : "http";
    const char *bh = base;
    if (starts(base, "https://")) bh = base + 8;
    else if (starts(base, "http://")) bh = base + 7;
    u32 hl = 0;
    while (bh[hl] && bh[hl] != '/') hl++;
    const char *bpath = bh + hl;
    if (!*bpath) bpath = "/";

    sb_str(&s, scheme);
    if (href[0] == '/' && href[1] == '/') { sb_str(&s, ":"); sb_str(&s, href); return; }
    sb_str(&s, "://");
    sb_add(&s, bh, hl);
    if (href[0] == '/') { sb_str(&s, href); return; }
    if (href[0] == '?') {                                 /* query on the current path */
        u32 pl = 0; while (bpath[pl] && bpath[pl] != '?') pl++;
        sb_add(&s, bpath, pl); sb_str(&s, href); return;
    }
    u32 last = 0;                                         /* relative to the base directory */
    for (u32 i = 0; bpath[i] && bpath[i] != '?'; i++) if (bpath[i] == '/') last = i;
    sb_add(&s, bpath, last + 1);
    sb_str(&s, href);
}
