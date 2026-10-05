/* CareOS v9 -- apps/app_pkgmgr.c -- Packages (v10 Store, Phase 3 spec §6)
 *
 * CareOS has no package server, so the Store shows what really exists:
 * installed packages (carepkg's registry) and .care files on this computer
 * (Downloads, the home folder and /tmp). A file that is newer than the
 * installed copy is a real update. apps/store_model.c parses manifests and
 * decides the page state (host-tested). */
#include "apps_common.h"
#include "ui.h"
#include "store_model.h"

#define AVAIL_MAX 24
#define PHITS     96

enum { V_DISCOVER = 0, V_INSTALLED, V_UPDATES, V_CATEGORY };
enum {
    PA_NONE = 0, PA_VIEW, PA_CAT, PA_SEARCH, PA_GET, PA_UPDATE, PA_REMOVE, PA_UPDATE_ALL,
    PA_NEWPKG, PA_SHEET_OK, PA_SHEET_CANCEL, PA_SHOW_INSTALLED
};
typedef struct { rect_t r; u8 act; i32 arg; } phit_t;

typedef struct {
    care_manifest_t m;
    char path[FS_PATH_MAX];
    pkg_rel_t rel;
} avail_t;

typedef struct {
    avail_t avail[AVAIL_MAX]; u32 navail;
    char    cats[12][32]; u32 ncats;
    u32     view; i32 cat;
    char    search[32]; u32 search_len; bool search_focus;
    bool    sheet; char pname[32]; u32 pname_len;
    i32     remove_armed; u32 remove_ms;
    u32     scan_ms;
    phit_t  hits[PHITS]; u32 nhits;
} pstate_t;

static pstate_t *pst(window_t *w) {
    if (!w->app_state) {
        pstate_t *s = (pstate_t *)kmalloc(sizeof(pstate_t));
        if (!s) return NULL;
        kmemset(s, 0, sizeof *s);
        s->remove_armed = -1;
        w->app_state = s;
    }
    return (pstate_t *)w->app_state;
}
static void hit(pstate_t *s, rect_t r, u8 act, i32 arg) {
    if (s->nhits < PHITS) { s->hits[s->nhits].r = r; s->hits[s->nhits].act = act; s->hits[s->nhits].arg = arg; s->nhits++; }
}
static i32 lh(u32 slot) { return gfx_role_line_h(FONT_ROLE_UI, slot); }

/* -- Model ---------------------------------------------------------------------- */
/* The installed version of name, or NULL. */
static const char *installed_version(const char *name, char *buf) {
    for (u32 i = 0; i < carepkg_count(); i++) {
        char n[32], v[16], d[128], c[32]; bool inst;
        if (carepkg_get_info(i, n, v, d, c, &inst) && inst && !kstrcmp(n, name)) { kstrcpy(buf, v); return buf; }
    }
    return NULL;
}

static void add_cat(pstate_t *s, const char *c) {
    if (!c[0]) return;
    for (u32 i = 0; i < s->ncats; i++) if (!kstrcmp(s->cats[i], c)) return;
    if (s->ncats < 12) { kstrncpy(s->cats[s->ncats], c, 31); s->cats[s->ncats][31] = '\0'; s->ncats++; }
}

static void scan_dir(pstate_t *s, const char *dirpath) {
    fs_node_t *d = vfs_resolve_path(dirpath);
    if (!d || d->type != FS_DIR) return;
    for (u32 i = 0; i < d->child_count && s->navail < AVAIL_MAX; i++) {
        fs_node_t *f = d->children[i];
        const char *e = kstrrchr(f->name, '.');
        if (f->type != FS_FILE || !e || (kstrcmp(e, ".care") && kstrcmp(e, ".cpk"))) continue;
        avail_t *a = &s->avail[s->navail];
        if (!care_manifest_parse(vfs_file_str(f), f->size, &a->m)) continue;
        bool dup = false;                                  /* same package seen already: keep the newest */
        for (u32 k = 0; k < s->navail; k++)
            if (!kstrcmp(s->avail[k].m.name, a->m.name)) {
                dup = true;
                if (care_version_cmp(a->m.version, s->avail[k].m.version) > 0) {
                    s->avail[k].m = a->m; vfs_get_path(f, s->avail[k].path, FS_PATH_MAX);
                }
            }
        if (dup) continue;
        vfs_get_path(f, a->path, FS_PATH_MAX);
        s->navail++;
    }
}

