/* =============================================================================
 * CareOS gui/gui.c -- main GUI entry point with splash, login, and desktop
 * ============================================================================= */
#include "kernel.h"
#include "gui.h"
#include "ui.h"
#include "session/session.h"
#include "image.h"
#include "resource_cache.h"
#include "icon.h"
#include "perf.h"
#include "keychord.h"


/* Set by the launcher's footer session buttons (Lock / Log Out / Switch);
   consumed once per frame at the top of gui_run()'s main loop. */
volatile session_action_t g_session_action = SESSION_ACT_NONE;

static i32 ui_clampi(i32 v, i32 lo, i32 hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

void gui_init(u32 *fb, u32 w, u32 h, u32 pitch) {
    serial_write("  [gui_init] gfx_init\n");
    gfx_init(fb, w, h, pitch);

    /* Load default theme based on settings */
    {
        const careos_settings_t *cfg = settings_get();
        theme_switch(cfg ? (cfg->theme == 0) : true);
    }

    /* Before mouse/wm so the first painted frame already has its artwork.
     * Publishes /system/icons and /system/wallpapers and starts the cache. */
    serial_write("  [gui_init] resources_init\n");
    resources_init();

    serial_write("  [gui_init] mouse_init\n");
    mouse_init();
    serial_write("  [gui_init] wm_init\n");
    wm_init();
    serial_write("  [gui_init] done\n");
}

static void draw_elite_wallpaper(void) {
    i32 sw = (i32)SCREEN_W;
    i32 sh = (i32)SCREEN_H;

    /* The selected background: the procedural Care one, or a photo slot.
     * wallpaper_draw() cover-fits photos, so they need no particular aspect
     * ratio and survive a resolution change without reloading. */
    if (wallpaper_draw(0, 0, sw, sh)) return;
    /* No artwork for the chosen slot: fall back to the Care background. */
    wallpaper_care_draw(0, 0, sw, sh);
}

void gui_wallpaper_draw(void) { draw_elite_wallpaper(); }

/* Desktop backdrop for the main loop: blit the cached wallpaper when it is
 * valid (one memcpy), otherwise render it the slow way once and cache the
 * result. draw_elite_wallpaper() is still used directly on the login/lock
 * screens, which composite changing overlays on top and are not hot paths. */
/* =============================================================================
 * CareOS Control Center & Notification Center State & Core Implementation
 * ============================================================================= */

/* Control Center State */
static bool s_control_center_open = false;
static int  s_brightness = 100;      /* 10% to 100%, software dimming (persisted) */

void gui_set_brightness_live(u32 pct) {
    if (pct < 10) pct = 10;
    if (pct > 100) pct = 100;
    s_brightness = (int)pct;
}

/* Notification Center: the store and cards live in gui/notify.c. */
static bool s_notif_center_open = false;

/* Read one key, collapsing the driver's "ESC [ X" navigation sequences into a
 * single KEY_NAV_* code. The driver pushes all three bytes from one IRQ, so if
 * '[' is not already queued behind the ESC it was a genuine Escape press. */
char gui_read_key(void) {
    char c = keyboard_getchar();
    if (c != 0x1B || keyboard_peekchar() != '[') return c;
    keyboard_getchar();                               /* '[' */
    char f = keyboard_haschar() ? keyboard_getchar() : 0;
    switch (f) {
    case 'A': return (char)KEY_NAV_UP;
    case 'B': return (char)KEY_NAV_DOWN;
    case 'C': return (char)KEY_NAV_RIGHT;
    case 'D': return (char)KEY_NAV_LEFT;
    case 'H': return (char)KEY_NAV_HOME;
    case 'F': return (char)KEY_NAV_END;
    case '5': if (keyboard_peekchar() == '~') keyboard_getchar(); return (char)KEY_NAV_PGUP;
    case '6': if (keyboard_peekchar() == '~') keyboard_getchar(); return (char)KEY_NAV_PGDN;
    default:  return 0;                               /* unknown: drop */
    }
}

/* Honoured by the desktop loop: lock the session on the next iteration. */
volatile bool g_lock_request = false;
/* Shell-originated notifications (Control Center, screenshots). */
static void control_center_add_notif(const char *title, const char *body, u32 color) {
    (void)color;
    notify_push_ex(APP_NONE, title, body, NULL, NULL, NULL);
}

/* ── Menu bar (visual-system spec §8, TopBar artboard) ──────────────────────
 * 40 px glass band. Left: the CareOS menu, the focused app's name. Right:
 * Network, Sound, Control Center and the date/time (the artboard's Search
 * button waits for the AI assistant; the launcher has Super and the dock); every icon is
 * backed by real state (e1000 link, speaker mute) and battery is omitted
 * because nothing reports one. Draw and click share menubar_layout(). */
typedef struct {
    rect_t logo, app, net, sound, cc, clock;
    char   date_s[24], time_s[16];   /* drawn as "date · time" */
    char   app_s[32];
} menubar_layout_t;

static bool s_care_menu_open = false;

#define MB_BTN_H   30
#define MB_BTN_Y   ((TOPBAR_H - MB_BTN_H) / 2)
#define MB_ICON_W  34
#define MB_DOT_GAP 15                 /* room for the "·" between date and time */

/* Local wall-clock time: one helper for the whole shell (gui/widgets.c). */
static void menubar_local_time(rtc_time_t *t) { clock_read_local(t); }
/* 0 = Sunday (Sakamoto). */
static int weekday_of(int y, int m, int d) {
    static const int k[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    if (m < 3) y -= 1;
    return (y + y / 4 - y / 100 + y / 400 + k[(m - 1) % 12] + d) % 7;
}

static void menubar_layout(menubar_layout_t *L) {
    i32 sw = (i32)SCREEN_W;

    rtc_time_t t;
    menubar_local_time(&t);
    static const char *const DAYS[7] = { "Sun","Mon","Tue","Wed","Thu","Fri","Sat" };
    static const char *const MONS[12] = { "Jan","Feb","Mar","Apr","May","Jun",
                                          "Jul","Aug","Sep","Oct","Nov","Dec" };
    const char *mon = (t.month >= 1 && t.month <= 12) ? MONS[t.month - 1] : "---";
    const char *day = DAYS[weekday_of((int)t.year, (int)t.month, (int)t.day)];
    const careos_settings_t *cfg = settings_get();
    ksprintf(L->date_s, "%s %s %d", day, mon, (int)t.day);
    if (cfg && cfg->clock_24h) {
        ksprintf(L->time_s, "%02d:%02d", (int)t.hour, (int)t.minute);
    } else {
        int h = (int)t.hour % 12;
        if (h == 0) h = 12;
        ksprintf(L->time_s, "%d:%02d %s", h, (int)t.minute, t.hour >= 12 ? "PM" : "AM");
    }

    window_t *fw = wm_focused();
    const char *app = launcher_open ? "Launcher"
                    : (fw && fw->active && !fw->minimized && fw->title[0]) ? fw->title : "Desktop";
    kstrncpy(L->app_s, app, sizeof(L->app_s) - 1);
    L->app_s[sizeof(L->app_s) - 1] = '\0';

    i32 logo_w = 10 + 18 + 8 + gfx_str_role_width("CareOS", FONT_ROLE_DISPLAY, 0, FONT_W_BOLD) + 10;
    L->logo = rect_make(14, MB_BTN_Y, logo_w, MB_BTN_H);
    L->app  = rect_make(L->logo.x + logo_w + 4, MB_BTN_Y,
                        gfx_str_role_width(L->app_s, FONT_ROLE_UI, 1, FONT_W_BOLD) + 20, MB_BTN_H);

    i32 cw = gfx_str_role_width(L->date_s, FONT_ROLE_UI, 1, FONT_W_BOLD) + MB_DOT_GAP
           + gfx_str_role_width(L->time_s, FONT_ROLE_UI, 1, FONT_W_BOLD) + 20;
    i32 x = sw - 14 - cw;
    L->clock  = rect_make(x, MB_BTN_Y, cw, MB_BTN_H);
    x -= 2 + MB_ICON_W; L->cc     = rect_make(x, MB_BTN_Y, MB_ICON_W, MB_BTN_H);
    x -= 2 + MB_ICON_W; L->sound  = rect_make(x, MB_BTN_Y, MB_ICON_W, MB_BTN_H);
    x -= 2 + MB_ICON_W; L->net    = rect_make(x, MB_BTN_Y, MB_ICON_W, MB_BTN_H);
}

/* The CareOS menu (heart logo). Real actions only. */
enum { CM_ABOUT, CM_SETTINGS, CM_SEP, CM_LOCK, CM_LOGOUT, CM_POWER, CM_COUNT };
static const char *const CARE_MENU[CM_COUNT] = {
    "About this computer", "Settings...", NULL, "Lock", "Log out", "Power..."
};
#define CM_ITEM_H 34
#define CM_SEP_H  9
#define CM_W      232

static rect_t care_menu_rect(void) {
    i32 h = 8 * 2 + (CM_COUNT - 1) * CM_ITEM_H + CM_SEP_H;
    return rect_make(14, TOPBAR_H + 6, CM_W, h);
}

static rect_t care_menu_item(u32 i) {
    rect_t r = care_menu_rect();
    i32 y = r.y + 8;
    for (u32 k = 0; k < i; k++) y += (k == CM_SEP) ? CM_SEP_H : CM_ITEM_H;
    return rect_make(r.x + 6, y, r.w - 12, i == CM_SEP ? CM_SEP_H : CM_ITEM_H);
}

static void shell_open_app(app_id_t app, const char *name) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        window_t *w = wm_get_window(i);
        if (w && w->active && w->app == app) {
            if (w->minimized) wm_minimize(w);
            wm_focus(w);
            return;
        }
    }
    i32 sw = (i32)SCREEN_W, sh = (i32)SCREEN_H, ww, wh;
    app_default_size(app, sw, sh, &ww, &wh);
    wm_open(app, name, (sw - ww) / 2, (sh - wh) / 2 - 10, ww, wh);
}

