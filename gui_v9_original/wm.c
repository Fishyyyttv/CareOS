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
static u32      wm_next_z    = 1;
static bool     launcher_open = false;
static u32      open_count    = 0;   /* for cascade offset */

/* -- Desktop icon table --------------------------------------------------- */
static desktop_icon_t icons[] = {
    {"Terminal", APP_TERMINAL, 0,0,false,false, 0x4ade80},
    {"Notes",    APP_NOTES,    0,0,false,false, 0xfbbf24},
    {"Files",    APP_FILES,    0,0,false,false, 0x4a6fff},
    {"Monitor",  APP_SYSMON,   0,0,false,false, 0x22d3ee},
    {"Calc",     APP_CALC,     0,0,false,false, 0xfb923c},
    {"Browser",  APP_BROWSER,  0,0,false,false, 0x7c3aed},
    {"Settings", APP_SETTINGS, 0,0,false,false, 0xa78bfa},
    {"Packages", APP_PKGMGR,   0,0,false,false, 0x06b6d4},
    {"Editor",   APP_EDITOR,   0,0,false,false, 0x4ade80},
    {"Paint",    APP_PAINT,    0,0,false,false, 0xf87171},
    {"Clock",    APP_CLOCK,    0,0,false,false, 0xfbbf24},
    {"NetMon",   APP_NETMON,   0,0,false,false, 0x22d3ee},
    {"Users",    APP_USERS,    0,0,false,false, 0xfb923c},
    {"About",    APP_ABOUT,    0,0,false,false, 0x6b7280},
};
#define ICON_COUNT   14
#define ICON_W       72
#define ICON_H       80
#define ICON_MARGIN  16

/* Taskbar clock cache */
static u32        tb_last_tick = 0;
static rtc_time_t tb_time;

/* -- Icon layout ---------------------------------------------------------- */
static void layout_icons(void) {
    i32 max_y = (i32)SCREEN_H - TASKBAR_H - ICON_MARGIN;
    i32 x = ICON_MARGIN, y = ICON_MARGIN;
    for (int i = 0; i < ICON_COUNT; i++) {
        if (y + ICON_H > max_y) {
            y  = ICON_MARGIN;
            x += ICON_W + ICON_MARGIN;
            if (x + ICON_W > (i32)SCREEN_W) x = ICON_MARGIN;
        }
        icons[i].x = x;
        icons[i].y = y;
        y += ICON_H + ICON_MARGIN;
    }
}

