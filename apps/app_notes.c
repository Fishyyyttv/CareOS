/* CareOS v9 -- apps/app_notes.c -- Notes (v10, Phase 3 spec §5)
 *
 * Three panes over ~/Notes: folders (subdirectories, All notes, Pinned),
 * the note list (search, title + first line), and the editor. Notes are
 * plain .md files (apps/notes_md.c), so the Editor opens them too.
 *
 * The editor is a live preview: every line is drawn styled (title,
 * headings, checkboxes, quotes, bullets, **bold**, *italic*) except the
 * line with the caret, which shows its Markdown source for editing.
 * Changes save on their own after a short pause, when switching notes and
 * when the window closes. */
#include "apps_common.h"
#include "ui.h"
#include "notes_md.h"

#define NOTES_MAX    64
#define NFOLD_MAX    12
#define NPATH        96
#define NHITS        96

enum {
    NA_NONE = 0, NA_FOLDER, NA_NOTE, NA_SEARCH, NA_NEW_NOTE, NA_NEW_FOLDER, NA_BOLD,
    NA_ITALIC, NA_HEAD, NA_CHECKLIST, NA_PIN, NA_DELETE, NA_CHECKBOX, NA_LINE, NA_EDITOR
};
typedef struct { rect_t r; u8 act; i32 arg; } nhit_t;

typedef struct {
    char path[NPATH];
    char title[48];
    char snippet[72];
    i8   folder;          /* -1 = top level of ~/Notes */
    bool pinned;
} note_t;

typedef struct {
    note_t notes[NOTES_MAX]; u32 nnotes;
    char   folders[NFOLD_MAX][32]; u32 fcount[NFOLD_MAX]; u32 nfolders;
    u32    npinned;
    i32    sel_folder;     /* -1 all, -2 pinned, else folder index */
    char   open[NPATH];    /* path of the note in the editor ("" none) */
    bool   dirty; u32 dirty_ms;
    char   search[40]; u32 search_len;
    u8     focus;          /* 0 editor, 1 search, 2 new-folder name */
    char   fname[32]; u32 fname_len;
    bool   del_armed; u32 del_ms;
    i32    scroll_px, content_h, view_h;
    i32    caret_y, caret_h;  /* last drawn caret (view coordinates) */
    bool   follow_caret;
    nhit_t hits[NHITS]; u32 nhits;
    i32    line_y[256]; u32 line_off[256]; u32 nlines;   /* logical lines as drawn */
} nstate_t;

static nstate_t *nst(window_t *w) {
    if (!w->app_state) {
        nstate_t *s = (nstate_t *)kmalloc(sizeof(nstate_t));
        if (!s) return NULL;
        kmemset(s, 0, sizeof *s);
        s->sel_folder = -1;
        w->app_state = s;
    }
    return (nstate_t *)w->app_state;
}
static void hit(nstate_t *s, rect_t r, u8 act, i32 arg) {
    if (s->nhits < NHITS) { s->hits[s->nhits].r = r; s->hits[s->nhits].act = act; s->hits[s->nhits].arg = arg; s->nhits++; }
}
static i32 lh(u32 slot) { return gfx_role_line_h(FONT_ROLE_UI, slot); }

/* -- Palette (Notes' accent is honey) ------------------------------------------ */
typedef struct { u32 body, side, line, ink, ink2, muted, sel, field, honey, quote, quote_ink; bool dark; } npal_t;
static npal_t npal(void) {
    npal_t p;
    p.dark = g_theme->is_dark;
    p.body = g_theme->window_body; p.side = g_theme->surface_alt;
    p.line = p.dark ? rgb(0x26, 0x2d, 0x48) : rgb(0xe1, 0xe4, 0xee);
    p.ink = g_theme->ink; p.ink2 = g_theme->ink2; p.muted = g_theme->muted;
    p.honey = rgb(0xf5, 0xae, 0x2e);
    p.sel = p.dark ? ui_mix(p.side, p.honey, 60) : rgb(0xff, 0xf1, 0xc9);
    p.field = p.dark ? rgb(0x10, 0x15, 0x29) : rgb(0xf5, 0xf6, 0xfa);
    p.quote = p.dark ? ui_mix(p.body, p.honey, 34) : rgb(0xff, 0xf8, 0xe3);
    p.quote_ink = p.dark ? rgb(0xf3, 0xdd, 0xa8) : rgb(0x4a, 0x3a, 0x10);
    return p;
}

/* -- Files on disk ----------------------------------------------------------- */
static void notes_root(char *out) { app_home_path(out); kstrcat(out, "/Notes"); }

/* ~/Notes, created on demand (and the home folder for accounts without
 * one, as the old Notes did). On first creation an old ~/notes.txt is
 * copied in as "Notes.md". */
static fs_node_t *notes_root_dir(bool create) {
    char root[NPATH]; notes_root(root);
    fs_node_t *d = vfs_resolve_path(root);
    if (d || !create) return d;
    char home[NPATH]; app_home_path(home);
    fs_node_t *h = vfs_resolve_path(home);
    if (!h && user_current_uid() != 0) {
        fs_node_t *hr = vfs_resolve_path("/home");
        if (hr) h = vfs_mkdir(hr, user_current_name());
    }
    if (!h) return NULL;
    d = vfs_mkdir(h, "Notes");
    if (d) {
        fs_node_t *old = vfs_find(h, "notes.txt");
        if (old && old->size) {
            fs_node_t *nf = vfs_mkfile(d, "Notes.md");
            if (nf) vfs_write(nf, vfs_file_str(old), old->size);
        }
    }
    return d;
}

