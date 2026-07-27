/* CareOS v9 -- gui/launcher.c
 *
 * Spotlight-style Launcher & Search Bar.
 * Renders the application registry (include/appdb.h) with fuzzy matching,
 * frosted glass styling, keyboard selection/navigation, and rich result views.
 */

#include "kernel.h"
#include "appdb.h"
#include "gui.h"
#include "image.h"
#include "resource_cache.h"
#include "icon.h"

#define LAUNCHER_MAX_TILES 16
#define LAUNCHER_MAX_MATCHES 64

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
};

/* icon token -> glyph. */
static const launcher_token_t launcher_icons[] = {
    { "terminal", APP_TERMINAL }, { "files",    APP_FILES    },
    { "editor",   APP_EDITOR   }, { "browser",  APP_BROWSER  },
    { "netmon",   APP_NETMON   }, { "settings", APP_SETTINGS },
    { "sysmon",   APP_SYSMON   }, { "users",    APP_USERS    },
    { "packages", APP_PKGMGR   }, { "calc",     APP_CALC     },
    { "clock",    APP_CLOCK    }, { "notes",    APP_NOTES    },
    { "paint",    APP_PAINT    }, { "about",    APP_ABOUT    },
    { "help",     APP_HELP     }, { "generic",  APP_PKGMGR   },
};

typedef struct {
    u32 app_index; /* Index in appdb */
    int score;     /* Fuzzy match score */
} matched_app_t;

/* Launcher & Spotlight Search State */
static char search_query[64] = {0};
static u32  search_len = 0;
static int  selected_index = 0;
static matched_app_t matched_apps[LAUNCHER_MAX_MATCHES];
static u32  matched_count = 0;
static bool prev_launcher_open = false;

static u32 launcher_strlen(const char *s) {
    if (!s) return 0;
    u32 len = 0;
    while (s[len]) len++;
    return len;
}

static char launcher_tolower(char c) {
    if (c >= 'A' && c <= 'Z') return (char)(c + 32);
    return c;
}

/* Calculate fuzzy match score between pattern (search query) and target string.
 * Returns 0 if pattern is not a match, or >0 score based on match quality. */
static int fuzzy_score_string(const char *pattern, const char *target) {
    if (!pattern || !target || pattern[0] == '\0') return 1;

    int score = 100;
    int p_idx = 0;
    int t_idx = 0;
    int consecutive = 0;
    int last_match_pos = -1;
    bool is_prefix = true;

    while (pattern[p_idx] && target[t_idx]) {
        char p = launcher_tolower(pattern[p_idx]);
        char t = launcher_tolower(target[t_idx]);

        if (p == t) {
            if (p_idx == 0 && t_idx == 0) {
                score += 300; /* Prefix match bonus */
            }
            if (t_idx == 0 || target[t_idx - 1] == ' ' || target[t_idx - 1] == '-' || 
                target[t_idx - 1] == '_' || target[t_idx - 1] == '/' || target[t_idx - 1] == ':') {
                score += 100; /* Word boundary bonus */
            }
            if (last_match_pos != -1 && t_idx == last_match_pos + 1) {
                consecutive++;
                score += 50 * consecutive; /* Consecutive character bonus */
            } else {
                consecutive = 0;
            }
            last_match_pos = t_idx;
            p_idx++;
        } else {
            if (p_idx == 0) is_prefix = false;
            score -= 3; /* Distance penalty */
        }
        t_idx++;
    }

    /* All query characters must be matched */
    if (pattern[p_idx] != '\0') return 0;

    if (t_idx == p_idx) score += 400; /* Exact match bonus */
    if (is_prefix) score += 200;

    int len_diff = (int)launcher_strlen(target) - p_idx;
    if (len_diff > 0) score -= len_diff * 2;

    return (score > 0) ? score : 1;
}

/* Evaluate total fuzzy score for an application entry across all searchable fields */
static int launcher_score_entry(const app_entry_t *entry, const char *query) {
    if (!entry) return 0;
    if (!query || query[0] == '\0') return 1;

    int score_name = fuzzy_score_string(query, entry->name);
    int score_id   = fuzzy_score_string(query, entry->id);
    int score_cat  = fuzzy_score_string(query, entry->category);
    int score_exec = fuzzy_score_string(query, entry->exec);

    int max_score = score_name * 3;
    if (score_id * 2 > max_score) max_score = score_id * 2;
    if (score_cat > max_score) max_score = score_cat;
    if (score_exec > max_score) max_score = score_exec;

    return max_score;
}

