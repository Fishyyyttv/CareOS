/* CareOS v9 -- apps/app_editor.c -- Code Editor (v10, Phase 3 spec §4)
 *
 * Explorer (the open file's folder, plus Problems from the last Run), tabs
 * for up to four files, a Mono 14 code view with Care / C syntax colours
 * (apps/care_syntax.c) and a real caret, an Output panel for Run, a Care
 * language docs panel built from care_lang's own built-in table, and a
 * status bar. The title-bar toolbar shows "<folder> · Editor" and Run.
 *
 * w->text_buf holds the active tab; the others keep their text in their
 * tab slot. w->tab: 0 normal, 1 find, 2 replace, 3 open/save path. */
#include "apps_common.h"
#include "ui.h"
#include "care_syntax.h"

#define ETABS  4
#define EHITS  96
#define EOUT   2048

enum {
    EA_NONE = 0, EA_TAB, EA_TAB_CLOSE, EA_DIR_UP, EA_DIR, EA_FILE, EA_CODE, EA_OUT_CLOSE,
    EA_RUN, EA_DOCS
};
typedef struct { rect_t r; u8 act; i32 arg; } ehit_t;
typedef struct {
    char  path[FS_PATH_MAX];
    char *buf; u32 len;          /* parked text while the tab is inactive */
    bool  modified;
    u32   cursor, scroll;
} etab_t;

typedef struct {
    etab_t tabs[ETABS]; u32 ntabs, cur;
    char   dir[FS_PATH_MAX];      /* Explorer folder */
    char   out[EOUT];
    bool   out_open, ran, ok;
    u32    run_ms, err_line;
    char   ran_path[FS_PATH_MAX];
    bool   docs;
    bool   close_armed; u32 close_ms;
    ehit_t hits[EHITS]; u32 nhits;
    ehit_t tb[4]; u32 ntb;
    i32    code_x, code_y, code_h, lh, chw;
} estate_t;

static estate_t *est(window_t *w) {
    if (!w->app_state) {
        estate_t *s = (estate_t *)kmalloc(sizeof(estate_t));
        if (!s) return NULL;
        kmemset(s, 0, sizeof *s);
        s->ntabs = 1; s->docs = true;
        w->app_state = s;
    }
    return (estate_t *)w->app_state;
}
static void hit(estate_t *s, rect_t r, u8 act, i32 arg) {
    if (s->nhits < EHITS) { s->hits[s->nhits].r = r; s->hits[s->nhits].act = act; s->hits[s->nhits].arg = arg; s->nhits++; }
}
static i32 lh(u32 slot) { return gfx_role_line_h(FONT_ROLE_UI, slot); }

/* Bar state: Save-as vs Open for the path bar, and which replace field
 * has the keyboard. */
static bool s_save_mode, s_replace_field;


/* -- Palette -------------------------------------------------------------------- */
typedef struct {
    u32 code, side, strip, line, ink, ink2, muted, gutter, curline, sel, card;
    u32 syn[CS_KIND_COUNT], builtin;
    bool dark;
} epal_t;
static epal_t epal(void) {
    epal_t p;
    p.dark = g_theme->is_dark;
    p.ink = g_theme->ink; p.ink2 = g_theme->ink2; p.muted = g_theme->muted;
    if (p.dark) {
        p.code = rgb(0x10, 0x15, 0x2a); p.side = rgb(0x0e, 0x13, 0x26); p.strip = rgb(0x0c, 0x10, 0x20);
        p.line = rgb(0x1f, 0x25, 0x3d); p.gutter = rgb(0x5a, 0x63, 0x85); p.curline = rgb(0x19, 0x20, 0x3d);
        p.sel = rgb(0x26, 0x30, 0x5a); p.card = rgb(0x16, 0x1c, 0x33);
        p.syn[CS_TEXT] = rgb(0xd5, 0xda, 0xea); p.syn[CS_KEYWORD] = rgb(0xc0, 0x8b, 0xff);
        p.syn[CS_STRING] = rgb(0x7c, 0xf0, 0xb5); p.syn[CS_NUMBER] = rgb(0xff, 0xc2, 0x4b);
        p.syn[CS_COMMENT] = rgb(0x6a, 0x73, 0x93); p.syn[CS_PUNCT] = rgb(0xd5, 0xda, 0xea);
        p.builtin = rgb(0x6c, 0xb8, 0xff);
    } else {
        p.code = rgb(0xff, 0xff, 0xff); p.side = rgb(0xf5, 0xf6, 0xfa); p.strip = rgb(0xee, 0xf0, 0xf6);
        p.line = rgb(0xe1, 0xe4, 0xee); p.gutter = rgb(0x9a, 0xa3, 0xbd); p.curline = rgb(0xf1, 0xf3, 0xfd);
        p.sel = rgb(0xdd, 0xe3, 0xff); p.card = rgb(0xff, 0xff, 0xff);
        p.syn[CS_TEXT] = rgb(0x2a, 0x2f, 0x45); p.syn[CS_KEYWORD] = rgb(0x7b, 0x4f, 0xd6);
        p.syn[CS_STRING] = rgb(0x1f, 0x8f, 0x55); p.syn[CS_NUMBER] = rgb(0xb0, 0x74, 0x12);
        p.syn[CS_COMMENT] = rgb(0x8a, 0x93, 0xb0); p.syn[CS_PUNCT] = rgb(0x2a, 0x2f, 0x45);
        p.builtin = rgb(0x1a, 0x6f, 0xd8);
    }
    return p;
}

/* -- Paths and files ------------------------------------------------------------- */
/* split "/a/b/c" -> dir="/a/b" name="c" (handles root and no-slash cases) */
static void editor_split_path(const char *path, char *dir, u32 dmax, char *name, u32 nmax) {
    const char *slash = kstrrchr(path, '/');
    if (!slash) { dir[0]='\0'; kstrncpy(name,path,nmax-1); name[nmax-1]='\0'; return; }
    u32 dlen = (u32)(slash-path);
    if (dlen==0) { dir[0]='/'; dir[1]='\0'; }
    else { u32 c=dlen<dmax-1?dlen:dmax-1; kmemcpy(dir,path,c); dir[c]='\0'; }
    kstrncpy(name,slash+1,nmax-1); name[nmax-1]='\0';
}
static const char *base_name(const char *path) {
    const char *b = kstrrchr(path, '/');
    return b ? b + 1 : path;
}
static const char *tab_name(const etab_t *t) { return t->path[0] ? base_name(t->path) : "untitled.cl"; }
static cs_lang_t tab_lang(const etab_t *t) { return t->path[0] ? care_syntax_lang(t->path) : CS_LANG_CARE; }

