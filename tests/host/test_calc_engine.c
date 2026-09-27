/* tests/host/test_calc_engine.c -- the Calculator must do real (floating
 * point) arithmetic: 2/3 used to print 0 because every value was an i32. */
#include "pure.h"
#include "calc_engine.h"
#include <assert.h>

static calc_t C;

/* Feed keys the way the keypad does: digits, '.', + - * /, '=', 'n' (+/-),
 * '%', 'b' (backspace), 'c' (clear). */
static const char *run(const char *keys) {
    calc_reset(&C);
    for (const char *k = keys; *k; k++) calc_key(&C, *k);
    return C.disp;
}

#define EXPECT(keys, want) do { const char *got = run(keys); \
    if (strcmp(got, want)) { printf("FAIL %-14s got '%s' want '%s'\n", keys, got, want); return 1; } } while (0)

int main(void) {
    EXPECT("2/3=",        "0.6666666667");
    EXPECT("10/4=",       "2.5");
    EXPECT("1+2=",        "3");
    EXPECT("0.1+0.2=",    "0.3");
    EXPECT("1/8=",        "0.125");
    EXPECT("2+3*4=",      "20");          /* immediate execution, like the old calc */
    EXPECT("7n",          "-7");
    EXPECT("50%",         "0.5");
    EXPECT("1.5.2",       "1.52");        /* second '.' ignored */
    EXPECT("12b",         "1");
    EXPECT("1b",          "0");
    EXPECT("0n",          "0");           /* no "-0" */
    EXPECT("3-5=",        "-2");
    EXPECT("1/3*3=",      "1");
    EXPECT("999999*999999=", "999998000001");
    EXPECT("1/1000000=",  "0.000001");
    EXPECT("1/3/1000000=", "3.333333333e-7");
    EXPECT("99999999*99999999=", "9.9999998e+15");
    EXPECT("5/0=",        "Error");
    assert(C.error);
    EXPECT("5/0=4",       "4");           /* typing after Error starts fresh */
    EXPECT("1234567890123456789", "123456789012345");  /* 15-digit entry cap */
    EXPECT("2/3=c",       "0");
    EXPECT("6/",          "6");           /* number stays shown after an operator */
    assert(!strcmp(C.expr, "6 /"));
    puts("calc_engine OK");
    return 0;
}
