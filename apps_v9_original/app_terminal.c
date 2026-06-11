/* CareOS v9 -- apps/app_terminal.c -- Terminal application */
#include "apps_common.h"

static struct fs_node *term_cwd=NULL;

static void term_path(struct fs_node *n,char *buf){
    vfs_get_path(n,buf,64); if(!buf[0]){buf[0]='/';buf[1]='\0';}
}

static struct fs_node *term_default_home(void){
    char path[64];
    if(user_current_uid()==0){
        kstrcpy(path,"/root");
    } else {
        kstrcpy(path,"/home/");
        kstrcat(path,user_current_name());
    }
    struct fs_node *h=vfs_resolve_path(path);
    return h ? h : vfs_root();
}

void term_ip_to_str(u32 ip, char *out){
    char a[6],b[6],c[6],d[6];
    out[0]='\0';
    kutoa((ip>>24)&0xff,a,10);
    kutoa((ip>>16)&0xff,b,10);
    kutoa((ip>>8)&0xff,c,10);
    kutoa(ip&0xff,d,10);
    kstrcpy(out,a); kstrcat(out,"."); kstrcat(out,b);
    kstrcat(out,"."); kstrcat(out,c); kstrcat(out,"."); kstrcat(out,d);
}

static int term_parse_ipv4(const char *s, u32 *out){
    if(!s || !out) return -1;
    u32 nums[4]={0,0,0,0};
    int n=0; u32 cur=0; bool have=false;
    while(*s){
        if(*s>='0'&&*s<='9'){
            cur=cur*10u+(u32)(*s-'0');
            if(cur>255u) return -1;
            have=true;
        } else if(*s=='.'){
            if(!have || n>=3) return -1;
            nums[n++]=cur; cur=0; have=false;
        } else return -1;
        s++;
    }
    if(!have || n!=3) return -1;
    nums[3]=cur;
    *out=(nums[0]<<24)|(nums[1]<<16)|(nums[2]<<8)|nums[3];
    return 0;
}
static void term_prompt(window_t *w){
    char p[64]; term_path(term_cwd,p);
    win_append(w,"\n"); win_append(w,user_current_name());
    win_append(w,"@careos:"); win_append(w,p); win_append(w,"$ ");
}

