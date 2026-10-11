/* CareOS -- gui/launcher.c
 *
 * The full-screen launcher (visual-system spec §11, Launcher artboard): the
 * screen behind it blurred and tinted, a search field, category chips and a
 * 7-column grid of 76 px app icons with page dots. Typing filters the
 * application registry (include/appdb.h) and the system actions with the
 * fuzzy scorer; arrows move the selection, Enter launches, Esc closes.
 */

#include "kernel.h"
#include "appdb.h"
#include "gui.h"
#include "ui.h"
#include "image.h"
#include "resource_cache.h"
#include "icon.h"
#include "glass.h"
#include "spring.h"

#define LAUNCHER_MAX_MATCHES 64
#define LN_W        1000            /* content column width            */
#define LN_COLS     7
#define LN_COL_GAP  12
#define LN_ROW_GAP  26
#define LN_ICON     76
#define LN_SEARCH_W 560
#define LN_SEARCH_H 52
#define LN_CHIP_H   38

typedef struct {
    const char *token;
    app_id_t    app;
} launcher_token_t;

/* exec "builtin:<token>" -> window to open. */
static const launcher_token_t launcher_apps[] = {
    { "terminal",   APP_TERMINAL }, { "files",    APP_FILES    },
    { "editor",     APP_EDITOR   }, { "browser",  APP_BROWSER  },
    { "netmon",     APP_NETMON   }, { "settings", APP_SETTINGS },
    { "sysmon",     APP_SYSMON   }, { "users",    APP_USERS    },
    { "packages",   APP_PKGMGR   }, { "calculator", APP_CALC   },
    { "clock",      APP_CLOCK    }, { "notes",    APP_NOTES    },
    { "paint",      APP_PAINT    }, { "about",    APP_ABOUT    },
    { "help",       APP_HELP     }, { "maze",     APP_MAZE     },
    { "3d",         APP_3D       }, { "doom",     APP_DOOM     },
    { "pip",        APP_PIP      },
};

/* icon token -> glyph, for registry entries that are not builtins. */
static const launcher_token_t launcher_icons[] = {
    { "terminal", APP_TERMINAL }, { "files",    APP_FILES    },
    { "editor",   APP_EDITOR   }, { "browser",  APP_BROWSER  },
    { "netmon",   APP_NETMON   }, { "settings", APP_SETTINGS },
    { "sysmon",   APP_SYSMON   }, { "users",    APP_USERS    },
    { "packages", APP_PKGMGR   }, { "calc",     APP_CALC     },
    { "clock",    APP_CLOCK    }, { "notes",    APP_NOTES    },
    { "paint",    APP_PAINT    }, { "about",    APP_ABOUT    },
    { "help",     APP_HELP     }, { "generic",  APP_PKGMGR   },
    { "pip",      APP_PIP      },
};

/* Categories (spec §11): a static table per app. */
enum { CAT_ALL = 0, CAT_MAKE, CAT_SYSTEM, CAT_INTERNET, CAT_GAMES, CAT_COUNT };
static const char *const CAT_NAMES[CAT_COUNT] = { "All apps", "Make", "System", "Internet", "Games" };

static u32 app_category(app_id_t app) {
    switch (app) {
    case APP_NOTES: case APP_EDITOR: case APP_PAINT: case APP_TERMINAL: return CAT_MAKE;
    case APP_BROWSER: case APP_NETMON:                                  return CAT_INTERNET;
    case APP_MAZE: case APP_3D: case APP_DOOM: case APP_PIP:            return CAT_GAMES;
    case APP_NONE:                                                      return CAT_ALL;
    default:                                                            return CAT_SYSTEM;
    }
}

typedef struct {
    u32  app_index;  /* index in appdb, OR index in launcher_actions if is_action */
    int  score;
    bool is_action;
} matched_app_t;

/* System actions searchable alongside apps ("shut", "lock", "theme"). */
typedef enum { ACT_SHUTDOWN, ACT_RESTART, ACT_LOCK, ACT_THEME } launcher_act_t;
typedef struct {
    const char *title;
    const char *keywords;
    u8          kind;
    ui_glyph_t  glyph;
} launcher_action_t;

