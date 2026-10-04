/* kernel/account_rules.c -- see account_rules.h. */
#include "account_rules.h"

bool username_valid(const char *name) {
    if (!name || !name[0]) return false;
    u32 n = 0;
    for (; name[n]; n++) {
        char c = name[n];
        bool ok = (c >= 'a' && c <= 'z') || (n > 0 && ((c >= '0' && c <= '9') || c == '_' || c == '-'));
        if (!ok || n >= 24) return false;
    }
    if (n == 5 && kstrncmp(name, "guest", 5) == 0) return false;
    if (n == 4 && kstrncmp(name, "root", 4) == 0) return false;
    return true;
}

u32 password_strength(const char *pw) {
    if (!pw) return PW_WEAK;
    u32 len = 0;
    bool up = false, lo = false, dig = false, sym = false;
    for (; pw[len]; len++) {
        char c = pw[len];
        if (c >= 'A' && c <= 'Z') up = true;
        else if (c >= 'a' && c <= 'z') lo = true;
        else if (c >= '0' && c <= '9') dig = true;
        else sym = true;
    }
    if (len < 8 || !up || !lo || !dig) return PW_WEAK;
    if (len >= 12 || (sym && len >= 10)) return PW_STRONG;
    return PW_OK;
}

u32 password_meter_bars(const char *pw) {
    if (!pw || !pw[0]) return 0;
    u32 s = password_strength(pw);
    if (s == PW_WEAK) return 1;
    if (s == PW_OK) return 2;
    u32 len = 0;
    while (pw[len]) len++;
    return len >= 16 ? 4 : 3;
}

u32 onboard_first_step(bool from_signup) { return from_signup ? 1u : 0u; }

u32 onboard_next(u32 step) { return step >= ONBOARD_STEPS - 1 ? ONBOARD_DONE : step + 1; }

u32 onboard_back(u32 step, u32 first) {
    if (step >= ONBOARD_DONE) return ONBOARD_STEPS - 1;
    return step > first ? step - 1 : first;
}

u32 onboard_resume(u32 stored_step, bool onboarded) {
    if (onboarded) return ONBOARD_DONE;
    return stored_step < ONBOARD_STEPS ? stored_step : 0u;
}

bool auto_theme_dark(u32 local_hour) { return local_hour < 7u || local_hour >= 19u; }
