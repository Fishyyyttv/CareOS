/* gui/session/onboarding.c -- first-run setup (Welcome, OnboardWifi,
 * OnboardLook and OnboardReady artboards).
 *
 * A 1000x660 glass card over the wallpaper: a 4-segment progress bar, the
 * step, Back / Skip / Continue. Everything shown is real: English is the only
 * language (CareOS has no translations), Region sets the clock's time zone,
 * the network step shows the actual Ethernet link, and the look step applies
 * theme mode and accent live. The step is stored in the profile so a reboot
 * resumes where the user left off. */
#include "kernel.h"
#include "session/session.h"
#include "../../kernel/account_rules.h"

#define OB_W 1000
#define OB_H 660

typedef struct { const char *name; i32 offset_min; } region_t;
/* Fixed offsets; each label says which, since there is no DST handling. */
static const region_t REGIONS[] = {
    { "US Pacific (UTC-7)", -420 },  { "US Mountain (UTC-6)", -360 }, { "US Central (UTC-5)", -300 },
    { "US Eastern (UTC-4)", -240 },  { "UTC", 0 },                    { "UK (UTC+1)", 60 },
    { "Central Europe (UTC+2)", 120 }, { "India (UTC+5:30)", 330 },  { "Japan (UTC+9)", 540 },
    { "Australia East (UTC+10)", 600 },
};
#define REGION_COUNT ((u32)(sizeof(REGIONS) / sizeof(REGIONS[0])))

typedef struct {
    u32  step, first;
    char user[32];
    user_profile_t prof;
    bool confirm_exit;
} ob_t;

typedef struct {
    rect_t card, back, skip, next, region, keyboard, lang;
    rect_t vibe[3], accent[8];
    rect_t confirm_box, confirm_keep, confirm_later;
    i32    body_y, x, w;
} olayout_t;

static u32 region_index(void) {
    i32 off = settings_get()->tz_offset_min;
    for (u32 i = 0; i < REGION_COUNT; i++) if (REGIONS[i].offset_min == off) return i;
    return 0;
}

static void olay(const ob_t *O, olayout_t *o) {
    i32 sw = (i32)SCREEN_W, sh = (i32)SCREEN_H;
    kmemset(o, 0, sizeof(*o));
    o->card = rect_make((sw - OB_W) / 2, (sh - OB_H) / 2, OB_W, OB_H);
    o->x = o->card.x + 48;
    o->w = OB_W - 96;
    o->body_y = o->card.y + 40 + 20 + 30;
    i32 by = o->card.y + OB_H - 40 - 52;
    const char *nl = O->step == 3 ? "Go to my desktop" : "Continue";
    i32 nw = gfx_str_role_width(nl, FONT_ROLE_UI, 3, FONT_W_BOLD) + 56 + 26;
    o->next = rect_make(o->x + o->w - nw, by, nw, 52);
    o->back = rect_make(o->x, by, gfx_str_role_width("Back", FONT_ROLE_UI, 2, FONT_W_BOLD) + 44, 52);
    i32 kw = gfx_str_role_width("Skip for now", FONT_ROLE_UI, 1, FONT_W_BOLD) + 24;
    o->skip = rect_make(o->next.x - 12 - kw, by, kw, 52);

    i32 col2 = o->x + (o->w + 48) / 2, cw = (o->w - 48) / 2;
    i32 lab = gfx_role_line_h(FONT_ROLE_UI, 1) + 8;
    o->lang = rect_make(col2, o->body_y + lab, cw, 52);
    i32 sel_y = o->lang.y + 52 + 36 + lab;
    o->region = rect_make(col2, sel_y, (cw - 12) / 2 + 40, 46);
    o->keyboard = rect_make(col2 + (cw - 12) / 2 + 52, sel_y, (cw - 12) / 2 - 40, 46);

    i32 vy = o->body_y + gfx_role_line_h(FONT_ROLE_DISPLAY, 3) + 6 + gfx_role_line_h(FONT_ROLE_UI, 3) + 24;
    i32 vw = (o->w - 32) / 3;
    for (u32 i = 0; i < 3; i++) o->vibe[i] = rect_make(o->x + (i32)i * (vw + 16), vy, vw, 150 + 20 + 30);
    i32 ay = vy + 200 + 28 + lab;
    u32 n = theme_accent_count();
    for (u32 i = 0; i < n && i < 8; i++) o->accent[i] = rect_make(o->x + (i32)i * 50, ay, 40, 40);

    o->confirm_box = rect_make(sw / 2 - 210, sh / 2 - 90, 420, 180);
    o->confirm_keep = rect_make(o->confirm_box.x + 24, o->confirm_box.y + 180 - 24 - 46, 176, 46);
    o->confirm_later = rect_make(o->confirm_box.x + 420 - 24 - 176, o->confirm_keep.y, 176, 46);
}

