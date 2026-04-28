/* CareOS v9 -- apps/app_sysmon.c -- System Monitor */
#include "apps_common.h"

void app_sysmon_tick(window_t *w){
    u32 pos=w->sysmon_hist_pos%60;
    /* CPU: fake load based on tick count */
    w->sysmon_cpu_hist[pos]=(timer_get_ticks()%70)+5;
    /* Mem: real */
    w->sysmon_mem_hist[pos]=kmem_used()*100/KERNEL_HEAP_SIZE;
    w->sysmon_hist_pos++;
}
void app_sysmon_draw(window_t *w){
    rect_t cr=wm_client_rect(w);
    gfx_rect(cr.x,cr.y,cr.w,cr.h,COL_SURFACE2);

    i32 sc = (i32)GFX_FONT_SCALE;
    /* Tabs — dynamic width filling the window */
    const char *tabs[]={"Overview","Processes","Memory","Network",NULL};
    i32 tab_count=4;
    i32 tab_h = 20 + 6 * sc;
    i32 tab_w = cr.w / tab_count;
    for(int i=0;i<tab_count;i++){
        i32 tx=cr.x + i * tab_w;
        bool active = (u32)i == w->tab;
        u32 bg = active ? COL_SURFACE3 : COL_SURFACE2;
        gfx_rect(tx, cr.y, tab_w, tab_h, bg);
        if (active) gfx_rect(tx + 4, cr.y + tab_h - 3, tab_w - 8, 3, COL_PRIMARY);
        gfx_set_clip(tx + 2, cr.y, tab_w - 4, tab_h);
        gfx_str_centered(tx, cr.y + (tab_h - (i32)(FONT_H * sc)) / 2, tab_w,
            tabs[i], active ? COL_TEXT : COL_DIM, COL_TRANSPARENT);
        gfx_clear_clip();
    }
    gfx_hline(cr.x, cr.y + tab_h, cr.w, COL_BORDER);
    i32 cy = cr.y + tab_h + 8;

    if(w->tab==0){
        /* Overview */
        char buf[32];
        i32 line_h = 12 + 6 * sc;
        gfx_str(cr.x+8,cy,   "CPU:",   COL_TEXT,COL_SURFACE2); cy+=line_h;
        u32 cpu=w->sysmon_cpu_hist[(w->sysmon_hist_pos+59)%60];
        gfx_bar(cr.x+8,cy,cr.w-16,14,COL_SURFACE3,COL_GREEN,cpu);
        kutoa(cpu,buf,10); kstrcat(buf,"%"); gfx_str(cr.x+cr.w-40,cy,buf,COL_TEXT,COL_SURFACE2);
        cy+=line_h + 2;
        gfx_str(cr.x+8,cy,"Memory:",COL_TEXT,COL_SURFACE2); cy+=line_h;
        u32 mem_pct=kmem_used()*100/KERNEL_HEAP_SIZE;
        gfx_bar(cr.x+8,cy,cr.w-16,14,COL_SURFACE3,COL_PRIMARY,mem_pct);
        kutoa(mem_pct,buf,10); kstrcat(buf,"%"); gfx_str(cr.x+cr.w-40,cy,buf,COL_TEXT,COL_SURFACE2);
        cy+=line_h + 2;
        gfx_str(cr.x+8,cy,"Tasks: ",COL_TEXT,COL_SURFACE2);
        kutoa(task_count_active(),buf,10); gfx_str(cr.x+80,cy,buf,COL_ACCENT,COL_SURFACE2); cy+=line_h;
        gfx_str(cr.x+8,cy,"Uptime:",COL_TEXT,COL_SURFACE2);
        kutoa(timer_get_ticks()/1000,buf,10); kstrcat(buf,"s");
        gfx_str(cr.x+80,cy,buf,COL_ACCENT,COL_SURFACE2); cy+=line_h;
        gfx_str(cr.x+8,cy,"Network:",COL_TEXT,COL_SURFACE2);
        gfx_str(cr.x+80,cy,net_is_up()?"UP (10.0.2.15)":"DOWN",net_is_up()?COL_GREEN:COL_RED,COL_SURFACE2);
        cy+=line_h;
        gfx_str(cr.x+8,cy,"FS Nodes:",COL_TEXT,COL_SURFACE2);
        kutoa(vfs_node_count(),buf,10); gfx_str(cr.x+80,cy,buf,COL_ACCENT,COL_SURFACE2);
    } else if(w->tab==1){
        /* Process list */
        i32 line_h = 10 + 6 * sc;
        gfx_str(cr.x+4,cy,"  PID  STATE   NAME",COL_YELLOW,COL_SURFACE2); cy+=line_h;
        for(u32 i=0;i<MAX_TASKS&&cy<cr.y+cr.h-line_h;i++){
            task_t *t=task_get(i+1); if(!t||t->state==TASK_DEAD) continue;
            const char *st[]={"UNUSED","READY","RUN","BLOCK","SLEEP","ZOMBIE","DEAD"};
            char line[48]; char n[8]; kstrcpy(line,"  ");
            kutoa(t->id,n,10); kstrcat(line,n);
            while(kstrlen(line)<7) kstrcat(line," ");
            kstrcat(line,t->state<=TASK_DEAD?st[t->state]:"?");
            while(kstrlen(line)<15) kstrcat(line," ");
            kstrcat(line,t->name);
            u32 col=(t->state==TASK_RUNNING)?COL_GREEN:COL_TEXT;
            gfx_set_clip(cr.x + 4, cy, cr.w - 8, line_h);
            gfx_str(cr.x+4,cy,line,col,COL_SURFACE2);
            gfx_clear_clip();
            cy+=line_h;
        }
    }
 else if(w->tab==2){
        /* Memory graph */
        gfx_str(cr.x+8,cy,"Memory Usage (60s)",COL_TEXT,COL_SURFACE2); cy+=12 + 6 * sc;
        i32 gh=40 + 40 * sc, gw=cr.w-20;
        gfx_rect(cr.x+8,cy,gw,gh,COL_SURFACE3);
        gfx_rect_outline(cr.x+8,cy,gw,gh,COL_BORDER);
        for(int i=1;i<60;i++){
            u32 v1=w->sysmon_mem_hist[(w->sysmon_hist_pos+i-1)%60];
            u32 v2=w->sysmon_mem_hist[(w->sysmon_hist_pos+i)%60];
            i32 y1=cy+gh-(i32)(v1*gh/100);
            i32 y2=cy+gh-(i32)(v2*gh/100);
            gfx_line(cr.x+8+(i-1)*gw/60,y1,cr.x+8+i*gw/60,y2,COL_PRIMARY);
        }
        cy+=gh+8;
        char buf[32]; kutoa(kmem_used()/1024,buf,10);
        gfx_str(cr.x+8,cy,"Used: ",COL_TEXT,COL_SURFACE2); kstrcat(buf," KB");
        gfx_str(cr.x+8 + 8 * sc * 6, cy, buf, COL_ACCENT, COL_SURFACE2);
    }
}
void app_sysmon_click(window_t *w,i32 x,i32 y){
    rect_t cr=wm_client_rect(w);
    i32 sc = (i32)GFX_FONT_SCALE;
    i32 tab_h = 20 + 6 * sc;
    i32 tab_w = cr.w / 4;
    if(y < cr.y + tab_h){
        int idx = (x - cr.x) / tab_w;
        if(idx >= 0 && idx < 4) w->tab = (u32)idx;
    }
}