static const launcher_action_t launcher_actions[] = {
    { "Shut Down",    "shutdown power off poweroff halt", ACT_SHUTDOWN, UI_GLYPH_POWER   },
    { "Restart",      "restart reboot",                   ACT_RESTART,  UI_GLYPH_RESTART },
    { "Lock Screen",  "lock screen signout logout",       ACT_LOCK,     UI_GLYPH_LOCK    },
    { "Toggle Theme", "theme dark light appearance mode", ACT_THEME,    UI_GLYPH_MOON    },
};
#define LAUNCHER_ACTION_COUNT ((u32)(sizeof(launcher_actions) / sizeof(launcher_actions[0])))

/* State */
static char search_query[64] = {0};
static u32  search_len = 0;
static int  selected_index = 0;
static matched_app_t matched_apps[LAUNCHER_MAX_MATCHES];
static u32  matched_count = 0;
static bool prev_launcher_open = false;
static u32  s_category = CAT_ALL;
static u32  s_page = 0;
static bool s_kbd_used = false;   /* show the selection once the keyboard moves it */

/* The blurred, tinted screen behind the launcher, built once per opening. */
static u32 *s_backdrop = NULL;
static i32  s_backdrop_w = 0, s_backdrop_h = 0;
static bool s_backdrop_ok = false;

static u32 launcher_strlen(const char *s) {
    u32 len = 0;
    while (s && s[len]) len++;
    return len;
}

static char launcher_tolower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

/* Fuzzy match score of pattern against target; 0 = no match. */
static int fuzzy_score_string(const char *pattern, const char *target) {
    if (!pattern || !target || pattern[0] == '\0') return 1;
    int score = 100, p_idx = 0, t_idx = 0, consecutive = 0, last_match_pos = -1;
    bool is_prefix = true;
    while (pattern[p_idx] && target[t_idx]) {
        char p = launcher_tolower(pattern[p_idx]), t = launcher_tolower(target[t_idx]);
        if (p == t) {
            if (p_idx == 0 && t_idx == 0) score += 300;
            if (t_idx == 0 || target[t_idx - 1] == ' ' || target[t_idx - 1] == '-' ||
                target[t_idx - 1] == '_' || target[t_idx - 1] == '/' || target[t_idx - 1] == ':')
                score += 100;
            if (last_match_pos != -1 && t_idx == last_match_pos + 1) { consecutive++; score += 50 * consecutive; }
            else consecutive = 0;
            last_match_pos = t_idx;
            p_idx++;
        } else {
            if (p_idx == 0) is_prefix = false;
            score -= 3;
        }
        t_idx++;
    }
    if (pattern[p_idx] != '\0') return 0;
    if (t_idx == p_idx) score += 400;
    if (is_prefix) score += 200;
    int len_diff = (int)launcher_strlen(target) - p_idx;
    if (len_diff > 0) score -= len_diff * 2;
    return score > 0 ? score : 1;
}

static int launcher_score_entry(const app_entry_t *entry, const char *query) {
    if (!entry) return 0;
    if (!query || query[0] == '\0') return 1;
    int best = fuzzy_score_string(query, entry->name) * 3;
    int s = fuzzy_score_string(query, entry->id) * 2;      if (s > best) best = s;
    s = fuzzy_score_string(query, entry->category);        if (s > best) best = s;
    s = fuzzy_score_string(query, entry->exec);            if (s > best) best = s;
    return best;
}

static app_id_t launcher_lookup(const launcher_token_t *table, u32 n, const char *token, app_id_t fallback) {
    if (!token) return fallback;
    for (u32 i = 0; i < n; i++)
        if (kstrcmp(table[i].token, token) == 0) return table[i].app;
    return fallback;
}

static app_id_t launcher_app_id(const app_entry_t *e) {
    return launcher_lookup(launcher_apps, sizeof(launcher_apps) / sizeof(launcher_apps[0]),
                           appdb_builtin_token(e), APP_NONE);
}

static app_id_t launcher_icon_id(const app_entry_t *e) {
    return launcher_lookup(launcher_icons, sizeof(launcher_icons) / sizeof(launcher_icons[0]),
                           e->icon, APP_PKGMGR);
}