static void set_explorer_dir(estate_t *s, const char *file_path) {
    char d[FS_PATH_MAX], n[FS_NAME_MAX];
    editor_split_path(file_path, d, sizeof d, n, sizeof n);
    if (d[0]) kstrcpy(s->dir, d);
}

/* Save w->text_buf to path, creating the file if needed. If path is a
 * directory, appends /untitled.cl. Updates the tab and closes the dialog. */
static void editor_save_to_path(window_t *w, const char *path) {
    estate_t *s = est(w);
    if (!path[0]) { notify_push("Editor","No path",g_theme->error); return; }
    char sp[FS_PATH_MAX];
    kstrncpy(sp,path,FS_PATH_MAX-1); sp[FS_PATH_MAX-1]='\0';
    fs_node_t *target = vfs_resolve_path(sp);
    if (target && target->type==FS_DIR) {
        kstrcat(sp,"/untitled.cl");
        target = vfs_resolve_path(sp);
    }
    if (!target) {
        char dir_p[FS_PATH_MAX], name_p[FS_NAME_MAX];
        editor_split_path(sp, dir_p, sizeof(dir_p), name_p, sizeof(name_p));
        if (!name_p[0]) { notify_push("Editor","Invalid path",g_theme->error); return; }
        fs_node_t *parent = dir_p[0] ? vfs_resolve_path(dir_p) : vfs_root();
        if (!parent || parent->type!=FS_DIR) { notify_push("Editor","That folder doesn't exist",g_theme->error); return; }
        target = vfs_mkfile(parent, name_p);
    }
    if (!target) { notify_push("Editor","Can't create a file there",g_theme->error); return; }
    if (vfs_write(target, w->text_buf, w->text_len) < 0) { notify_push("Editor","Saving failed -- is the disk full?",g_theme->error); return; }
    kstrncpy(w->editor_path,sp,FS_PATH_MAX-1); w->editor_path[FS_PATH_MAX-1]='\0';
    w->editor_modified = false;
    w->tab = 0;
    if (s) { kstrcpy(s->tabs[s->cur].path, w->editor_path); s->tabs[s->cur].modified = false; set_explorer_dir(s, w->editor_path); }
    notify_push("Editor","Saved",g_theme->success);
}

/* Load a whole file into the active text. Files too big to hold entirely
 * are refused and the current document is kept: opening a truncated copy
 * used to let Save write the cut-down text back over the file. */
static bool editor_load(window_t *w, fs_node_t *f) {
    u32 len = f->size;
    const char *src = vfs_file_str(f);
    if (len + 1 > WIN_TEXT_MAX || !win_reserve(w, len + 1)) {
        notify_push("Editor", "File too large to edit (limit 1 MB)", g_theme->warning);
        return false;
    }
    if (len) kmemcpy(w->text_buf, src, len);
    w->text_len = len;
    w->text_buf[len] = '\0';
    w->scroll = 0;
    w->cursor_pos = 0;
    w->editor_modified = false;
    return true;
}

/* -- Tabs ------------------------------------------------------------------------- */
static void tab_park(window_t *w, estate_t *s) {
    etab_t *t = &s->tabs[s->cur];
    if (t->buf) kfree(t->buf);
    t->buf = (char *)kmalloc(w->text_len + 1);
    if (t->buf) { kmemcpy(t->buf, w->text_buf, w->text_len); t->buf[w->text_len] = '\0'; t->len = w->text_len; }
    t->modified = w->editor_modified; t->cursor = w->cursor_pos; t->scroll = w->scroll;
}
static void tab_unpark(window_t *w, estate_t *s, u32 i) {
    etab_t *t = &s->tabs[i];
    s->cur = i;
    win_clear(w);
    if (t->buf && win_reserve(w, t->len + 1)) { kmemcpy(w->text_buf, t->buf, t->len); w->text_len = t->len; w->text_buf[t->len] = '\0'; }
    if (t->buf) { kfree(t->buf); t->buf = NULL; }
    w->cursor_pos = t->cursor; w->scroll = t->scroll; w->editor_modified = t->modified;
    kstrcpy(w->editor_path, t->path);
    if (t->path[0]) set_explorer_dir(s, t->path);
}
static void tab_switch(window_t *w, estate_t *s, u32 i) {
    if (i == s->cur || i >= s->ntabs) return;
    tab_park(w, s);
    tab_unpark(w, s, i);
    s->close_armed = false;
}
static void tab_open_file(window_t *w, estate_t *s, fs_node_t *f) {
    char path[FS_PATH_MAX]; vfs_get_path(f, path, sizeof path);
    for (u32 i = 0; i < s->ntabs; i++) if (!kstrcmp(s->tabs[i].path, path)) { tab_switch(w, s, i); return; }
    /* Reuse an untouched untitled tab, else add one, else replace the
     * current tab when it has no unsaved changes. */
    etab_t *c = &s->tabs[s->cur];
    bool reuse = !c->path[0] && !w->editor_modified;
    if (!reuse) {
        if (s->ntabs < ETABS) { tab_park(w, s); s->cur = s->ntabs++; kmemset(&s->tabs[s->cur], 0, sizeof(etab_t)); }
        else if (w->editor_modified) { notify_push("Editor", "Save or close a tab first (4 tabs max)", g_theme->warning); return; }
    }
    if (!editor_load(w, f)) return;
    kstrcpy(s->tabs[s->cur].path, path);
    s->tabs[s->cur].modified = false;
    kstrcpy(w->editor_path, path);
    set_explorer_dir(s, path);
}
static void tab_close(window_t *w, estate_t *s, u32 i) {
    if (i >= s->ntabs) return;
    if (i != s->cur) tab_switch(w, s, i);
    if (w->editor_modified && !(s->close_armed && timer_ms() - s->close_ms < 3000)) {
        s->close_armed = true; s->close_ms = timer_ms();
        notify_push("Editor", "Unsaved changes -- click x again to discard", g_theme->warning);
        return;
    }
    s->close_armed = false;
    if (s->tabs[i].buf) kfree(s->tabs[i].buf);
    for (u32 k = i; k + 1 < s->ntabs; k++) s->tabs[k] = s->tabs[k + 1];
    if (s->ntabs > 1) {
        s->ntabs--;
        kmemset(&s->tabs[s->ntabs], 0, sizeof(etab_t));
        tab_unpark(w, s, i < s->ntabs ? i : s->ntabs - 1);
    } else {                                    /* last tab: an empty untitled one */
        kmemset(&s->tabs[0], 0, sizeof(etab_t));
        win_clear(w); w->cursor_pos = 0; w->editor_path[0] = '\0'; w->editor_modified = false;
    }
}

