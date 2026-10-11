/* CareOS v9 -- apps/app_files.c -- Files (v10, Phase 3 spec §2)
 *
 * Sidebar: Favorites (only folders that exist) and Locations (the disk with
 * its real free space). Title-bar toolbar: back / forward, a breadcrumb,
 * search in this folder, grid / list, New. Body: 132 px tiles (coloured
 * folders, paper tiles tinted by type) or a list, a preview pane with the
 * first lines of text files (Care syntax colours) or an image thumbnail, and
 * a status bar. Draw records every clickable rect per window, and clicks
 * look the point up there. */
#include "apps_common.h"
#include "image.h"
#include "icon.h"
#include "ui.h"
#include "care_syntax.h"
#include "../kernel/ext2.h"

/* fm_mode stored in w->tab: 0=browse, 1=rename_input, 2=new_file_input, 3=new_folder_input */
#define FM_MODE_BROWSE   0
#define FM_MODE_RENAME   1
#define FM_MODE_NEWFILE  2
#define FM_MODE_NEWFOLD  3

void app_terminal_run(window_t *w, const char *cmd);

/* -- Per-window state (w->app_state) ---------------------------------------- */
enum {
    FA_NONE = 0, FA_PLACE, FA_ITEM, FA_BACK, FA_FWD, FA_CRUMB, FA_SEARCH, FA_VIEW,
    FA_NEW, FA_MENU_FOLDER, FA_MENU_FILE, FA_OPEN, FA_RUN, FA_SHEET_OK, FA_SHEET_CANCEL
};
typedef struct { rect_t r; u8 act; i16 arg; } fhit_t;
#define FHITS 96
#define FHIST 8
#define FPATH 64

typedef struct {
    fhit_t hits[FHITS]; u32 nhits;          /* client area, as last drawn */
    fhit_t tb[24];      u32 ntb;            /* title-bar toolbar */
    char   back[FHIST][FPATH]; u32 nback;
    char   fwd[FHIST][FPATH];  u32 nfwd;
    char   crumb[8][FPATH];    u32 ncrumb;
    char   place[8][FPATH];    u32 nplace;
    char   search[40]; u32 search_len; bool search_focus;
    bool   grid, menu_open;
    rect_t new_btn;
    u32    vis[32]; u32 nvis;               /* children shown (search filter) */
    i32    cols;                            /* grid columns, as last drawn */
    image_t *img; char img_path[FPATH]; bool img_tried;
} fstate_t;

static fstate_t *fst(window_t *w) {
    if (!w->app_state) {
        fstate_t *s = (fstate_t *)kmalloc(sizeof(fstate_t));
        if (!s) return NULL;
        kmemset(s, 0, sizeof *s);
        s->grid = true;
        w->app_state = s;
    }
    return (fstate_t *)w->app_state;
}

void app_files_close(window_t *w) {
    fstate_t *s = (fstate_t *)w->app_state;
    if (s && s->img) { image_free(s->img); s->img = NULL; }
}

static void hit(fhit_t *list, u32 *n, u32 cap, rect_t r, u8 act, i16 arg) {
    if (*n < cap) { list[*n].r = r; list[*n].act = act; list[*n].arg = arg; (*n)++; }
}
#define CHIT(s, r, a, g) hit((s)->hits, &(s)->nhits, FHITS, (r), (a), (g))
#define THIT(s, r, a, g) hit((s)->tb, &(s)->ntb, 24, (r), (a), (g))

/* -- Palette ---------------------------------------------------------------- */
typedef struct { u32 body, side, pane, line, ink, ink2, muted, sel, field; bool dark; } fpal_t;
static fpal_t fpal(void) {
    fpal_t p;
    p.dark = g_theme->is_dark;
    p.body = g_theme->window_body;
    p.side = g_theme->surface_alt;
    p.pane = p.dark ? rgb(0x11, 0x16, 0x29) : rgb(0xfb, 0xfb, 0xfd);
    p.line = p.dark ? rgb(0x26, 0x2d, 0x48) : rgb(0xe1, 0xe4, 0xee);
    p.ink = g_theme->ink; p.ink2 = g_theme->ink2; p.muted = g_theme->muted;
    p.sel = p.dark ? ui_mix(p.body, g_theme->care, 50) : rgb(0xff, 0xf0, 0xeb);
    p.field = p.dark ? rgb(0x10, 0x15, 0x29) : rgb(0xf5, 0xf6, 0xfa);
    return p;
}
static i32 lh(u32 slot) { return gfx_role_line_h(FONT_ROLE_UI, slot); }

/* -- File kinds --------------------------------------------------------------- */
typedef struct { const char *ext; const char *kind; u32 col, ink; bool text; } fkind_t;
static const fkind_t KINDS[] = {
    { ".cl",   "Care language script", rgb(0xff,0x7a,0x59), rgb(0x2a,0x12,0x0a), true  },
    { ".asm",  "Assembly source",      rgb(0x8c,0x9b,0xff), rgb(0x14,0x1a,0x3a), true  },
    { ".s",    "Assembly source",      rgb(0x8c,0x9b,0xff), rgb(0x14,0x1a,0x3a), true  },
    { ".md",   "Markdown text",        rgb(0xff,0xc2,0x4b), rgb(0x3a,0x2a,0x05), true  },
    { ".c",    "C source",             rgb(0x6c,0xb8,0xff), rgb(0x0a,0x24,0x40), true  },
    { ".h",    "C header",             rgb(0x6c,0xb8,0xff), rgb(0x0a,0x24,0x40), true  },
    { ".txt",  "Text file",            rgb(0xd5,0xda,0xea), rgb(0x2a,0x2f,0x45), true  },
    { ".care", "CareOS package",       rgb(0xc0,0x8b,0xff), rgb(0x22,0x10,0x4a), false },
    { ".iso",  "Disk image",           rgb(0xc0,0x8b,0xff), rgb(0x22,0x10,0x4a), false },
    { ".png",  "PNG image",            rgb(0xff,0x8f,0xa8), rgb(0x3a,0x0a,0x16), false },
    { ".bmp",  "Bitmap image",         rgb(0xff,0x8f,0xa8), rgb(0x3a,0x0a,0x16), false },
    { ".tga",  "TGA image",            rgb(0xff,0x8f,0xa8), rgb(0x3a,0x0a,0x16), false },
    { ".cri",  "CareOS image",         rgb(0xff,0x8f,0xa8), rgb(0x3a,0x0a,0x16), false },
    { ".qoi",  "QOI image",            rgb(0xff,0x8f,0xa8), rgb(0x3a,0x0a,0x16), false },
};
static const fkind_t KIND_MAKE  = { "",  "Makefile", rgb(0x7c,0xf0,0xb5), rgb(0x0a,0x3a,0x22), true };
static const fkind_t KIND_OTHER = { "",  "File",     rgb(0xdd,0xe1,0xec), rgb(0x4a,0x51,0x68), false };

static const fkind_t *fm_kind(const char *name) {
    if (!kstrcmp(name, "Makefile") || !kstrcmp(name, "makefile")) return &KIND_MAKE;
    const char *dot = kstrrchr(name, '.');
    if (dot && dot != name)
        for (u32 i = 0; i < sizeof KINDS / sizeof KINDS[0]; i++)
            if (!kstrcmp(dot, KINDS[i].ext)) return &KINDS[i];
    return &KIND_OTHER;
}
static bool fm_is_image(const fkind_t *k) { return k->col == rgb(0xff,0x8f,0xa8); }