static bool is_md(const char *name) {
    u32 n = (u32)kstrlen(name);
    return n > 3 && !kstrcmp(name + n - 3, ".md");
}

/* Pinned notes: relative paths, one per line, in ~/Notes/.pinned. */
static bool pinned_has(const char *pins, const char *rel) {
    u32 rl = (u32)kstrlen(rel);
    for (const char *p = pins; *p;) {
        const char *e = p; while (*e && *e != '\n') e++;
        if ((u32)(e - p) == rl && !kstrncmp(p, rel, rl)) return true;
        p = *e ? e + 1 : e;
    }
    return false;
}

static const char *rel_path(const char *path) {
    char root[NPATH]; notes_root(root);
    u32 rl = (u32)kstrlen(root);
    return (!kstrncmp(path, root, rl) && path[rl] == '/') ? path + rl + 1 : path;
}

static void add_note(nstate_t *s, fs_node_t *f, i8 folder, const char *pins) {
    if (s->nnotes >= NOTES_MAX) return;
    note_t *n = &s->notes[s->nnotes++];
    vfs_get_path(f, n->path, NPATH);
    const char *t = vfs_file_str(f);
    notes_title(t, f->size, n->title, sizeof n->title);
    notes_snippet(t, f->size, n->snippet, sizeof n->snippet);
    n->folder = folder;
    n->pinned = pinned_has(pins, rel_path(n->path));
    if (n->pinned) s->npinned++;
}

static void notes_scan(nstate_t *s) {
    s->nnotes = 0; s->nfolders = 0; s->npinned = 0;
    fs_node_t *root = notes_root_dir(false);
    if (!root) return;
    char rp[NPATH]; vfs_get_path(root, rp, sizeof rp);
    vfs_resolve_path(rp);                                   /* load ext2 children */
    fs_node_t *pf = vfs_find(root, ".pinned");
    const char *pins = pf ? vfs_file_str(pf) : "";
    for (u32 i = 0; i < root->child_count; i++) {
        fs_node_t *c = root->children[i];
        if (c->name[0] == '.') continue;
        if (c->type == FS_DIR && s->nfolders < NFOLD_MAX) {
            u32 fi = s->nfolders++;
            kstrncpy(s->folders[fi], c->name, 31); s->folders[fi][31] = '\0';
            s->fcount[fi] = 0;
            char cp[NPATH]; vfs_get_path(c, cp, sizeof cp); vfs_resolve_path(cp);
            for (u32 j = 0; j < c->child_count; j++) {
                fs_node_t *f = c->children[j];
                if (f->type == FS_FILE && is_md(f->name)) { add_note(s, f, (i8)fi, pins); s->fcount[fi]++; }
            }
        } else if (c->type == FS_FILE && is_md(c->name)) add_note(s, c, -1, pins);
    }
}

static void pinned_toggle(nstate_t *s, const char *path) {
    fs_node_t *root = notes_root_dir(true);
    if (!root) return;
    fs_node_t *pf = vfs_find(root, ".pinned");
    if (!pf) pf = vfs_mkfile(root, ".pinned");
    if (!pf) return;
    const char *rel = rel_path(path);
    const char *old = vfs_file_str(pf);
    char out[1024]; u32 o = 0;
    bool had = pinned_has(old, rel);
    for (const char *p = old; *p;) {                      /* copy all but rel */
        const char *e = p; while (*e && *e != '\n') e++;
        u32 n = (u32)(e - p);
        if (n && !(n == kstrlen(rel) && !kstrncmp(p, rel, n)) && o + n + 2 < sizeof out) {
            kmemcpy(out + o, p, n); o += n; out[o++] = '\n';
        }
        p = *e ? e + 1 : e;
    }
    if (!had) { u32 n = (u32)kstrlen(rel); if (o + n + 2 < sizeof out) { kmemcpy(out + o, rel, n); o += n; out[o++] = '\n'; } }
    vfs_write(pf, out, o);
    notes_scan(s);
}

/* -- Open / save ------------------------------------------------------------- */
static void note_save(window_t *w, nstate_t *s) {
    if (!s->open[0] || !s->dirty) return;
    fs_node_t *f = vfs_resolve_path(s->open);
    if (!f || vfs_write(f, w->text_buf, w->text_len) < 0) {
        notify_push("Notes", "Couldn't save the note -- is the disk full?", g_theme->error);
        return;
    }
    s->dirty = false;
    /* Keep the file named after the title, when that name is free. */
    char title[48], name[48];
    notes_title(w->text_buf, w->text_len, title, sizeof title);
    notes_file_name(title, name, sizeof name);
    if (kstrcmp(f->name, name) && f->parent && !vfs_find(f->parent, name) && vfs_rename(f, name) == 0)
        vfs_get_path(f, s->open, NPATH);
    notes_scan(s);
}

static void note_open(window_t *w, nstate_t *s, const char *path) {
    note_save(w, s);
    fs_node_t *f = vfs_resolve_path(path);
    if (!f) return;
    kstrncpy(s->open, path, NPATH - 1); s->open[NPATH - 1] = '\0';
    win_clear(w);
    if (f->size) win_append(w, vfs_file_str(f));
    w->cursor_pos = w->text_len;
    s->scroll_px = 0; s->dirty = false; s->del_armed = false;
    s->focus = 0;
    s->follow_caret = true;
}