static void store_scan(pstate_t *s) {
    s->navail = 0; s->ncats = 0;
    char home[64], dl[80];
    app_home_path(home);
    kstrcpy(dl, home); kstrcat(dl, "/Downloads");
    scan_dir(s, dl); scan_dir(s, home); scan_dir(s, "/tmp");
    for (u32 i = 0; i < s->navail; i++) {
        char v[16];
        s->avail[i].rel = store_relation(installed_version(s->avail[i].m.name, v), s->avail[i].m.version);
        add_cat(s, s->avail[i].m.category);
    }
    for (u32 i = 0; i < carepkg_count(); i++) {
        char n[32], v[16], d[128], c[32]; bool inst;
        if (carepkg_get_info(i, n, v, d, c, &inst) && inst) add_cat(s, c);
    }
    s->scan_ms = timer_ms();
}

static store_counts_t counts(pstate_t *s) {
    store_counts_t c = { 0, 0, 0 };
    for (u32 i = 0; i < carepkg_count(); i++) {
        char n[32], v[16], d[128], cat[32]; bool inst;
        if (carepkg_get_info(i, n, v, d, cat, &inst) && inst) c.installed++;
    }
    for (u32 i = 0; i < s->navail; i++) {
        if (s->avail[i].rel == PKG_NEW) c.fresh++;
        if (s->avail[i].rel == PKG_UPDATE) c.updates++;
    }
    return c;
}

static bool matches(pstate_t *s, const char *name, const char *desc, const char *cat) {
    if (s->view == V_CATEGORY && s->cat >= 0 && (u32)s->cat < s->ncats && kstrcmp(cat, s->cats[s->cat])) return false;
    if (!s->search_len) return true;
    for (u32 k = 0; k < 2; k++) {
        const char *hay = k ? desc : name;
        for (; *hay; hay++) {
            u32 i = 0;
            while (s->search[i] && hay[i]) {
                char a = hay[i], b = s->search[i];
                if (a >= 'A' && a <= 'Z') a += 32;
                if (b >= 'A' && b <= 'Z') b += 32;
                if (a != b) break;
                i++;
            }
            if (!s->search[i]) return true;
        }
    }
    return false;
}

/* -- Actions ---------------------------------------------------------------------- */
static void do_install(window_t *w, pstate_t *s, u32 i, bool update) {
    if (i >= s->navail) return;
    avail_t *a = &s->avail[i];
    w->pkgmgr_installing = true;
    if (update) carepkg_remove(a->m.name);                 /* reinstall would duplicate */
    int rc = carepkg_install(a->path);
    w->pkgmgr_installing = false;
    char msg[96];
    kstrcpy(msg, a->m.name); kstrcat(msg, rc == 0 ? (update ? " updated to " : " installed, version ") : " couldn't be installed");
    if (rc == 0) kstrcat(msg, a->m.version);
    notify_push("Packages", msg, rc == 0 ? g_theme->success : g_theme->error);
    store_scan(s);
}

/* -- Drawing ----------------------------------------------------------------------- */
typedef struct { u32 body, side, line, ink, ink2, muted, sel, field, card; bool dark; } ppal_t;
static ppal_t ppal(void) {
    ppal_t p;
    p.dark = g_theme->is_dark;
    p.body = g_theme->window_body; p.side = g_theme->surface_alt;
    p.line = p.dark ? rgb(0x26, 0x2d, 0x48) : rgb(0xe1, 0xe4, 0xee);
    p.ink = g_theme->ink; p.ink2 = g_theme->ink2; p.muted = g_theme->muted;
    p.sel = p.dark ? ui_mix(p.side, g_theme->care, 70) : rgb(0xff, 0xe3, 0xd9);
    p.field = p.dark ? rgb(0x10, 0x15, 0x29) : rgb(0xff, 0xff, 0xff);
    p.card = p.dark ? rgb(0x1a, 0x20, 0x38) : rgb(0xff, 0xff, 0xff);
    return p;
}

