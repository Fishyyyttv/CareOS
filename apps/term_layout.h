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

/* If len > max, drop whole lines from the front until at most keep bytes are
 * left (a hard cut when one line is longer than keep). Returns the new length
 * and NUL-terminates; buf must hold len+1 bytes. */
u32 term_trim(char *buf, u32 len, u32 max, u32 keep);

#endif
