/* CareOS v9 -- apps/app_notes.c -- Notes / text editor */
#include "apps_common.h"

void app_notes_init(window_t *w){
    win_clear(w);
    win_append(w,"New note\n=========\n");
    w->cursor_pos=w->text_len;
}
void app_notes_draw(window_t *w){
    rect_t cr=wm_client_rect(w);
    i32 sc = (i32)GFX_FONT_SCALE;
    gfx_rect(cr.x, cr.y, cr.w, cr.h, COL_SURFACE);

    /* Toolbar */
    i32 t_h = 20 + 8 * sc;
    gfx_rect(cr.x, cr.y, cr.w, t_h, COL_SURFACE2);
    gfx_hline(cr.x, cr.y + t_h, cr.w, COL_BORDER);
    i32 ty = cr.y + (t_h - (i32)(FONT_H * sc)) / 2;
    gfx_str(cr.x + 10, ty, "Save", COL_ACCENT, COL_TRANSPARENT);
    gfx_str(cr.x + 10 + 6 * (FONT_W * sc), ty, "  Clear", COL_DIM, COL_TRANSPARENT);
    gfx_str(cr.x + 10 + 14 * (FONT_W * sc), ty, "  New", COL_DIM, COL_TRANSPARENT);
    /* Line count */
    char lcbuf[8]; u32 lc=0;
    for(u32 i=0;i<w->text_len;i++) if(w->text_buf[i]=='\n') lc++;
    kutoa(lc+1,lcbuf,10);
    gfx_str_right(cr.x, ty, cr.w - 8, lcbuf, COL_MUTED, COL_TRANSPARENT);

    /* Line number gutter */
    i32 sb_w = 18 + 12 * sc;
    gfx_rect(cr.x, cr.y + t_h + 1, sb_w, cr.h - t_h - 1, COL_SURFACE2);
    gfx_vline(cr.x + sb_w, cr.y + t_h + 1, cr.h - t_h - 1, COL_BORDER);

    /* Text lines */
    u32 lh = (FONT_H * sc) + 5;
    u32 line = 0;
    const char *p = w->text_buf;
    i32 y = cr.y + t_h + 6;
    while (*p && y < cr.y + cr.h - 4) {
        char n[6]; kutoa(line + 1, n, 10);
        gfx_str(cr.x + 4, y, n, COL_MUTED, COL_TRANSPARENT);
        const char *end = p;
        while (*end && *end != '\n') end++;
        u32 len = (u32)(end - p);
        if (len > 127) len = 127;
        char tmp[128]; kmemcpy(tmp, p, len); tmp[len] = '\0';
        gfx_str(cr.x + sb_w + 6, y, tmp, COL_TEXT, COL_TRANSPARENT);
        y += lh; line++;
        if (*end == '\n') p = end + 1; else break;
    }
}
void app_notes_key(window_t *w,char c){
    if(c=='\b'){ if(w->text_len>0){w->text_len--;w->text_buf[w->text_len]='\0';} }
    else if(c>=32||c=='\n'){
        if(w->text_len<WIN_TEXT_BUF-1){ w->text_buf[w->text_len++]=c; w->text_buf[w->text_len]='\0'; }
    }
}
