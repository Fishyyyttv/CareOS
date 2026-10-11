/* kernel/kfmt.h -- printf-style formatting (pure, host-tested).
 *
 * Supports flags - 0 + space #, width and precision (digits or *), length
 * modifiers hh h l ll z j t, and conversions d i u o x X p c s %. Floating
 * point (no FPU use in the kernel) consumes its argument and prints "?".
 * snprintf semantics: writes at most size-1 chars plus a NUL (when size > 0)
 * and returns the length the full output would have had. */
#ifndef CAREOS_KFMT_H
#define CAREOS_KFMT_H
#include "pure.h"
#include <stdarg.h>

int kfmt_vformat(char *out, size_t size, const char *fmt, va_list ap);

#endif
