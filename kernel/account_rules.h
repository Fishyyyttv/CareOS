/* kernel/account_rules.h -- pure rules behind the start screens (host-tested):
 * username validation, the password strength meter, the onboarding step
 * machine and the automatic light/dark rule. */
#ifndef CAREOS_ACCOUNT_RULES_H
#define CAREOS_ACCOUNT_RULES_H
#include "pure.h"

/* 1..24 of a-z 0-9 _ -, starting with a letter; "guest" and "root" are taken. */
bool username_valid(const char *name);

/* Meter band: 0 weak (fails the account policy: 8+ chars with upper, lower and
 * a digit), 1 OK (meets it), 2 strong (meets it and is 12+ chars, or 10+ with
 * a symbol). password_meter_bars() maps a password to 0..4 lit bars. */
#define PW_WEAK   0u
#define PW_OK     1u
#define PW_STRONG 2u
u32  password_strength(const char *pw);
u32  password_meter_bars(const char *pw);

/* Onboarding: steps 0 Welcome, 1 Get online, 2 Pick your vibe, 3 All set;
 * ONBOARD_DONE once finished. Sign-up starts at step 1 (the account itself
 * was step one of its rail). */
#define ONBOARD_STEPS 4u
#define ONBOARD_DONE  ONBOARD_STEPS
u32  onboard_first_step(bool from_signup);
u32  onboard_next(u32 step);
u32  onboard_back(u32 step, u32 first);
u32  onboard_resume(u32 stored_step, bool onboarded);   /* ONBOARD_DONE = skip */

/* Theme mode "auto": dark before 07:00 and from 19:00 local time. */
bool auto_theme_dark(u32 local_hour);

#endif
