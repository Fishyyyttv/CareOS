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
 *       * close/min/max button hitboxes: 16x16 circles, reliable
 *       * app click coords passed relative to client rect
 *       * NEW: edge/corner resizing with min size enforcement
 *   - wm_draw_all: z-sorted draw. Enhanced shadow on focused window.
 *   - Titlebar text: COL_TRANSPARENT bg (no black box).
 *   - Window cascade: new windows slightly offset so they don't perfectly stack.
 * ============================================================================= */
#include "kernel.h"
#include "gui.h"

static window_t windows[MAX_WINDOWS];
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

/* -- Idle tracking (for screensaver / auto-lock) -------------------------- */
u32  g_last_activity_tick = 0;

/* -- Desktop icon table --------------------------------------------------- */
static desktop_icon_t icons[] = {
    {"Terminal", APP_TERMINAL, 0,0,false,false, 0x4ade80},
    {"Packages", APP_PKGMGR,   0,0,false,false, 0x06b6d4},
    {"Notes",    APP_NOTES,    0,0,false,false, 0xfbbf24},
    {"Editor",   APP_EDITOR,   0,0,false,false, 0x4ade80},
    {"Files",    APP_FILES,    0,0,false,false, 0x4a6fff},
    {"Paint",    APP_PAINT,    0,0,false,false, 0xf87171},
    {"Monitor",  APP_SYSMON,   0,0,false,false, 0x22d3ee},
    {"Clock",    APP_CLOCK,    0,0,false,false, 0xfbbf24},
    {"Calc",     APP_CALC,     0,0,false,false, 0xfb923c},
    {"NetMon",   APP_NETMON,   0,0,false,false, 0x22d3ee},
    {"Browser",  APP_BROWSER,  0,0,false,false, 0x7c3aed},
    {"Users",    APP_USERS,    0,0,false,false, 0xfb923c},
    {"Settings", APP_SETTINGS, 0,0,false,false, 0xa78bfa},
    {"About",    APP_ABOUT,    0,0,false,false, 0x6b7280},
};
#define ICON_COUNT   14
static i32 ICON_W      = 72;
static i32 ICON_H      = 80;
static i32 ICON_COL_W  = 96;   /* full column width = ICON_W + gap between cols */
#define    ICON_MARGIN  20
#define SIDEBAR_PANEL_X 2
#define SIDEBAR_ROW_X   12
#define SIDEBAR_DIVIDER_GAP 14

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

/* -- Icon layout (Sidebar style) ------------------------------------------ */
static i32 sidebar_row_h(void) {
    return ((i32)SCREEN_H < 720) ? 36 : 44;
}

static i32 sidebar_row_gap(void) {
    return ((i32)SCREEN_H < 720) ? 3 : 5;
}

static i32 sidebar_icon_size(void) {
    return ((i32)SCREEN_H < 720) ? 24 : 32;
}

static bool sidebar_break_after(int idx) {
    return idx == 3 || idx == 5 || idx == 7 || idx == 9 || idx == 11;
}

static rect_t sidebar_icon_rect(int idx) {
    return rect_make(SIDEBAR_PANEL_X + 2, icons[idx].y, SIDEBAR_W - (SIDEBAR_PANEL_X + 2) * 2, sidebar_row_h());
}

static void layout_icons(void) {
    i32 x = SIDEBAR_ROW_X;
    i32 y = TOPBAR_H + 18;
    i32 row_h = sidebar_row_h();
    i32 gap = sidebar_row_gap();
    for (int i = 0; i < ICON_COUNT; i++) {
        icons[i].x = x;
        icons[i].y = y;
        y += row_h + gap;
        if (sidebar_break_after(i)) y += SIDEBAR_DIVIDER_GAP;
    }
}