/* -- Run --------------------------------------------------------------------------- */
static void editor_run(window_t *w, estate_t *s) {
    if (tab_lang(&s->tabs[s->cur]) != CS_LANG_CARE) { notify_push("Editor", "Only Care scripts (.cl) can run", g_theme->warning); return; }
    u32 t0 = timer_ms();
    int rc = w->text_len ? care_lang_exec_buf(w->text_buf, w->text_len, s->out, EOUT) : -1;
    if (!w->text_len) kstrcpy(s->out, "(the file is empty)\n");
    s->run_ms = timer_ms() - t0;
    s->ok = rc == 0;
    s->err_line = s->ok ? 0 : care_lang_last_error_line();
    s->ran = true; s->out_open = true;
    kstrcpy(s->ran_path, s->tabs[s->cur].path);
}

/* -- Find / replace (unchanged behaviour) ------------------------------------------- */
static void editor_find(window_t *w) {
    if (!w->input_buf[0]) return;
    u32 nlen = (u32)kstrlen(w->input_buf);
    u32 start = w->cursor_pos < w->text_len ? w->cursor_pos + 1 : 0;
    for (u32 pass = 0; pass < 2; pass++) {
        for (u32 i = pass ? 0 : start; i + nlen <= w->text_len; i++) {
            if (kmemcmp(w->text_buf + i, w->input_buf, nlen) == 0) { w->cursor_pos = i; return; }
        }
    }
    notify_push("Editor", "Not found", g_theme->warning);
}

static void editor_replace_first(window_t *w) {
    const char *find = w->input_buf;
    const char *repl = w->input_buf + 128;
    u32 flen = (u32)kstrlen(find);
    u32 rlen = (u32)kstrlen(repl);
    if (!flen) return;
    u32 found_at = w->text_len;
    for (u32 i = 0; i + flen <= w->text_len; i++)
        if (kmemcmp(w->text_buf + i, find, flen) == 0) { found_at = i; break; }
    if (found_at == w->text_len) { notify_push("Editor", "Not found", g_theme->warning); return; }
    u32 new_len = w->text_len - flen + rlen;
    if (!win_reserve(w, new_len + 1)) return;
    u32 tail_start = found_at + flen;
    u32 tail_len   = w->text_len - tail_start;
    if (rlen > flen) {
        u32 shift = rlen - flen;
        for (u32 i = tail_len; i > 0; i--)
            w->text_buf[tail_start + shift + i - 1] = w->text_buf[tail_start + i - 1];
    } else if (rlen < flen) {
        u32 shift = flen - rlen;
        for (u32 i = 0; i < tail_len; i++)
            w->text_buf[tail_start - shift + i] = w->text_buf[tail_start + i];
    }
    kmemcpy(w->text_buf + found_at, repl, rlen);
    w->text_len = new_len;
    w->text_buf[new_len] = '\0';
    w->editor_modified = true;
    w->cursor_pos = found_at + rlen;
}

/* -- Editing ------------------------------------------------------------------------- */
static u32 line_start(window_t *w, u32 i) { while (i > 0 && w->text_buf[i - 1] != '\n') i--; return i; }
static u32 line_end(window_t *w, u32 i) { while (i < w->text_len && w->text_buf[i] != '\n') i++; return i; }
static u32 line_of(window_t *w, u32 pos) { u32 n = 0; for (u32 i = 0; i < pos && i < w->text_len; i++) if (w->text_buf[i] == '\n') n++; return n; }
static u32 line_offset(window_t *w, u32 line) {
    u32 i = 0;
    while (line && i < w->text_len) { if (w->text_buf[i] == '\n') line--; i++; }
    return i;
}

static void ed_insert(window_t *w, const char *t, u32 n) {
    if (!n || !win_reserve(w, w->text_len + n + 1)) return;
    u32 c = w->cursor_pos > w->text_len ? w->text_len : w->cursor_pos;
    for (u32 i = w->text_len + 1; i-- > c;) w->text_buf[i + n] = w->text_buf[i];
    kmemcpy(w->text_buf + c, t, n);
    w->text_len += n; w->cursor_pos = c + n;
    w->editor_modified = true;
}
static void ed_erase(window_t *w, u32 at, u32 n) {
    if (at >= w->text_len) return;
    if (at + n > w->text_len) n = w->text_len - at;
    for (u32 i = at; i + n <= w->text_len; i++) w->text_buf[i] = w->text_buf[i + n];
    w->text_len -= n;
    w->editor_modified = true;
}

/* -- Init ----------------------------------------------------------------------------- */
void app_editor_init(window_t *w){
    win_clear(w);
    win_append(w,"# CareOS Care Language script\n\n");
    win_append(w,"var greeting = \"Hello, World!\";\n");
    win_append(w,"print greeting;\n\n");
    win_append(w,"var x = 5;\n");
    win_append(w,"while (x > 0) {\n");
    win_append(w,"    print x;\n");
    win_append(w,"    x = x - 1;\n");
    win_append(w,"}\n");
    win_append(w,"print \"Done!\";\n");
    w->tab = 0;
    w->input_buf[0] = '\0';
    w->input_len = 0;
    w->cursor_pos = 0;
    w->editor_path[0] = '\0';
    w->editor_modified = false;
    estate_t *s = est(w);
    if (s) { app_home_path(s->dir); if (!vfs_resolve_path(s->dir)) kstrcpy(s->dir, "/"); }
}

void app_editor_close(window_t *w) {
    estate_t *s = (estate_t *)w->app_state;
    if (!s) return;
    for (u32 i = 0; i < ETABS; i++) if (s->tabs[i].buf) { kfree(s->tabs[i].buf); s->tabs[i].buf = NULL; }
}

