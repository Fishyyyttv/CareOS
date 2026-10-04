/* apps/notes_md.c -- Notes Markdown subset (pure). */
#include "notes_md.h"

static bool starts(const char *s, u32 len, const char *p) {
    u32 i = 0;
    for (; p[i]; i++) if (i >= len || s[i] != p[i]) return false;
    return true;
}

nl_kind_t notes_line_kind(const char *s, u32 len, u32 *off) {
    u32 o = 0;
    nl_kind_t k = NL_PARA;
    bool blank = true;
    for (u32 i = 0; i < len; i++) if (s[i] != ' ' && s[i] != '\t') { blank = false; break; }
    if (blank) k = NL_BLANK;
    else if (starts(s, len, "- [ ] ") || (len == 5 && starts(s, len, "- [ ]"))) { k = NL_CHECK; o = len < 6 ? len : 6; }
    else if (starts(s, len, "- [x] ") || starts(s, len, "- [X] ") ||
             (len == 5 && (starts(s, len, "- [x]") || starts(s, len, "- [X]")))) { k = NL_CHECKED; o = len < 6 ? len : 6; }
    else if (starts(s, len, "## ")) { k = NL_H2; o = 3; }
    else if (starts(s, len, "# "))  { k = NL_H1; o = 2; }
    else if (starts(s, len, "> "))  { k = NL_QUOTE; o = 2; }
    else if (starts(s, len, "- ") || starts(s, len, "* ")) { k = NL_BULLET; o = 2; }
    if (off) *off = o;
    return k;
}

static u32 line_end(const char *buf, u32 len, u32 i) { while (i < len && buf[i] != '\n') i++; return i; }

bool notes_toggle_check(char *buf, u32 len, u32 ls) {
    if (ls >= len) return false;
    u32 le = line_end(buf, len, ls);
    nl_kind_t k = notes_line_kind(buf + ls, le - ls, NULL);
    if (k == NL_CHECK)   { buf[ls + 3] = 'x'; return true; }
    if (k == NL_CHECKED) { buf[ls + 3] = ' '; return true; }
    return false;
}

u32 notes_toggle_prefix(char *buf, u32 len, u32 cap, u32 ls, const char *prefix) {
    u32 pl = 0; while (prefix[pl]) pl++;
    if (ls > len) return len;
    if (starts(buf + ls, len - ls, prefix)) {
        for (u32 i = ls; i + pl <= len; i++) buf[i] = buf[i + pl];
        len -= pl;
        if (len < cap) buf[len] = '\0';
        return len;
    }
    if (len + pl + 1 > cap) return len;
    for (u32 i = len + 1; i-- > ls;) { if (i + pl < cap) buf[i + pl] = (i < len) ? buf[i] : '\0'; }
    for (u32 i = 0; i < pl; i++) buf[ls + i] = prefix[i];
    len += pl;
    buf[len] = '\0';
    return len;
}

/* Copy s[0..n) without inline ** / * markers, trimmed, into out. */
static void plain(const char *s, u32 n, char *out, u32 cap) {
    u32 o = 0;
    while (n && (*s == ' ' || *s == '\t')) { s++; n--; }
    for (u32 i = 0; i < n && o + 1 < cap; i++) if (s[i] != '*') out[o++] = s[i];
    while (o && out[o - 1] == ' ') o--;
    out[o] = '\0';
}

/* The i-th (0-based) non-blank line; false when there is none. */
static bool nth_line(const char *buf, u32 len, u32 want, u32 *ls, u32 *le) {
    u32 i = 0, seen = 0;
    while (i < len) {
        u32 e = line_end(buf, len, i);
        if (notes_line_kind(buf + i, e - i, NULL) != NL_BLANK) {
            if (seen == want) { *ls = i; *le = e; return true; }
            seen++;
        }
        i = e + 1;
    }
    return false;
}

void notes_title(const char *buf, u32 len, char *out, u32 cap) {
    u32 ls, le, off;
    out[0] = '\0';
    if (nth_line(buf, len, 0, &ls, &le)) {
        notes_line_kind(buf + ls, le - ls, &off);
        plain(buf + ls + off, le - ls - off, out, cap);
    }
    if (!out[0]) { const char *u = "Untitled"; u32 i = 0; for (; u[i] && i + 1 < cap; i++) out[i] = u[i]; out[i] = '\0'; }
}

void notes_snippet(const char *buf, u32 len, char *out, u32 cap) {
    u32 ls, le, off;
    out[0] = '\0';
    if (nth_line(buf, len, 1, &ls, &le)) {
        notes_line_kind(buf + ls, le - ls, &off);
        plain(buf + ls + off, le - ls - off, out, cap);
    }
}

void notes_file_name(const char *title, char *out, u32 cap) {
    u32 o = 0;
    if (cap < 12) { if (cap) out[0] = '\0'; return; }
    for (const char *p = title; *p && o + 4 < cap; p++) {
        char c = *p;
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                  c == ' ' || c == '-' || c == '_';
        if (!ok) continue;
        if (c == ' ' && (o == 0 || out[o - 1] == ' ')) continue;   /* trim / squeeze */
        out[o++] = c;
    }
    while (o && out[o - 1] == ' ') o--;
    if (!o) { const char *u = "Untitled"; for (; u[o]; o++) out[o] = u[o]; }
    out[o++] = '.'; out[o++] = 'm'; out[o++] = 'd'; out[o] = '\0';
}

static void span(nspan_t *out, u32 max, u32 *n, u32 st, u32 len, u8 style) {
    if (!len) return;
    if (out && *n < max) { out[*n].start = (u16)st; out[*n].len = (u16)len; out[*n].style = style; }
    (*n)++;
}

/* Index of the closing marker (length ml) at or after i, or len. */
static u32 find_close(const char *s, u32 len, u32 i, u32 ml) {
    for (; i + ml <= len; i++) {
        if (s[i] != '*') continue;
        if (ml == 2 && s[i + 1] == '*') return i;
        if (ml == 1 && (i + 1 >= len || s[i + 1] != '*')) return i;
        if (ml == 1) i++;                       /* skip a "**" inside italics */
    }
    return len;
}

u32 notes_spans(const char *s, u32 len, nspan_t *out, u32 max) {
    u32 n = 0, i = 0, run = 0;
    while (i < len) {
        if (s[i] == '*') {
            u32 ml = (i + 1 < len && s[i + 1] == '*') ? 2 : 1;
            u32 c = find_close(s, len, i + ml, ml);
            if (c < len && c > i + ml) {
                span(out, max, &n, run, i - run, 0);
                span(out, max, &n, i + ml, c - i - ml, ml == 2 ? NS_BOLD : NS_ITALIC);
                i = c + ml; run = i;
                continue;
            }
            i += ml;                           /* unclosed: plain text */
            continue;
        }
        i++;
    }
    span(out, max, &n, run, len - run, 0);
    return n;
}
