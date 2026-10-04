/* apps/care_syntax.c -- Care / C line tokenizer (pure). */
#include "care_syntax.h"

static const char *const CARE_KW[] = { "var", "print", "if", "else", "while", "return", "func", NULL };
static const char *const C_KW[] = {
    "int", "void", "char", "return", "if", "else", "while", "for", "struct", "static",
    "const", "unsigned", "bool", "true", "false", "u8", "u16", "u32", "u64", "i32", "i64",
    "typedef", "enum", "sizeof", "break", "continue", "switch", "case", "default", "NULL", NULL
};

static bool is_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
static bool is_digit(char c) { return c >= '0' && c <= '9'; }
static bool streq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }

cs_lang_t care_syntax_lang(const char *name) {
    if (!name) return CS_LANG_PLAIN;
    const char *dot = NULL;
    for (const char *p = name; *p; p++) if (*p == '.') dot = p;
    if (!dot) return CS_LANG_PLAIN;
    if (streq(dot, ".cl")) return CS_LANG_CARE;
    if (streq(dot, ".c") || streq(dot, ".h")) return CS_LANG_C;
    return CS_LANG_PLAIN;
}

static bool is_kw(cs_lang_t lang, const char *s, u32 n) {
    const char *const *kw = lang == CS_LANG_CARE ? CARE_KW : C_KW;
    for (; *kw; kw++) {
        u32 k = 0;
        while (k < n && (*kw)[k] == s[k]) k++;
        if (k == n && (*kw)[k] == '\0') return true;
    }
    return false;
}

static void emit(cs_tok_t *out, u32 max, u32 *n, u32 start, u32 len, u8 kind) {
    if (len == 0) return;
    if (*n && out && *n <= max && out[*n - 1].kind == kind &&
        out[*n - 1].start + out[*n - 1].len == start) {   /* merge a run */
        out[*n - 1].len = (u16)(out[*n - 1].len + len);
        return;
    }
    if (out && *n < max) { out[*n].start = (u16)start; out[*n].len = (u16)len; out[*n].kind = kind; }
    (*n)++;
}

u32 care_syntax_line(cs_lang_t lang, const char *s, u32 len, cs_tok_t *out, u32 max) {
    u32 n = 0, i = 0;
    if (lang == CS_LANG_PLAIN) { emit(out, max, &n, 0, len, CS_TEXT); return n; }
    while (i < len) {
        char c = s[i];
        u32 st = i;
        bool line_comment = (lang == CS_LANG_CARE && c == '#') ||
                            (c == '/' && i + 1 < len && s[i + 1] == '/');
        if (line_comment) { emit(out, max, &n, st, len - st, CS_COMMENT); break; }
        if (lang == CS_LANG_C && c == '#') {                 /* preprocessor word */
            i++;
            while (i < len && is_alpha(s[i])) i++;
            emit(out, max, &n, st, i - st, CS_KEYWORD);
            continue;
        }
        if (c == '"' || c == '\'') {
            i++;
            while (i < len && s[i] != c) { if (s[i] == '\\' && i + 1 < len) i++; i++; }
            if (i < len) i++;                                 /* closing quote */
            emit(out, max, &n, st, i - st, CS_STRING);
            continue;
        }
        if (is_digit(c)) {
            while (i < len && (is_digit(s[i]) || is_alpha(s[i]) || s[i] == '.')) i++;
            emit(out, max, &n, st, i - st, CS_NUMBER);
            continue;
        }
        if (is_alpha(c)) {
            while (i < len && (is_alpha(s[i]) || is_digit(s[i]))) i++;
            emit(out, max, &n, st, i - st, is_kw(lang, s + st, i - st) ? CS_KEYWORD : CS_TEXT);
            continue;
        }
        if (c == ' ' || c == '\t') {
            while (i < len && (s[i] == ' ' || s[i] == '\t')) i++;
            emit(out, max, &n, st, i - st, CS_TEXT);
            continue;
        }
        i++;
        emit(out, max, &n, st, 1, CS_PUNCT);
    }
    return n;
}
