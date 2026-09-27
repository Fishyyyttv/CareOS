/* gui/textbuf.h -- geometric growth for heap text buffers (pure; host-tested). */
#ifndef CAREOS_TEXTBUF_H
#define CAREOS_TEXTBUF_H
#include "pure.h"

/* Grow *buf (keeping its contents) so that *cap >= need, never beyond max.
 * A freshly allocated buffer starts as "". Returns false -- leaving the
 * buffer untouched -- if need > max or the allocation fails. */
bool tb_reserve(char **buf, u32 *cap, u32 need, u32 max);

#endif