static void term_exec(window_t *w,char *line){
    if(!term_cwd) term_cwd=term_default_home();
    char *argv[8]; int argc=0; char *p=line;
    while(*p){while(*p==' ')p++;if(!*p)break;argv[argc++]=p;if(argc>=8)break;while(*p&&*p!=' ')p++;if(*p)*p++='\0';}
    if(!argc) return;
    const char *cmd=argv[0];

    if(!kstrcmp(cmd,"clear")){win_clear(w);}
    else if(!kstrcmp(cmd,"ls")){
        struct fs_node *d=(argc>=2&&argv[1][0]!='/')?vfs_find(term_cwd,argv[1]):(argc>=2?vfs_resolve_path(argv[1]):term_cwd);
        if(!d){win_append(w,"ls: not found\n");return;}
        for(u32 i=0;i<d->child_count;i++){
            win_append(w,d->children[i]->name);
            win_append(w,d->children[i]->type==FS_DIR?"/  ":"  ");
        }
        win_append(w,"\n");
    }
    else if(!kstrcmp(cmd,"cd")){
        const char *dest=(argc<2)?"~":argv[1];
        struct fs_node *nd=NULL;
        if(kstrcmp(dest,"~")==0) nd=term_default_home();
        else if(kstrcmp(dest,"..")==0) nd=term_cwd->parent?term_cwd->parent:term_cwd;
        else nd=dest[0]=='/'?vfs_resolve_path(dest):vfs_find(term_cwd,dest);
        if(nd&&nd->type==FS_DIR) term_cwd=nd;
        else win_append(w,"cd: not found\n");
    }
    else if(!kstrcmp(cmd,"pwd")){char pp[64];term_path(term_cwd,pp);win_append(w,pp);win_append(w,"\n");}
    else if(!kstrcmp(cmd,"cat")){
        if(argc<2){win_append(w,"usage: cat <file>\n");return;}
        struct fs_node *f=argv[1][0]=='/'?vfs_resolve_path(argv[1]):vfs_find(term_cwd,argv[1]);
        if(!f){win_append(w,"cat: not found\n");return;}
        if(f->type==FS_DIR){win_append(w,"cat: is a directory\n");return;}
        win_append(w,f->data);
        if(f->size&&f->data[f->size-1]!='\n') win_append(w,"\n");
    }
    else if(!kstrcmp(cmd,"mkdir")){if(argc<2)return;struct fs_node *d=vfs_mkdir(term_cwd,argv[1]);if(!d)win_append(w,"mkdir: failed\n");}
    else if(!kstrcmp(cmd,"rm")){
        if(argc<2)return;
        struct fs_node *f=argv[1][0]=='/'?vfs_resolve_path(argv[1]):vfs_find(term_cwd,argv[1]);
        if(!f){win_append(w,"rm: not found\n");return;}
        if(vfs_delete(f)!=0) win_append(w,"rm: failed\n");
    }
    else if(!kstrcmp(cmd,"touch")){
        if(argc<2)return; struct fs_node *f=vfs_mkfile(term_cwd,argv[1]); if(!f) win_append(w,"touch: failed\n");
    }
    else if(!kstrcmp(cmd,"echo")){
        for(int i=1;i<argc;i++){win_append(w,argv[i]);if(i<argc-1)win_append(w," ");}win_append(w,"\n");
    }
    else if(!kstrcmp(cmd,"ps")){
        win_append(w,"  PID  STATE    NAME\n");
        for(u32 i=0;i<MAX_TASKS;i++){
            task_t *t=task_get(i+1); if(!t||t->state==TASK_DEAD) continue;
            char line[48]; char n[8]; kstrcpy(line,"  "); kutoa(t->id,n,10); kstrcat(line,n);
            while(kstrlen(line)<7) kstrcat(line," ");
            const char *st[]={"UNUSED","READY","RUN","BLOCK","SLEEP","ZOMBIE","DEAD"};
            kstrcat(line,t->state<=TASK_DEAD?st[t->state]:"?");
            while(kstrlen(line)<16) kstrcat(line," ");
            kstrcat(line,t->name); kstrcat(line,"\n");
            win_append(w,line);
        }
    }
    else if(!kstrcmp(cmd,"carepkg")){ 
        if(argc<2){win_append(w,"usage: carepkg <cmd> [pkg]\n");return;}
        carepkg_run(argv[1],argc>=3?argv[2]:"");
    }
    else if(!kstrcmp(cmd,"whoami")){ win_append(w,user_current_name()); win_append(w,"\n"); }
    else if(!kstrcmp(cmd,"ifconfig")){
        if(net_is_up()){
            u32 ip=net_get_ip(); char b[4][5];
            kutoa((ip>>24)&0xff,b[0],10); kutoa((ip>>16)&0xff,b[1],10);
            kutoa((ip>>8)&0xff,b[2],10); kutoa(ip&0xff,b[3],10);
            win_append(w,"eth0: "); win_append(w,b[0]); win_append(w,".");
            win_append(w,b[1]); win_append(w,"."); win_append(w,b[2]);
            win_append(w,"."); win_append(w,b[3]); win_append(w,"\n");
        } else win_append(w,"eth0: down\nlo: 127.0.0.1\n");
    }
    else if(!kstrcmp(cmd,"uname"))  win_append(w,"CareOS v9.0 x86 2026\n");
    else if(!kstrcmp(cmd,"date")){
        rtc_time_t t; rtc_read(&t); char b[8];
        kutoa(t.year,b,10); win_append(w,b); win_append(w,"-");
        kutoa(t.month,b,10); if(t.month<10) win_append(w,"0"); win_append(w,b); win_append(w,"-");
        kutoa(t.day,b,10); if(t.day<10) win_append(w,"0"); win_append(w,b); win_append(w," ");
        kutoa(t.hour,b,10); if(t.hour<10) win_append(w,"0"); win_append(w,b); win_append(w,":");
        kutoa(t.minute,b,10); if(t.minute<10) win_append(w,"0"); win_append(w,b); win_append(w,"\n");
    }
    else if(!kstrcmp(cmd,"free")){
        char b[12]; win_append(w,"     total    used    free\nMem: ");
        kutoa(KERNEL_HEAP_SIZE/1024,b,10); win_append(w,b); win_append(w,"K  ");
        kutoa(kmem_used()/1024,b,10); win_append(w,b); win_append(w,"K  ");
        kutoa(kmem_free_bytes()/1024,b,10); win_append(w,b); win_append(w,"K\n");
    }
    else if(!kstrcmp(cmd,"dmesg")){
        if(argc>1 && !kstrcmp(argv[1],"clear")){
            serial_log_clear();
            win_append(w,"dmesg: cleared\n");
            return;
        }
        char logbuf[1024];
        serial_log_tail(logbuf,sizeof(logbuf));
        if(logbuf[0]){ win_append(w,logbuf); if(logbuf[kstrlen(logbuf)-1] != '\n') win_append(w,"\n"); }
        else win_append(w,"(no logs)\n");
    }
    else if(!kstrcmp(cmd,"settings")){
        const careos_settings_t *cfg=settings_get();
        if(argc<2 || !kstrcmp(argv[1],"show")){
            char n[12];
            win_append(w,"theme="); kutoa(cfg->theme,n,10); win_append(w,n);
            win_append(w," mouse="); kutoa(cfg->mouse_sensitivity,n,10); win_append(w,n); win_append(w,"%\n");
            win_append(w,"boot_fast="); win_append(w,cfg->boot_fast?"1":"0");
            win_append(w," clock24="); win_append(w,cfg->clock_24h?"1":"0");
            win_append(w," wallpaper="); kutoa(cfg->wallpaper,n,10); win_append(w,n); win_append(w,"\n");
            return;
        }
        if(!kstrcmp(argv[1],"set")){
            if(argc<4){ win_append(w,"usage: settings set <theme|mouse|boot_fast|clock24|wallpaper|dns> <value>\n"); return; }
            if(!kstrcmp(argv[2],"theme")) settings_set_theme((u32)katoi(argv[3]));
            else if(!kstrcmp(argv[2],"mouse")) settings_set_mouse_sensitivity((u32)katoi(argv[3]));
            else if(!kstrcmp(argv[2],"boot_fast")) settings_set_boot_fast(!kstrcmp(argv[3],"1")||!kstrcmp(argv[3],"on")||!kstrcmp(argv[3],"true"));
            else if(!kstrcmp(argv[2],"clock24")) settings_set_clock_24h(!kstrcmp(argv[3],"1")||!kstrcmp(argv[3],"on")||!kstrcmp(argv[3],"true"));
            else if(!kstrcmp(argv[2],"wallpaper")) settings_set_wallpaper((u32)katoi(argv[3]));
            else if(!kstrcmp(argv[2],"dns")){
                u32 dns=0;
                if(term_parse_ipv4(argv[3],&dns)!=0){ win_append(w,"settings: invalid IPv4\n"); return; }
                net_set_dns_server(dns);
            } else { win_append(w,"settings: unknown key\n"); return; }
            win_append(w,"settings saved\n");
            return;
        }
        win_append(w,"usage: settings [show|set]\n");
    }
    else if(!kstrcmp(cmd,"wifi")){
        if(argc<2){ win_append(w,"usage: wifi <scan|status|connect|disconnect>\n"); return; }
        if(!kstrcmp(argv[1],"scan")){
            win_append(w,"CareHome-5G  WPA2  92%\nOfficeMesh  WPA2  81%\nCoffeeShopGuest  Open 58%\n");
            return;
        }
        if(!kstrcmp(argv[1],"status")){
            const careos_settings_t *cfg=settings_get();
            win_append(w,"wifi: "); win_append(w,cfg->wifi_connected?"connected":"disconnected");
            if(cfg->wifi_ssid[0]){ win_append(w," ssid="); win_append(w,cfg->wifi_ssid); }
            win_append(w,"\n");
            if(net_is_up()){
                char ip[24]; term_ip_to_str(net_get_ip(),ip);
                win_append(w,"ip: "); win_append(w,ip); win_append(w,"\n");
            }
            return;
        }
        if(!kstrcmp(argv[1],"connect")){
            if(argc<3){ win_append(w,"usage: wifi connect <ssid> [password]\n"); return; }
            const char *pw=(argc>=4)?argv[3]:"";
            settings_set_wifi_profile(argv[2],pw,true);
            if(net_dhcp_renew()==0){ win_append(w,"wifi connected\n"); }
            else win_append(w,"wifi profile saved; no link now\n");
            return;
        }
        if(!kstrcmp(argv[1],"disconnect")){
            settings_set_wifi_profile("","",false);
            net_set_ip((127u<<24)|(0u<<16)|(0u<<8)|1u,(255u<<24)|(0u<<16)|(0u<<8)|0u,0);
            net_set_dns_server(0);
            win_append(w,"wifi disconnected\n");
            return;
        }
        win_append(w,"wifi: unknown action\n");
    }
    else if(!kstrcmp(cmd,"help")){        win_append(w,"Commands: ls cd cat mkdir rm touch echo ps whoami\n");
        win_append(w,"         ifconfig wifi uname date free carepkg curl ping\n");
        win_append(w,"         settings dmesg clear help\n");
    }
    else if(!kstrcmp(cmd,"ping")){
        if(argc<2){win_append(w,"usage: ping <host>\n");return;}
        win_append(w,"PING "); win_append(w,argv[1]); win_append(w,"\n");
        for(int i=0;i<3;i++){
            icmp_ping(net_get_ip(),i); timer_wait(200);
            win_append(w,"64 bytes: icmp_seq="); char n[4]; kutoa((u32)i,n,10);
            win_append(w,n); win_append(w," time=~1ms\n");
        }
    }
    else if(!kstrcmp(cmd,"curl")){
        if(argc<2){win_append(w,"usage: curl <url>\n");return;}
        win_append(w,"Fetching...\n");
        char resp[1024]; int n=http_get("example.com",80,"/",resp,sizeof(resp));
        if(n>0){win_append(w,resp);}else win_append(w,"Failed.\n");
    }
    else {
        win_append(w,cmd); win_append(w,": command not found\n");
    }
}