static void draw_care_menu(const mouse_t *m) {
    if (!s_care_menu_open) return;
    rect_t r = care_menu_rect();
    ui_glass(2, r, 14, g_theme->panel_tint, (u8)(g_theme->panel_alpha + 30 > 255 ? 255 : g_theme->panel_alpha + 30),
             30, UI_GLASS_HAIRLINE | UI_GLASS_SHADOW_M);
    for (u32 i = 0; i < CM_COUNT; i++) {
        rect_t it = care_menu_item(i);
        if (i == CM_SEP) {
            gfx_hline(it.x + 8, it.y + it.h / 2, it.w - 16,
                      ui_mix(g_theme->panel_tint, g_theme->hairline, g_theme->hairline_alpha));
            continue;
        }
        if (m && rect_contains(it, m->x, m->y))
            gfx_rect_rounded(it.x, it.y, it.w, it.h, 8, g_theme->care);
        bool hot = m && rect_contains(it, m->x, m->y);
        gfx_str_role(it.x + 12, it.y + (it.h - gfx_role_line_h(FONT_ROLE_UI, 1)) / 2, CARE_MENU[i],
                     hot ? g_theme->on_care : g_theme->ink, FONT_ROLE_UI, 1, FONT_W_REGULAR);
    }
}

static bool handle_care_menu_mouse(mouse_t *m) {
    if (!s_care_menu_open || !m || !m->left_clicked) return false;
    rect_t r = care_menu_rect();
    if (!rect_contains(r, m->x, m->y)) {
        /* A click on the logo itself toggles; let the bar handle that one. */
        if (m->y < TOPBAR_H) return false;
        s_care_menu_open = false;
        return true;
    }
    for (u32 i = 0; i < CM_COUNT; i++) {
        if (i == CM_SEP || !rect_contains(care_menu_item(i), m->x, m->y)) continue;
        s_care_menu_open = false;
        switch (i) {
        case CM_ABOUT:    shell_open_app(APP_ABOUT, "About"); break;
        case CM_SETTINGS: shell_open_app(APP_SETTINGS, "Settings"); break;
        case CM_LOCK:     g_session_action = SESSION_ACT_LOCK; break;
        case CM_LOGOUT:   g_session_action = SESSION_ACT_LOGOUT; break;   /* closes this user's windows */
        case CM_POWER:
            if (session_power_menu(m, false) == POWER_LOGOUT) g_session_action = SESSION_ACT_LOGOUT;
            break;
        default: break;
        }
        return true;
    }
    return true;
}

/* ── Network popover (spec §13, WifiMenu artboard) ──────────────────────────
 * Wired only today: the real e1000 link and IP, a note that there is no
 * wireless adapter, and a link to Network settings. The e1000 driver cannot
 * be switched off, so the header shows the link state rather than a toggle
 * that would do nothing. The Wi-Fi list and join dialog arrive with a
 * wireless driver. */
static bool s_net_popover_open = false;

#define NP_W   340
#define NP_PAD 14

typedef struct { rect_t panel, card, settings; } netpop_layout_t;

static void netpop_layout(netpop_layout_t *P, rect_t anchor) {
    i32 x = anchor.x + anchor.w / 2 - NP_W / 2;
    if (x + NP_W > (i32)SCREEN_W - 12) x = (i32)SCREEN_W - 12 - NP_W;
    i32 y = TOPBAR_H + 10;
    i32 cy = y + NP_PAD + 4 + 30 + 6;                    /* header row */
    P->card = rect_make(x + NP_PAD, cy, NP_W - 2 * NP_PAD, 60);
    cy += 60 + 6 + 8 + gfx_role_line_h(FONT_ROLE_UI, 0) + 2 + 40 + 6 + 1 + 6;
    P->settings = rect_make(x + NP_PAD, cy, NP_W - 2 * NP_PAD, 40);
    P->panel = rect_make(x, y, NP_W, cy + 40 + NP_PAD - y);
}