static const u32 CAT_COLS[6] = {
    rgb(0xff,0x7a,0x59), rgb(0x4f,0x5e,0xf2), rgb(0x1f,0xa5,0x62),
    rgb(0xf5,0xae,0x2e), rgb(0x8e,0x4d,0xf0), rgb(0x12,0x90,0xc2)
};
static u32 cat_col(const char *c) { u32 h = 0; for (; *c; c++) h = h * 31u + (u8)*c; return CAT_COLS[h % 6u]; }

static i32 pill(i32 x, i32 y, const char *label, u32 bg, u32 fg) {
    i32 w = gfx_str_role_width(label, FONT_ROLE_UI, 1, FONT_W_BOLD) + 28;
    gfx_rect_rounded(x - w, y, w, 34, 17, bg);
    gfx_str_role(x - w + 14, y + (34 - lh(1)) / 2, label, fg, FONT_ROLE_UI, 1, FONT_W_BOLD);
    return w;
}

/* One package card; returns nothing, registers the action. kind: 0 get, 1 update, 2 remove, 3 installed (no action) */
static void pkg_card(pstate_t *s, const ppal_t *p, i32 x, i32 y, i32 w, const char *name, const char *ver,
                     const char *desc, const char *cat, u32 kind, i32 arg) {
    i32 h = 76;
    gfx_rect_rounded(x, y, w, h, 18, p->card);
    if (!p->dark) gfx_rect_rounded_outline(x, y, w, h, 18, p->line);
    char l[2] = { name[0] >= 'a' && name[0] <= 'z' ? (char)(name[0] - 32) : name[0], '\0' };
    gfx_rect_rounded(x + 14, y + 16, 44, 44, 14, cat_col(cat[0] ? cat : name));
    i32 tw = gfx_str_role_width(l, FONT_ROLE_DISPLAY, 1, FONT_W_BOLD);
    gfx_str_role(x + 14 + (44 - tw) / 2, y + 16 + (44 - gfx_role_line_h(FONT_ROLE_DISPLAY, 1)) / 2, l, rgb(0xff, 0xff, 0xff), FONT_ROLE_DISPLAY, 1, FONT_W_BOLD);
    const char *lab = kind == 0 ? "Get" : kind == 1 ? "Update" : kind == 2 ? (s->remove_armed == arg && timer_ms() - s->remove_ms < 3000 ? "Sure?" : "Remove") : "Installed";
    u32 bg = kind == 0 ? g_theme->care : kind == 1 ? (p->dark ? p->ink : rgb(0x16, 0x1a, 0x2b)) : kind == 2 ? (lab[0] == 'S' ? g_theme->error : (p->dark ? rgb(0x2a, 0x33, 0x58) : rgb(0xee, 0xf0, 0xf6))) : 0;
    u32 fg = kind == 0 ? g_theme->on_care : kind == 1 ? (p->dark ? g_theme->ground : rgb(0xff, 0xff, 0xff)) : kind == 2 ? (lab[0] == 'S' ? rgb(0xff, 0xff, 0xff) : p->ink) : 0;
    i32 bw;
    if (kind == 3) {
        bw = gfx_str_role_width(lab, FONT_ROLE_UI, 1, FONT_W_BOLD) + 24;
        ui_glyph(UI_GLYPH_CHECK, x + w - 14 - bw, y + (h - 16) / 2, 16, g_theme->success);
        gfx_str_role(x + w - 14 - bw + 20, y + (h - lh(1)) / 2, lab, g_theme->success, FONT_ROLE_UI, 1, FONT_W_BOLD);
    } else {
        bw = pill(x + w - 14, y + (h - 34) / 2, lab, bg, fg);
        hit(s, rect_make(x + w - 14 - bw, y + (h - 34) / 2, bw, 34), kind == 0 ? PA_GET : kind == 1 ? PA_UPDATE : PA_REMOVE, arg);
    }
    i32 tx = x + 72, tmax = w - 72 - bw - 26;
    gfx_str_role_fit(tx, y + 16, tmax, name, p->ink, FONT_ROLE_UI, 2, FONT_W_BOLD);
    i32 nw = gfx_str_role_width(name, FONT_ROLE_UI, 2, FONT_W_BOLD);
    if (nw + 60 < tmax) gfx_str_role(tx + nw + 8, y + 16 + (lh(2) - lh(0)) / 2 + 1, ver, p->muted, FONT_ROLE_UI, 0, FONT_W_REGULAR);
    gfx_str_role_fit(tx, y + 16 + lh(2) + 2, tmax, desc[0] ? desc : "No description", p->muted, FONT_ROLE_UI, 1, FONT_W_REGULAR);
}

