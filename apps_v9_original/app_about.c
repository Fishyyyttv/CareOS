/* CareOS v9 -- apps/app_about.c -- About screen */
#include "apps_common.h"

void app_about_draw(window_t *w){
    rect_t cr=wm_client_rect(w);
    gfx_gradient_rect(cr.x,cr.y,cr.w,cr.h,COL_SURFACE,COL_BG);
    gfx_circle_fill(cr.x+cr.w/2,cr.y+50,36,COL_PRIMARY);
    gfx_circle(cr.x+cr.w/2,cr.y+50,36,COL_ACCENT);
    gfx_str_centered(cr.x,cr.y+18,cr.w,"C",COL_WHITE,COL_TRANSPARENT);
    gfx_str_centered(cr.x,cr.y+96,cr.w,"CareOS v9.0",COL_TEXT,COL_TRANSPARENT);
    gfx_str_centered(cr.x,cr.y+114,cr.w,"Complete Hobby Operating System",COL_DIM,COL_TRANSPARENT);
    gfx_hline(cr.x+40,cr.y+130,cr.w-80,COL_BORDER);
    const char *info[]={
        "Kernel: CareOS 9.0 x86","Arch: IA-32 protected mode",
        "GUI: Framebuffer 32bpp","Net: TCP/IP stack","FS: In-memory VFS",
        "Built: March 2026",NULL};
    i32 y=cr.y+140;
    for(int i=0;info[i];i++){gfx_str_centered(cr.x,y,cr.w,info[i],COL_DIM,COL_TRANSPARENT);y+=16;}
}
