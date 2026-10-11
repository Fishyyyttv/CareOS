/* gui/session/signup.c -- Sign up (Signup artboard).
 *
 * Drawn light whatever the system theme, over Care Day, as the artboard is.
 * Left: the CareOS panel with the three-step rail. Right: avatar colour, name,
 * username (validated live), password (show/hide, strength meter), optional
 * hint. Creating the account signs it in; onboarding continues at step 2.
 * The artboard's "Make this the administrator account" box is left out: at
 * sign-up nobody is signed in, and an administrator always exists (root), so
 * it could never be offered. Admins promote accounts in the Users app. */
#include "kernel.h"
#include "session/session.h"
#include "glass.h"
#include "care_wallpaper.h"
#include "../../kernel/account_rules.h"

/* The artboard's light palette, fixed. */
#define C_INK     0x161A2Bu
#define C_LABEL   0x4A5168u
#define C_MUTED   0x6A7189u
#define C_BORDER  0xE1E4EEu
#define C_FILL    0xF5F6FAu
#define C_FOCUS   0x4F5EF2u
#define C_OKTEXT  0x1F7A4Du
#define C_OKBAR   0x22A564u
#define C_BAD     0xE5484Du
#define C_WARN    0xD98A06u

#define CARD_W 1080
#define CARD_H 680
#define LEFT_W 440

enum { F_NAME, F_USER, F_PASS, F_HINT, F_COUNT };

typedef struct {
    sfield_t f[F_COUNT];
    u32      focus;
    u32      color;
    char     msg[96];
    u32      *wall;          /* Care Day, screen-sized */
    u32      *panel;         /* the left panel, (LEFT_W + 32) x CARD_H */
} signup_t;

typedef struct {
    rect_t card, avatar, swatch[AVATAR_COLORS], field[F_COUNT], eye, cancel, create, signin;
    i32    title_y, sub_y, user_hint_y, meter_y, msg_y;
} slayout_t;

static void slay(slayout_t *o) {
    i32 sw = (i32)SCREEN_W, sh = (i32)SCREEN_H;
    kmemset(o, 0, sizeof(*o));
    o->card = rect_make((sw - CARD_W) / 2, (sh - CARD_H) / 2, CARD_W, CARD_H);
    i32 x = o->card.x + LEFT_W + 56, w = CARD_W - LEFT_W - 112, y = o->card.y + 40;
    o->title_y = y;
    y += gfx_role_line_h(FONT_ROLE_DISPLAY, 2) + 4;
    o->sub_y = y;
    i32 lw = gfx_str_role_width("Already have one? ", FONT_ROLE_UI, 1, FONT_W_REGULAR);
    o->signin = rect_make(x + lw - 2, y - 2, gfx_str_role_width("Sign in", FONT_ROLE_UI, 1, FONT_W_BOLD) + 4,
                          gfx_role_line_h(FONT_ROLE_UI, 1) + 4);
    y += gfx_role_line_h(FONT_ROLE_UI, 1) + 18;
    o->avatar = rect_make(x, y, 64, 64);
    for (u32 i = 0; i < AVATAR_COLORS; i++) o->swatch[i] = rect_make(x + 78 + (i32)i * 36, y + 30, 28, 28);
    y += 64 + 16;
    i32 half = (w - 14) / 2, lab = gfx_role_line_h(FONT_ROLE_UI, 1) + 6;
    o->field[F_NAME] = rect_make(x, y + lab, half, 48);
    o->field[F_USER] = rect_make(x + half + 14, y + lab, half, 48);
    y += lab + 48 + 6;
    o->user_hint_y = y;
    y += gfx_role_line_h(FONT_ROLE_UI, 1) + 12;
    o->field[F_PASS] = rect_make(x, y + lab, w, 48);
    o->eye = rect_make(x + w - 44, y + lab + 6, 36, 36);
    y += lab + 48 + 8;
    o->meter_y = y;
    y += 5 + 16;
    o->field[F_HINT] = rect_make(x, y + lab, w, 48);
    y += lab + 48 + 10;
    o->msg_y = y;
    i32 by = o->card.y + CARD_H - 40 - 48;
    i32 cw = gfx_str_role_width("Create account", FONT_ROLE_UI, 2, FONT_W_BOLD) + 24 + 8 + 18 + 24;   /* pad, gap, arrow, pad */
    o->create = rect_make(x + w - cw, by, cw, 48);
    i32 kw = gfx_str_role_width("Cancel", FONT_ROLE_UI, 2, FONT_W_BOLD) + 40;
    o->cancel = rect_make(o->create.x - 10 - kw, by, kw, 48);
}

