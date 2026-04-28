/* CareOS v9 -- apps/app_notes.c -- Notes / text editor */
#include "apps_common.h"

void app_notes_init(window_t *w){
    win_clear(w);
    win_append(w,"New note\n=========\n");
    w->cursor_pos=w->text_len;
}
void app_notes_draw(window_t *w){
    rect_t cr=wm_client_rect(w);
    gfx_rect(cr.x,cr.y,cr.w,cr.h,rgb(0xff,0xff,0xee));
    /* Toolbar */
    gfx_rect(cr.x,cr.y,cr.w,20,COL_SURFACE);
    gfx_str(cr.x+4,cr.y+4,"[Save] [Clear] [New]",COL_DIM,COL_SURFACE);
    gfx_hline(cr.x,cr.y+20,cr.w,COL_BORDER);
    /* Text with line numbers */
    u32 lh=FONT_H+2; u32 line=0; const char *p=w->text_buf;
    i32 y=cr.y+24;
    gfx_rect(cr.x,cr.y+21,28,cr.h-21,COL_SURFACE2);
    gfx_vline(cr.x+28,cr.y+21,cr.h-21,COL_BORDER);
    while(*p&&y<cr.y+cr.h-4){
        char n[4]; kutoa(line+1,n,10);
        gfx_str(cr.x+2,y,n,COL_MUTED,COL_SURFACE2);
        const char *end=p; while(*end&&*end!='\n') end++;
        u32 len=(u32)(end-p); char tmp[128]; if(len>127) len=127;
        kmemcpy(tmp,p,len); tmp[len]='\0';
        gfx_str(cr.x+32,y,tmp,COL_BLACK,rgb(0xff,0xff,0xee));
        y+=lh; line++;
        if(*end=='\n') p=end+1; else break;
    }
}
void app_notes_key(window_t *w,char c){
    if(c=='\b'){ if(w->text_len>0){w->text_len--;w->text_buf[w->text_len]='\0';} }
    else if(c>=32||c=='\n'){
        if(w->text_len<WIN_TEXT_BUF-1){ w->text_buf[w->text_len++]=c; w->text_buf[w->text_len]='\0'; }
    }
}
