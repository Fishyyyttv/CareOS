#include "pure.h"
#include "account_rules.h"
#include <assert.h>

int main(void) {
    /* usernames */
    assert(username_valid("fishfish"));
    assert(username_valid("a"));
    assert(username_valid("dev_ops-2"));
    assert(username_valid("abcdefghijklmnopqrstuvwx"));          /* 24 */
    assert(!username_valid("abcdefghijklmnopqrstuvwxy"));        /* 25 */
    assert(!username_valid(""));
    assert(!username_valid(NULL));
    assert(!username_valid("Fish"));                              /* uppercase */
    assert(!username_valid("9lives"));                            /* must start with a letter */
    assert(!username_valid("_x"));
    assert(!username_valid("fish fish"));
    assert(!username_valid("guest") && !username_valid("root"));
    assert(username_valid("guests") && username_valid("rooted"));

    /* strength bands follow the account policy */
    assert(password_strength("") == PW_WEAK);
    assert(password_strength("careful1") == PW_WEAK);             /* no upper */
    assert(password_strength("Careful1") == PW_OK);               /* 8, U+l+d */
    assert(password_strength("Careful12") == PW_OK);
    assert(password_strength("Careful1234!") == PW_STRONG);
    assert(password_strength("Careful12!") == PW_STRONG);         /* 10 + symbol */
    assert(password_strength("CarefulOtter12") == PW_STRONG);     /* 14 */
    assert(password_meter_bars("") == 0);
    assert(password_meter_bars("abc") == 1);
    assert(password_meter_bars("Careful1") == 2);
    assert(password_meter_bars("CarefulOtter12") == 3);
    assert(password_meter_bars("CarefulOtterRiver12") == 4);

    /* onboarding: forward, back (never before the first step), done, resume */
    u32 s = onboard_first_step(false);
    assert(s == 0);
    s = onboard_next(s); assert(s == 1);
    s = onboard_next(s); assert(s == 2);
    s = onboard_back(s, 0); assert(s == 1);
    s = onboard_next(onboard_next(s)); assert(s == 3);
    assert(onboard_next(3) == ONBOARD_DONE);
    assert(onboard_first_step(true) == 1);
    assert(onboard_back(1, 1) == 1);                              /* sign-up can't go back to Welcome */
    assert(onboard_resume(2, false) == 2);
    assert(onboard_resume(2, true) == ONBOARD_DONE);
    assert(onboard_resume(9, false) == 0);                        /* corrupt step restarts */

    /* auto theme */
    assert(auto_theme_dark(0) && auto_theme_dark(6));
    assert(!auto_theme_dark(7) && !auto_theme_dark(12) && !auto_theme_dark(18));
    assert(auto_theme_dark(19) && auto_theme_dark(23));

    puts("account_rules OK");
    return 0;
}
