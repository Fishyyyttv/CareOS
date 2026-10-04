/* =============================================================================
 * CareOS gui/taskbar.c -- the dock (visual-system spec §9, Dock artboard)
 *
 * A glass pill bottom-centre, 8 px above the screen edge: the launcher, a
 * hairline separator, the pinned apps plus any other running apps. The active
 * app gets an 18x5 coral bar under its icon, running apps a 5x5 dot. Hover
 * magnifies (phase 5 replaces this with a spring) and shows a glass label.
 * Every app has a home in the launcher, so the dock has no folder popover.
 * ============================================================================= */
#include "kernel.h"
#include "gui.h"
#include "ui.h"
#include "image.h"
#include "icon.h"

#define DOCK_SLOT      56      /* slot (button) size            */
#define DOCK_ICON      52      /* icon size inside a slot       */
#define DOCK_GAP        8      /* gap between slots             */
#define DOCK_PAD_T     10      /* pill padding: top             */
#define DOCK_PAD_X     12      /*               left / right    */
#define DOCK_PAD_B     14      /*               bottom (dots)   */
#define DOCK_RADIUS    26
#define DOCK_EDGE       8      /* gap to the bottom of the screen */
#define DOCK_SEP_H     40
#define DOCK_SHOW_DESK_W 20    /* show-desktop nub at the far right */

/* Magnification: visual only, click rects stay on the base layout. */
#define DOCK_MAG_EXTRA  14
#define DOCK_MAG_RANGE (DOCK_SLOT * 2)

static i32 g_dock_mouse_x = -10000;
static i32 g_dock_mouse_y = -10000;

/* Pinned apps, in the artboard's order (Pip's Home arrives with phase 4). */
static const app_id_t pinned_apps[] = {
    APP_TERMINAL, APP_FILES, APP_BROWSER, APP_NOTES, APP_EDITOR, APP_PKGMGR, APP_SETTINGS
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
        case APP_SYSMON:   return "Monitor";
        case APP_CALC:     return "Calculator";
        case APP_PAINT:    return "Paint";
        case APP_CLOCK:    return "Clock";
        case APP_NETMON:   return "NetMon";
        case APP_USERS:    return "Users";
        case APP_PKGMGR:   return "Packages";
        case APP_ABOUT:    return "About";
        case APP_HELP:     return "Help";
        case APP_MAZE:     return "Maze";
        case APP_3D:       return "3D";
        case APP_DOOM:     return "Doom";
        default:           return "App";
    }
}

/* ---- Layout -------------------------------------------------------------- */
#define DOCK_SLOT_MAX 24

typedef struct {
    rect_t   rect;
    app_id_t app;
    window_t *win;     /* NULL if pinned-only (not open) */
} dock_slot_t;

typedef struct {
    rect_t      dock_rect;
    rect_t      launcher_rect;
    i32         sep_x;
    dock_slot_t slots[DOCK_SLOT_MAX];
    i32         slot_count;
} dock_layout_t;

static window_t *window_for(app_id_t app) {
    window_t *best = NULL;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        window_t *w = wm_get_window(i);
        if (!w || !w->active || w->app != app) continue;
        if (!best || (w->focused && !w->minimized)) best = w;
    }
    return best;
}