/* Update and sort fuzzy matched apps */
static void launcher_update_matches(void) {
    matched_count = 0;
    u32 total = appdb_count();
    if (total > LAUNCHER_MAX_MATCHES) total = LAUNCHER_MAX_MATCHES;

    for (u32 i = 0; i < total; i++) {
        const app_entry_t *entry = appdb_get(i);
        if (!entry) continue;

        int score = launcher_score_entry(entry, search_query);
        if (score > 0) {
            matched_apps[matched_count].app_index = i;
            matched_apps[matched_count].score = score;
            matched_count++;
        }
    }

    /* Sort matches descending by score */
    for (u32 i = 0; i < matched_count; i++) {
        for (u32 j = i + 1; j < matched_count; j++) {
            if (matched_apps[j].score > matched_apps[i].score) {
                matched_app_t tmp = matched_apps[i];
                matched_apps[i] = matched_apps[j];
                matched_apps[j] = tmp;
            }
        }
    }

    if (selected_index >= (int)matched_count) {
        selected_index = matched_count > 0 ? (int)matched_count - 1 : 0;
    }
    if (selected_index < 0) selected_index = 0;
}

static app_id_t launcher_lookup(const launcher_token_t *table, u32 n,
                                const char *token, app_id_t fallback) {
    if (!token) return fallback;
    for (u32 i = 0; i < n; i++)
        if (kstrcmp(table[i].token, token) == 0) return table[i].app;
    return fallback;
}

static app_id_t launcher_app_id(const app_entry_t *e) {
    return launcher_lookup(launcher_apps,
                           sizeof(launcher_apps) / sizeof(launcher_apps[0]),
                           appdb_builtin_token(e), APP_NONE);
}

static app_id_t launcher_icon_id(const app_entry_t *e) {
    return launcher_lookup(launcher_icons,
                           sizeof(launcher_icons) / sizeof(launcher_icons[0]),
                           e->icon, APP_PKGMGR);
}

static bool launcher_match(const app_entry_t *entry) {
    if (search_len == 0) return true;
    return launcher_score_entry(entry, search_query) > 0;
}