/* -- Icon graphics -------------------------------------------------------- */
static void draw_icon_graphic(i32 ix, i32 iy, app_id_t app, u32 color) {
    switch (app) {
    case APP_TERMINAL:
        gfx_rect(ix,iy,40,28,COL_SURFACE2);
        gfx_rect_outline(ix,iy,40,28,color);
        gfx_str(ix+4,iy+4,">_",color,COL_SURFACE2);
        gfx_str(ix+4,iy+14,"ls -la",COL_DIM,COL_SURFACE2); break;
    case APP_NOTES:
        gfx_rect(ix,iy,36,40,COL_YELLOW);
        gfx_rect(ix,iy,36,7,COL_ORANGE);
        for (int l=0;l<4;l++) gfx_hline(ix+4,iy+12+l*7,28,COL_SURFACE); break;
    case APP_FILES:
        gfx_rect(ix,iy+10,40,28,color);
        gfx_rect(ix,iy+6,20,10,color);
        gfx_rect(ix+2,iy+12,18,8,COL_SURFACE2); break;
    case APP_SYSMON:
        gfx_rect(ix,iy,40,28,COL_SURFACE2);
        gfx_rect_outline(ix,iy,40,28,color);
        for (int b=0;b<6;b++) { i32 bh=6+(b*5)%18; gfx_rect(ix+3+b*6,iy+26-bh,5,bh,color); } break;
    case APP_CALC:
        gfx_rect(ix,iy,38,40,COL_SURFACE2);
        gfx_rect_outline(ix,iy,38,40,color);
        gfx_str(ix+4,iy+4,"0",COL_TEXT,COL_SURFACE2);
        gfx_hline(ix+2,iy+14,34,COL_BORDER);
        gfx_str(ix+4,iy+18,"7 8 9",COL_DIM,COL_SURFACE2);
        gfx_str(ix+4,iy+26,"4 5 6",COL_DIM,COL_SURFACE2);
        gfx_str(ix+4,iy+34,"1 2 3",COL_DIM,COL_SURFACE2); break;
    case APP_BROWSER:
        gfx_rect(ix,iy,40,30,COL_SURFACE2);
        gfx_rect_outline(ix,iy,40,30,color);
        gfx_rect(ix+1,iy+1,38,8,COL_SURFACE3);
        gfx_str(ix+4,iy+2,"[URL]",COL_DIM,COL_SURFACE3);
        gfx_str(ix+4,iy+12,"Hello",color,COL_SURFACE2); break;
    case APP_SETTINGS:
        gfx_circle_fill(ix+20,iy+20,14,COL_SURFACE2);
        gfx_circle(ix+20,iy+20,14,color);
        gfx_circle_fill(ix+20,iy+20,5,color); break;
    case APP_PKGMGR:
        gfx_rect(ix+4,iy,32,12,color);
        gfx_rect(ix+4,iy+14,32,12,color);
        gfx_rect(ix+4,iy+28,32,12,color); break;
    case APP_EDITOR:
        gfx_rect(ix,iy,40,36,COL_SURFACE2);
        gfx_rect_outline(ix,iy,40,36,color);
        gfx_str(ix+2,iy+2,"int main",color,COL_SURFACE2);
        gfx_str(ix+2,iy+12,"{ hello",COL_DIM,COL_SURFACE2);
        gfx_str(ix+2,iy+22,"return 0",COL_DIM,COL_SURFACE2); break;
    case APP_PAINT:
        gfx_rect(ix,iy,40,36,COL_WHITE);
        gfx_circle_fill(ix+10,iy+10,8,COL_RED);
        gfx_circle_fill(ix+25,iy+18,7,COL_YELLOW);
        gfx_circle_fill(ix+15,iy+26,6,COL_PRIMARY); break;
    case APP_CLOCK:
        gfx_circle(ix+20,iy+20,18,color);
        gfx_vline(ix+20,iy+8,10,color);
        gfx_hline(ix+20,iy+20,8,color); break;
    case APP_NETMON:
        gfx_rect(ix,iy,40,28,COL_SURFACE2);
        gfx_rect_outline(ix,iy,40,28,color);
        for (int b=0;b<8;b++) { i32 bh=3+(b*3)%14; gfx_rect(ix+2+b*5,iy+25-bh,4,bh,b%2?color:COL_ACCENT); } break;
    case APP_USERS:
        gfx_circle_fill(ix+20,iy+12,10,color);
        gfx_rect(ix+6,iy+26,28,14,color); break;
    default:
        gfx_circle_fill(ix+20,iy+18,16,COL_DIM); break;
    }
}