/* ---- Pieces ------------------------------------------------------------------- */
static u32 raised(void) { return g_theme->is_dark ? 0x1C2440u : g_theme->raised; }

static void caps(i32 x, i32 y, const char *s) {
    gfx_str_role(x, y, s, g_theme->muted, FONT_ROLE_UI, 1, FONT_W_BOLD);
}

static void btn(rect_t r, const char *s, bool primary, bool arrow, bool hover) {
    gfx_rect_rounded(r.x, r.y, r.w, r.h, 16, primary ? g_theme->care : raised());
    if (hover) gfx_rect_rounded_blend(r.x, r.y, r.w, r.h, 16, 0xFFFFFF, primary ? 40 : 18);
    u32 fg = primary ? g_theme->on_care : g_theme->ink;
    i32 sl = primary ? 3 : 2;
    i32 tw = gfx_str_role_width(s, FONT_ROLE_UI, (u32)sl, FONT_W_BOLD);
    i32 tx = arrow ? r.x + 28 : r.x + (r.w - tw) / 2;
    gfx_str_role(tx, r.y + (r.h - gfx_role_line_h(FONT_ROLE_UI, (u32)sl)) / 2, s, fg, FONT_ROLE_UI, (u32)sl, FONT_W_BOLD);
    if (arrow) ui_glyph(UI_GLYPH_ARROW_R, tx + tw + 8, r.y + (r.h - 18) / 2, 18, fg);
}

static void select_box(rect_t r, const char *s, bool chevron, bool hover) {
    gfx_rect_rounded(r.x, r.y, r.w, r.h, 14, raised());
    if (hover) gfx_rect_rounded_blend(r.x, r.y, r.w, r.h, 14, 0xFFFFFF, 14);
    ui_hairline(r, 14, raised());
    gfx_str_role_fit(r.x + 12, r.y + (r.h - gfx_role_line_h(FONT_ROLE_UI, 1)) / 2, r.w - (chevron ? 40 : 24), s,
                     g_theme->ink, FONT_ROLE_UI, 1, FONT_W_REGULAR);
    if (chevron) ui_glyph(UI_GLYPH_CHEVRON_D, r.x + r.w - 28, r.y + (r.h - 16) / 2, 16, g_theme->ink2);
}

/* Miniature desktops for the look previews (0 light, 1 dark, 2 auto = half
 * of each), rendered off-screen once and blitted through a rounded mask --
 * the primitives that draw the soft blobs don't honour clipping. */
static u32 *s_mini[3];
static i32  s_mini_w, s_mini_h;

static void mini_paint(u32 *px, i32 w, i32 h, bool light) {
    for (i32 i = 0; i < w * h; i++) px[i] = light ? 0xE9ECF7u : 0x0B1020u;
    gfx_buffer_t off = { px, (u32)w, (u32)h, (u32)w * 4u };
    gfx_buffer_t *prev = g_target;
    gfx_set_target(&off);
    if (light) gfx_circle_fill(40, 20, 70, 0xD5DCFF);
    else { gfx_circle_fill(40, h, 70, 0x1C2A78); gfx_circle_fill(w - 20, 10, 50, 0x5A3438); }
    gfx_rect(0, 0, w, 12, light ? 0xFAFAFDu : 0x0E1328u);
    gfx_rect_rounded(34, 34, w * 52 / 100, 84, 10, light ? 0xFFFFFFu : 0x1A2038u);
    gfx_rect_rounded(w / 2 - 55, h - 24, 110, 16, 8, light ? 0xF2F4FBu : 0x1C2440u);
    gfx_set_target(prev);
}

