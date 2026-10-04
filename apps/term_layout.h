/* apps/term_layout.h -- Terminal line wrapping and scrollback trim (pure). */
#ifndef CAREOS_TERM_LAYOUT_H
#define CAREOS_TERM_LAYOUT_H
#include "pure.h"

typedef i32 (*term_char_w_fn)(char c);            /* pixel advance of one char */
typedef struct { u32 off; u32 len; } term_vline_t; /* one visual line of the buffer */

/* Split buf[0..len) into visual lines at most max_w pixels wide: hard breaks
 * at '\n', soft breaks after the last space that fits, otherwise mid-word, and
 * always at least one character per line (so it always progresses). Writes up
 * to max_out lines (out may be NULL when max_out is 0) and returns the TOTAL
 * count, which is >= 1: an empty buffer is one empty line, and a trailing
 * '\n' starts a final empty line. */
u32 term_wrap(const char *buf, u32 len, i32 max_w, term_char_w_fn cw,
              term_vline_t *out, u32 max_out);

/* Rich lines: a logical line that starts with TERM_MARK is drawn by the
 * renderer as one block (prompt pills, help cards, a suggestion card). It is
 * never wrapped; it becomes one visual line spanning the whole logical line,
 * followed by rows-1 continuation lines (len == TERM_VL_CONT, off = the
 * block's start) so blocks scroll by whole rows like text. */
#define TERM_MARK     '\x1e'
#define TERM_SEP      '\x1f'
#define TERM_VL_CONT  0xFFFFFFFFu
typedef u32 (*term_rows_fn)(const char *line, u32 len, void *ctx);  /* >= 1 */
u32 term_wrap_rich(const char *buf, u32 len, i32 max_w, term_char_w_fn cw,
                   term_rows_fn rows, void *ctx, term_vline_t *out, u32 max_out);

/* If len > max, drop whole lines from the front until at most keep bytes are
 * left (a hard cut when one line is longer than keep). Returns the new length
 * and NUL-terminates; buf must hold len+1 bytes. */
u32 term_trim(char *buf, u32 len, u32 max, u32 keep);

#endif
