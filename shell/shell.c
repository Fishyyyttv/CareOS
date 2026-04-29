/* =============================================================================
 * CareOS v6 â€” shell/shell.c
 * Interactive shell: history, tab-complete, built-in commands, networking
 * ============================================================================= */
 #include "kernel.h"

 #define MAX_LINE   512
 #define HIST_SIZE  64
 #define MAX_ARGS   16
 
 static char      line_buf[MAX_LINE];
 static u32       line_len   = 0;
 static char      history[HIST_SIZE][MAX_LINE];
 static u32       hist_count = 0;
 static u32       hist_pos   = 0;
 static fs_node_t *cwd       = NULL;
 
 /* â”€â”€ Line editing helpers â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
 static void line_clear(void){
     while(line_len>0){ terminal_putchar('\b'); line_len--; }
     line_buf[0]='\0';
 }
 static void line_redraw(void){
     for(u32 i=0;i<line_len;i++) terminal_putchar(line_buf[i]);
 }
 static void hist_push(void){
     if(!line_len) return;
     kstrncpy(history[hist_count%HIST_SIZE],line_buf,MAX_LINE-1);
     hist_count++; hist_pos=hist_count;
 }
 
 /* â”€â”€ Prompt â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
 static void print_prompt(void){
     char path[128]; vfs_get_path(cwd,path,sizeof(path));
     if(!path[0]){path[0]='/';path[1]='\0';}
     terminal_write_colored(user_current_name(),VGA_ENTRY_COLOR(VGA_LIGHT_GREEN,VGA_BLACK));
     terminal_write_colored("@careos:",VGA_ENTRY_COLOR(VGA_WHITE,VGA_BLACK));
     terminal_write_colored(path,VGA_ENTRY_COLOR(VGA_LIGHT_CYAN,VGA_BLACK));
     terminal_write_colored("$ ",VGA_ENTRY_COLOR(VGA_WHITE,VGA_BLACK));
 }
 
 /* â”€â”€ Built-in command dispatcher â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
 static void current_home_path(char *buf, u32 max){
    if(!buf || max==0) return;
    if(user_current_uid()==0){
        kstrncpy(buf,"/root",max-1);
        buf[max-1]='\0';
        return;
    }
    kstrncpy(buf,"/home/",max-1);
    buf[max-1]='\0';
    kstrcat(buf,user_current_name());
}

static void ip_to_str(u32 ip, char *out, u32 max){
    if(!out || max<8) return;
    char a[6],b[6],c[6],d[6];
    kutoa((ip>>24)&0xff,a,10);
    kutoa((ip>>16)&0xff,b,10);
    kutoa((ip>>8)&0xff,c,10);
    kutoa(ip&0xff,d,10);
    out[0]='\0';
    kstrncpy(out,a,max-1); out[max-1]='\0';
    kstrcat(out,"."); kstrcat(out,b);
    kstrcat(out,"."); kstrcat(out,c);
    kstrcat(out,"."); kstrcat(out,d);
}

static int parse_ipv4(const char *s, u32 *out){
    if(!s || !out) return -1;
    u32 nums[4]={0,0,0,0};
    int n=0;
    u32 cur=0;
    bool have=false;
    while(*s){
        if(*s>='0'&&*s<='9'){
            cur=cur*10u+(u32)(*s-'0');
            if(cur>255u) return -1;
            have=true;
        } else if(*s=='.'){
            if(!have || n>=3) return -1;
            nums[n++]=cur;
            cur=0; have=false;
        } else {
            return -1;
        }
        s++;
    }
    if(!have || n!=3) return -1;
    nums[3]=cur;
    *out=(nums[0]<<24)|(nums[1]<<16)|(nums[2]<<8)|nums[3];
    return 0;
}
/* Pipe support: output capture buffer for left side of pipe */
static char   g_pipe_stdin[PIPE_BUF_SIZE];
static bool   g_pipe_stdin_active = false;
static char   g_pipe_out[PIPE_BUF_SIZE];
static u32    g_pipe_out_len = 0;
static bool   g_pipe_capture = false;

/* Wrapper: write to pipe capture buffer or terminal */
static void shout(const char *s) {
    if (g_pipe_capture) {
        u32 l = (u32)kstrlen(s);
        if (g_pipe_out_len + l < PIPE_BUF_SIZE - 1) {
            kmemcpy(g_pipe_out + g_pipe_out_len, s, l);
            g_pipe_out_len += l;
            g_pipe_out[g_pipe_out_len] = '\0';
        }
    } else {
        terminal_write(s);
    }
}

static void exec_cmd(char *line);   /* forward */

static void exec_line(char *line){
     /* Pipe detection: split on first | */
     char *pipe_pos = NULL;
     for (u32 pi = 0; line[pi]; pi++) {
         if (line[pi]=='|') { pipe_pos = &line[pi]; break; }
     }
     if (pipe_pos) {
         *pipe_pos = '\0';
         char right[MAX_LINE];
         kstrncpy(right, pipe_pos+1, MAX_LINE-1);
         /* trim leading spaces from right */
         char *rp = right; while(*rp==' ') rp++;

         /* Run left command, capture output */
         g_pipe_out[0]='\0'; g_pipe_out_len=0; g_pipe_capture=true;
         exec_cmd(line);
         g_pipe_capture=false;

         /* Feed captured output as stdin for right command */
         kmemcpy(g_pipe_stdin, g_pipe_out, g_pipe_out_len+1);
         g_pipe_stdin_active=true;
         exec_cmd(rp);
         g_pipe_stdin_active=false;
         return;
     }
     exec_cmd(line);
}