static void draw_net_popover(const mouse_t *m, rect_t anchor) {
    if (!s_net_popover_open) return;
    netpop_layout_t P;
    netpop_layout(&P, anchor);
    ui_glass(6, P.panel, UI_R_PANEL, g_theme->panel_tint, g_theme->is_dark ? 230 : 235, 36,
             UI_GLASS_HAIRLINE | UI_GLASS_SHADOW_L);

    bool up = net_is_up();
    i32 x = P.panel.x + NP_PAD, w = NP_W - 2 * NP_PAD, y = P.panel.y + NP_PAD + 4;
    i32 dh = gfx_role_line_h(FONT_ROLE_DISPLAY, 0);
    gfx_str_role(x + 6, y + (30 - dh) / 2, "Ethernet", g_theme->ink, FONT_ROLE_DISPLAY, 0, FONT_W_REGULAR);
    const char *st = up ? "Connected" : "Disconnected";
    i32 sw = gfx_str_role_width(st, FONT_ROLE_UI, 0, FONT_W_BOLD);
    gfx_str_role(x + w - 6 - sw, y + (30 - gfx_role_line_h(FONT_ROLE_UI, 0)) / 2, st,
                 up ? g_theme->success : g_theme->muted, FONT_ROLE_UI, 0, FONT_W_BOLD);

    /* The connection card: coral wash while connected. */
    rect_t c = P.card;
    gfx_rect_rounded_blend(c.x, c.y, c.w, c.h, 16, up ? g_theme->care : g_theme->raised2, up ? 36 : 255);
    i32 ccx = c.x + 12 + 18, ccy = c.y + c.h / 2;
    gfx_circle_fill(ccx, ccy, 18, up ? g_theme->care : g_theme->track);
    ui_glyph(UI_GLYPH_WIRED, ccx - 9, ccy - 9, 18, up ? g_theme->on_care : g_theme->ink2);
    char sub[48];
    if (up) {
        u32 ip = net_get_ip();
        ksprintf(sub, "Connected  %d.%d.%d.%d", (int)((ip >> 24) & 0xFF), (int)((ip >> 16) & 0xFF),
                 (int)((ip >> 8) & 0xFF), (int)(ip & 0xFF));
    } else {
        kstrcpy(sub, "Cable unplugged or no link");
    }
    i32 h1 = gfx_role_line_h(FONT_ROLE_UI, 2), h2 = gfx_role_line_h(FONT_ROLE_UI, 0);
    i32 ty = c.y + (c.h - h1 - h2) / 2;
    gfx_str_role(c.x + 12 + 36 + 12, ty, "Wired network", g_theme->ink, FONT_ROLE_UI, 2, FONT_W_BOLD);
    gfx_str_role(c.x + 12 + 36 + 12, ty + h1, sub, g_theme->is_dark ? 0xD5DAEAu : g_theme->ink2,
                 FONT_ROLE_UI, 0, FONT_W_REGULAR);

    y = c.y + c.h + 6 + 8;
    gfx_str_role(x + 6, y, "WI-FI", g_theme->muted, FONT_ROLE_UI, 0, FONT_W_BOLD);
    y += gfx_role_line_h(FONT_ROLE_UI, 0) + 2;
    ui_glyph(UI_GLYPH_WIFI, x + 12, y + 10, 20, g_theme->muted);
    gfx_str_role(x + 44, y + (40 - gfx_role_line_h(FONT_ROLE_UI, 1)) / 2, "No wireless adapter",
                 g_theme->muted, FONT_ROLE_UI, 1, FONT_W_REGULAR);
    y += 40 + 6;
    gfx_hline(x + 4, y, w - 8, ui_mix(g_theme->panel_tint, g_theme->hairline, g_theme->hairline_alpha));

    rect_t s = P.settings;
    if (m && rect_contains(s, m->x, m->y))
        gfx_rect_rounded_blend(s.x, s.y, s.w, s.h, 12, g_theme->is_dark ? 0xFFFFFFu : g_theme->ink, 20);
    gfx_str_role(s.x + 12, s.y + (s.h - gfx_role_line_h(FONT_ROLE_UI, 1)) / 2, "Network settings...",
                 g_theme->ink, FONT_ROLE_UI, 1, FONT_W_BOLD);
}

/* True when the click belonged to the popover (or closed it). */
static bool handle_net_popover_mouse(mouse_t *m, rect_t anchor) {
    if (!s_net_popover_open || !m || !m->left_clicked) return false;
    netpop_layout_t P;
    netpop_layout(&P, anchor);
    if (rect_contains(P.settings, m->x, m->y)) {
        s_net_popover_open = false;
        shell_open_app(APP_SETTINGS, "Settings");
        return true;
    }
    if (rect_contains(P.panel, m->x, m->y)) return true;
    if (m->y < TOPBAR_H) return false;      /* the bar handles its own buttons */
    s_net_popover_open = false;
    return true;
}

static void mb_text_button(rect_t r, const char *s, u32 fg, font_role_t role, font_weight_t w,
                           const mouse_t *m, bool active) {
    bool hover = m && rect_contains(r, m->x, m->y);
    if (active || hover) {
        u32 over = g_theme->is_dark ? 0xFFFFFFu : g_theme->ink;
        gfx_rect_rounded_blend(r.x, r.y, r.w, r.h, 8, over, active ? (g_theme->is_dark ? 36 : 26) : (g_theme->is_dark ? 22 : 16));
    }
    gfx_str_role(r.x + 10, r.y + (r.h - gfx_role_line_h(role, 1)) / 2, s, fg, role, 1, w);
}

static void draw_top_bar(const mouse_t *m) {
    i32 sw = (i32)SCREEN_W;
    menubar_layout_t L;
    menubar_layout(&L);

    ui_glass(1, rect_make(0, 0, sw, TOPBAR_H), 0, g_theme->menubar_tint, g_theme->menubar_alpha, 24, 0);
    gfx_hline(0, TOPBAR_H - 1, sw, ui_mix(g_theme->menubar_tint, g_theme->hairline,
                                          g_theme->is_dark ? 15 : 20));

    /* CareOS menu: heart logo + wordmark */
    {
        bool hover = m && rect_contains(L.logo, m->x, m->y);
        if (s_care_menu_open || hover) {
            u32 over = g_theme->is_dark ? 0xFFFFFFu : g_theme->ink;
            gfx_rect_rounded_blend(L.logo.x, L.logo.y, L.logo.w, L.logo.h, 8, over,
                                   s_care_menu_open ? (g_theme->is_dark ? 36 : 26) : (g_theme->is_dark ? 22 : 16));
        }
        ui_care_logo(L.logo.x + 10, L.logo.y + (MB_BTN_H - 18) / 2, 18);
        gfx_str_role(L.logo.x + 36, L.logo.y + (MB_BTN_H - gfx_role_line_h(FONT_ROLE_DISPLAY, 0)) / 2,
                     "CareOS", g_theme->ink, FONT_ROLE_DISPLAY, 0, FONT_W_BOLD);
    }
    mb_text_button(L.app, L.app_s, g_theme->ink, FONT_ROLE_UI, FONT_W_BOLD, m, false);

    /* Workspace hint, only off the first desktop (Ctrl+1..4 switch). */
    {
        extern u32 g_current_desktop;
        if (g_current_desktop != 0) {
            i32 dx = L.app.x + L.app.w + 10, dcy = TOPBAR_H / 2;
            for (u32 d = 0; d < 4; d++) {
                if (d == g_current_desktop) gfx_circle_fill(dx + (i32)d * 12 + 3, dcy, 3, g_theme->care);
                else gfx_circle_fill(dx + (i32)d * 12 + 3, dcy, 2, g_theme->ink2);
            }
        }
    }

    {
        bool up = net_is_up();
        ui_state_t st = ui_state_for(L.net, m, false, false);
        if (!up && st == UI_NORMAL) st = UI_DISABLED;
        ui_icon_button(L.net, UI_GLYPH_WIRED, st, s_net_popover_open);
    }
    ui_icon_button(L.sound, speaker_muted() ? UI_GLYPH_MUTE : UI_GLYPH_SOUND,
                   ui_state_for(L.sound, m, false, false), false);
    ui_icon_button(L.cc, UI_GLYPH_SLIDERS, ui_state_for(L.cc, m, false, false), s_control_center_open);
    mb_text_button(L.clock, L.date_s, g_theme->ink, FONT_ROLE_UI, FONT_W_BOLD, m, s_notif_center_open);
    {
        i32 dx = L.clock.x + 10 + gfx_str_role_width(L.date_s, FONT_ROLE_UI, 1, FONT_W_BOLD);
        gfx_circle_fill(dx + MB_DOT_GAP / 2, L.clock.y + MB_BTN_H / 2, 1, g_theme->ink);
        gfx_str_role(dx + MB_DOT_GAP, L.clock.y + (MB_BTN_H - gfx_role_line_h(FONT_ROLE_UI, 1)) / 2,
                     L.time_s, g_theme->ink, FONT_ROLE_UI, 1, FONT_W_BOLD);
    }

    draw_care_menu(m);
    draw_net_popover(m, L.net);
}