/* -- Title-bar toolbar ------------------------------------------------------------------ */
bool app_editor_toolbar(window_t *w, rect_t bar) {
    estate_t *s = est(w);
    if (!s) return false;
    epal_t p = epal();
    s->ntb = 0;
    char title[96];
    const char *d = s->dir[0] ? base_name(s->dir) : "";
    if (!d[0]) d = "/";
    kstrncpy(title, d, 60); title[60] = '\0';
    kstrcat(title, " - Editor");
    bool can_run = tab_lang(&s->tabs[s->cur]) == CS_LANG_CARE;
    i32 rw = can_run ? gfx_str_role_width("Run", FONT_ROLE_UI, 2, FONT_W_BOLD) + 44 : 0;
    i32 tw = gfx_str_role_width(title, FONT_ROLE_UI, 1, FONT_W_BOLD);
    i32 cx = bar.x + (bar.w - tw) / 2;
    if (cx + tw > bar.x + bar.w - rw - 12) cx = bar.x + bar.w - rw - 12 - tw;
    if (cx < bar.x) cx = bar.x;
    gfx_str_role_fit(cx, bar.y + (bar.h - lh(1)) / 2, bar.x + bar.w - rw - 12 - cx, title, p.ink2, FONT_ROLE_UI, 1, FONT_W_BOLD);
    if (can_run) {
        rect_t r = rect_make(bar.x + bar.w - rw, bar.y + (bar.h - 34) / 2, rw, 34);
        gfx_rect_rounded(r.x, r.y, r.w, r.h, 10, g_theme->care);
        /* play triangle */
        for (i32 i = 0; i < 12; i++) gfx_vline(r.x + 14 + i, r.y + 11 + i / 2, 12 - i, g_theme->on_care);
        gfx_str_role(r.x + 32, r.y + (34 - lh(2)) / 2, "Run", g_theme->on_care, FONT_ROLE_UI, 2, FONT_W_BOLD);
        s->tb[s->ntb].r = r; s->tb[s->ntb].act = EA_RUN; s->ntb++;
    }
    return true;
}

bool app_editor_toolbar_click(window_t *w, i32 x, i32 y) {
    estate_t *s = est(w);
    if (!s) return false;
    for (u32 i = 0; i < s->ntb; i++)
        if (rect_contains(s->tb[i].r, x, y) && s->tb[i].act == EA_RUN) { editor_run(w, s); return true; }
    return false;
}

/* -- Drawing ------------------------------------------------------------------------------ */
static void draw_explorer(window_t *w, estate_t *s, const epal_t *p, i32 x, i32 y, i32 wd, i32 h) {
    gfx_rect(x, y, wd, h, p->side);
    gfx_vline(x + wd, y, h, p->line);
    i32 ix = x + 10, iw = wd - 20, iy = y + 14;
    gfx_str_role(ix + 8, iy, "EXPLORER", p->muted, FONT_ROLE_UI, 0, FONT_W_BOLD);
    iy += lh(0) + 10;
    fs_node_t *d = vfs_resolve_path(s->dir[0] ? s->dir : "/");
    if (!d) d = vfs_root();
    /* the folder itself: click goes up */
    ui_glyph(UI_GLYPH_CHEVRON_D, ix + 6, iy + 8, 16, p->ink);
    gfx_str_role_fit(ix + 28, iy + (32 - lh(2)) / 2, iw - 36, d->name[0] ? d->name : "/", p->ink, FONT_ROLE_UI, 2, FONT_W_BOLD);
    if (d->parent) hit(s, rect_make(ix, iy, iw, 32), EA_DIR_UP, 0);
    iy += 34;
    i32 problems_h = 76;
    i32 limit = y + h - problems_h - 20;
    char cur[FS_PATH_MAX]; kstrcpy(cur, s->tabs[s->cur].path);
    for (u32 pass = 0; pass < 2; pass++) {                 /* files, then folders */
        for (u32 i = 0; i < d->child_count && iy + 32 <= limit; i++) {
            fs_node_t *c = d->children[i];
            if (c->name[0] == '.') continue;
            bool dir = c->type == FS_DIR;
            if ((pass == 0) == dir) continue;
            char cp[FS_PATH_MAX]; vfs_get_path(c, cp, sizeof cp);
            bool on = !dir && !kstrcmp(cp, cur);
            if (on) gfx_rect_rounded(ix, iy, iw, 32, 8, p->sel);
            if (dir) {
                ui_glyph(UI_GLYPH_CHEVRON_R, ix + 6, iy + 8, 16, p->ink2);
                gfx_str_role_fit(ix + 28, iy + (32 - lh(2)) / 2, iw - 36, c->name, p->ink2, FONT_ROLE_UI, 2, FONT_W_REGULAR);
                hit(s, rect_make(ix, iy, iw, 32), EA_DIR, (i32)i);
            } else {
                cs_lang_t lg = care_syntax_lang(c->name);
                const char *dot = kstrrchr(c->name, '.');
                u32 dc = lg == CS_LANG_CARE ? rgb(0xff, 0x7a, 0x59) : lg == CS_LANG_C ? rgb(0x6c, 0xb8, 0xff)
                       : (dot && !kstrcmp(dot, ".md")) ? rgb(0xff, 0xc2, 0x4b) : p->muted;
                gfx_rect_rounded(ix + 26, iy + 11, 10, 10, 3, dc);
                gfx_str_role_fit(ix + 44, iy + (32 - lh(2)) / 2, iw - 52, c->name, on ? p->ink : p->ink2, FONT_ROLE_UI, 2, on ? FONT_W_BOLD : FONT_W_REGULAR);
                hit(s, rect_make(ix, iy, iw, 32), EA_FILE, (i32)i);
            }
            iy += 34;
        }
    }
    /* Problems: from the last Run of this file */
    i32 py = y + h - problems_h - 12;
    gfx_rect_rounded(ix, py, iw, problems_h, 14, p->card);
    gfx_str_role(ix + 12, py + 12, "Problems", p->ink, FONT_ROLE_UI, 1, FONT_W_BOLD);
    i32 ly = py + 12 + lh(1) + 6;
    bool this_file = s->ran && !kstrcmp(s->ran_path, s->tabs[s->cur].path);
    if (tab_lang(&s->tabs[s->cur]) != CS_LANG_CARE) {
        gfx_str_role_fit(ix + 12, ly, iw - 24, "Checked when a .cl file runs", p->muted, FONT_ROLE_UI, 0, FONT_W_REGULAR);
    } else if (!this_file) {
        gfx_str_role_fit(ix + 12, ly, iw - 24, "Not checked yet -- press Run", p->muted, FONT_ROLE_UI, 0, FONT_W_REGULAR);
    } else if (s->ok) {
        ui_glyph(UI_GLYPH_CHECK, ix + 12, ly, 14, g_theme->success);
        gfx_str_role_fit(ix + 32, ly, iw - 44, "No problems found", g_theme->success, FONT_ROLE_UI, 0, FONT_W_BOLD);
    } else {
        char m[48] = "Stopped at line ", n[12];
        kutoa(s->err_line, n, 10); kstrcat(m, n);
        gfx_str_role_fit(ix + 12, ly, iw - 24, m, g_theme->error, FONT_ROLE_UI, 0, FONT_W_BOLD);
    }
}

