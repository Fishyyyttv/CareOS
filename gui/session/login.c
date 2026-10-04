/* gui/session/login.c -- the login screen, the lock screen and the forced
 * password change (Login and Lock artboards).
 *
 * Behaviour is the old run_login_flow's: PBKDF2 sign-in (kernel/users.c), the
 * kernel's lockout after repeated failures, the mandatory change of a shipped
 * bootstrap password, the guest session and account creation (Sign up). Only
 * the presentation is new. */
#include "kernel.h"
#include "session/session.h"
#include "glass.h"

typedef enum { LS_LOCKFACE, LS_SIGNIN, LS_OTHER, LS_CHANGE } lstate_t;

#define MAX_PILLS 7

typedef struct {
    lstate_t st;
    bool     lock_mode;
    char     user[32];               /* the account being signed in */
    user_profile_t prof;
    bool     has_prof;
    sfield_t pass, uname, newpw, confirm;
    u32      focus;                  /* LS_OTHER / LS_CHANGE: 0 first field, 1 second */
    char     msg[96];
    u32      msg_color;
    u32      shake_t0;
    u32      fails;
    u32      lock_until_ms;
    bool     forgot_open;
    char     verified[64];           /* old password, to authorise a forced change */
    u32      *lockbg;                /* blurred + dimmed wallpaper for the lock face */
} login_t;

typedef struct {
    i32    clock_y, date_y;
    rect_t avatar;                   /* circle bounds */
    i32    name_y;
    rect_t field, submit, field2;    /* field2: second field (other user / confirm) */
    rect_t forgot;
    i32    msg_y;
    rect_t pills[MAX_PILLS + 3];     /* accounts, then Other, Guest, New account */
    char   pill_name[MAX_PILLS][32];
    u32    pill_count;               /* account pills */
    rect_t other, guest, newacct;
    bool   has_other;
    rect_t power;
} llayout_t;

static void set_msg(login_t *L, const char *s, u32 color) {
    kstrncpy(L->msg, s, sizeof(L->msg) - 1);
    L->msg[sizeof(L->msg) - 1] = '\0';
    L->msg_color = color;
}

static void select_user(login_t *L, const char *name) {
    kstrncpy(L->user, name, sizeof(L->user) - 1);
    L->user[sizeof(L->user) - 1] = '\0';
    L->has_prof = user_profile_get(L->user, &L->prof);
    sfield_clear(&L->pass);
    L->fails = 0;
    L->msg[0] = '\0';
    L->forgot_open = false;
}

/* ---- Layout -------------------------------------------------------------------- */
static i32 sy(i32 v) { return v * (i32)SCREEN_H / 900; }

