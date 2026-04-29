/* CareOS v9 -- apps/app_pkgmgr.c -- Package Manager */
#include "apps_common.h"

#define PKG_TAB_INSTALLED  0
#define PKG_TAB_INSTALL    1
#define PKG_TAB_CREATE     2
#define PKG_TAB_ABOUT      3

/* Shared layout constants — used by both draw and click */
#define PKG_HDR_H      38
#define PKG_TAB_Y      (PKG_HDR_H + 2)
#define PKG_TAB_H      26
#define PKG_SEP_Y      (PKG_TAB_Y + PKG_TAB_H + 2)
#define PKG_CONTENT_Y  (PKG_SEP_Y + 1)
#define PKG_ROW_H      26
#define PKG_COL_H      20
#define PKG_SB_H       24

/* Create-tab sub-layout (relative to PKG_CONTENT_Y) */
#define PKG_CR_TITLE_H  28
#define PKG_CR_SEP_H    14
#define PKG_CR_LABEL_H  28
#define PKG_CR_BTN_Y    (PKG_CR_TITLE_H + PKG_CR_SEP_H + PKG_CR_LABEL_H)
#define PKG_CR_BTN_H    28

void app_pkgmgr_init(window_t *w){
    w->pkgmgr_tab = PKG_TAB_INSTALLED;
    w->pkgmgr_sel = 0;
    w->input_buf[0] = '\0';
    w->input_len = 0;
    kstrcpy(w->pkgmgr_status, "Ready");
}