static void draw_desktop_icon(const desktop_icon_t *ic) {
    i32 x = ic->x, y = ic->y;
    u32 bg = ic->hover ? COL_HOVER : (ic->selected ? COL_SELECTION : 0);
    if (bg) gfx_rect_rounded(x, y, ICON_W, ICON_H, 6, bg);
    if (ic->hover) gfx_rect_rounded_outline(x, y, ICON_W, ICON_H, 6, COL_BORDER);
    i32 ix = x + (ICON_W-40)/2, iy = y + 6;
    draw_icon_graphic(ix, iy, ic->app, ic->icon_color);
    gfx_str_centered(x, y+ICON_H-14, ICON_W, ic->label, COL_TEXT, COL_TRANSPARENT);
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
static void app_default_size(app_id_t app, i32 sw, i32 sh, i32 *w, i32 *h) {
    switch (app) {
    case APP_CALC:     *w = 300; *h = 400; return;
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

        /* Cascade: offset each new window by 20px so they don't perfectly stack */
        i32 cascade = (i32)(open_count % 8) * 20;
        i32 fx = x + cascade, fy = y + cascade;
        /* Keep on-screen */
        i32 sw = (i32)SCREEN_W, sh = (i32)SCREEN_H;
        if (fx + w > sw - 8)  fx = sw - w - 8;
        if (fy + h > sh - TASKBAR_H - 8) fy = 8;
        if (fx < 0) fx = 0;
        if (fy < 0) fy = 0;

        win->rect         = rect_make(fx, fy, w, h);
        win->restore_rect = win->rect;
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
        case APP_ABOUT:    app_help_init(win);     break;
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
        w->rect = rect_make(0, 0, (i32)SCREEN_W, (i32)SCREEN_H - TASKBAR_H);
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

    if (mode == 4) {
        wm_maximize(w);
        return;
    }

    i32 sw = (i32)SCREEN_W;
    i32 sh = (i32)SCREEN_H - TASKBAR_H;

    w->maximized = false;
    if (mode == 0) {
        w->rect = rect_make(0, 0, sw / 2, sh);
    } else if (mode == 1) {
        w->rect = rect_make(sw / 2, 0, sw - (sw / 2), sh);
    } else if (mode == 2) {
        w->rect = rect_make(0, 0, sw, sh / 2);
    } else if (mode == 3) {
        w->rect = rect_make(0, sh / 2, sw, sh - (sh / 2));
    }
}

/* -- Detect resize edge under cursor (6px border zone) -------------------- */
#define EDGE_ZONE 6
static u32 detect_resize_edge(window_t *w, i32 mx, i32 my) {
    rect_t r = w->rect;
    bool l = (mx >= r.x && mx < r.x + EDGE_ZONE);
    bool rr = (mx >= r.x + r.w - EDGE_ZONE && mx < r.x + r.w);
    bool t = (my >= r.y && my < r.y + EDGE_ZONE);
    bool b = (my >= r.y + r.h - EDGE_ZONE && my < r.y + r.h);

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

/* -- Draw one window ------------------------------------------------------ */
static void draw_window(window_t *w) {
    if (!w->active || w->minimized) return;
    i32 x=w->rect.x, y=w->rect.y, wd=w->rect.w, ht=w->rect.h;

    /* Shadow -- enhanced for focused window */
    if (w->focused) {
        gfx_shadow_ext(x, y, wd, ht, 8);
    } else {
        gfx_shadow_ext(x, y, wd, ht, 3);
    }

    /* Client area */
    gfx_rect(x, y+TITLEBAR_H, wd, ht-TITLEBAR_H, COL_SURFACE);

    /* Title bar gradient */
    u32 tbar = w->focused ? COL_WINBAR : rgb(0x10,0x12,0x1c);
    gfx_gradient_rect(x, y, wd, TITLEBAR_H, tbar, COL_SURFACE);
    gfx_rect_outline(x, y, wd, ht, w->focused ? COL_BORDER : rgb(0x20,0x24,0x38));
    gfx_hline(x, y+TITLEBAR_H, wd, COL_BORDER);

    /* Traffic-light buttons -- 16x16 hit area, 6px circle */
    i32 bmy = y + TITLEBAR_H/2;
    i32 b_close = x + wd - 54;
    i32 b_min   = x + wd - 36;
    i32 b_max   = x + wd - 18;
    gfx_circle_fill(b_close, bmy, 6, COL_BTN_CLOSE);
    gfx_circle_fill(b_min,   bmy, 6, COL_BTN_MIN);
    gfx_circle_fill(b_max,   bmy, 6, COL_BTN_MAX);

    /* Title text -- COL_TRANSPARENT so gradient shows through */
    u32 tcolor = w->focused ? COL_TEXT : COL_MUTED;
    gfx_str_clipped(x+10, y+(TITLEBAR_H-FONT_H)/2, wd-70, w->title, tcolor, COL_TRANSPARENT);

    /* App content clipped to client area */
    gfx_set_clip(x+1, y+TITLEBAR_H+1, wd-2, ht-TITLEBAR_H-2);
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
    default: break;
    }
    gfx_clear_clip();
}

/* Sort windows by z_order before drawing so layering is always correct */
void wm_draw_all(void) {
    /* Build sorted index array (simple insertion sort, MAX_WINDOWS is small) */
    int order[MAX_WINDOWS];
    int cnt = 0;
    for (int i = 0; i < MAX_WINDOWS; i++)
        if (windows[i].active && !windows[i].minimized)
            order[cnt++] = i;
    /* Bubble sort by z_order ascending (lowest drawn first = behind) */
    for (int a = 0; a < cnt-1; a++)
        for (int b = a+1; b < cnt; b++)
            if (windows[order[a]].z_order > windows[order[b]].z_order) {
                int tmp = order[a]; order[a] = order[b]; order[b] = tmp;
            }
    for (int i = 0; i < cnt; i++)
        draw_window(&windows[order[i]]);
    launcher_draw(launcher_open);
    notify_draw();
}

/* -- Mouse handling ------------------------------------------------------- */

/* Helper: is cursor within +/-8px of a titlebar button circle? */
static bool hit_btn(i32 mx, i32 my, i32 bx, i32 bmy) {
    i32 dx = mx - bx, dy = my - bmy;
    return (dx*dx + dy*dy) <= 100;  /* 10px radius */
}

void wm_handle_mouse(mouse_t *m) {
    if (launcher_open && launcher_handle_mouse(m, &launcher_open)) return;

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
            w->rect.x = m->x - w->drag_ox;
            w->rect.y = m->y - w->drag_oy;
            if (w->rect.y < 0) w->rect.y = 0;
            if (w->rect.y > (i32)SCREEN_H - TASKBAR_H - TITLEBAR_H)
                w->rect.y = (i32)SCREEN_H - TASKBAR_H - TITLEBAR_H;
            if (w->rect.x < -(w->rect.w-40)) w->rect.x = -(w->rect.w-40);
            if (w->rect.x > (i32)SCREEN_W-40) w->rect.x = (i32)SCREEN_W-40;
        } else {
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

    /* -- 4. Continuous left-hold for paint-style apps --------------------- */
    if (m->left && !m->left_clicked && hit && hit->focused) {
        rect_t cr = wm_client_rect(hit);
        if (rect_contains(cr, m->x, m->y)) {
            i32 rx = m->x - cr.x, ry = m->y - cr.y;
            if (hit->app == APP_PAINT) app_paint_click(hit, rx, ry, m);
        }
        return;
    }

    /* Only discrete clicks from here */
    if (!m->left_clicked) return;

    if (!hit) return;

    /* Focus the clicked window */
    wm_focus(hit);

    /* -- 5. Traffic-light buttons ----------------------------------------- */
    i32 bmy     = hit->rect.y + TITLEBAR_H/2;
    i32 b_close = hit->rect.x + hit->rect.w - 54;
    i32 b_min   = hit->rect.x + hit->rect.w - 36;
    i32 b_max   = hit->rect.x + hit->rect.w - 18;

    if (hit_btn(m->x, m->y, b_close, bmy)) { wm_close(hit);    return; }
    if (hit_btn(m->x, m->y, b_min,   bmy)) { wm_minimize(hit); return; }
    if (hit_btn(m->x, m->y, b_max,   bmy)) { wm_maximize(hit); return; }

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

    /* -- 8. Client-area click --------------------------------------------- */
    rect_t cr = wm_client_rect(hit);
    if (rect_contains(cr, m->x, m->y)) {
        i32 rx = m->x - cr.x, ry = m->y - cr.y;
        switch (hit->app) {
        case APP_FILES:    app_files_click(hit, rx, ry, m);    break;
        case APP_CALC:     app_calc_click(hit, rx, ry);        break;
        case APP_SYSMON:   app_sysmon_click(hit, rx, ry);      break;
        case APP_SETTINGS: app_settings_click(hit, rx, ry, m); break;
        case APP_BROWSER:  app_browser_click(hit, rx, ry);     break;
        case APP_PKGMGR:   app_pkgmgr_click(hit, rx, ry);     break;
        case APP_PAINT:    app_paint_click(hit, rx, ry, m);    break;
        case APP_USERS:    app_users_click(hit, rx, ry);       break;
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
    default: break;
    }
}

/* -- Desktop -------------------------------------------------------------- */
void desktop_draw(void) {
    gfx_gradient_rect(0, 0, (i32)SCREEN_W, (i32)SCREEN_H - TASKBAR_H,
                      rgb(0x0e,0x10,0x1a), rgb(0x1c,0x20,0x32));
    /* Subtle dot grid */
    for (u32 dy=24; dy<SCREEN_H-TASKBAR_H; dy+=40)
        for (u32 dx=32; dx<SCREEN_W; dx+=40)
            gfx_setpixel((i32)dx, (i32)dy, rgb(0x22,0x26,0x3a));
    layout_icons();
    for (int i = 0; i < ICON_COUNT; i++) draw_desktop_icon(&icons[i]);
}

void desktop_handle_mouse(mouse_t *m) {
    for (int i = 0; i < ICON_COUNT; i++) {
        rect_t r = rect_make(icons[i].x, icons[i].y, ICON_W, ICON_H);
        icons[i].hover = rect_contains(r, m->x, m->y);
        if (m->left_clicked && icons[i].hover) {
            /* Close launcher if open */
            launcher_open = false;
            i32 sw=(i32)SCREEN_W, sh=(i32)SCREEN_H;
            i32 ww, wh;
            app_default_size(icons[i].app, sw, sh, &ww, &wh);
            wm_open(icons[i].app, icons[i].label,
                    (sw-ww)/2, (sh-wh)/2 - 20, ww, wh);
        }
    }
}

/* -- Taskbar -------------------------------------------------------------- */
void taskbar_draw(void) {
    i32 ty = (i32)SCREEN_H - TASKBAR_H;

    /* Background */
    gfx_rect(0, ty, (i32)SCREEN_W, TASKBAR_H, COL_TASKBAR);
    gfx_hline(0, ty, (i32)SCREEN_W, COL_BORDER);

    /* Launcher button ("::::" grid icon) */
    u32 lbg = launcher_open ? COL_PRIMARY : COL_SURFACE2;
    gfx_rect_rounded(4, ty+4, 40, TASKBAR_H-8, 5, lbg);
    u32 lfc = launcher_open ? COL_WHITE : COL_PRIMARY;
    /* Draw 3x3 dot grid manually for a clear icon */
    for (int row=0; row<3; row++)
        for (int col=0; col<3; col++)
            gfx_circle_fill(4+8+col*10, ty+4+TASKBAR_H/2-8+row*8, 2, lfc);

    /* Window buttons -- leave room for clock */
    i32 max_x = (i32)SCREEN_W - 130;
    int slot = 0;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!windows[i].active) continue;
        i32 bx=50+slot*118, bw=114, bh=TASKBAR_H-8;
        if (bx + bw > max_x) break;

        u32 bg = windows[i].focused ? COL_SURFACE3 : COL_SURFACE2;
        gfx_rect_rounded(bx, ty+4, bw, bh, 4, bg);

        /* Active indicator bar */
        if (windows[i].focused && !windows[i].minimized)
            gfx_rect(bx+4, ty+bh+2, bw-8, 2, COL_PRIMARY);

        u32 tc = windows[i].minimized ? COL_MUTED : COL_TEXT;
        gfx_str_clipped(bx+6, ty+(TASKBAR_H-FONT_H)/2, bw-12,
                        windows[i].title, tc, COL_TRANSPARENT);
        slot++;
    }

    /* Clock -- right side */
    u32 now = timer_get_ticks();
    if (now - tb_last_tick > 50) { rtc_read(&tb_time); tb_last_tick = now; }
    char ts[12]; rtc_format_time(&tb_time, ts);
    i32 tw = gfx_str_width(ts);
    gfx_str((i32)SCREEN_W-tw-10, ty+(TASKBAR_H-FONT_H)/2, ts, COL_TEXT, COL_TRANSPARENT);

    /* Network status dot */
    u32 nc = net_is_up() ? COL_GREEN : COL_RED;
    gfx_circle_fill((i32)SCREEN_W-tw-24, ty+TASKBAR_H/2, 4, nc);
}

void taskbar_handle_mouse(mouse_t *m) {
    i32 ty = (i32)SCREEN_H - TASKBAR_H;
    if (!m->left_clicked) return;
    if (m->y < ty) return;

    /* Launcher toggle */
    if (m->x >= 4 && m->x < 44) {
        launcher_open = !launcher_open;
        return;
    }

    /* Window buttons */
    int slot = 0;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!windows[i].active) continue;
        i32 bx = 50 + slot * 118;
        if (m->x >= bx && m->x < bx+114) {
            if (windows[i].focused && !windows[i].minimized)
                wm_minimize(&windows[i]);
            else {
                windows[i].minimized = false;
                wm_focus(&windows[i]);
            }
            return;
        }
        slot++;
    }
}