static void layout(const login_t *L, llayout_t *o) {
    i32 sw = (i32)SCREEN_W, sh = (i32)SCREEN_H;
    kmemset(o, 0, sizeof(*o));
    o->clock_y = sy(96) - gfx_clock_cap_top(2);
    o->date_y = sy(96) + gfx_clock_cap_h(2) + 14;

    i32 cx = sw / 2, y = sy(400);
    if (y < o->date_y + gfx_role_line_h(FONT_ROLE_UI, 4) + 40) y = o->date_y + gfx_role_line_h(FONT_ROLE_UI, 4) + 40;
    o->avatar = rect_make(cx - 52, y, 104, 104);
    y += 104 + 18;
    o->name_y = y;
    y += gfx_role_line_h(FONT_ROLE_DISPLAY, 2) + 18;
    if (L->st == LS_OTHER || L->st == LS_CHANGE) {
        o->field2 = rect_make(cx - 200, y, 400, 56);
        y += 56 + 12;
    }
    o->field = rect_make(cx - 200, y, 400, 56);
    o->submit = rect_make(o->field.x + o->field.w - 6 - 44, y + 6, 44, 44);
    y += 56 + 12;
    o->msg_y = y;
    y += gfx_role_line_h(FONT_ROLE_UI, 1) + 8;
    i32 fw = gfx_str_role_width("Forgot password?", FONT_ROLE_UI, 1, FONT_W_BOLD);
    o->forgot = rect_make(cx - fw / 2 - 8, y, fw + 16, 30);

    o->power = rect_make(sw - 40 - 52, sh - 36 - 52, 52, 52);

    if (L->lock_mode) return;                        /* no account switching while locked */
    i32 px = 40, py = sh - 36 - 52;
    u32 n = user_enum_count();
    for (u32 i = 0; i < n && o->pill_count < MAX_PILLS; i++) {
        u32 uid; bool root;
        char name[32];
        if (!user_enum_at(i, &uid, name, sizeof(name), &root)) break;
        i32 w = 8 + 36 + 10 + gfx_str_role_width(name, FONT_ROLE_UI, 1, FONT_W_BOLD) + 16;
        o->pills[o->pill_count] = rect_make(px, py, w, 52);
        kstrcpy(o->pill_name[o->pill_count], name);
        o->pill_count++;
        px += w + 10;
    }
    if (n > MAX_PILLS) {
        i32 w = 8 + 36 + 10 + gfx_str_role_width("Other user", FONT_ROLE_UI, 1, FONT_W_BOLD) + 16;
        o->other = rect_make(px, py, w, 52);
        o->has_other = true;
        px += w + 10;
    }
    i32 gw = 8 + 36 + 10 + gfx_str_role_width("Guest", FONT_ROLE_UI, 1, FONT_W_BOLD) + 16;
    o->guest = rect_make(px, py, gw, 52);
    px += gw + 10;
    i32 nw = 8 + 36 + 10 + gfx_str_role_width("New account", FONT_ROLE_UI, 1, FONT_W_BOLD) + 18;
    o->newacct = rect_make(px, py, nw, 52);
}

/* ---- Drawing ------------------------------------------------------------------- */
static void pill(rect_t r, const char *label, bool selected, bool hover, bool dashed) {
    u32 over = g_theme->is_dark ? 0xFFFFFFu : g_theme->ink;
    if (dashed) {
        gfx_rect_rounded_outline(r.x, r.y, r.w, r.h, 26, ui_mix(g_theme->ground, over, 72));
        if (hover) gfx_rect_rounded_blend(r.x, r.y, r.w, r.h, 26, over, 18);
    } else if (selected) {
        gfx_rect_rounded_blend(r.x, r.y, r.w, r.h, 26, over, 31);
        gfx_rect_rounded_outline(r.x, r.y, r.w, r.h, 26, ui_mix(g_theme->ground, over, 90));
    } else {
        gfx_rect_rounded_blend(r.x, r.y, r.w, r.h, 26, g_theme->panel_tint, hover ? 200 : 153);
        gfx_rect_rounded_outline(r.x, r.y, r.w, r.h, 26, ui_mix(g_theme->panel_tint, over, 26));
    }
    gfx_str_role(r.x + 8 + 36 + 10, r.y + (52 - gfx_role_line_h(FONT_ROLE_UI, 1)) / 2, label,
                 selected ? g_theme->ink : g_theme->ink2, FONT_ROLE_UI, 1, FONT_W_BOLD);
}

static void round_button(rect_t r, ui_glyph_t g, bool hover) {
    u32 over = g_theme->is_dark ? 0xFFFFFFu : g_theme->ink;
    gfx_circle_fill(r.x + r.w / 2, r.y + r.h / 2, r.w / 2, ui_mix(g_theme->ground, g_theme->panel_tint, 153));
    if (hover) gfx_circle_fill(r.x + r.w / 2, r.y + r.h / 2, r.w / 2, ui_mix(g_theme->panel_tint, over, 40));
    ui_glyph(g, r.x + (r.w - 20) / 2, r.y + (r.h - 20) / 2, 20, g_theme->ink);
}

