/* CareOS v9 -- apps/app_notes.c -- Notes / text editor */
#include "apps_common.h"

#define NOTES_HEADER "New note\n=========\n\n"

void app_notes_init(window_t *w){
    win_clear(w);
    win_append(w,NOTES_HEADER);
    w->cursor_pos=w->text_len;
}

/* -- Toolbar geometry, shared by draw and click so they cannot drift ------ */
enum { NOTES_SAVE = 0, NOTES_CLEAR = 1, NOTES_NEW = 2, NOTES_ITEMS = 3 };
static const char *const notes_items[NOTES_ITEMS] = { "Save", "Clear", "New" };

static i32 notes_toolbar_h(void){ return 16 + 10 * (i32)GFX_FONT_SCALE; }

static rect_t notes_item_rect(rect_t cr, int idx){
    i32 sc = (i32)GFX_FONT_SCALE;
    i32 t_h = notes_toolbar_h();
    i32 pad = (i32)(FONT_W * sc);          /* horizontal padding inside a pill */
    i32 ix = cr.x + 8;
    for (int i = 0; i < idx; i++) ix += gfx_str_width(notes_items[i]) + pad * 2 + 6;
    return rect_make(ix, cr.y + 4, gfx_str_width(notes_items[idx]) + pad * 2, t_h - 8);
}

static void notes_save(window_t *w){
    char dir_path[64], file_path[80];
    app_home_path(dir_path);
    kstrcpy(file_path, dir_path); kstrcat(file_path, "/notes.txt");

    struct fs_node *f = vfs_resolve_path(file_path);
    if (!f) {
        struct fs_node *dir = vfs_resolve_path(dir_path);
        /* Accounts without a provisioned home (e.g. guest) get one on first
         * save, the same /home/<name> that users.c creates for real accounts. */
        if (!dir && user_current_uid() != 0) {
            struct fs_node *home_root = vfs_resolve_path("/home");
            if (home_root) dir = vfs_mkdir(home_root, user_current_name());
        }
        if (dir) f = vfs_mkfile(dir, "notes.txt");
    }
    if (f && vfs_write(f, w->text_buf, w->text_len) >= 0) {
        char msg[96]; kstrcpy(msg, "Saved to "); kstrcat(msg, file_path);
        notify_push("Notes", msg, g_theme->success);
    } else {
        notify_push("Notes", "Save failed: home directory unavailable", COL_RED);
    }
}

void app_notes_draw(window_t *w){
    rect_t cr=wm_client_rect(w);
    i32 sc = (i32)GFX_FONT_SCALE;

    /* Background */
    gfx_rect(cr.x, cr.y, cr.w, cr.h, COL_SURFACE);

    /* Toolbar */
    i32 t_h = notes_toolbar_h();
    gfx_rect(cr.x, cr.y, cr.w, t_h, COL_SURFACE2);
    gfx_hline(cr.x, cr.y + t_h - 1, cr.w, COL_BORDER);
    i32 ty = cr.y + (t_h - (i32)(FONT_H * sc)) / 2;

    /* Toolbar items: pill buttons, Save as the accented primary action */
    for (int i = 0; i < NOTES_ITEMS; i++) {
        rect_t r = notes_item_rect(cr, i);
        bool primary = (i == NOTES_SAVE);
        gfx_rect_rounded(r.x, r.y, r.w, r.h, 6, primary ? COL_PRIMARY : g_theme->surface3);
        gfx_str_centered(r.x, ty, r.w, notes_items[i],
                         primary ? COL_WHITE : COL_TEXT, COL_TRANSPARENT);
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
    /* Vertical scroll: clamp to the content, then skip the scrolled-past lines
     * so the wheel can page through a long note (typing snaps back to bottom). */
    {
        i32 vis = (cr.y + cr.h - 10 - (cr.y + t_h + 6)) / lh; if (vis < 1) vis = 1;
        u32 maxs = ((lc + 1) > (u32)vis) ? (lc + 1) - (u32)vis : 0;
        if (w->scroll > maxs) w->scroll = maxs;
    }
    u32 linenum = w->scroll;
    const char *p = w->text_buf;
    { u32 skip = w->scroll; while (skip > 0 && *p) { if (*p == '\n') skip--; p++; } }
    i32 y = cr.y + t_h + 6;
    gfx_set_clip(cr.x, cr.y + t_h, cr.w, cr.h - t_h);
    char last[128] = "";
    while (*p && y < cr.y + cr.h - 4) {
        char n[6]; kutoa(linenum + 1, n, 10);
        gfx_str_right(cr.x + 2, y, gutter_w - 4, n, COL_MUTED, COL_TRANSPARENT);
        const char *end = p;
        while (*end && *end != '\n') end++;
        u32 len = (u32)(end - p);
        if (len > 127) len = 127;
        kmemcpy(last, p, len); last[len] = '\0';
        gfx_str_clipped(cr.x + gutter_w + 8, y, cr.w - gutter_w - 16, last, COL_TEXT, COL_TRANSPARENT);
        y += lh; linenum++;
        if (*end == '\n') { p = end + 1; last[0] = '\0'; } else break;
    }
    /* Blinking caret after the last character typed (text is append-only).
     * A trailing newline leaves the loop on the fresh, still-empty line. */
    if (w->text_len == 0 || w->text_buf[w->text_len - 1] == '\n') y += lh;
    if ((timer_ms()/400)%2==0) {
        gfx_vline(cr.x + gutter_w + 8 + gfx_str_width(last), y - lh, lh - 2, COL_PRIMARY);
    }
    gfx_clear_clip();
}

void app_notes_click(window_t *w, i32 x, i32 y){
    rect_t cr = wm_client_rect(w);
    if (y >= cr.y + notes_toolbar_h()) return;
    for (int i = 0; i < NOTES_ITEMS; i++) {
        if (!rect_contains(notes_item_rect(cr, i), x, y)) continue;
        switch (i) {
        case NOTES_SAVE:  notes_save(w); break;
        case NOTES_CLEAR: win_clear(w); w->cursor_pos = 0; break;
        case NOTES_NEW:   app_notes_init(w); break;
        }
        return;
    }
}

void app_notes_key(window_t *w,char c){
    /* Any keystroke follows the cursor: snap the view to the bottom (draw
     * clamps this to the real last page). The wheel can scroll up until the
     * next keypress. */
    w->scroll = 0xFFFFu;
    if(c=='\b'){
        if(w->text_len>0){
            w->text_len--;
            w->text_buf[w->text_len]='\0';
        }
    } else if(c==0x13){ /* Ctrl+S: Save */
        notes_save(w);
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
    } else if((c>=32&&c<127)||c=='\n'){
        if(w->text_len<WIN_TEXT_BUF-1){ w->text_buf[w->text_len++]=c; w->text_buf[w->text_len]='\0'; }
    }
    w->cursor_pos = w->text_len;
}