/* Matches in display order: actions (only while searching), then apps. With an
 * empty query the apps keep registry order; while searching they sort by score. */
static void launcher_update_matches(void) {
    matched_count = 0;
    if (search_len > 0) {
        for (u32 i = 0; i < LAUNCHER_ACTION_COUNT && matched_count < LAUNCHER_MAX_MATCHES; i++) {
            int s1 = fuzzy_score_string(search_query, launcher_actions[i].title);
            int s2 = fuzzy_score_string(search_query, launcher_actions[i].keywords);
            int sc = s1 > s2 ? s1 : s2;
            if (sc > 0) {
                matched_apps[matched_count].app_index = i;
                matched_apps[matched_count].score = sc + 40;
                matched_apps[matched_count].is_action = true;
                matched_count++;
            }
        }
    }
    u32 total = appdb_count();
    for (u32 i = 0; i < total && matched_count < LAUNCHER_MAX_MATCHES; i++) {
        const app_entry_t *entry = appdb_get(i);
        if (!entry) continue;
        if (s_category != CAT_ALL && app_category(launcher_app_id(entry)) != s_category) continue;
        int score = launcher_score_entry(entry, search_query);
        if (score > 0) {
            matched_apps[matched_count].app_index = i;
            matched_apps[matched_count].score = score;
            matched_apps[matched_count].is_action = false;
            matched_count++;
        }
    }
    if (search_len > 0) {
        for (u32 i = 0; i < matched_count; i++)
            for (u32 j = i + 1; j < matched_count; j++)
                if (matched_apps[j].score > matched_apps[i].score) {
                    matched_app_t tmp = matched_apps[i];
                    matched_apps[i] = matched_apps[j];
                    matched_apps[j] = tmp;
                }
    }
    if (selected_index >= (int)matched_count) selected_index = matched_count ? (int)matched_count - 1 : 0;
    if (selected_index < 0) selected_index = 0;
}

/* Closing only drops the flag; launcher_draw() sees the edge and plays the
 * exit animation, then frees the backdrop. */
static void launcher_close(void) {
    launcher_open = false;
}

static void launcher_open_entry(const app_entry_t *entry) {
    if (!entry) return;
    launcher_close();
    app_id_t app = launcher_app_id(entry);
    if (app != APP_NONE) {
        i32 scrw = (i32)SCREEN_W, scrh = (i32)SCREEN_H, ww, wh;
        app_default_size(app, scrw, scrh, &ww, &wh);
        wm_open(app, entry->name, (scrw - ww) / 2, (scrh - wh) / 2, ww, wh);
        return;
    }
    char out[512];
    if (appdb_launch(entry, out, sizeof(out)) != 0) {
        notify_push(entry->name, "Failed to launch", COL_RED);
        return;
    }
    notify_push(entry->name, out[0] ? out : "Finished", COL_ACCENT);
}

static void launcher_run_action(u8 kind) {
    launcher_close();
    switch (kind) {
    case ACT_SHUTDOWN: power_shutdown(); break;   /* does not return */
    case ACT_RESTART:  power_reboot();   break;   /* does not return */
    case ACT_LOCK:     g_session_action = SESSION_ACT_LOCK; break;
    case ACT_THEME:
        theme_switch(!g_theme->is_dark);
        settings_set_theme(g_theme->is_dark ? 0u : 1u);
        break;
    default: break;
    }
}

static void launcher_activate(u32 idx) {
    if (idx >= matched_count) return;
    if (matched_apps[idx].is_action) launcher_run_action(launcher_actions[matched_apps[idx].app_index].kind);
    else launcher_open_entry(appdb_get(matched_apps[idx].app_index));
}

/* ---- Layout (shared by draw and hit-testing) ------------------------------ */
typedef struct {
    i32    x, w;                 /* content column */
    rect_t search;
    rect_t chips[CAT_COUNT];
    i32    grid_y, col_w, tile_h, rows, per_page, pages;
    i32    dots_y;
} ln_layout_t;

