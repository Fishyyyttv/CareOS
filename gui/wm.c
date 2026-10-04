/* =============================================================================
 * CareOS gui/wm.c  -- Window Manager v9
 *
 * Improvements over v4:
 *   - wm_open: single, clean init path per app. No double-init.
 *   - wm_close: marks slot free immediately; app cleanup called first.
 *             Also decrements open_count to keep cascade accurate.
 *   - wm_focus: correct z_order field. Clears ALL other focused flags.
 *   - wm_maximize: dedicated maximized + restore_rect fields.
 *   - wm_handle_mouse:
 *       * drag processed first (before click detection) -- fixes drag-stop bug
 *       * hit detection uses z_order
 *       * shared, non-overlapping left-side close/min/max targets
 *       * app click coords passed relative to client rect
 *       * NEW: edge/corner resizing with min size enforcement
 *   - wm_draw_all: z-sorted draw. Enhanced shadow on focused window.
 *   - Titlebar text: COL_TRANSPARENT bg (no black box).
 *   - Window cascade: new windows slightly offset so they don't perfectly stack.
 * ============================================================================= */
#include "kernel.h"
#include "gui.h"
#include "ui.h"
#include "image.h"
#include "icon.h"
#include "font.h"
#include "window_chrome.h"
#include "shadow.h"

static window_t windows[MAX_WINDOWS];
static u32 next_window_instance_id;
window_t *wm_get_window(int i) { if(i<0||i>=MAX_WINDOWS) return NULL; return &windows[i]; }
static u32      wm_next_z    = 1;
bool     launcher_open = false;
static u32      open_count    = 0;   /* for cascade offset */

/* -- Global clipboard ----------------------------------------------------- */
char g_clipboard[CLIPBOARD_SIZE] = {0};
u32  g_clipboard_len = 0;
bool g_clipboard_is_cut = false;

/* -- Multi-desktop -------------------------------------------------------- */
u32  g_current_desktop = 0;

/* Is w shown on the current workspace? (0xFF = sticky, on every workspace.)
 * Every focus/input path uses this: windows on other workspaces used to stay
 * focusable and clickable while invisible. */
bool wm_on_current_desktop(const window_t *w) {
    return w->desktop == g_current_desktop || w->desktop == 0xFF;
}

/* Last pointer position seen by wm_handle_mouse: an unfocused window with the
 * pointer near it may show hover effects, so it is drawn live, not cached. */
static i32 s_wm_mouse_x = -10000, s_wm_mouse_y = -10000;
/* The topmost window within 16 px of the pointer this frame (set by
 * wm_draw_all). Only it can show hover effects; windows below it cannot. */
static const window_t *s_wm_hover_top;

static u32 wm_theme_id(void) {
    /* Accent changes mutate the theme struct in place, so fold them in. */
    return (u32)(uintptr_t)g_theme ^ g_theme->primary ^ (g_theme->accent << 1) ^
           (g_theme->selection << 2) ^ g_theme->surface;
}

/* Apps whose pixels change on their own every frame or two. */
static bool wm_app_always_live(const window_t *w) {
    switch (w->app) {
    case APP_CLOCK: case APP_SYSMON: case APP_NETMON:
    case APP_DOOM: case APP_MAZE: case APP_3D:
        return true;
    case APP_BROWSER:
        return w->browser_loading;
    default:
        return false;
    }
}

static wcache_action_t wm_cache_action(window_t *w, i32 wx, i32 wy, i32 wd, i32 ht, bool transition) {
    wcache_query_t q;
    kmemset(&q, 0, sizeof q);
    q.focused    = w->focused;
    q.animating  = transition || w->animating || w->opacity != 255;
    (void)wx; (void)wy;
    q.hover_near = (w == s_wm_hover_top);
    q.busy       = wm_app_always_live(w) || w->showing_snap_layouts ||
                   w->hover_edge != RESIZE_NONE || w->resizing || w->dragging;
    q.x = wx; q.y = wy; q.w = wd; q.h = ht;
    q.screen_w = (i32)SCREEN_W; q.screen_h = (i32)SCREEN_H;
    q.now_ms   = timer_ms();
    q.theme_id = wm_theme_id();
    q.font_id  = font_active_index();
    q.stagger_ms = (u32)((w - windows) * 97 % 400);
    wcache_action_t a = wcache_decide(&w->wcache, &q);
    if (a == WC_DRAW && w->focused && w->wcache_px) {       /* free RAM while focused */
        kfree(w->wcache_px); w->wcache_px = NULL;
    }
    if (a == WC_BLIT && !w->wcache_px) a = WC_DRAW;
    return a;
}

static void wm_cache_capture(window_t *w, i32 wx, i32 wy, i32 wd, i32 ht) {
    if (w->wcache_px && (w->wcache.w != wd || w->wcache.h != ht)) {
        kfree(w->wcache_px); w->wcache_px = NULL;
    }
    if (!w->wcache_px) w->wcache_px = (u32 *)kmalloc((size_t)wd * (size_t)ht * 4u);
    if (!w->wcache_px) { w->wcache.valid = false; return; }
    gfx_snapshot_rect(wx, wy, wd, ht, w->wcache_px);
    wcache_query_t q;
    kmemset(&q, 0, sizeof q);
    q.w = wd; q.h = ht; q.now_ms = timer_ms();
    q.theme_id = wm_theme_id(); q.font_id = font_active_index();
    wcache_captured(&w->wcache, &q);
}

/* -- Idle tracking (for screensaver / auto-lock) -------------------------- */
u32  g_last_activity_tick = 0;

/* -- Desktop shortcut column (visual-system spec §10, Desktop artboard) ----
 * Left 28, top 68, 96 px buttons with 56 px icons and a 13 px semibold label.
 * Only shortcuts with something real behind them: Home opens Files, Wallpapers
 * opens Settings. "Care scripts" and "Trash" join when CareOS has them. */
static desktop_icon_t icons[] = {
    {"Home",       APP_FILES,    0,0,false,false, 0},
    {"Wallpapers", APP_SETTINGS, 0,0,false,false, 0},
};
#define ICON_COUNT ((int)(sizeof(icons) / sizeof(icons[0])))
#define SHORTCUT_W     96
#define SHORTCUT_ICON  56
#define SHORTCUT_GAP   14

/* Taskbar layout constants */
#define TB_LAUNCHER_W  54
#define TB_TRAY_W     190
#define TB_SLOT_W     140
#define TB_SLOT_H     (TASKBAR_H - 14)
#define TB_PAD          7
#define TB_SHOWDESK_W  22   /* "show desktop" nub at far-right edge */

/* Taskbar clock cache */
static u32        tb_last_tick = 0;
static rtc_time_t tb_time;

static i32 shortcut_h(void) {
    return 8 + SHORTCUT_ICON + 8 + gfx_role_line_h(FONT_ROLE_UI, 1) + 8;
}

static rect_t sidebar_icon_rect(int idx) {
    return rect_make(icons[idx].x, icons[idx].y, SHORTCUT_W, shortcut_h());
}

static void layout_icons(void) {
    i32 y = TOPBAR_H + 28;
    for (int i = 0; i < ICON_COUNT; i++) {
        icons[i].x = 28;
        icons[i].y = y;
        y += shortcut_h() + SHORTCUT_GAP;
    }
}

static void draw_desktop_icon(const desktop_icon_t *ic) {
    rect_t r = sidebar_icon_rect((int)(ic - icons));
    if (ic->hover)
        gfx_rect_rounded_blend(r.x, r.y, r.w, r.h, 16, g_theme->is_dark ? 0xFFFFFFu : g_theme->ink, 20);
    ui_app_icon(ui_icon_for_app(ic->app), r.x + (r.w - SHORTCUT_ICON) / 2, r.y + 8, SHORTCUT_ICON);
    /* Label with the artboard's soft text shadow, so it reads on any wallpaper. */
    i32 tw = gfx_str_role_width(ic->label, FONT_ROLE_UI, 1, FONT_W_BOLD);
    i32 tx = r.x + (r.w - tw) / 2, ty = r.y + 8 + SHORTCUT_ICON + 8;
    u32 ink = g_theme->is_dark ? g_theme->ink : g_theme->ink;
    if (g_theme->is_dark) gfx_str_role(tx, ty + 1, ic->label, 0x000000, FONT_ROLE_UI, 1, FONT_W_BOLD);
    gfx_str_role(tx, ty, ic->label, ink, FONT_ROLE_UI, 1, FONT_W_BOLD);
}