static void draw_docs(estate_t *s, const epal_t *p, i32 x, i32 y, i32 wd, i32 h) {
    (void)s;
    gfx_rect(x, y, wd, h, p->side);
    gfx_vline(x, y, h, p->line);
    i32 ix = x + 16, iw = wd - 32, iy = y + 16;
    gfx_str_role(ix, iy, "CARE LANGUAGE DOCS", p->muted, FONT_ROLE_UI, 0, FONT_W_BOLD);
    iy += lh(0) + 12;
    static const char *const cards[3][2] = {
        { "Basics",       "var x = 10;\nvar who = \"Pip\";\nprint who + \"!\";" },
        { "Functions",    "func add(a, b) {\n  return a + b;\n}" },
        { "Control flow", "if (x > 5) { ... }\nelse { ... }\nwhile (x > 0) { ... }" },
    };
    i32 mlh = gfx_role_line_h(FONT_ROLE_MONO, 0) + 3;
    gfx_set_clip(x, y, wd, h);
    for (u32 c = 0; c < 4 && iy < y + h - 40; c++) {
        u32 lines = 0;
        if (c < 3) { lines = 1; for (const char *q = cards[c][1]; *q; q++) if (*q == '\n') lines++; }
        else lines = care_lang_builtin_count();
        i32 chh = 14 + lh(2) + 6 + (i32)lines * mlh + 12;
        gfx_rect_rounded(ix, iy, iw, chh, 14, p->card);
        gfx_str_role(ix + 14, iy + 14, c < 3 ? cards[c][0] : "Built-ins", p->ink, FONT_ROLE_UI, 2, FONT_W_BOLD);
        i32 ty = iy + 14 + lh(2) + 6;
        if (c < 3) {
            const char *q = cards[c][1];
            while (*q) {
                const char *e = q; while (*e && *e != '\n') e++;
                char ln[64]; u32 n = (u32)(e - q) < 63 ? (u32)(e - q) : 63;
                kmemcpy(ln, q, n); ln[n] = '\0';
                cs_tok_t tk[16]; u32 nt = care_syntax_line(CS_LANG_CARE, ln, n, tk, 16); if (nt > 16) nt = 16;
                i32 tx = ix + 14;
                for (u32 t = 0; t < nt; t++) {
                    char seg[64]; kmemcpy(seg, ln + tk[t].start, tk[t].len); seg[tk[t].len] = '\0';
                    gfx_str_role(tx, ty, seg, p->syn[tk[t].kind], FONT_ROLE_MONO, 0, FONT_W_REGULAR);
                    tx += gfx_str_role_width(seg, FONT_ROLE_MONO, 0, FONT_W_REGULAR);
                }
                ty += mlh;
                q = *e ? e + 1 : e;
            }
        } else {
            for (u32 b = 0; b < care_lang_builtin_count(); b++) {
                gfx_str_role_fit(ix + 14, ty, iw - 28, care_lang_builtin_sig(b), p->builtin, FONT_ROLE_MONO, 0, FONT_W_REGULAR);
                ty += mlh;
            }
        }
        iy += chh + 12;
    }
    gfx_clear_clip();
}

static void draw_code(window_t *w, estate_t *s, const epal_t *p, i32 x, i32 y, i32 wd, i32 h) {
    gfx_rect(x, y, wd, h, p->code);
    i32 mlh = gfx_role_line_h(FONT_ROLE_MONO, 2);
    s->lh = mlh * 13 / 10 + 3;
    s->chw = gfx_str_role_width("M", FONT_ROLE_MONO, 2, FONT_W_REGULAR);
    i32 gut = 56;
    s->code_x = x + gut; s->code_y = y + 12; s->code_h = h - 12;
    u32 vis = (u32)((h - 24) / s->lh); if (vis < 1) vis = 1;
    u32 total = line_of(w, w->text_len) + 1;
    u32 caret = w->cursor_pos > w->text_len ? w->text_len : w->cursor_pos;
    u32 cline = line_of(w, caret);
    if (w->scroll + vis > total) w->scroll = total > vis ? total - vis : 0;
    hit(s, rect_make(x, y, wd, h), EA_CODE, 0);
    cs_lang_t lang = tab_lang(&s->tabs[s->cur]);
    u32 off = line_offset(w, w->scroll);
    gfx_set_clip(x, y, wd, h);
    for (u32 ln = w->scroll; ln < total && ln < w->scroll + vis + 1; ln++) {
        i32 ly = s->code_y + (i32)(ln - w->scroll) * s->lh;
        u32 e = line_end(w, off);
        if (ln == cline) gfx_rect(x, ly - 2, wd, s->lh, p->curline);
        if (s->ran && !s->ok && s->err_line == ln + 1 && !kstrcmp(s->ran_path, s->tabs[s->cur].path))
            gfx_rect_blend(x, ly - 2, wd, s->lh, g_theme->error, 40);
        char num[12]; kutoa(ln + 1, num, 10);
        i32 nw = gfx_str_role_width(num, FONT_ROLE_MONO, 2, FONT_W_REGULAR);
        gfx_str_role(x + gut - 16 - nw, ly + (s->lh - mlh) / 2 - 2, num, ln == cline ? p->ink : p->gutter, FONT_ROLE_MONO, 2, FONT_W_REGULAR);
        u32 n = e - off; if (n > 240) n = 240;
        char buf[244];
        for (u32 i = 0; i < n; i++) buf[i] = w->text_buf[off + i] == '\t' ? ' ' : w->text_buf[off + i];
        cs_tok_t tk[64]; u32 nt = care_syntax_line(lang, buf, n, tk, 64); if (nt > 64) nt = 64;
        i32 tx = s->code_x;
        for (u32 t = 0; t < nt; t++) {
            char seg[244]; kmemcpy(seg, buf + tk[t].start, tk[t].len); seg[tk[t].len] = '\0';
            u32 col = p->syn[tk[t].kind];
            u32 after = tk[t].start + tk[t].len;
            if (tk[t].kind == CS_TEXT && after < n && buf[after] == '(' && seg[0] != ' ') col = p->builtin;   /* calls */
            gfx_str_role(tx, ly + (s->lh - mlh) / 2 - 2, seg, col, FONT_ROLE_MONO, 2, FONT_W_REGULAR);
            tx += (i32)tk[t].len * s->chw;
        }
        if (ln == cline && w->focused && w->tab == 0 && ((timer_ms() / 500) & 1) == 0)
            gfx_rect(s->code_x + (i32)(caret - off) * s->chw, ly, 2, s->lh - 6, g_theme->care);
        if (e >= w->text_len) break;
        off = e + 1;
    }
    gfx_clear_clip();
}