/* -- Icon graphics (Sidebar style) ---------------------------------------- */
static void draw_desktop_icon(const desktop_icon_t *ic) {
    i32 idx = (i32)(ic - icons);
    rect_t r = sidebar_icon_rect(idx);
    i32 sc = (i32)GFX_FONT_SCALE;
    i32 icon_sz = sidebar_icon_size();

    /* Hover / active highlight */
    if (ic->hover) {
        gfx_rect_rounded(r.x + 2, r.y + 1, r.w - 4, r.h - 2, 8, COL_HOVER);
        gfx_rect_rounded_outline(r.x + 2, r.y + 1, r.w - 4, r.h - 2, 8, COL_BORDER);
    } else if (ic->selected) {
        gfx_rect_rounded(r.x + 2, r.y + 1, r.w - 4, r.h - 2, 8, COL_SELECTION);
    }

    /* Icon — left-aligned with consistent padding */
    i32 icon_x = r.x + 10;
    i32 icon_y = r.y + (r.h - icon_sz) / 2;
    gfx_draw_icon(ic->app, icon_x, icon_y, icon_sz, ic->icon_color);

    /* Label — vertically centred next to icon, full name visible */
    i32 tx = icon_x + icon_sz + 10;
    i32 ty = r.y + (r.h - (i32)FONT_H * sc) / 2;
    i32 max_lw = r.x + r.w - tx - 6;
    gfx_str_clipped(tx, ty, max_lw, ic->label, ic->hover ? COL_TEXT : COL_DIM, COL_TRANSPARENT);
}

void desktop_draw(void) {
    i32 panel_y = TOPBAR_H + 4;
    i32 panel_h = (i32)SCREEN_H - panel_y - 8;
    i32 panel_x = SIDEBAR_PANEL_X;
    i32 panel_w = SIDEBAR_W - SIDEBAR_PANEL_X * 2;

    /* Sidebar glass panel */
    gfx_rect_rounded(panel_x, panel_y, panel_w, panel_h, 12, g_theme->taskbar);
    gfx_rect_blend(panel_x, panel_y, panel_w, panel_h, COL_GLASS_TINT,
                   g_theme->is_dark ? 16 : 55);
    gfx_rect_rounded_outline(panel_x, panel_y, panel_w, panel_h, 12, COL_BORDER);

    /* App rows */
    for (int i = 0; i < ICON_COUNT; i++) {
        draw_desktop_icon(&icons[i]);
        if (sidebar_break_after(i)) {
            rect_t r = sidebar_icon_rect(i);
            i32 dy = r.y + r.h + sidebar_row_gap() / 2 + SIDEBAR_DIVIDER_GAP / 2;
            gfx_hline(panel_x + 14, dy, panel_w - 28, COL_BORDER);
        }
    }
}


/* -- WM init -------------------------------------------------------------- */
void wm_init(void) {
    kmemset(windows, 0, sizeof(windows));
    open_count = 0;
    wm_next_z  = 1;
    launcher_open = false;
    ICON_W     = SIDEBAR_W - SIDEBAR_ROW_X * 2;
    ICON_H     = sidebar_row_h();
    ICON_COL_W = SIDEBAR_W;
    layout_icons();
    rtc_read(&tb_time);
}

/* -- Window size helpers -------------------------------------------------- */
void app_default_size(app_id_t app, i32 sw, i32 sh, i32 *w, i32 *h) {
    switch (app) {
    case APP_CALC:     *w = 320; *h = 440; return;
    case APP_CLOCK:    *w = 280; *h = 240; return;
    case APP_ABOUT:    *w = 460; *h = 340; return;
    case APP_PAINT:    *w = sw*75/100; *h = sh*75/100; return;
    default:           *w = sw*65/100; *h = sh*70/100; return;
    }
}