/* -- Desktop hero (visual-system spec §10) --------------------------------
 * Date, "Good <part of day>, <name>." and the bar reserved for the coming AI
 * assistant (a placeholder; it does not open anything). Artboard position left 260 / top 200 / width 700 at 1440x900,
 * scaled to the screen. Resume chips need a real source (last session, recent
 * files, pending updates); none exists yet, so none are shown. Hidden on short
 * screens and while a maximized window covers the desktop. */
static bool hero_visible(void) {
    if ((i32)SCREEN_H < 720) return false;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        window_t *w = &windows[i];
        if (w->active && !w->minimized && w->maximized && wm_on_current_desktop(w)) return false;
    }
    return true;
}

static rect_t hero_search_rect(void) {
    i32 x = 260 * (i32)SCREEN_W / 1440, y = 200 * (i32)SCREEN_H / 900;
    i32 head = gfx_role_line_h(FONT_ROLE_UI, 4) + 6 + gfx_role_line_h(FONT_ROLE_DISPLAY, 4);
    return rect_make(x, y + head + 22, 700, 60);
}

static void desktop_draw_hero(void) {
    if (!hero_visible()) return;
    rtc_time_t t;
    clock_read_local(&t);
    static const char *const DAYS[7] = { "Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday" };
    static const char *const MONS[12] = { "January","February","March","April","May","June","July",
                                          "August","September","October","November","December" };
    i32 x = 260 * (i32)SCREEN_W / 1440, y = 200 * (i32)SCREEN_H / 900;

    char line[40];
    ksprintf(line, "%s, %s %d", DAYS[clock_day_of_week((i32)t.year, (i32)t.month, (i32)t.day)],
             (t.month >= 1 && t.month <= 12) ? MONS[t.month - 1] : "", (int)t.day);
    gfx_str_role(x, y, line, g_theme->is_dark ? 0xD5DAEAu : g_theme->ink2, FONT_ROLE_UI, 4, FONT_W_REGULAR);
    y += gfx_role_line_h(FONT_ROLE_UI, 4) + 6;

    /* Greeting by local hour: 5-12 morning, 12-18 afternoon, otherwise evening. */
    const char *part = (t.hour >= 5 && t.hour < 12) ? "morning" : (t.hour >= 12 && t.hour < 18) ? "afternoon" : "evening";
    const char *name = user_current_name();
    char greet[80];
    ksprintf(greet, "Good %s, %s.", part, (name && name[0]) ? name : "friend");
    gfx_str_role_fit(x, y, (i32)SCREEN_W - x - 360, greet, g_theme->ink, FONT_ROLE_DISPLAY, 4, FONT_W_BOLD);

    /* Search bar: glass, 20 radius, hairline, the artboard's soft drop shadow. */
    rect_t s = hero_search_rect();
    ui_glass(30, s, 20, g_theme->panel_tint, g_theme->is_dark ? 184 : 199, 24,
             UI_GLASS_HAIRLINE | UI_GLASS_SHADOW_M);
    /* Reserved for the AI assistant: a placeholder, not wired to anything yet. */
    ui_care_logo(s.x + 18, s.y + (s.h - 22) / 2, 22);
    gfx_str_role(s.x + 52, s.y + (s.h - gfx_role_line_h(FONT_ROLE_UI, 3)) / 2,
                 "Ask Care anything", g_theme->muted, FONT_ROLE_UI, 3, FONT_W_REGULAR);
    {
        const char *soon = "Coming soon";
        i32 cw = gfx_str_role_width(soon, FONT_ROLE_UI, 0, FONT_W_BOLD) + 20, ch = 26;
        i32 cx = s.x + s.w - 14 - cw, cy = s.y + (s.h - ch) / 2;
        gfx_rect_rounded(cx, cy, cw, ch, ch / 2, g_theme->raised2);
        gfx_str_role(cx + 10, cy + (ch - gfx_role_line_h(FONT_ROLE_UI, 0)) / 2, soon,
                     g_theme->ink2, FONT_ROLE_UI, 0, FONT_W_BOLD);
    }
}

void desktop_draw(void) {
    desktop_draw_hero();
    for (int i = 0; i < ICON_COUNT; i++) draw_desktop_icon(&icons[i]);
}


/* -- WM init -------------------------------------------------------------- */
void wm_init(void) {
    kmemset(windows, 0, sizeof(windows));
    open_count = 0;
    wm_next_z  = 1;
    launcher_open = false;
    layout_icons();
    rtc_read(&tb_time);
}

/* -- Window size helpers -------------------------------------------------- */
void app_default_size(app_id_t app, i32 sw, i32 sh, i32 *w, i32 *h) {
    switch (app) {
    case APP_CALC:     *w = 320; *h = 440; return;
    case APP_CLOCK:    *w = 300; *h = 330; return;   /* room for time + weekday/date lines */
    case APP_ABOUT: {
        /* app_about_draw stacks a logo, an H2 wordmark, a version line, a
         * rule, seven info rows and a footer, all on the live line height. A
         * flat 340 already truncated the last rows and gets worse the taller
         * the face, so mirror that stack here. */
        i32 lh   = FONT_H * (i32)GFX_FONT_SCALE;
        i32 logo = 36 * (i32)GFX_FONT_SCALE;
        *w = 460;
        *h = TITLEBAR_H + 24 + 2 * logo + 18   /* logo block + gap */
           + 2 * lh + 4                        /* H2 wordmark row */
           + lh + 16                           /* version line + gap */
           + 14 + 7 * (lh + 8)                 /* rule + seven info rows */
           + 8 + 12 + lh + 20;                 /* rule + footer + padding */
        return;
    }
    case APP_PAINT:    *w = sw*75/100; *h = sh*75/100; return;
    case APP_DOOM:     *w = 640; *h = 400 + TITLEBAR_H; return; /* 640x400 client + titlebar */
    default:           *w = sw*65/100; *h = sh*70/100; return;
    }
}

/* Smallest window each app still lays out correctly in, derived from the
 * app's own fixed columns (keep in sync with them), scaled with the font and
 * capped to the screen. Applied on open, resize and snap. */
void app_min_size(app_id_t app, i32 *w, i32 *h) {
    i32 sc = (i32)GFX_FONT_SCALE;
    i32 mw = MIN_WIN_W, mh = MIN_WIN_H;
    switch (app) {
    case APP_SETTINGS: mw = (120 + 64 * sc) + 52 + 320 * sc; mh = 420 * sc; break; /* sidebar sb, app_settings.c */
    case APP_FILES:    mw = (88 + 52 * sc) + 420 * sc;       mh = 320 * sc; break; /* FM_SB + toolbar, app_files.c */
    case APP_BROWSER:  mw = 460 * sc;                         mh = 320 * sc; break;
    case APP_EDITOR:   mw = 380 * sc;                         mh = 280 * sc; break;
    case APP_PAINT:    mw = 500 * sc;                         mh = 340 * sc; break;
    case APP_USERS:    mw = 460 * sc;                         mh = 380 * sc; break;
    case APP_PKGMGR:   mw = 440 * sc;                         mh = 320 * sc; break;
    case APP_CALC:     mw = 260 * sc;                         mh = 380 * sc; break;
    case APP_DOOM:     mw = 640;                              mh = 400 + TITLEBAR_H; break;
    case APP_ABOUT:    mw = 460 * sc;                         mh = 420 * sc; break;
    case APP_NETMON:   mw = 360 * sc;                         mh = 280 * sc; break;
    case APP_SYSMON:   mw = 360 * sc;                         mh = 300 * sc; break;
    case APP_CLOCK:    mw = 280 * sc;                         mh = 320 * sc; break;
    default: break;
    }
    if (mw < MIN_WIN_W) mw = MIN_WIN_W;
    if (mh < MIN_WIN_H) mh = MIN_WIN_H;
    if (mw > (i32)SCREEN_W) mw = (i32)SCREEN_W;
    if (mh > (i32)SCREEN_H - (i32)TOPBAR_H) mh = (i32)SCREEN_H - (i32)TOPBAR_H;
    *w = mw; *h = mh;
}

/* -- Animation helpers (macOS-style open/close/minimise transitions) ------ */

/* Shrink/grow a rect by `pct` percent about its own centre (integer math). */
static rect_t rect_scale_center(rect_t r, i32 pct) {
    i32 nw = r.w * pct / 100;
    i32 nh = r.h * pct / 100;
    return rect_make(r.x + (r.w - nw) / 2, r.y + (r.h - nh) / 2, nw, nh);
}

/* Small landing rect at the bottom-centre (the dock) -- minimise target. */
static rect_t wm_dock_rect(void) {
    i32 mw = 80, mh = 48;
    return rect_make((i32)SCREEN_W / 2 - mw / 2,
                     (i32)SCREEN_H - (i32)TASKBAR_H - mh / 2, mw, mh);
}