static void build_art(signup_t *S, const slayout_t *o) {
    (void)o;
    i32 sw = (i32)SCREEN_W, sh = (i32)SCREEN_H;
    S->wall = (u32 *)kmalloc((size_t)sw * (size_t)sh * 4u);
    if (S->wall) care_wallpaper_render(S->wall, (u32)sw, (u32)sh, (u32)sw, false);

    /* Left panel: #151B2E with a coral and an indigo glow (50 %, blur 70). */
    i32 pw = LEFT_W + 32, ph = CARD_H;
    u32 *tmp = (u32 *)kmalloc((size_t)pw * (size_t)ph * 4u);
    u32 *scratch = (u32 *)kmalloc((size_t)glass_scratch_words((u32)pw, (u32)ph) * 4u);
    S->panel = (u32 *)kmalloc((size_t)pw * (size_t)ph * 4u);
    if (tmp && scratch && S->panel) {
        for (i32 i = 0; i < pw * ph; i++) tmp[i] = 0x151B2E;
        gfx_buffer_t off = { tmp, (u32)pw, (u32)ph, (u32)pw * 4u };
        gfx_buffer_t *prev = g_target;
        gfx_set_target(&off);
        gfx_circle_fill(LEFT_W + 160 - 210, ph + 120 - 210, 210, ui_mix(0x151B2E, 0xFF7A59, 128));
        gfx_circle_fill(-140 + 180, 160 + 180, 180, ui_mix(0x151B2E, 0x4F5EF2, 128));
        gfx_set_target(prev);
        glass_blur(tmp, (u32)pw, (u32)pw, (u32)ph, 70, S->panel, scratch);
    } else if (S->panel) {
        kfree(S->panel); S->panel = NULL;
    }
    if (tmp) kfree(tmp);
    if (scratch) kfree(scratch);
}

static void label(i32 x, i32 y, const char *s, const char *opt) {
    gfx_str_role(x, y, s, C_LABEL, FONT_ROLE_UI, 1, FONT_W_BOLD);
    if (opt) gfx_str_role(x + gfx_str_role_width(s, FONT_ROLE_UI, 1, FONT_W_BOLD) + 6, y, opt, C_MUTED,
                          FONT_ROLE_UI, 1, FONT_W_REGULAR);
}