/* Plain text by content: no NULs or stray control bytes in the first 512. */
static bool fm_looks_text(const fs_node_t *n) {
    if (!n->data || !n->size) return true;
    u32 m = n->size < 512 ? n->size : 512;
    for (u32 i = 0; i < m; i++) {
        u8 c = (u8)n->data[i];
        if (c == 0 || (c < 32 && c != '\n' && c != '\r' && c != '\t')) return false;
    }
    return true;
}

static void fm_ext_label(const char *name, char *out) {
    if (!kstrcmp(name, "Makefile") || !kstrcmp(name, "makefile")) { kstrcpy(out, "MAKE"); return; }
    const char *dot = kstrrchr(name, '.');
    u32 n = 0;
    if (dot && dot != name)
        for (const char *p = dot + 1; *p && n < 4; p++) out[n++] = (*p >= 'a' && *p <= 'z') ? (char)(*p - 32) : *p;
    out[n] = '\0';
}

/* Folder colours: favorites have their own; others hash the name. */
static const u32 FOLDER_PALETTE[6] = {
    rgb(0x4f,0x5e,0xf2), rgb(0x12,0x90,0xc2), rgb(0x1f,0xa5,0x62),
    rgb(0xf5,0xae,0x2e), rgb(0x8e,0x4d,0xf0), rgb(0xff,0x7a,0x59)
};
typedef struct { const char *label; const char *sub; u32 col; ui_glyph_t g; } fav_t;
static const fav_t FAVS[] = {
    { "Home",      "",          rgb(0x4f,0x5e,0xf2), UI_GLYPH_HOME },
    { "Desktop",   "Desktop",   rgb(0x12,0x90,0xc2), UI_GLYPH_GRID },
    { "Documents", "Documents", rgb(0xf5,0xae,0x2e), UI_GLYPH_FOLDER },
    { "Downloads", "Downloads", rgb(0x1f,0xa5,0x62), UI_GLYPH_CHEVRON_D },
    { "Pictures",  "Pictures",  rgb(0xe2,0x50,0x8f), UI_GLYPH_CAMERA },
    { "Projects",  "Projects",  rgb(0xff,0x7a,0x59), UI_GLYPH_FOLDER },
};
static u32 fm_folder_col(const fs_node_t *n) {
    for (u32 i = 1; i < sizeof FAVS / sizeof FAVS[0]; i++)
        if (!kstrcmp(n->name, FAVS[i].sub)) return FAVS[i].col;
    u32 h = 0;
    for (const char *p = n->name; *p; p++) h = h * 31u + (u8)*p;
    return FOLDER_PALETTE[h % 6u];
}

/* A folder (tab + body) or a paper tile with its type label; w is 64 at
 * full size, smaller in the list. */
static void fm_shape(i32 x, i32 y, i32 w, const fs_node_t *n) {
    if (n->type == FS_DIR) {
        u32 c = fm_folder_col(n);
        i32 bh = w * 46 / 64, th = w * 14 / 64, tw = w * 28 / 64;
        gfx_rect_rounded(x, y, tw, th + 6, w >= 40 ? 6 : 3, ui_mix(c, 0x000000, 50));
        gfx_rect_rounded(x, y + th - 2, w, bh, w >= 40 ? 10 : 4, c);
        return;
    }
    const fkind_t *k = fm_kind(n->name);
    i32 pw = w * 48 / 64, ph = w * 58 / 64, px = x + (w - pw) / 2;
    gfx_rect_rounded(px, y, pw, ph, w >= 40 ? 8 : 3, k->col);
    i32 fold = w * 14 / 64;
    gfx_rect_rounded(px + pw - fold, y, fold, fold, w >= 40 ? 4 : 2, ui_mix(k->col, 0xffffff, 110));
    if (w >= 40) {
        char ext[6]; fm_ext_label(n->name, ext);
        if (ext[0]) {
            i32 ew = gfx_str_role_width(ext, FONT_ROLE_MONO, 0, FONT_W_BOLD);
            gfx_str_role(px + (pw - ew) / 2, y + ph - gfx_role_line_h(FONT_ROLE_MONO, 0) - 6, ext, k->ink,
                         FONT_ROLE_MONO, 0, FONT_W_BOLD);
        }
    }
}

static void fm_size_str(u32 size, char *out) {
    char b[12];
    if (size >= 1024u * 1024u) { kutoa(size / (1024u * 1024u), out, 10); kstrcat(out, "."); kutoa((size % (1024u * 1024u)) * 10u / (1024u * 1024u), b, 10); kstrcat(out, b); kstrcat(out, " MB"); }
    else if (size >= 1024u) { kutoa(size / 1024u, out, 10); kstrcat(out, " KB"); }
    else { kutoa(size, out, 10); kstrcat(out, size == 1 ? " byte" : " bytes"); }
}

/* "~/x" inside the home folder, else the absolute path. */
static void fm_pretty_path(const fs_node_t *n, char *out) {
    char p[FPATH], home[FPATH];
    vfs_get_path((fs_node_t *)n, p, sizeof p); if (!p[0]) kstrcpy(p, "/");
    app_home_path(home);
    u32 hl = (u32)kstrlen(home);
    if (!kstrncmp(p, home, hl) && (p[hl] == '\0' || p[hl] == '/')) { out[0] = '~'; kstrcpy(out + 1, p + hl); }
    else kstrcpy(out, p);
}

static bool ci_contains(const char *hay, const char *needle) {
    if (!needle[0]) return true;
    for (; *hay; hay++) {
        u32 k = 0;
        while (needle[k]) {
            char a = hay[k], b = needle[k];
            if (!a) break;
            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b) break;
            k++;
        }
        if (!needle[k]) return true;
    }
    return false;
}

/* -- Navigation ---------------------------------------------------------------- */
static void fm_go(window_t *w, fs_node_t *d, bool record) {
    fstate_t *s = fst(w);
    if (!d || d->type != FS_DIR || d == w->fm_dir) return;
    if (s && record && w->fm_dir) {
        if (s->nback == FHIST) { for (u32 i = 1; i < FHIST; i++) kstrcpy(s->back[i - 1], s->back[i]); s->nback--; }
        vfs_get_path(w->fm_dir, s->back[s->nback], FPATH);
        if (!s->back[s->nback][0]) kstrcpy(s->back[s->nback], "/");
        s->nback++;
        s->nfwd = 0;
    }
    char p[FPATH]; vfs_get_path(d, p, sizeof p);
    if (p[0]) { fs_node_t *r = vfs_resolve_path(p); if (r) d = r; }   /* loads ext2 children */
    w->fm_dir = d; w->fm_sel = 0; w->scroll = 0;
    if (s) { s->search[0] = '\0'; s->search_len = 0; s->search_focus = false; }
}

static void fm_back(window_t *w, bool forward) {
    fstate_t *s = fst(w);
    if (!s) return;
    u32 *n = forward ? &s->nfwd : &s->nback;
    char (*stack)[FPATH] = forward ? s->fwd : s->back;
    char (*other)[FPATH] = forward ? s->back : s->fwd;
    u32 *on = forward ? &s->nback : &s->nfwd;
    if (*n == 0) return;
    fs_node_t *d = vfs_resolve_path(stack[*n - 1]);
    (*n)--;
    if (!d) return;
    if (*on == FHIST) { for (u32 i = 1; i < FHIST; i++) kstrcpy(other[i - 1], other[i]); (*on)--; }
    vfs_get_path(w->fm_dir, other[*on], FPATH);
    if (!other[*on][0]) kstrcpy(other[*on], "/");
    (*on)++;
    fm_go(w, d, false);
}

static void fm_up(window_t *w) { if (w->fm_dir && w->fm_dir->parent) fm_go(w, w->fm_dir->parent, true); }