static void launcher_layout(ln_layout_t *L) {
    i32 sw = (i32)SCREEN_W, sh = (i32)SCREEN_H;
    L->w = sw - 48 < LN_W ? sw - 48 : LN_W;
    L->x = (sw - L->w) / 2;
    i32 y = TOPBAR_H + 44;
    L->search = rect_make((sw - LN_SEARCH_W) / 2, y, LN_SEARCH_W, LN_SEARCH_H);
    y += LN_SEARCH_H + 22;

    i32 total = 0;
    i32 cw[CAT_COUNT];
    for (u32 i = 0; i < CAT_COUNT; i++) {
        cw[i] = gfx_str_role_width(CAT_NAMES[i], FONT_ROLE_UI, 1, FONT_W_BOLD) + 32;
        total += cw[i] + (i ? 8 : 0);
    }
    i32 cx = (sw - total) / 2;
    for (u32 i = 0; i < CAT_COUNT; i++) {
        L->chips[i] = rect_make(cx, y, cw[i], LN_CHIP_H);
        cx += cw[i] + 8;
    }
    y += LN_CHIP_H + 22 + 14;

    L->grid_y = y;
    L->col_w = (L->w - (LN_COLS - 1) * LN_COL_GAP) / LN_COLS;
    L->tile_h = 10 + LN_ICON + 10 + gfx_role_line_h(FONT_ROLE_UI, 1) + 10;
    /* Rows that fit above the dock, leaving room for the page dots. */
    i32 avail = sh - TASKBAR_H - 40 - y;
    L->rows = (avail + LN_ROW_GAP) / (L->tile_h + LN_ROW_GAP);
    if (L->rows < 1) L->rows = 1;
    L->per_page = L->rows * LN_COLS;
    L->pages = matched_count ? ((i32)matched_count + L->per_page - 1) / L->per_page : 1;
    L->dots_y = y + L->rows * (L->tile_h + LN_ROW_GAP) - LN_ROW_GAP + 18;
}

static rect_t tile_rect(const ln_layout_t *L, i32 slot) {
    i32 col = slot % LN_COLS, row = slot / LN_COLS;
    return rect_make(L->x + col * (L->col_w + LN_COL_GAP), L->grid_y + row * (L->tile_h + LN_ROW_GAP),
                     L->col_w, L->tile_h);
}

static rect_t dot_rect(const ln_layout_t *L, i32 p) {
    i32 total = L->pages * 8 + (L->pages - 1) * 8 + 14;   /* active dot is 22 wide */
    i32 x = ((i32)SCREEN_W - total) / 2;
    for (i32 k = 0; k < p; k++) x += ((u32)k == s_page ? 22 : 8) + 8;
    return rect_make(x, L->dots_y, (u32)p == s_page ? 22 : 8, 8);
}

/* ---- Drawing --------------------------------------------------------------- */
static void build_backdrop(void) {
    i32 sw = (i32)SCREEN_W, sh = (i32)SCREEN_H;
    i32 y0 = TOPBAR_H, h = sh - y0;
    if (!s_backdrop || s_backdrop_w != sw || s_backdrop_h != h) {
        if (s_backdrop) kfree(s_backdrop);
        s_backdrop = (u32 *)kmalloc((size_t)sw * (size_t)h * 4u);
        s_backdrop_w = sw; s_backdrop_h = h;
    }
    s_backdrop_ok = false;
    if (!s_backdrop || !g_target || !g_target->pixels) return;
    /* The artboard's blur(30px) of the live screen, then its rgba(7,10,22,.55)
     * tint (white at 55 % on the light theme), baked in once. The screen
     * itself is left untouched so the animation can fade between the two. */
    u32 *scratch = (u32 *)kmalloc((size_t)glass_scratch_words((u32)sw, (u32)h) * 4u);
    if (!scratch) return;
    u32 st = g_target->pitch / 4u;
    glass_blur(g_target->pixels + (u32)y0 * st, st, (u32)sw, (u32)h, 30, s_backdrop, scratch);
    kfree(scratch);
    u32 tint = g_theme->is_dark ? 0x070A16u : 0xFFFFFFu;
    for (u32 i = 0; i < (u32)sw * (u32)h; i++) s_backdrop[i] = ui_mix(s_backdrop[i], tint, 140);
    s_backdrop_ok = true;
}