static void draw(signup_t *S, const slayout_t *o, const mouse_t *m) {
    i32 sw = (i32)SCREEN_W, sh = (i32)SCREEN_H;
    if (S->wall) gfx_blit_argb(0, 0, S->wall, sw, sh, 0, 0, sw, sh);
    else gfx_clear(0xE9ECF7);

    rect_t c = o->card;
    gfx_shadow_soft_ex(c.x, c.y, c.w, c.h, 32, 60, 46, 30);
    gfx_rect_rounded(c.x, c.y, c.w, c.h, 32, 0xFFFFFF);
    if (S->panel) gfx_blit_argb_rounded(c.x, c.y, S->panel, LEFT_W + 32, CARD_H, 32);
    else gfx_rect_rounded(c.x, c.y, LEFT_W + 32, CARD_H, 32, 0x151B2E);
    gfx_rect(c.x + LEFT_W, c.y, 40, CARD_H, 0xFFFFFF);           /* square seam */

    /* Left panel */
    i32 lx = c.x + 48, ly = c.y + 48;
    ui_care_logo(lx, ly, 30);
    gfx_str_role(lx + 40, ly + (30 - gfx_role_line_h(FONT_ROLE_DISPLAY, 1)) / 2, "CareOS", 0xEEF1FA,
                 FONT_ROLE_DISPLAY, 1, FONT_W_BOLD);
    i32 mid = c.y + 190;
    mid += gfx_str_role_wrap(lx, mid, LEFT_W - 96, "Make this computer yours.", 0xEEF1FA,
                             FONT_ROLE_DISPLAY, 3, FONT_W_BOLD, -6) + 14;
    gfx_str_role_wrap(lx, mid, LEFT_W - 96,
                      "Your account keeps your files, wallpaper and settings in one place. Takes about a minute.",
                      0xC9CFE2, FONT_ROLE_UI, 2, FONT_W_REGULAR, 5);
    static const char *const RAIL[3] = { "Create account", "Connect to the network", "Pick your look" };
    i32 ry = c.y + CARD_H - 48 - 3 * 28 - 2 * 12;
    for (u32 i = 0; i < 3; i++, ry += 40) {
        char n[2] = { (char)('1' + i), 0 };
        if (i == 0) gfx_circle_fill(lx + 14, ry + 14, 14, 0xFF7A59);
        else { gfx_circle(lx + 14, ry + 14, 13, 0x5A6385); gfx_circle(lx + 14, ry + 14, 14, 0x5A6385); }
        i32 nw = gfx_str_role_width(n, FONT_ROLE_UI, 1, FONT_W_BOLD);
        gfx_str_role(lx + 14 - nw / 2, ry + 14 - gfx_role_line_h(FONT_ROLE_UI, 1) / 2, n,
                     i == 0 ? 0x2A120Au : 0xB4BBD2u, FONT_ROLE_UI, 1, FONT_W_BOLD);
        gfx_str_role(lx + 40, ry + 14 - gfx_role_line_h(FONT_ROLE_UI, 1) / 2, RAIL[i],
                     i == 0 ? 0xEEF1FAu : 0xB4BBD2u, FONT_ROLE_UI, 1, i == 0 ? FONT_W_BOLD : FONT_W_REGULAR);
    }

    /* Form */
    i32 x = c.x + LEFT_W + 56;
    gfx_str_role(x, o->title_y, "Create your account", C_INK, FONT_ROLE_DISPLAY, 2, FONT_W_REGULAR);
    gfx_str_role(x, o->sub_y, "Already have one?", 0x5A6178, FONT_ROLE_UI, 1, FONT_W_REGULAR);
    gfx_str_role(o->signin.x + 2, o->sub_y, "Sign in", C_FOCUS, FONT_ROLE_UI, 1, FONT_W_BOLD);

    const char *shown = S->f[F_NAME].len ? S->f[F_NAME].buf : (S->f[F_USER].len ? S->f[F_USER].buf : "?");
    session_avatar(o->avatar.x + 32, o->avatar.y + 32, 32, S->color, shown, false);
    gfx_str_role(o->swatch[0].x, o->avatar.y + 4, "Avatar color", C_LABEL, FONT_ROLE_UI, 1, FONT_W_BOLD);
    for (u32 i = 0; i < AVATAR_COLORS; i++) {
        rect_t r = o->swatch[i];
        if (i == S->color) { gfx_circle_fill(r.x + 14, r.y + 14, 18, C_INK); gfx_circle_fill(r.x + 14, r.y + 14, 16, 0xFFFFFF); }
        session_avatar(r.x + 14, r.y + 14, 14, i, " ", false);
    }

    static const char *const LBL[F_COUNT] = { "Your name", "Username", "Password", "Password hint" };
    static const char *const PH[F_COUNT] = { "How should CareOS greet you?", "lowercase, no spaces", "8+ characters", "Something only you would get" };
    for (u32 i = 0; i < F_COUNT; i++) {
        rect_t r = o->field[i];
        label(r.x, r.y - gfx_role_line_h(FONT_ROLE_UI, 1) - 6, LBL[i], i == F_HINT ? "(optional)" : NULL);
        session_field(r, &S->f[i], PH[i], S->focus == i, S->focus == i ? 0xFFFFFFu : C_FILL, C_BORDER, C_INK, 0x9AA3BD);
    }
    /* Eye toggle */
    ui_glyph(UI_GLYPH_EYE, o->eye.x + 8, o->eye.y + 8, 20, S->f[F_PASS].reveal ? C_FOCUS : C_LABEL);

    /* Username feedback */
    {
        const char *u = S->f[F_USER].buf;
        char line[96];
        u32 col = C_OKTEXT;
        if (S->f[F_USER].len == 0) { kstrcpy(line, "Your home folder will be /home/<username>"); col = C_MUTED; }
        else if (!username_valid(u)) {
            kstrcpy(line, "Start with a letter; use a-z, 0-9, _ or - (up to 24).");
            col = C_BAD;
        } else {
            user_profile_t p;
            if (user_profile_get(u, &p)) { kstrcpy(line, "That username is taken."); col = C_BAD; }
            else ksprintf(line, "Your home folder will be /home/%s", u);
        }
        if (col == C_OKTEXT) ui_glyph(UI_GLYPH_CHECK, x, o->user_hint_y, 16, C_OKTEXT);
        gfx_str_role(x + (col == C_OKTEXT ? 22 : 0), o->user_hint_y, line, col, FONT_ROLE_UI, 1, FONT_W_REGULAR);
    }
    /* Strength meter */
    {
        u32 bars = password_meter_bars(S->f[F_PASS].buf), st = password_strength(S->f[F_PASS].buf);
        static const char *const NAMES[3] = { "Weak", "OK", "Strong" };
        u32 col = S->f[F_PASS].len == 0 ? C_BORDER : (st == PW_WEAK ? C_BAD : (st == PW_OK ? C_WARN : C_OKBAR));
        i32 mw = o->field[F_PASS].w - 70, bw = (mw - 12) / 4;
        for (u32 i = 0; i < 4; i++)
            gfx_rect_rounded(x + (i32)i * (bw + 4), o->meter_y, bw, 5, 2, i < bars ? col : C_BORDER);
        if (S->f[F_PASS].len)
            gfx_str_role(x + mw + 10, o->meter_y - 6, NAMES[st], st == PW_STRONG ? C_OKTEXT : col, FONT_ROLE_UI, 0, FONT_W_BOLD);
    }
    if (S->msg[0]) gfx_str_role(x, o->msg_y, S->msg, C_BAD, FONT_ROLE_UI, 1, FONT_W_BOLD);

    /* Buttons */
    bool hc = m && rect_contains(o->cancel, m->x, m->y), hk = m && rect_contains(o->create, m->x, m->y);
    gfx_rect_rounded(o->cancel.x, o->cancel.y, o->cancel.w, o->cancel.h, 14, hc ? 0xE1E4EEu : 0xEEF0F6u);
    gfx_str_role(o->cancel.x + 20, o->cancel.y + (48 - gfx_role_line_h(FONT_ROLE_UI, 2)) / 2, "Cancel", C_INK,
                 FONT_ROLE_UI, 2, FONT_W_BOLD);
    gfx_rect_rounded(o->create.x, o->create.y, o->create.w, o->create.h, 14, 0xFF7A59);
    if (hk) gfx_rect_rounded_blend(o->create.x, o->create.y, o->create.w, o->create.h, 14, 0xFFFFFF, 40);
    gfx_str_role(o->create.x + 24, o->create.y + (48 - gfx_role_line_h(FONT_ROLE_UI, 2)) / 2, "Create account",
                 0x2A120A, FONT_ROLE_UI, 2, FONT_W_BOLD);
    ui_glyph(UI_GLYPH_ARROW_R, o->create.x + o->create.w - 24 - 18, o->create.y + 15, 18, 0x2A120A);
}

