/* tests/host/test_term_layout.c -- Terminal wrapping and scrollback trim. */
#include "pure.h"
#include "term_layout.h"
#include <assert.h>

static i32 mono10(char c){ (void)c; return 10; }
static term_vline_t L[64];
#define S(i) (buf + L[i].off)

static void test_word_wrap(void){
    const char *buf = "hello world";
    u32 n = term_wrap(buf, 11, 60, mono10, L, 64);
    assert(n == 2);
    assert(L[0].len == 6 && !strncmp(S(0), "hello ", 6));
    assert(L[1].len == 5 && !strncmp(S(1), "world", 5));
}
static void test_hard_wrap_long_word(void){
    const char *buf = "abcdefghij";
    assert(term_wrap(buf, 10, 30, mono10, L, 64) == 4);
    assert(L[3].len == 1 && buf[L[3].off] == 'j');
}
static void test_newlines(void){
    const char *buf = "a\nb\n";
    assert(term_wrap(buf, 4, 100, mono10, L, 64) == 3);
    assert(L[2].len == 0);
}
static void test_empty(void){
    assert(term_wrap("", 0, 100, mono10, L, 64) == 1 && L[0].len == 0);
}
static void test_wrap_zero_width(void){
    const char *buf = "abc";
    assert(term_wrap(buf, 3, 0, mono10, L, 64) == 3);   /* one char per line, no hang */
    assert(term_wrap(buf, 3, -50, mono10, L, 64) == 3);
}
static void test_max_out_counts_all(void){
    const char *buf = "a\nb\nc\nd";
    assert(term_wrap(buf, 7, 100, mono10, L, 2) == 4);
    assert(term_wrap(buf, 7, 100, mono10, NULL, 0) == 4);  /* counting only */
}
static void test_blank_lines_and_spaces(void){
    const char *buf = "\n\n   x";
    assert(term_wrap(buf, 6, 20, mono10, L, 64) == 4);     /* "", "", "  ", " x" */
    for (u32 i = 0; i < 4; i++) assert(L[i].off + L[i].len <= 6);
}
static void test_trim_noop(void){
    char b[] = "x\ny\n"; assert(term_trim(b, 4, 10, 5) == 4);
}
static void test_trim_line_boundary(void){
    char b[] = "aaaa\nbbbb\ncccc\n";                         /* 15 bytes */
    u32 n = term_trim(b, 15, 12, 10);
    assert(n == 10 && !strcmp(b, "bbbb\ncccc\n"));
}
static void test_trim_single_giant_line(void){
    char b[64]; memset(b, 'z', 40); b[40] = 0;
    u32 n = term_trim(b, 40, 30, 20);
    assert(n == 20 && strlen(b) == 20);
}
static void test_trim_keep_ge_len(void){
    char b[] = "abc"; assert(term_trim(b, 3, 2, 5) == 3);   /* nothing sensible to drop */
}
static u32 rows3(const char *line, u32 len, void *ctx){
    (void)ctx; return (len >= 2 && line[1] == 'H') ? 3 : 1;
}
static void test_rich_blocks(void){
    /* text, a 3-row block, a long 1-row block (never wrapped), text */
    const char *buf = "ab\n\x1eH\n\x1ePxxxxxxxxxxxxxxxx\ncd";
    u32 len = (u32)strlen(buf);
    u32 n = term_wrap_rich(buf, len, 50, mono10, rows3, NULL, L, 64);
    assert(n == 6);
    assert(L[0].len == 2);
    assert(L[1].off == 3 && L[1].len == 2);
    assert(L[2].len == TERM_VL_CONT && L[2].off == 3);
    assert(L[3].len == TERM_VL_CONT);
    assert(L[4].off == 6 && L[4].len == 18);
    assert(L[5].len == 2 && !strncmp(S(5), "cd", 2));
    /* without a rows callback a marker is ordinary text */
    assert(term_wrap_rich(buf, len, 50, mono10, NULL, NULL, L, 64) > 6);
    /* a marker mid-line is plain text */
    assert(term_wrap_rich("a\x1eH", 3, 100, mono10, rows3, NULL, L, 64) == 1);
    /* a block at the very end followed by a newline keeps the trailing line */
    assert(term_wrap_rich("\x1eH\n", 3, 100, mono10, rows3, NULL, L, 64) == 4);
}
int main(void){
    test_rich_blocks();
    test_word_wrap(); test_hard_wrap_long_word(); test_newlines(); test_empty();
    test_wrap_zero_width(); test_max_out_counts_all(); test_blank_lines_and_spaces();
    test_trim_noop(); test_trim_line_boundary(); test_trim_single_giant_line();
    test_trim_keep_ge_len();
    puts("term_layout OK"); return 0;
}
