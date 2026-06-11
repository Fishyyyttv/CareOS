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

    /* Tabs */
    const char *tabs[]={"Overview","Processes","Memory","Network",NULL};
    for(int i=0;tabs[i];i++){
        i32 tx=cr.x+4+i*80;
        u32 bg=(u32)i==w->tab?COL_SURFACE3:COL_SURFACE2;
        gfx_rect(tx,cr.y,76,18,bg); gfx_hline(tx,cr.y+18,76,COL_BORDER);
        gfx_str_centered(tx,cr.y+3,76,tabs[i],COL_TEXT,bg);
    }
    gfx_hline(cr.x,cr.y+18,cr.w,COL_BORDER);
    i32 cy=cr.y+22;

    if(w->tab==0){
        /* Overview */
        char buf[32];
        gfx_str(cr.x+8,cy,   "CPU:",   COL_TEXT,COL_SURFACE2); cy+=18;
        u32 cpu=w->sysmon_cpu_hist[(w->sysmon_hist_pos+59)%60];
        gfx_bar(cr.x+8,cy,cr.w-16,14,COL_SURFACE3,COL_GREEN,cpu);
        kutoa(cpu,buf,10); kstrcat(buf,"%"); gfx_str(cr.x+cr.w-40,cy,buf,COL_TEXT,COL_SURFACE2);
        cy+=20;
        gfx_str(cr.x+8,cy,"Memory:",COL_TEXT,COL_SURFACE2); cy+=18;
        u32 mem_pct=kmem_used()*100/KERNEL_HEAP_SIZE;
        gfx_bar(cr.x+8,cy,cr.w-16,14,COL_SURFACE3,COL_PRIMARY,mem_pct);
        kutoa(mem_pct,buf,10); kstrcat(buf,"%"); gfx_str(cr.x+cr.w-40,cy,buf,COL_TEXT,COL_SURFACE2);
        cy+=20;
        gfx_str(cr.x+8,cy,"Tasks: ",COL_TEXT,COL_SURFACE2);
        kutoa(task_count_active(),buf,10); gfx_str(cr.x+80,cy,buf,COL_ACCENT,COL_SURFACE2); cy+=18;
        gfx_str(cr.x+8,cy,"Uptime:",COL_TEXT,COL_SURFACE2);
        kutoa(timer_get_ticks()/1000,buf,10); kstrcat(buf,"s");
        gfx_str(cr.x+80,cy,buf,COL_ACCENT,COL_SURFACE2); cy+=18;
        gfx_str(cr.x+8,cy,"Network:",COL_TEXT,COL_SURFACE2);
        gfx_str(cr.x+80,cy,net_is_up()?"UP (10.0.2.15)":"DOWN",net_is_up()?COL_GREEN:COL_RED,COL_SURFACE2);
        cy+=18;
        gfx_str(cr.x+8,cy,"FS Nodes:",COL_TEXT,COL_SURFACE2);
        kutoa(vfs_node_count(),buf,10); gfx_str(cr.x+80,cy,buf,COL_ACCENT,COL_SURFACE2);
    } else if(w->tab==1){
        /* Process list */
        gfx_str(cr.x+4,cy,"  PID  STATE   NAME",COL_YELLOW,COL_SURFACE2); cy+=16;
        for(u32 i=0;i<MAX_TASKS&&cy<cr.y+cr.h-4;i++){
            task_t *t=task_get(i+1); if(!t||t->state==TASK_DEAD) continue;
            const char *st[]={"UNUSED","READY","RUN","BLOCK","SLEEP","ZOMBIE","DEAD"};
            char line[48]; char n[8]; kstrcpy(line,"  ");
            kutoa(t->id,n,10); kstrcat(line,n);
            while(kstrlen(line)<7) kstrcat(line," ");
            kstrcat(line,t->state<=TASK_DEAD?st[t->state]:"?");
            while(kstrlen(line)<15) kstrcat(line," ");
            kstrcat(line,t->name);
            u32 col=(t->state==TASK_RUNNING)?COL_GREEN:COL_TEXT;
            gfx_str(cr.x+4,cy,line,col,COL_SURFACE2); cy+=14;
        }
    } else if(w->tab==2){
        /* Memory graph */
        gfx_str(cr.x+8,cy,"Memory Usage (60s)",COL_TEXT,COL_SURFACE2); cy+=18;
        i32 gh=80, gw=cr.w-20;
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
        gfx_str(cr.x+56,cy,buf,COL_ACCENT,COL_SURFACE2);
    }
}
void app_sysmon_click(window_t *w,i32 x,i32 y){
    (void)x; (void)y;
    /* Tab click */
    rect_t cr=wm_client_rect(w);
    if(y<cr.y+18){
        for(int i=0;i<4;i++) if(x>cr.x+4+i*80&&x<cr.x+80+i*80) w->tab=(u32)i;
    }
}
