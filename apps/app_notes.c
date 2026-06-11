/* CareOS v9 -- apps/app_notes.c -- Notes / text editor */
#include "apps_common.h"

void app_notes_init(window_t *w){
    win_clear(w);
    win_append(w,"New note\n=========\n\n");
    w->cursor_pos=w->text_len;
}

void app_notes_draw(window_t *w){
    rect_t cr=wm_client_rect(w);
    i32 sc = (i32)GFX_FONT_SCALE;

    /* Background */
    gfx_rect(cr.x, cr.y, cr.w, cr.h, COL_SURFACE);

    /* Toolbar */
    i32 t_h = 16 + 10 * sc;
    gfx_rect(cr.x, cr.y, cr.w, t_h, COL_SURFACE2);
    gfx_hline(cr.x, cr.y + t_h - 1, cr.w, COL_BORDER);
    i32 ty = cr.y + (t_h - (i32)(FONT_H * sc)) / 2;

    /* Toolbar items */
    const char *items[] = { "Save", "Clear", "New" };
    u32 item_cols[] = { COL_ACCENT, COL_DIM, COL_DIM };
    i32 ix = cr.x + 12;
    for (int i = 0; i < 3; i++) {
        i32 iw = gfx_str_width(items[i]);
        gfx_str(ix, ty, items[i], item_cols[i], COL_TRANSPARENT);
        ix += iw + (i32)(FONT_W * sc) * 3;
    }

    /* Line count (right-aligned) */
    char lcbuf[16] = "Lines: ";
    char lc_n[6]; u32 lc=0;
    for(u32 i=0;i<w->text_len;i++) if(w->text_buf[i]=='\n') lc++;
    kutoa(lc+1,lc_n,10); kstrcat(lcbuf,lc_n);
    gfx_str_right(cr.x, ty, cr.w - 10, lcbuf, COL_MUTED, COL_TRANSPARENT);

    /* Line number gutter */
    i32 gutter_w = 6 + (FONT_W * sc) * 4;
    gfx_rect(cr.x, cr.y + t_h, gutter_w, cr.h - t_h, COL_SURFACE2);
    gfx_vline(cr.x + gutter_w, cr.y + t_h, cr.h - t_h, COL_BORDER);

    /* Text lines with numbers */
    i32 lh = FONT_H * sc + 5;
    u32 linenum = 0;
    const char *p = w->text_buf;
    i32 y = cr.y + t_h + 6;
    gfx_set_clip(cr.x, cr.y + t_h, cr.w, cr.h - t_h);
    while (*p && y < cr.y + cr.h - 4) {
        char n[6]; kutoa(linenum + 1, n, 10);
        gfx_str_right(cr.x + 2, y, gutter_w - 4, n, COL_MUTED, COL_TRANSPARENT);
        const char *end = p;
        while (*end && *end != '\n') end++;
        u32 len = (u32)(end - p);
        if (len > 127) len = 127;
        char tmp[128]; kmemcpy(tmp, p, len); tmp[len] = '\0';
        gfx_str_clipped(cr.x + gutter_w + 8, y, cr.w - gutter_w - 16, tmp, COL_TEXT, COL_TRANSPARENT);
        y += lh; linenum++;
        if (*end == '\n') p = end + 1; else break;
    }
    /* Blinking cursor on last line */
    if ((timer_get_ticks()/40)%2==0) {
        gfx_vline(cr.x + gutter_w + 8, y - lh, lh - 2, COL_PRIMARY);
    }
    gfx_clear_clip();
}

void app_notes_key(window_t *w,char c){
    if(c=='\b'){ 
        if(w->text_len>0){
            w->text_len--;
            w->text_buf[w->text_len]='\0';
        } 
    } else if(c==0x13){ /* Ctrl+S: Save */
        struct fs_node *f = vfs_resolve_path("/home/user/notes.txt");
        if (!f) {
            struct fs_node *dir = vfs_resolve_path("/home/user");
            if (dir) f = vfs_mkfile(dir, "notes.txt");
        }
        if (f) {
            vfs_write(f, w->text_buf, w->text_len);
            notify_push("Notes", "Saved to /home/user/notes.txt", g_theme->success);
        }
    } else if(c==0x03){ /* Ctrl+C */
        kstrncpy(g_clipboard, w->text_buf, CLIPBOARD_SIZE-1);
        g_clipboard_len = w->text_len;
        notify_push("Clipboard", "Copied notes text", COL_ACCENT);
    } else if(c==0x16){ /* Ctrl+V */
        u32 l = g_clipboard_len;
        if (w->text_len + l < WIN_TEXT_BUF-1) {
            kstrcpy(w->text_buf + w->text_len, g_clipboard);
            w->text_len += l;
        }
    } else if(c>=32||c=='\n'){
        if(w->text_len<WIN_TEXT_BUF-1){ w->text_buf[w->text_len++]=c; w->text_buf[w->text_len]='\0'; }
    }
}