/* Hand focus to the topmost visible window other than `skip`, skipping any
 * window that is itself busy closing or minimising. */
static void wm_refocus_after(window_t *skip) {
    window_t *top = NULL; u32 bz = 0;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        window_t *o = &windows[i];
        if (o == skip || !o->active || o->minimized || !wm_on_current_desktop(o)) continue;
        if (o->anim_kind == WM_ANIM_CLOSE || o->anim_kind == WM_ANIM_MINIMIZE) continue;
        if (o->z_order >= bz) { top = o; bz = o->z_order; }
    }
    if (top) top->focused = true;
}

/* Real teardown -- deferred until the close animation finishes. Mirrors the
 * original wm_close side effects (slot freed, cascade count decremented, buffer freed). */
static void wm_finish_close(window_t *w) {
    if (w->win_buffer.pixels) { kfree(w->win_buffer.pixels); w->win_buffer.pixels = NULL; }
    if (w->wcache_px) { kfree(w->wcache_px); w->wcache_px = NULL; }
    if (w->app == APP_TERMINAL) app_terminal_close(w);
    if (w->app == APP_BROWSER) {
        browser_fetch_cancel(w->instance_id);
        if (w->bview_px) { kfree(w->bview_px); w->bview_px = NULL; }
        if (w->browser_links) { kfree(w->browser_links); w->browser_links = NULL; }
        for (int t = 0; t < 4; t++)
            if (w->browser_tab_content[t]) { kfree(w->browser_tab_content[t]); w->browser_tab_content[t] = NULL; }
    }
    if (w->text_buf) { kfree(w->text_buf); w->text_buf = NULL; w->text_cap = 0; }
    w->wcache.valid = false;
    if (w->root) { widget_destroy(w->root); w->root = NULL; }
    w->active    = false;
    w->focused   = false;
    w->animating = false;
    w->anim_kind = WM_ANIM_NONE;
    if (open_count > 0) open_count--;
}

/* Start the reverse-of-minimise transition: grow out of the dock + fade in.
 * Called both from the minimise-toggle and when re-opening a minimised app. */
static void wm_start_restore(window_t *w) {
    rect_t home = w->rect;              /* real geometry kept while minimised */
    w->minimized       = false;
    w->anim_from       = wm_dock_rect();
    w->target_rect     = home;
    w->anim_kind       = WM_ANIM_RESTORE;
    w->anim_start_tick = timer_ms();
    w->opacity         = 0;
    w->animating       = true;
    wm_focus(w);
}

/* -- Open window -- SINGLE init path -------------------------------------- */
window_t *wm_open(app_id_t app, const char *title,
                  i32 x, i32 y, i32 w, i32 h) {
    {   /* Never open an app smaller than it can lay out. */
        i32 mnw, mnh; app_min_size(app, &mnw, &mnh);
        if (w < mnw) w = mnw;
        if (h < mnh) h = mnh;
    }

    /* If app already open -- restore + focus, no re-init */
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (windows[i].active && windows[i].app == app && app != APP_NONE) {
            windows[i].desktop = g_current_desktop;
            if (windows[i].minimized) wm_start_restore(&windows[i]); /* grow from dock */
            else                      wm_focus(&windows[i]);
            serial_write("  [wm_open] restored existing window\n");
            return &windows[i];
        }
    }

    /* Find free slot */
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (windows[i].active) continue;

        window_t *win = &windows[i];
        kmemset(win, 0, sizeof(window_t));
        win->desktop = g_current_desktop;
        /* Every app's text/state buffer. Zero-filled: Paint uses it as a 64x64
         * canvas and NetMon as history bytes. If it cannot be allocated the
         * window is not created at all (the slot stays free). */
        win->text_buf = (char *)kmalloc(WIN_TEXT_BUF);
        if (!win->text_buf) return NULL;
        kmemset(win->text_buf, 0, WIN_TEXT_BUF);
        win->text_cap = WIN_TEXT_BUF;
        win->active   = true;
        win->instance_id = ++next_window_instance_id;
        if (!win->instance_id) win->instance_id = ++next_window_instance_id;
        win->focused  = true;
        win->app      = app;
        win->z_order  = wm_next_z++;

        kstrncpy(win->title, title, 31);

        /* Cascade: offset each new window so they don't perfectly stack */
        i32 cascade = (i32)(open_count % 8) * 32;

        i32 fx = x + cascade;
        i32 fy = y + cascade;

        i32 sw = (i32)SCREEN_W;
        i32 sh = (i32)SCREEN_H;

        /* Keep windows fully on-screen (clean clamp, no weird snapping) */
        if (fx < 0) fx = 0;
        if (fy < TOPBAR_H) fy = TOPBAR_H;

        if (fx + w > sw)
            fx = sw - w;

        if (fy + h > sh - TASKBAR_H)
            fy = sh - TASKBAR_H - h;

        win->rect         = rect_make(fx, fy, w, h);
        win->restore_rect = win->rect;
        win->target_rect  = win->rect;

        /* macOS-style open: scale up from ~95% about the centre + fade in.
         * opacity starts at 0 so the full-size rect never flashes before the
         * first animation tick moves it. */
        win->anim_from       = rect_scale_center(win->rect, 95);
        win->anim_kind       = WM_ANIM_OPEN;
        win->anim_start_tick = timer_ms();
        win->opacity         = 0;
        win->animating       = true;


        /* Initialize structural UI root */
        rect_t cr = wm_client_rect(win);
        win->root = widget_create(WIDGET_PANEL, 0, 0, cr.w, cr.h);
        if (!win->root) {
            kfree(win->text_buf); win->text_buf = NULL; win->text_cap = 0;
            win->active = false;
            return NULL;
        }
        win->root->bg_color = COL_SURFACE;
        
        /* No per-window offscreen buffer: only DOOM needs one, and it
         * allocates its own fixed-size frame in app_doom_init. This used to
         * cost w*h*4 bytes (several MB) for every window of every app. */

        open_count++;

        /* Clear all other focused flags */
        for (int j = 0; j < MAX_WINDOWS; j++)
            if (j != i) windows[j].focused = false;

        /* App init -- called exactly once */
        serial_write("  [wm_open] app_init for: ");
        serial_write(title); serial_write("\n");

        switch (app) {
        case APP_TERMINAL: app_terminal_init(win); break;
        case APP_NOTES:    app_notes_init(win);    break;
        case APP_FILES:    app_files_init(win);    break;
        case APP_CALC:     app_calc_init(win);     break;
        case APP_SETTINGS: app_settings_init(win); break;
        case APP_BROWSER:  app_browser_init(win);  break;
        case APP_PKGMGR:   app_pkgmgr_init(win);  break;
        case APP_EDITOR:   app_editor_init(win);   break;
        case APP_PAINT:    app_paint_init(win);    break;
        case APP_USERS:    app_users_init(win);    break;
        case APP_MAZE:     app_maze_init(win);     break;
        case APP_3D:       app_3d_init(win);       break;
        case APP_DOOM:     app_doom_init(win);     break;
        case APP_ABOUT:    app_about_init(win);    break;
        case APP_HELP:     app_help_init(win);     break;
        default: break;
        }

        return win;
    }

    serial_write("  [wm_open] WARN: max windows reached\n");
    notify_push("CareOS", "Max windows open", COL_YELLOW);
    return NULL;
}

void wm_close(window_t *w) {
    if (!w) return;
    if (w->anim_kind == WM_ANIM_CLOSE) return;  /* already closing */

    /* Start the close transition: shrink to ~95% about centre + fade out. The
     * window stays active/drawn until the animation finishes; the real teardown
     * (slot free + cascade decrement + buffer free) then runs in wm_finish_close()
     * from wm_animate_all(). Hand focus off now so input stops routing to it. */
    w->anim_from       = w->rect;
    w->target_rect     = rect_scale_center(w->rect, 95);
    w->anim_kind       = WM_ANIM_CLOSE;
    w->anim_start_tick = timer_ms();
    w->opacity         = 255;
    w->animating       = true;
    w->focused         = false;
    wm_refocus_after(w);
}

void wm_close_all(void) {
    /* Close every open window (e.g. on Log Out / Switch User) so the next
       session starts with a clean desktop. Tears down synchronously: the login
       flow runs its own loop and never ticks wm_animate_all(), so an animated
       close would leave these windows alive into the next user's session. */
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (windows[i].active) wm_finish_close(&windows[i]);
    }
    open_count = 0;
}

void wm_focus(window_t *w) {
    if (!w) return;
    for (int i = 0; i < MAX_WINDOWS; i++) windows[i].focused = false;
    w->focused = true;
    w->z_order = wm_next_z++;
}

