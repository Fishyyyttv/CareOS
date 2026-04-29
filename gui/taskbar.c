/* =============================================================================
 * CareOS gui/taskbar.c -- Dock-style Taskbar
 * ============================================================================= */
#include "kernel.h"
#include "gui.h"

/* Dock constants */
#define DOCK_ICON_W    48      /* icon slot width  */
#define DOCK_ICON_H    48      /* icon slot height */
#define DOCK_ICON_GAP   8      /* gap between slots */
#define DOCK_PAD_H     12      /* vertical pad inside pill  */
#define DOCK_PAD_X     14      /* horizontal pad at pill edges */
#define DOCK_LAUNCHER_W 44     /* launcher button width */
#define DOCK_SHOW_DESK_W 20    /* show-desktop nub */
#define DOCK_SEP_W       2     /* separator between launcher and apps */
#define DOCK_CORNER     (TASKBAR_H / 2)

/* Pinned apps always shown in dock */
static const app_id_t pinned_apps[] = {
    APP_TERMINAL, APP_FILES, APP_BROWSER, APP_NOTES, APP_EDITOR, APP_SETTINGS
};
#define PINNED_COUNT ((i32)(sizeof(pinned_apps) / sizeof(pinned_apps[0])))

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

    /* ---- Pill background ---- */
    gfx_shadow_ext(dr.x - 4, dr.y - 4, dr.w + 8, dr.h + 8, 10);
    gfx_rect_rounded(dr.x, dr.y, dr.w, dr.h, DOCK_CORNER, g_theme->taskbar);
    gfx_rect_blend(dr.x, dr.y, dr.w, dr.h, COL_WHITE, 10);
    gfx_rect_rounded_outline(dr.x, dr.y, dr.w, dr.h, DOCK_CORNER, COL_BORDER);

    /* ---- Launcher grid button ---- */
    rect_t lr = L.launcher_rect;
    u32 lbg = launcher_open ? COL_PRIMARY : COL_SURFACE2;
    gfx_rect_rounded(lr.x, lr.y, lr.w, lr.h, 10, lbg);
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

    /* ---- App slots ---- */
    for (int i = 0; i < L.slot_count; i++) {
        dock_slot_t *s = &L.slots[i];
        rect_t r = s->rect;
        window_t *w = s->win;
        bool is_focused = w && w->focused && !w->minimized;
        bool is_open    = w != NULL;

        /* Active slot highlight */
        if (is_focused) {
            gfx_rect_rounded(r.x - 2, r.y - 2, r.w + 4, r.h + 4, 10, COL_SURFACE3);
            gfx_rect_rounded_outline(r.x - 2, r.y - 2, r.w + 4, r.h + 4, 10, COL_PRIMARY);
        } else if (is_open && w->minimized) {
            /* Minimized: subtle tint */
            gfx_rect_rounded(r.x - 1, r.y - 1, r.w + 2, r.h + 2, 8, COL_SURFACE2);
        }

        /* Icon */
        u32 icon_col = is_focused ? slot_color(s->app) :
                       (is_open   ? COL_DIM              :
                                    COL_MUTED);
        gfx_draw_icon(s->app, r.x + (r.w - 32) / 2, r.y + (r.h - 32) / 2, 32, icon_col);

        /* Running dot */
        if (is_open) {
            u32 dot = is_focused ? COL_PRIMARY : COL_DIM;
            gfx_circle_fill(r.x + r.w / 2, r.y + r.h + 4, 2, dot);
        }
    }

    /* ---- Show-desktop nub (right edge, subtle line) ---- */
    i32 nd_x = (i32)SCREEN_W - DOCK_SHOW_DESK_W - 2;
    gfx_vline(nd_x, L.dock_rect.y + DOCK_PAD_H,
              L.dock_rect.h - DOCK_PAD_H * 2, COL_BORDER);
}

/* ---- Mouse handling ------------------------------------------------------ */
void taskbar_handle_mouse(mouse_t *m) {
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
            /* Window exists: toggle focus/minimize */
            if (w->focused && !w->minimized) wm_minimize(w);
            else { w->minimized = false; wm_focus(w); }
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
        return;
    }
}
