/* CareOS v9 -- apps/app_terminal.c -- Terminal application */
#include "apps_common.h"

/* -- Terminal Themes & Customization -------------------------------------- */
typedef struct {
    const char *name;
    u32 bg;
    u32 text;
    u32 prompt;
    u32 input_bg;
    u32 cursor;
    u32 border;
} term_theme_t;

static const term_theme_t g_term_themes[] = {
    {
        "Monokai",
        rgb(0x27, 0x28, 0x22), /* Dark Charcoal/Olive */
        rgb(0xf8, 0xf8, 0xf2), /* Text */
        rgb(0xa6, 0xe2, 0x2e), /* Monokai Green Prompt */
        rgb(0x1e, 0x1f, 0x1c), /* Input BG */
        rgb(0xf9, 0x26, 0x72), /* Monokai Pink Cursor */
        rgb(0x3e, 0x3d, 0x32)  /* Border */
    },
    {
        "Dracula",
        rgb(0x28, 0x2a, 0x36), /* Dark Blue-Gray */
        rgb(0xf8, 0xf8, 0xf2), /* Bright Text */
        rgb(0x50, 0xfa, 0x7b), /* Dracula Green Prompt */
        rgb(0x21, 0x22, 0x2c), /* Input BG */
        rgb(0xff, 0x79, 0xc6), /* Dracula Pink Cursor */
        rgb(0x44, 0x47, 0x5a)  /* Border */
    },
    {
        "Nord",
        rgb(0x2e, 0x34, 0x40), /* Nord Dark Blue */
        rgb(0xd8, 0xde, 0xe9), /* Nord Ice Text */
        rgb(0xa3, 0xbe, 0x8c), /* Nord Green Prompt */
        rgb(0x24, 0x29, 0x33), /* Input BG */
        rgb(0x88, 0xc0, 0xd0), /* Nord Frost Cyan Cursor */
        rgb(0x4c, 0x56, 0x6a)  /* Border */
    },
    {
        "Solarized",
        rgb(0x00, 0x2b, 0x36), /* Solarized Dark Base03 */
        rgb(0x83, 0x94, 0x96), /* Base0 Text */
        rgb(0x85, 0x99, 0x00), /* Solarized Green Prompt */
        rgb(0x07, 0x36, 0x42), /* Base02 Input BG */
        rgb(0x2a, 0xa1, 0x98), /* Solarized Cyan Cursor */
        rgb(0x58, 0x6e, 0x75)  /* Base01 Border */
    }
};

#define TERM_THEME_COUNT (sizeof(g_term_themes) / sizeof(g_term_themes[0]))

