/* =============================================================================
 * CareOS gui/taskbar.c -- Dock-style Taskbar
 * ============================================================================= */
#include "kernel.h"
#include "gui.h"
#include "image.h"
#include "icon.h"

/* Dock constants */
#define DOCK_ICON_W    48      /* icon slot width  */
#define DOCK_ICON_H    48      /* icon slot height */
#define DOCK_ICON_GAP   8      /* gap between slots */
#define DOCK_PAD_H     12      /* vertical pad inside pill  */
#define DOCK_PAD_X     14      /* horizontal pad at pill edges */
#define DOCK_LAUNCHER_W 44     /* launcher button width */
#define DOCK_SHOW_DESK_W 20    /* show-desktop nub */
#define DOCK_SEP_W       2     /* separator between launcher and apps */
#define DOCK_CORNER     CDL_R_DOCK   /* standardized dock rounding (24) */

/* Dock magnification (macOS-style). Visual only: click rects stay on the base
 * layout so hit-testing never drifts (see taskbar_handle_mouse). */
#define DOCK_ICON_BASE   32                 /* unmagnified icon draw size    */
#define DOCK_MAG_EXTRA   16                 /* max extra px added at cursor   */
#define DOCK_MAG_RANGE  (DOCK_ICON_W * 2)   /* falloff span (~2 icon widths)  */

/* Last-known cursor position, captured by taskbar_handle_mouse() every frame so
 * taskbar_draw() can compute magnification. Far-away sentinel = no hover. */
static i32 g_dock_mouse_x = -10000;
static i32 g_dock_mouse_y = -10000;

/* Pinned apps always shown in dock */
static const app_id_t pinned_apps[] = {
    APP_TERMINAL, APP_FILES, APP_BROWSER, APP_NOTES, APP_EDITOR, APP_SETTINGS
};
#define PINNED_COUNT ((i32)(sizeof(pinned_apps) / sizeof(pinned_apps[0])))

static u32 slot_click_tick[32] = {0};

static const char *taskbar_app_name(app_id_t app) {
    switch (app) {
        case APP_TERMINAL: return "Terminal";
        case APP_FILES:    return "Files";
        case APP_BROWSER:  return "Browser";
        case APP_NOTES:    return "Notes";
        case APP_EDITOR:   return "Editor";
        case APP_SETTINGS: return "Settings";
        case APP_SYSMON:   return "SysMon";
        case APP_CALC:     return "Calculator";
        case APP_PAINT:    return "Paint";
        case APP_CLOCK:    return "Clock";
        case APP_NETMON:   return "NetMon";
        case APP_USERS:    return "Users";
        case APP_PKGMGR:   return "Packages";
        default:           return "App";
    }
}

/* ---- Layout cache -------------------------------------------------------- */
#define DOCK_SLOT_MAX 24

typedef struct {
    rect_t  rect;
    app_id_t app;
    window_t *win;     /* NULL if pinned-only (not open) */
    bool     pinned;
} dock_slot_t;

typedef struct {
    rect_t       dock_rect;
    rect_t       launcher_rect;
    rect_t       showdesk_rect;
    dock_slot_t  slots[DOCK_SLOT_MAX];
    i32          slot_count;
} dock_layout_t;

static const char *slot_icon_color_map(app_id_t app) {
    /* Not used for drawing but kept for potential tooltip */
    (void)app;
    return "";
}

static u32 slot_color(app_id_t app) {
    switch (app) {
    case APP_TERMINAL: return rgb(0x4a, 0xde, 0x80);
    case APP_FILES:    return rgb(0x55, 0x9a, 0xff);
    case APP_BROWSER:  return rgb(0x7c, 0x3a, 0xed);
    case APP_NOTES:    return rgb(0xfb, 0xbf, 0x24);
    case APP_EDITOR:   return rgb(0x4a, 0xde, 0x80);
    case APP_SETTINGS: return rgb(0xa7, 0x8b, 0xfa);
    case APP_SYSMON:   return rgb(0x22, 0xd3, 0xee);
    case APP_CALC:     return rgb(0xfb, 0x92, 0x3c);
    case APP_PAINT:    return rgb(0xf8, 0x71, 0x71);
    case APP_CLOCK:    return rgb(0xfb, 0xbf, 0x24);
    case APP_NETMON:   return rgb(0x22, 0xd3, 0xee);
    case APP_USERS:    return rgb(0xfb, 0x92, 0x3c);
    case APP_PKGMGR:   return rgb(0x06, 0xb6, 0xd4);
    default:           return rgb(0x8a, 0x99, 0xba);
    }
}

