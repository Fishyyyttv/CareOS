/* gui/span.h -- subtract a hole from a horizontal span (pure). */
#ifndef CAREOS_SPAN_H
#define CAREOS_SPAN_H
#include "pure.h"

/* Parts of [x, x+len) outside [hx0, hx1) into ox/ol; returns 0, 1 or 2. */
u32 span_minus(i32 x, i32 len, i32 hx0, i32 hx1, i32 ox[2], i32 ol[2]);

#endif