static void note_new(window_t *w, nstate_t *s) {
    note_save(w, s);
    fs_node_t *root = notes_root_dir(true);
    if (!root) { notify_push("Notes", "Notes needs a home folder to save into", g_theme->error); return; }
    fs_node_t *dir = root;
    if (s->sel_folder >= 0 && (u32)s->sel_folder < s->nfolders) {
        fs_node_t *d = vfs_find(root, s->folders[s->sel_folder]);
        if (d) dir = d;
    }
    char name[32];
    for (u32 i = 1; i < 100; i++) {
        kstrcpy(name, "Untitled");
        if (i > 1) { char b[6]; kstrcat(name, " "); kutoa(i, b, 10); kstrcat(name, b); }
        kstrcat(name, ".md");
        if (!vfs_find(dir, name)) break;
    }
    fs_node_t *f = vfs_mkfile(dir, name);
    if (!f) { notify_push("Notes", "Couldn't create a note here", g_theme->error); return; }
    const char *tmpl = "# New note\n\n";
    vfs_write(f, tmpl, (u32)kstrlen(tmpl));
    char path[NPATH]; vfs_get_path(f, path, sizeof path);
    notes_scan(s);
    note_open(w, s, path);
    w->cursor_pos = 2;                                    /* on the title */
}

/* -- Text editing --------------------------------------------------------------- */
static u32 line_start(window_t *w, u32 i) { while (i > 0 && w->text_buf[i - 1] != '\n') i--; return i; }
static u32 line_end_at(window_t *w, u32 i) { while (i < w->text_len && w->text_buf[i] != '\n') i++; return i; }

static void ed_insert(window_t *w, nstate_t *s, const char *t, u32 n) {
    if (!win_reserve(w, w->text_len + n + 1)) return;
    u32 c = w->cursor_pos > w->text_len ? w->text_len : w->cursor_pos;
    for (u32 i = w->text_len + 1; i-- > c;) w->text_buf[i + n] = w->text_buf[i];
    kmemcpy(w->text_buf + c, t, n);
    w->text_len += n; w->cursor_pos = c + n;
    s->dirty = true; s->dirty_ms = timer_ms(); s->follow_caret = true;
}
static void ed_erase(window_t *w, nstate_t *s, u32 at, u32 n) {
    if (at >= w->text_len) return;
    if (at + n > w->text_len) n = w->text_len - at;
    for (u32 i = at; i + n <= w->text_len; i++) w->text_buf[i] = w->text_buf[i + n];
    w->text_len -= n;
    s->dirty = true; s->dirty_ms = timer_ms(); s->follow_caret = true;
}
static void ed_toggle_prefix(window_t *w, nstate_t *s, const char *prefix) {
    u32 ls = line_start(w, w->cursor_pos);
    u32 pl = (u32)kstrlen(prefix);
    if (!win_reserve(w, w->text_len + pl + 1)) return;
    u32 before = w->text_len;
    w->text_len = notes_toggle_prefix(w->text_buf, w->text_len, w->text_cap, ls, prefix);
    if (w->text_len > before) w->cursor_pos += pl;
    else if (w->text_len < before) w->cursor_pos = w->cursor_pos >= ls + pl ? w->cursor_pos - pl : ls;
    s->dirty = true; s->dirty_ms = timer_ms();
}

/* -- Styled line layout ----------------------------------------------------------
 * Lays out text[0..n) word by word inside maxw. style_spans == false draws
 * it as plain source (the caret line). When caret >= 0 its position is
 * reported through cx/cy. Returns the height used. */
typedef struct { u32 slot; font_role_t role; u32 col, bold_col, italic_col; bool strike, draw; i32 gap; } lstyle_t;

static i32 layout_text(const char *t, u32 n, bool spans, const lstyle_t *st, i32 x0, i32 y0, i32 maxw,
                       i32 caret, i32 *cx, i32 *cy) {
    nspan_t sp[24]; u32 ns;
    if (spans) { ns = notes_spans(t, n, sp, 24); if (ns > 24) ns = 24; }
    else { sp[0].start = 0; sp[0].len = (u16)n; sp[0].style = 0; ns = n ? 1 : 0; }
    i32 lhp = gfx_role_line_h(st->role, st->slot) + st->gap;
    i32 x = x0, y = y0;
    if (caret == 0 && cx) { *cx = x; *cy = y; }
    for (u32 k = 0; k < ns; k++) {
        font_weight_t wt = (sp[k].style & NS_BOLD) ? FONT_W_BOLD : FONT_W_REGULAR;
        u32 col = (sp[k].style & NS_BOLD) ? st->bold_col : (sp[k].style & NS_ITALIC) ? st->italic_col : st->col;
        u32 i = sp[k].start, end = sp[k].start + sp[k].len;
        while (i < end) {
            u32 j = i;
            while (j < end && t[j] != ' ') j++;
            while (j < end && t[j] == ' ') j++;               /* word + its spaces */
            i32 ww = gfx_str_role_width_n(t + i, j - i, st->role, st->slot, wt);
            i32 wword = gfx_str_role_width_n(t + i, j - i - (t[j - 1] == ' ' ? 1 : 0), st->role, st->slot, wt);
            if (x > x0 && x + wword > x0 + maxw) { x = x0; y += lhp; }
            if (st->draw) {
                char buf[128]; u32 m = j - i < 127 ? j - i : 127;
                kmemcpy(buf, t + i, m); buf[m] = '\0';
                gfx_str_role(x, y, buf, col, st->role, st->slot, wt);
                if (st->strike) gfx_hline(x, y + gfx_role_line_h(st->role, st->slot) / 2 + 1, wword, col);
            }
            if (cx && caret > (i32)i && caret <= (i32)j) {
                *cx = x + gfx_str_role_width_n(t + i, (u32)caret - i, st->role, st->slot, wt);
                *cy = y;
            }
            x += ww; i = j;
        }
    }
    return y - y0 + lhp;
}