void wm_minimize(window_t *w) {
    if (!w) return;

    /* Already minimised -> play the reverse (grow out of the dock + fade in). */
    if (w->minimized) { wm_start_restore(w); return; }
    if (w->anim_kind == WM_ANIM_MINIMIZE) return;  /* already shrinking */

    /* Start the minimise transition: shrink/fade toward the dock. minimized is
     * set only when the animation completes (in wm_animate_all). */
    w->anim_from       = w->rect;
    w->target_rect     = wm_dock_rect();
    w->anim_kind       = WM_ANIM_MINIMIZE;
    w->anim_start_tick = timer_ms();
    w->opacity         = 255;
    w->animating       = true;

    if (w->focused) { w->focused = false; wm_refocus_after(w); }
}

void wm_maximize(window_t *w) {
    if (!w) return;
    if (w->maximized) {
        w->rect      = w->restore_rect;
        w->maximized = false;
    } else {
        w->restore_rect = w->rect;
        /* Start below the top bar (which draws over windows) so the titlebar and
         * its min/max/close buttons stay reachable, and stop above the dock. */
        w->rect = rect_make(0, (i32)TOPBAR_H, (i32)SCREEN_W,
                            (i32)SCREEN_H - (i32)TOPBAR_H - (i32)TASKBAR_H);
        w->maximized = true;
    }
}

rect_t wm_client_rect(window_t *w) {
    return rect_make(w->rect.x, w->rect.y + TITLEBAR_H,
                     w->rect.w, w->rect.h - TITLEBAR_H);
}

window_t *wm_focused(void) {
    window_t *best = NULL; u32 bz = 0;
    for (int i = 0; i < MAX_WINDOWS; i++)
        if (windows[i].active && windows[i].focused && wm_on_current_desktop(&windows[i]) &&
            windows[i].z_order >= bz) {
            best = &windows[i]; bz = windows[i].z_order;
        }
    return best;
}

window_t *wm_find_app(app_id_t app) {
    for (int i = 0; i < MAX_WINDOWS; i++)
        if (windows[i].active && windows[i].app == app) return &windows[i];
    return NULL;
}

void wm_cycle_focus(int dir) {
    int order[MAX_WINDOWS];
    int cnt = 0;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (windows[i].active && !windows[i].minimized && wm_on_current_desktop(&windows[i]))
            order[cnt++] = i;
    }
    if (cnt <= 1) return;

    for (int a = 0; a < cnt - 1; a++) {
        for (int b = a + 1; b < cnt; b++) {
            if (windows[order[a]].z_order > windows[order[b]].z_order) {
                int t = order[a];
                order[a] = order[b];
                order[b] = t;
            }
        }
    }

    int cur = cnt - 1;
    for (int i = 0; i < cnt; i++) {
        if (windows[order[i]].focused) { cur = i; break; }
    }

    int step = (dir >= 0) ? 1 : -1;
    int nxt = (cur + step + cnt) % cnt;
    wm_focus(&windows[order[nxt]]);
}

void wm_snap_focused(int mode) {
    window_t *w = wm_focused();
    if (!w) return;

    if (mode == SNAP_FULL) {
        wm_maximize(w);
        return;
    }

    i32 sw  = (i32)SCREEN_W;
    i32 top = (i32)TOPBAR_H;                                    /* below the menu bar */
    i32 sh  = (i32)SCREEN_H - top - (i32)TASKBAR_H;             /* usable height       */

    w->maximized = false;
    w->animating = true;
    w->anim_start_tick = timer_ms();
    w->restore_rect = w->rect;

    switch (mode) {
    case SNAP_LEFT:
        w->target_rect = rect_make(0, top, sw/2, sh); break;
    case SNAP_RIGHT:
        w->target_rect = rect_make(sw/2, top, sw-sw/2, sh); break;
    case SNAP_TOP:
        w->target_rect = rect_make(0, top, sw, sh/2); break;
    case SNAP_BOTTOM:
        w->target_rect = rect_make(0, top + sh/2, sw, sh-sh/2); break;
    case SNAP_TL:
        w->target_rect = rect_make(0, top, sw/2, sh/2); break;
    case SNAP_TR:
        w->target_rect = rect_make(sw/2, top, sw-sw/2, sh/2); break;
    case SNAP_BL:
        w->target_rect = rect_make(0, top + sh/2, sw/2, sh-sh/2); break;
    case SNAP_BR:
        w->target_rect = rect_make(sw/2, top + sh/2, sw-sw/2, sh-sh/2); break;
    }
    {   /* A snap zone smaller than the app's minimum grows to the minimum,
         * staying on screen. */
        i32 mnw, mnh; app_min_size(w->app, &mnw, &mnh);
        rect_t *t = &w->target_rect;
        if (t->w < mnw) { if (t->x + mnw > (i32)SCREEN_W) t->x = (i32)SCREEN_W - mnw; t->w = mnw; }
        if (t->h < mnh) { if (t->y + mnh > (i32)SCREEN_H) t->y = (i32)SCREEN_H - mnh; t->h = mnh; }
    }
}

/* -- Detect resize edge under cursor. A wider zone is much easier to grab, and
 *    corners get an even wider hit box so diagonal resize is forgiving. */
#define EDGE_ZONE 14
static cursor_shape_t edge_to_cursor(u32 edge) {
    switch (edge) {
    case RESIZE_LEFT: case RESIZE_RIGHT:  return CURSOR_RESIZE_H;
    case RESIZE_TOP:  case RESIZE_BOTTOM: return CURSOR_RESIZE_V;
    case RESIZE_TL:   case RESIZE_BR:     return CURSOR_RESIZE_NWSE;
    case RESIZE_TR:   case RESIZE_BL:     return CURSOR_RESIZE_NESW;
    default:                              return CURSOR_ARROW;
    }
}
static u32 detect_resize_edge(window_t *w, i32 mx, i32 my) {
    rect_t r = w->rect;
    if (mx < r.x || mx >= r.x + r.w || my < r.y || my >= r.y + r.h) return RESIZE_NONE;
    if (wm_control_hit(r.x, r.y, TITLEBAR_H, mx, my) != WM_CONTROL_NONE) return RESIZE_NONE;

    /* Corners get a larger square so diagonal resize is easy to hit; edges use
     * the base zone. Corners are tested first. */
    const i32 cz = EDGE_ZONE + 10;
    bool cl = mx <  r.x + cz;
    bool cr = mx >= r.x + r.w - cz;
    bool ct = my <  r.y + cz;
    bool cb = my >= r.y + r.h - cz;
    if (ct && cl) return RESIZE_TL;
    if (ct && cr) return RESIZE_TR;
    if (cb && cl) return RESIZE_BL;
    if (cb && cr) return RESIZE_BR;

    bool l  = mx <  r.x + EDGE_ZONE;
    bool rr = mx >= r.x + r.w - EDGE_ZONE;
    bool t  = my <  r.y + EDGE_ZONE;
    bool b  = my >= r.y + r.h - EDGE_ZONE;
    if (l)  return RESIZE_LEFT;
    if (rr) return RESIZE_RIGHT;
    if (t)  return RESIZE_TOP;
    if (b)  return RESIZE_BOTTOM;
    return RESIZE_NONE;
}

bool wm_animate_all(void) {
    bool any = false;
    u32  now = timer_ms();
    for (int i = 0; i < MAX_WINDOWS; i++) {
        window_t *w = &windows[i];
        if (!w->active || !w->animating) continue;   // <-- add !w->animating

        /* -- Transition animations (open/close/minimise/restore) ---------- *
         * Ease the drawn rect from anim_from -> target_rect and fade opacity.
         * anim_kind==NONE falls through to the existing snap/geometry lerp.  */
        if (w->anim_kind != WM_ANIM_NONE) {
            u32 t = cdl_ease_out(now - w->anim_start_tick, CDL_ANIM_MS);

            w->rect.x = cdl_lerp(w->anim_from.x, w->target_rect.x, t);
            w->rect.y = cdl_lerp(w->anim_from.y, w->target_rect.y, t);
            w->rect.w = cdl_lerp(w->anim_from.w, w->target_rect.w, t);
            w->rect.h = cdl_lerp(w->anim_from.h, w->target_rect.h, t);

            /* Opens/restores fade in; closes/minimises fade out. */
            if (w->anim_kind == WM_ANIM_OPEN || w->anim_kind == WM_ANIM_RESTORE)
                w->opacity = (u8)(t * 255 / 256);
            else
                w->opacity = (u8)((256 - t) * 255 / 256);

            if (t >= 256) {
                switch (w->anim_kind) {
                case WM_ANIM_CLOSE:
                    wm_finish_close(w);
                    continue;                      /* slot is now free */
                case WM_ANIM_MINIMIZE:
                    w->rect      = w->anim_from;    /* keep real geometry */
                    w->minimized = true;
                    w->opacity   = 255;
                    break;
                default: /* OPEN / RESTORE settle at the target, fully opaque */
                    w->rect    = w->target_rect;
                    w->opacity = 255;
                    break;
                }
                w->anim_kind = WM_ANIM_NONE;
                w->animating = false;
            } else {
                any = true;
            }
            continue;
        }

        i32 dx = w->target_rect.x - w->rect.x;
        i32 dy = w->target_rect.y - w->rect.y;
        i32 dw = w->target_rect.w - w->rect.w;
        i32 dh = w->target_rect.h - w->rect.h;

        if (dx != 0 || dy != 0 || dw != 0 || dh != 0) {
            w->rect.x += dx / 4;
            w->rect.y += dy / 4;
            w->rect.w += dw / 4;
            w->rect.h += dh / 4;

            /* Division by four cannot advance a remaining delta of 1..3. */
            if (kabs(dx) < 4) w->rect.x = w->target_rect.x;
            if (kabs(dy) < 4) w->rect.y = w->target_rect.y;
            if (kabs(dw) < 4) w->rect.w = w->target_rect.w;
            if (kabs(dh) < 4) w->rect.h = w->target_rect.h;

            any = true;
        } else {
            w->animating = false;   // stop once we’ve reached the target
        }
    }
    return any;
}