/* The 56 px glass password field with the coral arrow button inside it. */
static void glass_field(rect_t r, const sfield_t *f, const char *placeholder, bool focused,
                        ui_glyph_t lead, bool with_submit, const mouse_t *m) {
    ui_glass(50, r, 18, g_theme->panel_tint, 184, 20, UI_GLASS_HAIRLINE);
    if (focused) gfx_rect_rounded_outline(r.x, r.y, r.w, r.h, 18, ui_mix(g_theme->panel_tint, g_theme->care, 150));
    ui_glyph(lead, r.x + 18, r.y + (r.h - 18) / 2, 18, g_theme->ink2);
    i32 tx = r.x + 48, ty = r.y + (r.h - gfx_role_line_h(FONT_ROLE_UI, 3)) / 2;
    i32 maxx = r.x + r.w - (with_submit ? 60 : 18);
    if (f->len == 0) {
        gfx_str_role(tx, ty, placeholder, g_theme->muted, FONT_ROLE_UI, 3, FONT_W_REGULAR);
    } else if (f->secret) {
        i32 cx = tx + 5;
        for (u32 i = 0; i < f->len && cx < maxx; i++, cx += 14) gfx_circle_fill(cx, r.y + r.h / 2, 4, g_theme->ink);
    } else {
        gfx_str_role_fit(tx, ty, maxx - tx, f->buf, g_theme->ink, FONT_ROLE_UI, 3, FONT_W_REGULAR);
    }
    if (focused && (timer_ms() / 500) % 2 == 0) {
        i32 cx = f->secret ? tx + (i32)f->len * 14 : tx + gfx_str_role_width(f->buf, FONT_ROLE_UI, 3, FONT_W_REGULAR) + 1;
        if (cx < maxx) gfx_rect(cx, ty + 2, 2, gfx_role_line_h(FONT_ROLE_UI, 3) - 4, g_theme->care);
    }
    if (with_submit) {
        rect_t b = rect_make(r.x + r.w - 6 - 44, r.y + 6, 44, 44);
        bool hover = m && rect_contains(b, m->x, m->y);
        gfx_rect_rounded(b.x, b.y, b.w, b.h, 14, g_theme->care);
        if (hover) gfx_rect_rounded_blend(b.x, b.y, b.w, b.h, 14, 0xFFFFFF, 40);
        ui_glyph(UI_GLYPH_ARROW_R, b.x + 12, b.y + 12, 20, g_theme->on_care);
    }
}

static i32 shake_dx(const login_t *L) {
    static const i8 SHAKE[] = { -8, 8, -7, 7, -5, 5, -3, 3, -1, 0 };
    u32 t = timer_ms() - L->shake_t0;
    if (!L->shake_t0 || t >= 300) return 0;
    return SHAKE[t * 10 / 300];
}

static void draw_clock(const llayout_t *o, u32 slot, i32 top_override) {
    char t[16], d[40];
    session_time_line(t);
    session_date_line(d, true);
    i32 sw = (i32)SCREEN_W;
    i32 cy = top_override >= 0 ? top_override : o->clock_y;
    i32 tw = gfx_str_clock_width(t, slot);
    gfx_str_clock(sw / 2 - tw / 2, cy, t, g_theme->ink, slot);
    i32 dy = top_override >= 0 ? -1 : o->date_y;
    if (dy >= 0) {
        i32 dw = gfx_str_role_width(d, FONT_ROLE_UI, 4, FONT_W_REGULAR);
        gfx_str_role(sw / 2 - dw / 2, dy, d, g_theme->is_dark ? 0xD5DAEAu : g_theme->ink2, FONT_ROLE_UI, 4, FONT_W_REGULAR);
    }
}

