/* CareOS v9 -- apps/app_editor.c -- Code Editor */
#include "apps_common.h"

/* w->tab: 0=normal, 1=find, 2=replace, 3=open-dialog
 * w->input_buf[0..127]: find string (w->input_len = its length)
 * w->input_buf[128..255]: replace string (null-terminated)
 * w->cursor_pos: active field in replace mode (0=find, 1=replace) */

void app_editor_init(window_t *w){
    win_clear(w);
    win_append(w,"/* CareOS v9 Code Editor */\n\n");
    win_append(w,"#include <syscall.h>\n\n");
    win_append(w,"int main(void) {\n");
    win_append(w,"    sys_write(1, \"Hello!\\n\", 7);\n");
    win_append(w,"    sys_exit(0);\n");
    win_append(w,"    return 0;\n}\n");
    w->tab = 0;
    w->input_buf[0] = '\0';
    w->input_len = 0;
    w->cursor_pos = 0;
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
            gfx_str(cr.x + 8, ty, "Open:", g_theme->dim, COL_TRANSPARENT);
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

        u32 fc = COL_TEXT;
        if (kstrncmp(tmp,"#include",8)==0 || kstrncmp(tmp,"#define",7)==0) fc = COL_PURPLE;
        else if (kstrncmp(tmp,"    /",5)==0 || kstrncmp(tmp,"/*",2)==0 || kstrncmp(tmp,"//",2)==0) fc = g_theme->muted;
        else if (kstrncmp(tmp,"int ",4)==0 || kstrncmp(tmp,"void ",5)==0 || kstrncmp(tmp,"char ",5)==0 || kstrncmp(tmp,"    return",10)==0) fc = COL_CYAN;
        else if (kstrncmp(tmp,"    sys_",8)==0) fc = g_theme->success;

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

    /* Open dialog */
    if (w->tab == 3) {
        if (c == 0x1B) { w->tab = 0; return; }
        if (c == '\n') {
            w->input_buf[w->input_len] = '\0';
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
            struct fs_node *f = vfs_resolve_path(w->editor_path);
            if (!f) f = vfs_mkfile(vfs_root(), w->editor_path);
            if (f) {
                vfs_write(f, w->text_buf, w->text_len);
                w->editor_modified = false;
                notify_push("Editor", "File saved", g_theme->success);
            }
        } else {
            /* No path: activate open dialog to set one */
            w->tab = 3;
            w->input_buf[0] = '\0';
            w->input_len = 0;
            notify_push("Editor", "Enter a path to save", g_theme->warning);
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
        char ins[5]; ins[0] = c; ins[1] = '\0';
        if (c == '\t') { kstrcpy(ins, "    "); }
        u32 l = kstrlen(ins);
        if (w->text_len + l < WIN_TEXT_BUF - 1) {
            kstrcpy(w->text_buf + w->text_len, ins);
            w->text_len += l;
            w->editor_modified = true;
        }
    }
}