/* Open the app behind a registry entry */
static void launcher_open_entry(const app_entry_t *entry) {
    if (!entry) return;
    launcher_open = false;

    app_id_t app = launcher_app_id(entry);
    if (app != APP_NONE) {
        i32 scrw = (i32)SCREEN_W;
        i32 scrh = (i32)SCREEN_H;
        i32 ww, wh;
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

/* Single pass over launcher UI for rendering (draw=true) or hit-testing (draw=false) */
static int launcher_render(mouse_t *m, bool draw) {
    i32 sw = (i32)SCREEN_W;
    i32 sh = (i32)SCREEN_H;
    i32 sc = (i32)GFX_FONT_SCALE;

    /* Reset state on initial open */
    if (launcher_open && !prev_launcher_open) {
        search_query[0] = '\0';
        search_len = 0;
        selected_index = 0;
        launcher_update_matches();
    }
    prev_launcher_open = launcher_open;

    /* Fixed Panel Size */
    i32 pw = 640, ph = 520;
    if (sw < 700) pw = sw - 40;
    if (sh < 600) ph = sh - 100;
    i32 px = (sw - pw) / 2;
    i32 py = (sh - ph) / 2 - 20;

    if (draw) {
        /* Subtle full-screen scrim overlay */
        gfx_rect_blend(0, 0, sw, sh, COL_BLACK, 110);

        /* Main Panel: soft shadow + frosted glass container */
        gfx_shadow_soft(px, py, pw, ph, CDL_R_CARD);
        gfx_glass_panel(px, py, pw, ph, CDL_R_CARD);

        /* Spotlight Frosted Glass Search Bar */
        i32 search_x = px + 20;
        i32 search_y = py + 20;
        i32 search_w = pw - 40;
        i32 search_h = 50;

        gfx_glass_panel_ex(search_x, search_y, search_w, search_h, CDL_R_INPUT, COL_GLASS_TINT, 90, CDL_GLASS_BLUR);
        gfx_rect_rounded_outline(search_x, search_y, search_w, search_h, CDL_R_INPUT, COL_PRIMARY);

        /* Magnifying Glass Search Icon */
        i32 icon_cx = search_x + 24;
        i32 icon_cy = search_y + 24;
        gfx_circle(icon_cx, icon_cy, 6, COL_ACCENT);
        gfx_line(icon_cx + 4, icon_cy + 4, icon_cx + 9, icon_cy + 9, COL_ACCENT);

        /* Search input text / placeholder */
        i32 text_x = search_x + 42;
        i32 text_y = search_y + 17;

        if (search_len == 0) {
            gfx_str(text_x, text_y, "Spotlight Search... (type app or command)", COL_DIM, COL_TRANSPARENT);
        } else {
            gfx_str(text_x, text_y, search_query, COL_TEXT, COL_TRANSPARENT);
            /* Caret */
            if ((timer_get_ticks() / 40) % 2 == 0) {
                gfx_rect(text_x + (i32)search_len * FONT_W * sc, text_y - 1, 2, 18, COL_PRIMARY);
            }
        }

        /* Match count indicator */
        if (search_len > 0) {
            char badge[32];
            ksprintf(badge, "%u match%s", matched_count, matched_count == 1 ? "" : "es");
            gfx_str_right(search_x + search_w - 15, search_y + 17, 100, badge, COL_MUTED, COL_TRANSPARENT);
        }
    }

    int hit = -1;

    /* Mode 1: Default Grid View (Empty query) */
    if (search_len == 0) {
        i32 grid_x = px + 25;
        i32 grid_y = py + 85;
        i32 item_w = (pw - 50) / 4;
        i32 item_h = 100;
        i32 cols = 4;

        int visible_idx = 0;
        u32 total = appdb_count();

        for (u32 i = 0; i < total; i++) {
            const app_entry_t *entry = appdb_get(i);
            if (!entry) continue;

            i32 col = visible_idx % cols;
            i32 row = visible_idx / cols;
            rect_t tile = rect_make(grid_x + col * item_w, grid_y + row * item_h, item_w, item_h);

            bool hover = rect_contains(tile, m->x, m->y);
            bool is_sel = (selected_index == visible_idx);

            if (hover) {
                selected_index = visible_idx;
                is_sel = true;
                if (m->left_clicked) hit = (int)i;
            }

            if (draw) {
                if (hover || is_sel) {
                    gfx_rect_rounded(tile.x + 5, tile.y + 5, tile.w - 10, tile.h - 10, CDL_R_MENU, COL_HOVER);
                    gfx_rect_rounded_outline(tile.x + 5, tile.y + 5, tile.w - 10, tile.h - 10, CDL_R_MENU, COL_ACCENT);
                }

                i32 icon_size = 48;
                icon_draw(entry->icon, tile.x + (tile.w - icon_size) / 2, tile.y + 10,
                          icon_size, launcher_icon_id(entry),
                          (hover || is_sel) ? COL_PRIMARY : COL_ACCENT);
                gfx_str_centered(tile.x, tile.y + 65, tile.w, entry->name, COL_TEXT, COL_TRANSPARENT);
            }

            visible_idx++;
            if (visible_idx >= LAUNCHER_MAX_TILES) break;
        }
    }
    /* Mode 2: Spotlight List View (Active search query) */
    else {
        i32 list_x = px + 20;
        i32 list_y = py + 80;
        i32 list_w = pw - 40;
        i32 row_h = 52;
        i32 max_visible = (ph - 150) / row_h;
        if (max_visible < 1) max_visible = 1;

        if (matched_count == 0) {
            if (draw) {
                gfx_str_centered(px, py + ph / 2 - 10, pw, "No matching apps or commands found", COL_MUTED, COL_TRANSPARENT);
            }
        } else {
            for (u32 idx = 0; idx < matched_count && (i32)idx < max_visible; idx++) {
                u32 app_idx = matched_apps[idx].app_index;
                const app_entry_t *entry = appdb_get(app_idx);
                if (!entry) continue;

                rect_t row_rect = rect_make(list_x, list_y + (i32)idx * row_h, list_w, row_h - 4);
                bool hover = rect_contains(row_rect, m->x, m->y);
                bool is_sel = (selected_index == (int)idx);

                if (hover) {
                    selected_index = (int)idx;
                    is_sel = true;
                    if (m->left_clicked) hit = (int)app_idx;
                }

                if (draw) {
                    if (is_sel || hover) {
                        gfx_rect_rounded(row_rect.x, row_rect.y, row_rect.w, row_rect.h, CDL_R_BUTTON, COL_SELECTION);
                        gfx_rect_rounded_outline(row_rect.x, row_rect.y, row_rect.w, row_rect.h, CDL_R_BUTTON, COL_ACCENT);
                    } else {
                        gfx_rect_rounded(row_rect.x, row_rect.y, row_rect.w, row_rect.h, CDL_R_BUTTON, COL_SURFACE2);
                    }

                    /* Icon */
                    i32 icon_sz = 32;
                    icon_draw(entry->icon, row_rect.x + 12, row_rect.y + (row_rect.h - icon_sz) / 2,
                              icon_sz, launcher_icon_id(entry),
                              is_sel ? COL_PRIMARY : COL_ACCENT);

                    /* Title */
                    gfx_str(row_rect.x + 56, row_rect.y + 10, entry->name, COL_TEXT, COL_TRANSPARENT);

                    /* Subtitle: Category & Exec */
                    char sub[128];
                    if (entry->category[0]) {
                        ksprintf(sub, "%s  *  %s", entry->category, entry->exec);
                    } else {
                        ksprintf(sub, "%s", entry->exec);
                    }
                    gfx_str_clipped(row_rect.x + 56, row_rect.y + 28, row_rect.w - 180, sub, COL_DIM, COL_TRANSPARENT);

                    /* Launch hint for active selection */
                    if (is_sel) {
                        gfx_str_right(row_rect.x + row_rect.w - 16, row_rect.y + 17, 100, "[Enter] Launch", COL_ACCENT, COL_TRANSPARENT);
                    }
                }
            }
        }
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

        /* Navigation helper text */
        gfx_str_right(px + pw - 60, fy + 22, 250, "Spotlight active | Esc: Close", COL_MUTED, COL_TRANSPARENT);

        /* Power Button */
        i32 pwr_x = px + pw - 50;
        bool pwr_hover = rect_contains(rect_make(pwr_x - 5, fy + 15, 40, 40), m->x, m->y);
        gfx_rect_rounded(pwr_x - 5, fy + 15, 40, 40, CDL_R_BUTTON, pwr_hover ? COL_RED : COL_SURFACE2);
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

    int hit = launcher_render(m, false);
    if (hit >= 0) {
        launcher_open_entry(appdb_get((u32)hit));
    } else {
        /* Check if clicked outside launcher window */
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

    /* ESC: Close launcher */
    if (c == 27) {
        launcher_open = false;
        search_query[0] = '\0';
        search_len = 0;
        selected_index = 0;
        return;
    }

    /* Enter: Launch selected app */
    if (c == '\n' || c == 13) {
        launcher_update_matches();
        if (matched_count > 0 && selected_index >= 0 && selected_index < (int)matched_count) {
            u32 app_idx = matched_apps[selected_index].app_index;
            launcher_open_entry(appdb_get(app_idx));
        } else {
            u32 total = appdb_count();
            for (u32 i = 0; i < total; i++) {
                const app_entry_t *entry = appdb_get(i);
                if (entry && launcher_match(entry)) {
                    launcher_open_entry(entry);
                    return;
                }
            }
        }
        return;
    }

    /* Navigation: Tab or Ctrl+N (Down), Ctrl+P (Up) */
    if (c == '\t' || c == 14) {
        launcher_update_matches();
        if (matched_count > 0) {
            selected_index = (selected_index + 1) % (int)matched_count;
        }
        return;
    }
    if (c == 16) {
        launcher_update_matches();
        if (matched_count > 0) {
            selected_index = (selected_index - 1 + (int)matched_count) % (int)matched_count;
        }
        return;
    }

    /* Backspace */
    if (c == '\b') {
        if (search_len > 0) {
            search_query[--search_len] = '\0';
            selected_index = 0;
            launcher_update_matches();
        }
        return;
    }

    /* Input printable ASCII characters */
    if (c >= 32 && c <= 126 && search_len < 63) {
        search_query[search_len++] = c;
        search_query[search_len] = '\0';
        selected_index = 0;
        launcher_update_matches();
    }
}