/* -- App Launcher overlay ------------------------------------------------- */
static u32 launcher_hovered = 0xFFFF;

void launcher_draw(bool open) {
    if (!open) return;

    i32 lw = (i32)SCREEN_W * 70/100;
    if (lw > 600) lw = 600; if (lw < 380) lw = 380;
    i32 lh = (i32)SCREEN_H * 65/100;
    if (lh > 460) lh = 460; if (lh < 300) lh = 300;
    i32 lx = ((i32)SCREEN_W - lw) / 2;
    i32 ly = (i32)SCREEN_H - TASKBAR_H - lh - 8;
    if (ly < 0) ly = 0;

    gfx_shadow(lx, ly, lw, lh);
    gfx_rect_rounded(lx, ly, lw, lh, 12, COL_SURFACE);
    gfx_rect_rounded_outline(lx, ly, lw, lh, 12, COL_BORDER);

    /* Header */
    gfx_gradient_rect(lx+2, ly+2, lw-4, 34, COL_PRIMARY, COL_ACCENT);
    gfx_str_centered(lx, ly+12, lw, "Applications", COL_WHITE, COL_TRANSPARENT);
    gfx_hline(lx+2, ly+36, lw-4, COL_BORDER);

    /* App grid */
    i32 cols = 5, col_w = (lw-20)/cols, row_h = 88;
    for (int i = 0; i < ICON_COUNT; i++) {
        i32 col=i%cols, row=i/cols;
        i32 ax=lx+10+col*col_w, ay=ly+42+row*row_h;
        if (ay+row_h > ly+lh-4) break;

        bool hov = ((u32)i == launcher_hovered);
        if (hov) {
            gfx_rect_rounded(ax, ay, col_w-4, row_h-4, 6, COL_HOVER);
            gfx_rect_rounded_outline(ax, ay, col_w-4, row_h-4, 6, COL_BORDER);
        }

        i32 ix = ax + (col_w-4-40)/2, iy = ay+6;
        draw_icon_graphic(ix, iy, icons[i].app, icons[i].icon_color);
        gfx_str_centered(ax, iy+46, col_w-4, icons[i].label, COL_TEXT, COL_TRANSPARENT);
    }
}