static bool handle_top_bar_mouse(mouse_t *m) {
    if (handle_care_menu_mouse(m)) return true;
    if (!m || !m->left_clicked) return false;

    menubar_layout_t L;
    menubar_layout(&L);
    if (handle_net_popover_mouse(m, L.net)) return true;
    if (m->y >= TOPBAR_H) return false;

    if (rect_contains(L.logo, m->x, m->y)) {
        s_care_menu_open = !s_care_menu_open;
        if (s_care_menu_open) { s_notif_center_open = false; s_control_center_open = false; launcher_open = false; }
        return true;
    }
    s_care_menu_open = false;
    if (rect_contains(L.net, m->x, m->y)) {
        s_net_popover_open = !s_net_popover_open;
        if (s_net_popover_open) { s_control_center_open = false; s_notif_center_open = false; }
        return true;
    }
    s_net_popover_open = false;
    if (rect_contains(L.sound, m->x, m->y)) {
        speaker_set_muted(!speaker_muted());
        return true;
    }
    if (rect_contains(L.cc, m->x, m->y)) {
        s_control_center_open = !s_control_center_open;
        if (s_control_center_open) s_notif_center_open = false;
        return true;
    }
    if (rect_contains(L.clock, m->x, m->y)) {
        s_notif_center_open = !s_notif_center_open;
        if (s_notif_center_open) s_control_center_open = false;
        return true;
    }
    return true;   /* the bar itself swallows clicks */
}

/* ── Control Center (visual-system spec §12, ControlCenter artboard) ───────
 * Real state only: the e1000 link, theme, Do Not Disturb, lock, screenshot,
 * software brightness and uptime. Bluetooth, the sound slider, media and the
 * battery stay hidden until CareOS has a device behind them. */
#define CC_W    384
#define CC_PAD  16
#define CC_GAP  12

typedef struct {
    rect_t panel, net, dark, dnd, lock, shot, sliders, bright, settings, power;
} cc_layout_t;

static bool s_screenshot_pending = false;

static void cc_layout(cc_layout_t *L) {
    i32 x = (i32)SCREEN_W - 24 - CC_W, y = TOPBAR_H + 12;
    i32 ix = x + CC_PAD, iw = CC_W - 2 * CC_PAD, cy = y + CC_PAD;
    L->net = rect_make(ix, cy, iw, 72);                     cy += 72 + CC_GAP;
    i32 tw = (iw - 3 * 10) / 4;
    L->dark = rect_make(ix,                cy, tw, 76);
    L->dnd  = rect_make(ix + (tw + 10),     cy, tw, 76);
    L->lock = rect_make(ix + 2 * (tw + 10), cy, tw, 76);
    L->shot = rect_make(ix + 3 * (tw + 10), cy, tw, 76);    cy += 76 + CC_GAP;
    L->sliders = rect_make(ix, cy, iw, 14 + 20 + 8 + 24 + 14);
    L->bright  = rect_make(ix + 14 + 28, cy + 14 + 20 + 8, iw - 28 - 28 - 10, 24);
    cy += L->sliders.h + CC_GAP;
    L->power    = rect_make(ix + iw - 40, cy, 40, 40);
    L->settings = rect_make(ix + iw - 86, cy, 40, 40);
    cy += 40;
    L->panel = rect_make(x, y, CC_W, cy + CC_PAD - y);
}

static void cc_round_button(rect_t r, ui_glyph_t g, const mouse_t *m) {
    bool hover = m && rect_contains(r, m->x, m->y);
    gfx_circle_fill(r.x + r.w / 2, r.y + r.h / 2, r.w / 2, g_theme->raised2);
    if (hover) gfx_circle_fill(r.x + r.w / 2, r.y + r.h / 2, r.w / 2,
                               ui_mix(g_theme->raised2, g_theme->ink, 30));
    ui_glyph(g, r.x + (r.w - 18) / 2, r.y + (r.h - 18) / 2, 18, g_theme->ink);
}

static void cc_square_tile(rect_t r, ui_glyph_t g, const char *label, bool on, u32 on_bg, u32 on_fg,
                           const mouse_t *m) {
    bool hover = m && rect_contains(r, m->x, m->y);
    u32 bg = on ? on_bg : g_theme->raised2, fg = on ? on_fg : g_theme->ink;
    gfx_rect_rounded(r.x, r.y, r.w, r.h, 20, bg);
    if (hover) gfx_rect_rounded_blend(r.x, r.y, r.w, r.h, 20, 0xFFFFFF, 20);
    i32 lh = gfx_role_line_h(FONT_ROLE_UI, 0);
    i32 top = r.y + (r.h - (20 + 6 + lh)) / 2;
    ui_glyph(g, r.x + (r.w - 20) / 2, top, 20, fg);
    i32 tw = gfx_str_role_width(label, FONT_ROLE_UI, 0, FONT_W_BOLD);
    gfx_str_role_fit(r.x + (r.w - (tw < r.w - 8 ? tw : r.w - 8)) / 2, top + 26, r.w - 8, label, fg,
                     FONT_ROLE_UI, 0, FONT_W_BOLD);
}