/* -- Panes ----------------------------------------------------------------------------- */
static void folders_pane(nstate_t *s, const npal_t *p, i32 x, i32 y, i32 w, i32 h) {
    gfx_rect(x, y, w, h, p->side);
    gfx_vline(x + w, y, h, p->line);
    i32 ix = x + 12, iw = w - 24, iy = y + 16;
    char num[12];
    struct { const char *label; u32 count; i32 id; } rows[2 + NFOLD_MAX];
    u32 nr = 0;
    rows[nr].label = "All notes"; rows[nr].count = s->nnotes; rows[nr].id = -1; nr++;
    for (u32 i = 0; i < s->nfolders; i++) { rows[nr].label = s->folders[i]; rows[nr].count = s->fcount[i]; rows[nr].id = (i32)i; nr++; }
    rows[nr].label = "Pinned"; rows[nr].count = s->npinned; rows[nr].id = -2; nr++;
    for (u32 i = 0; i < nr; i++) {
        bool on = rows[i].id == s->sel_folder;
        if (on) gfx_rect_rounded(ix, iy, iw, 40, 11, p->sel);
        kutoa(rows[i].count, num, 10);
        i32 nw = gfx_str_role_width(num, FONT_ROLE_UI, 0, FONT_W_REGULAR);
        gfx_str_role_fit(ix + 12, iy + (40 - lh(2)) / 2, iw - nw - 36, rows[i].label, p->ink, FONT_ROLE_UI, 2, on ? FONT_W_BOLD : FONT_W_REGULAR);
        gfx_str_role(ix + iw - 12 - nw, iy + (40 - lh(0)) / 2, num, p->muted, FONT_ROLE_UI, 0, FONT_W_REGULAR);
        hit(s, rect_make(ix, iy, iw, 40), NA_FOLDER, rows[i].id);
        iy += 42;
    }
    /* New folder: a dashed button, or the name field while typing one */
    i32 by = y + h - 16 - 40;
    if (s->focus == 2) {
        gfx_rect_rounded(ix, by, iw, 40, 11, p->field);
        gfx_rect_rounded_outline(ix, by, iw, 40, 11, p->honey);
        if (s->fname_len) gfx_str_role_fit(ix + 12, by + (40 - lh(2)) / 2, iw - 24, s->fname, p->ink, FONT_ROLE_UI, 2, FONT_W_REGULAR);
        else gfx_str_role_fit(ix + 12, by + (40 - lh(2)) / 2, iw - 24, "Folder name, Enter", p->muted, FONT_ROLE_UI, 2, FONT_W_REGULAR);
    } else {
        for (i32 dx = 0; dx < iw; dx += 8) { gfx_hline(ix + dx, by, 4, p->muted); gfx_hline(ix + dx, by + 39, 4, p->muted); }
        for (i32 dy = 0; dy < 40; dy += 8) { gfx_vline(ix, by + dy, 4, p->muted); gfx_vline(ix + iw - 1, by + dy, 4, p->muted); }
        ui_glyph(UI_GLYPH_PLUS, ix + 12, by + 12, 16, p->ink2);
        gfx_str_role(ix + 34, by + (40 - lh(2)) / 2, "New folder", p->ink2, FONT_ROLE_UI, 2, FONT_W_BOLD);
        hit(s, rect_make(ix, by, iw, 40), NA_NEW_FOLDER, 0);
    }
}

static bool note_visible(const nstate_t *s, const note_t *n) {
    if (s->sel_folder == -2 && !n->pinned) return false;
    if (s->sel_folder >= 0 && n->folder != s->sel_folder) return false;
    if (!s->search_len) return true;
    const char *q = s->search;
    for (u32 k = 0; k < 2; k++) {
        const char *hay = k == 0 ? n->title : n->snippet;
        for (; *hay; hay++) {
            u32 i = 0;
            while (q[i] && hay[i]) {
                char a = hay[i], b = q[i];
                if (a >= 'A' && a <= 'Z') a += 32;
                if (b >= 'A' && b <= 'Z') b += 32;
                if (a != b) break;
                i++;
            }
            if (!q[i]) return true;
        }
    }
    return false;
}