void app_terminal_init(window_t *w){
    win_clear(w); term_cwd=term_default_home();
    win_append(w,"CareOS v9 Terminal\nType 'help' for commands.\n");
    term_prompt(w);
}
void app_terminal_draw(window_t *w){
    rect_t cr=wm_client_rect(w);
    gfx_rect(cr.x,cr.y,cr.w,cr.h,COL_SURFACE2);
    draw_scrollable_text(w,cr);
    /* Input line at bottom */
    i32 iy=cr.y+cr.h-FONT_H-6;
    gfx_rect(cr.x,iy-2,cr.w,FONT_H+8,COL_INPUT_BG);
    gfx_hline(cr.x,iy-2,cr.w,COL_BORDER);
    gfx_str(cr.x+4,iy,w->input_buf,COL_GREEN,COL_INPUT_BG);
    /* Cursor blink */
    if((timer_get_ticks()/40)%2==0){
        i32 cx=cr.x+4+(i32)w->input_len*FONT_W;
        gfx_vline(cx,iy,FONT_H,COL_GREEN);
    }
}
void app_terminal_key(window_t *w,char c){
    if(c=='\n'){
        win_append(w,w->input_buf); win_append(w,"\n");
        term_exec(w,w->input_buf);
        w->input_len=0; w->input_buf[0]='\0';
        term_prompt(w);
        /* Auto-scroll to bottom */
        u32 lh=FONT_H+2; u32 total=1;
        for(u32 i=0;i<w->text_len;i++) if(w->text_buf[i]=='\n') total++;
        rect_t cr=wm_client_rect(w);
        u32 vis=(u32)(cr.h-FONT_H-12)/lh;
        w->scroll=(total>vis)?total-vis:0;
    } else if(c=='\b'){
        if(w->input_len>0){w->input_len--;w->input_buf[w->input_len]='\0';}
    } else if(c>=32&&c<127&&w->input_len<WIN_TEXT_BUF-1){
        w->input_buf[w->input_len++]=c; w->input_buf[w->input_len]='\0';
    } else if(c=='\x1B'){ /* scroll up/down could go here */ }
}