static void mini_free(void) {
    for (u32 i = 0; i < 3; i++) if (s_mini[i]) { kfree(s_mini[i]); s_mini[i] = NULL; }
}

static void mini_desktop(rect_t r, int kind) {
    if (s_mini_w != r.w || s_mini_h != r.h) mini_free();
    if (!s_mini[kind]) {
        s_mini_w = r.w; s_mini_h = r.h;
        s_mini[kind] = (u32 *)kmalloc((size_t)r.w * (size_t)r.h * 4u);
        if (!s_mini[kind]) { gfx_rect_rounded(r.x, r.y, r.w, r.h, 14, kind == 0 ? 0xE9ECF7u : 0x0B1020u); return; }
        if (kind < 2) mini_paint(s_mini[kind], r.w, r.h, kind == 0);
        else {
            u32 *d = (u32 *)kmalloc((size_t)r.w * (size_t)r.h * 4u);
            mini_paint(s_mini[kind], r.w, r.h, true);
            if (d) {
                mini_paint(d, r.w, r.h, false);
                for (i32 y = 0; y < r.h; y++)
                    for (i32 x = r.w / 2; x < r.w; x++) s_mini[kind][y * r.w + x] = d[y * r.w + x];
                kfree(d);
            }
        }
    }
    gfx_blit_argb_rounded(r.x, r.y, s_mini[kind], r.w, r.h, 14);
}

static void draw_step0(const ob_t *O, const olayout_t *o, const mouse_t *m) {
    i32 x = o->x, y = o->body_y, cw = (o->w - 48) / 2;
    ui_care_logo(x, y, 52);
    y += 52 + 18;
    char h[96];
    ksprintf(h, "Hi, %s.\nLet's get you settled in.", O->prof.display_name[0] ? O->prof.display_name : O->user);
    y += gfx_str_role_wrap(x, y, cw, h, g_theme->ink, FONT_ROLE_DISPLAY, 3, FONT_W_BOLD, -8) + 14;
    gfx_str_role_wrap(x, y, 380, "Pick your language and where you are. You can change any of this later in Settings.",
                      g_theme->ink2, FONT_ROLE_UI, 3, FONT_W_REGULAR, 6);

    caps(o->lang.x, o->body_y, "LANGUAGE");
    rect_t l = o->lang;
    gfx_rect_rounded(l.x, l.y, l.w, l.h, 16, g_theme->care);
    gfx_rect_rounded(l.x + 2, l.y + 2, l.w - 4, l.h - 4, 14, ui_mix(g_theme->panel_tint, g_theme->care, 36));
    gfx_str_role(l.x + 16, l.y + (52 - gfx_role_line_h(FONT_ROLE_UI, 2)) / 2, "English (United States)", g_theme->ink,
                 FONT_ROLE_UI, 2, FONT_W_BOLD);
    ui_glyph(UI_GLYPH_CHECK, l.x + l.w - 34, l.y + 16, 20, g_theme->care);
    gfx_str_role(l.x, l.y + 52 + 8, "More languages arrive with translations.", g_theme->muted, FONT_ROLE_UI, 0, FONT_W_REGULAR);

    i32 ly = o->region.y - gfx_role_line_h(FONT_ROLE_UI, 1) - 6;
    gfx_str_role(o->region.x, ly, "Region", g_theme->ink2, FONT_ROLE_UI, 1, FONT_W_BOLD);
    gfx_str_role(o->keyboard.x, ly, "Keyboard", g_theme->ink2, FONT_ROLE_UI, 1, FONT_W_BOLD);
    select_box(o->region, REGIONS[region_index()].name, true, m && rect_contains(o->region, m->x, m->y));
    select_box(o->keyboard, "US QWERTY", false, false);
}