/* Build the layout. Returns the dock rect. */
static void dock_build_layout(dock_layout_t *L) {
    i32 sw = (i32)SCREEN_W;
    i32 sh = (i32)SCREEN_H;

    kmemset(L, 0, sizeof(*L));

    /* ---- Collect open (non-pinned) windows ---- */
    bool pinned_open[PINNED_COUNT];
    kmemset(pinned_open, 0, sizeof(pinned_open));

    /* First pass: mark which pinned apps are open */
    for (int i = 0; i < MAX_WINDOWS; i++) {
        window_t *w = wm_get_window(i);
        if (!w || !w->active) continue;
        for (int p = 0; p < PINNED_COUNT; p++) {
            if (pinned_apps[p] == w->app) { pinned_open[p] = true; break; }
        }
    }

    /* Add pinned apps (open or not) */
    for (int p = 0; p < PINNED_COUNT && L->slot_count < DOCK_SLOT_MAX; p++) {
        dock_slot_t *s = &L->slots[L->slot_count++];
        s->app    = pinned_apps[p];
        s->pinned = true;
        s->win    = NULL;
        /* fill win if open */
        for (int i = 0; i < MAX_WINDOWS; i++) {
            window_t *w = wm_get_window(i);
            if (w && w->active && w->app == pinned_apps[p]) { s->win = w; break; }
        }
    }

    /* Add non-pinned open windows */
    for (int i = 0; i < MAX_WINDOWS && L->slot_count < DOCK_SLOT_MAX; i++) {
        window_t *w = wm_get_window(i);
        if (!w || !w->active) continue;
        /* skip if pinned */
        bool is_pinned = false;
        for (int p = 0; p < PINNED_COUNT; p++)
            if (pinned_apps[p] == w->app) { is_pinned = true; break; }
        if (is_pinned) continue;
        dock_slot_t *s = &L->slots[L->slot_count++];
        s->app    = w->app;
        s->pinned = false;
        s->win    = w;
    }

    /* ---- Compute geometry ---- */
    i32 n    = L->slot_count;
    i32 sep  = (n > 0) ? DOCK_SEP_W + DOCK_ICON_GAP : 0;
    i32 apps_w = n * (DOCK_ICON_W + DOCK_ICON_GAP) - (n > 0 ? DOCK_ICON_GAP : 0);
    i32 dock_w = DOCK_PAD_X + DOCK_LAUNCHER_W + sep + apps_w + DOCK_PAD_X;

    i32 dock_h  = TASKBAR_H;
    i32 dock_x  = (sw - dock_w) / 2;
    i32 dock_y  = sh - dock_h - 10;

    L->dock_rect = rect_make(dock_x, dock_y, dock_w, dock_h);

    /* Launcher button */
    i32 lx = dock_x + DOCK_PAD_X;
    i32 ly = dock_y + (dock_h - DOCK_ICON_H) / 2;
    L->launcher_rect = rect_make(lx, ly, DOCK_LAUNCHER_W, DOCK_ICON_H);

    /* App slots */
    i32 slot_x = lx + DOCK_LAUNCHER_W + (n > 0 ? DOCK_SEP_W + DOCK_ICON_GAP : 0);
    i32 slot_y = dock_y + (dock_h - DOCK_ICON_H) / 2;
    for (int i = 0; i < n; i++) {
        L->slots[i].rect = rect_make(slot_x, slot_y, DOCK_ICON_W, DOCK_ICON_H);
        slot_x += DOCK_ICON_W + DOCK_ICON_GAP;
    }

    /* Show-desktop nub */
    L->showdesk_rect = rect_make(sw - DOCK_SHOW_DESK_W - 4, dock_y, DOCK_SHOW_DESK_W, dock_h);
}