static void draw_signin(login_t *L, const llayout_t *o, const mouse_t *m) {
    gui_wallpaper_draw();
    draw_clock(o, 2, -1);
    i32 sw = (i32)SCREEN_W, cx = sw / 2;

    /* Avatar with the artboard's 5 px white-12 % ring and soft shadow. */
    rect_t a = o->avatar;
    gfx_shadow_soft_ex(a.x, a.y, a.w, a.h, 52, 30, 60, 14);
    gfx_circle_fill(a.x + 52, a.y + 52, 57, ui_mix(g_theme->ground, 0xFFFFFF, 31));
    bool other = L->st == LS_OTHER;
    session_avatar(a.x + 52, a.y + 52, 52, L->has_prof ? L->prof.avatar_color : 0,
                   other ? (L->uname.len ? L->uname.buf : "?") : L->user, false);

    const char *title = other ? "Sign in" : (L->has_prof && L->prof.display_name[0] ? L->prof.display_name : L->user);
    if (L->st == LS_CHANGE) title = "Choose a new password";
    i32 nw = gfx_str_role_width(title, FONT_ROLE_DISPLAY, 2, FONT_W_REGULAR);
    gfx_str_role(cx - nw / 2, o->name_y, title, g_theme->ink, FONT_ROLE_DISPLAY, 2, FONT_W_REGULAR);

    i32 dx = shake_dx(L);
    rect_t f = o->field;  f.x += dx;
    if (L->st == LS_OTHER) {
        rect_t u = o->field2; u.x += dx;
        glass_field(u, &L->uname, "Username", L->focus == 0, UI_GLYPH_USER, false, m);
        glass_field(f, &L->pass, "Password", L->focus == 1, UI_GLYPH_LOCK, true, m);
    } else if (L->st == LS_CHANGE) {
        rect_t u = o->field2; u.x += dx;
        glass_field(u, &L->newpw, "New password", L->focus == 0, UI_GLYPH_LOCK, false, m);
        glass_field(f, &L->confirm, "Repeat the new password", L->focus == 1, UI_GLYPH_LOCK, true, m);
    } else {
        glass_field(f, &L->pass, "Password", true, UI_GLYPH_LOCK, true, m);
    }

    if (L->msg[0]) {
        i32 mw = gfx_str_role_width(L->msg, FONT_ROLE_UI, 1, FONT_W_BOLD);
        gfx_str_role(cx - mw / 2, o->msg_y, L->msg, L->msg_color, FONT_ROLE_UI, 1, FONT_W_BOLD);
    }
    if (L->st == LS_SIGNIN || L->st == LS_OTHER) {
        bool hv = m && rect_contains(o->forgot, m->x, m->y);
        gfx_str_role(o->forgot.x + 8, o->forgot.y + (30 - gfx_role_line_h(FONT_ROLE_UI, 1)) / 2,
                     "Forgot password?", hv ? g_theme->ink : (g_theme->is_dark ? 0xB7C1FFu : g_theme->calm),
                     FONT_ROLE_UI, 1, FONT_W_BOLD);
    }
    if (L->forgot_open) {
        const char *t = "Ask an administrator to reset it in the Users app.";
        i32 w = gfx_str_role_width(t, FONT_ROLE_UI, 1, FONT_W_REGULAR) + 32;
        rect_t sh = rect_make(cx - w / 2, o->forgot.y + 36, w, 40);
        ui_glass(51, sh, 14, g_theme->panel_tint, 220, 20, UI_GLASS_HAIRLINE);
        gfx_str_role(sh.x + 16, sh.y + (40 - gfx_role_line_h(FONT_ROLE_UI, 1)) / 2, t, g_theme->ink2,
                     FONT_ROLE_UI, 1, FONT_W_REGULAR);
    }

    /* Accounts, Guest, New account (not while a password change is forced). */
    for (u32 i = 0; L->st != LS_CHANGE && i < o->pill_count; i++) {
        rect_t r = o->pills[i];
        bool sel = L->st != LS_OTHER && kstrcmp(o->pill_name[i], L->user) == 0;
        pill(r, o->pill_name[i], sel, m && rect_contains(r, m->x, m->y), false);
        user_profile_t p;
        bool hp = user_profile_get(o->pill_name[i], &p);
        session_avatar(r.x + 8 + 18, r.y + 26, 18, hp ? p.avatar_color : 0, o->pill_name[i], false);
    }
    if (!L->lock_mode && L->st != LS_CHANGE) {
        if (o->has_other) {
            pill(o->other, "Other user", L->st == LS_OTHER, m && rect_contains(o->other, m->x, m->y), false);
            gfx_circle_fill(o->other.x + 26, o->other.y + 26, 18, g_theme->raised2);
            ui_glyph(UI_GLYPH_USER, o->other.x + 26 - 9, o->other.y + 26 - 9, 18, g_theme->ink2);
        }
        pill(o->guest, "Guest", false, m && rect_contains(o->guest, m->x, m->y), false);
        session_avatar(o->guest.x + 26, o->guest.y + 26, 18, 0, "Guest", true);
        pill(o->newacct, "New account", false, m && rect_contains(o->newacct, m->x, m->y), true);
        gfx_circle_fill(o->newacct.x + 26, o->newacct.y + 26, 18, g_theme->raised2);
        ui_glyph(UI_GLYPH_PLUS, o->newacct.x + 26 - 9, o->newacct.y + 26 - 9, 18, g_theme->ink2);
    }
    round_button(o->power, UI_GLYPH_POWER, m && rect_contains(o->power, m->x, m->y));
}

