/* gui/span.c -- subtract a hole from a horizontal span (pure). */
#include "span.h"

u32 span_minus(i32 x, i32 len, i32 hx0, i32 hx1, i32 ox[2], i32 ol[2]) {
    if (len <= 0) return 0;
    i32 x1 = x + len;
    if (hx1 <= hx0 || hx1 <= x || hx0 >= x1) { ox[0] = x; ol[0] = len; return 1; }
    u32 n = 0;
    if (hx0 > x)  { ox[n] = x;   ol[n] = hx0 - x;  n++; }
    if (hx1 < x1) { ox[n] = hx1; ol[n] = x1 - hx1; n++; }
    return n;
}
