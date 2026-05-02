/* CareOS v9 -- apps/app_editor.c -- Code Editor
 * w->tab: 0=normal, 1=find, 2=replace, 3=open/save-dialog
 * w->input_buf[0..127]: find string (tab=1/2) or path (tab=3); w->input_len = length
 * w->input_buf[128..255]: replace string (tab=2, null-terminated)
 * w->cursor_pos: in tab=2: 0=find field, 1=replace field
 *                in tab=3: 0=Open mode, 1=Save mode */
#include "apps_common.h"

/* split "/a/b/c" → dir="/a/b" name="c"  (handles root and no-slash cases) */
static void editor_split_path(const char *path, char *dir, u32 dmax, char *name, u32 nmax) {
    const char *slash = kstrrchr(path, '/');
    if (!slash) { dir[0]='\0'; kstrncpy(name,path,nmax-1); name[nmax-1]='\0'; return; }
    u32 dlen = (u32)(slash-path);
    if (dlen==0) { dir[0]='/'; dir[1]='\0'; }
    else { u32 c=dlen<dmax-1?dlen:dmax-1; kmemcpy(dir,path,c); dir[c]='\0'; }
    kstrncpy(name,slash+1,nmax-1); name[nmax-1]='\0';
}

/* Save w->text_buf to path, creating intermediate file if needed.
 * If path is a directory, appends /untitled.cl.
 * Updates w->editor_path, clears modified flag, closes dialog. */
static void editor_save_to_path(window_t *w, const char *path) {
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
        if (!parent || parent->type!=FS_DIR) {
            notify_push("Editor","Directory not found",g_theme->error); return;
        }
        target = vfs_mkfile(parent, name_p);
    }

    if (!target) { notify_push("Editor","Cannot create file",g_theme->error); return; }

    vfs_write(target, w->text_buf, w->text_len);
    kstrncpy(w->editor_path,sp,FS_PATH_MAX-1); w->editor_path[FS_PATH_MAX-1]='\0';
    w->editor_modified = false;
    w->tab = 0;
    w->cursor_pos = 0;
    notify_push("Editor","File saved",g_theme->success);
}

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
    w->editor_show_sidebar = true;
    w->editor_sidebar_tab = 1; /* Docs by default */
    w->fm_dir = vfs_resolve_path("/home/user");
    if (!w->fm_dir) w->fm_dir = vfs_root();
}