static void draw_step1(const olayout_t *o) {
    i32 x = o->x, y = o->body_y, lw = 340;
    ui_app_icon(UI_ICON_BROWSER, x, y, 64);
    y += 64 + 18;
    y += gfx_str_role_wrap(x, y, lw, "Get online", g_theme->ink, FONT_ROLE_DISPLAY, 3, FONT_W_BOLD, 0) + 10;
    gfx_str_role_wrap(x, y, lw, "CareOS connects to the network on its own. Here's how it looks right now.",
                      g_theme->ink2, FONT_ROLE_UI, 2, FONT_W_REGULAR, 6);
    rect_t tip = rect_make(x, o->back.y - 24 - 96, lw, 96);
    gfx_rect_rounded(tip.x, tip.y, tip.w, tip.h, 18, raised());
    ui_glyph(UI_GLYPH_WIRED, tip.x + 16, tip.y + 18, 20, g_theme->ink2);
    gfx_str_role_wrap(tip.x + 48, tip.y + 16, lw - 64,
                      "Plugged into a cable? CareOS uses Ethernet automatically, so you can skip this.",
                      g_theme->is_dark ? 0xD5DAEAu : g_theme->ink2, FONT_ROLE_UI, 1, FONT_W_REGULAR, 4);

    i32 rx = x + lw + 48, rw = o->w - lw - 48, ry = o->body_y;
    caps(rx, ry, "NETWORK");
    ry += gfx_role_line_h(FONT_ROLE_UI, 1) + 8;
    bool up = net_is_up();
    rect_t c = rect_make(rx, ry, rw, 76);
    gfx_rect_rounded(c.x, c.y, c.w, c.h, 18, up ? g_theme->care : raised());
    if (up) gfx_rect_rounded(c.x + 2, c.y + 2, c.w - 4, c.h - 4, 16, ui_mix(g_theme->panel_tint, g_theme->care, 26));
    gfx_circle_fill(c.x + 16 + 20, c.y + 38, 20, up ? g_theme->care : g_theme->track);
    ui_glyph(UI_GLYPH_WIRED, c.x + 16 + 10, c.y + 28, 20, up ? g_theme->on_care : g_theme->ink2);
    char sub[48];
    if (up) {
        u32 ip = net_get_ip();
        ksprintf(sub, "Connected  %d.%d.%d.%d", (int)((ip >> 24) & 0xFF), (int)((ip >> 16) & 0xFF),
                 (int)((ip >> 8) & 0xFF), (int)(ip & 0xFF));
    } else kstrcpy(sub, "No cable or no link");
    i32 h1 = gfx_role_line_h(FONT_ROLE_UI, 3), h2 = gfx_role_line_h(FONT_ROLE_UI, 1);
    gfx_str_role(c.x + 72, c.y + (76 - h1 - h2) / 2, "Ethernet", g_theme->ink, FONT_ROLE_UI, 3, FONT_W_BOLD);
    gfx_str_role(c.x + 72, c.y + (76 - h1 - h2) / 2 + h1, sub, g_theme->ink2, FONT_ROLE_UI, 1, FONT_W_REGULAR);
    if (up) ui_glyph(UI_GLYPH_CHECK, c.x + c.w - 40, c.y + 28, 20, g_theme->care);
    ry += 76 + 18;
    ui_glyph(UI_GLYPH_WIFI, rx + 4, ry, 20, g_theme->muted);
    gfx_str_role_wrap(rx + 34, ry + 1, rw - 34, up ? "No Wi-Fi adapter found. You're online through Ethernet."
                                                   : "No Wi-Fi adapter found, and no Ethernet link yet. You can carry on offline.",
                      g_theme->muted, FONT_ROLE_UI, 1, FONT_W_REGULAR, 4);
}