/* ---- Lock face ------------------------------------------------------------------- */
static void build_lockbg(login_t *L) {
    i32 sw = (i32)SCREEN_W, sh = (i32)SCREEN_H;
    u32 n = (u32)sw * (u32)sh;
    u32 *tmp = (u32 *)kmalloc((size_t)n * 4u);
    L->lockbg = (u32 *)kmalloc((size_t)n * 4u);
    u32 *scratch = (u32 *)kmalloc((size_t)glass_scratch_words((u32)sw, (u32)sh) * 4u);
    if (!tmp || !L->lockbg || !scratch) {
        if (L->lockbg) { kfree(L->lockbg); L->lockbg = NULL; }
    } else {
        gfx_buffer_t off = { tmp, (u32)sw, (u32)sh, (u32)sw * 4u };
        gfx_buffer_t *prev = g_target;
        gfx_set_target(&off);
        gui_wallpaper_draw();
        gfx_set_target(prev);
        glass_blur(tmp, (u32)sw, (u32)sw, (u32)sh, 40, L->lockbg, scratch);
        u32 dim = g_theme->is_dark ? 0x05081Au : 0x161A2Bu;
        for (u32 i = 0; i < n; i++) L->lockbg[i] = ui_mix(L->lockbg[i], dim, g_theme->is_dark ? 90 : 50);
    }
    if (tmp) kfree(tmp);
    if (scratch) kfree(scratch);
}