void app_pkgmgr_draw(window_t *w){
    rect_t cr = wm_client_rect(w);
    i32 sc = (i32)GFX_FONT_SCALE;
    (void)sc;

    gfx_rect(cr.x, cr.y, cr.w, cr.h, COL_SURFACE);

    /* Header */
    gfx_gradient_rect(cr.x, cr.y, cr.w, PKG_HDR_H, g_theme->surface3, COL_SURFACE);
    gfx_hline(cr.x, cr.y + PKG_HDR_H, cr.w, COL_BORDER);
    gfx_str_ex(cr.x + 12, cr.y + 10, "Package Manager", COL_TEXT, COL_TRANSPARENT, FONT_H2);
    gfx_str_right(cr.x, cr.y + 12, cr.w - 12, ".care format", COL_PRIMARY, COL_TRANSPARENT);

    /* Tabs */
    const char *tabs[] = { "Installed", "Install", "Create", "About", NULL };
    i32 tab_w = (cr.w - 8) / 4;
    for (int i = 0; tabs[i]; i++) {
        i32 tx  = cr.x + 4 + i * tab_w;
        bool sel = ((u32)i == w->pkgmgr_tab);
        u32 bg  = sel ? COL_SELECTION : g_theme->surface2;
        gfx_rect_rounded(tx, cr.y + PKG_TAB_Y, tab_w - 4, PKG_TAB_H, 6, bg);
        if (sel) {
            gfx_rect_rounded_outline(tx, cr.y + PKG_TAB_Y, tab_w - 4, PKG_TAB_H, 6, COL_BORDER);
            gfx_rect_rounded(tx + 8, cr.y + PKG_TAB_Y + PKG_TAB_H - 3, tab_w - 20, 2, 1, COL_PRIMARY);
        }
        gfx_str_centered(tx, cr.y + PKG_TAB_Y + (PKG_TAB_H - FONT_H) / 2, tab_w - 4,
                         tabs[i], sel ? COL_TEXT : g_theme->dim, bg);
    }
    gfx_hline(cr.x, cr.y + PKG_SEP_Y, cr.w, COL_BORDER);

    i32 y          = cr.y + PKG_CONTENT_Y;
    i32 content_h  = cr.h - PKG_CONTENT_Y - PKG_SB_H;

    if (w->pkgmgr_tab == PKG_TAB_INSTALLED) {
        /* Column header row */
        gfx_rect(cr.x, y, cr.w, PKG_COL_H, g_theme->surface3);
        gfx_hline(cr.x, y + PKG_COL_H, cr.w, COL_BORDER);
        gfx_str(cr.x + 44, y + 4, "Name",        g_theme->dim, g_theme->surface3);
        gfx_str(cr.x + 144, y + 4, "Version",    g_theme->dim, g_theme->surface3);
        gfx_str(cr.x + 214, y + 4, "Category",   g_theme->dim, g_theme->surface3);
        gfx_str(cr.x + 314, y + 4, "Description",g_theme->dim, g_theme->surface3);
        y += PKG_COL_H + 1;

        u32 cnt = carepkg_count();
        u32 shown = 0;
        for (u32 i = 0; i < cnt && y < cr.y + PKG_CONTENT_Y + content_h; i++) {
            char name[32], ver[16], desc[128], cat[32]; bool installed;
            if (!carepkg_get_info(i, name, ver, desc, cat, &installed)) continue;
            if (!installed) continue;
            bool sel = (i == w->pkgmgr_sel);
            u32 bg = sel ? COL_SELECTION : (shown % 2 == 0 ? COL_SURFACE : COL_SURFACE2);
            gfx_rect(cr.x + 1, y, cr.w - 2, PKG_ROW_H, bg);
            if (sel) gfx_vline(cr.x + 1, y, PKG_ROW_H, COL_PRIMARY);
            gfx_rect_rounded(cr.x + 4, y + 5, 32, 14, 3, rgb(0x06, 0x3a, 0x1e));
            gfx_str(cr.x + 6, y + 7, ".care", COL_GREEN, rgb(0x06, 0x3a, 0x1e));
            gfx_str(cr.x + 42, y + 6, name, COL_TEXT, bg);
            gfx_str(cr.x + 144, y + 6, ver, COL_ACCENT, bg);
            gfx_str(cr.x + 214, y + 6, cat[0] ? cat : "Utility", g_theme->dim, bg);
            gfx_str_clipped(cr.x + 314, y + 6, cr.w - 390, desc, g_theme->muted, bg);
            /* Uninstall button */
            i32 rx = cr.x + cr.w - 74;
            u32 rbg = sel ? rgb(0x3a, 0x0a, 0x0a) : g_theme->surface3;
            gfx_rect_rounded(rx, y + 4, 68, PKG_ROW_H - 8, 4, rbg);
            gfx_str_centered(rx, y + 8, 68, "Uninstall", COL_RED, rbg);
            y += PKG_ROW_H;
            shown++;
        }
        if (shown == 0) {
            gfx_str(cr.x + 14, y + 12, "No packages installed.", g_theme->muted, COL_SURFACE);
            gfx_str(cr.x + 14, y + 28, "Switch to 'Install' tab to add packages.", g_theme->dim, COL_SURFACE);
        }

    } else if (w->pkgmgr_tab == PKG_TAB_INSTALL) {
        gfx_str(cr.x + 12, y + 4, "Browse VFS for .care files:", g_theme->dim, COL_SURFACE);
        y += 24;

        fs_node_t *tmpd = vfs_find(vfs_root(), "tmp");
        if (tmpd) {
            for (u32 i = 0; i < tmpd->child_count && y < cr.y + PKG_CONTENT_Y + content_h; i++) {
                fs_node_t *f = tmpd->children[i];
                const char *ext = kstrrchr(f->name, '.');
                bool is_care = ext && (kstrcmp(ext, ".care") == 0 || kstrcmp(ext, ".cpk") == 0);
                if (!is_care) continue;
                bool sel = ((u32)i == w->pkgmgr_sel);
                u32 bg = sel ? COL_SELECTION : COL_SURFACE2;
                gfx_rect(cr.x + 4, y, cr.w - 8, PKG_ROW_H, bg);
                if (sel) gfx_vline(cr.x + 4, y, PKG_ROW_H, COL_PRIMARY);
                gfx_rect_rounded(cr.x + 8, y + 5, 32, 16, 3, rgb(0x0a, 0x20, 0x3e));
                gfx_str(cr.x + 10, y + 8, ".care", COL_PRIMARY, rgb(0x0a, 0x20, 0x3e));
                gfx_str(cr.x + 46, y + 7, f->name, COL_TEXT, bg);
                char sz[12]; kutoa(f->size, sz, 10); kstrcat(sz, " B");
                gfx_str(cr.x + 200, y + 7, sz, g_theme->dim, bg);
                i32 ib_x = cr.x + cr.w - 72;
                u32 ib_bg = sel ? COL_PRIMARY : g_theme->surface3;
                gfx_rect_rounded(ib_x, y + 4, 66, PKG_ROW_H - 8, 4, ib_bg);
                gfx_str_centered(ib_x, y + 8, 66, "Install",
                                 sel ? COL_WHITE : COL_PRIMARY, ib_bg);
                y += PKG_ROW_H;
            }
        }
        gfx_str(cr.x + 12, cr.y + PKG_CONTENT_Y + content_h - 16,
                "Tip: Use terminal 'carepkg create <name>' to make a .care template",
                g_theme->muted, COL_SURFACE);

    } else if (w->pkgmgr_tab == PKG_TAB_CREATE) {
        gfx_str_ex(cr.x + 12, y, "Create a .care package template",
                   COL_TEXT, COL_SURFACE, FONT_H2);
        y += PKG_CR_TITLE_H;
        gfx_hline(cr.x + 8, y, cr.w - 16, COL_BORDER);
        y += PKG_CR_SEP_H;

        /* Name input row */
        gfx_str(cr.x + 12, y + 6, "Package name:", g_theme->dim, COL_SURFACE);
        i32 inp_x = cr.x + 128, inp_w = cr.w - 188;
        gfx_rect_rounded(inp_x, y, inp_w, 24, 4, COL_INPUT_BG);
        gfx_rect_rounded_outline(inp_x, y, inp_w, 24, 4, COL_BORDER);
        gfx_str(inp_x + 8, y + 6, w->input_buf, COL_TEXT, COL_TRANSPARENT);
        if ((timer_get_ticks() / 30) % 2 == 0) {
            i32 cx = inp_x + 8 + (i32)w->input_len * FONT_W;
            gfx_vline(cx, y + 4, 16, COL_TEXT);
        }
        y += PKG_CR_LABEL_H;

        /* Create button — y is now cr.y + PKG_CONTENT_Y + PKG_CR_BTN_Y */
        bool can = (w->input_len > 0);
        u32 btn_bg = can ? COL_PRIMARY : g_theme->surface3;
        gfx_rect_rounded(cr.x + 12, y, 120, PKG_CR_BTN_H, 6, btn_bg);
        gfx_str_centered(cr.x + 12, y + (PKG_CR_BTN_H - FONT_H) / 2, 120,
                         "Create Template", can ? COL_WHITE : g_theme->muted, btn_bg);
        y += PKG_CR_BTN_H + 18;

        gfx_hline(cr.x + 8, y, cr.w - 16, COL_BORDER);
        y += 10;
        gfx_str(cr.x + 12, y, "Output: /tmp/<name>.care", g_theme->dim, COL_SURFACE);
        y += 16;
        gfx_str(cr.x + 12, y, "Edit in Editor app, then install from 'Install' tab.",
                g_theme->muted, COL_SURFACE);

    } else { /* About */
        const char *lines[] = {
            ".care Package Format  CareOS v9",
            "",
            "A .care file is a plain-text bundle that can be",
            "installed with carepkg or the Package Manager UI.",
            "",
            "Format:",
            "  CARE 1.0",
            "  name=myapp",
            "  version=1.0.0",
            "  description=My app",
            "  author=You",
            "  exec=main",
            "  icon=generic",
            "  category=Utilities",
            "  permissions=fs.read,gui",
            "  ---FILES---",
            "  FILE main",
            "  #!/bin/sh",
            "  echo Hello!",
            "  ---ENDFILE---",
            "  ---END---",
            "",
            "Packages install to /apps/<name>/",
            "carepkg install / remove / list / info / create",
            NULL
        };
        for (int i = 0; lines[i] && y < cr.y + PKG_CONTENT_Y + content_h; i++) {
            u32 col = (lines[i][0] == '.' || lines[i][0] == 'c') ? COL_PRIMARY :
                      (lines[i][0] == ' ') ? COL_ACCENT : COL_TEXT;
            gfx_str(cr.x + 12, y, lines[i], col, COL_SURFACE);
            y += 15;
        }
    }

    /* Status bar */
    i32 sb_y = cr.y + cr.h - PKG_SB_H;
    gfx_rect(cr.x, sb_y, cr.w, PKG_SB_H, g_theme->surface3);
    gfx_hline(cr.x, sb_y, cr.w, COL_BORDER);
    if (w->pkgmgr_installing) {
        static const char *dot_frames[] = { "Installing", "Installing.", "Installing..", "Installing..." };
        const char *msg = dot_frames[(timer_get_ticks() / 20) % 4];
        gfx_str(cr.x + 12, sb_y + (PKG_SB_H - FONT_H) / 2, msg, COL_ACCENT, g_theme->surface3);
    } else {
        char status[64]; kstrcpy(status, "Status: "); kstrcat(status, w->pkgmgr_status);
        gfx_str(cr.x + 12, sb_y + (PKG_SB_H - FONT_H) / 2, status, g_theme->dim, g_theme->surface3);
    }
    char cnt_s[32]; kstrcpy(cnt_s, "Installed: ");
    char cnt[8]; kutoa(carepkg_count(), cnt, 10); kstrcat(cnt_s, cnt);
    gfx_str_right(cr.x, sb_y + (PKG_SB_H - FONT_H) / 2, cr.w - 12, cnt_s,
                  g_theme->dim, g_theme->surface3);
}