static void dock_build_layout(dock_layout_t *L) {
    i32 sw = (i32)SCREEN_W, sh = (i32)SCREEN_H;
    kmemset(L, 0, sizeof(*L));

    for (int p = 0; p < PINNED_COUNT && L->slot_count < DOCK_SLOT_MAX; p++) {
        dock_slot_t *s = &L->slots[L->slot_count++];
        s->app = pinned_apps[p];
        s->win = window_for(pinned_apps[p]);
    }
    /* Running apps that are not pinned, one slot per app. */
    for (int i = 0; i < MAX_WINDOWS && L->slot_count < DOCK_SLOT_MAX; i++) {
        window_t *w = wm_get_window(i);
        if (!w || !w->active) continue;
        bool seen = false;
        for (int k = 0; k < L->slot_count; k++) if (L->slots[k].app == w->app) { seen = true; break; }
        if (seen) continue;
        dock_slot_t *s = &L->slots[L->slot_count++];
        s->app = w->app;
        s->win = window_for(w->app);
    }

    i32 n = L->slot_count;
    i32 inner = DOCK_SLOT + DOCK_GAP + 1 + DOCK_GAP + n * (DOCK_SLOT + DOCK_GAP) - DOCK_GAP;
    i32 dock_w = DOCK_PAD_X * 2 + inner;
    i32 dock_h = DOCK_PAD_T + DOCK_SLOT + DOCK_PAD_B;
    i32 dock_x = (sw - dock_w) / 2;
    i32 dock_y = sh - DOCK_EDGE - dock_h;
    L->dock_rect = rect_make(dock_x, dock_y, dock_w, dock_h);

    i32 x = dock_x + DOCK_PAD_X, y = dock_y + DOCK_PAD_T;
    L->launcher_rect = rect_make(x, y, DOCK_SLOT, DOCK_SLOT);
    x += DOCK_SLOT + DOCK_GAP;
    L->sep_x = x;
    x += 1 + DOCK_GAP;
    for (int i = 0; i < n; i++) {
        L->slots[i].rect = rect_make(x, y, DOCK_SLOT, DOCK_SLOT);
        x += DOCK_SLOT + DOCK_GAP;
    }
}

/* Icon size for a slot centred at cx: 52 px, plus magnification near the pointer. */
static i32 dock_icon_size(i32 cx, bool mag_active) {
    i32 size = DOCK_ICON;
    if (mag_active) {
        i32 dist = g_dock_mouse_x - cx;
        if (dist < 0) dist = -dist;
        if (dist < DOCK_MAG_RANGE) size += (DOCK_MAG_EXTRA * (DOCK_MAG_RANGE - dist)) / DOCK_MAG_RANGE;
    }
    return size;
}

static void dock_tooltip(rect_t slot, const char *name) {
    i32 tw = gfx_str_role_width(name, FONT_ROLE_UI, 1, FONT_W_BOLD) + 24;
    i32 th = 30;
    i32 tx = slot.x + slot.w / 2 - tw / 2;
    i32 ty = slot.y - DOCK_MAG_EXTRA - th - 10;
    ui_glass(5, rect_make(tx, ty, tw, th), th / 2, g_theme->panel_tint,
             (u8)(g_theme->panel_alpha + 40 > 255 ? 255 : g_theme->panel_alpha + 40), 24,
             UI_GLASS_HAIRLINE | UI_GLASS_SHADOW_S);
    gfx_str_role(tx + 12, ty + (th - gfx_role_line_h(FONT_ROLE_UI, 1)) / 2, name,
                 g_theme->ink, FONT_ROLE_UI, 1, FONT_W_BOLD);
}

