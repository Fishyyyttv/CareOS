/* apps/calc_engine.h -- Calculator arithmetic (pure, double precision).
 *
 * Uses real floating point, so it may only be compiled into objects built with
 * FP_CFLAGS (see Makefile) -- never into kernel-flag (-mno-sse) code. The GUI
 * keeps a calc_t inside window_t as opaque storage (window_t.calc_state). */
#ifndef CAREOS_CALC_ENGINE_H
#define CAREOS_CALC_ENGINE_H
#include "pure.h"

#define CALC_MAX_DIGITS 15

typedef struct {
    double acc;         /* left operand / running result            */
    char   op;          /* pending operator: + - * / or 0            */
    bool   fresh;       /* next digit starts a new entry             */
    bool   error;       /* showing "Error" (divide by zero, overflow) */
    char   disp[32];    /* what the display shows                    */
    char   expr[48];    /* small history line, e.g. "6 /"            */
} calc_t;

void   calc_reset(calc_t *c);
/* One keypad key: '0'-'9' '.' '+' '-' '*' '/' '=' '%'
 * 'n' (+/-), 'b' (backspace), 'c' (clear). Unknown keys are ignored. */
void   calc_key(calc_t *c, char k);
/* Format like a desk calculator: integers exactly below 1e12, otherwise
 * 10 significant digits, scientific outside [1e-6, 1e12). */
void   calc_format(double v, char *out, u32 cap);
double calc_parse(const char *s);

#endif