static void list_pane(nstate_t *s, const npal_t *p, i32 x, i32 y, i32 w, i32 h) {
    gfx_vline(x + w, y, h, p->line);
    /* search + new note */
    rect_t sr = rect_make(x + 16, y + 16, w - 32 - 48, 40);
    gfx_rect_rounded(sr.x, sr.y, sr.w, sr.h, 12, p->field);
    gfx_rect_rounded_outline(sr.x, sr.y, sr.w, sr.h, 12, s->focus == 1 ? p->honey : p->line);
    i32 ty = sr.y + (40 - lh(2)) / 2;
    if (s->search_len) gfx_str_role_fit(sr.x + 12, ty, sr.w - 24, s->search, p->ink, FONT_ROLE_UI, 2, FONT_W_REGULAR);
    else gfx_str_role_fit(sr.x + 12, ty, sr.w - 24, "Search notes", p->muted, FONT_ROLE_UI, 2, FONT_W_REGULAR);
    if (s->focus == 1 && ((timer_ms() / 500) & 1) == 0) {
        i32 cxp = sr.x + 12 + (s->search_len ? gfx_str_role_width(s->search, FONT_ROLE_UI, 2, FONT_W_REGULAR) : 0);
        if (cxp < sr.x + sr.w - 8) gfx_rect(cxp, ty, 2, lh(2), p->honey);
    }
    hit(s, sr, NA_SEARCH, 0);
    rect_t nb = rect_make(x + w - 16 - 40, y + 16, 40, 40);
    gfx_rect_rounded(nb.x, nb.y, nb.w, nb.h, 12, p->honey);
    ui_glyph(UI_GLYPH_PLUS, nb.x + 11, nb.y + 11, 18, rgb(0x3a, 0x26, 0x00));
    hit(s, nb, NA_NEW_NOTE, 0);
    gfx_hline(x, y + 72, w, p->line);

    i32 iy = y + 82, ix = x + 10, iw = w - 20;
    u32 shown = 0;
    gfx_set_clip(x, y + 73, w, h - 73);
    for (u32 i = 0; i < s->nnotes && iy < y + h; i++) {
        const note_t *n = &s->notes[i];
        if (!note_visible(s, n)) continue;
        shown++;
        bool on = !kstrcmp(n->path, s->open);
        i32 ch = 12 + lh(2) + 4 + lh(1) + 12;
        if (on) gfx_rect_rounded(ix, iy, iw, ch, 14, p->sel);
        i32 tx = ix + 14, tw = iw - 28;
        if (n->pinned) { ui_glyph(UI_GLYPH_HEART, ix + iw - 30, iy + 12, 16, p->honey); tw -= 22; }
        gfx_str_role_fit(tx, iy + 12, tw, n->title, p->ink, FONT_ROLE_UI, 2, FONT_W_BOLD);
        gfx_str_role_fit(tx, iy + 12 + lh(2) + 4, iw - 28, n->snippet[0] ? n->snippet : "No more text", on ? p->ink2 : p->muted,
                         FONT_ROLE_UI, 1, FONT_W_REGULAR);
        hit(s, rect_make(ix, iy, iw, ch), NA_NOTE, (i32)i);
        iy += ch + 4;
    }
    gfx_clear_clip();
    if (!shown) {
        const char *a = s->search_len ? "No notes match." : s->sel_folder == -2 ? "Nothing pinned yet." : "No notes yet.";
        const char *b = s->search_len ? "Try another word." : s->sel_folder == -2 ? "Pin a note from its toolbar." : "Press + to write one.";
        gfx_str_role(ix + 14, y + 96, a, p->ink, FONT_ROLE_UI, 2, FONT_W_BOLD);
        gfx_str_role(ix + 14, y + 96 + lh(2) + 2, b, p->muted, FONT_ROLE_UI, 1, FONT_W_REGULAR);
    }
}