static void draw_step2(const olayout_t *o, const mouse_t *m) {
    i32 x = o->x, y = o->body_y;
    gfx_str_role(x, y, "Pick your vibe", g_theme->ink, FONT_ROLE_DISPLAY, 3, FONT_W_BOLD);
    y += gfx_role_line_h(FONT_ROLE_DISPLAY, 3) + 6;
    gfx_str_role(x, y, "Light for daytime, dark for late nights, or let CareOS switch morning and evening.",
                 g_theme->ink2, FONT_ROLE_UI, 3, FONT_W_REGULAR);

    static const char *const NAMES[3] = { "Light", "Dark", "Auto (day and night)" };
    static const u32 MODE[3] = { 1, 0, 2 };
    u32 cur = settings_get()->theme_mode;
    for (u32 i = 0; i < 3; i++) {
        rect_t r = o->vibe[i];
        bool sel = cur == MODE[i], hv = m && rect_contains(r, m->x, m->y);
        gfx_rect_rounded(r.x, r.y, r.w, r.h, 22, sel ? g_theme->care : ui_mix(raised(), g_theme->ink, 26));
        gfx_rect_rounded(r.x + 2, r.y + 2, r.w - 4, r.h - 4, 20, sel ? ui_mix(raised(), g_theme->care, 30) : raised());
        if (hv && !sel) gfx_rect_rounded_blend(r.x + 2, r.y + 2, r.w - 4, r.h - 4, 20, 0xFFFFFF, 12);
        rect_t pv = rect_make(r.x + 10, r.y + 10, r.w - 20, 150);
        mini_desktop(pv, (int)i);
        gfx_str_role_fit(r.x + 16, pv.y + 150 + 12, r.w - 56, NAMES[i], g_theme->ink, FONT_ROLE_UI, 2, FONT_W_BOLD);
        if (sel) ui_glyph(UI_GLYPH_CHECK, r.x + r.w - 36, pv.y + 150 + 12, 20, g_theme->care);
    }

    caps(x, o->accent[0].y - gfx_role_line_h(FONT_ROLE_UI, 1) - 10, "ACCENT COLOR");
    u32 n = theme_accent_count(), sel = theme_get_accent();
    for (u32 i = 0; i < n && i < 8; i++) {
        rect_t r = o->accent[i];
        if (i == sel) { gfx_circle_fill(r.x + 20, r.y + 20, 25, 0xFFFFFF); gfx_circle_fill(r.x + 20, r.y + 20, 23, g_theme->panel_tint); }
        gfx_circle_fill(r.x + 20, r.y + 20, 20, theme_accent_swatch(i));
        if (i == n - 1) {   /* Auto: picks a colour from the wallpaper */
            i32 tw = gfx_str_role_width("A", FONT_ROLE_UI, 1, FONT_W_BOLD);
            gfx_str_role(r.x + 20 - tw / 2, r.y + 20 - gfx_role_line_h(FONT_ROLE_UI, 1) / 2, "A", 0xFFFFFF, FONT_ROLE_UI, 1, FONT_W_BOLD);
        }
    }
    gfx_str_role(o->accent[n - 1].x + 56, o->accent[0].y + 20 - gfx_role_line_h(FONT_ROLE_UI, 1) / 2,
                 theme_accent_name(sel), g_theme->ink2, FONT_ROLE_UI, 1, FONT_W_BOLD);
}