/* ---- Draw ---------------------------------------------------------------- */
void taskbar_draw(void) {
    dock_layout_t L;
    dock_build_layout(&L);

    rect_t dr = L.dock_rect;

    /* ---- Frosted-glass dock background ----
     * Soft floating shadow first, then the glass panel (blur backdrop + tint +
     * top sheen + hairline) at the standard dock radius. The panel is
     * translucent so the wallpaper reads through it. */
    gfx_shadow_soft(dr.x, dr.y, dr.w, dr.h, DOCK_CORNER);
    gfx_glass_panel(dr.x, dr.y, dr.w, dr.h, DOCK_CORNER);

    /* ---- Launcher grid button ---- */
    rect_t lr = L.launcher_rect;
    u32 lbg = launcher_open ? COL_PRIMARY : COL_SURFACE2;
    gfx_rect_rounded(lr.x, lr.y, lr.w, lr.h, CDL_R_BUTTON, lbg);
    if (launcher_open)
        gfx_rect_blend(lr.x, lr.y, lr.w, lr.h, COL_WHITE, 30);
    /* 3x2 dot grid */
    u32 dot_col = launcher_open ? COL_WHITE : COL_PRIMARY;
    for (int row = 0; row < 2; row++) {
        for (int col = 0; col < 3; col++) {
            i32 dx = lr.x + 10 + col * 9;
            i32 dy = lr.y + 14 + row * 10;
            gfx_circle_fill(dx + 3, dy + 3, 3, dot_col);
        }
    }

    /* ---- Separator ---- */
    if (L.slot_count > 0) {
        i32 sep_x = lr.x + lr.w + DOCK_ICON_GAP / 2;
        gfx_rect(sep_x, dr.y + DOCK_PAD_H, DOCK_SEP_W,
                 dr.h - DOCK_PAD_H * 2, COL_BORDER);
    }

    /* ---- App slots ----
     * Magnification is applied to the *visual* icon size only. Each slot's
     * click rect (s->rect) is unchanged, so hit-testing in
     * taskbar_handle_mouse() stays exact regardless of the animation. */
    bool mag_active = (g_dock_mouse_y >= dr.y - 24) &&
                      (g_dock_mouse_y <= dr.y + dr.h + 24);

    for (int i = 0; i < L.slot_count; i++) {
        dock_slot_t *s = &L.slots[i];
        rect_t r = s->rect;
        window_t *w = s->win;
        bool is_focused = w && w->focused && !w->minimized;
        bool is_open    = w != NULL;

        /* Slot centre and fixed baseline (bottom of the base icon). Icons grow
         * upward from this baseline so the dock's lower edge never shifts. */
        i32 cx          = r.x + r.w / 2;
        i32 base_bottom = r.y + (r.h - DOCK_ICON_BASE) / 2 + DOCK_ICON_BASE;

        /* Distance->scale ramp: full size + extra px that decays linearly over
         * ~2 icon widths. Integer math only. */
        i32 size = DOCK_ICON_BASE;
        if (mag_active) {
            i32 dist = g_dock_mouse_x - cx;
            if (dist < 0) dist = -dist;
            if (dist < DOCK_MAG_RANGE)
                size += (DOCK_MAG_EXTRA * (DOCK_MAG_RANGE - dist)) / DOCK_MAG_RANGE;
        }

        i32 icon_x = cx - size / 2;          /* centred horizontally */
        i32 icon_y = base_bottom - size;     /* anchored to baseline */

        /* Active/minimized chip, sized to the (possibly magnified) icon */
        if (is_focused) {
            gfx_rect_rounded(icon_x - 4, icon_y - 4, size + 8, size + 8,
                             CDL_R_BUTTON, COL_SURFACE3);
            gfx_rect_rounded_outline(icon_x - 4, icon_y - 4, size + 8, size + 8,
                                     CDL_R_BUTTON, COL_PRIMARY);
        } else if (is_open && w->minimized) {
            gfx_rect_rounded(icon_x - 3, icon_y - 3, size + 6, size + 6,
                             CDL_R_BUTTON, COL_SURFACE2);
        }

        /* Icon */
        u32 icon_col = is_focused ? slot_color(s->app) :
                       (is_open   ? COL_DIM              :
                                    COL_MUTED);
        icon_draw_app(s->app, icon_x, icon_y, size, icon_col);

        /* Ripple effect */
        u32 now = timer_get_ticks();
        u32 ct = slot_click_tick[s->app];
        if (ct > 0 && now - ct < 300) {
            u32 prog = now - ct;
            i32 rw = size + (prog * 30 / 300);
            u32 alpha = 100 - (prog * 100 / 300);
            gfx_rect_blend(icon_x - (rw-size)/2, icon_y - (rw-size)/2, rw, rw, COL_WHITE, alpha);
        }

        /* Running indicator: dot anchored to the dock baseline (not magnified)
         * so dots stay aligned. Focused app gets a brighter, wider dot. */
        if (is_open) {
            i32 dot_y = dr.y + dr.h - 6;
            if (is_focused) gfx_circle_fill(cx, dot_y, 3, COL_ACCENT);
            else            gfx_circle_fill(cx, dot_y, 2, COL_DIM);
        }
    }

    /* Draw tooltips after everything so they're on top */
    for (int i = 0; i < L.slot_count; i++) {
        dock_slot_t *s = &L.slots[i];
        rect_t r = s->rect;
        if (rect_contains(r, g_dock_mouse_x, g_dock_mouse_y)) {
            const char* name = taskbar_app_name(s->app);
            i32 tw = gfx_str_width_ex(name, FONT_BODY) + 16;
            i32 th = gfx_line_h_ex(FONT_BODY) + 8;
            i32 tx = r.x + r.w / 2 - tw / 2;
            i32 ty = r.y - th - 12;
            gfx_shadow_soft(tx, ty, tw, th, 6);
            gfx_glass_panel(tx, ty, tw, th, 6);
            gfx_str_centered(tx, ty, tw, name, COL_TEXT, COL_TRANSPARENT);
        }
    }

    /* ---- Show-desktop nub (right edge, subtle line) ---- */
    i32 nd_x = (i32)SCREEN_W - DOCK_SHOW_DESK_W - 2;
    gfx_vline(nd_x, L.dock_rect.y + DOCK_PAD_H,
              L.dock_rect.h - DOCK_PAD_H * 2, COL_BORDER);
}