static void draw_search(const ln_layout_t *L) {
    rect_t r = L->search;
    u32 over = g_theme->is_dark ? 0xFFFFFFu : g_theme->ink;
    gfx_rect_rounded_blend(r.x, r.y, r.w, r.h, 18, over, g_theme->is_dark ? 26 : 14);
    gfx_rect_rounded_outline(r.x, r.y, r.w, r.h, 18,
                             ui_mix(g_theme->is_dark ? 0x1A1F33u : 0xDDE0EAu, over, g_theme->is_dark ? 41 : 30));
    ui_glyph(UI_GLYPH_SEARCH, r.x + 18, r.y + (r.h - 20) / 2, 20, g_theme->ink2);
    i32 tx = r.x + 50, ty = r.y + (r.h - gfx_role_line_h(FONT_ROLE_UI, 3)) / 2;
    if (search_len == 0) {
        gfx_str_role(tx, ty, "Type to find an app", g_theme->muted, FONT_ROLE_UI, 3, FONT_W_REGULAR);
    } else {
        gfx_str_role_fit(tx, ty, r.w - 70, search_query, g_theme->ink, FONT_ROLE_UI, 3, FONT_W_REGULAR);
        if ((timer_ms() / 500) % 2 == 0) {
            i32 cw = gfx_str_role_width(search_query, FONT_ROLE_UI, 3, FONT_W_REGULAR);
            if (cw > r.w - 70) cw = r.w - 70;
            gfx_rect(tx + cw + 1, ty + 1, 2, gfx_role_line_h(FONT_ROLE_UI, 3) - 2, g_theme->care);
        }
    }
}

static void draw_tile(rect_t t, u32 idx, bool hover, bool sel) {
    u32 over = g_theme->is_dark ? 0xFFFFFFu : g_theme->ink;
    if (sel)        gfx_rect_rounded_blend(t.x, t.y, t.w, t.h, 22, over, g_theme->is_dark ? 31 : 20);
    else if (hover) gfx_rect_rounded_blend(t.x, t.y, t.w, t.h, 22, over, g_theme->is_dark ? 20 : 13);

    i32 ix = t.x + (t.w - LN_ICON) / 2, iy = t.y + 10;
    const char *label;
    if (matched_apps[idx].is_action) {
        const launcher_action_t *a = &launcher_actions[matched_apps[idx].app_index];
        i32 r = (LN_ICON * 28 + 50) / 100;
        gfx_shadow_soft_ex(ix, iy, LN_ICON, LN_ICON, r, 14, 40, 4);
        gfx_rect_rounded(ix, iy, LN_ICON, LN_ICON, r, g_theme->raised2);
        ui_glyph(a->glyph, ix + (LN_ICON - 40) / 2, iy + (LN_ICON - 40) / 2, 40, g_theme->ink);
        label = a->title;
    } else {
        const app_entry_t *e = appdb_get(matched_apps[idx].app_index);
        if (!e) return;
        app_id_t app = launcher_app_id(e);
        if (app != APP_NONE) ui_app_icon(ui_icon_for_app(app), ix, iy, LN_ICON);
        else icon_draw(e->icon, ix, iy, LN_ICON, launcher_icon_id(e), g_theme->care);
        label = e->name;
    }
    i32 lw = gfx_str_role_width(label, FONT_ROLE_UI, 1, FONT_W_BOLD);
    if (lw > t.w - 8) lw = t.w - 8;
    gfx_str_role_fit(t.x + (t.w - lw) / 2, iy + LN_ICON + 10, t.w - 8, label, g_theme->ink,
                     FONT_ROLE_UI, 1, FONT_W_BOLD);
}