static void draw_output(estate_t *s, const epal_t *p, i32 x, i32 y, i32 wd, i32 h) {
    gfx_rect(x, y, wd, h, p->strip);
    gfx_hline(x, y, wd, p->line);
    i32 hy = y, hh = 38;
    gfx_str_role(x + 16, hy + (hh - lh(1)) / 2, "Output", p->ink, FONT_ROLE_UI, 1, FONT_W_BOLD);
    gfx_rect(x + 16, hy + hh - 2, gfx_str_role_width("Output", FONT_ROLE_UI, 1, FONT_W_BOLD), 2, g_theme->care);
    char st[48];
    if (s->ok) {
        char n[12]; kstrcpy(st, "Finished in ");
        kutoa(s->run_ms / 1000, n, 10); kstrcat(st, n); kstrcat(st, ".");
        u32 cs = (s->run_ms % 1000) / 10; if (cs < 10) kstrcat(st, "0");
        kutoa(cs, n, 10); kstrcat(st, n); kstrcat(st, " s");
    } else { char n[12]; kstrcpy(st, "Stopped at line "); kutoa(s->err_line, n, 10); kstrcat(st, n); }
    i32 sw = gfx_str_role_width(st, FONT_ROLE_UI, 1, FONT_W_BOLD);
    gfx_str_role(x + wd - 52 - sw, hy + (hh - lh(1)) / 2, st, s->ok ? g_theme->success : g_theme->error, FONT_ROLE_UI, 1, FONT_W_BOLD);
    ui_glyph(UI_GLYPH_CLOSE, x + wd - 34, hy + 11, 16, p->muted);
    hit(s, rect_make(x + wd - 42, hy + 3, 32, 32), EA_OUT_CLOSE, 0);
    /* the last lines that fit */
    i32 mlh = gfx_role_line_h(FONT_ROLE_MONO, 1) + 4;
    i32 rows = (h - hh - 8) / mlh; if (rows < 1) rows = 1;
    u32 total = 0, len = (u32)kstrlen(s->out);
    for (u32 i = 0; i < len; i++) if (s->out[i] == '\n') total++;
    if (len && s->out[len - 1] != '\n') total++;
    u32 skip = total > (u32)rows ? total - (u32)rows : 0;
    const char *q = s->out;
    while (skip && *q) { if (*q == '\n') skip--; q++; }
    i32 ty = hy + hh + 4;
    gfx_set_clip(x, hy + hh, wd, h - hh);
    if (!*s->out) gfx_str_role(x + 16, ty, s->ok ? "(no output)" : "The script stopped before printing anything.", p->muted, FONT_ROLE_MONO, 1, FONT_W_REGULAR);
    while (*q) {
        const char *e = q; while (*e && *e != '\n') e++;
        char ln[200]; u32 n = (u32)(e - q) < 199 ? (u32)(e - q) : 199;
        kmemcpy(ln, q, n); ln[n] = '\0';
        gfx_str_role(x + 16, ty, ln, p->syn[CS_TEXT], FONT_ROLE_MONO, 1, FONT_W_REGULAR);
        ty += mlh;
        q = *e ? e + 1 : e;
    }
    gfx_clear_clip();
}