static void draw_control_center(mouse_t *m) {
    if (!s_control_center_open) return;
    cc_layout_t L;
    cc_layout(&L);

    ui_glass(3, L.panel, UI_R_CC, g_theme->panel_tint,
             (u8)(g_theme->is_dark ? 219 : 224), 36, UI_GLASS_HAIRLINE | UI_GLASS_SHADOW_L);

    /* Network: wired today, Wi-Fi once a wireless driver exists. */
    {
        bool up = net_is_up();
        bool hover = m && rect_contains(L.net, m->x, m->y);
        u32 bg = up ? g_theme->care : g_theme->raised2;
        u32 fg = up ? g_theme->on_care : g_theme->ink;
        gfx_rect_rounded(L.net.x, L.net.y, L.net.w, L.net.h, 20, bg);
        if (hover) gfx_rect_rounded_blend(L.net.x, L.net.y, L.net.w, L.net.h, 20, 0xFFFFFF, 20);
        i32 ccx = L.net.x + 14 + 20, ccy = L.net.y + L.net.h / 2;
        gfx_circle_fill(ccx, ccy, 20, ui_mix(bg, fg, 36));
        ui_glyph(UI_GLYPH_WIRED, ccx - 10, ccy - 10, 20, fg);
        char sub[48];
        if (up) {
            u32 ip = net_get_ip();
            ksprintf(sub, "Connected  %d.%d.%d.%d", (int)((ip >> 24) & 0xFF), (int)((ip >> 16) & 0xFF),
                     (int)((ip >> 8) & 0xFF), (int)(ip & 0xFF));
        } else {
            kstrcpy(sub, "Not connected");
        }
        i32 tx = L.net.x + 14 + 40 + 12;
        i32 h1 = gfx_role_line_h(FONT_ROLE_UI, 2), h2 = gfx_role_line_h(FONT_ROLE_UI, 0);
        i32 ty = L.net.y + (L.net.h - h1 - h2) / 2;
        gfx_str_role(tx, ty, "Ethernet", fg, FONT_ROLE_UI, 2, FONT_W_BOLD);
        gfx_str_role(tx, ty + h1, sub, fg, FONT_ROLE_UI, 0, FONT_W_BOLD);
        ui_glyph(UI_GLYPH_CHEVRON_R, L.net.x + L.net.w - 14 - 18, ccy - 9, 18, fg);
    }

    cc_square_tile(L.dark, UI_GLYPH_MOON, "Dark mode", g_theme->is_dark, g_theme->calm, 0x12163A, m);
    cc_square_tile(L.dnd, UI_GLYPH_BELL, "Focus", notify_dnd(), g_theme->calm, 0x12163A, m);
    cc_square_tile(L.lock, UI_GLYPH_LOCK, "Lock", false, 0, 0, m);
    cc_square_tile(L.shot, UI_GLYPH_CAMERA, "Screenshot", false, 0, 0, m);

    /* Sliders card: software brightness. Sound joins when there is a mixer. */
    {
        rect_t c = L.sliders;
        u32 card = g_theme->is_dark ? 0x1A2140u : g_theme->raised;
        gfx_rect_rounded(c.x, c.y, c.w, c.h, 20, card);
        gfx_str_role(c.x + 14, c.y + 14, "Display", g_theme->ink, FONT_ROLE_UI, 1, FONT_W_BOLD);
        ui_glyph(UI_GLYPH_SUN, c.x + 14, L.bright.y + 2, 20, g_theme->ink2);
        ui_slider(L.bright, (u32)s_brightness, ui_state_for(L.bright, m, false, false));
    }

    /* Footer: uptime (no battery to report), Settings, Power. */
    {
        u32 s = timer_ms() / 1000u, h = s / 3600u, mi = (s / 60u) % 60u;
        char up[40];
        if (h) ksprintf(up, "Up %u hr %u min", h, mi);
        else   ksprintf(up, "Up %u min", mi);
        gfx_str_role(L.panel.x + CC_PAD + 4, L.power.y + (40 - gfx_role_line_h(FONT_ROLE_UI, 1)) / 2,
                     up, g_theme->ink2, FONT_ROLE_UI, 1, FONT_W_BOLD);
        cc_round_button(L.settings, UI_GLYPH_SLIDERS, m);
        cc_round_button(L.power, UI_GLYPH_POWER, m);
    }
}

static bool handle_control_center_mouse(mouse_t *m) {
    if (!s_control_center_open || !m) return false;
    cc_layout_t L;
    cc_layout(&L);

    /* Dragging the brightness slider. */
    if (m->left && rect_contains(rect_make(L.bright.x - 10, L.bright.y, L.bright.w + 20, L.bright.h), m->x, m->y)) {
        i32 b = ui_clampi(((m->x - L.bright.x) * 100) / L.bright.w, 10, 100);
        if (b != s_brightness) { s_brightness = b; settings_set_brightness((u32)b); }
        return true;
    }
    if (!m->left_clicked) return false;

    if (!rect_contains(L.panel, m->x, m->y)) {
        if (m->y > TOPBAR_H) s_control_center_open = false;
        return false;
    }
    if (rect_contains(L.net, m->x, m->y)) {
        s_control_center_open = false;
        shell_open_app(APP_SETTINGS, "Settings");
    } else if (rect_contains(L.dark, m->x, m->y)) {
        bool dark = !g_theme->is_dark;
        theme_switch(dark);
        settings_set_theme(dark ? 0 : 1);
        user_set_current_theme_preference(dark ? 0 : 1);
    } else if (rect_contains(L.dnd, m->x, m->y)) {
        notify_set_dnd(!notify_dnd());
        settings_set_dnd(notify_dnd());
    } else if (rect_contains(L.lock, m->x, m->y)) {
        s_control_center_open = false;
        g_lock_request = true;
    } else if (rect_contains(L.shot, m->x, m->y)) {
        s_control_center_open = false;
        s_screenshot_pending = true;      /* taken next frame, without the panel */
    } else if (rect_contains(L.settings, m->x, m->y)) {
        s_control_center_open = false;
        shell_open_app(APP_SETTINGS, "Settings");
    } else if (rect_contains(L.power, m->x, m->y)) {
        s_control_center_open = false;
        if (session_power_menu(m, false) == POWER_LOGOUT) g_session_action = SESSION_ACT_LOGOUT;
    }
    return true;
}

/* Write the composed frame (no cursor, no panels) as a 24-bit BMP into
 * /home/Pictures. Runs once, on the frame after the Screenshot tile. */
static void take_screenshot(void) {
    i32 w = (i32)SCREEN_W, h = (i32)SCREEN_H;
    u32 row = ((u32)w * 3u + 3u) & ~3u;
    u32 size = 54u + row * (u32)h;
    u8 *bmp = (u8 *)kmalloc(size);
    u32 *px = (u32 *)kmalloc((size_t)w * (size_t)h * 4u);
    if (!bmp || !px) {
        if (bmp) kfree(bmp);
        if (px) kfree(px);
        control_center_add_notif("Screenshot", "Not enough memory to save the screen.", COL_RED);
        return;
    }
    gfx_snapshot_rect(0, 0, w, h, px);
    kmemset(bmp, 0, 54);
    bmp[0] = 'B'; bmp[1] = 'M';
    *(u32 *)(bmp + 2) = size;   *(u32 *)(bmp + 10) = 54u;  *(u32 *)(bmp + 14) = 40u;
    *(i32 *)(bmp + 18) = w;     *(i32 *)(bmp + 22) = h;
    *(u16 *)(bmp + 26) = 1;     *(u16 *)(bmp + 28) = 24;   *(u32 *)(bmp + 34) = row * (u32)h;
    for (i32 y = 0; y < h; y++) {
        u8 *dst = bmp + 54 + (u32)(h - 1 - y) * row;
        const u32 *src = px + (u32)y * (u32)w;
        for (i32 x = 0; x < w; x++) {
            dst[x * 3 + 0] = (u8)(src[x]);
            dst[x * 3 + 1] = (u8)(src[x] >> 8);
            dst[x * 3 + 2] = (u8)(src[x] >> 16);
        }
    }
    kfree(px);

    fs_node_t *home = vfs_resolve_path("/home");
    fs_node_t *pics = home ? vfs_find(home, "Pictures") : NULL;
    if (home && !pics) pics = vfs_mkdir(home, "Pictures");
    char name[32], path[64];
    fs_node_t *f = NULL;
    for (u32 n = 1; pics && n < 1000; n++) {
        ksprintf(name, "Screenshot-%u.bmp", n);
        if (vfs_find(pics, name)) continue;
        f = vfs_mkfile(pics, name);
        break;
    }
    if (f && vfs_write(f, (const char *)bmp, size) == (int)size) {
        ksprintf(path, "Saved to /home/Pictures/%s", name);
        control_center_add_notif("Screenshot", path, COL_GREEN);
    } else {
        control_center_add_notif("Screenshot", "Could not save to /home/Pictures.", COL_RED);
    }
    kfree(bmp);
}