/* -- Open window -- SINGLE init path -------------------------------------- */
window_t *wm_open(app_id_t app, const char *title,
                  i32 x, i32 y, i32 w, i32 h) {

    /* If app already open -- restore + focus, no re-init */
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (windows[i].active && windows[i].app == app && app != APP_NONE) {
            windows[i].minimized = false;
            wm_focus(&windows[i]);
            serial_write("  [wm_open] restored existing window\n");
            return &windows[i];
        }
    }

    /* Find free slot */
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (windows[i].active) continue;

        window_t *win = &windows[i];
        kmemset(win, 0, sizeof(window_t));
        win->active   = true;
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
        win->animating    = false;

        
        /* Initialize structural UI root */
        rect_t cr = wm_client_rect(win);
        win->root = widget_create(WIDGET_PANEL, 0, 0, cr.w, cr.h);
        win->root->bg_color = COL_SURFACE;
        
        /* Allocate offscreen buffer */
        win->win_buffer.w = (u32)w;
        win->win_buffer.h = (u32)h;
        win->win_buffer.pitch = (u32)w * 4;
        win->win_buffer.pixels = (u32 *)kmalloc((size_t)w * (size_t)h * 4);
        if (win->win_buffer.pixels) {
            kmemset(win->win_buffer.pixels, 0, (size_t)w * (size_t)h * 4);
        }

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
    /* Clear app state before freeing slot */
    w->active  = false;
    w->focused = false;
    /* Decrement open_count for accurate cascade */
    if (open_count > 0) open_count--;
    /* If we closed the focused window, focus the next visible one */
    bool any_focused = false;
    for (int i = 0; i < MAX_WINDOWS; i++)
        if (windows[i].active && windows[i].focused) { any_focused = true; break; }
    if (!any_focused) {
        /* Find the topmost active window */
        window_t *top = NULL; u32 bz = 0;
        for (int i = 0; i < MAX_WINDOWS; i++)
            if (windows[i].active && windows[i].z_order >= bz) {
                top = &windows[i]; bz = windows[i].z_order;
            }
        if (top) top->focused = true;
    }
}

void wm_focus(window_t *w) {
    if (!w) return;
    for (int i = 0; i < MAX_WINDOWS; i++) windows[i].focused = false;
    w->focused = true;
    w->z_order = wm_next_z++;
}

void wm_minimize(window_t *w) {
    if (!w) return;
    w->minimized = !w->minimized;
    /* If minimizing the focused window, focus next */
    if (w->minimized && w->focused) {
        w->focused = false;
        window_t *top = NULL; u32 bz = 0;
        for (int i = 0; i < MAX_WINDOWS; i++) {
            if (windows[i].active && !windows[i].minimized && windows[i].z_order >= bz) {
                top = &windows[i]; bz = windows[i].z_order;
            }
        }
        if (top) top->focused = true;
    }
}