void app_editor_draw(window_t *w){
    rect_t cr = wm_client_rect(w);
    estate_t *s = est(w);
    epal_t p = epal();
    gfx_rect(cr.x, cr.y, cr.w, cr.h, p.code);
    if (!s) return;
    s->nhits = 0;
    /* Another app (Files) may have loaded a file into this window. */
    if (kstrcmp(s->tabs[s->cur].path, w->editor_path)) {
        kstrcpy(s->tabs[s->cur].path, w->editor_path);
        s->tabs[s->cur].modified = w->editor_modified;
        if (w->editor_path[0]) set_explorer_dir(s, w->editor_path);
    }
    s->tabs[s->cur].modified = w->editor_modified;

    i32 sb_h = 30;
    i32 exw = cr.w >= 900 ? 240 : 0;
    i32 dw = (s->docs && cr.w >= 1180) ? 300 : 0;
    if (exw) draw_explorer(w, s, &p, cr.x, cr.y, exw, cr.h - sb_h);
    i32 mx = cr.x + exw + (exw ? 1 : 0), mw = cr.w - (mx - cr.x) - dw;
    if (dw) draw_docs(s, &p, cr.x + cr.w - dw, cr.y, dw, cr.h - sb_h);

    /* Tabs */
    i32 th = 40;
    gfx_rect(mx, cr.y, mw, th, p.strip);
    i32 tx = mx + 8;
    for (u32 i = 0; i < s->ntabs; i++) {
        const etab_t *t = &s->tabs[i];
        bool on = i == s->cur;
        const char *nm = tab_name(t);
        i32 tw = gfx_str_role_width(nm, FONT_ROLE_UI, 1, on ? FONT_W_BOLD : FONT_W_REGULAR) + 28 + (on ? 26 : 0);
        if (tx + tw > mx + mw - 8) break;
        if (on) gfx_rect_rounded(tx, cr.y + 6, tw, th - 6 + 10, 10, p.code);
        bool mod = on ? w->editor_modified : t->modified;
        if (mod) gfx_circle_fill(tx + 14, cr.y + 6 + (th - 6) / 2, 4, g_theme->care);
        gfx_str_role(tx + (mod ? 24 : 14), cr.y + 6 + (th - 6 - lh(1)) / 2, nm, on ? p.ink : p.muted, FONT_ROLE_UI, 1, on ? FONT_W_BOLD : FONT_W_REGULAR);
        if (on) {
            ui_glyph(UI_GLYPH_CLOSE, tx + tw - 24, cr.y + 6 + (th - 6 - 14) / 2, 14, p.muted);
            hit(s, rect_make(tx + tw - 28, cr.y + 6, 24, th - 6), EA_TAB_CLOSE, (i32)i);
        }
        hit(s, rect_make(tx, cr.y + 6, tw, th - 6), EA_TAB, (i32)i);
        tx += tw + 2;
    }

    /* Find / replace / path bar */
    i32 bar_h = w->tab ? 44 : 0;
    i32 by = cr.y + th;
    if (bar_h) {
        gfx_rect(mx, by, mw, bar_h, p.strip);
        gfx_hline(mx, by + bar_h - 1, mw, p.line);
        const char *lab = w->tab == 1 ? "Find" : w->tab == 2 ? "Find" : (s_save_mode ? "Save as" : "Open");
        gfx_str_role(mx + 16, by + (bar_h - lh(1)) / 2, lab, p.ink2, FONT_ROLE_UI, 1, FONT_W_BOLD);
        i32 fx = mx + 90, fw = w->tab == 2 ? (mw - 90 - 200) / 2 : mw - 90 - 140;
        if (fw < 80) fw = 80;
        gfx_rect_rounded(fx, by + 6, fw, bar_h - 12, 10, p.code);
        gfx_rect_rounded_outline(fx, by + 6, fw, bar_h - 12, 10, (w->tab != 2 || !s_replace_field) ? g_theme->care : p.line);
        gfx_str_role_fit(fx + 10, by + (bar_h - gfx_role_line_h(FONT_ROLE_MONO, 1)) / 2, fw - 20, w->input_buf, p.ink, FONT_ROLE_MONO, 1, FONT_W_REGULAR);
        if (w->tab == 2) {
            i32 rx = fx + fw + 80;
            gfx_str_role(fx + fw + 14, by + (bar_h - lh(1)) / 2, "Replace", p.ink2, FONT_ROLE_UI, 1, FONT_W_BOLD);
            gfx_rect_rounded(rx, by + 6, fw, bar_h - 12, 10, p.code);
            gfx_rect_rounded_outline(rx, by + 6, fw, bar_h - 12, 10, s_replace_field ? g_theme->care : p.line);
            gfx_str_role_fit(rx + 10, by + (bar_h - gfx_role_line_h(FONT_ROLE_MONO, 1)) / 2, fw - 20, w->input_buf + 128, p.ink, FONT_ROLE_MONO, 1, FONT_W_REGULAR);
        }
        const char *hint = w->tab == 2 ? "Tab switches - Esc" : "Enter - Esc";
        i32 hw = gfx_str_role_width(hint, FONT_ROLE_UI, 0, FONT_W_REGULAR);
        gfx_str_role(mx + mw - 16 - hw, by + (bar_h - lh(0)) / 2, hint, p.muted, FONT_ROLE_UI, 0, FONT_W_REGULAR);
    }

    /* Code + output */
    i32 out_h = s->out_open ? 170 : 0;
    i32 code_y = by + bar_h, code_h = cr.y + cr.h - sb_h - out_h - code_y;
    draw_code(w, s, &p, mx, code_y, mw, code_h);
    if (out_h) draw_output(s, &p, mx, code_y + code_h, mw, out_h);

    /* Status bar */
    i32 sy = cr.y + cr.h - sb_h;
    gfx_rect(cr.x, sy, cr.w, sb_h, g_theme->care);
    i32 ty = sy + (sb_h - lh(0)) / 2;
    cs_lang_t lg = tab_lang(&s->tabs[s->cur]);
    gfx_str_role(cr.x + 14, ty, lg == CS_LANG_CARE ? "Care 9.0" : lg == CS_LANG_C ? "C" : "Plain text", g_theme->on_care, FONT_ROLE_UI, 0, FONT_W_BOLD);
    if (w->editor_modified) gfx_str_role(cr.x + 100, ty, "Edited", g_theme->on_care, FONT_ROLE_UI, 0, FONT_W_BOLD);
    u32 caret = w->cursor_pos > w->text_len ? w->text_len : w->cursor_pos;
    char pos[48], n[12];
    kstrcpy(pos, "Ln "); kutoa(line_of(w, caret) + 1, n, 10); kstrcat(pos, n);
    kstrcat(pos, ", Col "); kutoa(caret - line_start(w, caret) + 1, n, 10); kstrcat(pos, n);
    kstrcat(pos, "     Spaces: 4");
    i32 pw = gfx_str_role_width(pos, FONT_ROLE_UI, 0, FONT_W_BOLD);
    gfx_str_role(cr.x + cr.w - 14 - pw, ty, pos, g_theme->on_care, FONT_ROLE_UI, 0, FONT_W_BOLD);
}

/* -- Input -------------------------------------------------------------------------------- */
void app_editor_click(window_t *w, i32 x, i32 y, mouse_t *m) {
    (void)m;
    estate_t *s = est(w);
    if (!s) return;
    for (u32 k = s->nhits; k-- > 0;) {
        const ehit_t *h = &s->hits[k];
        if (!rect_contains(h->r, x, y)) continue;
        if (h->act != EA_TAB_CLOSE) s->close_armed = false;
        switch (h->act) {
        case EA_TAB:       tab_switch(w, s, (u32)h->arg); break;
        case EA_TAB_CLOSE: tab_close(w, s, (u32)h->arg); break;
        case EA_DIR_UP: {
            fs_node_t *d = vfs_resolve_path(s->dir);
            if (d && d->parent) { vfs_get_path(d->parent, s->dir, FS_PATH_MAX); if (!s->dir[0]) kstrcpy(s->dir, "/"); }
            break;
        }
        case EA_DIR: case EA_FILE: {
            fs_node_t *d = vfs_resolve_path(s->dir);
            if (!d || (u32)h->arg >= d->child_count) break;
            fs_node_t *c = d->children[h->arg];
            if (h->act == EA_DIR) { vfs_get_path(c, s->dir, FS_PATH_MAX); vfs_resolve_path(s->dir); }
            else tab_open_file(w, s, c);
            break;
        }
        case EA_OUT_CLOSE: s->out_open = false; break;
        case EA_CODE: {
            if (s->lh <= 0) break;
            i32 row = (y - s->code_y) / s->lh; if (row < 0) row = 0;
            u32 ln = w->scroll + (u32)row;
            u32 total = line_of(w, w->text_len);
            if (ln > total) ln = total;
            u32 ls = line_offset(w, ln), le = line_end(w, ls);
            i32 col = s->chw > 0 ? (x - s->code_x + s->chw / 2) / s->chw : 0;
            if (col < 0) col = 0;
            w->cursor_pos = ls + ((u32)col < le - ls ? (u32)col : le - ls);
            if (w->tab) w->tab = 0;
            break;
        }
        default: break;
        }
        return;
    }
}

/* Keep the caret's line on screen. */
static void follow_caret(window_t *w, estate_t *s) {
    if (!s || s->lh <= 0) return;
    u32 vis = (u32)((s->code_h - 12) / s->lh); if (vis < 1) vis = 1;
    u32 cl = line_of(w, w->cursor_pos);
    if (cl < w->scroll) w->scroll = cl;
    else if (cl >= w->scroll + vis) w->scroll = cl - vis + 1;
}