static void editor_pane(window_t *w, nstate_t *s, const npal_t *p, i32 x, i32 y, i32 wd, i32 h) {
    i32 padx = wd >= 640 ? 48 : 22;
    i32 ex = x + padx, ew = wd - 2 * padx;
    if (ew > 720) ew = 720;
    if (!s->open[0]) {
        const char *a = s->nnotes ? "Pick a note, or start a new one." : "Your notes live here.";
        i32 tw = gfx_str_role_width(a, FONT_ROLE_DISPLAY, 1, FONT_W_BOLD);
        gfx_str_role(x + (wd - tw) / 2, y + h / 2 - 50, a, p->ink, FONT_ROLE_DISPLAY, 1, FONT_W_BOLD);
        const char *b = "They're saved as .md files in ~/Notes.";
        tw = gfx_str_role_width(b, FONT_ROLE_UI, 2, FONT_W_REGULAR);
        gfx_str_role(x + (wd - tw) / 2, y + h / 2 - 10, b, p->muted, FONT_ROLE_UI, 2, FONT_W_REGULAR);
        i32 bw = gfx_str_role_width("New note", FONT_ROLE_UI, 2, FONT_W_BOLD) + 36;
        rect_t r = rect_make(x + (wd - bw) / 2, y + h / 2 + 24, bw, 42);
        gfx_rect_rounded(r.x, r.y, r.w, r.h, 12, p->honey);
        gfx_str_role(r.x + 18, r.y + (42 - lh(2)) / 2, "New note", rgb(0x3a, 0x26, 0x00), FONT_ROLE_UI, 2, FONT_W_BOLD);
        hit(s, r, NA_NEW_NOTE, 0);
        return;
    }
    /* Toolbar: B I H checklist | pin, delete */
    i32 ty = y + 22;
    gfx_rect_rounded(ex, ty, 4 * 40 + 8, 42, 12, p->field);
    static const u8 acts[4] = { NA_BOLD, NA_ITALIC, NA_HEAD, NA_CHECKLIST };
    for (i32 i = 0; i < 4; i++) {
        i32 bx = ex + 4 + i * 40;
        if (i == 0) gfx_str_role(bx + 13, ty + (42 - lh(3)) / 2, "B", p->ink, FONT_ROLE_UI, 3, FONT_W_BOLD);
        if (i == 1) gfx_str_role(bx + 16, ty + (42 - lh(3)) / 2, "I", p->ink2, FONT_ROLE_UI, 3, FONT_W_REGULAR);
        if (i == 2) gfx_str_role(bx + 13, ty + (42 - lh(3)) / 2, "H", p->ink, FONT_ROLE_UI, 3, FONT_W_BOLD);
        if (i == 3) {
            gfx_rect_rounded_outline(bx + 11, ty + 12, 18, 18, 5, p->ink);
            ui_glyph(UI_GLYPH_CHECK, bx + 12, ty + 13, 16, p->ink);
        }
        hit(s, rect_make(bx, ty + 4, 36, 34), acts[i], 0);
    }
    {
        bool pinned = false;
        for (u32 i = 0; i < s->nnotes; i++) if (!kstrcmp(s->notes[i].path, s->open)) pinned = s->notes[i].pinned;
        i32 rx = x + wd - padx;
        bool armed = s->del_armed && timer_ms() - s->del_ms < 3000;
        if (!armed) s->del_armed = false;
        const char *dl = armed ? "Delete?" : NULL;
        i32 dw = dl ? gfx_str_role_width(dl, FONT_ROLE_UI, 2, FONT_W_BOLD) + 28 : 42;
        rx -= dw;
        gfx_rect_rounded(rx, ty, dw, 42, 12, armed ? g_theme->error : p->field);
        if (dl) gfx_str_role(rx + 14, ty + (42 - lh(2)) / 2, dl, rgb(0xff, 0xff, 0xff), FONT_ROLE_UI, 2, FONT_W_BOLD);
        else ui_glyph(UI_GLYPH_TRASH, rx + 12, ty + 12, 18, p->ink2);
        hit(s, rect_make(rx, ty, dw, 42), NA_DELETE, 0);
        const char *pl = pinned ? "Pinned" : "Pin";
        i32 pw = gfx_str_role_width(pl, FONT_ROLE_UI, 2, FONT_W_BOLD) + 44;
        rx -= 8 + pw;
        gfx_rect_rounded(rx, ty, pw, 42, 12, pinned ? p->sel : p->field);
        ui_glyph(UI_GLYPH_HEART, rx + 12, ty + 12, 18, pinned ? p->honey : p->ink2);
        gfx_str_role(rx + 34, ty + (42 - lh(2)) / 2, pl, p->ink, FONT_ROLE_UI, 2, FONT_W_BOLD);
        hit(s, rect_make(rx, ty, pw, 42), NA_PIN, 0);
    }

    /* Body */
    i32 top = ty + 42 + 22, bottom = y + h - 12;
    s->view_h = bottom - top;
    if (s->scroll_px > s->content_h - s->view_h + 40) s->scroll_px = s->content_h - s->view_h + 40;
    if (s->scroll_px < 0) s->scroll_px = 0;
    gfx_set_clip(x, top, wd, bottom - top);
    hit(s, rect_make(x, top, wd, bottom - top), NA_EDITOR, 0);
    u32 caret = w->cursor_pos > w->text_len ? w->text_len : w->cursor_pos;
    u32 cl = line_start(w, caret);
    i32 cy = top - s->scroll_px;
    i32 caret_x = -1, caret_y = 0, caret_h = lh(3);
    s->nlines = 0;
    u32 i = 0;
    bool first_line = true;
    for (;;) {
        u32 e = line_end_at(w, i);
        const char *t = w->text_buf + i;
        u32 n = e - i, off = 0;
        nl_kind_t k = notes_line_kind(t, n, &off);
        bool raw = i == cl;                                   /* caret line: source */
        if (s->nlines < 256) { s->line_y[s->nlines] = cy; s->line_off[s->nlines] = i; s->nlines++; }
        lstyle_t st = { 3, FONT_ROLE_UI, p->dark ? p->ink : rgb(0x33, 0x39, 0x4f), p->ink, p->ink2, false, true, 8 };
        i32 lx = ex, lw = ew, hgt;
        i32 ccx = -1, ccy = 0;
        i32 rel_caret = raw ? (i32)(caret - i) : -1;
        if (k == NL_H1) { st.role = FONT_ROLE_DISPLAY; st.slot = 2; st.col = p->ink; st.bold_col = p->ink; st.gap = 6; }
        else if (k == NL_H2) { st.slot = 4; st.col = p->ink; st.gap = 6; }
        if (raw) {
            if (k != NL_H1 && k != NL_H2) st.col = p->ink;
            hgt = layout_text(t, n, false, &st, lx, cy, lw, rel_caret, &ccx, &ccy);
            caret_h = gfx_role_line_h(st.role, st.slot);
        } else if (k == NL_BLANK) {
            hgt = lh(3) / 2 + 4;
        } else if (k == NL_CHECK || k == NL_CHECKED) {
            bool on = k == NL_CHECKED;
            i32 by = cy + (lh(3) - 20) / 2 + 1;
            if (on) { gfx_rect_rounded(lx, by, 20, 20, 6, p->honey); ui_glyph(UI_GLYPH_CHECK, lx + 2, by + 2, 16, rgb(0xff, 0xff, 0xff)); }
            else { gfx_rect_rounded_outline(lx, by, 20, 20, 5, p->muted); gfx_rect_rounded_outline(lx + 1, by + 1, 18, 18, 4, p->muted); }
            hit(s, rect_make(lx - 4, by - 4, 28, 28), NA_CHECKBOX, (i32)i);
            if (on) { st.col = st.bold_col = st.italic_col = p->muted; st.strike = true; }
            hgt = layout_text(t + off, n - off, true, &st, lx + 32, cy, lw - 32, -1, NULL, NULL);
        } else if (k == NL_QUOTE) {
            st.col = st.bold_col = st.italic_col = p->quote_ink; st.slot = 3;
            lstyle_t ms = st; ms.draw = false;
            i32 qh = layout_text(t + off, n - off, true, &ms, lx + 18, cy + 14, lw - 36, -1, NULL, NULL) + 28 - 8;
            gfx_rect_rounded(lx, cy + 4, lw, qh, 14, p->quote);
            layout_text(t + off, n - off, true, &st, lx + 18, cy + 4 + 14, lw - 36, -1, NULL, NULL);
            hgt = qh + 12;
        } else if (k == NL_BULLET) {
            gfx_circle_fill(lx + 6, cy + lh(3) / 2 + 1, 3, p->ink2);
            hgt = layout_text(t + off, n - off, true, &st, lx + 22, cy, lw - 22, -1, NULL, NULL);
        } else {
            hgt = layout_text(t + off, n - off, true, &st, lx, cy, lw, -1, NULL, NULL);
        }
        if (k == NL_H1 && first_line) hgt += 4;
        if (k != NL_BLANK) first_line = false;
        if (raw && ccx >= 0) { caret_x = ccx; caret_y = ccy; }
        hit(s, rect_make(x, cy, wd, hgt), NA_LINE, (i32)i);
        cy += hgt + 4;
        if (e >= w->text_len) break;
        i = e + 1;
    }
    s->content_h = cy + s->scroll_px - top;
    if (caret_x >= 0 && w->focused && s->focus == 0 && ((timer_ms() / 500) & 1) == 0)
        gfx_rect(caret_x, caret_y, 2, caret_h, p->honey);
    gfx_clear_clip();
    /* keep the caret in view after typing or moving */
    if (s->follow_caret && caret_x >= 0) {
        if (caret_y < top) s->scroll_px -= top - caret_y + 8;
        else if (caret_y + caret_h > bottom) s->scroll_px += caret_y + caret_h - bottom + 8;
        s->follow_caret = false;
    }
    /* autosave after a pause */
    if (s->dirty && timer_ms() - s->dirty_ms > 2000) note_save(w, s);
}

