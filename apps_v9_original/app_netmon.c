/* CareOS v9 -- apps/app_netmon.c -- Network Monitor */
#include "apps_common.h"

void app_netmon_tick(window_t *w){
    u32 pos=w->scroll /* net_hist_pos */%60;
    /* Simulate traffic */
    w->text_buf /* net_rx_hist */[pos]=(u32)(timer_get_ticks()%80);
    w->input_buf /* net_tx_hist */[pos]=(u32)(timer_get_ticks()%40);
    w->scroll /* net_hist_pos */++;
}
void app_netmon_draw(window_t *w){
    rect_t cr=wm_client_rect(w);
    gfx_rect(cr.x,cr.y,cr.w,cr.h,COL_SURFACE2);
    i32 y=cr.y+8;
    gfx_str(cr.x+8,y,"Network Monitor",COL_TEXT,COL_SURFACE2); y+=20;
    gfx_str(cr.x+8,y,"Status: ",COL_DIM,COL_SURFACE2);
    gfx_str(cr.x+72,y,net_is_up()?"Connected (eth0)":"Disconnected",net_is_up()?COL_GREEN:COL_RED,COL_SURFACE2); y+=20;
    if(net_is_up()){
        u32 ip=net_get_ip(); char b[4][5];
        kutoa((ip>>24)&0xff,b[0],10); kutoa((ip>>16)&0xff,b[1],10);
        kutoa((ip>>8)&0xff,b[2],10); kutoa(ip&0xff,b[3],10);
        char ip_s[20]; kstrcpy(ip_s,b[0]);kstrcat(ip_s,".");kstrcat(ip_s,b[1]);
        kstrcat(ip_s,".");kstrcat(ip_s,b[2]);kstrcat(ip_s,".");kstrcat(ip_s,b[3]);
        gfx_str(cr.x+8,y,"IP: ",COL_DIM,COL_SURFACE2); gfx_str(cr.x+40,y,ip_s,COL_ACCENT,COL_SURFACE2); y+=20;
    }
    /* RX graph */
    gfx_str(cr.x+8,y,"RX traffic:",COL_GREEN,COL_SURFACE2); y+=16;
    i32 gh=60,gw=cr.w-20;
    gfx_rect(cr.x+8,y,gw,gh,COL_SURFACE3); gfx_rect_outline(cr.x+8,y,gw,gh,COL_BORDER);
    for(int i=1;i<60;i++){
        u32 v1=w->text_buf /* net_rx_hist */[(w->scroll /* net_hist_pos */+i-1)%60];
        u32 v2=w->text_buf /* net_rx_hist */[(w->scroll /* net_hist_pos */+i)%60];
        gfx_line(cr.x+8+(i-1)*gw/60,y+gh-(i32)(v1*gh/100),cr.x+8+i*gw/60,y+gh-(i32)(v2*gh/100),COL_GREEN);
    }
    y+=gh+8;
    /* TX graph */
    gfx_str(cr.x+8,y,"TX traffic:",COL_CYAN,COL_SURFACE2); y+=16;
    gfx_rect(cr.x+8,y,gw,gh,COL_SURFACE3); gfx_rect_outline(cr.x+8,y,gw,gh,COL_BORDER);
    for(int i=1;i<60;i++){
        u32 v1=w->input_buf /* net_tx_hist */[(w->scroll /* net_hist_pos */+i-1)%60];
        u32 v2=w->input_buf /* net_tx_hist */[(w->scroll /* net_hist_pos */+i)%60];
        gfx_line(cr.x+8+(i-1)*gw/60,y+gh-(i32)(v1*gh/100),cr.x+8+i*gw/60,y+gh-(i32)(v2*gh/100),COL_CYAN);
    }
}