static void fm_open_file(fs_node_t *ch) {
    window_t *ew = wm_open(APP_EDITOR, "Editor", 60, 40, (i32)SCREEN_W * 65 / 100, (i32)SCREEN_H * 70 / 100);
    if (!ew) return;
    vfs_get_path(ch, ew->editor_path, FS_PATH_MAX);        /* full path, or a save lands in / */
    win_clear(ew);
    if (ch->size > 0) win_append(ew, vfs_file_str(ch));
    ew->editor_modified = false;
}

static void fm_open(window_t *w, u32 idx) {
    if (!w->fm_dir || idx >= w->fm_dir->child_count) return;
    fs_node_t *ch = w->fm_dir->children[idx];
    if (ch->type == FS_DIR) { fm_go(w, ch, true); return; }
    const fkind_t *k = fm_kind(ch->name);
    if (k->text || fm_looks_text(ch)) fm_open_file(ch);
    else notify_push("Files", "No app opens this kind of file yet", g_theme->warning);
}

static void fm_run(window_t *w, u32 idx) {
    if (!w->fm_dir || idx >= w->fm_dir->child_count) return;
    char path[FPATH], cmd[80];
    vfs_get_path(w->fm_dir->children[idx], path, sizeof path);
    kstrcpy(cmd, "care "); kstrcat(cmd, path);
    window_t *tw = wm_open(APP_TERMINAL, "Terminal", 120, 80, (i32)SCREEN_W * 55 / 100, (i32)SCREEN_H * 60 / 100);
    if (tw) app_terminal_run(tw, cmd);
}

static void fm_begin_input(window_t *w, u32 mode) {
    w->tab = mode;
    w->input_buf[0] = '\0'; w->input_len = 0;
    if (mode == FM_MODE_RENAME && w->fm_dir && w->fm_sel < w->fm_dir->child_count) {
        kstrncpy(w->input_buf, w->fm_dir->children[w->fm_sel]->name, 63);
        w->input_buf[63] = '\0';
        w->input_len = (u32)kstrlen(w->input_buf);
    }
}

static void fm_commit_input(window_t *w) {
    if (w->input_len > 0 && w->fm_dir) {
        if (w->tab == FM_MODE_RENAME && w->fm_sel < w->fm_dir->child_count) vfs_rename(w->fm_dir->children[w->fm_sel], w->input_buf);
        else if (w->tab == FM_MODE_NEWFILE) { if (!vfs_mkfile(w->fm_dir, w->input_buf)) notify_push("Files", "Couldn't create the file here", g_theme->error); }
        else if (w->tab == FM_MODE_NEWFOLD) { if (!vfs_mkdir(w->fm_dir, w->input_buf)) notify_push("Files", "Couldn't create the folder here", g_theme->error); }
    }
    w->tab = FM_MODE_BROWSE;
    w->input_buf[0] = '\0'; w->input_len = 0;
}

static void fm_delete_sel(window_t *w) {
    if (!w->fm_dir || w->fm_sel >= w->fm_dir->child_count) return;
    if (vfs_delete(w->fm_dir->children[w->fm_sel]) != 0) notify_push("Files", "Couldn't delete that", g_theme->error);
    if (w->fm_sel >= w->fm_dir->child_count && w->fm_sel > 0) w->fm_sel--;
}

/* -- Init ----------------------------------------------------------------------- */
void app_files_init(window_t *w){
    char home[64]; app_home_path(home);
    w->fm_dir = vfs_resolve_path(home);
    if(!w->fm_dir) w->fm_dir = vfs_root();
    w->fm_sel  = 0;
    w->tab     = FM_MODE_BROWSE;
    w->input_buf[0] = '\0';
    w->input_len = 0;
}