/* Search, chips, grid and page dots (everything above the backdrop). */
static void draw_content(mouse_t *m) {
    ln_layout_t L;
    launcher_layout(&L);
    if ((i32)s_page >= L.pages) s_page = (u32)(L.pages - 1);

    draw_search(&L);
    for (u32 i = 0; i < CAT_COUNT; i++) {
        bool hover = m && rect_contains(L.chips[i], m->x, m->y);
        if (i == s_category) {
            gfx_rect_rounded(L.chips[i].x, L.chips[i].y, L.chips[i].w, L.chips[i].h, LN_CHIP_H / 2, g_theme->ink);
        } else {
            u32 over = g_theme->is_dark ? 0xFFFFFFu : g_theme->ink;
            gfx_rect_rounded_blend(L.chips[i].x, L.chips[i].y, L.chips[i].w, L.chips[i].h, LN_CHIP_H / 2, over,
                                   hover ? (g_theme->is_dark ? 46 : 30) : (g_theme->is_dark ? 26 : 16));
        }
        gfx_str_role(L.chips[i].x + 16, L.chips[i].y + (LN_CHIP_H - gfx_role_line_h(FONT_ROLE_UI, 1)) / 2,
                     CAT_NAMES[i], i == s_category ? g_theme->ground : g_theme->ink, FONT_ROLE_UI, 1, FONT_W_BOLD);
    }

    if (matched_count == 0) {
        const char *msg = search_len ? "No apps or commands match" : "Nothing in this category yet";
        i32 w = gfx_str_role_width(msg, FONT_ROLE_UI, 2, FONT_W_REGULAR);
        gfx_str_role(((i32)SCREEN_W - w) / 2, L.grid_y + 40, msg, g_theme->ink2, FONT_ROLE_UI, 2, FONT_W_REGULAR);
        return;
    }

    u32 first_idx = s_page * (u32)L.per_page;
    for (i32 slot = 0; slot < L.per_page; slot++) {
        u32 idx = first_idx + (u32)slot;
        if (idx >= matched_count) break;
        rect_t t = tile_rect(&L, slot);
        draw_tile(t, idx, m && rect_contains(t, m->x, m->y), (int)idx == selected_index && (search_len > 0 || s_kbd_used));
    }

    if (L.pages > 1) {
        for (i32 p = 0; p < L.pages; p++) {
            rect_t d = dot_rect(&L, p);
            u32 over = g_theme->is_dark ? 0xFFFFFFu : g_theme->ink;
            if ((u32)p == s_page) gfx_rect_rounded(d.x, d.y, d.w, d.h, 4, g_theme->ink);
            else gfx_rect_rounded_blend(d.x, d.y, d.w, d.h, 4, over, 77);
        }
    }
}

/* ---- Open / close animation ----------------------------------------------------
 * Launchpad-style: the grid starts slightly small and recessed and springs
 * forward to full size (a ~1.5 % overshoot gives the "pop") while it fades in,
 * and the desktop behind it blurs and darkens at the same rate -- the work
 * recedes as the apps come forward. Closing plays it back on a quicker,
 * critically damped spring. While the spring runs, the launcher is a sprite
 * rendered once and scaled each frame; once it settles it draws live again so
 * hover, typing and paging respond normally. */
#define LN_SCALE_FROM 56360            /* 0.86 in Q16 */

static spring_t s_anim;                /* 0 = closed, SPRING_ONE = open */
static bool     s_anim_ready = false;
static u32      s_anim_ms = 0;
static u32     *s_layer = NULL;        /* ARGB sprite of draw_content() */
static rect_t   s_layer_r;

static void free_layer(void) {
    if (s_layer) { kfree(s_layer); s_layer = NULL; }
}

static rect_t content_bounds(void) {
    ln_layout_t L;
    launcher_layout(&L);
    i32 x0 = L.x < L.search.x ? L.x : L.search.x;
    i32 x1 = L.x + L.w > L.search.x + L.search.w ? L.x + L.w : L.search.x + L.search.w;
    for (u32 i = 0; i < CAT_COUNT; i++) {
        if (L.chips[i].x < x0) x0 = L.chips[i].x;
        if (L.chips[i].x + L.chips[i].w > x1) x1 = L.chips[i].x + L.chips[i].w;
    }
    i32 y0 = L.search.y - 4, y1 = L.dots_y + 16;
    x0 -= 24; x1 += 24;                /* icon shadows */
    if (x0 < 0) x0 = 0;
    if (x1 > (i32)SCREEN_W) x1 = (i32)SCREEN_W;
    if (y1 > (i32)SCREEN_H) y1 = (i32)SCREEN_H;
    return rect_make(x0, y0, x1 - x0, y1 - y0);
}