void wm_maximize(window_t *w) {
    if (!w) return;
    if (w->maximized) {
        w->rect      = w->restore_rect;
        w->maximized = false;
    } else {
        w->restore_rect = w->rect;
        w->rect = rect_make(0, 0, (i32)SCREEN_W, (i32)SCREEN_H - (i32)TASKBAR_H);
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
        if (windows[i].active && windows[i].focused && windows[i].z_order >= bz) {
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
        if (windows[i].active && !windows[i].minimized)
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

    i32 sw = (i32)SCREEN_W;
    i32 sh = (i32)SCREEN_H - (i32)TASKBAR_H;

    w->maximized = false;
    w->animating = true;
    w->anim_start_tick = timer_get_ticks();
    w->restore_rect = w->rect;

    switch (mode) {
    case SNAP_LEFT:
        w->target_rect = rect_make(0, 0, sw/2, sh); break;
    case SNAP_RIGHT:
        w->target_rect = rect_make(sw/2, 0, sw-sw/2, sh); break;
    case SNAP_TOP:
        w->target_rect = rect_make(0, 0, sw, sh/2); break;
    case SNAP_BOTTOM:
        w->target_rect = rect_make(0, sh/2, sw, sh-sh/2); break;
    case SNAP_TL:
        w->target_rect = rect_make(0, 0, sw/2, sh/2); break;
    case SNAP_TR:
        w->target_rect = rect_make(sw/2, 0, sw-sw/2, sh/2); break;
    case SNAP_BL:
        w->target_rect = rect_make(0, sh/2, sw/2, sh-sh/2); break;
    case SNAP_BR:
        w->target_rect = rect_make(sw/2, sh/2, sw-sw/2, sh-sh/2); break;
    }
}

/* -- Detect resize edge under cursor (10px border zone = actually grabbable) */
#define EDGE_ZONE 10
static u32 detect_resize_edge(window_t *w, i32 mx, i32 my) {
    rect_t r = w->rect;
    /* Only detect on border, not inside titlebar */
    if (my >= r.y + TITLEBAR_H - 4 || my < r.y) {
        /* corners and edges */
    }
    bool l  = (mx >= r.x          && mx < r.x + EDGE_ZONE);
    bool rr = (mx >= r.x + r.w - EDGE_ZONE && mx < r.x + r.w);
    bool t  = (my >= r.y          && my < r.y + EDGE_ZONE);
    bool b  = (my >= r.y + r.h - EDGE_ZONE && my < r.y + r.h);

    if (t && l)  return RESIZE_TL;
    if (t && rr) return RESIZE_TR;
    if (b && l)  return RESIZE_BL;
    if (b && rr) return RESIZE_BR;
    if (l)       return RESIZE_LEFT;
    if (rr)      return RESIZE_RIGHT;
    if (t)       return RESIZE_TOP;
    if (b)       return RESIZE_BOTTOM;
    return RESIZE_NONE;
}

bool wm_animate_all(void) {
    bool any = false;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        window_t *w = &windows[i];
        if (!w->active || !w->animating) continue;   // <-- add !w->animating

        i32 dx = w->target_rect.x - w->rect.x;
        i32 dy = w->target_rect.y - w->rect.y;
        i32 dw = w->target_rect.w - w->rect.w;
        i32 dh = w->target_rect.h - w->rect.h;

        if (dx != 0 || dy != 0 || dw != 0 || dh != 0) {
            w->rect.x += dx / 4;
            w->rect.y += dy / 4;
            w->rect.w += dw / 4;
            w->rect.h += dh / 4;

            if (kabs(dx) < 2) w->rect.x = w->target_rect.x;
            if (kabs(dy) < 2) w->rect.y = w->target_rect.y;
            if (kabs(dw) < 2) w->rect.w = w->target_rect.w;
            if (kabs(dh) < 2) w->rect.h = w->target_rect.h;

            any = true;
        } else {
            w->animating = false;   // stop once we’ve reached the target
        }
    }
    return any;
}


void wm_draw_snap_layouts(window_t *w) {
    if (!w->showing_snap_layouts) return;
    i32 wd = 180, ht = 120;
    i32 x = w->rect.x + w->rect.w - wd - 4;
    i32 y = w->rect.y + TITLEBAR_H + 4;

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

/* Titlebar button centers -- used identically in draw_window AND wm_handle_mouse */
#define TB_BTN_R      8
#define TB_BTN_CY(wy) ((wy) + TITLEBAR_H / 2)
#define TB_BTN_MIN_X(wx, ww)    ((wx) + (ww) - 74)
#define TB_BTN_MAX_X(wx, ww)    ((wx) + (ww) - 48)
#define TB_BTN_CLOSE_X(wx, ww)  ((wx) + (ww) - 22)

/* -- Draw one window (direct-to-screen, no intermediate buffer) ------------- */
static void draw_window(window_t *w) {
    if (!w->active || w->minimized) return;
    i32 wx = w->rect.x, wy = w->rect.y;
    i32 wd = w->rect.w, ht = w->rect.h;

    /* Shadow */
    if (w->focused)
        gfx_shadow_ext(wx - 2, wy - 2, wd + 4, ht + 4, 15);
    else
        gfx_shadow_ext(wx, wy, wd, ht, 8);

    /* Window body */
    gfx_rect_rounded(wx, wy, wd, ht, 10, COL_SURFACE);

    /* Titlebar */
    u32 tbar_col = w->focused ? COL_WINBAR : g_theme->surface2;
    gfx_rect_rounded(wx, wy, wd, TITLEBAR_H, 10, tbar_col);
    gfx_rect_blend(wx + 1, wy + 1, wd - 2, TITLEBAR_H - 2, COL_WHITE, w->focused ? 25 : 12);
    gfx_hline(wx, wy + TITLEBAR_H - 1, wd, COL_BORDER);
    gfx_rect_rounded_outline(wx, wy, wd, ht, 10, w->focused ? COL_BORDER : g_theme->surface2);

    /* App title on left, traffic-light controls on right. */
    i32 btn_cy = TB_BTN_CY(wy);
    gfx_circle_fill(TB_BTN_MIN_X(wx, wd),   btn_cy, TB_BTN_R, rgb(0xff, 0xbd, 0x2e));
    gfx_circle_fill(TB_BTN_MAX_X(wx, wd),   btn_cy, TB_BTN_R, rgb(0x28, 0xc9, 0x40));
    gfx_circle_fill(TB_BTN_CLOSE_X(wx, wd), btn_cy, TB_BTN_R, rgb(0xff, 0x5f, 0x57));

    u32 tc = w->focused ? COL_TEXT : g_theme->muted;
    gfx_draw_icon(w->app, wx + 14, wy + (TITLEBAR_H - 26) / 2, 26, w->focused ? COL_ACCENT : g_theme->muted);
    /* Title — draw with a slightly larger size for legibility */
    i32 title_y = wy + (TITLEBAR_H - (i32)FONT_H) / 2;
    gfx_str_clipped(wx + 50, title_y, wd - 155, w->title, tc, COL_TRANSPARENT);

    /* App content, clipped to client area */
    gfx_set_clip(wx + 1, wy + TITLEBAR_H, wd - 2, ht - TITLEBAR_H - 1);
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
    case APP_HELP:     app_help_draw(w);     break;
    default: break;
    }
    gfx_clear_clip();

    if (w->showing_snap_layouts) wm_draw_snap_layouts(w);
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
    for (int i = 0; i < cnt; i++)
        draw_window(&windows[order[i]]);
    notify_draw();
}

/* -- Mouse handling ------------------------------------------------------- */

/* Helper: is cursor within +/-8px of a titlebar button circle? */
static bool hit_btn(i32 mx, i32 my, i32 bx, i32 bmy) {
    i32 dx = mx - bx, dy = my - bmy;
    return (dx*dx + dy*dy) <= 196;  /* 14px radius */
}

void wm_handle_mouse(mouse_t *m) {
    /* launcher input is now pre-routed in gui.c */

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
            if (r.w < MIN_WIN_W) { r.w = MIN_WIN_W; if (w->resize_edge == RESIZE_LEFT || w->resize_edge == RESIZE_TL || w->resize_edge == RESIZE_BL) r.x = w->restore_rect.x + w->restore_rect.w - MIN_WIN_W; }
            if (r.h < MIN_WIN_H) { r.h = MIN_WIN_H; if (w->resize_edge == RESIZE_TOP || w->resize_edge == RESIZE_TL || w->resize_edge == RESIZE_TR) r.y = w->restore_rect.y + w->restore_rect.h - MIN_WIN_H; }
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

            /* NEW: Snap preview if mouse is at the very top */
            if (m->y < 4) {
                gfx_rect_blend(0, 0, (i32)SCREEN_W, (i32)SCREEN_H - (i32)TASKBAR_H, COL_PRIMARY, 40);
                gfx_rect_rounded_outline(8, 8, (i32)SCREEN_W - 16, (i32)SCREEN_H - (i32)TASKBAR_H - 16, 12, COL_PRIMARY);
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
        if (!w->active || w->minimized) continue;
        if (rect_contains(w->rect, m->x, m->y) && w->z_order >= hz) {
            hit = w; hz = w->z_order;
        }
    }

    if (hit) {
        /* Hover detection for Snap Layouts on Maximize button */
        i32 bx_max = TB_BTN_MAX_X(hit->rect.x, hit->rect.w);
        i32 by     = TB_BTN_CY(hit->rect.y);
        if (hit_btn(m->x, m->y, bx_max, by)) {
            if (hit->hover_start_tick == 0) hit->hover_start_tick = timer_get_ticks();
            if (timer_get_ticks() - hit->hover_start_tick > 50) hit->showing_snap_layouts = true;
        } else {
            hit->hover_start_tick = 0;
            hit->showing_snap_layouts = false;
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

    /* Only discrete clicks from here */
    if (!m->left_clicked) return;
    if (!hit) return;

    /* Focus the clicked window */
    wm_focus(hit);

    /* Click in Snap Layout Flyout */
    if (hit->showing_snap_layouts) {
        i32 wd = 180, ht = 120;
        i32 sx = hit->rect.x + hit->rect.w - wd - 4;
        i32 sy = hit->rect.y + TITLEBAR_H + 4;
        if (rect_contains(rect_make(sx, sy, wd, ht), m->x, m->y)) {
            /* Check which grid item was clicked */
            for (int i=0; i<6; i++) {
                i32 lx = sx + 8 + (i%3) * 56;
                i32 ly = sy + 8 + (i/3) * 54;
                if (rect_contains(rect_make(lx, ly, 48, 44), m->x, m->y)) {
                    wm_snap_focused(i + 1); /* +1 because 0 is NONE */
                    hit->showing_snap_layouts = false;
                    return;
                }
            }
        }
    }

    /* -- 5. Titlebar buttons (use same macro positions as draw_window) ------ */
    {
        i32 btn_cy    = TB_BTN_CY(hit->rect.y);
        i32 btn_close = TB_BTN_CLOSE_X(hit->rect.x, hit->rect.w);
        i32 btn_max   = TB_BTN_MAX_X(hit->rect.x, hit->rect.w);
        i32 btn_min   = TB_BTN_MIN_X(hit->rect.x, hit->rect.w);

        if (hit_btn(m->x, m->y, btn_close, btn_cy)) { wm_close(hit);    return; }
        if (hit_btn(m->x, m->y, btn_max,   btn_cy)) { wm_maximize(hit); return; }
        if (hit_btn(m->x, m->y, btn_min,   btn_cy)) { wm_minimize(hit); return; }
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

    /* -- 7. Titlebar drag ------------------------------------------------- */
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
        case APP_FILES:    app_files_click(hit, m->x, m->y, m);    break;
        case APP_CALC:     app_calc_click(hit, m->x, m->y);        break;
        case APP_SYSMON:   app_sysmon_click(hit, m->x, m->y);      break;
        case APP_SETTINGS: app_settings_click(hit, m->x, m->y, m); break;
        case APP_BROWSER:  app_browser_click(hit, m->x, m->y);     break;
        case APP_PKGMGR:   app_pkgmgr_click(hit, m->x, m->y);     break;
        case APP_PAINT:    app_paint_click(hit, m->x, m->y, m);    break;
        case APP_USERS:    app_users_click(hit, m->x, m->y);       break;
        default: break;
        }
    }

    /* -- 9. Right-click on titlebar: close window -------------------------- */
    if (m->right_clicked && hit) {
        if (m->y >= hit->rect.y && m->y < hit->rect.y + TITLEBAR_H) {
            wm_close(hit);
            return;
        }
    }

    /* -- 10. Scroll wheel: route to focused window terminal/editor/browser -- */
    if (m->scroll_delta != 0) {
        window_t *fw = wm_focused();
        if (fw && !fw->minimized) {
            rect_t scr = wm_client_rect(fw);
            if (rect_contains(scr, m->x, m->y)) {
                if (fw->app == APP_BROWSER) {
                    app_browser_scroll(fw, m->scroll_delta);
                } else {
                    if (fw->scroll + m->scroll_delta >= 0)
                        fw->scroll = (u32)((i32)fw->scroll + m->scroll_delta);
                    else
                        fw->scroll = 0;
                }
            }
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
            /* Centre window in the area right of the sidebar */
            i32 avail_x = SIDEBAR_W + 8;
            i32 cx = avail_x + (sw - avail_x - ww) / 2;
            if (cx < avail_x) cx = avail_x;
            wm_open(icons[i].app, icons[i].label, cx, (sh - wh) / 2 - 10, ww, wh);
        }
    }
}

void wm_minimize_all(void) {
    for (int i = 0; i < MAX_WINDOWS; i++)
        if (windows[i].active && !windows[i].minimized) {
            windows[i].minimized = true;
            windows[i].focused   = false;
        }
}

/* -- App Launcher overlay (Now in launcher.c) ----------------------------- */
/* Legacy code removed to resolve conflicts with new launcher.c */
