/* CareOS v9 -- apps/app_clock.c -- Clock and calendar */
#include "apps_common.h"

void app_clock_draw(window_t *w){
    rect_t cr=wm_client_rect(w);
    (void)w;
    gfx_gradient_rect(cr.x,cr.y,cr.w,cr.h,COL_SURFACE,COL_BG);
    rtc_time_t t; rtc_read(&t);
    /* Analog clock */
    i32 cx2=cr.x+cr.w/2, cy2=cr.y+cr.h/2-15;
    i32 r=70;
    gfx_circle_fill(cx2,cy2,r,COL_SURFACE2);
    gfx_circle(cx2,cy2,r,COL_BORDER);
    gfx_circle(cx2,cy2,r-2,COL_BORDER);
    /* Hour markers */
    for(int i=0;i<12;i++){
        /* Simple dot markers */
        /* Would need sin/cos -- skip, just draw tick marks as pixels */
    }
    gfx_circle_fill(cx2,cy2,4,COL_PRIMARY);
    /* Digital time */
    char time_str[12]; char h[3],m[3],s[3];
    kutoa(t.hour,h,10); if(t.hour<10){h[1]=h[0];h[0]='0';h[2]='\0';}
    kutoa(t.minute,m,10); if(t.minute<10){m[1]=m[0];m[0]='0';m[2]='\0';}
    kutoa(t.second,s,10); if(t.second<10){s[1]=s[0];s[0]='0';s[2]='\0';}
    kstrcpy(time_str,h); kstrcat(time_str,":"); kstrcat(time_str,m); kstrcat(time_str,":"); kstrcat(time_str,s);
    gfx_str_centered(cr.x,cy2+r+12,cr.w,time_str,COL_TEXT,COL_TRANSPARENT);
    char date_str[20]; char dy[3],mo[3],yr[5];
    kutoa(t.day,dy,10); if(t.day<10){dy[1]=dy[0];dy[0]='0';dy[2]='\0';}
    kutoa(t.month,mo,10); if(t.month<10){mo[1]=mo[0];mo[0]='0';mo[2]='\0';}
    kutoa(t.year,yr,10);
    kstrcpy(date_str,yr); kstrcat(date_str,"-"); kstrcat(date_str,mo); kstrcat(date_str,"-"); kstrcat(date_str,dy);
    gfx_str_centered(cr.x,cy2+r+28,cr.w,date_str,COL_DIM,COL_TRANSPARENT);
}