/* ---- Draw ---------------------------------------------------------------- */
void taskbar_draw(void) {
    dock_layout_t L;
    dock_build_layout(&L);
    rect_t dr = L.dock_rect;

    /* The pill: glass with the artboard's 0 18 50 rgba(5,8,22,.35) shadow. */
    gfx_shadow_soft_ex(dr.x, dr.y, dr.w, dr.h, DOCK_RADIUS, 40, g_theme->is_dark ? 70 : 40, 14);
    ui_glass(4, dr, DOCK_RADIUS, g_theme->dock_tint, g_theme->dock_alpha, 28, UI_GLASS_HAIRLINE);

    bool mag_active = g_dock_mouse_y >= dr.y - 24 && g_dock_mouse_y <= dr.y + dr.h + 24 &&
                      g_dock_mouse_x >= dr.x - 24 && g_dock_mouse_x <= dr.x + dr.w + 24;

    /* Launcher */
    {
        rect_t r = L.launcher_rect;
        i32 cx = r.x + r.w / 2, size = dock_icon_size(cx, mag_active);
        i32 bottom = r.y + (r.h + DOCK_ICON) / 2;
        ui_app_icon(UI_ICON_LAUNCHER, cx - size / 2, bottom - size, size);
        if (launcher_open)
            gfx_rect_rounded(cx - 9, r.y + r.h + 4, 18, 5, 2, g_theme->care);
    }

    gfx_rect(L.sep_x, dr.y + (dr.h - DOCK_SEP_H) / 2, 1, DOCK_SEP_H,
             ui_mix(g_theme->dock_tint, g_theme->hairline, g_theme->is_dark ? 26 : 26));

    u32 now = timer_ms();
    for (int i = 0; i < L.slot_count; i++) {
        dock_slot_t *s = &L.slots[i];
        rect_t r = s->rect;
        window_t *w = s->win;
        bool active = w && w->focused && !w->minimized;

        /* Icons grow upward from a fixed baseline so the pill never moves. */
        i32 cx = r.x + r.w / 2, size = dock_icon_size(cx, mag_active);
        i32 bottom = r.y + (r.h + DOCK_ICON) / 2;
        i32 ix = cx - size / 2, iy = bottom - size;
        ui_app_icon(ui_icon_for_app(s->app), ix, iy, size);

        /* Click feedback: a short brightening flash. */
        u32 ct = slot_click_tick[s->app];
        if (ct > 0 && now - ct < 240)
            gfx_rect_rounded_blend(ix, iy, size, size, (size * 28 + 50) / 100, 0xFFFFFF,
                                   (u8)(70 - (now - ct) * 70 / 240));

        /* Indicators, 9 px below the slot: active bar, running dot. */
        if (active)
            gfx_rect_rounded(cx - 9, r.y + r.h + 4, 18, 5, 2, g_theme->care);
        else if (w)
            gfx_circle_fill(cx, r.y + r.h + 6, 2, g_theme->is_dark ? g_theme->muted : g_theme->muted);
    }

    /* Hover label above the slot. */
    if (rect_contains(L.launcher_rect, g_dock_mouse_x, g_dock_mouse_y))
        dock_tooltip(L.launcher_rect, "Launcher");
    for (int i = 0; i < L.slot_count; i++)
        if (rect_contains(L.slots[i].rect, g_dock_mouse_x, g_dock_mouse_y))
            dock_tooltip(L.slots[i].rect, taskbar_app_name(L.slots[i].app));
}

/* ---- Mouse --------------------------------------------------------------- */
void taskbar_handle_mouse(mouse_t *m) {
    /* Cache the pointer every frame for magnification and hover labels. */
    g_dock_mouse_x = m->x;
    g_dock_mouse_y = m->y;
    if (!m->left_clicked) return;

    /* Show-desktop nub, bottom-right corner. */
    if (m->x >= (i32)SCREEN_W - DOCK_SHOW_DESK_W && m->y >= (i32)SCREEN_H - TASKBAR_H) {
        wm_minimize_all();
        return;
    }

    dock_layout_t L;
    dock_build_layout(&L);
    if (!rect_contains(L.dock_rect, m->x, m->y)) return;

    if (rect_contains(L.launcher_rect, m->x, m->y)) {
        launcher_open = !launcher_open;
        return;
    }

    for (int i = 0; i < L.slot_count; i++) {
        if (!rect_contains(L.slots[i].rect, m->x, m->y)) continue;
        window_t *w = L.slots[i].win;
        app_id_t app = L.slots[i].app;
        if (w) {
            /* wm_minimize() toggles: it minimises an open window or plays the
             * restore animation for a minimised one. */
            if (w->focused && !w->minimized) wm_minimize(w);
            else if (w->minimized)           wm_minimize(w);
            else                             wm_focus(w);
        } else {
            i32 sw = (i32)SCREEN_W, sh = (i32)SCREEN_H, ww, wh;
            app_default_size(app, sw, sh, &ww, &wh);
            wm_open(app, taskbar_app_name(app), (sw - ww) / 2, (sh - wh) / 2 - 10, ww, wh);
        }
        if ((u32)app < 32) slot_click_tick[app] = timer_ms();
        launcher_open = false;
        return;
    }
}

/* The dock is drawn above windows, so the pointer belongs to it inside its pill. */
bool taskbar_contains(i32 x, i32 y) {
    dock_layout_t L;
    dock_build_layout(&L);
    return rect_contains(L.dock_rect, x, y);
}