/* The notification column (spec §12): beside the Control Center when that is
 * open, otherwise on its own at the right edge when the clock is clicked. */
static i32 notif_column_x(void) {
    i32 sw = (i32)SCREEN_W;
    return s_control_center_open ? sw - 24 - CC_W - 16 - 360 : sw - 24 - 360;
}

static bool notif_column_shown(void) {
    return s_notif_center_open || (s_control_center_open && notify_count() > 0);
}

static void draw_notification_center(mouse_t *m) {
    bool shown = notif_column_shown();
    notify_center_set_visible(shown);
    if (shown) notify_center_draw(notif_column_x(), m);
}

static bool handle_notification_center_mouse(mouse_t *m) {
    if (!notif_column_shown() || !m || !m->left_clicked) return false;
    if (notify_center_click(notif_column_x(), m)) return true;
    /* A click elsewhere below the menu bar closes the stand-alone column. */
    if (s_notif_center_open && m->y > TOPBAR_H) s_notif_center_open = false;
    return false;
}

void gui_desktop_fade_in(mouse_t *m);
static void desktop_fade_in(mouse_t *mouse) { gui_desktop_fade_in(mouse); }
void gui_desktop_fade_in(mouse_t *mouse) {
    for (int step = 10; step >= 0; step--) {
        gfx_clear(COL_BG);
        desktop_draw();
        wm_draw_all();
        taskbar_draw();
        gfx_rect_blend(0, 0, (i32)SCREEN_W, (i32)SCREEN_H, COL_BLACK, (u8)(step * 22));
        mouse_draw_cursor(mouse->x, mouse->y);
        gfx_flip();
        timer_wait(14);
    }
}

static void run_matrix_screensaver(mouse_t *mouse) {
    i32 sw = (i32)SCREEN_W;
    i32 sh = (i32)SCREEN_H;
    /* The rain falls one text line per frame and the green trail sits exactly
     * one line above the white head, so the vertical step is the live line
     * height. The 14px column pitch stays put -- that is horizontal spacing
     * and the body advance has not changed. */
    i32 cell = FONT_H * (i32)GFX_FONT_SCALE;
    u32 cols = sw / 14;
    if (cols > 256) cols = 256;
    static i32 drops[256];
    for (u32 i = 0; i < cols; i++) drops[i] = -((i32)((timer_ms() + i * 17) % sh) / cell);

    mouse_update(mouse);
    keyboard_flush();
    i32 last_lx = mouse->x, last_ly = mouse->y;

    gfx_clear(COL_BLACK);

    while (1) {
        __asm__ volatile("sti; hlt");
        if (keyboard_haschar()) { keyboard_flush(); break; }
        mouse_update(mouse);
        if (mouse->left_clicked || mouse->right_clicked || kabs(mouse->x - last_lx) > 10 || kabs(mouse->y - last_ly) > 10) break;
        
        gfx_rect_blend(0, 0, sw, sh, COL_BLACK, 25); /* Trail effect */

        for (u32 i = 0; i < cols; i++) {
            char str[2]; 
            str[0] = 33 + (timer_ms() / 100 + i * 17) % 94; 
            str[1] = 0;
            
            i32 dy = drops[i] * cell;
            if (dy > 0) {
                gfx_str_bg_none((i32)i * 14, dy, str, COL_WHITE); /* Leading char is white */
                gfx_str_bg_none((i32)i * 14, dy - cell, str, COL_GREEN); /* Trail is green */
            }
            drops[i]++;
            if (dy > sh && ((timer_ms() + i) % 100 > 95)) drops[i] = 0;
        }
        
        gfx_flip();
        timer_wait(35);
    }
}

/* Post-login desktop setup, shared between the initial boot-to-desktop path
   and re-entry after Lock / Log Out / Switch User. Only opens a fresh
   Terminal if one isn't already open (Lock returns to an existing session). */
static void enter_desktop(mouse_t *mouse) {
    rc_care_run_startup();
    {
        const careos_settings_t *cfg = settings_get();
        s_brightness = (int)cfg->brightness_pct;
        notify_set_dnd(cfg->dnd != 0);
        theme_set_glass(cfg->glass_alpha_pct, cfg->reduce_transparency != 0);
    }

    i32 sw = (i32)SCREEN_W;
    i32 sh = (i32)SCREEN_H;
    i32 tw = sw * 62 / 100;
    i32 th = sh * 66 / 100;

    if (!wm_find_app(APP_TERMINAL))
        wm_open(APP_TERMINAL, "Terminal", (sw - tw) / 2, (sh - th) / 2 - 24, tw, th);
    notify_push("CareOS", "Desktop ready.", COL_PRIMARY);
    speaker_startup(); /* Audible startup melody */
    desktop_fade_in(mouse);
}