static void draw_step3(const ob_t *O, const olayout_t *o) {
    i32 x = o->x, y = o->body_y, cw = (o->w - 48) / 2;
    gfx_circle_fill(x + 36, y + 36, 36, g_theme->success);
    ui_glyph(UI_GLYPH_CHECK, x + 36 - 17, y + 36 - 17, 34, 0x06281A);
    y += 72 + 18;
    gfx_str_role(x, y, "You're all set.", g_theme->ink, FONT_ROLE_DISPLAY, 3, FONT_W_BOLD);
    y += gfx_role_line_h(FONT_ROLE_DISPLAY, 3) + 8;
    gfx_str_role(x, y, "A few things worth knowing before you dive in.", g_theme->ink2, FONT_ROLE_UI, 3, FONT_W_REGULAR);
    y += gfx_role_line_h(FONT_ROLE_UI, 3) + 22;

    char line[96];
    const careos_settings_t *cfg = settings_get();
    static const char *const MODES[3] = { "Dark", "Light", "Auto" };
    struct { ui_glyph_t g; char text[96]; } rows[3];
    rows[0].g = UI_GLYPH_USER;
    ksprintf(rows[0].text, "Account  %s", O->prof.display_name[0] ? O->prof.display_name : O->user);
    rows[1].g = UI_GLYPH_WIRED;
    kstrcpy(rows[1].text, net_is_up() ? "Online through Ethernet" : "Offline for now");
    rows[2].g = cfg->theme_mode == 0 ? UI_GLYPH_MOON : UI_GLYPH_SUN;
    ksprintf(line, "%s theme, %s accent", MODES[cfg->theme_mode > 2 ? 0 : cfg->theme_mode], theme_accent_name(theme_get_accent()));
    kstrcpy(rows[2].text, line);
    for (u32 i = 0; i < 3; i++, y += 30) {
        ui_glyph(rows[i].g, x, y, 18, g_theme->ink2);
        gfx_str_role(x + 28, y, rows[i].text, g_theme->is_dark ? 0xD5DAEAu : g_theme->ink2, FONT_ROLE_UI, 2, FONT_W_REGULAR);
    }

    i32 rx = x + cw + 48, rw = cw, ry = o->body_y;
    static const char *const TT[3] = { "All your apps live here", "Quick controls", "Make it yours" };
    static const char *const TS[3] = { "The grid at the left of the dock, or tap the Super key.",
                                       "Network, sound, brightness and Focus, top right.",
                                       "Wallpapers and accent colours live in Settings." };
    for (u32 i = 0; i < 3; i++, ry += 100 + 12) {
        rect_t c = rect_make(rx, ry, rw, 100);
        gfx_rect_rounded(c.x, c.y, c.w, c.h, 20, raised());
        if (i == 0) ui_app_icon(UI_ICON_LAUNCHER, c.x + 18, c.y + 24, 52);
        else {
            gfx_rect_rounded(c.x + 18, c.y + 24, 52, 52, 15, g_theme->track);
            ui_glyph(i == 1 ? UI_GLYPH_SLIDERS : UI_GLYPH_HEART, c.x + 18 + 15, c.y + 24 + 15, 22, g_theme->ink);
        }
        gfx_str_role(c.x + 86, c.y + 22, TT[i], g_theme->ink, FONT_ROLE_UI, 3, FONT_W_BOLD);
        gfx_str_role_wrap(c.x + 86, c.y + 22 + gfx_role_line_h(FONT_ROLE_UI, 3) + 2, c.w - 104, TS[i],
                          g_theme->ink2, FONT_ROLE_UI, 1, FONT_W_REGULAR, 2);
    }
}

static void draw(const ob_t *O, const olayout_t *o, const mouse_t *m) {
    gui_wallpaper_draw();
    rect_t c = o->card;
    ui_glass(60, c, 32, g_theme->is_dark ? 0x12172Au : g_theme->panel_tint, 204, 30, UI_GLASS_HAIRLINE | UI_GLASS_SHADOW_L);

    /* progress */
    i32 py = c.y + 40, tw = gfx_str_role_width("Step 4 of 4", FONT_ROLE_UI, 1, FONT_W_BOLD);
    i32 bw = (o->w - tw - 16 - 18) / 4;
    for (u32 i = 0; i < ONBOARD_STEPS; i++)
        gfx_rect_rounded(o->x + (i32)i * (bw + 6), py + 7, bw, 6, 3, i <= O->step ? g_theme->care : g_theme->track);
    char st[16];
    ksprintf(st, "Step %u of 4", O->step + 1);
    gfx_str_role(o->x + o->w - tw, py, st, g_theme->muted, FONT_ROLE_UI, 1, FONT_W_BOLD);

    if (O->step == 0) draw_step0(O, o, m);
    else if (O->step == 1) draw_step1(o);
    else if (O->step == 2) draw_step2(o, m);
    else draw_step3(O, o);

    if (O->step > O->first) btn(o->back, "Back", false, false, m && rect_contains(o->back, m->x, m->y));
    if (O->step == 1) {
        bool hv = m && rect_contains(o->skip, m->x, m->y);
        gfx_str_role(o->skip.x + 12, o->skip.y + (52 - gfx_role_line_h(FONT_ROLE_UI, 1)) / 2, "Skip for now",
                     hv ? g_theme->ink : (g_theme->is_dark ? 0xB7C1FFu : g_theme->calm), FONT_ROLE_UI, 1, FONT_W_BOLD);
    }
    btn(o->next, O->step == 3 ? "Go to my desktop" : "Continue", true, true, m && rect_contains(o->next, m->x, m->y));

    if (O->confirm_exit) {
        gfx_rect_blend(0, 0, (i32)SCREEN_W, (i32)SCREEN_H, 0x050814, 120);
        rect_t b = o->confirm_box;
        ui_panel(b, 24, g_theme->is_dark ? 0x151B2Eu : g_theme->surface, UI_GLASS_HAIRLINE | UI_GLASS_SHADOW_L);
        gfx_str_role(b.x + 24, b.y + 24, "Finish setup later?", g_theme->ink, FONT_ROLE_DISPLAY, 1, FONT_W_REGULAR);
        gfx_str_role_wrap(b.x + 24, b.y + 24 + gfx_role_line_h(FONT_ROLE_DISPLAY, 1) + 6, b.w - 48,
                          "Everything you picked so far is saved. The rest lives in Settings.",
                          g_theme->ink2, FONT_ROLE_UI, 1, FONT_W_REGULAR, 3);
        btn(o->confirm_keep, "Keep going", false, false, m && rect_contains(o->confirm_keep, m->x, m->y));
        btn(o->confirm_later, "Finish later", true, false, m && rect_contains(o->confirm_later, m->x, m->y));
    }
}

