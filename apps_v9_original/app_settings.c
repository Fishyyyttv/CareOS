/* CareOS v9 -- apps/app_settings.c -- Settings panel */
#include "apps_common.h"

/* term_ip_to_str is defined in app_terminal.c */
extern void term_ip_to_str(u32 ip, char *out);

void app_settings_init(window_t *w){ w->settings_tab=0; }
void app_settings_draw(window_t *w){
    rect_t cr=wm_client_rect(w);
    const careos_settings_t *cfg=settings_get();
    gfx_rect(cr.x,cr.y,cr.w,cr.h,COL_SURFACE);

    i32 sb=130;
    gfx_rect(cr.x,cr.y,sb,cr.h,COL_SURFACE2);
    gfx_vline(cr.x+sb,cr.y,cr.h,COL_BORDER);

    const char *tabs[]={"Display","Network","Users","Updates","Theme","Security",NULL};
    for(int i=0;tabs[i];i++){
        i32 ty=cr.y+6+i*28;
        u32 bg=(u32)i==w->settings_tab?COL_SELECTION:COL_SURFACE2;
        gfx_rect(cr.x+2,ty,sb-4,24,bg);
        if((u32)i==w->settings_tab) gfx_vline(cr.x+2,ty,24,COL_PRIMARY);
        gfx_str(cr.x+10,ty+6,tabs[i],COL_TEXT,bg);
    }

    i32 cx=cr.x+sb+12, cy=cr.y+12, cw=cr.w-sb-24;
    char b[16];

    switch(w->settings_tab){
    case 0:
        gfx_str(cx,cy,"Display Settings",COL_TEXT,COL_SURFACE); cy+=24;
        gfx_hline(cx,cy,cw,COL_BORDER); cy+=12;
        gfx_str(cx,cy,"Resolution:",COL_DIM,COL_SURFACE);
        char res[32];
        kutoa(SCREEN_W,res,10); kstrcat(res,"x");
        char hh[8]; kutoa(SCREEN_H,hh,10); kstrcat(res,hh); kstrcat(res," 32bpp");
        gfx_str(cx+100,cy,res,COL_ACCENT,COL_SURFACE); cy+=20;

        gfx_str(cx,cy,"Mouse sensitivity:",COL_DIM,COL_SURFACE);
        gfx_rect_rounded(cx+130,cy-3,24,18,3,COL_SURFACE3);
        gfx_str_centered(cx+130,cy+1,24,"-",COL_TEXT,COL_TRANSPARENT);
        gfx_rect_rounded(cx+158,cy-3,24,18,3,COL_SURFACE3);
        gfx_str_centered(cx+158,cy+1,24,"+",COL_TEXT,COL_TRANSPARENT);
        kutoa(cfg->mouse_sensitivity,b,10); gfx_str(cx+186,cy,b,COL_ACCENT,COL_SURFACE); gfx_str(cx+208,cy,"%",COL_ACCENT,COL_SURFACE); cy+=24;

        gfx_str(cx,cy,"Boot fast:",COL_DIM,COL_SURFACE);
        gfx_rect_rounded(cx+130,cy-3,78,18,3,cfg->boot_fast?COL_GREEN:COL_SURFACE3);
        gfx_str_centered(cx+130,cy+1,78,cfg->boot_fast?"Enabled":"Disabled",COL_WHITE,COL_TRANSPARENT); cy+=22;

        gfx_str(cx,cy,"Use shell: settings set mouse <40-200>",COL_MUTED,COL_SURFACE);
        break;

    case 1:
        gfx_str(cx,cy,"Network Settings",COL_TEXT,COL_SURFACE); cy+=24;
        gfx_hline(cx,cy,cw,COL_BORDER); cy+=12;
        gfx_str(cx,cy,"Status:",COL_DIM,COL_SURFACE);
        gfx_str(cx+90,cy,net_is_up()?"Connected":"Disconnected",net_is_up()?COL_GREEN:COL_RED,COL_SURFACE); cy+=20;

        {
            char ip[24]; term_ip_to_str(net_get_ip(),ip);
            gfx_str(cx,cy,"IP:",COL_DIM,COL_SURFACE); gfx_str(cx+90,cy,ip,COL_ACCENT,COL_SURFACE); cy+=20;
        }

        {
            u32 dns=net_get_dns_server();
            char dns_s[24];
            if(dns) term_ip_to_str(dns,dns_s); else kstrcpy(dns_s,"not set");
            gfx_str(cx,cy,"DNS:",COL_DIM,COL_SURFACE); gfx_str(cx+90,cy,dns_s,COL_ACCENT,COL_SURFACE); cy+=20;
        }

        gfx_str(cx,cy,"Wi-Fi:",COL_DIM,COL_SURFACE);
        gfx_str(cx+90,cy,cfg->wifi_connected?(cfg->wifi_ssid[0]?cfg->wifi_ssid:"connected"):"disconnected",COL_TEXT,COL_SURFACE); cy+=24;

        gfx_rect_rounded(cx,cy,140,22,4,cfg->wifi_connected?COL_RED:COL_PRIMARY);
        gfx_str_centered(cx,cy+6,140,cfg->wifi_connected?"Disconnect Wi-Fi":"Connect Wi-Fi",COL_WHITE,COL_TRANSPARENT);
        gfx_rect_rounded(cx+150,cy,120,22,4,COL_SURFACE3);
        gfx_str_centered(cx+150,cy+6,120,"Renew DHCP",COL_TEXT,COL_TRANSPARENT);
        break;

    case 2:
        gfx_str(cx,cy,"User Accounts",COL_TEXT,COL_SURFACE); cy+=24;
        gfx_hline(cx,cy,cw,COL_BORDER); cy+=12;
        gfx_str(cx,cy,"Current user:",COL_DIM,COL_SURFACE);
        gfx_str(cx+120,cy,user_current_name(),COL_ACCENT,COL_SURFACE); cy+=18;
        gfx_str(cx,cy,"Create users in Users app or via shell useradd.",COL_MUTED,COL_SURFACE);
        break;

    case 3:
        gfx_str(cx,cy,"System Updates",COL_TEXT,COL_SURFACE); cy+=24;
        gfx_hline(cx,cy,cw,COL_BORDER); cy+=12;
        gfx_str(cx,cy,"CareOS status: stable",COL_GREEN,COL_SURFACE); cy+=18;
        gfx_str(cx,cy,"Use carepkg update for package refresh.",COL_MUTED,COL_SURFACE);
        break;

    case 4:
        gfx_str(cx,cy,"Theme",COL_TEXT,COL_SURFACE); cy+=24;
        gfx_hline(cx,cy,cw,COL_BORDER); cy+=12;
        gfx_str(cx,cy,"Theme preset:",COL_DIM,COL_SURFACE);
        gfx_rect_rounded(cx+110,cy-3,92,18,3,COL_SURFACE3);
        {
            char tbuf[16]; kutoa(cfg->theme,tbuf,10);
            gfx_str_centered(cx+110,cy+1,92,tbuf,COL_TEXT,COL_TRANSPARENT); cy+=24;
        }

        gfx_str(cx,cy,"Wallpaper id:",COL_DIM,COL_SURFACE);
        gfx_rect_rounded(cx+110,cy-3,92,18,3,COL_SURFACE3);
        {
            char wbuf[16]; kutoa(cfg->wallpaper,wbuf,10);
            gfx_str_centered(cx+110,cy+1,92,wbuf,COL_TEXT,COL_TRANSPARENT); cy+=24;
        }

        gfx_rect_rounded(cx,cy,110,22,4,COL_SURFACE3);
        gfx_str_centered(cx,cy+6,110,"Cycle Theme",COL_TEXT,COL_TRANSPARENT);
        gfx_rect_rounded(cx+120,cy,130,22,4,COL_SURFACE3);
        gfx_str_centered(cx+120,cy+6,130,"Cycle Wallpaper",COL_TEXT,COL_TRANSPARENT);
        break;

    case 5:
        gfx_str(cx,cy,"Security",COL_TEXT,COL_SURFACE); cy+=24;
        gfx_hline(cx,cy,cw,COL_BORDER); cy+=12;
        gfx_str(cx,cy,"Password policy: min 8 + upper/lower/digit",COL_GREEN,COL_SURFACE); cy+=18;
        gfx_str(cx,cy,"Login lockout: enabled",COL_GREEN,COL_SURFACE); cy+=18;
        gfx_str(cx,cy,"Kernel panic + serial log tools active",COL_DIM,COL_SURFACE);
        break;
    }
}
void app_settings_key(window_t *w,char c){ (void)w;(void)c; }
void app_settings_click(window_t *w,i32 x,i32 y,mouse_t *m){
    (void)m;
    rect_t cr=wm_client_rect(w);
    i32 sb=130;
    if(x<cr.x+sb){
        int idx=(y-cr.y-6)/28;
        if(idx>=0&&idx<6) w->settings_tab=(u32)idx;
        return;
    }

    const careos_settings_t *cfg=settings_get();
    i32 cx=cr.x+sb+12, cy=cr.y+12;

    if(w->settings_tab==0){
        i32 y_mouse=cy+24+12+20;
        if(x>=cx+130 && x<=cx+154 && y>=y_mouse-3 && y<=y_mouse+15){
            u32 v=cfg->mouse_sensitivity; settings_set_mouse_sensitivity(v>40?v-10:40); return;
        }
        if(x>=cx+158 && x<=cx+182 && y>=y_mouse-3 && y<=y_mouse+15){
            u32 v=cfg->mouse_sensitivity; settings_set_mouse_sensitivity(v<200?v+10:200); return;
        }
        i32 y_boot=y_mouse+24;
        if(x>=cx+130 && x<=cx+208 && y>=y_boot-3 && y<=y_boot+15){
            settings_set_boot_fast(!cfg->boot_fast); return;
        }
    }

    if(w->settings_tab==1){
        i32 y_btn=cy+24+12+20+20+20+24;
        if(x>=cx && x<=cx+140 && y>=y_btn && y<=y_btn+22){
            if(cfg->wifi_connected){
                settings_set_wifi_profile("","",false);
                net_set_ip((127u<<24)|(0u<<16)|(0u<<8)|1u,(255u<<24)|(0u<<16)|(0u<<8)|0u,0);
                net_set_dns_server(0);
                notify_push("Wi-Fi","Disconnected",COL_YELLOW);
            } else {
                const char *ssid = cfg->wifi_ssid[0] ? cfg->wifi_ssid : "CareHome-5G";
                settings_set_wifi_profile(ssid,cfg->wifi_pass,true);
                if(net_dhcp_renew()==0) notify_push("Wi-Fi","Connected",COL_GREEN);
                else notify_push("Wi-Fi","Saved profile, no link",COL_YELLOW);
            }
            return;
        }
        if(x>=cx+150 && x<=cx+270 && y>=y_btn && y<=y_btn+22){
            if(net_dhcp_renew()==0) notify_push("Network","DHCP renewed",COL_GREEN);
            else notify_push("Network","DHCP failed",COL_RED);
            return;
        }
    }

    if(w->settings_tab==4){
        i32 y_btn=cy+24+12+24+24;
        if(x>=cx && x<=cx+110 && y>=y_btn && y<=y_btn+22){
            settings_set_theme((cfg->theme+1u)%4u);
            return;
        }
        if(x>=cx+120 && x<=cx+250 && y>=y_btn && y<=y_btn+22){
            settings_set_wallpaper((cfg->wallpaper+1u)%10u);
            return;
        }
    }
}
