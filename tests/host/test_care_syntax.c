/* tests/host/test_care_syntax.c -- Care / C line tokenizer. */
#include "pure.h"
#include "care_syntax.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static cs_tok_t T[64];

/* Tokens cover the line exactly, in order. */
static void covers(const char *s, u32 n) {
    u32 pos = 0;
    for (u32 i = 0; i < n; i++) { assert(T[i].start == pos); pos += T[i].len; }
    assert(pos == strlen(s));
}

static u32 run(cs_lang_t lang, const char *s) {
    u32 n = care_syntax_line(lang, s, (u32)strlen(s), T, 64);
    covers(s, n);
    return n;
}

static bool has(const char *s, u32 n, const char *text, u8 kind) {
    for (u32 i = 0; i < n; i++)
        if (T[i].kind == kind && T[i].len == strlen(text) && !strncmp(s + T[i].start, text, T[i].len)) return true;
    return false;
}

int main(void) {
    const char *a = "var greeting = \"Hello, World!\";";
    u32 n = run(CS_LANG_CARE, a);
    assert(has(a, n, "var", CS_KEYWORD));
    assert(has(a, n, "\"Hello, World!\"", CS_STRING));
    assert(!has(a, n, "greeting", CS_KEYWORD));

    const char *b = "while (x > 0) { print x; x = x - 1; }";
    n = run(CS_LANG_CARE, b);
    assert(has(b, n, "while", CS_KEYWORD) && has(b, n, "print", CS_KEYWORD));
    assert(has(b, n, "0", CS_NUMBER) && has(b, n, "1", CS_NUMBER));

    const char *c = "print 5 # count down";
    n = run(CS_LANG_CARE, c);
    assert(has(c, n, "# count down", CS_COMMENT));

    /* keywords only as whole words */
    const char *d = "variable printer iffy";
    n = run(CS_LANG_CARE, d);
    assert(n == 1 && T[0].kind == CS_TEXT);              /* one merged text run */

    /* escaped quote stays inside the string; an unterminated string ends the line */
    const char *e = "print \"a\\\"b\" + \"open";
    n = run(CS_LANG_CARE, e);
    assert(has(e, n, "\"a\\\"b\"", CS_STRING) && has(e, n, "\"open", CS_STRING));

    /* C flavour */
    const char *f = "#include \"kernel.h\" // hi";
    n = run(CS_LANG_C, f);
    assert(has(f, n, "#include", CS_KEYWORD) && has(f, n, "\"kernel.h\"", CS_STRING) && has(f, n, "// hi", CS_COMMENT));
    const char *g = "static u32 x = 0x1F;";
    n = run(CS_LANG_C, g);
    assert(has(g, n, "static", CS_KEYWORD) && has(g, n, "u32", CS_KEYWORD) && has(g, n, "0x1F", CS_NUMBER));
    /* '#' is not a comment in C, and var is not a C keyword */
    const char *h = "var";
    n = run(CS_LANG_C, h);
    assert(n == 1 && T[0].kind == CS_TEXT);

    /* plain text is one token; empty lines have none */
    assert(run(CS_LANG_PLAIN, "var x = 1") == 1 && T[0].kind == CS_TEXT);
    assert(care_syntax_line(CS_LANG_CARE, "", 0, T, 64) == 0);
    /* max caps writes, count is still total */
    assert(care_syntax_line(CS_LANG_CARE, a, (u32)strlen(a), T, 2) > 2);

    assert(care_syntax_lang("preview.cl") == CS_LANG_CARE);
    assert(care_syntax_lang("gui.c") == CS_LANG_C && care_syntax_lang("x.h") == CS_LANG_C);
    assert(care_syntax_lang("notes.md") == CS_LANG_PLAIN && care_syntax_lang("Makefile") == CS_LANG_PLAIN);
    assert(care_syntax_lang("archive.cl.bak") == CS_LANG_PLAIN);
    puts("care_syntax OK");
    return 0;
}