/* -- Toolbar (title bar) ---------------------------------------------------- */
bool app_files_toolbar(window_t *w, rect_t bar) {
    fstate_t *s = fst(w);
    if (!s) return false;
    fpal_t p = fpal();
    s->ntb = 0;
    i32 h = 36, y = bar.y + (bar.h - h) / 2;
    i32 x = bar.x;
    /* back / forward */
    for (i32 i = 0; i < 2; i++) {
        bool ok = i == 0 ? s->nback > 0 : s->nfwd > 0;
        ui_glyph(i == 0 ? UI_GLYPH_CHEVRON_L : UI_GLYPH_CHEVRON_R, x + 9, y + 9, 18, ok ? p.ink : p.muted);
        if (ok) THIT(s, rect_make(x, y, 36, h), i == 0 ? FA_BACK : FA_FWD, 0);
        x += 38;
    }
    x += 4;
    /* right side, right to left: New, view toggle, search */
    i32 rx = bar.x + bar.w;
    i32 nw = 28 + gfx_str_role_width("New", FONT_ROLE_UI, 2, FONT_W_BOLD) + 14;
    rx -= nw;
    u32 nbg = p.dark ? p.ink : rgb(0x16, 0x1a, 0x2b), nfg = p.dark ? g_theme->ground : rgb(0xff, 0xff, 0xff);
    gfx_rect_rounded(rx, y, nw, h, 10, nbg);
    ui_glyph(UI_GLYPH_PLUS, rx + 10, y + 10, 16, nfg);
    gfx_str_role(rx + 30, y + (h - lh(2)) / 2, "New", nfg, FONT_ROLE_UI, 2, FONT_W_BOLD);
    s->new_btn = rect_make(rx, y, nw, h);
    THIT(s, s->new_btn, FA_NEW, 0);
    rx -= 8 + 70;
    {
        i32 vx = rx, vy = y;
        gfx_rect_rounded(vx, vy, 70, h, 10, p.dark ? rgb(0x1c, 0x24, 0x40) : rgb(0xee, 0xf0, 0xf6));
        for (i32 i = 0; i < 2; i++) {
            bool on = (i == 0) == s->grid;
            i32 bx = vx + 3 + i * 32;
            if (on) gfx_rect_rounded(bx, vy + 3, 32, h - 6, 8, p.dark ? rgb(0x2a, 0x33, 0x58) : rgb(0xff, 0xff, 0xff));
            u32 c = on ? p.ink : p.muted;
            if (i == 0) {                       /* grid glyph */
                for (i32 q = 0; q < 4; q++) gfx_rect_rounded(bx + 9 + (q % 2) * 8, vy + 10 + (q / 2) * 8, 6, 6, 1, c);
            } else {                            /* list glyph */
                for (i32 q = 0; q < 3; q++) { gfx_rect(bx + 9, vy + 11 + q * 6, 2, 2, c); gfx_rect(bx + 13, vy + 11 + q * 6, 10, 2, c); }
            }
            THIT(s, rect_make(bx, vy, 32, h), FA_VIEW, (i16)i);
        }
    }
    i32 sw = 180;
    if (rx - 8 - sw < x + 160) sw = rx - 8 - (x + 160);
    if (sw >= 100) {
        rx -= 8 + sw;
        rect_t r = rect_make(rx, y, sw, h);
        gfx_rect_rounded(r.x, r.y, r.w, r.h, 10, p.field);
        gfx_rect_rounded_outline(r.x, r.y, r.w, r.h, 10, s->search_focus ? g_theme->care : p.line);
        ui_glyph(UI_GLYPH_SEARCH, r.x + 10, r.y + 10, 16, p.muted);
        i32 ty = r.y + (h - lh(2)) / 2;
        if (s->search_len) gfx_str_role_fit(r.x + 32, ty, r.w - 40, s->search, p.ink, FONT_ROLE_UI, 2, FONT_W_REGULAR);
        else {
            char ph[48]; kstrcpy(ph, "Search ");
            kstrncpy(ph + 7, w->fm_dir && w->fm_dir->name[0] ? w->fm_dir->name : "/", 40); ph[47] = '\0';
            gfx_str_role_fit(r.x + 32, ty, r.w - 40, ph, p.muted, FONT_ROLE_UI, 2, FONT_W_REGULAR);
        }
        if (s->search_focus && ((timer_ms() / 500) & 1) == 0) {
            i32 cx = r.x + 32 + (s->search_len ? gfx_str_role_width(s->search, FONT_ROLE_UI, 2, FONT_W_REGULAR) : 0);
            if (cx < r.x + r.w - 8) gfx_rect(cx, ty, 2, lh(2), g_theme->care);
        }
        THIT(s, r, FA_SEARCH, 0);
    }
    /* breadcrumb in the space left */
    i32 limit = rx - 12;
    char path[FPATH], home[FPATH];
    vfs_get_path(w->fm_dir, path, sizeof path); if (!path[0]) kstrcpy(path, "/");
    app_home_path(home);
    u32 hl = (u32)kstrlen(home);
    const char *rest = path;
    s->ncrumb = 0;
    if (!kstrncmp(path, home, hl) && (path[hl] == '\0' || path[hl] == '/')) {
        kstrcpy(s->crumb[s->ncrumb++], home);
        rest = path + hl;
    } else {
        kstrcpy(s->crumb[s->ncrumb++], "/");
    }
    while (*rest && s->ncrumb < 8) {
        while (*rest == '/') rest++;
        if (!*rest) break;
        const char *e = rest; while (*e && *e != '/') e++;
        u32 n = (u32)(e - path);
        if (n >= FPATH) n = FPATH - 1;
        kmemcpy(s->crumb[s->ncrumb], path, n); s->crumb[s->ncrumb][n] = '\0';
        s->ncrumb++;
        rest = e;
    }
    /* Show as many trailing segments as fit. */
    u32 first = 0;
    for (;;) {
        i32 tw = 0;
        for (u32 i = first; i < s->ncrumb; i++) {
            const char *nm = i == 0 ? (!kstrcmp(s->crumb[0], "/") ? "/" : user_current_name()) : kstrrchr(s->crumb[i], '/') + 1;
            bool last = i + 1 == s->ncrumb;
            tw += (last ? gfx_str_role_width(nm, FONT_ROLE_DISPLAY, 0, FONT_W_BOLD) : gfx_str_role_width(nm, FONT_ROLE_UI, 2, FONT_W_REGULAR)) + 16 + (last ? 0 : 18);
        }
        if (first > 0) tw += 24;
        if (x + tw <= limit || first + 1 >= s->ncrumb) break;
        first++;
    }
    if (first > 0) { gfx_str_role(x, y + (h - lh(2)) / 2, "...", p.muted, FONT_ROLE_UI, 2, FONT_W_REGULAR); x += 24; }
    for (u32 i = first; i < s->ncrumb && x < limit; i++) {
        bool last = i + 1 == s->ncrumb;
        const char *nm;
        if (i == 0) nm = !kstrcmp(s->crumb[0], "/") ? "/" : user_current_name();
        else nm = kstrrchr(s->crumb[i], '/') + 1;
        i32 tw = last ? gfx_str_role_width(nm, FONT_ROLE_DISPLAY, 0, FONT_W_BOLD) : gfx_str_role_width(nm, FONT_ROLE_UI, 2, FONT_W_REGULAR);
        i32 cw = tw + 16;
        if (x + cw > limit) cw = limit - x;
        if (last) gfx_str_role_fit(x + 8, y + (h - gfx_role_line_h(FONT_ROLE_DISPLAY, 0)) / 2, cw - 16, nm, p.ink, FONT_ROLE_DISPLAY, 0, FONT_W_BOLD);
        else {
            gfx_str_role_fit(x + 8, y + (h - lh(2)) / 2, cw - 16, nm, p.ink2, FONT_ROLE_UI, 2, FONT_W_REGULAR);
            THIT(s, rect_make(x, y + 2, cw, h - 4), FA_CRUMB, (i16)i);
        }
        x += cw;
        if (!last && x + 18 < limit) { ui_glyph(UI_GLYPH_CHEVRON_R, x + 1, y + 11, 14, p.muted); x += 18; }
    }
    return true;
}

bool app_files_toolbar_click(window_t *w, i32 x, i32 y) {
    fstate_t *s = fst(w);
    if (!s) return false;
    for (u32 i = 0; i < s->ntb; i++) {
        const fhit_t *h = &s->tb[i];
        if (!rect_contains(h->r, x, y)) continue;
        if (h->act != FA_NEW) s->menu_open = false;
        if (h->act != FA_SEARCH) s->search_focus = false;
        switch (h->act) {
        case FA_BACK:   fm_back(w, false); break;
        case FA_FWD:    fm_back(w, true); break;
        case FA_CRUMB:  if ((u32)h->arg < s->ncrumb) fm_go(w, vfs_resolve_path(s->crumb[h->arg]), true); break;
        case FA_SEARCH: s->search_focus = true; break;
        case FA_VIEW:   s->grid = h->arg == 0; w->scroll = 0; break;
        case FA_NEW:    s->menu_open = !s->menu_open; break;
        default: break;
        }
        return true;
    }
    s->menu_open = false;
    return false;
}

/* -- Body ------------------------------------------------------------------------ */
static void fm_sidebar(window_t *w, fstate_t *s, const fpal_t *p, rect_t cr, i32 sbw) {
    gfx_rect(cr.x, cr.y, sbw, cr.h, p->side);
    gfx_vline(cr.x + sbw, cr.y, cr.h, p->line);
    i32 x = cr.x + 12, iw = sbw - 24, y = cr.y + 14;
    char cur[FPATH]; vfs_get_path(w->fm_dir, cur, sizeof cur); if (!cur[0]) kstrcpy(cur, "/");
    char home[FPATH]; app_home_path(home);
    s->nplace = 0;
    bool any = false;
    for (u32 i = 0; i < sizeof FAVS / sizeof FAVS[0]; i++) {
        char path[FPATH]; kstrcpy(path, home);
        if (FAVS[i].sub[0]) { kstrcat(path, "/"); kstrcat(path, FAVS[i].sub); }
        fs_node_t *d = vfs_resolve_path(path);
        if (!d || d->type != FS_DIR) continue;                       /* only if it exists */
        if (!any) {                                                  /* heading only with entries */
            gfx_str_role(x + 10, y, "FAVORITES", p->muted, FONT_ROLE_UI, 0, FONT_W_BOLD);
            y += lh(0) + 8;
            any = true;
        }
        bool on = !kstrcmp(cur, path);
        if (on) gfx_rect_rounded(x, y, iw, 38, 11, p->sel);
        ui_glyph(FAVS[i].g, x + 10, y + 10, 18, FAVS[i].col);
        gfx_str_role_fit(x + 38, y + (38 - lh(2)) / 2, iw - 46, FAVS[i].label, p->ink, FONT_ROLE_UI, 2, on ? FONT_W_BOLD : FONT_W_REGULAR);
        if (s->nplace < 8) { kstrcpy(s->place[s->nplace], path); CHIT(s, rect_make(x, y, iw, 38), FA_PLACE, (i16)s->nplace); s->nplace++; }
        y += 40;
    }
    if (any) y += 12;
    gfx_str_role(x + 10, y, "LOCATIONS", p->muted, FONT_ROLE_UI, 0, FONT_W_BOLD);
    y += lh(0) + 8;
    {
        bool on = !kstrcmp(cur, "/");
        if (on) gfx_rect_rounded(x, y, iw, 38, 11, p->sel);
        ui_glyph(UI_GLYPH_WIRED, x + 10, y + 10, 18, p->ink2);
        gfx_str_role_fit(x + 38, y + (38 - lh(2)) / 2, iw - 46, "CareOS disk", p->ink, FONT_ROLE_UI, 2, on ? FONT_W_BOLD : FONT_W_REGULAR);
        if (s->nplace < 8) { kstrcpy(s->place[s->nplace], "/"); CHIT(s, rect_make(x, y, iw, 38), FA_PLACE, (i16)s->nplace); s->nplace++; }
        y += 40;
        u64 total = 0, freeb = 0;
        if (ext2_space(&total, &freeb) && total) {
            u32 pct = (u32)((total - freeb) * 100u / total);
            ui_progress(rect_make(x + 38, y, iw - 48, 5), pct, g_theme->calm);
            char a[16], b[16], line[48];
            u32 fm = (u32)(freeb >> 20), tm = (u32)(total >> 20);
            if (tm >= 1024) { kutoa(fm / 1024, a, 10); kstrcat(a, " GB"); kutoa(tm / 1024, b, 10); kstrcat(b, " GB"); }
            else { kutoa(fm, a, 10); kstrcat(a, " MB"); kutoa(tm, b, 10); kstrcat(b, " MB"); }
            kstrcpy(line, a); kstrcat(line, " free of "); kstrcat(line, b);
            gfx_str_role_fit(x + 38, y + 10, iw - 48, line, p->muted, FONT_ROLE_UI, 0, FONT_W_REGULAR);
        } else {
            gfx_str_role_fit(x + 38, y, iw - 48, "In memory (no disk mounted)", p->muted, FONT_ROLE_UI, 0, FONT_W_REGULAR);
        }
    }
}