bool launcher_handle_mouse(mouse_t *m, bool *open) {
    if (!*open) return false;
    i32 lw = (i32)SCREEN_W * 70/100;
    if (lw > 600) lw = 600; if (lw < 380) lw = 380;
    i32 lh = (i32)SCREEN_H * 65/100;
    if (lh > 460) lh = 460; if (lh < 300) lh = 300;
    i32 lx = ((i32)SCREEN_W - lw) / 2;
    i32 ly = (i32)SCREEN_H - TASKBAR_H - lh - 8;
    if (ly < 0) ly = 0;

    bool inside = rect_contains(rect_make(lx,ly,lw,lh), m->x, m->y);
    if (!inside) {
        if (m->left_clicked) {
            *open = false;
            launcher_hovered = 0xFFFF;
            return true;
        }
        launcher_hovered = 0xFFFF;
        return false;
    }

    i32 cols=5, col_w=(lw-20)/cols, row_h=88;
    launcher_hovered = 0xFFFF;
    for (int i = 0; i < ICON_COUNT; i++) {
        i32 col=i%cols, row=i/cols;
        i32 ax=lx+10+col*col_w, ay=ly+42+row*row_h;
        if (rect_contains(rect_make(ax,ay,col_w-4,row_h-4), m->x, m->y)) {
            launcher_hovered = (u32)i;
            if (m->left_clicked) {
                *open = false;
                i32 sw=(i32)SCREEN_W, sh=(i32)SCREEN_H;
                i32 ww, wh;
                app_default_size(icons[i].app, sw, sh, &ww, &wh);
                wm_open(icons[i].app, icons[i].label,
                        (sw-ww)/2, (sh-wh)/2-20, ww, wh);
                return true;
            }
        }
    }

    return true;
}