/* ---- Mouse handling ------------------------------------------------------ */
void taskbar_handle_mouse(mouse_t *m) {
    /* Cache cursor every frame (this runs each frame) so taskbar_draw() can
     * drive magnification. Must happen before the click-only early return. */
    g_dock_mouse_x = m->x;
    g_dock_mouse_y = m->y;

    if (!m->left_clicked) return;

    /* Show-desktop nub */
    i32 nd_x = (i32)SCREEN_W - DOCK_SHOW_DESK_W - 2;
    if (m->x >= nd_x) { wm_minimize_all(); return; }

    dock_layout_t L;
    dock_build_layout(&L);

    /* Must be inside dock vertically */
    if (m->y < L.dock_rect.y || m->y >= L.dock_rect.y + L.dock_rect.h) return;

    /* Launcher */
    if (rect_contains(L.launcher_rect, m->x, m->y)) {
        launcher_open = !launcher_open;
        return;
    }

    /* App slots */
    for (int i = 0; i < L.slot_count; i++) {
        if (!rect_contains(L.slots[i].rect, m->x, m->y)) continue;
        window_t *w = L.slots[i].win;
        app_id_t  app = L.slots[i].app;

        if (w) {
            /* Window exists: toggle focus/minimize. wm_minimize() now toggles --
             * minimising an open window, or playing the dock-grow restore
             * animation for a minimised one -- so route both through it and only
             * plain-focus a window that is merely unfocused. */
            if (w->focused && !w->minimized) wm_minimize(w);
            else if (w->minimized)           wm_minimize(w);
            else                             wm_focus(w);
        } else {
            /* Pinned but not open: launch it */
            const char *name = "App";
            /* Simple name lookup */
            switch (app) {
            case APP_TERMINAL: name = "Terminal"; break;
            case APP_FILES:    name = "Files";    break;
            case APP_BROWSER:  name = "Browser";  break;
            case APP_NOTES:    name = "Notes";    break;
            case APP_EDITOR:   name = "Editor";   break;
            case APP_SETTINGS: name = "Settings"; break;
            default: break;
            }
            i32 sw = (i32)SCREEN_W, sh = (i32)SCREEN_H;
            i32 ww, wh;
            app_default_size(app, sw, sh, &ww, &wh);
            i32 avail_x = SIDEBAR_W + 8;
            i32 cx = avail_x + (sw - avail_x - ww) / 2;
            wm_open(app, name, cx, (sh - wh) / 2 - 10, ww, wh);
        }
        slot_click_tick[app] = timer_get_ticks();
        return;
    }
}