static rect_t wm_snap_rect(const window_t *w) {
    return rect_make(wm_snap_x(w->rect.x, (i32)SCREEN_W),
                     w->rect.y + TITLEBAR_H + 4, WM_SNAP_WIDTH, WM_SNAP_HEIGHT);
}

void wm_draw_snap_layouts(window_t *w) {
    if (!w->showing_snap_layouts) return;
    rect_t r = wm_snap_rect(w);
    i32 x = r.x, y = r.y, wd = r.w, ht = r.h;

    gfx_shadow(x, y, wd, ht);
    gfx_rect_rounded(x, y, wd, ht, 8, COL_SURFACE);
    gfx_rect_rounded_outline(x, y, wd, ht, 8, COL_BORDER);

    /* Draw 6 layout icons */
    for (int i=0; i<6; i++) {
        i32 lx = x + 8 + (i%3) * 56;
        i32 ly = y + 8 + (i/3) * 54;
        gfx_rect_outline(lx, ly, 48, 44, COL_DIM);
        /* Visual indicators for where it will snap */
        switch(i) {
            case 0: gfx_rect(lx+2, ly+2, 21, 40, COL_PRIMARY); break; /* Left half */
            case 1: gfx_rect(lx+2, ly+2, 44, 40, COL_PRIMARY); break; /* Full */
            case 2: gfx_rect(lx+24, ly+2, 21, 40, COL_PRIMARY); break; /* Right half */
            case 3: gfx_rect(lx+2, ly+2, 21, 19, COL_PRIMARY); break; /* TL quadrant */
            case 4: gfx_rect(lx+2, ly+2, 44, 19, COL_PRIMARY); break; /* Top half */
            case 5: gfx_rect(lx+24, ly+2, 21, 19, COL_PRIMARY); break; /* TR quadrant */
        }
    }
}

/* Only these small corner tiles need preserving around legacy apps, whose
 * rectangle fills do not all honour the client clip. Reused sequentially for
 * each window: no full-window buffer, heap allocation, or per-frame blur. */
static u32 chrome_corners[4][WM_CHROME_RADIUS * WM_CHROME_RADIUS];

static void wm_corners(i32 x, i32 y, i32 w, i32 h, bool restore) {
    const i32 r = WM_CHROME_RADIUS;
    for (i32 corner = 0; corner < 4; corner++) {
        i32 cx = (corner & 1) ? x + w - r : x;
        i32 cy = (corner & 2) ? y + h - r : y;
        if (!restore) {
            gfx_snapshot_rect(cx, cy, r, r, chrome_corners[corner]);
        } else {
            for (i32 row = 0; row < r; row++) {
                i32 x0, x1;
                if (!rr_row_span(x, y, w, h, r, cy + row, &x0, &x1)) continue;
                for (i32 col = 0; col < r; col++)
                    if (cx + col < x0 || cx + col >= x1)
                        gfx_setpixel(cx + col, cy + row, chrome_corners[corner][row * r + col]);
            }
        }
    }
}

static void wm_draw_controls(window_t *w, const wm_chrome_palette_t *p) {
    i32 cy = w->rect.y + TITLEBAR_H / 2;
    i32 hover = w == s_wm_hover_top
              ? wm_control_hit(w->rect.x, w->rect.y, TITLEBAR_H, s_wm_mouse_x, s_wm_mouse_y)
              : WM_CONTROL_NONE;
    u32 colors[] = {p->close, p->minimize, p->maximize};
    for (i32 i = 0; i < 3; i++) {
        i32 cx = wm_control_x(w->rect.x, i);
        u32 color = w->focused || hover != WM_CONTROL_NONE ? colors[i] : p->inactive;
        /* Rounded fill has an exact 14px footprint (circle_fill is 2r+1). */
        gfx_rect_rounded_blend(cx - 7, cy - 7, 14, 14, 7, color, 255);
        if (hover == WM_CONTROL_NONE) continue;
        if (i == WM_CONTROL_CLOSE) {
            gfx_line(cx - 3, cy - 3, cx + 2, cy + 2, p->glyph);
            gfx_line(cx - 3, cy + 2, cx + 2, cy - 3, p->glyph);
        } else if (i == WM_CONTROL_MIN) {
            gfx_hline(cx - 3, cy, 6, p->glyph);
        } else if (w->maximized) {
            gfx_rect_outline(cx - 2, cy - 3, 5, 5, p->glyph);
            gfx_rect_outline(cx - 3, cy - 1, 5, 4, p->glyph);
        } else {
            gfx_rect_outline(cx - 3, cy - 3, 6, 6, p->glyph);
        }
    }
}

