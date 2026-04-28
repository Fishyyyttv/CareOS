#include "kernel.h"
#include "gui.h"

typedef struct {
    app_id_t app;
    const char *name;
    const char *category;
    const char *subtitle;
} launcher_entry_t;

static const launcher_entry_t launcher_entries[] = {
    { APP_TERMINAL, "Terminal",   "Productivity", "Shell and command tools" },
    { APP_FILES,    "Files",      "Productivity", "Browse local storage" },
    { APP_EDITOR,   "Editor",     "Productivity", "Edit notes and code" },
    { APP_BROWSER,  "Browser",    "Internet",     "HTTP browser client" },
    { APP_NETMON,   "NetMon",     "Internet",     "Network activity graphs" },
    { APP_SETTINGS, "Settings",   "System",       "Display, account, and shell options" },
    { APP_SYSMON,   "Monitor",    "System",       "Performance and process monitor" },
    { APP_USERS,    "Users",      "System",       "Manage local accounts" },
    { APP_PKGMGR,   "Packages",   "System",       "Install and update software" },
    { APP_CALC,     "Calculator", "Utilities",    "Quick calculations" },
    { APP_CLOCK,    "Clock",      "Utilities",    "Clock and time display" },
    { APP_NOTES,    "Notes",      "Utilities",    "Scratchpad text editor" },
    { APP_PAINT,    "Paint",      "Creative",     "Pixel art and drawing" },
    { APP_ABOUT,    "About",      "CareOS",       "System guide and overview" },
};

static char search_query[64];
static u32  search_len = 0;