static void draw_lockface(login_t *L, const llayout_t *o, const mouse_t *m) {
    i32 sw = (i32)SCREEN_W, sh = (i32)SCREEN_H;
    if (L->lockbg) gfx_blit_argb(0, 0, L->lockbg, sw, sh, 0, 0, sw, sh);
    else gui_wallpaper_draw();

    ui_glyph(UI_GLYPH_LOCK, 32, 24, 18, g_theme->ink);
    gfx_str_role(32 + 26, 24 + (18 - gfx_role_line_h(FONT_ROLE_UI, 1)) / 2, "Locked", g_theme->ink,
                 FONT_ROLE_UI, 1, FONT_W_BOLD);

    char d[40], t[16];
    session_date_line(d, true);
    session_time_line(t);
    i32 dw = gfx_str_role_width(d, FONT_ROLE_UI, 4, FONT_W_BOLD);
    gfx_str_role(sw / 2 - dw / 2, sy(90), d, g_theme->is_dark ? 0xD5DAEAu : g_theme->ink2, FONT_ROLE_UI, 4, FONT_W_BOLD);
    i32 ty = sy(90) + gfx_role_line_h(FONT_ROLE_UI, 4) + 8 - gfx_clock_cap_top(4);
    i32 tw = gfx_str_clock_width(t, 4);
    gfx_str_clock(sw / 2 - tw / 2, ty, t, g_theme->ink, 4);

    /* Real notifications, newest first: app, time and title only. */
    notify_preview_t pv[2];
    u32 n = notify_previews(pv, 2);
    i32 y = sy(380);
    for (u32 i = 0; i < n; i++) {
        rect_t c = rect_make(sw / 2 - 200, y, 400, 70);
        ui_glass(52 + i, c, 22, g_theme->panel_tint, 168, 30, UI_GLASS_HAIRLINE);
        ui_app_icon(ui_icon_for_app(pv[i].app), c.x + 16, c.y + 16, 38);
        i32 tx = c.x + 16 + 38 + 12, tw2 = c.w - (tx - c.x) - 16;
        gfx_str_role(tx, c.y + 14, pv[i].app_name, g_theme->ink2, FONT_ROLE_UI, 0, FONT_W_BOLD);
        i32 ww = gfx_str_role_width(pv[i].when, FONT_ROLE_UI, 0, FONT_W_REGULAR);
        gfx_str_role(tx + tw2 - ww, c.y + 14, pv[i].when, g_theme->ink2, FONT_ROLE_UI, 0, FONT_W_REGULAR);
        gfx_str_role_fit(tx, c.y + 14 + gfx_role_line_h(FONT_ROLE_UI, 0) + 2, tw2, pv[i].title, g_theme->ink,
                         FONT_ROLE_UI, 2, FONT_W_BOLD);
        y += 70 + 10;
    }
    session_pet_slot(rect_make(sw / 2 - 200, sy(560), 400, 128), 0);

    /* "Press any key or click to unlock" (no swipe input to name). */
    const char *hint = "Press any key or click to unlock";
    i32 hw = 22 + 18 + 10 + gfx_str_role_width(hint, FONT_ROLE_UI, 2, FONT_W_BOLD) + 22;
    rect_t hp = rect_make(sw / 2 - hw / 2, sh - 56 - 52, hw, 52);
    u32 over = g_theme->is_dark ? 0xFFFFFFu : g_theme->ink;
    gfx_rect_rounded_blend(hp.x, hp.y, hp.w, hp.h, 26, over, 36);
    gfx_rect_rounded_outline(hp.x, hp.y, hp.w, hp.h, 26, ui_mix(g_theme->ground, over, 51));
    ui_glyph(UI_GLYPH_LOCK, hp.x + 22, hp.y + 17, 18, g_theme->ink);
    gfx_str_role(hp.x + 22 + 18 + 10, hp.y + (52 - gfx_role_line_h(FONT_ROLE_UI, 2)) / 2, hint, g_theme->ink,
                 FONT_ROLE_UI, 2, FONT_W_BOLD);
    round_button(o->power, UI_GLYPH_POWER, m && rect_contains(o->power, m->x, m->y));
}

/* ---- Actions --------------------------------------------------------------------- */
static void fail(login_t *L, const char *msg) {
    L->shake_t0 = timer_ms() | 1u;
    set_msg(L, msg, g_theme->error);
}

