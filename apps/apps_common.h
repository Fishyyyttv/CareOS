/* CareOS v9 -- apps/apps_common.h -- Shared helpers for all apps */
#ifndef APPS_COMMON_H
#define APPS_COMMON_H

#include "kernel.h"
#include "gui.h"

extern rect_t wm_client_rect(window_t *w);

/* -- Shared helpers ------------------------------------------------------- */
static inline void win_append(window_t *w,const char *s){
    while(*s&&w->text_len<WIN_TEXT_BUF-1) w->text_buf[w->text_len++]=*s++;
    w->text_buf[w->text_len]='\0';
}
static inline void win_clear(window_t *w){w->text_len=0;w->text_buf[0]='\0';w->scroll=0;}

static inline void draw_scrollable_text(window_t *w,rect_t cr){
    u32 line_h=(u32)(FONT_H * GFX_FONT_SCALE)+2;
    u32 lines_visible=(u32)cr.h/line_h;
    /* Count lines */
    u32 total_lines=1;
    for(u32 i=0;i<w->text_len;i++) if(w->text_buf[i]=='\n') total_lines++;
    if(w->scroll+lines_visible>total_lines) w->scroll=(total_lines>lines_visible)?total_lines-lines_visible:0;
    /* Find start line */
    u32 line=0; const char *p=w->text_buf;
    while(line<w->scroll&&*p){ if(*p=='\n') line++; p++; }
    /* Draw lines */
    i32 y=cr.y+2; u32 drawn=0;
    gfx_set_clip(cr.x, cr.y, cr.w, cr.h);
    while(*p&&drawn<lines_visible){
        const char *end=p; while(*end&&*end!='\n') end++;
        u32 len=(u32)(end-p);
        char tmp[160]; if(len>159) len=159;
        kmemcpy(tmp,p,len); tmp[len]='\0';
        gfx_str_clipped(cr.x+4,y,cr.w-8,tmp,COL_TEXT,COL_SURFACE);
        y+=line_h; drawn++;
        if(*end=='\n') p=end+1; else break;
    }
    gfx_clear_clip();
}

#endif /* APPS_COMMON_H */