static void editor_find(window_t *w) {
    if (!w->input_buf[0]) return;
    u32 nlen = (u32)kstrlen(w->input_buf);
    const char *p = w->text_buf;
    u32 line = 0;
    while (*p) {
        const char *start = p;
        while (*p && *p != '\n') p++;
        const char *q = start;
        while ((u32)(q - start) + nlen <= (u32)(p - start)) {
            if (kmemcmp(q, w->input_buf, nlen) == 0) {
                w->scroll = (line > 2) ? line - 2 : 0;
                return;
            }
            q++;
        }
        if (*p == '\n') { p++; line++; } else break;
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
    for (u32 i = 0; i + flen <= w->text_len; i++) {
        if (kmemcmp(w->text_buf + i, find, flen) == 0) { found_at = i; break; }
    }
    if (found_at == w->text_len) { notify_push("Editor", "Not found", g_theme->warning); return; }

    u32 new_len = w->text_len - flen + rlen;
    if (new_len >= WIN_TEXT_BUF - 1) return;

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
}

void app_editor_draw(window_t *w){
    rect_t cr = wm_client_rect(w);
    i32 sc = (i32)GFX_FONT_SCALE;
    u32 editor_bg = rgb(0x10, 0x14, 0x20);
    u32 gutter_bg = rgb(0x0b, 0x0f, 0x1a);
    i32 sb_h  = 20 + 4 * sc;
    i32 bar_h = (w->tab != 0) ? 32 : 0;
    
    i32 sidebar_w = 0;
    if (w->editor_show_sidebar) {
        sidebar_w = 200 * sc;
        gfx_rect(cr.x, cr.y, sidebar_w, cr.h, g_theme->surface2);
        gfx_vline(cr.x + sidebar_w, cr.y, cr.h, COL_BORDER);
        
        gfx_rect(cr.x, cr.y, sidebar_w, 24 * sc, g_theme->surface3);
        gfx_hline(cr.x, cr.y + 24 * sc, sidebar_w, COL_BORDER);
        gfx_str(cr.x + 8, cr.y + 6 * sc, "Explorer", w->editor_sidebar_tab == 0 ? COL_PRIMARY : COL_DIM, COL_TRANSPARENT);
        gfx_str(cr.x + 90 * sc, cr.y + 6 * sc, "Docs", w->editor_sidebar_tab == 1 ? COL_PRIMARY : COL_DIM, COL_TRANSPARENT);
        
        i32 sy = cr.y + 24 * sc + 4;
        if (w->editor_sidebar_tab == 0) {
            if (w->fm_dir) {
                /* Parent dir link */
                if (w->fm_dir->parent) {
                    if (w->fm_sel == 0xFFFFFFFF) gfx_rect(cr.x + 2, sy, sidebar_w - 4, 16 * sc, COL_SELECTION);
                    gfx_str_clipped(cr.x + 8, sy + 2, sidebar_w - 12, "..", COL_PRIMARY, COL_TRANSPARENT);
                    sy += 16 * sc;
                }
                for (u32 i = 0; i < w->fm_dir->child_count && sy < cr.y + cr.h - 20; i++) {
                    fs_node_t *ch = w->fm_dir->children[i];
                    u32 c_col = ch->type == FS_DIR ? COL_PRIMARY : COL_TEXT;
                    if (i == w->fm_sel) gfx_rect(cr.x + 2, sy, sidebar_w - 4, 16 * sc, COL_SELECTION);
                    gfx_str_clipped(cr.x + 8, sy + 2, sidebar_w - 12, ch->name, c_col, COL_TRANSPARENT);
                    sy += 16 * sc;
                }
            }
        } else {
            static const char *CL_DOCS[] = {
                "Care Language (CL)", "",
                "1. Basics",
                "var x = 10;",
                "var n = \"Bob\";",
                "print x;", "",
                "2. Functions",
                "func add(a,b) {",
                "  return a+b;",
                "}", "",
                "3. Control Flow",
                "if (x>5) {",
                "  print \"Big\";",
                "} else {",
                "  print \"Small\";",
                "}",
                "while (x>0) {",
                "  x = x-1;",
                "}", "",
                "4. Operators",
                "+ - * / == !=",
                "< > <= >=", NULL
            };
            for (int i=0; CL_DOCS[i] != NULL && sy < cr.y + cr.h - 16; i++) {
                u32 tc = COL_DIM;
                if (CL_DOCS[i][0] >= '1' && CL_DOCS[i][0] <= '9') tc = COL_PRIMARY;
                else if (kstrncmp(CL_DOCS[i], "Care", 4) == 0) tc = COL_ACCENT;
                else if (kstrncmp(CL_DOCS[i], "func", 4) == 0 || kstrncmp(CL_DOCS[i], "var", 3) == 0) tc = COL_TEXT;
                gfx_str_clipped(cr.x + 8, sy, sidebar_w - 12, CL_DOCS[i], tc, COL_TRANSPARENT);
                sy += 14 * sc;
            }
        }
        cr.x += sidebar_w + 1;
        cr.w -= sidebar_w + 1;
    }

    i32 content_h = cr.h - sb_h - bar_h;
    gfx_rect(cr.x, cr.y, cr.w, cr.h, editor_bg);

    /* Status bar */
    gfx_rect(cr.x, cr.y + cr.h - sb_h, cr.w, sb_h, g_theme->surface2);
    gfx_hline(cr.x, cr.y + cr.h - sb_h, cr.w, COL_BORDER);
    i32 sb_ty = cr.y + cr.h - sb_h + (sb_h - FONT_H * sc) / 2;
    const char *fname = w->editor_path[0] ? w->editor_path : "untitled";
    gfx_str(cr.x + 8, sb_ty, fname, g_theme->dim, COL_TRANSPARENT);
    gfx_str(cr.x + 8 + (i32)kstrlen(fname) * FONT_W * sc + 8, sb_ty,
            "|  C  |  UTF-8", g_theme->muted, COL_TRANSPARENT);
    if (w->editor_modified)
        gfx_str_right(cr.x, sb_ty, cr.w - 8, "modified", g_theme->warning, COL_TRANSPARENT);

    /* Find/Replace/Open bar */
    if (bar_h > 0) {
        i32 bar_y = cr.y + cr.h - sb_h - bar_h;
        i32 ty    = bar_y + (bar_h - FONT_H * sc) / 2;
        i32 fh    = 20;
        i32 fy2   = bar_y + (bar_h - fh) / 2;

        gfx_rect(cr.x, bar_y, cr.w, bar_h, g_theme->surface3);
        gfx_hline(cr.x, bar_y, cr.w, COL_BORDER);

        if (w->tab == 1 || w->tab == 2) {
            i32 lbl_w = 5 * FONT_W * sc + 8;
            gfx_str(cr.x + 8, ty, "Find:", g_theme->dim, COL_TRANSPARENT);
            i32 ff_x = cr.x + 8 + lbl_w;
            i32 ff_w = (w->tab == 2) ? cr.w / 2 - 8 - lbl_w - 16
                                      : cr.w - 8 - lbl_w - 76;
            if (ff_w < 20) ff_w = 20;
            gfx_rect_rounded(ff_x, fy2, ff_w, fh, 4, COL_INPUT_BG);
            if (w->tab == 1 || w->cursor_pos == 0)
                gfx_rect_outline(ff_x, fy2, ff_w, fh, COL_PRIMARY);
            gfx_set_clip(ff_x + 4, fy2, ff_w - 8, fh);
            gfx_str(ff_x + 4, ty, w->input_buf, COL_TEXT, COL_INPUT_BG);
            gfx_clear_clip();

            if (w->tab == 2) {
                i32 rl_x  = cr.x + cr.w / 2 + 8;
                i32 rl_w  = 8 * FONT_W * sc + 4;
                gfx_str(rl_x, ty, "Replace:", g_theme->dim, COL_TRANSPARENT);
                i32 rf_x  = rl_x + rl_w;
                i32 rf_w  = cr.w - rf_x - 76;
                if (rf_w > 20) {
                    gfx_rect_rounded(rf_x, fy2, rf_w, fh, 4, COL_INPUT_BG);
                    if (w->cursor_pos == 1)
                        gfx_rect_outline(rf_x, fy2, rf_w, fh, COL_PRIMARY);
                    gfx_set_clip(rf_x + 4, fy2, rf_w - 8, fh);
                    gfx_str(rf_x + 4, ty, w->input_buf + 128, COL_TEXT, COL_INPUT_BG);
                    gfx_clear_clip();
                }
                gfx_str(cr.x + cr.w - 72, ty, "Tab=switch", g_theme->muted, COL_TRANSPARENT);
            } else {
                gfx_str(cr.x + cr.w - 68, ty, "Esc=close", g_theme->muted, COL_TRANSPARENT);
            }
        } else if (w->tab == 3) {
            i32 lbl_w = 5 * FONT_W * sc + 8;
            gfx_str(cr.x + 8, ty, w->cursor_pos==1 ? "Save:" : "Open:", g_theme->dim, COL_TRANSPARENT);
            i32 ff_x = cr.x + 8 + lbl_w;
            i32 ff_w = cr.w - 8 - lbl_w - 76;
            if (ff_w < 20) ff_w = 20;
            gfx_rect_rounded(ff_x, fy2, ff_w, fh, 4, COL_INPUT_BG);
            gfx_rect_outline(ff_x, fy2, ff_w, fh, COL_PRIMARY);
            gfx_set_clip(ff_x + 4, fy2, ff_w - 8, fh);
            gfx_str(ff_x + 4, ty, w->input_buf, COL_TEXT, COL_INPUT_BG);
            gfx_clear_clip();
            gfx_str(cr.x + cr.w - 72, ty, "Esc=cancel", g_theme->muted, COL_TRANSPARENT);
        }
    }

    /* Gutter */
    i32 lnw = 32 + 8 * sc;
    gfx_rect(cr.x, cr.y, lnw, content_h, gutter_bg);
    gfx_vline(cr.x + lnw, cr.y, content_h, COL_BORDER);

    /* Lines */
    u32 lh = (u32)(FONT_H * sc) + 3;
    u32 line = 0; const char *p = w->text_buf;
    while (line < w->scroll && *p) {
        if (*p == '\n') line++;
        p++;
    }

    i32 y = cr.y + 4;
    u32 cur_line = line;
    while (*p && y < cr.y + content_h - (i32)lh) {
        char n[5]; kutoa(cur_line + 1, n, 10);
        gfx_str_right(cr.x, y, lnw - 4, n, g_theme->muted, COL_TRANSPARENT);

        const char *end = p;
        while (*end && *end != '\n') end++;
        u32 len = (u32)(end - p);
        char tmp[140]; if (len > 139) len = 139;
        kmemcpy(tmp, p, len); tmp[len] = '\0';

        /* trim leading whitespace for keyword matching */
        const char *tr = tmp;
        while (*tr==' '||*tr=='\t') tr++;

        u32 fc = COL_TEXT;
        /* comments */
        if (*tr=='#' || kstrncmp(tr,"//",2)==0 || kstrncmp(tr,"/*",2)==0) fc = g_theme->muted;
        /* Care keywords */
        else if (kstrncmp(tr,"var ",4)==0)    fc = COL_CYAN;
        else if (kstrncmp(tr,"print ",6)==0 || kstrncmp(tr,"print;",6)==0) fc = g_theme->success;
        else if (kstrncmp(tr,"if ",3)==0   || kstrncmp(tr,"if(",3)==0)   fc = COL_PURPLE;
        else if (kstrncmp(tr,"else",4)==0)    fc = COL_PURPLE;
        else if (kstrncmp(tr,"while ",6)==0 || kstrncmp(tr,"while(",6)==0) fc = COL_PURPLE;
        else if (kstrncmp(tr,"return",6)==0)  fc = COL_PURPLE;
        else if (kstrncmp(tr,"func ",5)==0)   fc = COL_CYAN;
        /* C keywords (for editing C files too) */
        else if (kstrncmp(tr,"#include",8)==0||kstrncmp(tr,"#define",7)==0) fc = COL_PURPLE;
        else if (kstrncmp(tr,"int ",4)==0||kstrncmp(tr,"void ",5)==0||kstrncmp(tr,"char ",5)==0) fc = COL_CYAN;
        else if (kstrncmp(tr,"sys_",4)==0) fc = g_theme->success;
        else if (*tr == '}' || *tr == '{') fc = COL_DIM;

        gfx_str(cr.x + lnw + 6, y, tmp, fc, COL_TRANSPARENT);

        if (w->focused && *(end) == '\0' && (timer_get_ticks()/40)%2 == 0) {
            i32 cx = cr.x + lnw + 6 + (i32)kstrlen(tmp) * FONT_W * sc;
            gfx_vline(cx, y, (i32)lh - 2, COL_ACCENT);
        }

        y += lh; cur_line++;
        if (*end == '\n') p = end + 1; else break;
    }
}

void app_editor_key(window_t *w, char c){
    /* Mode-activation shortcuts (always available) */
    if (c == 0x02) { /* Ctrl+B: Toggle Sidebar */
        w->editor_show_sidebar = !w->editor_show_sidebar;
        return;
    }
    if (c == 0x06) { /* Ctrl+F: Find */
        w->tab = 1; w->cursor_pos = 0;
        w->input_buf[0] = '\0'; w->input_len = 0;
        return;
    }
    if (c == 0x12) { /* Ctrl+R: Replace */
        w->tab = 2; w->cursor_pos = 0;
        w->input_buf[0] = '\0'; w->input_len = 0;
        w->input_buf[128] = '\0';
        return;
    }
    if (c == 0x0F) { /* Ctrl+O: Open */
        w->tab = 3;
        w->cursor_pos = 0; /* open mode */
        kstrncpy(w->input_buf, w->editor_path, 127);
        w->input_buf[127] = '\0';
        w->input_len = (u32)kstrlen(w->input_buf);
        return;
    }

    /* Find mode */
    if (w->tab == 1) {
        if (c == 0x1B) { w->tab = 0; return; }
        if (c == '\n') { editor_find(w); return; }
        if (c == '\b') {
            if (w->input_len > 0) { w->input_len--; w->input_buf[w->input_len] = '\0'; }
            return;
        }
        if (c >= 32 && c < 127 && w->input_len < 126) {
            w->input_buf[w->input_len++] = c;
            w->input_buf[w->input_len] = '\0';
        }
        return;
    }

    /* Replace mode */
    if (w->tab == 2) {
        if (c == 0x1B) { w->tab = 0; return; }
        if (c == '\t') { w->cursor_pos ^= 1; return; }
        if (c == '\n') { editor_replace_first(w); return; }
        if (c == '\b') {
            if (w->cursor_pos == 0) {
                if (w->input_len > 0) { w->input_len--; w->input_buf[w->input_len] = '\0'; }
            } else {
                u32 rlen = (u32)kstrlen(w->input_buf + 128);
                if (rlen > 0) w->input_buf[128 + rlen - 1] = '\0';
            }
            return;
        }
        if (c >= 32 && c < 127) {
            if (w->cursor_pos == 0 && w->input_len < 126) {
                w->input_buf[w->input_len++] = c;
                w->input_buf[w->input_len] = '\0';
            } else if (w->cursor_pos == 1) {
                u32 rlen = (u32)kstrlen(w->input_buf + 128);
                if (rlen < 126) {
                    w->input_buf[128 + rlen] = c;
                    w->input_buf[128 + rlen + 1] = '\0';
                }
            }
        }
        return;
    }

    /* Open / Save dialog */
    if (w->tab == 3) {
        if (c == 0x1B) { w->tab = 0; w->cursor_pos = 0; return; }
        if (c == '\n') {
            w->input_buf[w->input_len] = '\0';
            if (w->cursor_pos == 1) {
                /* Save mode */
                editor_save_to_path(w, w->input_buf);
            } else {
                /* Open mode */
                fs_node_t *f = vfs_resolve_path(w->input_buf);
                if (f && f->type == FS_FILE) {
                    kstrncpy(w->editor_path, w->input_buf, FS_PATH_MAX - 1);
                    win_clear(w);
                    if (f->size > 0) {
                        u32 load_len = f->size < WIN_TEXT_BUF - 1 ? f->size : WIN_TEXT_BUF - 1;
                        kmemcpy(w->text_buf, f->data, load_len);
                        w->text_len = load_len;
                        w->text_buf[load_len] = '\0';
                    }
                    w->editor_modified = false;
                    w->tab = 0;
                    notify_push("Editor", "File opened", g_theme->success);
                } else {
                    notify_push("Editor", "File not found", g_theme->error);
                }
            }
            return;
        }
        if (c == '\b') {
            if (w->input_len > 0) { w->input_len--; w->input_buf[w->input_len] = '\0'; }
            return;
        }
        if (c >= 32 && c < 127 && w->input_len < 127) {
            w->input_buf[w->input_len++] = c;
            w->input_buf[w->input_len] = '\0';
        }
        return;
    }

    /* Normal editing mode (w->tab == 0) */
    if (c == '\b') {
        if (w->text_len > 0) {
            w->text_len--;
            w->text_buf[w->text_len] = '\0';
            w->editor_modified = true;
        }
    } else if (c == 0x13) { /* Ctrl+S: Save */
        if (w->editor_path[0]) {
            editor_save_to_path(w, w->editor_path);
        } else {
            w->tab = 3;
            w->cursor_pos = 1; /* save mode */
            w->input_buf[0] = '\0';
            w->input_len = 0;
            notify_push("Editor", "Enter path to save", g_theme->warning);
        }
    } else if (c == 0x03) { /* Ctrl+C: Copy all */
        kstrncpy(g_clipboard, w->text_buf, CLIPBOARD_SIZE - 1);
        g_clipboard_len = w->text_len;
        g_clipboard_is_cut = false;
        notify_push("Clipboard", "Copied editor text", COL_ACCENT);
    } else if (c == 0x16) { /* Ctrl+V: Paste */
        u32 l = g_clipboard_len;
        if (w->text_len + l < WIN_TEXT_BUF - 1) {
            kstrcpy(w->text_buf + w->text_len, g_clipboard);
            w->text_len += l;
            w->editor_modified = true;
        }
    } else if (c >= 32 || c == '\n' || c == '\t') {
        char ins[128]; 
        ins[0] = '\0';
        if (c == '\n') {
            ins[0] = '\n'; ins[1] = '\0';
            if (w->text_len > 0) {
                i32 line_start = w->text_len;
                while (line_start > 0 && w->text_buf[line_start - 1] != '\n') line_start--;
                
                u32 indent = 0;
                while (line_start + indent < w->text_len && w->text_buf[line_start + indent] == ' ') indent++;
                
                bool ends_brace = false;
                i32 scan = w->text_len - 1;
                while (scan >= line_start && w->text_buf[scan] == ' ') scan--;
                if (scan >= line_start && w->text_buf[scan] == '{') ends_brace = true;
                
                if (ends_brace) indent += 4;
                
                u32 len = 1;
                for (u32 i = 0; i < indent && len < 127; i++) ins[len++] = ' ';
                ins[len] = '\0';
            }
        } else if (c == '\t') { 
            kstrcpy(ins, "    "); 
        } else if (c == '}') {
            i32 line_start = w->text_len;
            while (line_start > 0 && w->text_buf[line_start - 1] != '\n') line_start--;
            bool only_spaces = true;
            u32 spaces = 0;
            for (i32 i = line_start; i < (i32)w->text_len; i++) {
                if (w->text_buf[i] != ' ') { only_spaces = false; break; }
                spaces++;
            }
            if (only_spaces && spaces >= 4) {
                w->text_len -= 4;
                w->text_buf[w->text_len] = '\0';
            }
            ins[0] = '}'; ins[1] = '\0';
        } else {
            ins[0] = c; ins[1] = '\0';
        }
        
        u32 l = (u32)kstrlen(ins);
        if (w->text_len + l < WIN_TEXT_BUF - 1) {
            kstrcpy(w->text_buf + w->text_len, ins);
            w->text_len += l;
            w->editor_modified = true;
        }
    }
}

void app_editor_click(window_t *w, i32 x, i32 y, mouse_t *m) {
    (void)m;
    rect_t cr = wm_client_rect(w);
    i32 sc = (i32)GFX_FONT_SCALE;
    i32 sidebar_w = 200 * sc;
    
    if (w->editor_show_sidebar && x >= cr.x && x < cr.x + sidebar_w) {
        if (y < cr.y + 24 * sc) {
            /* Clicked tabs */
            if (x < cr.x + 80 * sc) w->editor_sidebar_tab = 0;
            else w->editor_sidebar_tab = 1;
        } else if (w->editor_sidebar_tab == 0 && w->fm_dir) {
            /* Clicked Explorer item */
            i32 sy = cr.y + 24 * sc + 4;
            i32 idx_off = 0;
            
            if (w->fm_dir->parent) {
                if (y >= sy && y < sy + 16 * sc) {
                    w->fm_dir = w->fm_dir->parent;
                    w->fm_sel = 0;
                    return;
                }
                sy += 16 * sc;
            }
            
            i32 idx = (y - sy) / (16 * sc);
            if (idx >= 0 && idx < (i32)w->fm_dir->child_count) {
                if (w->fm_sel == (u32)idx) {
                    /* Double click approx */
                    fs_node_t *ch = w->fm_dir->children[idx];
                    if (ch->type == FS_DIR) {
                        w->fm_dir = ch;
                        w->fm_sel = 0;
                    } else if (ch->type == FS_FILE) {
                        /* Open file */
                        kstrncpy(w->editor_path, ch->name, FS_PATH_MAX - 1);
                        u32 load_len = ch->size < WIN_TEXT_BUF - 1 ? ch->size : WIN_TEXT_BUF - 1;
                        kmemcpy(w->text_buf, ch->data, load_len);
                        w->text_len = load_len;
                        w->text_buf[load_len] = '\0';
                        w->editor_modified = false;
                        notify_push("Editor", "Opened file", g_theme->success);
                    }
                } else {
                    w->fm_sel = (u32)idx;
                }
            }
        }
        return;
    }
}