/* 1 = signed in, 0 = keep going. */
static int try_signin(login_t *L) {
    const char *name = L->st == LS_OTHER ? L->uname.buf : L->user;
    u32 now = timer_ms();
    if (L->lock_until_ms && (i32)(L->lock_until_ms - now) > 0) {
        char b[96];
        ksprintf(b, "Too many tries. Try again in %u s.", (L->lock_until_ms - now + 999u) / 1000u);
        fail(L, b);
        return 0;
    }
    if (!name[0] || L->pass.len == 0) { fail(L, "Type your password first."); return 0; }

    int rc = user_login(name, L->pass.buf);
    if (rc == 0) {
        if (user_must_change_password()) {
            kstrncpy(L->verified, L->pass.buf, sizeof(L->verified) - 1);
            L->verified[sizeof(L->verified) - 1] = '\0';
            if (L->st == LS_OTHER) select_user(L, name);
            sfield_clear(&L->pass);
            sfield_clear(&L->newpw); sfield_clear(&L->confirm);
            L->st = LS_CHANGE;
            L->focus = 0;
            set_msg(L, "This account still has its first-boot password.", g_theme->warning);
            return 0;
        }
        sfield_clear(&L->pass);
        return 1;
    }
    sfield_clear(&L->pass);
    if (rc == -3) {
        L->lock_until_ms = now + 30000u;
        fail(L, "Too many tries. Try again in 30 s.");
        return 0;
    }
    L->fails++;
    char hint[64];
    if (L->fails >= 2 && L->st != LS_OTHER && user_hint_get(L->user, hint, sizeof(hint))) {
        char b[96];
        ksprintf(b, "Hint: %s", hint);
        fail(L, b);
    } else {
        fail(L, "That password didn't work. Try again.");
    }
    return 0;
}

static int apply_change(login_t *L) {
    if (L->newpw.len == 0 || L->confirm.len == 0) { fail(L, "Type the new password twice."); return 0; }
    if (kstrcmp(L->newpw.buf, L->confirm.buf) != 0) {
        sfield_clear(&L->confirm);
        L->focus = 1;
        fail(L, "Those two don't match.");
        return 0;
    }
    if (kstrcmp(L->newpw.buf, L->verified) == 0) { fail(L, "Pick a password you haven't used here."); return 0; }
    int rc = user_change_password(L->user, L->verified, L->newpw.buf);
    if (rc == 0) {
        kmemset(L->verified, 0, sizeof(L->verified));
        sfield_clear(&L->newpw); sfield_clear(&L->confirm);
        return 1;
    }
    if (rc == -2) fail(L, "Use 8+ characters with upper, lower case and a number.");
    else if (rc == -4) fail(L, "Your current password changed. Sign in again.");
    else fail(L, "Couldn't change the password.");
    return 0;
}

static void fade_out(login_t *L, const mouse_t *m) {
    llayout_t o;
    layout(L, &o);
    for (int step = 1; step <= 8; step++) {
        draw_signin(L, &o, NULL);
        gfx_rect_blend(0, 0, (i32)SCREEN_W, (i32)SCREEN_H, 0, (u8)(step * 30));
        session_frame_end(m);
        timer_wait(14);
    }
}

static void leave(login_t *L) {
    if (L->lockbg) { kfree(L->lockbg); L->lockbg = NULL; }
    kmemset(L, 0, sizeof(*L));          /* no secrets left behind */
}