void app_pkgmgr_key(window_t *w, char c){
    if (w->pkgmgr_tab == PKG_TAB_CREATE) {
        if (c == '\b') {
            if (w->input_len > 0) { w->input_len--; w->input_buf[w->input_len] = '\0'; }
        } else if (c == '\n') {
            if (w->input_len > 0) {
                carepkg_run("create", w->input_buf);
                kstrcpy(w->pkgmgr_status, "Template: /tmp/");
                kstrcat(w->pkgmgr_status, w->input_buf);
                kstrcat(w->pkgmgr_status, ".care");
                w->input_buf[0] = '\0'; w->input_len = 0;
            }
        } else if (c >= 32 && c < 127 && w->input_len < 30) {
            w->input_buf[w->input_len++] = c;
            w->input_buf[w->input_len] = '\0';
        }
    }
}

void app_pkgmgr_click(window_t *w, i32 x, i32 y){
    rect_t cr = wm_client_rect(w);

    /* Tab bar */
    if (y >= cr.y + PKG_TAB_Y && y < cr.y + PKG_SEP_Y) {
        i32 tab_w = (cr.w - 8) / 4;
        int t = (x - cr.x - 4) / tab_w;
        if (t >= 0 && t <= 3) { w->pkgmgr_tab = (u32)t; w->pkgmgr_sel = 0; }
        return;
    }

    if (y < cr.y + PKG_CONTENT_Y) return;

    if (w->pkgmgr_tab == PKG_TAB_INSTALLED) {
        i32 list_y = cr.y + PKG_CONTENT_Y + PKG_COL_H + 1;
        if (y < list_y) return;
        u32 row = (u32)(y - list_y) / PKG_ROW_H;
        w->pkgmgr_sel = row;
        if (x >= cr.x + cr.w - 74) {
            u32 shown = 0;
            for (u32 i = 0; i < carepkg_count(); i++) {
                char name[32], ver[16], desc[128], cat[32]; bool installed;
                if (!carepkg_get_info(i, name, ver, desc, cat, &installed)) continue;
                if (!installed) continue;
                if (shown == row) {
                    w->pkgmgr_installing = true;
                    carepkg_remove(name);
                    w->pkgmgr_installing = false;
                    kstrcpy(w->pkgmgr_status, "Removed: "); kstrcat(w->pkgmgr_status, name);
                    break;
                }
                shown++;
            }
        }
    } else if (w->pkgmgr_tab == PKG_TAB_INSTALL) {
        i32 list_y = cr.y + PKG_CONTENT_Y + 24;
        if (y < list_y) return;
        u32 row = (u32)(y - list_y) / PKG_ROW_H;
        w->pkgmgr_sel = row;
        if (x >= cr.x + cr.w - 72) {
            fs_node_t *tmpd = vfs_find(vfs_root(), "tmp");
            if (tmpd) {
                u32 shown = 0;
                for (u32 i = 0; i < tmpd->child_count; i++) {
                    fs_node_t *f = tmpd->children[i];
                    const char *ext = kstrrchr(f->name, '.');
                    bool is_care = ext && (kstrcmp(ext,".care")==0 || kstrcmp(ext,".cpk")==0);
                    if (!is_care) continue;
                    if (shown == row) {
                        char path[FS_PATH_MAX]; kstrcpy(path, "/tmp/"); kstrcat(path, f->name);
                        w->pkgmgr_installing = true;
                        int r = carepkg_install(path);
                        w->pkgmgr_installing = false;
                        kstrcpy(w->pkgmgr_status, r == 0 ? "Installed: " : "Failed: ");
                        kstrcat(w->pkgmgr_status, f->name);
                        break;
                    }
                    shown++;
                }
            }
        }
    } else if (w->pkgmgr_tab == PKG_TAB_CREATE) {
        /* Create button — must match draw's y computation exactly */
        i32 btn_y = cr.y + PKG_CONTENT_Y + PKG_CR_BTN_Y;
        if (x >= cr.x + 12 && x < cr.x + 132 && y >= btn_y && y < btn_y + PKG_CR_BTN_H) {
            if (w->input_len > 0) {
                carepkg_run("create", w->input_buf);
                kstrcpy(w->pkgmgr_status, "Template: /tmp/");
                kstrcat(w->pkgmgr_status, w->input_buf);
                kstrcat(w->pkgmgr_status, ".care");
                w->input_buf[0] = '\0'; w->input_len = 0;
            }
        }
    }
}
