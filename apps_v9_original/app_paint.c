/* CareOS v9 -- apps/app_paint.c -- Paint application */
#include "apps_common.h"

void app_paint_init(window_t *w){
    kmemset(w->text_buf /* paint_canvas */,0xff,sizeof(w->text_buf /* paint_canvas */)); /* white */
    w->paint_color=0x00; /* black */ w->paint_color /* paint_tool */=0; w->input_len /* paint_size */=4;
}
void app_paint_draw(window_t *w){
    rect_t cr=wm_client_rect(w);
    /* Toolbar */
    gfx_rect(cr.x,cr.y,cr.w,28,COL_SURFACE2);
    gfx_hline(cr.x,cr.y+28,cr.w,COL_BORDER);
    const char *tools[]={"Pen","Fill","Erase","Line"};
    for(int i=0;i<4;i++){
        u32 bg=(u32)i==w->paint_color /* paint_tool */?COL_PRIMARY:COL_SURFACE3;
        gfx_rect_rounded(cr.x+4+i*52,cr.y+4,48,20,4,bg);
        gfx_str_centered(cr.x+4+i*52,cr.y+8,48,tools[i],COL_TEXT,bg);
    }
    /* Color palette */
    u32 palette[]={0x000000,0xffffff,0xff0000,0x00cc44,0x4a6fff,0xfbba00,0x22d3ee,0xa78bfa,0xf87171,0xfb923c};
    for(int i=0;i<10;i++){
        i32 px=cr.x+cr.w-140+i*14;
        gfx_circle_fill(px,cr.y+14,5,palette[i]);
        if((u32)i==w->paint_color) gfx_circle(px,cr.y+14,6,COL_WHITE);
    }
    /* Canvas */
    i32 cw=cr.w-2,ch=cr.h-30;
    i32 cx=cr.x+1,cy=cr.y+30;
    /* 64x64 canvas scaled to fit */
    i32 px_w=cw/64, px_h=ch/64;
    if(px_w<1) px_w=1; if(px_h<1) px_h=1;
    static const u32 pal[]={0x000000,0xffffff,0xff0000,0x00cc44,0x4a6fff,0xfbba00,0x22d3ee,0xa78bfa,0xf87171,0xfb923c};
    for(int y=0;y<64;y++) for(int x=0;x<64;x++){
        u8 ci=w->text_buf /* paint_canvas */[y*64+x];
        u32 col=(ci<10)?pal[ci]:0xffffff;
        gfx_rect(cx+x*px_w,cy+y*px_h,px_w,px_h,col);
    }
}
void app_paint_key(window_t *w,char c){
    if(c=='c'||c=='C') app_paint_init(w);
    else if(c>='1'&&c<='9') w->paint_color /* paint_tool */=(u32)(c-'1')%4;
}
void app_paint_click(window_t *w,i32 x,i32 y,mouse_t *m){
    rect_t cr=wm_client_rect(w);
    (void)m;
    /* Color click */
    if(y<cr.y+28){
        for(int i=0;i<10;i++){
            i32 px=cr.x+cr.w-140+i*14;
            if(x>=px-6&&x<px+6) w->paint_color=(u32)i;
        }
        /* Tool click */
        for(int i=0;i<4;i++) if(x>cr.x+4+i*52&&x<cr.x+52+i*52) w->paint_color /* paint_tool */=(u32)i;
        return;
    }
    /* Canvas draw */
    i32 cw=cr.w-2, ch=cr.h-30;
    i32 cx=cr.x+1, cy=cr.y+30;
    i32 px_w=cw/64, px_h=ch/64;
    if(px_w<1) px_w=1; if(px_h<1) px_h=1;
    i32 gx=(x-cx)/px_w, gy=(y-cy)/px_h;
    if(gx>=0&&gx<64&&gy>=0&&gy<64){
        u8 c2=(w->paint_color /* paint_tool */==2)?0xff:(u8)w->paint_color;
        for(i32 dy=-w->input_len /* paint_size *//2;dy<=w->input_len /* paint_size *//2;dy++)
            for(i32 dx=-w->input_len /* paint_size *//2;dx<=w->input_len /* paint_size *//2;dx++){
                i32 nx=gx+dx, ny=gy+dy;
                if(nx>=0&&nx<64&&ny>=0&&ny<64) w->text_buf /* paint_canvas */[ny*64+nx]=c2;
            }
    }
}