/* Render draw_content() over the backdrop off-screen and keep the pixels that
 * differ from the backdrop: that is the content, as an ARGB sprite. */
static void render_layer(void) {
    free_layer();
    if (!s_backdrop_ok) return;
    i32 sw = (i32)SCREEN_W, sh = (i32)SCREEN_H;
    u32 *tmp = (u32 *)kmalloc((size_t)sw * (size_t)sh * 4u);
    if (!tmp) return;
    kmemset(tmp, 0, (size_t)sw * (size_t)TOPBAR_H * 4u);
    kmemcpy(tmp + (u32)sw * (u32)TOPBAR_H, s_backdrop, (size_t)s_backdrop_w * (size_t)s_backdrop_h * 4u);
    gfx_buffer_t off = { tmp, (u32)sw, (u32)sh, (u32)sw * 4u };
    gfx_buffer_t *prev = g_target;
    gfx_set_target(&off);
    draw_content(NULL);
    gfx_set_target(prev);

    rect_t r = content_bounds();
    s_layer = (u32 *)kmalloc((size_t)r.w * (size_t)r.h * 4u);
    if (s_layer) {
        for (i32 y = 0; y < r.h; y++) {
            const u32 *t = tmp + (u32)(r.y + y) * (u32)sw + (u32)r.x;
            const u32 *b = s_backdrop + (u32)(r.y + y - TOPBAR_H) * (u32)s_backdrop_w + (u32)r.x;
            u32 *o = s_layer + (u32)y * (u32)r.w;
            for (i32 x = 0; x < r.w; x++) o[x] = (t[x] == b[x]) ? 0u : (0xFF000000u | t[x]);
        }
        s_layer_r = r;
    }
    kfree(tmp);
}

bool launcher_visible(void) {
    return launcher_open || (s_anim_ready && (s_anim.pos > 0 || !spring_settled(&s_anim)));
}

bool launcher_animating(void) {
    return s_anim_ready && !spring_settled(&s_anim);
}

void launcher_draw(mouse_t *m) {
    u32 now = timer_ms();
    if (!s_anim_ready) { spring_init(&s_anim, 0, SPRING_POP_K, SPRING_POP_C); s_anim_ready = true; }

    if (launcher_open && !prev_launcher_open) {            /* opening */
        search_query[0] = '\0';
        search_len = 0;
        selected_index = 0;
        s_category = CAT_ALL;
        s_page = 0;
        s_kbd_used = false;
        launcher_update_matches();
        if (!s_backdrop_ok) build_backdrop();               /* reopening mid-close keeps it */
        render_layer();
        s_anim.k = SPRING_POP_K; s_anim.c = SPRING_POP_C;
        spring_set_target(&s_anim, SPRING_ONE);
        s_anim_ms = now;
    } else if (!launcher_open && prev_launcher_open) {     /* closing */
        render_layer();                                     /* as it looks right now */
        s_anim.k = SPRING_EXIT_K; s_anim.c = SPRING_EXIT_C;
        spring_set_target(&s_anim, 0);
        s_anim_ms = now;
    }
    prev_launcher_open = launcher_open;

    if (!s_backdrop_ok) return;
    spring_step(&s_anim, now - s_anim_ms);
    s_anim_ms = now;

    if (!launcher_open && spring_settled(&s_anim)) {        /* fully closed: release ~10 MB */
        free_layer();
        if (s_backdrop) { kfree(s_backdrop); s_backdrop = NULL; }
        s_backdrop_ok = false;
        return;
    }
    if (launcher_open && spring_settled(&s_anim)) {         /* at rest: live and interactive */
        free_layer();
        gfx_blit_argb(0, TOPBAR_H, s_backdrop, s_backdrop_w, s_backdrop_h,
                      0, TOPBAR_H, s_backdrop_w, s_backdrop_h);
        draw_content(m);
        return;
    }

    /* In motion. p may pass 1.0 briefly (the overshoot). */
    i32 p = s_anim.pos;
    i32 pc = p < 0 ? 0 : (p > SPRING_ONE ? SPRING_ONE : p);
    gfx_blend_image(0, TOPBAR_H, s_backdrop, s_backdrop_w, s_backdrop_h, (u8)((u32)pc * 255u / SPRING_ONE));
    if (!s_layer) return;
    u32 scale = (u32)(LN_SCALE_FROM + (i32)(((i64)(SPRING_ONE - LN_SCALE_FROM) * p) >> 16));
    u32 fade = (u32)pc * 3u / 2u;                           /* content is opaque by ~2/3 */
    if (fade > SPRING_ONE) fade = SPRING_ONE;
    gfx_blit_argb_scaled(s_layer_r.x + s_layer_r.w / 2, s_layer_r.y + s_layer_r.h / 2,
                         s_layer, s_layer_r.w, s_layer_r.h, scale, (u8)(fade * 255u / SPRING_ONE));
}

