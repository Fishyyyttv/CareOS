/* =============================================================================
 * CareOS gui/gui.c -- main GUI entry point with splash, login, and desktop
 * ============================================================================= */
#include "kernel.h"
#include "gui.h"
#include "ui.h"
#include "image.h"
#include "resource_cache.h"
#include "icon.h"
#include "perf.h"
#include "keychord.h"

static void draw_glass_panel(rect_t r, i32 radius);

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

static const char *BOOT_STAGES[] = {
    "Initializing graphics pipeline...",
    "Starting input and device services...",
    "Mounting virtual filesystem...",
    "Starting process scheduler...",
    "Starting desktop services...",
    "Preparing secure login session...",
};
#define BOOT_STAGE_COUNT 6

static void draw_boot_splash(int done, u32 tick) {
    i32 sw = (i32)SCREEN_W;
    i32 sh = (i32)SCREEN_H;
    i32 area_h = sh;
    i32 ring = ui_clampi(sw / 22, 38, 72);
    i32 bar_w = ui_clampi(sw * 34 / 100, 360, 560);
    i32 bar_h = ui_clampi(sh / 120, 8, 14);
    i32 by = sh / 2 + ring + 72;

    gfx_gradient_rect(0, 0, sw, area_h, rgb(0x04,0x09,0x14), rgb(0x12,0x1a,0x30));

    gfx_circle_fill(sw / 2 - sw / 5, sh / 3, sw / 7, rgb(0x11,0x22,0x40));
    gfx_circle_fill(sw / 2 + sw / 4, sh / 2, sw / 8, rgb(0x0c,0x1c,0x39));
    for (i32 gy = 0; gy < sh; gy += 34)
        for (i32 gx = 0; gx < sw; gx += 34)
            if ((((gx / 34) + (gy / 34)) + (i32)(tick % 2)) % 2 == 0)
                gfx_setpixel(gx, gy, rgb(0x1a,0x2b,0x4a));

    gfx_circle_fill(sw / 2, sh / 2 - 70, ring + 12, rgb(0x1d,0x2f,0x5b));
    gfx_circle_fill(sw / 2, sh / 2 - 70, ring + 2, rgb(0x0d,0x15,0x2b));
    gfx_circle(sw / 2, sh / 2 - 70, ring + 18, COL_PRIMARY);
    gfx_circle(sw / 2, sh / 2 - 70, ring + 28, COL_ACCENT);

    gfx_circle_fill(sw / 2, sh / 2 - 70, ring - 10, COL_PRIMARY);
    gfx_rect(sw / 2 - ring / 2, sh / 2 - 70 - ring + 10, ring, ring * 2 - 20, rgb(0x0d,0x15,0x2b));
    gfx_str_centered_ex(0, sh / 2 - 83, sw, "OS", COL_ACCENT, COL_TRANSPARENT, FONT_H2);

    gfx_str_centered_ex(0, sh / 2 + 12, sw, "CareOS", COL_WHITE, COL_TRANSPARENT, FONT_H1);
    gfx_str_centered(0, sh / 2 + 12 + gfx_line_h_ex(FONT_H1) + 2, sw,
        "Performance focused desktop operating environment",
        COL_DIM, COL_TRANSPARENT);

    gfx_rect_rounded(sw / 2 - bar_w / 2, by, bar_w, bar_h, bar_h / 2, rgb(0x0b,0x10,0x20));
    gfx_rect_rounded_outline(sw / 2 - bar_w / 2, by, bar_w, bar_h, bar_h / 2, COL_BORDER);

    if (done > 0) {
        i32 fill = (bar_w - 2) * done / BOOT_STAGE_COUNT;
        if (fill < 0) fill = 0;
        gfx_rect_rounded(sw / 2 - bar_w / 2 + 1, by + 1, fill, bar_h - 2, (bar_h - 2) / 2,
            done >= BOOT_STAGE_COUNT ? COL_GREEN : COL_PRIMARY);
    }

    for (int i = 0; i < BOOT_STAGE_COUNT; i++) {
        i32 dx = sw / 2 - bar_w / 2 + (bar_w * i / (BOOT_STAGE_COUNT - 1));
        u32 dc = (i < done) ? COL_GREEN : (i == done ? COL_ACCENT : rgb(0x2a,0x35,0x54));
        gfx_circle_fill(dx, by + bar_h / 2, 4, dc);
    }

    {
        i32 lh      = FONT_H * (i32)GFX_FONT_SCALE;
        i32 stage_y = by + bar_h + 11;
        gfx_str_centered(0, stage_y, sw,
            (done >= BOOT_STAGE_COUNT) ? "Boot sequence complete" : BOOT_STAGES[done],
            COL_TEXT, COL_TRANSPARENT);

        const char spin[] = "|/-\\";
        char spin_buf[24] = "Loader: [ ]";
        spin_buf[9] = spin[tick % 4];
        gfx_str_centered(0, stage_y + lh + 2, sw, spin_buf, COL_MUTED, COL_TRANSPARENT);
    }

    gfx_str_centered(0, sh - 22, sw,
        "CareOS v9.0  |  April 2026  |  x86_64",
        COL_MUTED, COL_TRANSPARENT);
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

/* Desktop backdrop for the main loop: blit the cached wallpaper when it is
 * valid (one memcpy), otherwise render it the slow way once and cache the
 * result. draw_elite_wallpaper() is still used directly on the login/lock
 * screens, which composite changing overlays on top and are not hot paths. */
/* =============================================================================
 * CareOS Control Center & Notification Center State & Core Implementation
 * ============================================================================= */

/* Control Center State */
static bool s_control_center_open = false;
static int  s_brightness = 100;      /* 10% to 100%, software dimming */

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

/* Local wall-clock time. The RTC runs on UTC; CareOS has no time-zone setting
 * yet and has always shown UTC-7 here, so keep that offset, but roll the date
 * back properly (the old code could show day 0). */
static void menubar_local_time(rtc_time_t *t) {
    rtc_read(t);
    int hour = (int)t->hour - 7;
    if (hour < 0) {
        hour += 24;
        if (t->day > 1) t->day--;
        else {
            static const u8 dim[12] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
            t->month = (u8)(t->month > 1 ? t->month - 1 : 12);
            if (t->month == 12) t->year--;
            u8 d = dim[(t->month - 1) % 12];
            if (t->month == 2 && (t->year % 4 == 0 && (t->year % 100 != 0 || t->year % 400 == 0))) d = 29;
            t->day = d;
        }
    }
    t->hour = (u8)hour;
}

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
enum { CM_ABOUT, CM_SETTINGS, CM_SEP, CM_LOCK, CM_LOGOUT, CM_RESTART, CM_SHUTDOWN, CM_COUNT };
static const char *const CARE_MENU[CM_COUNT] = {
    "About this computer", "Settings...", NULL, "Lock", "Log out", "Restart", "Shut down"
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
        case CM_RESTART:  power_reboot(); break;
        case CM_SHUTDOWN: power_shutdown(); break;
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
        s_brightness = ui_clampi(((m->x - L.bright.x) * 100) / L.bright.w, 10, 100);
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
        s_care_menu_open = true;          /* Restart / Shut down live there */
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

typedef enum {
    /* Distro-style greeter: pick an account before the password step. This is
     * the first mode shown whenever at least one account exists; its numeric
     * value is kept out of the 0..2 run so nothing that switches on the old
     * values by arithmetic (there is none today, but better safe) shifts. */
    LOGIN_MODE_PICK = 3,
    LOGIN_MODE_SIGNIN = 0,
    LOGIN_MODE_SIGNUP = 1,
    /* Credentials were correct, but the account still holds a shipped
     * bootstrap password. The desktop stays unreachable until it is replaced. */
    LOGIN_MODE_MUST_CHANGE = 2,
} login_mode_t;

typedef struct {
    char username[32];
    char password[64];
    u32  user_len;
    u32  pass_len;
    u32  field; /* 0=username, 1=password (0=new, 1=confirm when MUST_CHANGE) */
    char status[96];
    u32  status_color;
    u32  failed_attempts;
    u32  lock_until_tick;
    login_mode_t mode;
    u32  picked_uid; /* uid of the account row clicked on the greeter, if any.
                       * Not a sentinel -- 0 is root's real uid, so this field
                       * is informational only and nothing branches on it. */
    bool locked; /* Lock-screen mode: re-authenticate the current user only.
                  * Username is pinned, the greeter/signup/back-to-picker
                  * paths are all disabled -- password is the only input. */

    /* MUST_CHANGE mode only. username still holds the authenticated account. */
    char newpass[64];
    u32  newpass_len;
    char confirm[64];
    u32  confirm_len;
    char verified_pass[64];   /* the old password, to authorise the change */
} login_state_t;

typedef struct {
    rect_t panel;
    rect_t avatar;
    rect_t user_field;
    rect_t pass_field;
    rect_t status_bar;
    button_t primary_btn;
    button_t secondary_btn;
    /* Text baselines derived from the live line height, so draw_login_screen
     * and the hit-testing layout can never drift apart. */
    i32 wordmark_y;
    i32 title_y;
    i32 subtitle_y;
    i32 label_dy;     /* how far a field's label sits above the field top */
    i32 status_ty;
    i32 footer_y;
} login_layout_t;

static void login_set_status(login_state_t *s, const char *msg, u32 color) {
    kstrncpy(s->status, msg, sizeof(s->status) - 1);
    s->status[sizeof(s->status) - 1] = '\0';
    s->status_color = color;
}

static void login_mask_n(char *out, u32 max, u32 n) {
    if (n >= max) n = max - 1;
    for (u32 i = 0; i < n; i++) out[i] = '*';
    out[n] = '\0';
}

static void login_mask_password(const login_state_t *s, char *out, u32 max) {
    login_mask_n(out, max, s->pass_len);
}

static login_layout_t login_make_layout(const login_state_t *s) {
    login_layout_t l;
    i32 sw = (i32)SCREEN_W;
    i32 sh = (i32)SCREEN_H;
    i32 pw = ui_clampi(sw * 32 / 100, 430, 560);
    i32 ph = ui_clampi(sh * 52 / 100, 420, 520);
    i32 px = (sw - pw) / 2;
    i32 py = (sh - ph) / 2;
    i32 btn_gap = 12;
    i32 btn_w = (pw - 84 - btn_gap) / 2;

    if (sw < 520) {
        pw = sw - 32;
        px = 16;
        btn_w = (pw - 84 - btn_gap) / 2;
    }
    if (sh < 560) {
        ph = sh - 56;
        py = 38;
    }

    /* Every text row in the card is stacked from the live line heights. With
     * the old 13px body face the numbers came out at the hand-tuned values
     * this layout used to hardcode (188/252 fields, 136/156 headings); with a
     * 17px face they spread instead of the subtitle descenders landing in the
     * "Username" label and the footer sitting on the card's bottom edge. */
    i32 lh       = FONT_H * (i32)GFX_FONT_SCALE;
    i32 status_h = lh + 9;

    l.panel = rect_make(px, py, pw, ph);
    l.avatar = rect_make(px + pw / 2 - 30, py + 28, 60, 60);

    l.wordmark_y = py + 98;
    l.title_y    = l.wordmark_y + gfx_line_h_ex(FONT_H2) + 12;
    l.subtitle_y = l.title_y + lh + 3;
    l.label_dy   = lh + 5;

    l.user_field = rect_make(px + 42, l.subtitle_y + lh + 4 + l.label_dy, pw - 84, 38);
    l.pass_field = rect_make(px + 42, l.user_field.y + 64, pw - 84, 38);

    l.footer_y   = py + ph - lh - 8;
    l.status_bar = rect_make(px + 42, l.footer_y - status_h - 8, pw - 84, status_h);
    l.status_ty  = l.status_bar.y + (status_h - lh) / 2;

    l.primary_btn = (button_t){
        .rect = rect_make(px + 42, l.status_bar.y - 48, btn_w, 36),
        .hover = false,
        .pressed = false,
        .active = true,
        .bg = COL_PRIMARY,
        .fg = COL_WHITE,
    };
    l.secondary_btn = (button_t){
        .rect = rect_make(px + 42 + btn_w + btn_gap, l.status_bar.y - 48, btn_w, 36),
        .hover = false,
        .pressed = false,
        .active = false,
        .bg = COL_SURFACE2,
        .fg = COL_TEXT,
    };
    if (s->mode == LOGIN_MODE_MUST_CHANGE) {
        kstrcpy(l.primary_btn.label, "Set Password");
        kstrcpy(l.secondary_btn.label, "Cancel");
    } else {
        kstrcpy(l.primary_btn.label, s->mode == LOGIN_MODE_SIGNIN ? "Sign In" : "Create Account");
        kstrcpy(l.secondary_btn.label, s->mode == LOGIN_MODE_SIGNIN ? "Create Account" : "Back To Sign In");
    }
    return l;
}

static void draw_glass_panel(rect_t r, i32 radius) {
    /* Real frosted glass (blurred backdrop + tint + sheen + hairline), matching
     * the dock/sidebar/widgets. Callers add their own soft shadow. */
    gfx_glass_panel(r.x, r.y, r.w, r.h, radius);
}

static void draw_login_screen(const login_state_t *s, mouse_t *mouse) {
    i32 sw = (i32)SCREEN_W;
    i32 sh = (i32)SCREEN_H;
    login_layout_t l = login_make_layout(s);
    char pass_mask[64];
    char title[40];
    char subtitle[80];

    draw_elite_wallpaper();
    gfx_rect_blend(0, 0, sw, sh, COL_BLACK, 96);

    gfx_shadow_soft(l.panel.x, l.panel.y, l.panel.w, l.panel.h, CDL_R_CARD);
    draw_glass_panel(l.panel, CDL_R_CARD);

    /* Branding Header */
    gfx_circle_fill(l.avatar.x + l.avatar.w / 2, l.avatar.y + l.avatar.h / 2, l.avatar.w / 2, COL_PRIMARY);
    gfx_circle_fill(l.avatar.x + l.avatar.w / 2 + 4, l.avatar.y + l.avatar.h / 2,
                    l.avatar.w / 2 - 12, COL_SURFACE);
    gfx_str_centered_ex(l.avatar.x, l.avatar.y + 15, l.avatar.w, "C", COL_ACCENT, COL_TRANSPARENT, FONT_H2);
    gfx_str_centered_ex(l.panel.x, l.wordmark_y, l.panel.w, "CareOS", COL_TEXT, COL_TRANSPARENT, FONT_H2);

    if (s->mode == LOGIN_MODE_MUST_CHANGE) {
        kstrcpy(title, "Change Your Password");
        kstrcpy(subtitle, "This account uses a default password");
    } else if (s->locked) {
        kstrcpy(title, "Locked");
        kstrcpy(subtitle, "Enter your password to continue");
    } else {
        kstrcpy(title, s->mode == LOGIN_MODE_SIGNIN ? "Welcome Back" : "Create Account");
        kstrcpy(subtitle, s->mode == LOGIN_MODE_SIGNIN
            ? "Please sign in to access your desktop"
            : "Set up a new secure local profile");
    }

    gfx_str_centered(l.panel.x, l.title_y, l.panel.w, title, COL_TEXT, COL_TRANSPARENT);
    gfx_str_centered(l.panel.x, l.subtitle_y, l.panel.w, subtitle, COL_DIM, COL_TRANSPARENT);

    /* Inputs */
    if (s->mode == LOGIN_MODE_MUST_CHANGE) {
        gfx_str(l.user_field.x, l.user_field.y - l.label_dy, "New Password", COL_MUTED, COL_TRANSPARENT);
        gfx_str(l.pass_field.x, l.pass_field.y - l.label_dy, "Confirm Password", COL_MUTED, COL_TRANSPARENT);
    } else {
        gfx_str(l.user_field.x, l.user_field.y - l.label_dy, "Username", COL_MUTED, COL_TRANSPARENT);
        gfx_str(l.pass_field.x, l.pass_field.y - l.label_dy, "Password", COL_MUTED, COL_TRANSPARENT);
    }

    {
        textinput_t u_box, p_box;
        kmemset(&u_box, 0, sizeof(u_box)); kmemset(&p_box, 0, sizeof(p_box));
        
        u_box.rect = l.user_field;
        u_box.focused = (s->field == 0);
        u_box.hover = rect_contains(l.user_field, mouse->x, mouse->y);
        p_box.rect = l.pass_field;
        p_box.focused = (s->field == 1);
        p_box.hover = rect_contains(l.pass_field, mouse->x, mouse->y);

        if (s->mode == LOGIN_MODE_MUST_CHANGE) {
            /* Both fields are secrets here, so both are masked. */
            char new_mask[64];
            login_mask_n(new_mask, sizeof(new_mask), s->newpass_len);
            kstrcpy(u_box.buf, new_mask);
            u_box.len = (u32)kstrlen(new_mask); u_box.cursor = u_box.len;
            kstrcpy(u_box.placeholder, "New password");

            login_mask_n(pass_mask, sizeof(pass_mask), s->confirm_len);
            kstrcpy(p_box.buf, pass_mask);
            p_box.len = (u32)kstrlen(pass_mask); p_box.cursor = p_box.len;
            kstrcpy(p_box.placeholder, "Repeat password");
        } else {
            kstrcpy(u_box.buf, s->username);
            u_box.len = s->user_len; u_box.cursor = s->user_len;
            kstrcpy(u_box.placeholder, "User ID");

            login_mask_password(s, pass_mask, sizeof(pass_mask));
            kstrcpy(p_box.buf, pass_mask);
            p_box.len = (u32)kstrlen(pass_mask); p_box.cursor = p_box.len;
            kstrcpy(p_box.placeholder, "Password");
        }

        textinput_draw(&u_box);
        textinput_draw(&p_box);
    }

    button_update(&l.primary_btn, mouse);
    button_draw(&l.primary_btn);
    if (!s->locked) {
        /* Locked mode has no "Create Account" escape hatch -- the current
         * user's password is the only way past this screen. */
        button_update(&l.secondary_btn, mouse);
        button_draw(&l.secondary_btn);
    }

    /* Footer / Status — plain centred text, not a button-like filled bar. */
    gfx_str_centered(l.status_bar.x, l.status_ty, l.status_bar.w, s->status, s->status_color, COL_TRANSPARENT);

    gfx_str_centered(l.panel.x, l.footer_y, l.panel.w,
                     "CareOS v9 secure desktop", COL_MUTED, COL_TRANSPARENT);
}

/* -- Greeter (distro-style account picker) --------------------------------
 * Purely an added front step: it renders inside the same glass panel as
 * draw_login_screen, lists one clickable row per active account, and on a
 * hit hands off to the unmodified LOGIN_MODE_SIGNIN flow with the username
 * pre-filled. */
#define GREETER_MAX_ROWS 6
#define GREETER_ROW_H    60
#define GREETER_ROW_GAP  10

typedef struct {
    u32    uid;
    char   name[32];
    bool   is_root;
    rect_t rect;
} greeter_row_t;

typedef struct {
    rect_t panel;
    rect_t avatar;
    i32    wordmark_y;
    i32    title_y;
    i32    subtitle_y;
    i32    footer_y;
    greeter_row_t rows[GREETER_MAX_ROWS];
    u32    row_count;
    /* Escape hatch: the row cap (GREETER_MAX_ROWS) is only a display limit --
     * MAX_USERS (16) can exceed it, so this manual-entry row is always
     * present and reachable, keeping every account clickable-or-typeable. */
    rect_t other_row;
} greeter_layout_t;

/* Recomputed fresh every frame from the same panel geometry draw_login_screen
 * uses (via login_make_layout), so drawing and hit-testing can never drift
 * apart -- mirrors how login_make_layout itself is recomputed each frame. */
static greeter_layout_t greeter_make_layout(const login_state_t *s) {
    greeter_layout_t g;
    kmemset(&g, 0, sizeof(g));

    login_layout_t l = login_make_layout(s);
    g.panel      = l.panel;
    g.avatar     = l.avatar;
    g.wordmark_y = l.wordmark_y;
    g.title_y    = l.title_y;
    g.subtitle_y = l.subtitle_y;
    g.footer_y   = l.footer_y;

    i32 lh      = FONT_H * (i32)GFX_FONT_SCALE;
    i32 row_x   = g.panel.x + 42;
    i32 row_w   = g.panel.w - 84;
    i32 row_top = g.subtitle_y + lh + 24;

    u32 n = user_enum_count();
    if (n > GREETER_MAX_ROWS) n = GREETER_MAX_ROWS;

    for (u32 i = 0; i < n; i++) {
        u32  uid;
        char name[32];
        bool is_root;
        if (!user_enum_at(i, &uid, name, sizeof(name), &is_root)) break;

        greeter_row_t *row = &g.rows[g.row_count++];
        row->uid = uid;
        kstrncpy(row->name, name, sizeof(row->name) - 1);
        row->name[sizeof(row->name) - 1] = '\0';
        row->is_root = is_root;
        row->rect = rect_make(row_x,
                               row_top + (i32)i * (GREETER_ROW_H + GREETER_ROW_GAP),
                               row_w, GREETER_ROW_H);
    }

    {
        i32 other_y = row_top + (i32)g.row_count * (GREETER_ROW_H + GREETER_ROW_GAP);
        g.other_row = rect_make(row_x, other_y, row_w, 34);
    }
    return g;
}

static void draw_greeter(const login_state_t *s, mouse_t *mouse) {
    i32 sw = (i32)SCREEN_W;
    i32 sh = (i32)SCREEN_H;
    greeter_layout_t g = greeter_make_layout(s);

    draw_elite_wallpaper();
    gfx_rect_blend(0, 0, sw, sh, COL_BLACK, 96);

    draw_glass_panel(g.panel, 18);

    /* Branding header, identical to draw_login_screen's. */
    gfx_circle_fill(g.avatar.x + g.avatar.w / 2, g.avatar.y + g.avatar.h / 2, g.avatar.w / 2, COL_PRIMARY);
    gfx_circle_fill(g.avatar.x + g.avatar.w / 2 + 4, g.avatar.y + g.avatar.h / 2,
                    g.avatar.w / 2 - 12, COL_SURFACE);
    gfx_str_centered_ex(g.avatar.x, g.avatar.y + 15, g.avatar.w, "C", COL_ACCENT, COL_TRANSPARENT, FONT_H2);
    gfx_str_centered_ex(g.panel.x, g.wordmark_y, g.panel.w, "CareOS", COL_TEXT, COL_TRANSPARENT, FONT_H2);

    gfx_str_centered(g.panel.x, g.title_y, g.panel.w, "Who's signing in?", COL_TEXT, COL_TRANSPARENT);
    gfx_str_centered(g.panel.x, g.subtitle_y, g.panel.w,
                     "Choose an account to continue", COL_DIM, COL_TRANSPARENT);

    i32 lh = FONT_H * (i32)GFX_FONT_SCALE;

    for (u32 i = 0; i < g.row_count; i++) {
        const greeter_row_t *row = &g.rows[i];
        bool hover = rect_contains(row->rect, mouse->x, mouse->y);

        gfx_rect_rounded(row->rect.x, row->rect.y, row->rect.w, row->rect.h, 12,
                         hover ? COL_SURFACE2 : COL_SURFACE);
        if (hover)
            gfx_rect_rounded_outline(row->rect.x, row->rect.y, row->rect.w, row->rect.h, 12, COL_BORDER);

        /* Distinct per-account color, derived deterministically from the uid. */
        u32 col = (0x4a6fffu ^ (row->uid * 0x9E3779B1u)) & 0xFFFFFFu;
        i32 cy = row->rect.y + row->rect.h / 2;
        i32 cx = row->rect.x + 30;
        gfx_circle_fill(cx, cy, 22, col);

        char initial[2];
        initial[0] = row->name[0];
        if (initial[0] >= 'a' && initial[0] <= 'z') initial[0] = (char)(initial[0] - 32);
        initial[1] = '\0';
        gfx_str_centered_ex(cx - 22, cy - 8, 44, initial, COL_WHITE, COL_TRANSPARENT, FONT_H2);

        i32 name_x = cx + 40;
        i32 name_y = cy - lh / 2;
        gfx_str(name_x, name_y, row->name, COL_TEXT, COL_TRANSPARENT);
        if (row->is_root)
            gfx_str(name_x, name_y + lh, "Administrator", COL_MUTED, COL_TRANSPARENT);
    }

    if (g.row_count == 0)
        gfx_str_centered(g.panel.x, g.subtitle_y + lh + 24, g.panel.w,
                         "No accounts found", COL_MUTED, COL_TRANSPARENT);

    /* Manual-entry escape hatch: always present so any account -- not just
     * the first GREETER_MAX_ROWS -- is reachable. */
    {
        bool hover = rect_contains(g.other_row, mouse->x, mouse->y);
        i32 ty = g.other_row.y + (g.other_row.h - lh) / 2;
        gfx_str_centered(g.other_row.x, ty, g.other_row.w,
                         "Other user... (type a username)",
                         hover ? COL_TEXT : COL_DIM, COL_TRANSPARENT);
    }

    gfx_str_centered(g.panel.x, g.footer_y, g.panel.w,
                     "CareOS v9 secure desktop", COL_MUTED, COL_TRANSPARENT);
}

static bool login_try(login_state_t *s) {
    u32 now = timer_ms();
    if (s->lock_until_tick > now) {
        char wait_s[12];
        char msg[96] = "Too many attempts. Wait ";
        u32 left = (s->lock_until_tick - now + PIT_HZ - 1) / PIT_HZ;
        kutoa(left, wait_s, 10);
        kstrcat(msg, wait_s);
        kstrcat(msg, "s");
        login_set_status(s, msg, COL_YELLOW);
        return false;
    }

    if (s->user_len == 0 || s->pass_len == 0) {
        login_set_status(s, "Please enter username and password", COL_YELLOW);
        return false;
    }

    if (user_login(s->username, s->password) == 0) {
        s->failed_attempts = 0;

        /* Correct credentials, but a shipped bootstrap password: divert to a
         * mandatory change instead of reaching the desktop. */
        if (user_must_change_password()) {
            kstrncpy(s->verified_pass, s->password, sizeof(s->verified_pass) - 1);
            s->verified_pass[sizeof(s->verified_pass) - 1] = '\0';
            s->password[0] = '\0';
            s->pass_len = 0;
            s->newpass[0] = '\0'; s->newpass_len = 0;
            s->confirm[0] = '\0'; s->confirm_len = 0;
            s->mode = LOGIN_MODE_MUST_CHANGE;
            s->field = 0;
            login_set_status(s, "Default password must be changed", COL_YELLOW);
            serial_write("[login] '");
            serial_write(s->username);
            serial_write("' uses a default password, forcing change\n");
            return false;
        }

        login_set_status(s, "Login successful. Launching desktop...", COL_GREEN);
        return true;
    }

    s->failed_attempts++;
    s->pass_len = 0;
    s->password[0] = '\0';

    if (s->failed_attempts >= 5) {
        s->lock_until_tick = now + (10 * PIT_HZ);
        s->failed_attempts = 0;
        login_set_status(s, "Locked for 10s after repeated failures", COL_RED);
    } else {
        login_set_status(s, "Invalid credentials. Try again", COL_RED);
    }
    return false;
}

/* Returns true when the password was changed and the desktop may be entered. */
static bool login_apply_password_change(login_state_t *s) {
    if (s->newpass_len == 0 || s->confirm_len == 0) {
        login_set_status(s, "Enter the new password twice", COL_YELLOW);
        return false;
    }
    if (kstrcmp(s->newpass, s->confirm) != 0) {
        login_set_status(s, "Passwords do not match", COL_RED);
        s->confirm[0] = '\0';
        s->confirm_len = 0;
        s->field = 1;
        return false;
    }
    if (kstrcmp(s->newpass, s->verified_pass) == 0) {
        login_set_status(s, "Choose a password you have not used", COL_YELLOW);
        return false;
    }

    int rc = user_change_password(s->username, s->verified_pass, s->newpass);
    if (rc == 0) {
        /* Clear the secrets we were holding in this frame's state. */
        kmemset(s->newpass, 0, sizeof(s->newpass));
        kmemset(s->confirm, 0, sizeof(s->confirm));
        kmemset(s->verified_pass, 0, sizeof(s->verified_pass));
        s->newpass_len = 0;
        s->confirm_len = 0;
        login_set_status(s, "Password updated. Launching desktop...", COL_GREEN);
        serial_write("[login] password changed, entering desktop\n");
        return true;
    }

    if (rc == -2)
        login_set_status(s, "Need 8+ chars with upper, lower and a number", COL_YELLOW);
    else if (rc == -4)
        login_set_status(s, "Current password no longer valid. Sign in again", COL_RED);
    else
        login_set_status(s, "Could not change password", COL_RED);
    return false;
}

static bool login_create_account(login_state_t *s) {
    int rc;
    if (s->user_len == 0 || s->pass_len == 0) {
        login_set_status(s, "Enter a username and strong password", COL_YELLOW);
        return false;
    }

    rc = user_register(s->username, s->password);
    if (rc == 0) {
        login_set_status(s, "Account created. Sign in with your new credentials.", COL_GREEN);
        s->pass_len = 0;
        s->password[0] = '\0';
        s->field = 1;
        s->mode = LOGIN_MODE_SIGNIN;
        return false;
    }

    if (rc == -2)
        login_set_status(s, "Password must include upper/lowercase letters and a number", COL_YELLOW);
    else if (rc == -1)
        login_set_status(s, "That username is unavailable", COL_RED);
    else
        login_set_status(s, "Unable to create account right now", COL_RED);
    return false;
}

static void login_fade_out(const login_state_t *s, mouse_t *mouse) {
    for (int step = 0; step <= 10; step++) {
        draw_login_screen(s, mouse);
        gfx_rect_blend(0, 0, (i32)SCREEN_W, (i32)SCREEN_H, COL_BLACK, (u8)(step * 20));
        mouse_draw_cursor(mouse->x, mouse->y);
        gfx_flip();
        timer_wait(14);
    }
}

static void desktop_fade_in(mouse_t *mouse) {
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

static bool run_login_flow(mouse_t *mouse, bool lock_mode) {
#ifdef CAREOS_QA_AUTOLOGIN
    /* Headless QA build: the login gate needs a mouse/keyboard that the
     * harness cannot reliably drive. Built only with make QA_AUTOLOGIN=1. */
    (void)mouse; (void)lock_mode;
    return true;
#endif
    login_state_t login;
    kmemset(&login, 0, sizeof(login));
    login.locked = lock_mode;

    if (lock_mode) {
        /* Re-authenticate the current user only: pin the username, skip the
         * greeter entirely, and land straight on the password field. */
        login.mode = LOGIN_MODE_SIGNIN;
        const char *cur = user_current_name();
        if (cur) {
            kstrncpy(login.username, cur, sizeof(login.username) - 1);
            login.username[sizeof(login.username) - 1] = '\0';
            login.user_len = (u32)kstrlen(login.username);
        }
        login.field = 1;
        login_set_status(&login, "Enter your password to unlock", COL_DIM);
    } else {
        login.field = 0;
        login.mode = (user_enum_count() > 0) ? LOGIN_MODE_PICK : LOGIN_MODE_SIGNUP;
        if (login.mode == LOGIN_MODE_PICK)
            login_set_status(&login, "Choose an account to continue", COL_DIM);
        else
            login_set_status(&login, "Create a strong local account", COL_DIM);
    }

    keyboard_flush();
    mouse->x = (i32)SCREEN_W / 2;
    mouse->y = (i32)SCREEN_H / 2;
    serial_write("[login] login screen ready\n");

    while (1) {
        login_layout_t layout = login_make_layout(&login);
        /* Only computed in PICK mode -- greeter_make_layout() calls
         * user_enum_at() up to GREETER_MAX_ROWS times, no need to pay that
         * every frame once past the picker. */
        greeter_layout_t glayout;
        kmemset(&glayout, 0, sizeof(glayout));
        if (login.mode == LOGIN_MODE_PICK)
            glayout = greeter_make_layout(&login);

        while (keyboard_haschar()) {
            char c = gui_read_key();

            if (login.mode == LOGIN_MODE_PICK) {
                /* Rows only cover the first GREETER_MAX_ROWS accounts, so
                 * typing is the escape hatch that makes every account (up to
                 * MAX_USERS) reachable: any printable key drops straight to
                 * the password screen with an empty, editable username and
                 * feeds that keystroke in as its first character. Enter,
                 * Tab, and Backspace are no-ops here -- there is no field to
                 * act on yet. */
                if (c == '\n' || c == '\t' || c == '\b') continue;
                if (c < 32 || c > 126) continue;

                login.username[0] = c;
                login.username[1] = '\0';
                login.user_len = 1;
                login.pass_len = 0;
                login.password[0] = '\0';
                login.mode = LOGIN_MODE_SIGNIN;
                login.field = 0;
                login_set_status(&login, "Enter your username and password", COL_DIM);
                continue;
            }

            if (c == '\t') {
                /* Locked mode has no username field to tab into. */
                if (!login.locked) login.field = 1 - login.field;
                continue;
            }
            if (c == '\n') {
                if (login.mode == LOGIN_MODE_MUST_CHANGE) {
                    if (login_apply_password_change(&login)) {
                        login_fade_out(&login, mouse);
                        return true;
                    }
                } else if (login.mode == LOGIN_MODE_SIGNIN) {
                    if (login_try(&login)) {
                        login_fade_out(&login, mouse);
                        return true;
                    }
                } else {
                    login_create_account(&login);
                }
                continue;
            }
            if (c == '\b') {
                if (login.mode == LOGIN_MODE_MUST_CHANGE) {
                    if (login.field == 0 && login.newpass_len > 0) {
                        login.newpass_len--;
                        login.newpass[login.newpass_len] = '\0';
                    } else if (login.field == 1 && login.confirm_len > 0) {
                        login.confirm_len--;
                        login.confirm[login.confirm_len] = '\0';
                    }
                } else if (!login.locked && login.field == 0 && login.user_len > 0) {
                    login.user_len--;
                    login.username[login.user_len] = '\0';
                } else if (login.field == 1 && login.pass_len > 0) {
                    login.pass_len--;
                    login.password[login.pass_len] = '\0';
                }
                continue;
            }
            if (c < 32 || c > 126) continue;

            if (login.mode == LOGIN_MODE_MUST_CHANGE) {
                if (login.field == 0) {
                    if (login.newpass_len < sizeof(login.newpass) - 1) {
                        login.newpass[login.newpass_len++] = c;
                        login.newpass[login.newpass_len] = '\0';
                    }
                } else {
                    if (login.confirm_len < sizeof(login.confirm) - 1) {
                        login.confirm[login.confirm_len++] = c;
                        login.confirm[login.confirm_len] = '\0';
                    }
                }
            } else if (!login.locked && login.field == 0) {
                if (login.user_len < sizeof(login.username) - 1) {
                    login.username[login.user_len++] = c;
                    login.username[login.user_len] = '\0';
                }
            } else {
                if (login.pass_len < sizeof(login.password) - 1) {
                    login.password[login.pass_len++] = c;
                    login.password[login.pass_len] = '\0';
                }
            }
        }

        mouse_update(mouse);

        if (mouse->left_clicked) {
            if (login.mode == LOGIN_MODE_PICK) {
                bool hit = false;
                for (u32 i = 0; i < glayout.row_count; i++) {
                    if (!rect_contains(glayout.rows[i].rect, mouse->x, mouse->y)) continue;

                    login.picked_uid = glayout.rows[i].uid;
                    kstrncpy(login.username, glayout.rows[i].name, sizeof(login.username) - 1);
                    login.username[sizeof(login.username) - 1] = '\0';
                    login.user_len = (u32)kstrlen(login.username);
                    login.pass_len = 0;
                    login.password[0] = '\0';
                    login.mode = LOGIN_MODE_SIGNIN;
                    login.field = 1;

                    char msg[96] = "Enter password for ";
                    kstrcat(msg, login.username);
                    login_set_status(&login, msg, COL_DIM);
                    hit = true;
                    break;
                }
                /* Escape hatch: same destination as typing directly -- an
                 * empty, editable username -- so every account beyond the
                 * GREETER_MAX_ROWS display cap stays reachable by mouse too. */
                if (!hit && rect_contains(glayout.other_row, mouse->x, mouse->y)) {
                    login.username[0] = '\0';
                    login.user_len = 0;
                    login.pass_len = 0;
                    login.password[0] = '\0';
                    login.mode = LOGIN_MODE_SIGNIN;
                    login.field = 0;
                    login_set_status(&login, "Enter your username and password", COL_DIM);
                }
            } else if (!login.locked && rect_contains(layout.user_field, mouse->x, mouse->y))
                login.field = 0;
            else if (rect_contains(layout.pass_field, mouse->x, mouse->y))
                login.field = 1;
            else if (!login.locked && login.mode == LOGIN_MODE_SIGNIN && user_enum_count() > 0 &&
                     rect_contains(layout.avatar, mouse->x, mouse->y)) {
                /* Back affordance: the avatar returns to the greeter. Not
                 * offered in lock mode -- there is no picker to return to. */
                login.mode = LOGIN_MODE_PICK;
                login.pass_len = 0;
                login.password[0] = '\0';
                login_set_status(&login, "Choose an account to continue", COL_DIM);
            } else if (button_take_click(&layout.primary_btn, mouse)) {
                if (login.mode == LOGIN_MODE_MUST_CHANGE) {
                    if (login_apply_password_change(&login)) {
                        login_fade_out(&login, mouse);
                        return true;
                    }
                } else if (login.mode == LOGIN_MODE_SIGNIN) {
                    if (login_try(&login)) {
                        login_fade_out(&login, mouse);
                        return true;
                    }
                } else {
                    login_create_account(&login);
                }
            } else if (!login.locked && button_take_click(&layout.secondary_btn, mouse)) {
                if (login.mode == LOGIN_MODE_MUST_CHANGE) {
                    /* Cancel drops the session and returns to sign-in. The
                     * must_change flag stays set, so there is no way past it. */
                    user_logout();
                    kmemset(&login.newpass, 0, sizeof(login.newpass));
                    kmemset(&login.confirm, 0, sizeof(login.confirm));
                    kmemset(&login.verified_pass, 0, sizeof(login.verified_pass));
                    login.newpass_len = 0;
                    login.confirm_len = 0;
                    login.pass_len = 0;
                    login.password[0] = '\0';
                    login.mode = LOGIN_MODE_SIGNIN;
                    login.field = 1;
                    login_set_status(&login, "Sign in to continue", COL_DIM);
                } else {
                    login.mode = (login.mode == LOGIN_MODE_SIGNIN) ? LOGIN_MODE_SIGNUP : LOGIN_MODE_SIGNIN;
                    login.pass_len = 0;
                    login.password[0] = '\0';
                    login.field = (login.mode == LOGIN_MODE_SIGNIN) ? 1 : 0;
                    login_set_status(&login,
                        login.mode == LOGIN_MODE_SIGNIN
                            ? "Sign in with an existing account"
                            : "Create a strong local account",
                        COL_DIM);
                }
            }
        }

        if (login.mode == LOGIN_MODE_PICK)
            draw_greeter(&login, mouse);
        else
            draw_login_screen(&login, mouse);
        mouse_draw_cursor(mouse->x, mouse->y);
        gfx_flip();
        __asm__ volatile("sti; hlt");
    }
}

/* Post-login desktop setup, shared between the initial boot-to-desktop path
   and re-entry after Lock / Log Out / Switch User. Only opens a fresh
   Terminal if one isn't already open (Lock returns to an existing session). */
static void enter_desktop(mouse_t *mouse) {
    rc_care_run_startup();

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
    if (fast_boot) {
        draw_boot_splash(BOOT_STAGE_COUNT, timer_ms());
        timer_wait(20);
    } else {
        for (int i = 0; i <= BOOT_STAGE_COUNT; i++) {
            draw_boot_splash(i, timer_ms());
            timer_wait(90);
        }
    }
    serial_write("  [gui_run] splash done\n");

    serial_write("  [gui_run] login gate\n");
    kmemset(&mouse, 0, sizeof(mouse));
    if (!run_login_flow(&mouse, false)) {
        serial_write("  [gui_run] login flow returned failure\n");
    }
    session_begin(user_current_uid());
    theme_switch(settings_get()->theme == 0);

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
                    run_login_flow(&mouse, true);
                    session_begin(user_current_uid());
                    theme_switch(settings_get()->theme == 0);
                    needs_redraw = true;
                } else {
                    /* LOGOUT / Switch User: drop the session entirely and show
                       the full greeter so a different account can sign in. */
                    wm_close_all();      /* clean desktop for the next user */
                    session_end();
                    run_login_flow(&mouse, false);
                    session_begin(user_current_uid());
                    theme_switch(settings_get()->theme == 0);
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
                run_login_flow(&mouse, true);
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
                run_login_flow(&mouse, true);
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
                if (bw && bw->active && bw->app == APP_BROWSER && bw->browser_loading) {
                    app_browser_tick(bw);
                    activity = true;           /* spinner + result */
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