static bool create(signup_t *S) {
    const char *u = S->f[F_USER].buf, *p = S->f[F_PASS].buf;
    if (!username_valid(u)) { kstrcpy(S->msg, "Pick a username: a letter, then a-z, 0-9, _ or -."); S->focus = F_USER; return false; }
    if (password_strength(p) == PW_WEAK) { kstrcpy(S->msg, "Use 8+ characters with upper, lower case and a number."); S->focus = F_PASS; return false; }
    int rc = user_register(u, p);
    if (rc == -1) { kstrcpy(S->msg, "That username is taken."); S->focus = F_USER; return false; }
    if (rc != 0) { kstrcpy(S->msg, "Couldn't create the account. Try again."); return false; }
    if (user_login(u, p) != 0) { kstrcpy(S->msg, "Account made, but signing in failed. Use the login screen."); return false; }

    user_profile_t prof;
    if (user_profile_get(u, &prof)) {
        if (S->f[F_NAME].len) { kstrncpy(prof.display_name, S->f[F_NAME].buf, sizeof(prof.display_name) - 1); prof.display_name[sizeof(prof.display_name) - 1] = 0; }
        prof.avatar_color = (u8)S->color;
        prof.onboarded = false;
        prof.onboard_step = (u8)onboard_first_step(true);
        if (user_profile_set(u, &prof) != 0) serial_write("[signup] could not save the profile\n");
    }
    if (S->f[F_HINT].len && user_hint_set(u, S->f[F_HINT].buf) != 0) serial_write("[signup] could not save the hint\n");
    serial_write("[signup] account created and signed in\n");
    return true;
}