void app_editor_key(window_t *w, char c){
    estate_t *s = est(w);
    u8 k = (u8)c;
    if (c == 0x02) { if (s) s->docs = !s->docs; return; }            /* Ctrl+B: docs panel */
    if (c == 0x05) { if (s) editor_run(w, s); return; }             /* Ctrl+E: Run */
    if (c == 0x17) { if (s) tab_close(w, s, s->cur); return; }      /* Ctrl+W: close tab */
    if (c == 0x06) { w->tab = 1; w->input_buf[0] = '\0'; w->input_len = 0; return; }   /* Ctrl+F */
    if (c == 0x12) { w->tab = 2; w->cursor_pos = w->cursor_pos; w->input_buf[0] = '\0'; w->input_len = 0; w->input_buf[128] = '\0'; return; }
    if (c == 0x0F) {                                                 /* Ctrl+O: Open */
        w->tab = 3; s_save_mode = false;
        kstrncpy(w->input_buf, w->editor_path, 127); w->input_buf[127] = '\0';
        w->input_len = (u32)kstrlen(w->input_buf);
        if (s) s->close_armed = false;
        return;
    }

    /* Find / replace / open-save bars. */
    if (w->tab == 1 || w->tab == 2 || w->tab == 3) {
        if (c == 0x1B) { w->tab = 0; return; }
        if (w->tab == 2 && c == '\t') { s_replace_field = !s_replace_field; return; }
        char *buf = (w->tab == 2 && s_replace_field) ? w->input_buf + 128 : w->input_buf;
        u32 len = (u32)kstrlen(buf);
        if (c == '\n') {
            if (w->tab == 1) editor_find(w);
            else if (w->tab == 2) editor_replace_first(w);
            else {
                w->input_buf[w->input_len] = '\0';
                if (s_save_mode) editor_save_to_path(w, w->input_buf);
                else {
                    fs_node_t *f = vfs_resolve_path(w->input_buf);
                    if (f && f->type == FS_FILE && s) { tab_open_file(w, s, f); w->tab = 0; }
                    else notify_push("Editor", "File not found", g_theme->error);
                }
                s_save_mode = false;
            }
            return;
        }
        if (c == '\b') { if (len) buf[len - 1] = '\0'; }
        else if (c >= 32 && c < 127 && len < 126) { buf[len] = c; buf[len + 1] = '\0'; }
        if (buf == w->input_buf) w->input_len = (u32)kstrlen(w->input_buf);
        return;
    }

    u32 cp = w->cursor_pos > w->text_len ? w->text_len : w->cursor_pos;
    w->cursor_pos = cp;
    if (k == KEY_NAV_LEFT)  { if (cp) w->cursor_pos--; follow_caret(w, s); return; }
    if (k == KEY_NAV_RIGHT) { if (cp < w->text_len) w->cursor_pos++; follow_caret(w, s); return; }
    if (k == KEY_NAV_HOME)  {
        u32 ls = line_start(w, cp), fi = ls;
        while (fi < w->text_len && w->text_buf[fi] == ' ') fi++;
        w->cursor_pos = (cp == fi) ? ls : fi;                        /* smart home */
        return;
    }
    if (k == KEY_NAV_END)   { w->cursor_pos = line_end(w, cp); return; }
    if (k == KEY_NAV_UP || k == KEY_NAV_DOWN || k == KEY_NAV_PGUP || k == KEY_NAV_PGDN) {
        u32 step = 1;
        if ((k == KEY_NAV_PGUP || k == KEY_NAV_PGDN) && s && s->lh > 0) step = (u32)((s->code_h - 12) / s->lh) - 1;
        if (step < 1) step = 1;
        bool up = k == KEY_NAV_UP || k == KEY_NAV_PGUP;
        u32 ln = line_of(w, cp), col = cp - line_start(w, cp);
        u32 total = line_of(w, w->text_len);
        ln = up ? (ln > step ? ln - step : 0) : (ln + step > total ? total : ln + step);
        u32 ls = line_offset(w, ln), le = line_end(w, ls);
        w->cursor_pos = ls + (col < le - ls ? col : le - ls);
        follow_caret(w, s);
        return;
    }
    if (k == KEY_NAV_DELETE) { ed_erase(w, cp, 1); return; }
    if (c == '\b') {
        if (!cp) return;
        /* Backspace in leading spaces removes a whole indent step. */
        u32 ls = line_start(w, cp), n = 1;
        bool lead = true;
        for (u32 i = ls; i < cp; i++) if (w->text_buf[i] != ' ') lead = false;
        if (lead && cp - ls >= 4 && (cp - ls) % 4 == 0) n = 4;
        ed_erase(w, cp - n, n); w->cursor_pos = cp - n;
        follow_caret(w, s);
        return;
    }
    if (c == 0x13) {                                                 /* Ctrl+S */
        if (w->editor_path[0]) editor_save_to_path(w, w->editor_path);
        else {
            w->tab = 3; s_save_mode = true;
            char home[64]; app_home_path(home); kstrcpy(w->input_buf, home); kstrcat(w->input_buf, "/untitled.cl");
            w->input_len = (u32)kstrlen(w->input_buf);
        }
        return;
    }
    if (c == 0x03) {                                                 /* Ctrl+C: copy all */
        u32 n = w->text_len < CLIPBOARD_SIZE - 1 ? w->text_len : CLIPBOARD_SIZE - 1;
        kmemcpy(g_clipboard, w->text_buf, n); g_clipboard[n] = '\0'; g_clipboard_len = n;
        g_clipboard_is_cut = false;
        notify_push("Clipboard", "Copied the whole file", COL_ACCENT);
        return;
    }
    if (c == 0x16) { ed_insert(w, g_clipboard, g_clipboard_len); follow_caret(w, s); return; }
    if (c == '\n') {
        u32 ls = line_start(w, cp), indent = 0;
        while (ls + indent < cp && w->text_buf[ls + indent] == ' ') indent++;
        i32 q = (i32)cp - 1;
        while (q >= (i32)ls && w->text_buf[q] == ' ') q--;
        if (q >= (i32)ls && w->text_buf[q] == '{') indent += 4;
        char ins[128]; u32 n = 0;
        ins[n++] = '\n';
        for (u32 i = 0; i < indent && n < 127; i++) ins[n++] = ' ';
        ed_insert(w, ins, n);
        follow_caret(w, s);
        return;
    }
    if (c == '\t') { ed_insert(w, "    ", 4); return; }
    if (c == '}') {
        u32 ls = line_start(w, cp);
        bool only = cp - ls >= 4;
        for (u32 i = ls; i < cp; i++) if (w->text_buf[i] != ' ') only = false;
        if (only) { ed_erase(w, cp - 4, 4); w->cursor_pos = cp - 4; }
        ed_insert(w, "}", 1);
        return;
    }
    if (c >= 32 && c < 127) { char ch = c; ed_insert(w, &ch, 1); follow_caret(w, s); }
}