static void section_title(const ppal_t *p, i32 x, i32 y, const char *t) {
    gfx_str_role(x, y, t, p->ink, FONT_ROLE_DISPLAY, 1, FONT_W_BOLD);
}

void app_pkgmgr_init(window_t *w){
    w->pkgmgr_tab = 0; w->pkgmgr_sel = 0;
    w->input_buf[0] = '\0'; w->input_len = 0;
    kstrcpy(w->pkgmgr_status, "Ready");
    pstate_t *s = pst(w);
    if (s) { s->cat = -1; store_scan(s); }
}

void app_pkgmgr_draw(window_t *w){
    rect_t cr = wm_client_rect(w);
    pstate_t *s = pst(w);
    ppal_t p = ppal();
    gfx_rect(cr.x, cr.y, cr.w, cr.h, p.body);
    if (!s) return;
    s->nhits = 0;
    if (timer_ms() - s->scan_ms > 3000) store_scan(s);         /* new files appear on their own */
    store_counts_t c = counts(s);

    /* Sidebar */
    i32 sbw = cr.w >= 820 ? 230 : 190;
    gfx_rect(cr.x, cr.y, sbw, cr.h, p.side);
    gfx_vline(cr.x + sbw, cr.y, cr.h, p.line);
    i32 sx = cr.x + 12, sw = sbw - 24, y = cr.y + 16;
    {
        rect_t r = rect_make(sx, y, sw, 40);
        gfx_rect_rounded(r.x, r.y, r.w, r.h, 12, p.field);
        gfx_rect_rounded_outline(r.x, r.y, r.w, r.h, 12, s->search_focus ? g_theme->care : p.line);
        ui_glyph(UI_GLYPH_SEARCH, r.x + 12, r.y + 11, 18, p.muted);
        i32 ty = r.y + (40 - lh(2)) / 2;
        if (s->search_len) gfx_str_role_fit(r.x + 38, ty, r.w - 46, s->search, p.ink, FONT_ROLE_UI, 2, FONT_W_REGULAR);
        else gfx_str_role_fit(r.x + 38, ty, r.w - 46, "Search packages", p.muted, FONT_ROLE_UI, 2, FONT_W_REGULAR);
        if (s->search_focus && ((timer_ms() / 500) & 1) == 0) {
            i32 cx = r.x + 38 + (s->search_len ? gfx_str_role_width(s->search, FONT_ROLE_UI, 2, FONT_W_REGULAR) : 0);
            if (cx < r.x + r.w - 8) gfx_rect(cx, ty, 2, lh(2), g_theme->care);
        }
        hit(s, r, PA_SEARCH, 0);
        y += 52;
    }
    struct { const char *label; u32 view; i32 cat; u32 badge; } rows[16];
    u32 nr = 0;
    rows[nr].label = "Discover"; rows[nr].view = V_DISCOVER; rows[nr].cat = -1; rows[nr].badge = 0; nr++;
    for (u32 i = 0; i < s->ncats && nr < 13; i++) { rows[nr].label = s->cats[i]; rows[nr].view = V_CATEGORY; rows[nr].cat = (i32)i; rows[nr].badge = 0; nr++; }
    u32 sep = nr;
    rows[nr].label = "Installed"; rows[nr].view = V_INSTALLED; rows[nr].cat = -1; rows[nr].badge = 0; nr++;
    if (c.updates) { rows[nr].label = "Updates"; rows[nr].view = V_UPDATES; rows[nr].cat = -1; rows[nr].badge = c.updates; nr++; }
    for (u32 i = 0; i < nr; i++) {
        if (i == sep) { gfx_hline(sx + 6, y + 6, sw - 12, p.line); y += 14; }
        bool on = s->view == rows[i].view && (rows[i].view != V_CATEGORY || s->cat == rows[i].cat);
        if (on) gfx_rect_rounded(sx, y, sw, 40, 11, p.sel);
        gfx_str_role_fit(sx + 12, y + (40 - lh(2)) / 2, sw - 50, rows[i].label, p.ink, FONT_ROLE_UI, 2, on ? FONT_W_BOLD : FONT_W_REGULAR);
        if (rows[i].badge) {
            char b[8]; kutoa(rows[i].badge, b, 10);
            i32 bw = gfx_str_role_width(b, FONT_ROLE_UI, 0, FONT_W_BOLD) + 12; if (bw < 22) bw = 22;
            gfx_rect_rounded(sx + sw - 10 - bw, y + 9, bw, 22, 11, g_theme->care);
            gfx_str_role(sx + sw - 10 - bw + (bw - gfx_str_role_width(b, FONT_ROLE_UI, 0, FONT_W_BOLD)) / 2, y + 9 + (22 - lh(0)) / 2, b, g_theme->on_care, FONT_ROLE_UI, 0, FONT_W_BOLD);
        }
        hit(s, rect_make(sx, y, sw, 40), rows[i].view == V_CATEGORY ? PA_CAT : PA_VIEW, rows[i].view == V_CATEGORY ? rows[i].cat : (i32)rows[i].view);
        y += 42;
    }
    {   /* New package (template) */
        i32 by = cr.y + cr.h - 16 - 40;
        for (i32 dx = 0; dx < sw; dx += 8) { gfx_hline(sx + dx, by, 4, p.muted); gfx_hline(sx + dx, by + 39, 4, p.muted); }
        for (i32 dy = 0; dy < 40; dy += 8) { gfx_vline(sx, by + dy, 4, p.muted); gfx_vline(sx + sw - 1, by + dy, 4, p.muted); }
        ui_glyph(UI_GLYPH_PLUS, sx + 12, by + 12, 16, p.ink2);
        gfx_str_role(sx + 34, by + (40 - lh(2)) / 2, "New package", p.ink2, FONT_ROLE_UI, 2, FONT_W_BOLD);
        hit(s, rect_make(sx, by, sw, 40), PA_NEWPKG, 0);
    }

    /* Main */
    i32 mx = cr.x + sbw + 30, mw = cr.w - sbw - 60, my = cr.y + 26;
    gfx_set_clip(cr.x + sbw + 1, cr.y, cr.w - sbw - 1, cr.h);
    i32 cols = mw >= 900 ? 3 : mw >= 560 ? 2 : 1;
    i32 cw = (mw - (cols - 1) * 12) / cols;
    if (s->view == V_DISCOVER && !s->search_len) {
        /* Hero: how packages work here (there is no online store) */
        i32 hh = 180;
        gfx_rect_rounded(mx, my, mw, hh, 24, rgb(0x15, 0x1b, 0x2e));
        if (mw >= 640) ui_care_logo(mx + mw - 34 - 96, my + 22, 96);
        gfx_str_role(mx + 34, my + 30, "PACKAGES ON THIS COMPUTER", rgb(0xff, 0x9a, 0x76), FONT_ROLE_UI, 0, FONT_W_BOLD);
        gfx_str_role_fit(mx + 34, my + 30 + lh(0) + 8, mw - 220, "Add apps with .care packages.", rgb(0xee, 0xf1, 0xfa), FONT_ROLE_DISPLAY, 2, FONT_W_BOLD);
        gfx_str_role_wrap(mx + 34, my + 30 + lh(0) + 8 + gfx_role_line_h(FONT_ROLE_DISPLAY, 2) + 6, mw - 260,
                          "There's no online store yet. Any .care file in Downloads, your home folder or /tmp shows up here, ready to install.",
                          rgb(0xd5, 0xda, 0xea), FONT_ROLE_UI, 1, FONT_W_REGULAR, 2);
        i32 bw = gfx_str_role_width("See installed", FONT_ROLE_UI, 1, FONT_W_BOLD) + 28;
        pill(mx + 34 + bw, my + hh - 26 - 34, "See installed", rgb(0xff, 0xff, 0xff), rgb(0x16, 0x1a, 0x2b));
        hit(s, rect_make(mx + 34, my + hh - 26 - 34, bw, 34), PA_SHOW_INSTALLED, 0);
        my += hh + 22;
        /* Updates banner, only with real updates */
        if (store_state(c) == STORE_UPDATES) {
            i32 bh = 68;
            gfx_rect_rounded(mx, my, mw, bh, 18, p.dark ? rgb(0x1a, 0x20, 0x38) : rgb(0xf5, 0xf6, 0xfa));
            gfx_rect_rounded(mx + 14, my + 14, 40, 40, 12, g_theme->care);
            ui_glyph(UI_GLYPH_RESTART, mx + 24, my + 24, 20, g_theme->on_care);
            char t[48]; kutoa(c.updates, t, 10); kstrcat(t, c.updates == 1 ? " update ready" : " updates ready");
            gfx_str_role(mx + 68, my + 14, t, p.ink, FONT_ROLE_UI, 2, FONT_W_BOLD);
            gfx_str_role(mx + 68, my + 14 + lh(2) + 2, "Newer .care files than what's installed", p.muted, FONT_ROLE_UI, 1, FONT_W_REGULAR);
            i32 uw = gfx_str_role_width("Update all", FONT_ROLE_UI, 2, FONT_W_BOLD) + 36;
            gfx_rect_rounded(mx + mw - 14 - uw, my + 14, uw, 40, 12, p.dark ? p.ink : rgb(0x16, 0x1a, 0x2b));
            gfx_str_role(mx + mw - 14 - uw + 18, my + 14 + (40 - lh(2)) / 2, "Update all", p.dark ? g_theme->ground : rgb(0xff, 0xff, 0xff), FONT_ROLE_UI, 2, FONT_W_BOLD);
            hit(s, rect_make(mx + mw - 14 - uw, my + 14, uw, 40), PA_UPDATE_ALL, 0);
            my += bh + 22;
        }
        section_title(&p, mx, my, "Ready to install");
        my += gfx_role_line_h(FONT_ROLE_DISPLAY, 1) + 12;
        u32 k = 0;
        for (u32 i = 0; i < s->navail; i++) {
            if (s->avail[i].rel != PKG_NEW) continue;
            pkg_card(s, &p, mx + (i32)(k % (u32)cols) * (cw + 12), my + (i32)(k / (u32)cols) * 88, cw,
                     s->avail[i].m.name, s->avail[i].m.version, s->avail[i].m.description, s->avail[i].m.category, 0, (i32)i);
            k++;
        }
        if (!k) {
            gfx_str_role_wrap(mx, my, mw, store_state(c) == STORE_EMPTY
                ? "Nothing here yet. Download a .care file, or make one with New package."
                : "Everything found is already installed. New .care files will show up here.",
                p.muted, FONT_ROLE_UI, 2, FONT_W_REGULAR, 2);
        }
    } else {
        const char *title = s->search_len ? "Search" : s->view == V_INSTALLED ? "Installed" : s->view == V_UPDATES ? "Updates"
                          : (s->cat >= 0 && (u32)s->cat < s->ncats ? s->cats[s->cat] : "Packages");
        gfx_str_role(mx, my, title, p.ink, FONT_ROLE_DISPLAY, 2, FONT_W_BOLD);
        my += gfx_role_line_h(FONT_ROLE_DISPLAY, 2) + 16;
        u32 k = 0;
        bool show_inst = s->view != V_UPDATES;
        bool show_avail = s->view != V_INSTALLED;
        if (show_inst) {
            for (u32 i = 0; i < carepkg_count(); i++) {
                char n[32], v[16], d[128], ct[32]; bool inst;
                if (!carepkg_get_info(i, n, v, d, ct, &inst) || !inst || !matches(s, n, d, ct)) continue;
                pkg_card(s, &p, mx + (i32)(k % (u32)cols) * (cw + 12), my + (i32)(k / (u32)cols) * 88, cw, n, v, d, ct,
                         s->view == V_INSTALLED ? 2u : 3u, (i32)i);
                k++;
            }
        }
        if (show_avail) {
            for (u32 i = 0; i < s->navail; i++) {
                avail_t *a = &s->avail[i];
                if (a->rel == PKG_SAME || a->rel == PKG_OLDER) continue;
                if (s->view == V_UPDATES && a->rel != PKG_UPDATE) continue;
                if (s->view == V_CATEGORY && a->rel == PKG_UPDATE) continue;   /* the installed card covers it */
                if (!matches(s, a->m.name, a->m.description, a->m.category)) continue;
                pkg_card(s, &p, mx + (i32)(k % (u32)cols) * (cw + 12), my + (i32)(k / (u32)cols) * 88, cw,
                         a->m.name, a->m.version, a->m.description, a->m.category, a->rel == PKG_UPDATE ? 1u : 0u, (i32)i);
                k++;
            }
        }
        if (!k) gfx_str_role(mx, my, s->search_len ? "No packages match." : "Nothing here.", p.muted, FONT_ROLE_UI, 2, FONT_W_REGULAR);
    }
    gfx_clear_clip();

    /* New package sheet */
    if (s->sheet) {
        s->nhits = 0;
        i32 bx = cr.x + sbw + 1, bw2 = cr.w - sbw - 1;
        gfx_rect_rounded_blend(bx, cr.y, bw2, cr.h, 0, p.dark ? 0x000000 : 0x161a2b, p.dark ? 90 : 40);
        i32 shw = bw2 - 60 < 440 ? bw2 - 60 : 440, shh = 196;
        i32 shx = bx + (bw2 - shw) / 2, shy = cr.y + (cr.h - shh) / 3;
        gfx_shadow_soft(shx, shy, shw, shh, 20);
        gfx_rect_rounded(shx, shy, shw, shh, 20, p.card);
        gfx_str_role(shx + 22, shy + 20, "New package", p.ink, FONT_ROLE_DISPLAY, 1, FONT_W_BOLD);
        gfx_str_role_fit(shx + 22, shy + 20 + gfx_role_line_h(FONT_ROLE_DISPLAY, 1) + 2, shw - 44,
                         "Makes a .care template in /tmp to edit and install.", p.muted, FONT_ROLE_UI, 1, FONT_W_REGULAR);
        rect_t f = rect_make(shx + 22, shy + 78, shw - 44, 44);
        gfx_rect_rounded(f.x, f.y, f.w, f.h, 12, p.field);
        gfx_rect_rounded_outline(f.x, f.y, f.w, f.h, 12, g_theme->care);
        i32 ty = f.y + (44 - lh(2)) / 2;
        if (s->pname_len) gfx_str_role_fit(f.x + 12, ty, f.w - 24, s->pname, p.ink, FONT_ROLE_UI, 2, FONT_W_REGULAR);
        else gfx_str_role(f.x + 12, ty, "package-name", p.muted, FONT_ROLE_UI, 2, FONT_W_REGULAR);
        i32 by = shy + shh - 22 - 40;
        i32 okw = gfx_str_role_width("Create", FONT_ROLE_UI, 2, FONT_W_BOLD) + 32, cnw = gfx_str_role_width("Cancel", FONT_ROLE_UI, 2, FONT_W_BOLD) + 32;
        gfx_rect_rounded(shx + shw - 22 - okw, by, okw, 40, 12, g_theme->care);
        gfx_str_role(shx + shw - 22 - okw + 16, by + (40 - lh(2)) / 2, "Create", g_theme->on_care, FONT_ROLE_UI, 2, FONT_W_BOLD);
        gfx_rect_rounded(shx + shw - 30 - okw - cnw, by, cnw, 40, 12, p.dark ? rgb(0x2a, 0x33, 0x58) : rgb(0xee, 0xf0, 0xf6));
        gfx_str_role(shx + shw - 30 - okw - cnw + 16, by + (40 - lh(2)) / 2, "Cancel", p.ink, FONT_ROLE_UI, 2, FONT_W_BOLD);
        hit(s, rect_make(shx + shw - 22 - okw, by, okw, 40), PA_SHEET_OK, 0);
        hit(s, rect_make(shx + shw - 30 - okw - cnw, by, cnw, 40), PA_SHEET_CANCEL, 0);
    }
}