bool session_signup(mouse_t *m) {
    static signup_t S;
    kmemset(&S, 0, sizeof(S));
    S.f[F_PASS].secret = true;
    S.focus = F_NAME;
    slayout_t o;
    slay(&o);
    build_art(&S, &o);
    keyboard_flush();
    bool result = false;

    while (1) {
        bool done = false;
        while (keyboard_haschar()) {
            char c = gui_read_key();
            if (c == 27) { done = true; break; }
            if (c == '\t') { S.focus = (S.focus + 1) % F_COUNT; continue; }
            if (c == '\n' || c == 13) {
                if (S.focus < F_HINT) { S.focus++; continue; }
                if (create(&S)) { result = true; done = true; }
                continue;
            }
            if (S.focus == F_USER && c >= 'A' && c <= 'Z') c = (char)(c + 32);   /* usernames are lowercase */
            if (sfield_key(&S.f[S.focus], c)) S.msg[0] = '\0';
        }
        mouse_update(m);
        if (!done && m->left_clicked) {
            for (u32 i = 0; i < F_COUNT; i++) if (rect_contains(o.field[i], m->x, m->y)) S.focus = i;
            for (u32 i = 0; i < AVATAR_COLORS; i++) if (rect_contains(o.swatch[i], m->x, m->y)) S.color = i;
            if (rect_contains(o.eye, m->x, m->y)) S.f[F_PASS].reveal = !S.f[F_PASS].reveal;
            if (rect_contains(o.cancel, m->x, m->y) || rect_contains(o.signin, m->x, m->y)) done = true;
            if (rect_contains(o.create, m->x, m->y) && create(&S)) { result = true; done = true; }
        }
        if (done) break;
        draw(&S, &o, m);
        session_frame_end(m);
    }
    if (S.wall) kfree(S.wall);
    if (S.panel) kfree(S.panel);
    kmemset(&S, 0, sizeof(S));          /* the password */
    keyboard_flush();
    return result;
}