/* -- Draw one window (direct-to-screen, no intermediate buffer) ------------- */
static void draw_window(window_t *w) {
    if (!w->active || w->minimized) return;
    i32 wx = w->rect.x, wy = w->rect.y;
    i32 wd = w->rect.w, ht = w->rect.h;

    /* While a transition (open/close/minimise/restore) is running, draw only
     * the window chrome so app content is never laid out at odd interim sizes.
     * anim_kind==NONE (idle, or snapping) still gets full content as before. */
    bool transition = (w->animating && w->anim_kind != WM_ANIM_NONE);

    /* Soft floating drop shadow (CDL). Focused windows float higher (full CDL
     * defaults); unfocused ones sit lower with a fainter, tighter halo. Drawn
     * before the surface. */
    /* Settled windows use the hollow variant: the opaque body drawn next hides
     * everything under it, so blending there was wasted work. Transitions keep
     * the full shadow (their body is faded over the backdrop). */
    bool hollow = (w->opacity == 255 && !transition);
    if (w->focused) {
        if (hollow) gfx_shadow_soft_hollow(wx, wy, wd, ht, CDL_R_WINDOW,
                                           CDL_SHADOW_BLUR, CDL_SHADOW_ALPHA, CDL_SHADOW_DY);
        else        gfx_shadow_soft(wx, wy, wd, ht, CDL_R_WINDOW);
    } else {
        if (hollow) gfx_shadow_soft_hollow(wx, wy, wd, ht, CDL_R_WINDOW,
                                           CDL_SHADOW_BLUR * 3 / 4, CDL_SHADOW_ALPHA / 2,
                                           CDL_SHADOW_DY / 2);
        else        gfx_shadow_soft_ex(wx, wy, wd, ht, CDL_R_WINDOW,
                                       CDL_SHADOW_BLUR * 3 / 4, CDL_SHADOW_ALPHA / 2,
                                       CDL_SHADOW_DY / 2);
    }

    /* Unfocused, settled windows are blitted from their cached pixels. */
    wcache_action_t wc = wm_cache_action(w, wx, wy, wd, ht, transition);
    if (wc == WC_BLIT) {
        gfx_blit_argb_rounded(wx, wy, w->wcache_px, wd, ht, CDL_R_WINDOW);
        return;
    }

    wm_corners(wx, wy, wd, ht, false);
    const wm_chrome_palette_t *chrome = theme_window_chrome();
    /* Opaque scanline fill: avoid alpha arithmetic across the whole window.
     * The saved corner tiles restore the rounded silhouette after app paint. */
    gfx_rect(wx, wy, wd, ht, chrome->body);

    /* Round only the top corners; the titlebar meets the client along a
     * straight hairline, without the old rectangular highlight leaking out. */
    i32 bar_h = ht < TITLEBAR_H ? ht : TITLEBAR_H;
    for (i32 row = 0; row < bar_h; row++) {
        i32 x0, x1;
        if (rr_row_span(wx, wy, wd, ht, CDL_R_WINDOW, wy + row, &x0, &x1))
            gfx_hline(x0, wy + row, x1 - x0, chrome->bar);
    }
    if (ht > TITLEBAR_H)
        gfx_hline(wx + 1, wy + TITLEBAR_H - 1, wd - 2, chrome->divider);

    /* Resize affordance: light up the edge/corner under the cursor so it is
     * obvious what a drag will grab. Corners get an L of two short bars. */
    if (!transition && w->hover_edge != RESIZE_NONE) {
        u32 hc = COL_ACCENT;
        i32 t = 3, seg = 28, R = CDL_R_WINDOW;
        switch (w->hover_edge) {
        case RESIZE_LEFT:   gfx_rect_rounded(wx, wy + R, t, ht - 2 * R, 1, hc); break;
        case RESIZE_RIGHT:  gfx_rect_rounded(wx + wd - t, wy + R, t, ht - 2 * R, 1, hc); break;
        case RESIZE_TOP:    gfx_rect_rounded(wx + R, wy, wd - 2 * R, t, 1, hc); break;
        case RESIZE_BOTTOM: gfx_rect_rounded(wx + R, wy + ht - t, wd - 2 * R, t, 1, hc); break;
        case RESIZE_TL: gfx_rect_rounded(wx, wy, seg, t, 1, hc); gfx_rect_rounded(wx, wy, t, seg, 1, hc); break;
        case RESIZE_TR: gfx_rect_rounded(wx + wd - seg, wy, seg, t, 1, hc); gfx_rect_rounded(wx + wd - t, wy, t, seg, 1, hc); break;
        case RESIZE_BL: gfx_rect_rounded(wx, wy + ht - t, seg, t, 1, hc); gfx_rect_rounded(wx, wy + ht - seg, t, seg, 1, hc); break;
        case RESIZE_BR: gfx_rect_rounded(wx + wd - seg, wy + ht - t, seg, t, 1, hc); gfx_rect_rounded(wx + wd - t, wy + ht - seg, t, seg, 1, hc); break;
        }
    }

    /* Tiny transition frames have no room for controls or a title. */
    if (wd >= WM_TITLE_LEFT + 14 && ht >= TITLEBAR_H) {
        wm_draw_controls(w, chrome);
        /* App toolbar if the app has one, else the title: UI 13 bold (spec §7). */
        rect_t bar = rect_make(wx + WM_TITLE_LEFT, wy, wd - WM_TITLE_LEFT - 14, TITLEBAR_H);
        if (transition || !app_toolbar_draw(w, bar)) {
            i32 title_y = wy + (TITLEBAR_H - gfx_role_line_h(FONT_ROLE_UI, 1)) / 2;
            gfx_str_role_fit(wx + WM_TITLE_LEFT, title_y, wd - WM_TITLE_LEFT - 14, w->title,
                             w->focused ? chrome->title : chrome->muted, FONT_ROLE_UI, 1, FONT_W_BOLD);
        }
    }

    /* App content, clipped to client area (skipped mid-transition). */
    if (!transition) {
    gfx_set_clip_base(wx + 1, wy + TITLEBAR_H, wd - 2, ht - TITLEBAR_H - 1);
    switch (w->app) {
    case APP_TERMINAL: app_terminal_draw(w); break;
    case APP_NOTES:    app_notes_draw(w);    break;
    case APP_FILES:    app_files_draw(w);    break;
    case APP_SYSMON:   app_sysmon_draw(w);   break;
    case APP_CALC:     app_calc_draw(w);     break;
    case APP_ABOUT:    app_about_draw(w);    break;
    case APP_SETTINGS: app_settings_draw(w); break;
    case APP_BROWSER:  app_browser_draw(w);  break;
    case APP_PKGMGR:   app_pkgmgr_draw(w);  break;
    case APP_EDITOR:   app_editor_draw(w);   break;
    case APP_PAINT:    app_paint_draw(w);    break;
    case APP_CLOCK:    app_clock_draw(w);    break;
    case APP_NETMON:   app_netmon_draw(w);   break;
    case APP_USERS:    app_users_draw(w);    break;
    case APP_MAZE:     app_maze_draw(w);     break;
    case APP_3D:       app_3d_draw(w);       break;
    case APP_DOOM:     app_doom_draw(w);     break;
    case APP_HELP:     app_help_draw(w);     break;
    default: break;
    }
    gfx_clear_clip_base();

    if (w->showing_snap_layouts) wm_draw_snap_layouts(w);
    }

    gfx_rect_rounded_outline(wx, wy, wd, ht, CDL_R_WINDOW, chrome->border);
    wm_corners(wx, wy, wd, ht, true);
    if (wc == WC_DRAW_AND_CAPTURE) wm_cache_capture(w, wx, wy, wd, ht);

    /* Fade: overlay the background colour at (255-opacity) inside the rounded
     * window shape so open/restore read as a fade-in and close/minimise as a
     * fade-out. Opacity is 255 for settled windows, so this is a no-op then. */
    if (w->opacity < 255)
        gfx_rect_rounded_blend(wx, wy, wd, ht, CDL_R_WINDOW, COL_BG, 255 - w->opacity);
}

/* Sort windows by z_order before drawing so layering is always correct.
   Multi-desktop: only show windows on current desktop (or desktop==0 shows all). */
void wm_draw_all(void) {
    int order[MAX_WINDOWS];
    int cnt = 0;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!windows[i].active || windows[i].minimized) continue;
        /* Show window if it belongs to current desktop OR is "sticky" (desktop==0xFF) */
        if (windows[i].desktop != g_current_desktop && windows[i].desktop != 0xFF) continue;
        order[cnt++] = i;
    }
    for (int a = 0; a < cnt-1; a++)
        for (int b = a+1; b < cnt; b++)
            if (windows[order[a]].z_order > windows[order[b]].z_order) {
                int tmp = order[a]; order[a] = order[b]; order[b] = tmp;
            }
    s_wm_hover_top = NULL;
    for (int i = cnt - 1; i >= 0; i--) {
        const window_t *w = &windows[order[i]];
        if (s_wm_mouse_x >= w->rect.x - 16 && s_wm_mouse_x < w->rect.x + w->rect.w + 16 &&
            s_wm_mouse_y >= w->rect.y - 16 && s_wm_mouse_y < w->rect.y + w->rect.h + 16) {
            s_wm_hover_top = w; break;
        }
    }
    for (int i = 0; i < cnt; i++)
        draw_window(&windows[order[i]]);
    notify_draw();
}

/* -- Mouse handling ------------------------------------------------------- */