static void make_template(pstate_t *s) {
    if (!s->pname_len) return;
    carepkg_run("create", s->pname);
    char msg[80]; kstrcpy(msg, "Template: /tmp/"); kstrcat(msg, s->pname); kstrcat(msg, ".care");
    notify_push("Packages", msg, g_theme->success);
    s->sheet = false; s->pname_len = 0; s->pname[0] = '\0';
    store_scan(s);
}

void app_pkgmgr_key(window_t *w, char c){
    pstate_t *s = pst(w);
    if (!s) return;
    char *buf = s->sheet ? s->pname : s->search;
    u32 *len = s->sheet ? &s->pname_len : &s->search_len;
    u32 cap = s->sheet ? sizeof s->pname : sizeof s->search;
    if (!s->sheet && !s->search_focus) { if (c == 0x06) s->search_focus = true; return; }
    if (c == 0x1B) { if (s->sheet) s->sheet = false; else { *len = 0; buf[0] = '\0'; s->search_focus = false; } return; }
    if (c == '\n') { if (s->sheet) make_template(s); else s->search_focus = false; return; }
    if (c == '\b') { if (*len) buf[--*len] = '\0'; return; }
    bool ok = c >= 32 && c < 127 && (!s->sheet || c == '-' || c == '_' || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'));
    if (ok && *len + 1 < cap) { buf[(*len)++] = c; buf[*len] = '\0'; }
    (void)w;
}

void app_pkgmgr_click(window_t *w, i32 x, i32 y){
    pstate_t *s = pst(w);
    if (!s) return;
    for (u32 i = s->nhits; i-- > 0;) {
        const phit_t *h = &s->hits[i];
        if (!rect_contains(h->r, x, y)) continue;
        if (h->act != PA_SEARCH) s->search_focus = false;
        if (h->act != PA_REMOVE) s->remove_armed = -1;
        switch (h->act) {
        case PA_VIEW:   s->view = (u32)h->arg; s->cat = -1; break;
        case PA_CAT:    s->view = V_CATEGORY; s->cat = h->arg; break;
        case PA_SHOW_INSTALLED: s->view = V_INSTALLED; break;
        case PA_SEARCH: s->search_focus = true; break;
        case PA_GET:    do_install(w, s, (u32)h->arg, false); break;
        case PA_UPDATE: do_install(w, s, (u32)h->arg, true); break;
        case PA_UPDATE_ALL:
            for (u32 k = 0; k < s->navail; k++) if (s->avail[k].rel == PKG_UPDATE) do_install(w, s, k, true);
            break;
        case PA_REMOVE: {
            char n[32], v[16], d[128], ct[32]; bool inst;
            if (!carepkg_get_info((u32)h->arg, n, v, d, ct, &inst)) break;
            if (s->remove_armed == h->arg && timer_ms() - s->remove_ms < 3000) {
                int rc = carepkg_remove(n);
                notify_push("Packages", rc == 0 ? "Removed" : "Couldn't remove that package", rc == 0 ? g_theme->success : g_theme->error);
                s->remove_armed = -1;
                store_scan(s);
            } else { s->remove_armed = h->arg; s->remove_ms = timer_ms(); }
            break;
        }
        case PA_NEWPKG: s->sheet = true; s->pname_len = 0; s->pname[0] = '\0'; break;
        case PA_SHEET_OK: make_template(s); break;
        case PA_SHEET_CANCEL: s->sheet = false; break;
        default: break;
        }
        return;
    }
    s->search_focus = false;
}