static void exec_cmd(char *line){
     char *argv[MAX_ARGS]; int argc=0; char *p=line;
     while(*p){ while(*p==' ')p++; if(!*p)break; argv[argc++]=p; if(argc>=MAX_ARGS)break; while(*p&&*p!=' ')p++; if(*p)*p++='\0'; }
     if(!argc) return;
     const char *cmd=argv[0];
 
     /* â”€â”€ Filesystem â”€â”€ */
     if(!kstrcmp(cmd,"ls")){
         fs_node_t *d=cwd;
         bool long_fmt=false;
         for(int i=1;i<argc;i++){
             if(kstrcmp(argv[i],"-l")==0) long_fmt=true;
             else { d=(argv[i][0]=='/')?vfs_resolve_path(argv[i]):vfs_find(cwd,argv[i]); }
         }
         if(!d){terminal_write("ls: not found\n");return;}
         for(u32 i=0;i<d->child_count;i++){
             fs_node_t *c=d->children[i];
             if(long_fmt){
                 terminal_write_colored(c->type==FS_DIR?"d":"-",VGA_ENTRY_COLOR(VGA_LIGHT_CYAN,VGA_BLACK));
                 terminal_write("rwxr-xr-x  1  user  user  ");
                 char sz[12]; kutoa(c->size,sz,10);
                 while(kstrlen(sz)<8){char tmp[12];tmp[0]=' ';kstrcpy(tmp+1,sz);kstrcpy(sz,tmp);}
                 terminal_write(sz); terminal_write("  ");
                 terminal_write_colored(c->name,c->type==FS_DIR?VGA_ENTRY_COLOR(VGA_LIGHT_BLUE,VGA_BLACK):VGA_ENTRY_COLOR(VGA_WHITE,VGA_BLACK));
                 terminal_write("\n");
             } else {
                 terminal_write_colored(c->name,c->type==FS_DIR?VGA_ENTRY_COLOR(VGA_LIGHT_BLUE,VGA_BLACK):VGA_ENTRY_COLOR(VGA_WHITE,VGA_BLACK));
                 terminal_write(c->type==FS_DIR?"/  ":"  ");
             }
         }
         if(!long_fmt) terminal_write("\n");
         return;
     }
     if(!kstrcmp(cmd,"cd")){
         const char *dest=(argc<2)?"~":argv[1];
         fs_node_t *nd=NULL;
         if(kstrcmp(dest,"~")==0||kstrcmp(dest,"")==0) {
             char home[64]; current_home_path(home,sizeof(home));
             nd=vfs_resolve_path(home);
         }
         else if(kstrcmp(dest,"..")==0) nd=cwd->parent?cwd->parent:cwd;
         else if(kstrcmp(dest,".")==0)  nd=cwd;
         else nd=(dest[0]=='/')?vfs_resolve_path(dest):vfs_find(cwd,dest);
         if(nd&&nd->type==FS_DIR) cwd=nd;
         else { terminal_write_colored(dest,VGA_ENTRY_COLOR(VGA_LIGHT_RED,VGA_BLACK)); terminal_write(": no such directory\n"); }
         return;
     }
     if(!kstrcmp(cmd,"pwd")){
         char pp[128]; vfs_get_path(cwd,pp,sizeof(pp));
         if(!pp[0]){pp[0]='/';pp[1]='\0';}
         terminal_write(pp); terminal_write("\n"); return;
     }
     if(!kstrcmp(cmd,"cat")){
         if(argc<2){terminal_write("usage: cat <file>\n");return;}
         fs_node_t *f=(argv[1][0]=='/')?vfs_resolve_path(argv[1]):vfs_find(cwd,argv[1]);
         if(!f){terminal_write("cat: "); terminal_write(argv[1]); terminal_write(": not found\n");return;}
         if(f->type==FS_DIR){terminal_write("cat: is a directory\n");return;}
         terminal_write(f->data);
         if(f->size&&f->data[f->size-1]!='\n') terminal_write("\n");
         return;
     }
     if(!kstrcmp(cmd,"mkdir")){
         if(argc<2){terminal_write("usage: mkdir <dir>\n");return;}
         for(int i=1;i<argc;i++){ fs_node_t *d=vfs_mkdir(cwd,argv[i]); if(!d){terminal_write("mkdir: cannot create ");terminal_write(argv[i]);terminal_write("\n");} }
         return;
     }
     if(!kstrcmp(cmd,"touch")){
         if(argc<2){terminal_write("usage: touch <file>\n");return;}
         for(int i=1;i<argc;i++){ vfs_mkfile(cwd,argv[i]); }
         return;
     }
     if(!kstrcmp(cmd,"rm")){
         if(argc<2){terminal_write("usage: rm [-r] <path>\n");return;}
         for(int i=1;i<argc;i++){
             if(kstrcmp(argv[i],"-r")==0) continue;
             fs_node_t *f=(argv[i][0]=='/')?vfs_resolve_path(argv[i]):vfs_find(cwd,argv[i]);
             if(!f){terminal_write("rm: ");terminal_write(argv[i]);terminal_write(": not found\n");}
             else if(vfs_delete(f)!=0){terminal_write("rm: cannot remove ");terminal_write(argv[i]);terminal_write("\n");}
         }
         return;
     }
     if(!kstrcmp(cmd,"cp")){
         if(argc<3){terminal_write("usage: cp <src> <dst>\n");return;}
         fs_node_t *src=(argv[1][0]=='/')?vfs_resolve_path(argv[1]):vfs_find(cwd,argv[1]);
         if(!src){terminal_write("cp: source not found\n");return;}
         vfs_copy(src,cwd,argv[2]);
         return;
     }
     if(!kstrcmp(cmd,"mv")){
         if(argc<3){terminal_write("usage: mv <src> <dst>\n");return;}
         fs_node_t *src=(argv[1][0]=='/')?vfs_resolve_path(argv[1]):vfs_find(cwd,argv[1]);
         if(!src){terminal_write("mv: not found\n");return;}
         vfs_rename(src,argv[2]);
         return;
     }
     if(!kstrcmp(cmd,"echo")){
         for(int i=1;i<argc;i++){terminal_write(argv[i]);if(i<argc-1)terminal_write(" ");}
         terminal_write("\n"); return;
     }
     if(!kstrcmp(cmd,"find")){
         if(argc<2){terminal_write("usage: find <name>\n");return;}
         /* Simple recursive search from cwd */
         fs_node_t *stack[64]; int top=0;
         stack[top++]=cwd;
         while(top>0){
             fs_node_t *n=stack[--top];
             for(u32 i=0;i<n->child_count;i++){
                 fs_node_t *c=n->children[i];
                 if(kstrcmp(c->name,argv[1])==0){ char pp[128]; vfs_get_path(c,pp,sizeof(pp)); terminal_write(pp); terminal_write("\n"); }
                 if(c->type==FS_DIR&&top<63) stack[top++]=c;
             }
         }
         return;
     }
     if(!kstrcmp(cmd,"grep")){
         if(argc<3){terminal_write("usage: grep <pattern> <file>\n");return;}
         fs_node_t *f=(argv[2][0]=='/')?vfs_resolve_path(argv[2]):vfs_find(cwd,argv[2]);
         if(!f){terminal_write("grep: file not found\n");return;}
         /* Line-by-line search */
         const char *p=f->data; char line2[256];
         while(*p){
             int l=0; const char *start=p;
             while(*p&&*p!='\n'&&l<255) line2[l++]=(char)*p++;
             line2[l]='\0'; if(*p=='\n') p++;
             if(kstrchr(line2,argv[1][0])){ /* simple substring */
                 bool found=false;
                 for(int i=0;line2[i];i++){
                     if(kstrncmp(line2+i,argv[1],kstrlen(argv[1]))==0){found=true;break;}
                 }
                 if(found){ terminal_write_colored(line2,VGA_ENTRY_COLOR(VGA_LIGHT_GREEN,VGA_BLACK)); terminal_write("\n"); }
             }
             (void)start;
         }
         return;
     }
     if(!kstrcmp(cmd,"wc")){
         if(argc<2){terminal_write("usage: wc <file>\n");return;}
         fs_node_t *f=(argv[1][0]=='/')?vfs_resolve_path(argv[1]):vfs_find(cwd,argv[1]);
         if(!f){terminal_write("wc: not found\n");return;}
         u32 lines=0,words=0,bytes=f->size; bool in_word=false;
         for(u32 i=0;i<f->size;i++){
             char c=f->data[i]; if(c=='\n') lines++;
             if(c==' '||c=='\t'||c=='\n'){in_word=false;}else if(!in_word){words++;in_word=true;}
         }
         char b[12];
         kutoa(lines,b,10); terminal_write(b); terminal_write(" ");
         kutoa(words,b,10); terminal_write(b); terminal_write(" ");
         kutoa(bytes,b,10); terminal_write(b); terminal_write(" ");
         terminal_write(argv[1]); terminal_write("\n");
         return;
     }
     if(!kstrcmp(cmd,"write")||!kstrcmp(cmd,"echo>")){ /* write file */
        if(argc<3){terminal_write("usage: write <file> <content>\n");return;}
        fs_node_t *f=vfs_find(cwd,argv[1]);
        if(!f) f=vfs_mkfile(cwd,argv[1]);
        if(f){ vfs_write(f,argv[2],kstrlen(argv[2])); }
        return;
    }
    if(!kstrcmp(cmd,"lspci")){
        pci_list();
        return;
    }
 
     /* ── Process ── */
     if(!kstrcmp(cmd,"ps")){
         terminal_write("  PID  PRI   STATE     NAME\n");
         for(u32 i=0;i<MAX_TASKS;i++){
             task_t *t=task_get(i+1); if(!t||t->state==TASK_DEAD) continue;
             const char *states[]={"UNUSED","READY","RUNNING","BLOCKED","SLEEP","ZOMBIE","DEAD"};
             char line2[64]; char n[8]; kstrcpy(line2,"  ");
             kutoa(t->id,n,10); kstrcat(line2,n);
             while(kstrlen(line2)<6) kstrcat(line2," ");
             kutoa(t->tick_count,n,10); kstrcat(line2,n);
             while(kstrlen(line2)<10) kstrcat(line2," ");
             kstrcat(line2,t->state<=TASK_DEAD?states[t->state]:"?");
             while(kstrlen(line2)<21) kstrcat(line2," ");
             kstrcat(line2,t->name); kstrcat(line2,"\n");
             u32 col=t->state==TASK_RUNNING?VGA_ENTRY_COLOR(VGA_LIGHT_GREEN,VGA_BLACK):VGA_ENTRY_COLOR(VGA_WHITE,VGA_BLACK);
             terminal_write_colored(line2,col);
         }
         return;
     }
     if(!kstrcmp(cmd,"kill")){
         if(argc<2){terminal_write("usage: kill <pid>\n");return;}
         u32 pid=(u32)katoi(argv[1]);
         task_t *t=task_get(pid);
         if(t){ t->state=TASK_DEAD; terminal_write("killed\n"); }
         else terminal_write("kill: no such process\n");
         return;
     }
     if(!kstrcmp(cmd,"sleep")){
         if(argc<2){terminal_write("usage: sleep <seconds>\n");return;}
         u32 ms=(u32)katoi(argv[1])*1000;
         timer_wait(ms);
         return;
     }
 
     /* â”€â”€ System info â”€â”€ */
     if(!kstrcmp(cmd,"whoami"))  { terminal_write(user_current_name()); terminal_write("\n"); return; }
     if(!kstrcmp(cmd,"id")){
         char n[8]; terminal_write("uid="); kutoa(user_current_uid(),n,10); terminal_write(n);
         terminal_write("("); terminal_write(user_current_name()); terminal_write(")");
         terminal_write(user_is_admin(user_current_uid())?" groups=0(root)\n":" groups=1000(user)\n"); return;
     }
     if(!kstrcmp(cmd,"uname")){
         bool all=(argc>1&&kstrcmp(argv[1],"-a")==0);
         terminal_write("CareOS");
         if(all) terminal_write(" careos v6.0 #1 SMP i686 x86 GNU/CareOS");
         terminal_write("\n"); return;
     }
     if(!kstrcmp(cmd,"hostname")){ terminal_write("careos\n"); return; }
     if(!kstrcmp(cmd,"uptime")){
         char b[12]; u32 s=timer_get_ticks()/1000, m=s/60, h=m/60;
         terminal_write("up "); kutoa(h,b,10); terminal_write(b); terminal_write("h ");
         kutoa(m%60,b,10); terminal_write(b); terminal_write("m ");
         kutoa(s%60,b,10); terminal_write(b); terminal_write("s, tasks=");
         kutoa(task_count_active(),b,10); terminal_write(b); terminal_write("\n"); return;
     }
     if(!kstrcmp(cmd,"date")){
         rtc_time_t t; rtc_read(&t); char b[8];
         kutoa(t.year,b,10); terminal_write(b); terminal_write("-");
         kutoa(t.month,b,10); if(t.month<10) terminal_write("0"); terminal_write(b); terminal_write("-");
         kutoa(t.day,b,10);   if(t.day<10)   terminal_write("0"); terminal_write(b); terminal_write(" ");
         kutoa(t.hour,b,10); if(t.hour<10) terminal_write("0"); terminal_write(b); terminal_write(":");
         kutoa(t.minute,b,10); if(t.minute<10) terminal_write("0"); terminal_write(b); terminal_write(":");
         kutoa(t.second,b,10); if(t.second<10) terminal_write("0"); terminal_write(b); terminal_write("\n"); return;
     }
     if(!kstrcmp(cmd,"free")){
         char b[12];
         terminal_write("              total        used        free\n");
         terminal_write("Mem:     ");
         kutoa(KERNEL_HEAP_SIZE/1024,b,10); while(kstrlen(b)<12){char t[12];t[0]=' ';kstrcpy(t+1,b);kstrcpy(b,t);} terminal_write(b);
         kutoa(kmem_used()/1024,b,10); while(kstrlen(b)<12){char t[12];t[0]=' ';kstrcpy(t+1,b);kstrcpy(b,t);} terminal_write(b);
         kutoa(kmem_free_bytes()/1024,b,10); while(kstrlen(b)<12){char t[12];t[0]=' ';kstrcpy(t+1,b);kstrcpy(b,t);} terminal_write(b);
         terminal_write("\n"); return;
     }
     if(!kstrcmp(cmd,"df")){
         terminal_write("Filesystem     Size   Used  Avail  Use%  Mount\n");
         char b[12]; kutoa(vfs_node_count(),b,10);
         terminal_write("/dev/hda       512M    --      --    --    /\n");
         terminal_write("tmpfs           16M  "); terminal_write(b); terminal_write(" inodes    --    /tmp\n");
         return;
     }
     if(!kstrcmp(cmd,"sysinfo")){ sysinfo_print(); return; }
     if(!kstrcmp(cmd,"lspci")){ pci_list_devices(); return; }
     if(!kstrcmp(cmd,"lsblk")){ terminal_write("sda    512M   disk\nâ””â”€sda1 512M   part  /\n"); return; }
     if(!kstrcmp(cmd,"dmesg")){
         if(argc>1 && !kstrcmp(argv[1],"clear")){
             serial_log_clear();
             terminal_write("dmesg: cleared\n");
             return;
         }
         char logbuf[2048];
         serial_log_tail(logbuf,sizeof(logbuf));
         if(logbuf[0]){
             terminal_write(logbuf);
             if(logbuf[kstrlen(logbuf)-1] != '\n') terminal_write("\n");
         } else {
             terminal_write("(no logs)\n");
         }
         return;
     }
     if(!kstrcmp(cmd,"settings")){
         const careos_settings_t *cfg=settings_get();
         if(argc<2 || !kstrcmp(argv[1],"show")){
             char b[16];
             terminal_write("theme="); kutoa(cfg->theme,b,10); terminal_write(b);
             terminal_write("  mouse="); kutoa(cfg->mouse_sensitivity,b,10); terminal_write(b); terminal_write("%\n");
             terminal_write("boot_fast="); terminal_write(cfg->boot_fast?"1":"0");
             terminal_write("  clock24="); terminal_write(cfg->clock_24h?"1":"0");
             terminal_write("  wallpaper="); kutoa(cfg->wallpaper,b,10); terminal_write(b); terminal_write("\n");
             terminal_write("wifi="); terminal_write(cfg->wifi_connected?"connected":"disconnected");
             if(cfg->wifi_ssid[0]){ terminal_write(" ssid="); terminal_write(cfg->wifi_ssid); }
             terminal_write("\n");
             return;
         }
         if(!kstrcmp(argv[1],"set")){
             if(argc<4){
                 terminal_write("usage: settings set <theme|mouse|boot_fast|clock24|wallpaper|dns> <value>\n");
                 return;
             }
             if(!kstrcmp(argv[2],"theme")){
                 settings_set_theme((u32)katoi(argv[3]));
             } else if(!kstrcmp(argv[2],"mouse")){
                 settings_set_mouse_sensitivity((u32)katoi(argv[3]));
             } else if(!kstrcmp(argv[2],"boot_fast")){
                 bool on = !kstrcmp(argv[3],"1") || !kstrcmp(argv[3],"on") || !kstrcmp(argv[3],"true");
                 settings_set_boot_fast(on);
             } else if(!kstrcmp(argv[2],"clock24")){
                 bool on = !kstrcmp(argv[3],"1") || !kstrcmp(argv[3],"on") || !kstrcmp(argv[3],"true");
                 settings_set_clock_24h(on);
             } else if(!kstrcmp(argv[2],"wallpaper")){
                 settings_set_wallpaper((u32)katoi(argv[3]));
             } else if(!kstrcmp(argv[2],"dns")){
                 u32 dns=0;
                 if(parse_ipv4(argv[3],&dns)!=0){ terminal_write("settings: invalid IPv4\n"); return; }
                 net_set_dns_server(dns);
             } else {
                 terminal_write("settings: unknown key\n");
                 return;
             }
             terminal_write("settings saved\n");
             return;
         }
         terminal_write("usage: settings [show|set]\n");
         return;
     }
 
     /* â”€â”€ User management â”€â”€ */
     if(!kstrcmp(cmd,"passwd")){
         const char *uname=(argc>1)?argv[1]:user_current_name();
         terminal_write("New password: ");
         char pw[64]; u32 pl=0; char c;
         while((c=keyboard_getchar())!='\n'&&pl<63){ pw[pl++]=c; } pw[pl]='\0';
         terminal_write("\n");
         user_passwd(uname,pw); terminal_write("Password updated.\n");
         return;
     }
     if(!kstrcmp(cmd,"su")){
         const char *uname=(argc>1)?argv[1]:"root";
         terminal_write("Password: ");
         char pw[64]; u32 pl=0; char c2;
         while((c2=keyboard_getchar())!='\n'&&pl<63){ pw[pl++]=c2; } pw[pl]='\0';
         terminal_write("\n");
         if(user_login(uname,pw)==0){ terminal_write("Switched to "); terminal_write(uname); terminal_write("\n"); }
         else terminal_write("su: Authentication failure\n");
         return;
     }
     if(!kstrcmp(cmd,"useradd")){
         if(argc<3){terminal_write("usage: useradd <name> <password>\n");return;}
         int rc=user_create(argv[1],argv[2]);
         if(rc==0) terminal_write("User created.\n");
         else if(rc==-2) terminal_write("useradd: weak password (min 8, upper/lower/digit)\n");
         else if(rc==-3) terminal_write("useradd: root privileges required\n");
         else terminal_write("useradd: failed\n");
         return;
     }
     if(!kstrcmp(cmd,"userdel")){
         if(argc<2){terminal_write("usage: userdel <name>\n");return;}
         user_delete(argv[1]); terminal_write("User deleted.\n");
         return;
     }
 
     /* â”€â”€ Networking â”€â”€ */
     if(!kstrcmp(cmd,"ifconfig")){
         if(net_is_up()){
             u32 ip=net_get_ip(); char b[4][6];
             kutoa((ip>>24)&0xff,b[0],10); kutoa((ip>>16)&0xff,b[1],10);
             kutoa((ip>>8)&0xff,b[2],10);  kutoa(ip&0xff,b[3],10);
             terminal_write_colored("eth0",VGA_ENTRY_COLOR(VGA_LIGHT_CYAN,VGA_BLACK));
             terminal_write(": flags=4163<UP,BROADCAST,RUNNING,MULTICAST>\n");
             terminal_write("        inet "); terminal_write(b[0]); terminal_write(".");
             terminal_write(b[1]); terminal_write("."); terminal_write(b[2]);
             terminal_write("."); terminal_write(b[3]); terminal_write("  netmask 255.255.255.0\n");
             terminal_write("        ether 52:54:00:12:34:56\n\n");
         } else terminal_write("eth0: flags=0<DOWN>\n");
         terminal_write_colored("lo",VGA_ENTRY_COLOR(VGA_LIGHT_CYAN,VGA_BLACK));
         terminal_write(":   flags=73<UP,LOOPBACK,RUNNING>\n");
         terminal_write("        inet 127.0.0.1  netmask 255.0.0.0\n");
         return;
     }
     if(!kstrcmp(cmd,"wifi")){
         if(argc<2){ terminal_write("usage: wifi <scan|status|connect|disconnect>\n"); return; }
         if(!kstrcmp(argv[1],"scan")){
             terminal_write("Scanning Wi-Fi networks...\n");
             terminal_write("  1) CareHome-5G      WPA2  92%\n");
             terminal_write("  2) OfficeMesh       WPA2  81%\n");
             terminal_write("  3) CoffeeShopGuest  Open  58%\n");
             return;
         }
         if(!kstrcmp(argv[1],"status")){
             const careos_settings_t *cfg=settings_get();
             terminal_write("wifi: "); terminal_write(cfg->wifi_connected?"connected":"disconnected");
             if(cfg->wifi_ssid[0]){ terminal_write(" ssid="); terminal_write(cfg->wifi_ssid); }
             terminal_write("\n");
             if(net_is_up()){
                 char ip[20]; ip_to_str(net_get_ip(),ip,sizeof(ip));
                 terminal_write("ip: "); terminal_write(ip); terminal_write("\n");
             }
             return;
         }
         if(!kstrcmp(argv[1],"connect")){
             if(argc<3){ terminal_write("usage: wifi connect <ssid> [password]\n"); return; }
             const char *pw=(argc>=4)?argv[3]:"";
             settings_set_wifi_profile(argv[2],pw,true);
             if(net_dhcp_renew()==0){
                 terminal_write("wifi: connected to "); terminal_write(argv[2]); terminal_write("\n");
             } else {
                 terminal_write("wifi: profile saved, link unavailable right now\n");
             }
             return;
         }
         if(!kstrcmp(argv[1],"disconnect")){
             settings_set_wifi_profile("","",false);
             net_set_ip((127u<<24)|(0u<<16)|(0u<<8)|1u,(255u<<24)|(0u<<16)|(0u<<8)|0u,0);
             net_set_dns_server(0);
             terminal_write("wifi: disconnected\n");
             return;
         }
         terminal_write("wifi: unknown action\n");
         return;
     }
     if(!kstrcmp(cmd,"ping")){         if(argc<2){terminal_write("usage: ping <host>\n");return;}
         u32 ip=net_get_ip(); u32 dst=ip; /* default to self */
         dns_resolve(argv[1],&dst);
         terminal_write("PING "); terminal_write(argv[1]); terminal_write(" (");
         char b[4][6];
         kutoa((dst>>24)&0xff,b[0],10); kutoa((dst>>16)&0xff,b[1],10);
         kutoa((dst>>8)&0xff,b[2],10);  kutoa(dst&0xff,b[3],10);
         terminal_write(b[0]); terminal_write("."); terminal_write(b[1]); terminal_write(".");
         terminal_write(b[2]); terminal_write("."); terminal_write(b[3]); terminal_write("):\n");
         for(int i=0;i<4;i++){
             icmp_ping(dst,(u32)i); timer_wait(250);
             terminal_write("64 bytes from ");
             terminal_write(b[0]); terminal_write("."); terminal_write(b[1]); terminal_write(".");
             terminal_write(b[2]); terminal_write("."); terminal_write(b[3]);
             terminal_write(": icmp_seq="); char n[4]; kutoa((u32)i,n,10);
             terminal_write(n); terminal_write(" ttl=64 time=~1ms\n");
         }
         return;
     }
     if(!kstrcmp(cmd,"curl")||!kstrcmp(cmd,"wget")){
         if(argc<2){terminal_write("usage: curl <url>\n");return;}
         const char *url=argv[1];
         const char *host=url; if(kstrncmp(url,"http://",7)==0) host=url+7;
         const char *slash=kstrchr(host,'/');
         char host_buf[64]; const char *path="/";
         if(slash){u32 l=(u32)(slash-host);if(l>63)l=63;kmemcpy(host_buf,host,l);host_buf[l]='\0';path=slash;}
         else kstrncpy(host_buf,host,63);
         terminal_write("Connecting to "); terminal_write(host_buf); terminal_write("...\n");
         char resp[4096]; int n=http_get(host_buf,80,path,resp,sizeof(resp));
         if(n>0){ terminal_write(resp); if(resp[n-1]!='\n') terminal_write("\n"); }
         else terminal_write("curl: connection failed\n");
         return;
     }
     if(!kstrcmp(cmd,"netstat")){
         terminal_write("Proto  Local           Remote          State\n");
         terminal_write("tcp    0.0.0.0:80      0.0.0.0:*       LISTEN\n");
         terminal_write("udp    0.0.0.0:53      0.0.0.0:*       \n");
         return;
     }
     if(!kstrcmp(cmd,"nslookup")||!kstrcmp(cmd,"host")){
         if(argc<2){terminal_write("usage: nslookup <host>\n");return;}
         u32 ip=0;
         if(dns_resolve(argv[1],&ip)==0){
             char b[4][6];
             kutoa((ip>>24)&0xff,b[0],10); kutoa((ip>>16)&0xff,b[1],10);
             kutoa((ip>>8)&0xff,b[2],10);  kutoa(ip&0xff,b[3],10);
             terminal_write("Address: ");
             terminal_write(b[0]); terminal_write("."); terminal_write(b[1]);
             terminal_write("."); terminal_write(b[2]); terminal_write("."); terminal_write(b[3]); terminal_write("\n");
         } else terminal_write("nslookup: cannot resolve\n");
         return;
     }
 
     /* â”€â”€ Package manager â”€â”€ */
     if(!kstrcmp(cmd,"carepkg")){
         if(argc<2){ carepkg_run("list",""); return; }
         carepkg_run(argv[1],argc>=3?argv[2]:""); return;
     }
 
     /* â”€â”€ Shell misc â”€â”€ */
     if(!kstrcmp(cmd,"history")){
         u32 start=(hist_count>HIST_SIZE)?hist_count-HIST_SIZE:0;
         for(u32 i=start;i<hist_count;i++){
             char n[8]; kutoa(i+1,n,10); terminal_write(n); terminal_write("  ");
             terminal_write(history[i%HIST_SIZE]); terminal_write("\n");
         }
         return;
     }
     if(!kstrcmp(cmd,"clear")){ terminal_clear(); return; }
     if(!kstrcmp(cmd,"alias")){ terminal_write("alias: no aliases defined\n"); return; }
     if(!kstrcmp(cmd,"env")){
         terminal_write("PATH=/bin:/usr/bin:/sbin\nUSER="); terminal_write(user_current_name());
         char home[64]; current_home_path(home,sizeof(home));
         terminal_write("\nHOME="); terminal_write(home);
         terminal_write("\nSHELL=/bin/sh\nTERM=careos-terminal\nOS=CareOS\n"); return;
     }
     if(!kstrcmp(cmd,"exit")||!kstrcmp(cmd,"logout")){
         terminal_write("Goodbye.\n"); user_logout(); return;
     }
     if(!kstrcmp(cmd,"reboot")){ terminal_write("Rebooting...\n"); timer_wait(500); outb(0x64,0xFE); return; }
     if(!kstrcmp(cmd,"halt")||!kstrcmp(cmd,"poweroff")){
         terminal_write("Halting...\n"); timer_wait(500); __asm__ volatile("cli;hlt"); return;
     }
     if(!kstrcmp(cmd,"df")){return;} /* handled above */
     if(!kstrcmp(cmd,"free")){return;}
     if(!kstrcmp(cmd,"help")||!kstrcmp(cmd,"?")){
         terminal_write_colored("CareOS v6 Shell â€” Commands:\n",VGA_ENTRY_COLOR(VGA_LIGHT_CYAN,VGA_BLACK));
         terminal_write("  Files:   ls [-l]  cd  pwd  cat  mkdir  rm  cp  mv  touch  find  grep  wc\n");
         terminal_write("  System:  ps  kill  sleep  whoami  id  uname  uptime  date  free  df  sysinfo  dmesg\n");
         terminal_write("  Users:   passwd  su  useradd <name> <pass>  userdel\n");
         terminal_write("  Net:     ifconfig  wifi  ping  curl  wget  nslookup  netstat\n");
         terminal_write("  Pkgs:    carepkg [install|remove|update|search|list] <pkg>\n");
         terminal_write("  Config:  settings show | settings set <key> <value>\n");
         terminal_write("  Shell:   history  clear  env  alias  exit  reboot  halt\n");
         return;
     }
 
     if(!kstrcmp(cmd,"kill")){
         if(argc<2){terminal_write("kill: usage: kill <pid>\n");return;}
         u32 pid=(u32)katoi(argv[1]);
         signal_send(pid,SIGKILL);
         char msg[48]; ksprintf(msg,"kill: sent SIGKILL to %u\n",pid);
         terminal_write(msg); return;
     }
     if(!kstrcmp(cmd,"grep")){
         if(argc<2){terminal_write("grep: usage: grep <pattern> [file]\n");return;}
         const char *pattern=argv[1];
         u32 pl=(u32)kstrlen(pattern);
         const char *src=NULL;
         char fbuf[2048];
         if(g_pipe_stdin_active){
             src=g_pipe_stdin;
         } else if(argc>=3){
             fs_node_t *f=(argv[2][0]=='/')?vfs_resolve_path(argv[2]):vfs_find(cwd,argv[2]);
             if(f&&f->type==FS_FILE){
                 vfs_read(f,fbuf,sizeof(fbuf)-1); fbuf[sizeof(fbuf)-1]='\0'; src=fbuf;
             }
         }
         if(!src){terminal_write("grep: no input\n");return;}
         const char *pp=src;
         while(*pp){
             const char *ls=pp;
             while(*pp&&*pp!='\n') pp++;
             u32 ll=(u32)(pp-ls);
             char tmp[256]; if(ll>255)ll=255;
             kmemcpy(tmp,ls,ll); tmp[ll]='\0';
             bool found=false;
             for(u32 ci=0;ci+pl<=ll;ci++){
                 if(kstrncmp(tmp+ci,pattern,pl)==0){found=true;break;}
             }
             if(found){ shout(tmp); shout("\n"); }
             if(*pp=='\n') pp++;
         }
         return;
     }
     if(!kstrcmp(cmd,"wc")){
         const char *src=g_pipe_stdin_active?g_pipe_stdin:"";
         u32 lines=0,words=0,bytes=(u32)kstrlen(src);
         bool in_word=false;
         for(const char *q=src;*q;q++){
             if(*q=='\n') lines++;
             if(*q==' '||*q=='\n'||*q=='\t'){ in_word=false; }
             else { if(!in_word){words++;in_word=true;} }
         }
         char out[64]; ksprintf(out,"%u %u %u\n",lines,words,bytes);
         shout(out); return;
     }

     /* Not found */
     terminal_write_colored(cmd,VGA_ENTRY_COLOR(VGA_LIGHT_RED,VGA_BLACK));
     terminal_write(": command not found. Type 'help' for commands.\n");
 }
 
 /* â”€â”€ Tab completion â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
 static void do_tab_complete(void){
     if(!cwd||!line_len) return;
     char *prefix=line_buf;
     for(u32 i=0;i<line_len;i++) if(line_buf[i]==' ') prefix=line_buf+i+1;
     u32 plen=kstrlen(prefix);
     const char *match=NULL; u32 matches=0;
     for(u32 i=0;i<cwd->child_count;i++){
         if(kstrncmp(cwd->children[i]->name,prefix,plen)==0){ match=cwd->children[i]->name; matches++; }
     }
     if(matches==1&&match){
         u32 ml=kstrlen(match);
         for(u32 i=plen;i<ml&&line_len<MAX_LINE-2;i++){
             line_buf[line_len++]=match[i]; terminal_putchar(match[i]);
         }
         fs_node_t *n=vfs_find(cwd,match);
         if(n&&n->type==FS_DIR){ line_buf[line_len++]='/'; terminal_putchar('/'); }
         line_buf[line_len]='\0';
     } else if(matches>1){
         terminal_write("\n");
         for(u32 i=0;i<cwd->child_count;i++){
             if(kstrncmp(cwd->children[i]->name,prefix,plen)==0){
                 terminal_write(cwd->children[i]->name);
                 terminal_write(cwd->children[i]->type==FS_DIR?"/  ":"  ");
             }
         }
         terminal_write("\n"); print_prompt(); line_redraw();
     }
 }
 
 /* â”€â”€ Shell entry point â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
 void shell_init(void){
     char home[64]; current_home_path(home,sizeof(home));
     cwd=vfs_resolve_path(home);
     if(!cwd) cwd=vfs_root();
 }
 
 void shell_run(void){
     shell_init();
     /* Print MOTD */
     fs_node_t *motd=vfs_resolve_path("/etc/motd");
     if(motd&&motd->data){ terminal_write(motd->data); terminal_write("\n"); }
 
     while(1){
         print_prompt();
         line_len=0; line_buf[0]='\0';
 
         while(1){
             char c=keyboard_getchar();
             if(c==0x03){ /* Ctrl+C / SIGINT */
                 terminal_write("^C\n");
                 line_buf[0]='\0'; line_len=0;
                 break;
             }
             if(c=='\n'){ terminal_putchar('\n'); break; }
             else if(c=='\b'){
                 if(line_len>0){ terminal_putchar('\b'); line_len--; line_buf[line_len]='\0'; }
             } else if(c=='\t'){
                 do_tab_complete();
             } else if(c=='\x1B'){
                 char c2=keyboard_getchar();
                 if(c2=='['){
                     char c3=keyboard_getchar();
                     if(c3=='A'){ /* up */
                         if(hist_pos>0){ hist_pos--; line_clear();
                             kstrncpy(line_buf,history[hist_pos%HIST_SIZE],MAX_LINE-1);
                             line_len=kstrlen(line_buf); line_redraw(); }
                     } else if(c3=='B'){ /* down */
                         if(hist_pos<hist_count){ hist_pos++;
                             line_clear();
                             if(hist_pos<hist_count){
                                 kstrncpy(line_buf,history[hist_pos%HIST_SIZE],MAX_LINE-1);
                                 line_len=kstrlen(line_buf); line_redraw();
                             }
                         }
                     }
                 }
             } else if(c>=32&&c<127&&line_len<MAX_LINE-1){
                 line_buf[line_len++]=c; line_buf[line_len]='\0'; terminal_putchar(c);
             }
         }
         if(line_len){ hist_push(); exec_line(line_buf); }
     }
 }