void wm_handle_mouse(mouse_t *m) {
    s_wm_mouse_x = m->x; s_wm_mouse_y = m->y;
    /* launcher input is now pre-routed in gui.c */

    /* -- 0. Pointer feedback: pick the cursor shape and the hovered edge for
     *       the highlight, up front so it is right even in the branches below
     *       that return early. -------------------------------------------- */
    for (int i = 0; i < MAX_WINDOWS; i++) windows[i].hover_edge = RESIZE_NONE;
    g_cursor_shape = CURSOR_ARROW;
    {
        window_t *busy = NULL;
        for (int i = 0; i < MAX_WINDOWS; i++)
            if (windows[i].active && (windows[i].resizing || windows[i].dragging)) { busy = &windows[i]; break; }
        if (busy && busy->resizing) {
            g_cursor_shape = edge_to_cursor(busy->resize_edge);
            busy->hover_edge = (u8)busy->resize_edge;
        } else if (busy && busy->dragging) {
            g_cursor_shape = CURSOR_MOVE;
        } else {
            window_t *h = NULL; u32 hz = 0;
            for (int i = 0; i < MAX_WINDOWS; i++) {
                window_t *w = &windows[i];
                if (!w->active || w->minimized || !wm_on_current_desktop(w)) continue;
                if (rect_contains(w->rect, m->x, m->y) && w->z_order >= hz) { h = w; hz = w->z_order; }
            }
            if (h && !h->maximized) {
                u32 e = detect_resize_edge(h, m->x, m->y);
                if (e != RESIZE_NONE) { h->hover_edge = (u8)e; g_cursor_shape = edge_to_cursor(e); }
            }
        }
    }

    /* -- 1. Ongoing window resize ----------------------------------------- */
    for (int i = 0; i < MAX_WINDOWS; i++) {
        window_t *w = &windows[i];
        if (!w->active || !w->resizing) continue;
        if (m->left) {
            i32 dx = m->x - w->drag_ox;
            i32 dy = m->y - w->drag_oy;
            rect_t r = w->restore_rect; /* original rect at resize start */

            switch (w->resize_edge) {
            case RESIZE_RIGHT:
                r.w += dx;
                break;
            case RESIZE_BOTTOM:
                r.h += dy;
                break;
            case RESIZE_LEFT:
                r.x += dx; r.w -= dx;
                break;
            case RESIZE_TOP:
                r.y += dy; r.h -= dy;
                break;
            case RESIZE_BR:
                r.w += dx; r.h += dy;
                break;
            case RESIZE_BL:
                r.x += dx; r.w -= dx; r.h += dy;
                break;
            case RESIZE_TR:
                r.w += dx; r.y += dy; r.h -= dy;
                break;
            case RESIZE_TL:
                r.x += dx; r.w -= dx; r.y += dy; r.h -= dy;
                break;
            }
            /* Enforce minimum size */
            i32 mnw, mnh; app_min_size(w->app, &mnw, &mnh);
            if (r.w < mnw) { r.w = mnw; if (w->resize_edge == RESIZE_LEFT || w->resize_edge == RESIZE_TL || w->resize_edge == RESIZE_BL) r.x = w->restore_rect.x + w->restore_rect.w - mnw; }
            if (r.h < mnh) { r.h = mnh; if (w->resize_edge == RESIZE_TOP || w->resize_edge == RESIZE_TL || w->resize_edge == RESIZE_TR) r.y = w->restore_rect.y + w->restore_rect.h - mnh; }
            w->rect = r;
        } else {
            w->resizing = false;
        }
        return;
    }

    /* -- 2. Ongoing window drag ------------------------------------------- */
    for (int i = 0; i < MAX_WINDOWS; i++) {
        window_t *w = &windows[i];
        if (!w->active || !w->dragging) continue;
        if (m->left) {
            i32 nx = m->x - w->drag_ox;
            i32 ny = m->y - w->drag_oy;
            
            /* 8px Magnetic snap zone (resistance) */
            #define SNAP_ZONE 8
            if (nx < SNAP_ZONE && nx > -SNAP_ZONE) nx = 0;
            if (ny < SNAP_ZONE && ny > -SNAP_ZONE) ny = 0;
            if (nx + w->rect.w > (i32)SCREEN_W - SNAP_ZONE && nx + w->rect.w < (i32)SCREEN_W + SNAP_ZONE) 
                nx = (i32)SCREEN_W - w->rect.w;
            if (ny + w->rect.h > (i32)SCREEN_H - (i32)TASKBAR_H - SNAP_ZONE && ny + w->rect.h < (i32)SCREEN_H - (i32)TASKBAR_H + SNAP_ZONE)
                ny = (i32)SCREEN_H - (i32)TASKBAR_H - w->rect.h;

            w->rect.x = nx;
            w->rect.y = ny;

            if (w->rect.y < TOPBAR_H) w->rect.y = TOPBAR_H;
            if (w->rect.y > (i32)SCREEN_H - (i32)TASKBAR_H - TITLEBAR_H)
                w->rect.y = (i32)SCREEN_H - (i32)TASKBAR_H - TITLEBAR_H;

            /* Snap-to-maximize preview when the cursor hits the very top. Mirror
             * the maximised bounds: below the top bar, above the dock. */
            if (m->y < 4) {
                i32 pt = (i32)TOPBAR_H;
                i32 ph = (i32)SCREEN_H - pt - (i32)TASKBAR_H;
                gfx_rect_blend(0, pt, (i32)SCREEN_W, ph, COL_PRIMARY, 40);
                gfx_rect_rounded_outline(8, pt + 8, (i32)SCREEN_W - 16, ph - 16, CDL_R_WINDOW, COL_PRIMARY);
            }
        } else {
            /* If released at the top: Maximize */
            if (m->y < 4) wm_maximize(w);
            w->dragging = false;
        }
        return;
    }

    /* -- 3. Find topmost window under cursor ------------------------------ */
    window_t *hit = NULL; u32 hz = 0;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        window_t *w = &windows[i];
        if (!w->active || w->minimized || !wm_on_current_desktop(w)) continue;
        if (rect_contains(w->rect, m->x, m->y) && w->z_order >= hz) {
            hit = w; hz = w->z_order;
        }
    }

    for (int i = 0; i < MAX_WINDOWS; i++) {
        window_t *w = &windows[i];
        if (w != hit) { w->hover_start_tick = 0; w->showing_snap_layouts = false; }
    }
    if (hit) {
        /* Keep the open flyout alive while crossing the short gap from the
         * green light, and while choosing a layout inside the flyout. */
        rect_t sr = wm_snap_rect(hit);
        rect_t bridge = rect_make(sr.x, hit->rect.y + TITLEBAR_H / 2,
                                  sr.w, TITLEBAR_H / 2 + 4);
        i32 control = wm_control_hit(hit->rect.x, hit->rect.y, TITLEBAR_H, m->x, m->y);
        if (control == WM_CONTROL_MAX) {
            if (hit->hover_start_tick == 0) hit->hover_start_tick = timer_ms();
            if (timer_ms() - hit->hover_start_tick > 500) hit->showing_snap_layouts = true;
        } else if (!(hit->showing_snap_layouts && control == WM_CONTROL_NONE &&
                     (rect_contains(sr, m->x, m->y) || rect_contains(bridge, m->x, m->y)))) {
            hit->hover_start_tick = 0;
            hit->showing_snap_layouts = false;
        }
    }

    /* -- 3b. Scroll wheel -> the window under the cursor. Handled here, BEFORE
     *        the click gate below, so a bare wheel spin (no click) works. Most
     *        apps read w->scroll as a line offset and clamp it in their own
     *        draw; the browser has a pixel scroller. Wheel up decreases the
     *        offset (moves toward the top). ------------------------------- */
    if (m->scroll_delta != 0 && hit && !hit->minimized) {
        rect_t scr = wm_client_rect(hit);
        if (rect_contains(scr, m->x, m->y)) {
            if (hit->app == APP_BROWSER) {
                app_browser_scroll(hit, m->scroll_delta);
            } else if (hit->app == APP_TERMINAL) {
                app_terminal_scroll(hit, m->scroll_delta * 3);   /* 3 lines / notch */
            } else {
                i32 ns = (i32)hit->scroll - m->scroll_delta * 3;   /* 3 lines / notch */
                hit->scroll = (ns > 0) ? (u32)ns : 0;
            }
        }
    }

    /* -- 4. Continuous left-hold for paint-style apps --------------------- */
    if (m->left && !m->left_clicked && hit && hit->focused) {
        rect_t cr = wm_client_rect(hit);
        if (rect_contains(cr, m->x, m->y)) {
            if (hit->app == APP_PAINT) app_paint_click(hit, m->x, m->y, m);
        }
        return;
    }

    /* -- Right-click on titlebar: close window. Must run before the left-click
     *    gate below, which returned early and made this unreachable. ------- */
    if (m->right_clicked && hit &&
        m->y >= hit->rect.y && m->y < hit->rect.y + TITLEBAR_H) {
        wm_close(hit);
        return;
    }

    /* Only discrete clicks from here */
    if (!m->left_clicked) return;
    if (!hit) return;

    /* Focus the clicked window */
    wm_focus(hit);

    /* Click in Snap Layout Flyout */
    if (hit->showing_snap_layouts) {
        rect_t sr = wm_snap_rect(hit);
        i32 sx = sr.x, sy = sr.y;
        if (rect_contains(sr, m->x, m->y)) {
            /* Check which grid item was clicked */
            for (int i=0; i<6; i++) {
                i32 lx = sx + 8 + (i%3) * 56;
                i32 ly = sy + 8 + (i/3) * 54;
                if (rect_contains(rect_make(lx, ly, 48, 44), m->x, m->y)) {
                    static const int modes[] = {SNAP_LEFT, SNAP_FULL, SNAP_RIGHT,
                                                 SNAP_TL, SNAP_TOP, SNAP_TR};
                    wm_snap_focused(modes[i]);
                    hit->showing_snap_layouts = false;
                    return;
                }
            }
            return; /* Padding belongs to the flyout, not the app underneath. */
        }
    }

    /* -- 5. Titlebar buttons (same geometry as painting and hover) ---------- */
    {
        i32 control = wm_control_hit(hit->rect.x, hit->rect.y, TITLEBAR_H, m->x, m->y);
        if (control != WM_CONTROL_NONE) {
            hit->showing_snap_layouts = false;
            hit->hover_start_tick = 0;
            if (control == WM_CONTROL_CLOSE) wm_close(hit);
            if (control == WM_CONTROL_MIN) wm_minimize(hit);
            if (control == WM_CONTROL_MAX) wm_maximize(hit);
            return;
        }
    }

    /* -- 6. Edge/corner resize -------------------------------------------- */
    if (!hit->maximized) {
        u32 edge = detect_resize_edge(hit, m->x, m->y);
        if (edge != RESIZE_NONE) {
            hit->resizing     = true;
            hit->resize_edge  = edge;
            hit->drag_ox      = m->x;
            hit->drag_oy      = m->y;
            hit->restore_rect = hit->rect;
            return;
        }
    }

    /* -- 7. Title-bar toolbar, then titlebar drag --------------------------- */
    if (m->y >= hit->rect.y && m->y < hit->rect.y + TITLEBAR_H &&
        app_toolbar_click(hit, m->x, m->y))
        return;
    if (m->y >= hit->rect.y && m->y < hit->rect.y + TITLEBAR_H && !hit->maximized) {
        hit->dragging = true;
        hit->drag_ox  = m->x - hit->rect.x;
        hit->drag_oy  = m->y - hit->rect.y;
        return;
    }

    /* -- 8. Client-area click — pass absolute screen coords to all handlers */
    rect_t cr = wm_client_rect(hit);
    if (rect_contains(cr, m->x, m->y)) {
        switch (hit->app) {
        case APP_TERMINAL: app_terminal_click(hit, m->x, m->y);    break;
        case APP_FILES:    app_files_click(hit, m->x, m->y, m);    break;
        case APP_CALC:     app_calc_click(hit, m->x, m->y);        break;
        case APP_SYSMON:   app_sysmon_click(hit, m->x, m->y);      break;
        case APP_SETTINGS: app_settings_click(hit, m->x, m->y, m); break;
        case APP_BROWSER:  app_browser_click(hit, m->x, m->y);     break;
        case APP_PKGMGR:   app_pkgmgr_click(hit, m->x, m->y);     break;
        case APP_EDITOR:   app_editor_click(hit, m->x, m->y, m);   break;
        case APP_PAINT:    app_paint_click(hit, m->x, m->y, m);    break;
        case APP_USERS:    app_users_click(hit, m->x, m->y);       break;
        case APP_NOTES:    app_notes_click(hit, m->x, m->y);       break;
        default: break;
        }
    }
}