static void save_step(ob_t *O, bool finished) {
    O->prof.onboarded = finished;
    O->prof.onboard_step = (u8)(O->step < ONBOARD_STEPS ? O->step : 0);
    if (user_profile_set(O->user, &O->prof) != 0) serial_write("[onboarding] could not save progress\n");
}

static void apply_theme_mode(u32 mode) {
    settings_set_theme_mode(mode);
    bool dark = mode == 0;
    if (mode == 2) { rtc_time_t t; clock_read_local(&t); dark = auto_theme_dark(t.hour); }
    if (mode <= 1) user_set_current_theme_preference(mode);
    theme_switch(dark);
}

void session_onboarding(mouse_t *m, bool from_signup) {
    static ob_t O;
    kmemset(&O, 0, sizeof(O));
    const char *me = user_current_name();
    if (!me || !user_session_active()) return;          /* guest: nothing to set up */
    kstrncpy(O.user, me, sizeof(O.user) - 1);
    if (!user_profile_get(O.user, &O.prof)) return;
    O.step = onboard_resume(O.prof.onboard_step, O.prof.onboarded);
    if (O.step == ONBOARD_DONE) return;
    O.first = onboard_first_step(from_signup);
    if (O.step < O.first) O.step = O.first;
    serial_write("[onboarding] start\n");
    keyboard_flush();

    while (1) {
        olayout_t o;
        olay(&O, &o);
        bool finish = false;
        while (keyboard_haschar()) {
            char c = gui_read_key();
            if (c == 27) O.confirm_exit = !O.confirm_exit;
            else if ((c == '\n' || c == 13) && !O.confirm_exit) {
                O.step = onboard_next(O.step);
                if (O.step == ONBOARD_DONE) finish = true; else save_step(&O, false);
            }
        }
        mouse_update(m);
        if (m->left_clicked) {
            if (O.confirm_exit) {
                if (rect_contains(o.confirm_later, m->x, m->y)) finish = true;
                else if (rect_contains(o.confirm_keep, m->x, m->y) || !rect_contains(o.confirm_box, m->x, m->y)) O.confirm_exit = false;
            } else if (rect_contains(o.next, m->x, m->y) || (O.step == 1 && rect_contains(o.skip, m->x, m->y))) {
                O.step = onboard_next(O.step);
                if (O.step == ONBOARD_DONE) finish = true; else save_step(&O, false);
            } else if (O.step > O.first && rect_contains(o.back, m->x, m->y)) {
                O.step = onboard_back(O.step, O.first);
                save_step(&O, false);
            } else if (O.step == 0 && rect_contains(o.region, m->x, m->y)) {
                settings_set_tz_offset(REGIONS[(region_index() + 1) % REGION_COUNT].offset_min);
            } else if (O.step == 2) {
                static const u32 MODE[3] = { 1, 0, 2 };
                for (u32 i = 0; i < 3; i++) if (rect_contains(o.vibe[i], m->x, m->y)) apply_theme_mode(MODE[i]);
                for (u32 i = 0; i < theme_accent_count() && i < 8; i++)
                    if (rect_contains(o.accent[i], m->x, m->y)) settings_set_accent(i);
            }
        }
        if (finish) break;
        draw(&O, &o, m);
        session_frame_end(m);
    }
    mini_free();
    O.step = ONBOARD_DONE;
    save_step(&O, true);
    serial_write("[onboarding] done\n");
    keyboard_flush();
}