/* Preview of a text file: first 12 lines, Care / C syntax colours. */
static i32 fm_preview_text(const fs_node_t *n, i32 x, i32 y, i32 w) {
    i32 mlh = gfx_role_line_h(FONT_ROLE_MONO, 0), step = mlh * 3 / 2;
    u32 nl = 1;
    for (u32 i = 0; n->data && i < n->size && nl < 12; i++) if (n->data[i] == '\n' && i + 1 < n->size) nl++;
    if (nl < 3) nl = 3;
    i32 boxh = 32 + (i32)nl * step;
    gfx_rect_rounded(x, y, w, boxh, 16, rgb(0x12, 0x17, 0x29));
    static const u32 COLS[CS_KIND_COUNT] = {
        rgb(0xd5,0xda,0xea), rgb(0xc0,0x8b,0xff), rgb(0x7c,0xf0,0xb5),
        rgb(0xff,0xc2,0x4b), rgb(0x8a,0x93,0xb0), rgb(0xd5,0xda,0xea)
    };
    cs_lang_t lang = care_syntax_lang(n->name);
    const char *d = n->data ? n->data : "";
    u32 pos = 0, line = 0;
    i32 ly = y + 16;
    gfx_set_clip(x + 12, y + 8, w - 24, boxh - 16);
    while (pos < n->size && line < 12) {
        u32 e = pos;
        while (e < n->size && d[e] != '\n') e++;
        u32 len = e - pos; if (len > 120) len = 120;
        char buf[124];
        for (u32 i = 0; i < len; i++) buf[i] = (d[pos + i] == '\t') ? ' ' : d[pos + i];
        cs_tok_t toks[48];
        u32 nt = care_syntax_line(lang, buf, len, toks, 48);
        if (nt > 48) nt = 48;
        i32 tx = x + 16;
        for (u32 t = 0; t < nt; t++) {
            char seg[124]; u32 sl = toks[t].len;
            kmemcpy(seg, buf + toks[t].start, sl); seg[sl] = '\0';
            gfx_str_role(tx, ly, seg, COLS[toks[t].kind], FONT_ROLE_MONO, 0, FONT_W_REGULAR);
            tx += gfx_str_role_width(seg, FONT_ROLE_MONO, 0, FONT_W_REGULAR);
        }
        ly += step; line++;
        pos = e + 1;
    }
    gfx_clear_clip();
    if (n->size == 0) gfx_str_role(x + 16, y + 16, "(empty file)", rgb(0x8a, 0x93, 0xb0), FONT_ROLE_MONO, 0, FONT_W_REGULAR);
    return boxh;
}

static void fm_preview(window_t *w, fstate_t *s, const fpal_t *p, i32 x, i32 y, i32 pw, i32 ph) {
    gfx_rect(x, y, pw, ph, p->pane);
    gfx_vline(x, y, ph, p->line);
    i32 ix = x + 22, iw = pw - 44, iy = y + 22;
    if (!w->fm_dir || w->fm_sel >= w->fm_dir->child_count) {
        gfx_str_role_wrap(ix, iy, iw, "Select a file to see it here.", p->muted, FONT_ROLE_UI, 2, FONT_W_REGULAR, 2);
        return;
    }
    fs_node_t *n = w->fm_dir->children[w->fm_sel];
    bool dir = n->type == FS_DIR;
    const fkind_t *k = fm_kind(n->name);
    bool text = !dir && (k->text || (k == &KIND_OTHER && fm_looks_text(n)));
    char path[FPATH]; vfs_get_path(n, path, sizeof path);

    if (dir) {
        fm_shape(ix + (iw - 96) / 2, iy + 10, 96, n);
        iy += 10 + 96 * 60 / 64 + 20;
    } else if (text) {
        iy += fm_preview_text(n, ix, iy, iw) + 18;
    } else if (fm_is_image(k)) {
        if (kstrcmp(s->img_path, path)) {
            if (s->img) { image_free(s->img); s->img = NULL; }
            kstrcpy(s->img_path, path);
            s->img = image_load(path);
        }
        if (s->img && s->img->width && s->img->height) {
            u32 sx = (u32)iw * 65536u / s->img->width, sy = 180u * 65536u / s->img->height;
            u32 sc = sx < sy ? sx : sy; if (sc > 65536u) sc = 65536u;
            i32 dh = (i32)((s->img->height * sc) >> 16);
            gfx_blit_argb_scaled(ix + iw / 2, iy + dh / 2, s->img->pixels, (i32)s->img->width, (i32)s->img->height, sc, 255);
            iy += dh + 18;
        } else {
            fm_shape(ix + (iw - 96) / 2, iy, 96, n);
            iy += 96 + 8;
            gfx_str_role_fit(ix, iy, iw, "This image can't be shown (unsupported format).", p->muted, FONT_ROLE_UI, 1, FONT_W_REGULAR);
            iy += lh(1) + 14;
        }
    } else {
        fm_shape(ix + (iw - 96) / 2, iy, 96, n);
        iy += 96 + 18;
    }
    gfx_str_role_fit(ix, iy, iw, n->name, p->ink, FONT_ROLE_DISPLAY, 1, FONT_W_BOLD);
    iy += gfx_role_line_h(FONT_ROLE_DISPLAY, 1);
    char sub[64];
    if (dir) {
        char cnt[12]; kutoa(n->child_count, cnt, 10);
        kstrcpy(sub, "Folder - "); kstrcat(sub, cnt);
        kstrcat(sub, n->child_count == 1 ? " item" : " items");
    } else {
        char sz[24]; fm_size_str(n->size, sz);
        kstrcpy(sub, text && k == &KIND_OTHER ? "Text file" : k->kind); kstrcat(sub, " - "); kstrcat(sub, sz);
    }
    gfx_str_role_fit(ix, iy, iw, sub, p->muted, FONT_ROLE_UI, 1, FONT_W_REGULAR);
    iy += lh(1) + 14;
    /* Location / Opens with */
    char loc[FPATH]; fm_pretty_path(w->fm_dir, loc);
    gfx_str_role(ix, iy, "Location", p->muted, FONT_ROLE_UI, 1, FONT_W_REGULAR);
    gfx_str_role_fit(ix + 96, iy, iw - 96, loc, p->ink, FONT_ROLE_MONO, 0, FONT_W_BOLD);
    iy += lh(1) + 8;
    const char *opens = dir ? "Files" : text ? "Editor" : NULL;
    if (opens) {
        gfx_str_role(ix, iy, "Opens with", p->muted, FONT_ROLE_UI, 1, FONT_W_REGULAR);
        gfx_str_role(ix + 96, iy, opens, p->ink, FONT_ROLE_UI, 1, FONT_W_BOLD);
        iy += lh(1) + 8;
    }
    /* Buttons, at the bottom */
    bool care = !dir && care_syntax_lang(n->name) == CS_LANG_CARE;
    /* Open, with Run in Terminal stacked under it for Care scripts (the pane
     * is too narrow for both labels side by side). */
    i32 by = y + ph - 22 - (care ? 44 + 8 + 44 : 44);
    if (by < iy + 8) by = iy + 8;
    if (opens) {
        gfx_rect_rounded(ix, by, iw, 44, 12, g_theme->care);
        i32 tw = gfx_str_role_width("Open", FONT_ROLE_UI, 2, FONT_W_BOLD);
        gfx_str_role(ix + (iw - tw) / 2, by + (44 - lh(2)) / 2, "Open", g_theme->on_care, FONT_ROLE_UI, 2, FONT_W_BOLD);
        CHIT(s, rect_make(ix, by, iw, 44), FA_OPEN, 0);
        if (care) {
            i32 ry = by + 44 + 8;
            gfx_rect_rounded(ix, ry, iw, 44, 12, p->dark ? rgb(0x1c, 0x24, 0x40) : rgb(0xee, 0xf0, 0xf6));
            tw = gfx_str_role_width("Run in Terminal", FONT_ROLE_UI, 2, FONT_W_BOLD);
            gfx_str_role(ix + (iw - tw) / 2, ry + (44 - lh(2)) / 2, "Run in Terminal", p->ink, FONT_ROLE_UI, 2, FONT_W_BOLD);
            CHIT(s, rect_make(ix, ry, iw, 44), FA_RUN, 0);
        }
    }
}