static i32 launcher_clampi(i32 v, i32 lo, i32 hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static bool launcher_match(const launcher_entry_t *entry) {
    if (search_len == 0) return true;

    for (u32 start = 0; entry->name[start]; start++) {
        u32 i = 0;
        while (search_query[i] && entry->name[start + i]) {
            char a = entry->name[start + i];
            char b = search_query[i];
            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
            if (a != b) break;
            i++;
        }
        if (i == search_len) return true;
    }

    for (u32 start = 0; entry->category[start]; start++) {
        u32 i = 0;
        while (search_query[i] && entry->category[start + i]) {
            char a = entry->category[start + i];
            char b = search_query[i];
            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
            if (a != b) break;
            i++;
        }
        if (i == search_len) return true;
    }

    return false;
}

static void launcher_open_app(app_id_t app) {
    i32 scrw = (i32)SCREEN_W;
    i32 scrh = (i32)SCREEN_H;
    i32 ww, wh;
    const char *title = "App";

    for (u32 i = 0; i < sizeof(launcher_entries) / sizeof(launcher_entries[0]); i++)
        if (launcher_entries[i].app == app) title = launcher_entries[i].name;

    launcher_open = false;
    app_default_size(app, scrw, scrh, &ww, &wh);
    wm_open(app, title, (scrw - ww) / 2, (scrh - wh) / 2, ww, wh);
}

static app_id_t launcher_render(mouse_t *m, bool draw) {
    i32 sw = (i32)SCREEN_W;
    i32 sh = (i32)SCREEN_H;
    i32 sc = (i32)GFX_FONT_SCALE;

    /* Fixed Panel Size */
    i32 pw = 640, ph = 520;
    if (sw < 700) pw = sw - 40;
    if (sh < 600) ph = sh - 100;
    i32 px = (sw - pw) / 2;
    i32 py = (sh - ph) / 2 - 20;
    rect_t panel = rect_make(px, py, pw, ph);

    if (draw) {
        /* Dim background */
        gfx_rect_blend(0, 0, sw, sh, COL_BLACK, 120);
        gfx_shadow_ext(px, py, pw, ph, 16);
        
        /* Main Body */
        gfx_rect_rounded(px, py, pw, ph, 20, COL_SURFACE);
        gfx_rect_blend(px, py, pw, ph, COL_GLASS_TINT, 20);
        gfx_rect_rounded_outline(px, py, pw, ph, 20, COL_BORDER);

        /* Header / Search Area */
        i32 search_y = py + 20;
        gfx_rect_rounded(px + 20, search_y, pw - 40, 48, 12, COL_SURFACE2);
        gfx_rect_rounded_outline(px + 20, search_y, pw - 40, 48, 12, COL_PRIMARY);
        
        if (search_len == 0) {
            gfx_str(px + 35, search_y + 15, "Search apps and tools...", COL_DIM, COL_TRANSPARENT);
        } else {
            gfx_str(px + 35, search_y + 15, search_query, COL_TEXT, COL_TRANSPARENT);
            /* Caret */
            if ((timer_get_ticks() / 40) % 2 == 0)
                gfx_rect(px + 35 + (i32)search_len * FONT_W * sc, search_y + 14, 2, 20, COL_PRIMARY);
        }
    }

    /* Grid Layout */
    i32 grid_x = px + 25;
    i32 grid_y = py + 85;
    i32 item_w = (pw - 50) / 4;
    i32 item_h = 100;
    i32 cols = 4;
    
    app_id_t hit = APP_NONE;
    int visible_idx = 0;

    for (u32 i = 0; i < sizeof(launcher_entries) / sizeof(launcher_entries[0]); i++) {
        const launcher_entry_t *entry = &launcher_entries[i];
        if (!launcher_match(entry)) continue;

        i32 col = visible_idx % cols;
        i32 row = visible_idx / cols;
        rect_t tile = rect_make(grid_x + col * item_w, grid_y + row * item_h, item_w, item_h);
        
        bool hover = rect_contains(tile, m->x, m->y);
        if (hover && m->left_clicked) hit = entry->app;

        if (draw) {
            if (hover) {
                gfx_rect_rounded(tile.x + 5, tile.y + 5, tile.w - 10, tile.h - 10, 12, COL_SURFACE2);
                gfx_rect_rounded_outline(tile.x + 5, tile.y + 5, tile.w - 10, tile.h - 10, 12, COL_PRIMARY);
            }
            
            i32 icon_size = 48;
            gfx_draw_icon(entry->app, tile.x + (tile.w - icon_size) / 2, tile.y + 10, icon_size, hover ? COL_PRIMARY : COL_ACCENT);
            gfx_str_centered(tile.x, tile.y + 65, tile.w, entry->name, COL_TEXT, COL_TRANSPARENT);
        }
        visible_idx++;
        if (visible_idx >= 16) break; /* Grid limit */
    }

    if (draw) {
        /* Footer / User Profile */
        i32 footer_h = 60;
        i32 fy = py + ph - footer_h;
        gfx_hline(px + 10, fy, pw - 20, COL_BORDER);
        
        /* User Info */
        gfx_circle_fill(px + 35, fy + 30, 15, COL_PRIMARY);
        gfx_str(px + 30, fy + 22, "?", COL_WHITE, COL_TRANSPARENT);
        gfx_str(px + 60, fy + 22, user_current_name(), COL_TEXT, COL_TRANSPARENT);

        /* Power Button */
        i32 pwr_x = px + pw - 50;
        bool pwr_hover = rect_contains(rect_make(pwr_x - 5, fy + 15, 40, 40), m->x, m->y);
        gfx_rect_rounded(pwr_x - 5, fy + 15, 40, 40, 8, pwr_hover ? COL_RED : COL_SURFACE2);
        gfx_str(pwr_x + 8, fy + 26, "!", COL_WHITE, COL_TRANSPARENT);
    }

    return hit;
}

void launcher_draw(mouse_t *m) {
    if (!launcher_open) return;
    launcher_render(m, true);
}

void launcher_handle_mouse(mouse_t *m) {
    if (!launcher_open || !m->left_clicked) return;
    
    app_id_t hit = launcher_render(m, false);
    if (hit != APP_NONE) {
        launcher_open_app(hit);
    } else {
        /* Check if clicked outside to close */
        i32 sw = (i32)SCREEN_W, sh = (i32)SCREEN_H;
        i32 pw = 640, ph = 520;
        i32 px = (sw - pw) / 2, py = (sh - ph) / 2 - 20;
        if (!rect_contains(rect_make(px, py, pw, ph), m->x, m->y)) {
            launcher_open = false;
        }
    }
}

void launcher_handle_key(char c) {
    if (!launcher_open) return;
    if (c == 27) { launcher_open = false; return; }
    if (c == '\n') {
        app_id_t hit = launcher_render(&(mouse_t){0}, false);
        if (hit != APP_NONE) launcher_open_app(hit);
        return;
    }
    if (c == '\b') {
        if (search_len > 0) search_query[--search_len] = '\0';
    } else if (c >= 32 && c <= 126 && search_len < 63) {
        search_query[search_len++] = c;
        search_query[search_len] = '\0';
    }
}
