/* gui/session/session.h -- the v10 start screens (start-screens spec):
 * boot splash, login, sign up, lock, power menu and first-run onboarding.
 * Built only from the phase 1 kit (gui/ui.h, theme tokens, font roles). */
#ifndef CAREOS_SESSION_H
#define CAREOS_SESSION_H
#include "gui.h"
#include "ui.h"

/* ---- Exported by gui/gui.c ------------------------------------------------ */
void gui_wallpaper_draw(void);                 /* the selected desktop background */
void gui_desktop_fade_in(mouse_t *m);

/* ---- Boot splash (boot.c) ---------------------------------------------------
 * Runs the real remaining start-up work (wallpaper, icon cache, network
 * check) and shows its progress. fast = settings "fast boot". */
void session_boot(bool fast);

/* ---- Login / lock (login.c) ---------------------------------------------- */
/* Blocks until someone is signed in (or chose Guest). Lock mode shows the lock
 * screen first and only accepts the current user's password. Returns true
 * when a session should start; *created is set when the account was just made
 * through Sign up (onboarding then starts at step 2). */
bool session_login(mouse_t *m, bool lock_mode, bool *created);

/* ---- Sign up (signup.c) ---------------------------------------------------- */
/* True when an account was created and signed in. */
bool session_signup(mouse_t *m);

/* ---- Power menu (power.c) ---------------------------------------------------
 * Modal over the current screen. from_login hides Log out. Returns the choice;
 * Restart / Shut down do not return. */
typedef enum { POWER_CANCEL = 0, POWER_LOGOUT } power_choice_t;
power_choice_t session_power_menu(mouse_t *m, bool from_login);

/* ---- Onboarding (onboarding.c) --------------------------------------------- */
/* Runs the first-run steps for the signed-in user if they are not onboarded. */
void session_onboarding(mouse_t *m, bool from_signup);

/* ---- Shared pieces (common.c) ---------------------------------------------- */
#define AVATAR_COLORS 6
void   session_avatar(i32 cx, i32 cy, i32 r, u32 color_idx, const char *name, bool guest);
void   session_avatar_colors(u32 idx, u32 *c0, u32 *c1);
/* Pip lives here from phase 4; the start screens leave the space. */
void   session_pet_slot(rect_t r, u32 mood);
void   session_frame_end(const mouse_t *m);      /* cursor + flip + idle */
void   session_date_line(char *out, bool weekday_long);   /* "Saturday, October 3" */
void   session_time_line(char *out);                      /* "9:41" / "21:41" */

/* A one-line text field for the start screens. */
typedef struct {
    char buf[64];
    u32  len;
    bool secret;
    bool reveal;       /* secret shown in clear (eye toggle) */
} sfield_t;
bool   sfield_key(sfield_t *f, char c);           /* true if the key edited it */
void   sfield_clear(sfield_t *f);
void   sfield_text(const sfield_t *f, char *out, u32 cap);   /* masked if secret */
/* Draw a field: rounded, filled, 1.5 px border (focus = calm ring). */
void   session_field(rect_t r, const sfield_t *f, const char *placeholder, bool focused,
                     u32 fill, u32 border, u32 ink, u32 muted);

#endif