static void fm_sheet(window_t *w, fstate_t *s, const fpal_t *p, i32 x, i32 y, i32 mw, i32 mh) {
    i32 cw = mw - 60 < 420 ? mw - 60 : 420, ch = 176;
    i32 cx = x + (mw - cw) / 2, cy = y + (mh - ch) / 3;
    gfx_rect_rounded_blend(x, y, mw, mh, 0, p->dark ? 0x000000 : 0x161a2b, p->dark ? 90 : 40);
    gfx_shadow_soft(cx, cy, cw, ch, 20);
    gfx_rect_rounded(cx, cy, cw, ch, 20, p->dark ? rgb(0x1a, 0x20, 0x38) : rgb(0xff, 0xff, 0xff));
    const char *title = w->tab == FM_MODE_RENAME ? "Rename" : w->tab == FM_MODE_NEWFILE ? "New file" : "New folder";
    gfx_str_role(cx + 22, cy + 20, title, p->ink, FONT_ROLE_DISPLAY, 1, FONT_W_BOLD);
    rect_t f = rect_make(cx + 22, cy + 20 + gfx_role_line_h(FONT_ROLE_DISPLAY, 1) + 10, cw - 44, 44);
    gfx_rect_rounded(f.x, f.y, f.w, f.h, 12, p->field);
    gfx_rect_rounded_outline(f.x, f.y, f.w, f.h, 12, g_theme->care);
    i32 ty = f.y + (f.h - lh(2)) / 2;
    gfx_str_role_fit(f.x + 12, ty, f.w - 24, w->input_buf, p->ink, FONT_ROLE_UI, 2, FONT_W_REGULAR);
    if (((timer_ms() / 500) & 1) == 0) {
        i32 cxp = f.x + 12 + gfx_str_role_width(w->input_buf, FONT_ROLE_UI, 2, FONT_W_REGULAR);
        if (cxp < f.x + f.w - 8) gfx_rect(cxp, ty, 2, lh(2), g_theme->care);
    }
    i32 by = cy + ch - 22 - 40;
    const char *ok = w->tab == FM_MODE_RENAME ? "Rename" : "Create";
    i32 okw = gfx_str_role_width(ok, FONT_ROLE_UI, 2, FONT_W_BOLD) + 32;
    i32 cnw = gfx_str_role_width("Cancel", FONT_ROLE_UI, 2, FONT_W_BOLD) + 32;
    i32 okx = cx + cw - 22 - okw, cnx = okx - 8 - cnw;
    gfx_rect_rounded(okx, by, okw, 40, 12, g_theme->care);
    gfx_str_role(okx + 16, by + (40 - lh(2)) / 2, ok, g_theme->on_care, FONT_ROLE_UI, 2, FONT_W_BOLD);
    gfx_rect_rounded(cnx, by, cnw, 40, 12, p->dark ? rgb(0x2a, 0x33, 0x58) : rgb(0xee, 0xf0, 0xf6));
    gfx_str_role(cnx + 16, by + (40 - lh(2)) / 2, "Cancel", p->ink, FONT_ROLE_UI, 2, FONT_W_BOLD);
    gfx_str_role_fit(cx + 22, by + (40 - lh(0)) / 2, cnx - cx - 30, "Enter to confirm", p->muted, FONT_ROLE_UI, 0, FONT_W_REGULAR);
    CHIT(s, rect_make(okx, by, okw, 40), FA_SHEET_OK, 0);
    CHIT(s, rect_make(cnx, by, cnw, 40), FA_SHEET_CANCEL, 0);
}

