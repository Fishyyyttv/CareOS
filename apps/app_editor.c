/* CareOS v9 -- apps/app_editor.c -- Code Editor */
#include "apps_common.h"

void app_editor_init(window_t *w){
    win_clear(w);
    win_append(w,"/* CareOS v9 Code Editor */\n\n");
    win_append(w,"#include <syscall.h>\n\n");
    win_append(w,"int main(void) {\n");
    win_append(w,"    sys_write(1, \"Hello!\\n\", 7);\n");
    win_append(w,"    sys_exit(0);\n");
    win_append(w,"    return 0;\n}\n");
}
void app_editor_draw(window_t *w){
    rect_t cr=wm_client_rect(w);
    i32 sc = (i32)GFX_FONT_SCALE;
    u32 editor_bg = rgb(0x10, 0x14, 0x20);
    u32 gutter_bg = rgb(0x0b, 0x0f, 0x1a);
    i32 sb_h = 20 + 4 * sc;

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

    /* Gutter */
    i32 lnw = 32 + 8 * sc;
    gfx_rect(cr.x, cr.y, lnw, cr.h - sb_h, gutter_bg);
    gfx_vline(cr.x + lnw, cr.y, cr.h - sb_h, COL_BORDER);

    /* Lines */
    u32 lh = (u32)(FONT_H * sc) + 3;
    u32 line = 0; const char *p = w->text_buf;
    i32 y = cr.y + 4;
    while (*p && y < cr.y + cr.h - sb_h - 2) {
        char n[5]; kutoa(line + 1, n, 10);
        gfx_str_right(cr.x, y, lnw - 4, n, g_theme->muted, COL_TRANSPARENT);
        const char *end = p;
        while (*end && *end != '\n') end++;
        u32 len = (u32)(end - p);
        if (len > 139) len = 139;
        char tmp[140]; kmemcpy(tmp, p, len); tmp[len] = '\0';
        u32 fc = COL_TEXT;
        if(kstrncmp(tmp,"#include",8)==0||kstrncmp(tmp,"#define",7)==0) fc=COL_PURPLE;
        else if(kstrncmp(tmp,"    /",5)==0||kstrncmp(tmp,"/*",2)==0||kstrncmp(tmp,"//",2)==0) fc=g_theme->muted;
        else if(kstrncmp(tmp,"int ",4)==0||kstrncmp(tmp,"void ",5)==0||kstrncmp(tmp,"char ",5)==0||kstrncmp(tmp,"    return",10)==0) fc=COL_CYAN;
        else if(kstrncmp(tmp,"    sys_",8)==0) fc=g_theme->success;
        gfx_str(cr.x + lnw + 6, y, tmp, fc, COL_TRANSPARENT);
        y += lh; line++;
        if (*end == '\n') p = end + 1; else break;
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