void gui_run(void) {
    const careos_settings_t *cfg = settings_get();
    bool fast_boot = cfg && cfg->boot_fast;
    mouse_t mouse;

    serial_write("  [gui_run] splash start\n");
    session_boot(fast_boot);
    serial_write("  [gui_run] splash done\n");

    serial_write("  [gui_run] login gate\n");
    kmemset(&mouse, 0, sizeof(mouse));
    bool created = false;
    if (!session_login(&mouse, false, &created)) {
        serial_write("  [gui_run] login flow returned failure\n");
    }
    session_begin(user_current_uid());
    theme_switch(settings_get()->theme == 0);
    session_onboarding(&mouse, created);

    enter_desktop(&mouse);

    serial_write("  [gui_run] entering main loop\n");

    widgets_init();
    perf_init();

    mouse.x = (i32)SCREEN_W / 2;
    mouse.y = (i32)SCREEN_H / 2;

    u32 last_sysmon = 0, last_netmon = 0, last_notify = 0, last_slow = 0;
    bool needs_redraw = true;
    u32 lx = 0, ly = 0;
    bool lb = false;


    while (1) {
            if (g_session_action != SESSION_ACT_NONE) {
                session_action_t act = g_session_action;
                g_session_action = SESSION_ACT_NONE;
                launcher_open = false;
                if (act == SESSION_ACT_LOCK) {
                    /* Real lock screen: re-authenticate the current user only.
                       No picker, no signup, windows/session stay exactly as
                       they are, and there is no desktop re-entry (no startup
                       chime replay). */
                    session_login(&mouse, true, NULL);
                    session_begin(user_current_uid());
                    theme_switch(settings_get()->theme == 0);
                    needs_redraw = true;
                } else {
                    /* LOGOUT / Switch User: drop the session entirely and show
                       the full greeter so a different account can sign in. */
                    wm_close_all();      /* clean desktop for the next user */
                    session_end();
                    bool made = false;
                    session_login(&mouse, false, &made);
                    session_begin(user_current_uid());
                    theme_switch(settings_get()->theme == 0);
                    session_onboarding(&mouse, made);
                    enter_desktop(&mouse);
                }
                g_last_activity_tick = timer_ms();
                needs_redraw = true;
                continue;
            }

            bool activity = false;

            while (keyboard_haschar()) {
                activity = true;
                char c = gui_read_key();
                char kl = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
                
                /* Global Activity Trigger */
                g_last_activity_tick = timer_ms();

                /* Global Hotkeys */
                if (keyboard_alt_held() && c == '\t') { wm_cycle_focus(1); continue; }
                if (keyboard_alt_held() && c == 0x1B) { /* Alt+F4 (handled as ESC scan in some drivers) */
                     window_t *fw = wm_focused();
                     if (fw) wm_close(fw);
                     continue;
                }
                /* Alt+F4 detection if scan is mapped to F4 */
                // if (keyboard_alt_held() && is_f4(c)) ...

                /* Super+L locks the screen. */
                if (keyboard_super_held() && (c == 'l' || c == 'L')) {
                    g_session_action = SESSION_ACT_LOCK;
                    continue;
                }

                /* Multi-desktop Switch: Ctrl + 1-4 */
                if (keyboard_ctrl_held() && c >= '1' && c <= '4') {
                    wm_switch_desktop((u32)(c - '1'));
                    activity = true;
                    notify_push("Workspace", (c == '1' ? "Desktop 1" : (c == '2' ? "Desktop 2" : (c == '3' ? "Desktop 3" : "Desktop 4"))), COL_ACCENT);
                    continue;
                }

                /* Ctrl+<letter> arrives as a control code (Ctrl+M = 0x0D), so
                 * chords compare the normalised letter, not kl -- with kl
                 * none of these shortcuts could ever fire. */
                char ck = key_chord_letter(c);                if (keyboard_alt_held() && keyboard_ctrl_held() && ck == 'p') {
                    perf_hud_set(!perf_hud_enabled());
                    activity = true;
                    continue;
                }

                if (keyboard_alt_held() && keyboard_ctrl_held()) {
                    if (ck == 'h' || ck == 'a') { wm_snap_focused(SNAP_LEFT); continue; }
                    if (ck == 'l' || ck == 'd') { wm_snap_focused(SNAP_RIGHT); continue; }
                    if (ck == 'k' || ck == 'w') { wm_snap_focused(SNAP_TOP); continue; }
                    if (ck == 'j' || ck == 's') { wm_snap_focused(SNAP_BOTTOM); continue; }
                    if (ck == 'm' || ck == 'f') { wm_snap_focused(SNAP_FULL); continue; }
                }
                if (launcher_open) launcher_handle_key(c);
                else { window_t *fw = wm_focused(); if (fw) wm_handle_key(c, fw); }
            }

            /* Tap Super to summon / dismiss the Spotlight launcher, like GNOME
             * or Windows. Polled every frame (it is a modifier, not a char). */
            if (keyboard_super_tapped()) {
                launcher_open = !launcher_open;
                if (launcher_open) { s_notif_center_open = false; s_control_center_open = false; }
                g_last_activity_tick = timer_ms();
                activity = true;
            }

            mouse_update(&mouse);
            if (mouse.x != lx || mouse.y != ly || mouse.left != lb || mouse.left_clicked || mouse.right_clicked || mouse.scroll_delta != 0) {
                activity = true; lx = mouse.x; ly = mouse.y; lb = mouse.left;
                g_last_activity_tick = timer_ms();
            }
            /* A left click ripples out from the cursor, everywhere. */
            if (mouse.left_clicked) { gfx_ripple(mouse.x, mouse.y, COL_ACCENT); activity = true; }
            /* Keep redrawing while any ripple is still blooming. */
            if (gfx_ripples_active()) activity = true;

            /* Explicit lock request (Spotlight "Lock Screen", a power menu, ...). */
            if (g_lock_request) {
                g_lock_request = false;
                session_login(&mouse, true, NULL);
                session_begin(user_current_uid());
                theme_switch(settings_get()->theme == 0);
                g_last_activity_tick = timer_ms();
                needs_redraw = true;
            }

            /* Idle / screensaver timeouts are measured in milliseconds. */
            u32 now = timer_ms();
            if (now - g_last_activity_tick > GUI_AUTOLOCK_MS) {
                /* Auto-lock the screen if idle for 10 mins. Same lock screen
                   as an explicit Lock: current user's password only, windows
                   stay open, no picker and no startup chime replay. */
                serial_write("  [gui] auto-locking due to idle\n");
                session_login(&mouse, true, NULL);
                session_begin(user_current_uid());
                theme_switch(settings_get()->theme == 0);
                g_last_activity_tick = timer_ms();
                needs_redraw = true;
            } else if (now - g_last_activity_tick > GUI_SCREENSAVER_MS) {
                /* Show Matrix screensaver after five minutes. */
                run_matrix_screensaver(&mouse);
                g_last_activity_tick = timer_ms();
                needs_redraw = true;
            }
            /* Step window open/close/minimise animations every frame so they
             * read as smooth motion instead of the 5 Hz steps the old 200 ms
             * gate produced. */
            if (wm_animate_all()) activity = true;
            if (launcher_animating()) activity = true;
            if (taskbar_animate(&mouse)) activity = true;
            if (app_doom_new_frame()) activity = true;

            /* Theme mode "auto": light 07:00-19:00, dark otherwise (checked
             * every 30 s; a minute boundary is the finest it can change on). */
            {
                static u32 last_auto = 0;
                const careos_settings_t *ac = settings_get();
                if (ac && ac->theme_mode == 2 && (u32)(timer_ms() - last_auto) > 30000u) {
                    last_auto = timer_ms();
                    rtc_time_t lt;
                    clock_read_local(&lt);
                    bool dark = settings_auto_theme_dark(lt.hour);
                    if (dark != g_theme->is_dark) { theme_switch(dark); needs_redraw = true; }
                }
            }

            /* Keep relative times and toast expiry moving while either shows. */
            if (notify_active()) activity = true;

            if (now - last_sysmon >= 200) {
                window_t *sm = wm_find_app(APP_SYSMON);
                window_t *nm = wm_find_app(APP_NETMON);
                if (sm) app_sysmon_tick(sm);
                if (nm) app_netmon_tick(nm);
                last_sysmon = now;
                if (sm || nm) activity = true;   /* live graphs, only while a monitor is open */
            }
            /* The browser fetch worker owns the NIC while it runs (it polls it
             * itself); poll here only when the network is free. */
            if (now - last_netmon >= 100 && net_trylock()) { net_poll(); net_unlock(); last_netmon = now; }
            for (int bi = 0; bi < MAX_WINDOWS; bi++) {
                window_t *bw = wm_get_window(bi);
                if (bw && bw->active && bw->app == APP_BROWSER) {
                    app_browser_tick(bw);
                    if (bw->browser_loading) activity = true; /* spinner + result */
                }
            }
            if (now - last_notify >= 100)  { notify_tick(); last_notify = now; }

            /* Low-rate heartbeat: keeps on-screen clocks/meters ticking without
             * recompositing the whole glass desktop at 100 Hz while idle. Input
             * and animations (above) still drive full-rate redraws when needed. */
            if (now - last_slow >= 250) { last_slow = now; activity = true; }
            /* ~25 Hz while a Clock window is showing, so its second hand sweeps. */
            {
                static u32 last_clock = 0;
                window_t *ck = wm_find_app(APP_CLOCK);
                if (ck && !ck->minimized && now - last_clock >= 40) { last_clock = now; activity = true; }
            }

            #ifdef CAREOS_QA_LAYOUT
            {   /* QA build only (make QA_LAYOUT=1): every app at its minimum
                 * size, four at a time, 6 s per group, with a serial marker so
                 * a harness can screenshot each group. */
                static const app_id_t qa_apps[] = { APP_TERMINAL, APP_FILES, APP_SETTINGS, APP_BROWSER,
                    APP_EDITOR, APP_NOTES, APP_CALC, APP_PAINT, APP_USERS, APP_PKGMGR, APP_CLOCK,
                    APP_SYSMON, APP_NETMON, APP_HELP, APP_ABOUT, APP_MAZE };
                static const u32 qa_n = sizeof(qa_apps) / sizeof(qa_apps[0]);
                static u32 grp = 0, t0 = 0;
                static window_t *open_w[4];
                if (!t0) t0 = timer_ms();
                if ((u32)(timer_ms() - t0) > 6000u && grp * 4 < qa_n) {
                    for (int k = 0; k < 4; k++) if (open_w[k] && open_w[k]->active) wm_close(open_w[k]);
                    i32 cw = ((i32)SCREEN_W - (i32)SIDEBAR_W - 30) / 2;
                    i32 ch = ((i32)SCREEN_H - (i32)TOPBAR_H - (i32)TASKBAR_H - 30) / 2;
                    for (int k = 0; k < 4; k++) {
                        u32 ai = grp * 4 + (u32)k; open_w[k] = NULL;
                        if (ai >= qa_n) break;
                        i32 mw, mh; app_min_size(qa_apps[ai], &mw, &mh);
                        i32 x = (i32)SIDEBAR_W + 10 + (k % 2) * (cw + 10);
                        i32 y = (i32)TOPBAR_H + 10 + (k / 2) * (ch + 10);
                        open_w[k] = wm_open(qa_apps[ai], "QA", x, y, mw, mh);
                    }
                    char b[40]; ksprintf(b, "[qa-layout] group %d\n", (int)grp); serial_write(b);
                    grp++; t0 = timer_ms();
                }
                activity = true;
            }
            #endif
            #ifdef CAREOS_PERF_BENCH_AT_BOOT
            {
                static bool started = false;
                static u32 desktop_ms = 0;
                if (!desktop_ms) desktop_ms = timer_ms();
                if (!started && (u32)(timer_ms() - desktop_ms) > 3000u) {
                    started = true;
                    perf_bench_start(NULL);
                }
            }
            #endif
            if (perf_bench_active()) { perf_bench_step(); activity = true; }

            /* Route mouse input before drawing its result in this iteration. */
            g_cursor_shape = CURSOR_ARROW;
            if (!notify_handle_mouse(&mouse)) {
                if (handle_control_center_mouse(&mouse)) {
                    needs_redraw = true;
                } else if (handle_notification_center_mouse(&mouse)) {
                    needs_redraw = true;
                } else if (handle_top_bar_mouse(&mouse)) {
                    needs_redraw = true;
                } else if (launcher_open) {
                    launcher_handle_mouse(&mouse);
                } else if (widgets_dragging()) {
                    widgets_handle_mouse(&mouse);
                    needs_redraw = true;
                } else {
                    /* One owner per event, topmost first: a live window drag,
                     * the dock (drawn above windows), a window under the
                     * pointer, then the desktop layer (widgets, shortcuts). */
                    taskbar_handle_mouse(&mouse);       /* tracks the pointer; acts only inside the dock */
                    if (wm_busy()) {
                        wm_handle_mouse(&mouse);
                    } else if (taskbar_contains(mouse.x, mouse.y)) {
                        desktop_clear_hover();
                    } else if (wm_pointer_owner(mouse.x, mouse.y)) {
                        desktop_clear_hover();
                        wm_handle_mouse(&mouse);
                    } else if (widgets_handle_mouse(&mouse)) {
                        needs_redraw = true;
                    } else {
                        desktop_handle_mouse(&mouse);
                        wm_handle_mouse(&mouse);
                    }
                }
            }
            if (activity) needs_redraw = true;

            static u32 s_last_frame_start = 0;
            u32 frame_now = timer_ms();
            if (needs_redraw && (u32)(frame_now - s_last_frame_start) >= GUI_FRAME_MIN_MS) {
                /* Count rendering toward the interval, avoiding an extra delay
                 * after frames that already take longer than the budget. */
                s_last_frame_start = frame_now;
                perf_frame_begin();
                ui_frame_begin();
                /* The desktop backdrop (wallpaper + frosted sidebar + widget
                 * panels) is expensive to composite -- a per-frame blur and soft
                 * shadow. It only changes when the chrome does: the clock/meters
                 * tick (~2 Hz here), the pointer hovers the sidebar or widgets,
                 * or the theme changes. Cache the composited backdrop and blit it
                 * (one memcpy) on every other frame; windows/taskbar/cursor still
                 * draw on top at full rate. This is what makes dragging and
                 * typing smooth instead of paying ~700 Mcyc of blur each frame. */
                static u32 last_backdrop_ms = 0;
                /* Rebuild only when the layer would look different: at once for
                 * hover, hero visibility, theme or a widget drag; at most once a
                 * second for the clock and meters. A timer-driven rebuild every
                 * 500 ms used to put a slow frame into every half second. */
                static u32 last_instant = 0xFFFFFFFFu, last_slow = 0xFFFFFFFFu;
                u32 instant = desktop_layer_key() ^ (g_theme->is_dark ? 0x80000000u : 0u);
                u32 slow = widgets_layer_key();
                bool rebuild = widgets_dragging() || instant != last_instant ||
                               (slow != last_slow && now - last_backdrop_ms >= 1000);
                last_instant = instant;
                if (rebuild) last_slow = slow;

                if (rebuild || !gfx_desktop_cache_blit()) {
                    if (!gfx_wallpaper_cache_blit()) {
                        draw_elite_wallpaper();
                        gfx_wallpaper_cache_capture();
                    }
                    desktop_draw();
                    widgets_draw(&mouse);
                    gfx_desktop_cache_capture();
                    last_backdrop_ms = now;
                }

                wm_draw_all();
                taskbar_draw();
                draw_top_bar(&mouse);
                launcher_draw(&mouse);              /* also plays the close animation */
                if (launcher_visible()) taskbar_draw();   /* the dock stays crisp above it */

                draw_notification_center(&mouse);
                draw_control_center(&mouse);
                if (s_screenshot_pending) {
                    s_screenshot_pending = false;
                    take_screenshot();
                }

                if (s_brightness < 100) {
                    /* 10 % brightness = a 60 % black overlay (spec §12). */
                    u8 dim = (u8)((100 - s_brightness) * 153 / 90);
                    gfx_rect_blend(0, 0, (i32)SCREEN_W, (i32)SCREEN_H, COL_BLACK, dim);
                }

                gfx_ripples_draw();
                perf_hud_draw();
                mouse_draw_cursor(mouse.x, mouse.y);
                gfx_flip();
                perf_frame_end();
                ui_frame_end((u32)(timer_ms() - frame_now));
                needs_redraw = false;
            }

            if (sched_other_ready()) {
                /* Background work (page loads, DOOM) gets the rest of this
                 * frame; its short time slice brings us back quickly. */
                task_yield();
            } else {
                perf_idle_begin();
                __asm__ volatile("sti; hlt");
                perf_idle_end();
            }
            perf_tick();
    }
}
