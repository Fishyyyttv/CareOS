/* apps/term_layout.c -- Terminal line wrapping and scrollback trim (pure). */
#include "term_layout.h"

u32 term_wrap(const char *buf, u32 len, i32 max_w, term_char_w_fn cw,
              term_vline_t *out, u32 max_out) {
    u32 n = 0, i = 0;
    if (max_w < 1) max_w = 1;
    for (;;) {
        u32 start = i, last_space = 0xFFFFFFFFu;
        i32 w = 0;
        while (i < len && buf[i] != '\n') {
            i32 c = cw(buf[i]);
            if (w + c > max_w && i > start) break;
            if (buf[i] == ' ') last_space = i;
            w += c; i++;
        }
        u32 end = i;
        bool soft = (i < len && buf[i] != '\n');
        if (soft && last_space != 0xFFFFFFFFu && last_space > start) { end = last_space + 1; i = end; }
        if (n < max_out) { out[n].off = start; out[n].len = end - start; }
        n++;
        if (i >= len) break;
        if (buf[i] == '\n') {
            i++;
            if (i == len) {                               /* trailing newline */
                if (n < max_out) { out[n].off = i; out[n].len = 0; }
                n++;
                break;
            }
        }
    }
    return n;
}

u32 term_trim(char *buf, u32 len, u32 max, u32 keep) {
    if (len <= max || keep >= len) return len;
    u32 cut = len - keep;
    while (cut < len && buf[cut - 1] != '\n') cut++;      /* advance to a line start */
    if (cut >= len) cut = len - keep;                     /* one giant line: hard cut */
    for (u32 k = cut; k < len; k++) buf[k - cut] = buf[k];
    buf[len - cut] = '\0';
    return len - cut;
}