void app_files_draw(window_t *w){
    rect_t cr = wm_client_rect(w);
    fstate_t *s = fst(w);
    fpal_t p = fpal();
    gfx_rect(cr.x, cr.y, cr.w, cr.h, p.body);
    if (!s) return;
    s->nhits = 0;
    if (!w->fm_dir) w->fm_dir = vfs_root();

    i32 sbw = cr.w >= 760 ? 230 : 180;
    fm_sidebar(w, s, &p, cr, sbw);

    i32 mx = cr.x + sbw + 1, mw = cr.w - sbw - 1;
    i32 pw = mw >= 740 ? 300 : 0;
    i32 gw = mw - pw;
    i32 status_h = 36;
    i32 gy0 = cr.y, gh = cr.h - status_h;

    /* Visible children (search filter) */
    s->nvis = 0;
    for (u32 i = 0; i < w->fm_dir->child_count && s->nvis < 32; i++)
        if (ci_contains(w->fm_dir->children[i]->name, s->search)) s->vis[s->nvis++] = i;
    if (s->nvis && w->fm_sel >= w->fm_dir->child_count) w->fm_sel = s->vis[0];

    /* Items */
    i32 pad = gw >= 500 ? 22 : 12;
    i32 ax = mx + pad, aw = gw - 2 * pad, ay = gy0 + pad;
    gfx_set_clip(mx, gy0, gw, gh);
    if (s->nvis == 0) {
        const char *msg = s->search_len ? "Nothing here matches your search." : "This folder is empty.";
        const char *sub = s->search_len ? "Try a shorter word, or press Esc to clear it." : "Use New to add a file or folder.";
        i32 tw = gfx_str_role_width(msg, FONT_ROLE_UI, 3, FONT_W_BOLD);
        gfx_str_role(mx + (gw - tw) / 2, gy0 + gh / 2 - lh(3), msg, p.ink, FONT_ROLE_UI, 3, FONT_W_BOLD);
        tw = gfx_str_role_width(sub, FONT_ROLE_UI, 1, FONT_W_REGULAR);
        gfx_str_role(mx + (gw - tw) / 2, gy0 + gh / 2 + 4, sub, p.muted, FONT_ROLE_UI, 1, FONT_W_REGULAR);
    } else if (s->grid) {
        i32 gap = 12, th = 132;
        i32 cols = (aw + gap) / (120 + gap); if (cols < 1) cols = 1;
        s->cols = cols;
        i32 tw = (aw - (cols - 1) * gap) / cols;
        i32 rows_vis = (gh - pad) / (th + gap); if (rows_vis < 1) rows_vis = 1;
        u32 rows = (s->nvis + (u32)cols - 1) / (u32)cols;
        u32 maxs = rows > (u32)rows_vis ? rows - (u32)rows_vis : 0;
        if (w->scroll > maxs) w->scroll = maxs;
        for (u32 k = 0; k < s->nvis; k++) {
            i32 row = (i32)(k / (u32)cols) - (i32)w->scroll, col = (i32)(k % (u32)cols);
            if (row < 0) continue;
            i32 tx = ax + col * (tw + gap), ty = ay + row * (th + gap);
            if (ty > gy0 + gh) break;
            u32 idx = s->vis[k];
            fs_node_t *n = w->fm_dir->children[idx];
            bool sel = idx == w->fm_sel;
            if (sel) {
                gfx_rect_rounded(tx, ty, tw, th, 16, p.sel);
                gfx_rect_rounded_outline(tx, ty, tw, th, 16, g_theme->care);
                gfx_rect_rounded_outline(tx + 1, ty + 1, tw - 2, th - 2, 15, g_theme->care);
            }
            fm_shape(tx + (tw - 64) / 2, ty + 18, 64, n);
            gfx_str_role_fit(tx + 8 + (tw - 16 - gfx_str_role_width(n->name, FONT_ROLE_UI, 1, FONT_W_BOLD) > 0 ? (tw - 16 - gfx_str_role_width(n->name, FONT_ROLE_UI, 1, FONT_W_BOLD)) / 2 : 0),
                             ty + 18 + 64 + 14, tw - 16, n->name, p.ink, FONT_ROLE_UI, 1, FONT_W_BOLD);
            CHIT(s, rect_make(tx, ty, tw, th), FA_ITEM, (i16)idx);
        }
    } else {
        s->cols = 1;
        i32 rh = 40;
        /* header */
        i32 kx = ax + aw * 55 / 100, sx = ax + aw - 90;
        gfx_str_role(ax + 44, ay, "Name", p.muted, FONT_ROLE_UI, 0, FONT_W_BOLD);
        if (aw >= 420) gfx_str_role(kx, ay, "Kind", p.muted, FONT_ROLE_UI, 0, FONT_W_BOLD);
        gfx_str_role(sx, ay, "Size", p.muted, FONT_ROLE_UI, 0, FONT_W_BOLD);
        i32 ly = ay + lh(0) + 8;
        i32 vis_rows = (gy0 + gh - ly) / rh; if (vis_rows < 1) vis_rows = 1;
        u32 maxs = s->nvis > (u32)vis_rows ? s->nvis - (u32)vis_rows : 0;
        if (w->scroll > maxs) w->scroll = maxs;
        for (u32 k = w->scroll; k < s->nvis && ly < gy0 + gh; k++) {
            u32 idx = s->vis[k];
            fs_node_t *n = w->fm_dir->children[idx];
            bool sel = idx == w->fm_sel;
            if (sel) gfx_rect_rounded(ax - 6, ly, aw + 12, rh, 10, p.sel);
            else if ((k & 1) == 0) gfx_rect_rounded(ax - 6, ly, aw + 12, rh, 10, p.dark ? rgb(0x12, 0x17, 0x2a) : rgb(0xf8, 0xf9, 0xfb));
            fm_shape(ax + 4, ly + 8, 26, n);
            gfx_str_role_fit(ax + 44, ly + (rh - lh(2)) / 2, (aw >= 420 ? kx : sx) - ax - 56, n->name, p.ink, FONT_ROLE_UI, 2, sel ? FONT_W_BOLD : FONT_W_REGULAR);
            const fkind_t *kd = fm_kind(n->name);
            if (aw >= 420) gfx_str_role_fit(kx, ly + (rh - lh(1)) / 2, sx - kx - 12, n->type == FS_DIR ? "Folder" : kd->kind, p.ink2, FONT_ROLE_UI, 1, FONT_W_REGULAR);
            char sz[24];
            if (n->type == FS_DIR) { kutoa(n->child_count, sz, 10); kstrcat(sz, n->child_count == 1 ? " item" : " items"); }
            else fm_size_str(n->size, sz);
            gfx_str_role_fit(sx, ly + (rh - lh(1)) / 2, 90, sz, p.ink2, FONT_ROLE_UI, 1, FONT_W_REGULAR);
            CHIT(s, rect_make(ax - 6, ly, aw + 12, rh), FA_ITEM, (i16)idx);
            ly += rh;
        }
    }
    gfx_clear_clip();

    /* Status bar */
    {
        i32 sy = cr.y + cr.h - status_h;
        gfx_hline(mx, sy, gw, p.line);
        char st[64], n1[12];
        kutoa(s->nvis, n1, 10); kstrcpy(st, n1);
        if (s->search_len) { kstrcat(st, " of "); kutoa(w->fm_dir->child_count, n1, 10); kstrcat(st, n1); kstrcat(st, " items match"); }
        else kstrcat(st, s->nvis == 1 ? " item" : " items");
        if (w->fm_sel < w->fm_dir->child_count) kstrcat(st, " - 1 selected");
        i32 ty = sy + (status_h - lh(0)) / 2;
        gfx_str_role(mx + 18, ty, st, p.muted, FONT_ROLE_UI, 0, FONT_W_REGULAR);
        const char *hint = "Click again to open - Del deletes - R renames - Ctrl+F searches";
        i32 hw = gfx_str_role_width(hint, FONT_ROLE_UI, 0, FONT_W_REGULAR);
        if (gw - hw - 36 > gfx_str_role_width(st, FONT_ROLE_UI, 0, FONT_W_REGULAR) + 24)
            gfx_str_role(mx + gw - 18 - hw, ty, hint, p.muted, FONT_ROLE_UI, 0, FONT_W_REGULAR);
    }

    if (pw) fm_preview(w, s, &p, mx + gw, cr.y, pw, cr.h);

    /* New menu (under the toolbar's New button) */
    if (s->menu_open) {
        i32 mwid = 200, mh = 2 * 40 + 12;
        i32 mxp = s->new_btn.x + s->new_btn.w - mwid;
        if (mxp + mwid > cr.x + cr.w - 8) mxp = cr.x + cr.w - 8 - mwid;
        i32 myp = cr.y + 6;
        gfx_shadow_soft(mxp, myp, mwid, mh, 14);
        gfx_rect_rounded(mxp, myp, mwid, mh, 14, p.dark ? rgb(0x1c, 0x24, 0x40) : rgb(0xff, 0xff, 0xff));
        gfx_rect_rounded_outline(mxp, myp, mwid, mh, 14, p.line);
        ui_glyph(UI_GLYPH_FOLDER, mxp + 14, myp + 6 + 11, 18, g_theme->calm);
        gfx_str_role(mxp + 42, myp + 6 + (40 - lh(2)) / 2, "New folder", p.ink, FONT_ROLE_UI, 2, FONT_W_REGULAR);
        CHIT(s, rect_make(mxp + 6, myp + 6, mwid - 12, 40), FA_MENU_FOLDER, 0);
        ui_glyph(UI_GLYPH_PLUS, mxp + 14, myp + 46 + 11, 18, g_theme->care);
        gfx_str_role(mxp + 42, myp + 46 + (40 - lh(2)) / 2, "New file", p.ink, FONT_ROLE_UI, 2, FONT_W_REGULAR);
        CHIT(s, rect_make(mxp + 6, myp + 46, mwid - 12, 40), FA_MENU_FILE, 0);
    }

    if (w->tab != FM_MODE_BROWSE) {
        s->nhits = 0;                              /* the sheet is modal */
        fm_sheet(w, s, &p, mx, cr.y, mw, cr.h);
    }
}