/* -- Keyboard routing: ONLY to focused window ----------------------------- */
void wm_handle_key(char c, window_t *w) {
    if (!w) return;
    switch (w->app) {
    case APP_TERMINAL: app_terminal_key(w, c); break;
    case APP_NOTES:    app_notes_key(w, c);    break;
    case APP_EDITOR:   app_editor_key(w, c);   break;
    case APP_SETTINGS: app_settings_key(w, c); break;
    case APP_BROWSER:  app_browser_key(w, c);  break;
    case APP_PKGMGR:   app_pkgmgr_key(w, c);  break;
    case APP_USERS:    app_users_key(w, c);    break;
    case APP_ABOUT:    app_help_key(w, c);     break;
    case APP_PAINT:    app_paint_key(w, c);    break;
    case APP_CALC:     app_calc_key(w, c);     break;
    case APP_FILES:    app_files_key(w, c);    break;
    case APP_MAZE:     app_maze_key(w, c);     break;
    case APP_3D:       app_3d_key(w, c);       break;
    case APP_DOOM:     app_doom_key(w, c);     break;
    default: break;
    }
}

/* -- Desktop -------------------------------------------------------------- */
static void draw_wallpaper(u32 wallpaper, i32 h) {
    i32 sw = (i32)SCREEN_W;
    i32 sh = (i32)SCREEN_H;
    switch (wallpaper) {
    case 1: /* Midnight Ridge */
        gfx_gradient_rect(0, 0, sw, h, rgb(0x0f,0x17,0x2a), rgb(0x1e,0x29,0x3b));
        gfx_line(0, h * 3/4, sw, h * 4/5, rgb(0x33,0x41,0x55));
        gfx_line(0, h * 3/4 + 2, sw, h * 4/5 + 2, rgb(0x0f,0x17,0x2a));
        break;
    case 2: /* Forest Fog */
        gfx_gradient_rect(0, 0, sw, h, rgb(0x06,0x4e,0x3b), rgb(0x02,0x2c,0x22));
        for (i32 i = 0; i < sw; i += 120)
            gfx_rect_blend(i, h/2, 60, h/2, rgb(0x05,0x96,0x69), 10);
        break;
    case 3: /* Dark Carbon */
        gfx_gradient_rect(0, 0, sw, h, rgb(0x11,0x11,0x11), rgb(0x1a,0x1a,0x1a));
        for (i32 y = 0; y < h; y += 4)
            gfx_hline(0, y, sw, rgb(0x0a,0x0a,0x0a));
        break;
    case 4: /* Arctic Slate */
        gfx_gradient_rect(0, 0, sw, h, rgb(0x1e,0x29,0x3b), rgb(0x0f,0x17,0x2a));
        gfx_rect_blend(sw/2, 0, sw/2, h, rgb(0xff,0xff,0xff), 5);
        break;
    case 5: /* Crimson Night */
        gfx_gradient_rect(0, 0, sw, h, rgb(0x45,0x0a,0x0a), rgb(0x11,0x11,0x11));
        gfx_rect_blend(0, h/2, sw, 2, rgb(0x7f,0x1d,0x1d), 20);
        break;
    default: /* System Default — Azure Depth */
        gfx_gradient_rect(0, 0, sw, h, rgb(0x0d, 0x1a, 0x3c), rgb(0x14, 0x20, 0x44));
        /* Large decorative arc overlay */
        gfx_rect_blend(sw/6, 0, sw*3/4, h, rgb(0x1e, 0x3a, 0x8a), 14);
        gfx_rect_blend(sw/2, h/4, sw/2, h*3/4, rgb(0x3b, 0x6c, 0xd4), 8);
        break;
    }
}


void desktop_handle_mouse(mouse_t *m) {
    for (int i = 0; i < ICON_COUNT; i++) {
        rect_t r = sidebar_icon_rect(i);
        icons[i].hover = rect_contains(r, m->x, m->y);
        if (m->left_clicked && icons[i].hover) {
            launcher_open = false;
            i32 sw=(i32)SCREEN_W, sh=(i32)SCREEN_H;
            i32 ww, wh;
            app_default_size(icons[i].app, sw, sh, &ww, &wh);
            wm_open(icons[i].app, icons[i].app == APP_FILES ? "Files" : "Settings",
                    (sw - ww) / 2, (sh - wh) / 2 - 10, ww, wh);
        }
    }
}

void wm_minimize_all(void) {
    for (int i = 0; i < MAX_WINDOWS; i++)
        if (windows[i].active && !windows[i].minimized && wm_on_current_desktop(&windows[i])) {
            windows[i].minimized = true;
            windows[i].focused   = false;
        }
}

/* -- App Launcher overlay (Now in launcher.c) ----------------------------- */
/* Legacy code removed to resolve conflicts with new launcher.c */

/* Switch workspace: focus the topmost visible window there, if any. */
void wm_switch_desktop(u32 d) {
    if (d > 3) return;
    g_current_desktop = d;
    window_t *top = NULL; u32 bz = 0;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        window_t *w = &windows[i];
        w->focused = false;
        if (w->active && !w->minimized && wm_on_current_desktop(w) && w->z_order >= bz) { top = w; bz = w->z_order; }
    }
    if (top) top->focused = true;
}

/* Instant part of the desktop layer's look: shortcut hover and whether the
 * hero shows (a maximized window hides it). */
u32 desktop_layer_key(void) {
    u32 k = hero_visible() ? 1u : 0u;
    for (int i = 0; i < ICON_COUNT; i++) if (icons[i].hover) k |= 2u << i;
    return k;
}

/* Pointer routing (gui.c): the window manager owns the pointer while a window
 * is being dragged or resized, while a snap-layout flyout is open, and over
 * any visible window -- so a click on a window never reaches the desktop
 * widgets, shortcuts or hero search drawn underneath it. */
bool wm_busy(void) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        const window_t *w = &windows[i];
        if (w->active && (w->dragging || w->resizing || w->showing_snap_layouts)) return true;
    }
    return false;
}

bool wm_pointer_owner(i32 x, i32 y) {
    if (wm_busy()) return true;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        const window_t *w = &windows[i];
        if (w->active && !w->minimized && wm_on_current_desktop(w) && rect_contains(w->rect, x, y))
            return true;
    }
    return false;
}

void desktop_clear_hover(void) {
    for (int i = 0; i < ICON_COUNT; i++) icons[i].hover = false;
}
