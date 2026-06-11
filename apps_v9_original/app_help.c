/* CareOS v9 -- apps/app_help.c -- Help viewer */
#include "apps_common.h"

void app_help_init(window_t *w){
    win_clear(w);
    win_append(w,"CareOS v6 Help\n==============\n\n");
    win_append(w,"KEYBOARD SHORTCUTS\n");
    win_append(w,"  Terminal: Enter=run, Backspace=delete\n");
    win_append(w,"  Notes/Editor: type to edit\n");
    win_append(w,"  Files: click to select, double-click to open\n\n");
    win_append(w,"SHELL COMMANDS\n");
    win_append(w,"  ls [-l]       List files\n");
    win_append(w,"  cd <dir>      Change directory\n");
    win_append(w,"  cat <file>    Print file\n");
    win_append(w,"  mkdir <dir>   Make directory\n");
    win_append(w,"  rm <file>     Remove file\n");
    win_append(w,"  ps            List processes\n");
    win_append(w,"  whoami        Current user\n");
    win_append(w,"  carepkg       Package manager\n");
    win_append(w,"  ping <host>   Ping host\n");
    win_append(w,"  curl <url>    HTTP request\n");
    win_append(w,"  free          Memory usage\n");
    win_append(w,"  date          Current date\n");
    win_append(w,"  uname         System info\n\n");
    win_append(w,"PACKAGE MANAGER\n");
    win_append(w,"  carepkg list          List all packages\n");
    win_append(w,"  carepkg install <pkg> Install package\n");
    win_append(w,"  carepkg remove <pkg>  Remove package\n");
    win_append(w,"  carepkg search <q>    Search packages\n");
    win_append(w,"  carepkg update        Update all\n\n");
    win_append(w,"APPS\n");
    win_append(w,"  Terminal - Shell with full command set\n");
    win_append(w,"  Browser  - HTTP web browser\n");
    win_append(w,"  Files    - File manager\n");
    win_append(w,"  Editor   - Code editor with syntax hl\n");
    win_append(w,"  Paint    - Pixel art canvas\n");
    win_append(w,"  Monitor  - CPU/RAM/process graphs\n");
    win_append(w,"  Settings - System configuration\n");
    win_append(w,"  Packages - GUI package manager\n");
    win_append(w,"  Users    - User account manager\n");
    win_append(w,"  NetMon   - Network traffic monitor\n");
    win_append(w,"  Clock    - Clock and calendar\n");
}
void app_help_draw(window_t *w){
    rect_t cr=wm_client_rect(w);
    gfx_rect(cr.x,cr.y,cr.w,cr.h,COL_SURFACE);
    draw_scrollable_text(w,cr);
}
void app_help_key(window_t *w,char c){
    if(c=='\x1B') return;
    rect_t cr=wm_client_rect(w);
    u32 vis=(u32)cr.h/(FONT_H+2);
    if(c=='j'||c==' ') { if(w->scroll+vis<w->text_len) w->scroll+=3; }
    if(c=='k') { if(w->scroll>=3) w->scroll-=3; else w->scroll=0; }
}