void launcher_handle_mouse(mouse_t *m) {
    if (!launcher_open || !m->left_clicked) return;
    ln_layout_t L;
    launcher_layout(&L);

    for (u32 i = 0; i < CAT_COUNT; i++) {
        if (!rect_contains(L.chips[i], m->x, m->y)) continue;
        s_category = i;
        s_page = 0;
        selected_index = 0;
        launcher_update_matches();
        return;
    }
    if (rect_contains(L.search, m->x, m->y)) return;
    for (i32 p = 0; L.pages > 1 && p < L.pages; p++) {
        rect_t d = dot_rect(&L, p);
        if (rect_contains(rect_make(d.x - 6, d.y - 8, d.w + 12, d.h + 16), m->x, m->y)) { s_page = (u32)p; return; }
    }
    u32 first_idx = s_page * (u32)L.per_page;
    for (i32 slot = 0; slot < L.per_page; slot++) {
        u32 idx = first_idx + (u32)slot;
        if (idx >= matched_count) break;
        if (rect_contains(tile_rect(&L, slot), m->x, m->y)) { launcher_activate(idx); return; }
    }
    /* A click anywhere else on the backdrop closes the launcher. */
    launcher_close();
}

/* Keep the selection on the visible page. */
static void follow_selection(void) {
    ln_layout_t L;
    launcher_layout(&L);
    if (L.per_page > 0) s_page = (u32)(selected_index / L.per_page);
}

void launcher_handle_key(char c) {
    if (!launcher_open) return;
    u8 k = (u8)c;

    if (c == 27) { launcher_close(); search_query[0] = '\0'; search_len = 0; return; }
    if (c == '\n' || c == 13) {
        launcher_update_matches();
        if (matched_count) launcher_activate((u32)selected_index);
        return;
    }
    if (matched_count) {
        int n = (int)matched_count;
        if (c == '	' || c == 14 || c == 16 || k == KEY_NAV_RIGHT || k == KEY_NAV_LEFT ||
            k == KEY_NAV_DOWN || k == KEY_NAV_UP) s_kbd_used = true;
        if (c == '\t' || c == 14 || k == KEY_NAV_RIGHT) { selected_index = (selected_index + 1) % n; follow_selection(); return; }
        if (c == 16 || k == KEY_NAV_LEFT) { selected_index = (selected_index - 1 + n) % n; follow_selection(); return; }
        if (k == KEY_NAV_DOWN) { if (selected_index + LN_COLS < n) selected_index += LN_COLS; follow_selection(); return; }
        if (k == KEY_NAV_UP)   { if (selected_index - LN_COLS >= 0) selected_index -= LN_COLS; follow_selection(); return; }
        if (k == KEY_NAV_PGDN || k == KEY_NAV_PGUP) {
            ln_layout_t L;
            launcher_layout(&L);
            if (k == KEY_NAV_PGDN && (i32)s_page + 1 < L.pages) s_page++;
            if (k == KEY_NAV_PGUP && s_page > 0) s_page--;
            selected_index = (int)(s_page * (u32)L.per_page);
            return;
        }
    }
    if (c == '\b') {
        if (search_len > 0) {
            search_query[--search_len] = '\0';
            selected_index = 0;
            s_page = 0;
            launcher_update_matches();
        }
        return;
    }
    if (c >= 32 && c <= 126 && search_len < 63) {
        search_query[search_len++] = c;
        search_query[search_len] = '\0';
        selected_index = 0;
        s_page = 0;
        launcher_update_matches();
    }
}