/* -- Entry points ------------------------------------------------------------------- */
void app_notes_init(window_t *w){
    win_clear(w);
    w->cursor_pos = 0;
    nstate_t *s = nst(w);
    if (!s) return;
    notes_scan(s);
    if (s->nnotes) note_open(w, s, s->notes[0].path);
}

void app_notes_close(window_t *w) {
    nstate_t *s = (nstate_t *)w->app_state;
    if (s) note_save(w, s);
}

void app_notes_draw(window_t *w){
    rect_t cr = wm_client_rect(w);
    nstate_t *s = nst(w);
    npal_t p = npal();
    gfx_rect(cr.x, cr.y, cr.w, cr.h, p.body);
    if (!s) return;
    s->nhits = 0;
    i32 fw = cr.w >= 900 ? 210 : 0;
    i32 lw = cr.w >= 620 ? (cr.w >= 1100 ? 320 : 270) : 0;
    if (fw) folders_pane(s, &p, cr.x, cr.y, fw, cr.h);
    if (lw) list_pane(s, &p, cr.x + fw + (fw ? 1 : 0), cr.y, lw, cr.h);
    i32 ex = cr.x + fw + (fw ? 1 : 0) + lw + (lw ? 1 : 0);
    editor_pane(w, s, &p, ex, cr.y, cr.x + cr.w - ex, cr.h);
}

void app_notes_click(window_t *w, i32 x, i32 y){
    nstate_t *s = nst(w);
    if (!s) return;
    /* Controls (checkboxes, buttons) win over the line / editor areas that
     * are registered around them. */
    const nhit_t *h = NULL;
    for (u32 pass = 0; pass < 2 && !h; pass++)
        for (u32 k = s->nhits; k-- > 0;) {
            const nhit_t *c = &s->hits[k];
            bool area = c->act == NA_LINE || c->act == NA_EDITOR;
            if (area != (pass == 1) || !rect_contains(c->r, x, y)) continue;
            h = c; break;
        }
    if (h) {
        if (h->act != NA_SEARCH && s->focus == 1) s->focus = 0;
        if (h->act != NA_DELETE) s->del_armed = false;
        switch (h->act) {
        case NA_FOLDER: s->sel_folder = h->arg; break;
        case NA_NOTE:   if ((u32)h->arg < s->nnotes) note_open(w, s, s->notes[h->arg].path); break;
        case NA_SEARCH: s->focus = 1; break;
        case NA_NEW_NOTE: note_new(w, s); break;
        case NA_NEW_FOLDER: s->focus = 2; s->fname_len = 0; s->fname[0] = '\0'; break;
        case NA_BOLD:   ed_insert(w, s, "****", 4); w->cursor_pos -= 2; s->focus = 0; break;
        case NA_ITALIC: ed_insert(w, s, "**", 2); w->cursor_pos -= 1; s->focus = 0; break;
        case NA_HEAD:   ed_toggle_prefix(w, s, "## "); s->focus = 0; break;
        case NA_CHECKLIST: ed_toggle_prefix(w, s, "- [ ] "); s->focus = 0; break;
        case NA_PIN:    note_save(w, s); pinned_toggle(s, s->open); break;
        case NA_DELETE:
            if (s->del_armed) {
                fs_node_t *f = vfs_resolve_path(s->open);
                s->dirty = false;
                if (f && vfs_delete(f) == 0) { s->open[0] = '\0'; win_clear(w); notes_scan(s); }
                else notify_push("Notes", "Couldn't delete the note", g_theme->error);
                s->del_armed = false;
            } else { s->del_armed = true; s->del_ms = timer_ms(); }
            break;
        case NA_CHECKBOX:
            if (notes_toggle_check(w->text_buf, w->text_len, (u32)h->arg)) { s->dirty = true; s->dirty_ms = timer_ms(); }
            break;
        case NA_LINE: {
            /* caret to the end of the clicked line (or its start when the
             * click is left of the text) */
            u32 ls = (u32)h->arg, le = line_end_at(w, ls);
            w->cursor_pos = (x < h->r.x + 80) ? ls : le;
            s->focus = 0;
            break;
        }
        case NA_EDITOR: w->cursor_pos = w->text_len; s->focus = 0; break;
        default: break;
        }
        return;
    }
}