bool session_login(mouse_t *m, bool lock_mode, bool *created) {
    if (created) *created = false;
#ifdef CAREOS_QA_AUTOLOGIN
    /* Headless QA build (make QA_AUTOLOGIN=1): skip the gate. Never shipped. */
    (void)m; (void)lock_mode;
    return true;
#endif
    static login_t L;
    kmemset(&L, 0, sizeof(L));
    L.lock_mode = lock_mode;
    L.pass.secret = L.newpw.secret = L.confirm.secret = true;

    if (lock_mode) {
        select_user(&L, user_current_name());
        L.st = LS_LOCKFACE;
        build_lockbg(&L);
    } else {
        if (user_enum_count() == 0) {
            /* No account yet: making one is the only way in. */
            while (!session_signup(m)) {}
            if (created) *created = true;
            return true;
        }
        char last[32];
        if (user_last_login_name(last, sizeof(last))) select_user(&L, last);
        else {
            u32 uid; bool root; char name[32];
            if (user_enum_at(0, &uid, name, sizeof(name), &root)) select_user(&L, name);
        }
        L.st = LS_SIGNIN;
    }

    keyboard_flush();
    m->x = (i32)SCREEN_W / 2;
    m->y = (i32)SCREEN_H / 2;
    serial_write("[login] login screen ready\n");

    while (1) {
        llayout_t o;
        layout(&L, &o);

        while (keyboard_haschar()) {
            char c = gui_read_key();
            if (L.st == LS_LOCKFACE) {
                /* The key that wakes the lock screen only reveals the
                 * password card; it is not part of the password. */
                L.st = LS_SIGNIN;
                continue;
            }
            if (c == 27) {
                if (L.forgot_open) L.forgot_open = false;
                else if (L.lock_mode && L.st == LS_SIGNIN) { L.st = LS_LOCKFACE; sfield_clear(&L.pass); L.msg[0] = 0; }
                else if (L.st == LS_OTHER) { L.st = LS_SIGNIN; sfield_clear(&L.pass); L.msg[0] = 0; }
                continue;
            }
            if (c == '\t' && (L.st == LS_OTHER || L.st == LS_CHANGE)) { L.focus ^= 1u; continue; }
            if (c == '\n' || c == 13) {
                int ok = (L.st == LS_CHANGE) ? apply_change(&L) : try_signin(&L);
                if (ok) { fade_out(&L, m); leave(&L); return true; }
                continue;
            }
            sfield_t *f = &L.pass;
            if (L.st == LS_OTHER && L.focus == 0) f = &L.uname;
            if (L.st == LS_CHANGE) f = L.focus == 0 ? &L.newpw : &L.confirm;
            if (sfield_key(f, c) && L.msg_color == g_theme->error) L.msg[0] = '\0';
        }

        mouse_update(m);
        if (m->left_clicked) {
            if (L.st == LS_LOCKFACE) {
                if (rect_contains(o.power, m->x, m->y)) session_power_menu(m, true);
                else L.st = LS_SIGNIN;
            } else if (rect_contains(o.power, m->x, m->y)) {
                session_power_menu(m, true);
            } else if (rect_contains(o.submit, m->x, m->y)) {
                int ok = (L.st == LS_CHANGE) ? apply_change(&L) : try_signin(&L);
                if (ok) { fade_out(&L, m); leave(&L); return true; }
            } else if ((L.st == LS_SIGNIN || L.st == LS_OTHER) && rect_contains(o.forgot, m->x, m->y)) {
                L.forgot_open = !L.forgot_open;
            } else if ((L.st == LS_OTHER || L.st == LS_CHANGE) && rect_contains(o.field2, m->x, m->y)) {
                L.focus = 0;
            } else if ((L.st == LS_OTHER || L.st == LS_CHANGE) && rect_contains(o.field, m->x, m->y)) {
                L.focus = 1;
            } else if (!L.lock_mode && L.st != LS_CHANGE) {
                for (u32 i = 0; i < o.pill_count; i++)
                    if (rect_contains(o.pills[i], m->x, m->y)) { select_user(&L, o.pill_name[i]); L.st = LS_SIGNIN; }
                if (o.has_other && rect_contains(o.other, m->x, m->y)) {
                    sfield_clear(&L.uname); sfield_clear(&L.pass);
                    L.st = LS_OTHER; L.focus = 0; L.msg[0] = '\0';
                }
                if (rect_contains(o.guest, m->x, m->y)) {
                    user_logout();                       /* the guest session */
                    leave(&L);
                    return true;
                }
                if (rect_contains(o.newacct, m->x, m->y)) {
                    if (session_signup(m)) { if (created) *created = true; leave(&L); return true; }
                    keyboard_flush();
                }
            }
        }

        if (L.st == LS_LOCKFACE) draw_lockface(&L, &o, m);
        else draw_signin(&L, &o, m);
        session_frame_end(m);
    }
}