void app_files_click(window_t *w, i32 x, i32 y, mouse_t *m){
    (void)m;
    fstate_t *s = fst(w);
    if (!s) return;
    bool was_menu = s->menu_open;
    s->menu_open = false;
    for (u32 i = s->nhits; i-- > 0;) {
        const fhit_t *h = &s->hits[i];
        if (!rect_contains(h->r, x, y)) continue;
        if (was_menu && h->act != FA_MENU_FOLDER && h->act != FA_MENU_FILE) return;   /* click-away closes */
        s->search_focus = false;
        switch (h->act) {
        case FA_PLACE: if ((u32)h->arg < s->nplace) fm_go(w, vfs_resolve_path(s->place[h->arg]), true); break;
        case FA_ITEM:
            if ((u32)h->arg == w->fm_sel) fm_open(w, (u32)h->arg);      /* second click opens */
            else w->fm_sel = (u32)h->arg;
            break;
        case FA_OPEN: fm_open(w, w->fm_sel); break;
        case FA_RUN:  fm_run(w, w->fm_sel); break;
        case FA_MENU_FOLDER: fm_begin_input(w, FM_MODE_NEWFOLD); break;
        case FA_MENU_FILE:   fm_begin_input(w, FM_MODE_NEWFILE); break;
        case FA_SHEET_OK:    fm_commit_input(w); break;
        case FA_SHEET_CANCEL: w->tab = FM_MODE_BROWSE; w->input_len = 0; w->input_buf[0] = '\0'; break;
        default: break;
        }
        return;
    }
    s->search_focus = false;
}

/* Move the selection by delta positions in the visible (filtered) list. */
static void fm_move(window_t *w, fstate_t *s, i32 delta) {
    if (!s->nvis) return;
    i32 cur = 0;
    for (u32 k = 0; k < s->nvis; k++) if (s->vis[k] == w->fm_sel) cur = (i32)k;
    cur += delta;
    if (cur < 0) cur = 0;
    if (cur >= (i32)s->nvis) cur = (i32)s->nvis - 1;
    w->fm_sel = s->vis[cur];
    /* keep it in view */
    i32 cols = s->cols > 0 ? s->cols : 1;
    u32 row = (u32)cur / (u32)cols;
    if (row < w->scroll) w->scroll = row;
}

void app_files_key(window_t *w, char c){
    fstate_t *s = fst(w);
    if(!w->fm_dir || !s) return;
    u8 k = (u8)c;
    if(w->tab != FM_MODE_BROWSE){
        if(c=='\n') fm_commit_input(w);
        else if(c=='\b'){ if(w->input_len>0){ w->input_len--; w->input_buf[w->input_len]='\0'; } }
        else if(c=='\x1B'){ w->tab = FM_MODE_BROWSE; w->input_buf[0]='\0'; w->input_len=0; }
        else if(w->input_len < 63 && c>=32 && c<127){ w->input_buf[w->input_len++]=c; w->input_buf[w->input_len]='\0'; }
        return;
    }
    if (c == 0x06) { s->search_focus = true; return; }            /* Ctrl+F */
    if (s->search_focus) {
        if (c == 0x1B) { s->search_len = 0; s->search[0] = '\0'; s->search_focus = false; return; }
        if (c == '\n') { s->search_focus = false; return; }
        if (c == '\b') { if (s->search_len) s->search[--s->search_len] = '\0'; w->scroll = 0; return; }
        if (c >= 32 && c < 127) {
            if (s->search_len + 1 < sizeof s->search) { s->search[s->search_len++] = c; s->search[s->search_len] = '\0'; }
            w->scroll = 0;
            return;
        }
        /* arrows fall through to navigation */
    }
    if (c == 0x1B) { s->menu_open = false; if (s->search_len) { s->search_len = 0; s->search[0] = '\0'; } return; }
    u32 cnt = w->fm_dir->child_count;
    i32 cols = s->grid && s->cols > 0 ? s->cols : 1;
    switch(k){
    case KEY_NAV_UP:    case 'k': fm_move(w, s, -cols); break;
    case KEY_NAV_DOWN:  case 'j': fm_move(w, s, cols); break;
    case KEY_NAV_LEFT:  if (s->grid) fm_move(w, s, -1); else fm_back(w, false); break;
    case KEY_NAV_RIGHT: if (s->grid) fm_move(w, s, 1); break;
    case '\n': fm_open(w, w->fm_sel); break;
    case '\b': fm_up(w); break;
    case KEY_NAV_DELETE: case 'd': fm_delete_sel(w); break;
    case 0x12: case 'r': if (cnt > 0 && w->fm_sel < cnt) fm_begin_input(w, FM_MODE_RENAME); break;
    case 'n': fm_begin_input(w, FM_MODE_NEWFILE); break;
    case 'm': fm_begin_input(w, FM_MODE_NEWFOLD); break;
    case 'g': s->grid = !s->grid; w->scroll = 0; break;
    case 0x03: /* Ctrl+C: Copy file */
        if (cnt > 0 && w->fm_sel < cnt) {
            fs_node_t *ch = w->fm_dir->children[w->fm_sel];
            vfs_get_path(ch, g_clipboard, CLIPBOARD_SIZE-1);
            g_clipboard[CLIPBOARD_SIZE-1] = '\0';
            g_clipboard_len = kstrlen(g_clipboard);
            g_clipboard_is_cut = false;
            notify_push("Files", "Copied", COL_ACCENT);
        }
        break;
    case 0x18: /* Ctrl+X: Cut file */
        if (cnt > 0 && w->fm_sel < cnt) {
            fs_node_t *ch = w->fm_dir->children[w->fm_sel];
            vfs_get_path(ch, g_clipboard, CLIPBOARD_SIZE-1);
            g_clipboard[CLIPBOARD_SIZE-1] = '\0';
            g_clipboard_len = kstrlen(g_clipboard);
            g_clipboard_is_cut = true;
            notify_push("Files", "Cut (^V to move)", COL_YELLOW);
        }
        break;
    case 0x16: /* Ctrl+V: Paste */
        if (g_clipboard_len > 0) {
            fs_node_t *src = vfs_resolve_path(g_clipboard);
            if (src) {
                const char *fname = kstrrchr(g_clipboard, '/');
                if (fname) fname++; else fname = g_clipboard;
                if (vfs_copy(src, w->fm_dir, fname) == 0) {
                    if (g_clipboard_is_cut) {
                        vfs_delete(src);
                        g_clipboard_len = 0;
                        g_clipboard_is_cut = false;
                    }
                    notify_push("Files", "Pasted", g_theme->success);
                } else {
                    notify_push("Files", "Paste failed", g_theme->error);
                }
            } else {
                notify_push("Files", "Source not found", g_theme->error);
            }
        }
        break;
    default: break;
    }
}