static void notes_make_folder(nstate_t *s) {
    fs_node_t *root = notes_root_dir(true);
    char name[32]; u32 o = 0;
    for (u32 i = 0; i < s->fname_len && o < 31; i++) {
        char c = s->fname[i];
        if (c == '/' || c == '.' || (c == ' ' && o == 0)) continue;
        name[o++] = c;
    }
    name[o] = '\0';
    if (root && o && !vfs_find(root, name) && vfs_mkdir(root, name)) notes_scan(s);
    else if (o) notify_push("Notes", "Couldn't make that folder", g_theme->error);
    s->focus = 0;
}

void app_notes_key(window_t *w,char c){
    nstate_t *s = nst(w);
    if (!s) return;
    u8 k = (u8)c;
    if (c == 0x13) { note_save(w, s); return; }                       /* Ctrl+S */
    if (c == 0x0E) { note_new(w, s); return; }                        /* Ctrl+N */
    if (c == 0x06) { s->focus = 1; return; }                          /* Ctrl+F */
    if (s->focus == 1 || s->focus == 2) {
        char *buf = s->focus == 1 ? s->search : s->fname;
        u32 *len = s->focus == 1 ? &s->search_len : &s->fname_len;
        u32 cap = s->focus == 1 ? sizeof s->search : sizeof s->fname;
        if (c == 0x1B) { if (s->focus == 1) { *len = 0; buf[0] = '\0'; } s->focus = 0; return; }
        if (c == '\n') { if (s->focus == 2) notes_make_folder(s); else s->focus = 0; return; }
        if (c == '\b') { if (*len) buf[--*len] = '\0'; return; }
        if (c >= 32 && c < 127 && *len + 1 < cap) { buf[(*len)++] = c; buf[*len] = '\0'; }
        return;
    }
    if (!s->open[0]) { if (c == '\n') note_new(w, s); return; }
    u32 cp = w->cursor_pos > w->text_len ? w->text_len : w->cursor_pos;
    w->cursor_pos = cp;
    s->follow_caret = true;
    if (k == KEY_NAV_LEFT)  { if (cp) w->cursor_pos--; return; }
    if (k == KEY_NAV_RIGHT) { if (cp < w->text_len) w->cursor_pos++; return; }
    if (k == KEY_NAV_HOME)  { w->cursor_pos = line_start(w, cp); return; }
    if (k == KEY_NAV_END)   { w->cursor_pos = line_end_at(w, cp); return; }
    if (k == KEY_NAV_UP || k == KEY_NAV_DOWN) {
        u32 ls = line_start(w, cp), col = cp - ls;
        u32 ns;
        if (k == KEY_NAV_UP) { if (!ls) { w->cursor_pos = 0; return; } ns = line_start(w, ls - 1); }
        else { u32 le = line_end_at(w, cp); if (le >= w->text_len) { w->cursor_pos = w->text_len; return; } ns = le + 1; }
        u32 ne = line_end_at(w, ns);
        w->cursor_pos = ns + (col < ne - ns ? col : ne - ns);
        return;
    }
    if (k == KEY_NAV_PGUP)  { s->scroll_px -= s->view_h * 3 / 4; s->follow_caret = false; return; }
    if (k == KEY_NAV_PGDN)  { s->scroll_px += s->view_h * 3 / 4; s->follow_caret = false; return; }
    if (k == KEY_NAV_DELETE) { ed_erase(w, s, cp, 1); return; }
    if (c == '\b') { if (cp) { ed_erase(w, s, cp - 1, 1); w->cursor_pos = cp - 1; } return; }
    if (c == 0x02) { ed_insert(w, s, "****", 4); w->cursor_pos -= 2; return; }   /* Ctrl+B */
    if (c == 0x03) {                                                   /* Ctrl+C: whole note */
        u32 n = w->text_len < CLIPBOARD_SIZE - 1 ? w->text_len : CLIPBOARD_SIZE - 1;
        kmemcpy(g_clipboard, w->text_buf, n); g_clipboard[n] = '\0'; g_clipboard_len = n;
        notify_push("Clipboard", "Copied the note", COL_ACCENT);
        return;
    }
    if (c == 0x16) { ed_insert(w, s, g_clipboard, g_clipboard_len); return; }   /* Ctrl+V */
    if (c == '\n') {
        /* Enter continues a list: "- [ ] ", "- " or "> " carry on; on an
         * empty item it ends the list instead. */
        u32 ls = line_start(w, cp), off = 0;
        nl_kind_t kd = notes_line_kind(w->text_buf + ls, line_end_at(w, cp) - ls, &off);
        const char *cont = kd == NL_CHECK || kd == NL_CHECKED ? "- [ ] " : kd == NL_BULLET ? "- " : kd == NL_QUOTE ? "> " : NULL;
        if (cont && line_end_at(w, cp) - ls <= off) { ed_erase(w, s, ls, line_end_at(w, cp) - ls); w->cursor_pos = ls; return; }
        ed_insert(w, s, "\n", 1);
        if (cont) ed_insert(w, s, cont, (u32)kstrlen(cont));
        return;
    }
    if (c >= 32 && c < 127) { char ch = c; ed_insert(w, s, &ch, 1); }
}