static u32 g_term_theme_idx = 0; /* Default: Monokai */
static u8  g_term_opacity   = 217; /* Default ~85% opacity (217/255) */

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
    else if(!kstrcmp(cmd,"theme") || !kstrcmp(cmd,"termtheme")){
        if(argc<2 || !kstrcmp(argv[1],"list")){
            win_append(w,"Terminal Theme Presets:\n");
            for(u32 i=0; i<TERM_THEME_COUNT; i++){
                char num[4]; kutoa(i+1, num, 10);
                win_append(w, " "); win_append(w, num); win_append(w, ". ");
                win_append(w, g_term_themes[i].name);
                if(i == g_term_theme_idx) win_append(w, "  [active]");
                win_append(w, "\n");
            }
            win_append(w,"usage: theme <monokai|dracula|nord|solarized|1-4>\n");
            return;
        }
        const char *arg = argv[1];
        int chosen = -1;
        if(arg[0]>='1' && arg[0]<='4' && arg[1]=='\0'){
            chosen = arg[0] - '1';
        } else {
            for(u32 i=0; i<TERM_THEME_COUNT; i++){
                char tname[32];
                u32 len = kstrlen(g_term_themes[i].name);
                for(u32 k=0; k<len && k<31; k++){
                    char ch = g_term_themes[i].name[k];
                    tname[k] = (ch>='A' && ch<='Z') ? (ch+32) : ch;
                }
                tname[len] = '\0';
                
                char iname[32];
                u32 ilen = kstrlen(arg);
                for(u32 k=0; k<ilen && k<31; k++){
                    char ch = arg[k];
                    iname[k] = (ch>='A' && ch<='Z') ? (ch+32) : ch;
                }
                iname[ilen] = '\0';

                if(!kstrcmp(tname, iname)){
                    chosen = (int)i;
                    break;
                }
            }
        }
        if(chosen >= 0 && (u32)chosen < TERM_THEME_COUNT){
            g_term_theme_idx = (u32)chosen;
            win_append(w, "Terminal theme set to ");
            win_append(w, g_term_themes[g_term_theme_idx].name);
            win_append(w, "\n");
        } else {
            win_append(w, "theme: unknown preset. Use 'theme list' to view options.\n");
        }
    }
    else if(!kstrcmp(cmd,"opacity") || !kstrcmp(cmd,"transparency")){
        if(argc<2){
            char valbuf[16];
            u32 pct = (g_term_opacity * 100) / 255;
            kutoa(pct, valbuf, 10);
            win_append(w, "Current terminal opacity: ");
            win_append(w, valbuf);
            win_append(w, "% (alpha ");
            kutoa(g_term_opacity, valbuf, 10);
            win_append(w, valbuf);
            win_append(w, "/255)\nusage: opacity <0-100>\n");
            return;
        }
        int val = katoi(argv[1]);
        if(val < 0) val = 0;
        if(val > 255) val = 255;
        if(val <= 100 && kstrlen(argv[1]) <= 3){
            val = (val * 255) / 100;
        }
        g_term_opacity = (u8)val;
        u32 pct = (g_term_opacity * 100) / 255;
        char valbuf[16];
        kutoa(pct, valbuf, 10);
        win_append(w, "Terminal opacity set to ");
        win_append(w, valbuf);
        win_append(w, "%\n");
    }
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
        const char *txt=vfs_file_str(f);
        win_append(w,txt);
        if(f->size&&txt[f->size-1]!='\n') win_append(w,"\n");
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
    else if(!kstrcmp(cmd,"res")){
        char b[512]; res_status_text(b,sizeof(b)); win_append(w,b);
    }
    else if(!kstrcmp(cmd,"uptime")){
        u32 ticks = timer_get_ticks();
        u32 sec = ticks / 100;
        char b[12];
        win_append(w,"Uptime: "); kutoa(sec,b,10); win_append(w,b); win_append(w," seconds\n");
    }
    else if(!kstrcmp(cmd,"sysinfo")){
        if(argc>1 && !kstrcmp(argv[1],"--version")){
            win_append(w,"CareOS v9.0 x86 2026\n");
        } else if(argc>1 && !kstrcmp(argv[1],"--cpu")){
            win_append(w,"CPU: Generic x86\n");
        } else if(argc>1 && !kstrcmp(argv[1],"--mem")){
            char b[12]; win_append(w,"Memory Free: ");
            kutoa(kmem_free_bytes()/1024,b,10); win_append(w,b); win_append(w,"K\n");
        } else {
            win_append(w,"usage: sysinfo [--version|--cpu|--mem]\n");
        }
    }
    else if(!kstrcmp(cmd,"network")){
        if(argc>1 && !kstrcmp(argv[1],"--status")){
            win_append(w,net_is_up()?"Network is UP\n":"Network is DOWN\n");
        } else if(argc>1 && !kstrcmp(argv[1],"--ip")){
            if(net_is_up()){
                char ip[24]; term_ip_to_str(net_get_ip(),ip);
                win_append(w,"IP: "); win_append(w,ip); win_append(w,"\n");
            } else {
                win_append(w,"Not connected.\n");
            }
        } else {
            win_append(w,"usage: network [--status|--ip]\n");
        }
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
    else if(!kstrcmp(cmd,"care")){
        if(argc<2){win_append(w,"usage: care <file.cl>\n");return;}
        struct fs_node *f=argv[1][0]=='/'?vfs_resolve_path(argv[1]):vfs_find(term_cwd,argv[1]);
        if(!f||f->type!=FS_FILE){win_append(w,"care: file not found\n");return;}
        char outbuf[2048];
        if(care_lang_exec_buf(vfs_file_str(f),f->size,outbuf,sizeof(outbuf))!=0)
            win_append(w,"care: script error\n");
        else if(outbuf[0]) win_append(w,outbuf);
    }
    else if(!kstrcmp(cmd,"help")){
        win_append(w,"Available commands:\n");
        win_append(w,"- ls, cd, pwd, cat, mkdir, rm, touch\n");
        win_append(w,"- echo, ps, whoami, clear, help\n");
        win_append(w,"- theme [monokai|dracula|nord|solarized]\n");
        win_append(w,"- opacity <0-100>  set window transparency\n");
        win_append(w,"- ifconfig, ping, curl, wifi\n");
        win_append(w,"- uname, date, free, uptime\n");
        win_append(w,"- carepkg, dmesg, settings\n");
        win_append(w,"- sysinfo (--version, --cpu, --mem)\n");
        win_append(w,"- res   icon theme, wallpaper and image cache status\n");
        win_append(w,"- network (--status, --ip)\n");
        win_append(w,"- care <file.cl>  run a Care language script\n");
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
    const term_theme_t *cur_theme = &g_term_themes[g_term_theme_idx];

    /* Background with configurable transparency / opacity */
    if (g_term_opacity >= 253) {
        gfx_rect(cr.x, cr.y, cr.w, cr.h, cur_theme->bg);
    } else if (g_term_opacity > 0) {
        gfx_rect_blend(cr.x, cr.y, cr.w, cr.h, cur_theme->bg, g_term_opacity);
    }

    i32 sc = (i32)GFX_FONT_SCALE;
    i32 lh = FONT_H * sc + 3;
    i32 input_h = lh + 16;
    i32 text_h  = cr.h - input_h;

    /* ---- Scrollable output ---- */
    {
        /* Count total lines */
        u32 total = 1;
        for(u32 i=0;i<w->text_len;i++) if(w->text_buf[i]=='\n') total++;
        u32 vis = (u32)text_h / (u32)lh;
        
        /* Clamp scroll if it goes out of bounds */
        if (w->scroll > total) w->scroll = total;
        if (total > vis && w->scroll > total - vis) w->scroll = total - vis;

        /* Find start */
        u32 line = 0;
        const char *p = w->text_buf;
        while (line < w->scroll && *p) { if(*p=='\n') line++; p++; }

        /* Draw each visible line with theme prompt/text colouring */
        i32 y = cr.y + 6;
        u32 drawn = 0;
        gfx_set_clip(cr.x, cr.y, cr.w, text_h);
        while (*p && drawn < vis) {
            const char *end = p;
            while (*end && *end != '\n') end++;
            u32 len = (u32)(end - p);
            char tmp[160]; if(len>159) len=159;
            kmemcpy(tmp, p, len); tmp[len]='\0';

            /* Prompt vs regular text color from active theme */
            u32 col = cur_theme->text;
            if (tmp[0] && (kstrncmp(tmp, "user@", 5) == 0 || kstrncmp(tmp, "root@", 5) == 0 || kstrncmp(tmp, "CareOS", 6) == 0 || tmp[0] == '$'))
                col = cur_theme->prompt;

            gfx_str_clipped(cr.x + 12, y, cr.w - 20, tmp, col, COL_TRANSPARENT);
            y += lh;
            drawn++;
            if(*end=='\n') p=end+1; else break;
        }
        gfx_clear_clip();
    }

    /* ---- Input bar ---- */
    i32 iy = cr.y + text_h;
    if (g_term_opacity >= 253) {
        gfx_rect(cr.x, iy, cr.w, input_h, cur_theme->input_bg);
    } else if (g_term_opacity > 0) {
        u8 input_alpha = (g_term_opacity > 220) ? g_term_opacity : (u8)(g_term_opacity + 30);
        gfx_rect_blend(cr.x, iy, cr.w, input_h, cur_theme->input_bg, input_alpha);
    }
    gfx_hline(cr.x, iy, cr.w, cur_theme->border);

    /* Prompt symbol */
    i32 ty = iy + (input_h - FONT_H * sc) / 2;
    gfx_str(cr.x + 12, ty, "$", cur_theme->prompt, COL_TRANSPARENT);
    i32 input_x = cr.x + 12 + (FONT_W + 2) * sc;
    gfx_str_clipped(input_x, ty, cr.w - 28, w->input_buf, cur_theme->text, COL_TRANSPARENT);

    /* Animated blinking cursor */
    u32 tick = timer_get_ticks();
    u32 phase = tick % 50; /* 500ms cycle */
    u8 cursor_alpha;
    if (phase < 25) {
        cursor_alpha = (u8)(50 + (phase * 205) / 25);
    } else {
        cursor_alpha = (u8)(50 + ((50 - phase) * 205) / 25);
    }

    i32 cx = input_x + (i32)w->input_len * FONT_W * sc;
    i32 cursor_w = 2 * sc;
    i32 cursor_h = input_h - 8;
    i32 cursor_y = iy + 4;

    gfx_rect_blend(cx, cursor_y, cursor_w, cursor_h, cur_theme->cursor, cursor_alpha);
}
void app_terminal_key(window_t *w,char c){
    if(c=='\n'){
        win_append(w,w->input_buf); win_append(w,"\n");
        term_exec(w,w->input_buf);
        w->input_len=0; w->input_buf[0]='\0';
        term_prompt(w);
        /* Auto-scroll to bottom */
        i32 sc2 = (i32)GFX_FONT_SCALE;
        u32 lh = (u32)(FONT_H * sc2) + 2;
        u32 total = 1;
        for(u32 i=0;i<w->text_len;i++) if(w->text_buf[i]=='\n') total++;
        rect_t cr=wm_client_rect(w);
        i32 input_h2 = FONT_H * sc2 + 16;
        u32 vis=(u32)(cr.h - input_h2)/lh;
        w->scroll=(total>vis)?total-vis:0;
    } else if(c=='\b'){
        if(w->input_len>0){w->input_len--;w->input_buf[w->input_len]='\0';}
    } else if(c==0x0C){ /* Ctrl+L: Clear */
        win_clear(w);
        term_prompt(w);
    } else if(c==0x03){ /* Ctrl+C: Copy */
        if (w->input_len > 0) {
            kstrncpy(g_clipboard, w->input_buf, CLIPBOARD_SIZE-1);
            g_clipboard_len = w->input_len;
        } else {
            kstrncpy(g_clipboard, w->text_buf, CLIPBOARD_SIZE-1);
            g_clipboard_len = w->text_len;
        }
        notify_push("Clipboard", "Copied terminal text", COL_ACCENT);
    } else if(c==0x16){ /* Ctrl+V: Paste */
        for (u32 i=0; i<g_clipboard_len; i++) {
            char cc = g_clipboard[i];
            if (cc >= 32 && cc < 127 && w->input_len < 255) {
                w->input_buf[w->input_len++] = cc;
                w->input_buf[w->input_len] = '\0';
            }
        }
    } else if(c>=32&&c<127&&w->input_len<255){
        w->input_buf[w->input_len++]=c; w->input_buf[w->input_len]='\0';
    } else if(c=='\x1B'){ /* scroll up/down could go here */ }
}
