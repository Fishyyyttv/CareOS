/* CareOS v9 -- apps/app_editor.c -- Code Editor */
#include "apps_common.h"

void app_editor_init(window_t *w){
    win_clear(w);
    win_append(w,"/* CareOS v6 Code Editor */\n\n");
    win_append(w,"#include <syscall.h>\n\n");
    win_append(w,"int main(void) {\n");
    win_append(w,"    sys_write(1, \"Hello!\\n\", 7);\n");
    win_append(w,"    sys_exit(0);\n");
    win_append(w,"    return 0;\n}\n");
}
void app_editor_draw(window_t *w){
    rect_t cr=wm_client_rect(w);
    gfx_rect(cr.x,cr.y,cr.w,cr.h,rgb(0x1e,0x1e,0x2e));
    /* Status bar */
    gfx_rect(cr.x,cr.y+cr.h-18,cr.w,18,COL_SURFACE3);
    gfx_str(cr.x+4,cr.y+cr.h-15,"main.c  |  C  |  UTF-8",COL_DIM,COL_SURFACE3);
    /* Line numbers + code */
    i32 lnw=32; u32 line=0; const char *p=w->text_buf;
    i32 y=cr.y+2;
    gfx_rect(cr.x,cr.y,lnw,cr.h-18,rgb(0x18,0x18,0x28));
    gfx_vline(cr.x+lnw,cr.y,cr.h-18,COL_BORDER);
    while(*p&&y<cr.y+cr.h-20){
        char n[4]; kutoa(line+1,n,10);
        gfx_str(cr.x+2,y,n,COL_MUTED,rgb(0x18,0x18,0x28));
        const char *end=p; while(*end&&*end!='\n') end++;
        u32 len=(u32)(end-p); char tmp[140]; if(len>139) len=139;
        kmemcpy(tmp,p,len); tmp[len]='\0';
        /* Simple syntax highlighting */
        /* Keywords: int void return if else for while */
        u32 fc=COL_TEXT;
        if(kstrncmp(tmp,"#include",8)==0||kstrncmp(tmp,"#define",7)==0) fc=COL_PURPLE;
        else if(kstrncmp(tmp,"    /",5)==0||kstrncmp(tmp,"/*",2)==0||kstrncmp(tmp,"//",2)==0) fc=COL_DIM;
        else if(kstrncmp(tmp,"int ",4)==0||kstrncmp(tmp,"void ",5)==0||kstrncmp(tmp,"char ",5)==0||kstrncmp(tmp,"    return",10)==0) fc=COL_CYAN;
        else if(kstrncmp(tmp,"    sys_",8)==0) fc=COL_GREEN;
        gfx_str(cr.x+lnw+4,y,tmp,fc,rgb(0x1e,0x1e,0x2e));
        y+=FONT_H+2; line++;
        if(*end=='\n') p=end+1; else break;
    }
}
void app_editor_key(window_t *w,char c){
    if(c=='\b'){ if(w->text_len>0){w->text_len--;w->text_buf[w->text_len]='\0';} }
    else if(c>=32||c=='\n'||c=='\t'){
        char ins[5]; ins[0]=c; ins[1]='\0';
        if(c=='\t'){ kstrcpy(ins,"    "); }
        u32 l=kstrlen(ins);
        if(w->text_len+l<WIN_TEXT_BUF-1){ kstrcpy(w->text_buf+w->text_len,ins); w->text_len+=l; }
    }
}
