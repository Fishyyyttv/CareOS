/* CareOS v9 -- apps/app_terminal.c -- Terminal application */
#include "apps_common.h"
#include "term_layout.h"
#include "font.h"
#include "perf.h"

/* -- Terminal Themes & Customization --------------------------------------
 * Each theme carries its own foreground so text stays legible on its own
 * background (important for the Light theme). Colours are integer rgb()
 * literals from well-known palettes -- no floating point, no heap. */
typedef struct {
    const char *name;
    u32 bg;       /* terminal background            */
    u32 text;     /* primary output text            */
    u32 dim;      /* dimmed / secondary text         */
    u32 prompt;   /* prompt + accent colour          */
    u32 input_bg; /* input-bar background            */
    u32 cursor;   /* blinking cursor / selection     */
    u32 border;   /* divider between output & input  */
} term_theme_t;

static const term_theme_t g_term_themes[] = {
    {
        "CareOS Dark",         /* default -- CareOS house palette */
        rgb(0x14, 0x16, 0x1e), /* Deep slate background   */
        rgb(0xe4, 0xe7, 0xef), /* Off-white text          */
        rgb(0x7c, 0x83, 0x96), /* Muted slate dim         */
        rgb(0x5c, 0xc8, 0xf0), /* CareOS cyan accent      */
        rgb(0x1c, 0x1f, 0x2b), /* Input BG                */
        rgb(0x8b, 0xe0, 0xa4), /* Mint cursor             */
        rgb(0x2a, 0x2e, 0x3c)  /* Border                  */
    },
    {
        "Solarized Dark",      /* Ethan Schoonover Solarized */
        rgb(0x00, 0x2b, 0x36), /* Base03 background       */
        rgb(0x83, 0x94, 0x96), /* Base0 text              */
        rgb(0x58, 0x6e, 0x75), /* Base01 dim              */
        rgb(0x85, 0x99, 0x00), /* Green prompt            */
        rgb(0x07, 0x36, 0x42), /* Base02 input BG         */
        rgb(0x2a, 0xa1, 0x98), /* Cyan cursor             */
        rgb(0x58, 0x6e, 0x75)  /* Base01 border           */
    },
    {
        "Nord",                /* Arctic, north-bluish     */
        rgb(0x2e, 0x34, 0x40), /* Polar Night bg           */
        rgb(0xd8, 0xde, 0xe9), /* Snow Storm text          */
        rgb(0x61, 0x6e, 0x88), /* Comment dim              */
        rgb(0xa3, 0xbe, 0x8c), /* Aurora green prompt      */
        rgb(0x24, 0x29, 0x33), /* Input BG                 */
        rgb(0x88, 0xc0, 0xd0), /* Frost cyan cursor        */
        rgb(0x4c, 0x56, 0x6a)  /* Border                   */
    },
    {
        "Gruvbox Dark",        /* retro-groove             */
        rgb(0x28, 0x28, 0x28), /* dark0 background         */
        rgb(0xeb, 0xdb, 0xb2), /* light1 text              */
        rgb(0x92, 0x83, 0x74), /* gray dim                 */
        rgb(0xb8, 0xbb, 0x26), /* bright green prompt      */
        rgb(0x1d, 0x20, 0x21), /* dark0_hard input BG      */
        rgb(0xfe, 0x80, 0x19), /* bright orange cursor     */
        rgb(0x3c, 0x38, 0x36)  /* dark1 border             */
    },
    {
        "Light",               /* legible bright theme     */
        rgb(0xfa, 0xfa, 0xf7), /* near-white background    */
        rgb(0x21, 0x25, 0x2b), /* near-black text          */
        rgb(0x6b, 0x72, 0x80), /* slate dim                */
        rgb(0x1a, 0x73, 0xe8), /* blue accent prompt       */
        rgb(0xed, 0xee, 0xea), /* light-gray input BG      */
        rgb(0x1a, 0x73, 0xe8), /* blue cursor              */
        rgb(0xd4, 0xd7, 0xd2)  /* soft border              */
    },
    {
        "Monokai",             /* classic bright-on-dark   */
        rgb(0x27, 0x28, 0x22), /* Dark charcoal/olive bg   */
        rgb(0xf8, 0xf8, 0xf2), /* Text                     */
        rgb(0x75, 0x71, 0x5e), /* Comment dim              */
        rgb(0xa6, 0xe2, 0x2e), /* Green prompt             */
        rgb(0x1e, 0x1f, 0x1c), /* Input BG                 */
        rgb(0xf9, 0x26, 0x72), /* Pink cursor              */
        rgb(0x3e, 0x3d, 0x32)  /* Border                   */
    },
    {
        "Dracula",             /* the dark castle palette  */
        rgb(0x28, 0x2a, 0x36), /* Dark blue-gray bg        */
        rgb(0xf8, 0xf8, 0xf2), /* Bright text              */
        rgb(0x62, 0x72, 0xa4), /* Comment dim              */
        rgb(0x50, 0xfa, 0x7b), /* Green prompt             */
        rgb(0x21, 0x22, 0x2c), /* Input BG                 */
        rgb(0xff, 0x79, 0xc6), /* Pink cursor              */
        rgb(0x44, 0x47, 0x5a)  /* Border                   */
    }
};

#define TERM_THEME_COUNT (sizeof(g_term_themes) / sizeof(g_term_themes[0]))

static u32 g_term_theme_idx = 0; /* Default: CareOS Dark */
static u8  g_term_opacity   = 217; /* Default ~85% opacity (217/255) */

static struct fs_node *term_cwd=NULL;

/* -- Scrollback, wrapping and scrolling state (per Terminal window) --------
 * Output is capped at TERM_SCROLLBACK_MAX bytes (oldest lines dropped). The
 * buffer is wrapped into visual lines for the current width; that layout is
 * cached and rebuilt only when the text, width, font or scale changes. */
#define TERM_SCROLLBACK_MAX  (256u * 1024u)
#define TERM_SCROLLBACK_KEEP (192u * 1024u)
#define TERM_FONT            FONT_BODY

typedef struct {
    term_vline_t *lines;
    u32  cap, n;                   /* allocated / used visual lines */
    u32  key_len, key_font, key_scale;
    i32  key_width;
    bool follow;                   /* stick to the newest output */
} term_state_t;

typedef struct { i32 lh, input_h, text_h, text_x, text_w; u32 vis; } term_metrics_t;

static term_state_t *term_st(window_t *w) {
    if (!w->term_state) {
        term_state_t *st = (term_state_t *)kmalloc(sizeof(term_state_t));
        if (!st) return NULL;
        kmemset(st, 0, sizeof *st);
        st->follow = true;
        st->key_len = 0xFFFFFFFFu;
        w->term_state = st;
    }
    return (term_state_t *)w->term_state;
}

static void term_invalidate(window_t *w) {
    term_state_t *st = term_st(w);
    if (st) st->key_len = 0xFFFFFFFFu;
}

/* All Terminal output goes through here: append, then cap the scrollback. */
static void term_out(window_t *w, const char *s) {
    win_append(w, s);
    if (w->text_len > TERM_SCROLLBACK_MAX) {
        w->text_len = term_trim(w->text_buf, w->text_len, TERM_SCROLLBACK_MAX, TERM_SCROLLBACK_KEEP);
        term_invalidate(w);            /* content shifted; same length can hide it */
    }
}

/* The ONE place Terminal geometry is computed (draw, Enter, wheel and
 * PgUp/PgDn used to disagree, which hid the newest lines after Enter). */
static term_metrics_t term_metrics(window_t *w) {
    rect_t cr = wm_client_rect(w);
    term_metrics_t m;
    i32 sc = (i32)GFX_FONT_SCALE;
    m.lh      = FONT_H * sc + 3;
    m.input_h = m.lh + 16;
    m.text_h  = cr.h - m.input_h - 6;
    m.text_x  = cr.x + 12;
    m.text_w  = cr.w - 12 - 14;          /* room for the scrollbar */
    if (m.text_h < m.lh) m.text_h = m.lh;
    m.vis = (u32)(m.text_h / m.lh);
    if (m.vis == 0) m.vis = 1;
    return m;
}

static i32 term_char_w(char c) { return gfx_str_width_n(&c, 1, TERM_FONT); }

/* Re-wrap only when the text, width, font or scale changed. */
static void term_relayout(window_t *w, term_state_t *st, i32 width) {
    u32 font = font_active_index(), scale = GFX_FONT_SCALE;
    if (st->lines && st->key_len == w->text_len && st->key_width == width &&
        st->key_font == font && st->key_scale == scale) return;
    u32 need = term_wrap(w->text_buf, w->text_len, width, term_char_w, st->lines, st->cap);
    if (need > st->cap) {
        u32 cap = need + need / 2 + 64;
        term_vline_t *nl = (term_vline_t *)kmalloc(cap * sizeof(term_vline_t));
        if (nl) {
            if (st->lines) kfree(st->lines);
            st->lines = nl; st->cap = cap;
            need = term_wrap(w->text_buf, w->text_len, width, term_char_w, st->lines, cap);
        }
    }
    st->n = need < st->cap ? need : st->cap;
    st->key_len = w->text_len; st->key_width = width; st->key_font = font; st->key_scale = scale;
}

/* Scroll by delta visual lines (positive = back in history, like the wheel). */
void app_terminal_scroll(window_t *w, i32 delta) {
    term_state_t *st = term_st(w);
    if (!st) return;
    term_metrics_t m = term_metrics(w);
    u32 max_scroll = st->n > m.vis ? st->n - m.vis : 0;
    i32 ns = (i32)w->scroll - delta;
    if (ns < 0) ns = 0;
    if ((u32)ns >= max_scroll) { ns = (i32)max_scroll; st->follow = true; }
    else st->follow = false;
    w->scroll = (u32)ns;
}

void app_terminal_close(window_t *w) {
    term_state_t *st = (term_state_t *)w->term_state;
    if (!st) return;
    if (st->lines) kfree(st->lines);
    kfree(st);
    w->term_state = NULL;
}

/* Command history: 32-entry ring shared by all terminal windows. s_hist_idx
 * is the browse position while pressing Up/Down; == s_hist_count means "not
 * browsing" (the live line). s_hist_draft keeps the half-typed line so Down
 * past the newest entry gives it back. */
#define TERM_HIST 32
static char s_term_hist[TERM_HIST][256];
static u32  s_hist_count = 0;   /* total ever pushed (ring index = n % TERM_HIST) */
static u32  s_hist_idx   = 0;
static char s_hist_draft[256];

static void term_hist_push(const char *line){
    if(!line[0]) return;
    /* Skip immediate duplicates, like bash's ignoredups. */
    if(s_hist_count && !kstrcmp(s_term_hist[(s_hist_count-1)%TERM_HIST],line)) return;
    kstrncpy(s_term_hist[s_hist_count%TERM_HIST],line,255);
    s_term_hist[s_hist_count%TERM_HIST][255]='\0';
    s_hist_count++;
}

static void term_set_input(window_t *w,const char *s){
    kstrncpy(w->input_buf,s,255); w->input_buf[255]='\0';
    w->input_len=kstrlen(w->input_buf);
    w->cursor_pos=w->input_len;
}

/* Insert c at the cursor, shifting the tail right. */
static void term_insert(window_t *w,char c){
    if(w->input_len>=255) return;
    if(w->cursor_pos>w->input_len) w->cursor_pos=w->input_len;
    for(u32 i=w->input_len+1;i>w->cursor_pos;i--) w->input_buf[i]=w->input_buf[i-1];
    w->input_buf[w->cursor_pos++]=c;
    w->input_len++;
}

/* Delete the char at index i, shifting the tail left. */
static void term_erase_at(window_t *w,u32 i){
    if(i>=w->input_len) return;
    for(u32 j=i;j<w->input_len;j++) w->input_buf[j]=w->input_buf[j+1];
    w->input_len--;
}

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
    term_out(w,"\n"); term_out(w,user_current_name());
    term_out(w,"@careos:"); term_out(w,p); term_out(w,"$ ");
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
            char cntbuf[4]; kutoa(TERM_THEME_COUNT, cntbuf, 10);
            term_out(w,"Terminal Theme Presets:\n");
            for(u32 i=0; i<TERM_THEME_COUNT; i++){
                char num[4]; kutoa(i+1, num, 10);
                term_out(w, " "); term_out(w, num); term_out(w, ". ");
                term_out(w, g_term_themes[i].name);
                if(i == g_term_theme_idx) term_out(w, "  [active]");
                term_out(w, "\n");
            }
            term_out(w,"usage: theme <name|1-"); term_out(w,cntbuf);
            term_out(w,">   (name = first word, e.g. careos, gruvbox, light)\n");
            term_out(w,"tip: Ctrl+T cycles themes\n");
            return;
        }
        const char *arg = argv[1];
        int chosen = -1;
        if(arg[0]>='0' && arg[0]<='9'){
            /* Numeric selection 1..TERM_THEME_COUNT */
            int num = katoi(arg);
            if(num>=1 && num<=(int)TERM_THEME_COUNT) chosen = num-1;
        } else {
            /* Case-insensitive match against the first word of each name,
             * so "solarized" selects "Solarized Dark", etc. */
            for(u32 i=0; i<TERM_THEME_COUNT; i++){
                const char *nm = g_term_themes[i].name;
                u32 k = 0; bool match = true;
                while(arg[k] && nm[k] && nm[k]!=' '){
                    char a = arg[k]; char b = nm[k];
                    if(a>='A' && a<='Z') a += 32;
                    if(b>='A' && b<='Z') b += 32;
                    if(a != b){ match = false; break; }
                    k++;
                }
                if(match && arg[k]=='\0' && (nm[k]=='\0' || nm[k]==' ')){
                    chosen = (int)i;
                    break;
                }
            }
        }
        if(chosen >= 0 && (u32)chosen < TERM_THEME_COUNT){
            g_term_theme_idx = (u32)chosen;
            term_out(w, "Terminal theme set to ");
            term_out(w, g_term_themes[g_term_theme_idx].name);
            term_out(w, "\n");
        } else {
            term_out(w, "theme: unknown preset. Use 'theme list' to view options.\n");
        }
    }
    else if(!kstrcmp(cmd,"opacity") || !kstrcmp(cmd,"transparency")){
        if(argc<2){
            char valbuf[16];
            u32 pct = (g_term_opacity * 100) / 255;
            kutoa(pct, valbuf, 10);
            term_out(w, "Current terminal opacity: ");
            term_out(w, valbuf);
            term_out(w, "% (alpha ");
            kutoa(g_term_opacity, valbuf, 10);
            term_out(w, valbuf);
            term_out(w, "/255)\nusage: opacity <0-100>\n");
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
        term_out(w, "Terminal opacity set to ");
        term_out(w, valbuf);
        term_out(w, "%\n");
    }
    else if(!kstrcmp(cmd,"ls")){
        struct fs_node *d=(argc>=2&&argv[1][0]!='/')?vfs_find(term_cwd,argv[1]):(argc>=2?vfs_resolve_path(argv[1]):term_cwd);
        if(!d){term_out(w,"ls: not found\n");return;}
        for(u32 i=0;i<d->child_count;i++){
            term_out(w,d->children[i]->name);
            term_out(w,d->children[i]->type==FS_DIR?"/  ":"  ");
        }
        term_out(w,"\n");
    }
    else if(!kstrcmp(cmd,"cd")){
        const char *dest=(argc<2)?"~":argv[1];
        struct fs_node *nd=NULL;
        if(kstrcmp(dest,"~")==0) nd=term_default_home();
        else if(kstrcmp(dest,"..")==0) nd=term_cwd->parent?term_cwd->parent:term_cwd;
        else nd=dest[0]=='/'?vfs_resolve_path(dest):vfs_find(term_cwd,dest);
        if(nd&&nd->type==FS_DIR) term_cwd=nd;
        else term_out(w,"cd: not found\n");
    }
    else if(!kstrcmp(cmd,"pwd")){char pp[64];term_path(term_cwd,pp);term_out(w,pp);term_out(w,"\n");}
    else if(!kstrcmp(cmd,"cat")){
        if(argc<2){term_out(w,"usage: cat <file>\n");return;}
        struct fs_node *f=argv[1][0]=='/'?vfs_resolve_path(argv[1]):vfs_find(term_cwd,argv[1]);
        if(!f){term_out(w,"cat: not found\n");return;}
        if(f->type==FS_DIR){term_out(w,"cat: is a directory\n");return;}
        const char *txt=vfs_file_str(f);
        term_out(w,txt);
        if(f->size&&txt[f->size-1]!='\n') term_out(w,"\n");
    }
    else if(!kstrcmp(cmd,"mkdir")){if(argc<2)return;struct fs_node *d=vfs_mkdir(term_cwd,argv[1]);if(!d)term_out(w,"mkdir: failed\n");}
    else if(!kstrcmp(cmd,"rm")){
        if(argc<2)return;
        struct fs_node *f=argv[1][0]=='/'?vfs_resolve_path(argv[1]):vfs_find(term_cwd,argv[1]);
        if(!f){term_out(w,"rm: not found\n");return;}
        if(vfs_delete(f)!=0) term_out(w,"rm: failed\n");
    }
    else if(!kstrcmp(cmd,"touch")){
        if(argc<2)return; struct fs_node *f=vfs_mkfile(term_cwd,argv[1]); if(!f) term_out(w,"touch: failed\n");
    }
    else if(!kstrcmp(cmd,"echo")){
        for(int i=1;i<argc;i++){term_out(w,argv[i]);if(i<argc-1)term_out(w," ");}term_out(w,"\n");
    }
    else if(!kstrcmp(cmd,"ps")){
        term_out(w,"  PID  STATE    NAME\n");
        for(u32 i=0;i<MAX_TASKS;i++){
            task_t *t=task_get(i+1); if(!t||t->state==TASK_DEAD) continue;
            char line[48]; char n[8]; kstrcpy(line,"  "); kutoa(t->id,n,10); kstrcat(line,n);
            while(kstrlen(line)<7) kstrcat(line," ");
            const char *st[]={"UNUSED","READY","RUN","BLOCK","SLEEP","ZOMBIE","DEAD"};
            kstrcat(line,t->state<=TASK_DEAD?st[t->state]:"?");
            while(kstrlen(line)<16) kstrcat(line," ");
            kstrcat(line,t->name); kstrcat(line,"\n");
            term_out(w,line);
        }
    }
    else if(!kstrcmp(cmd,"carepkg")){ 
        if(argc<2){term_out(w,"usage: carepkg <cmd> [pkg]\n");return;}
        carepkg_run(argv[1],argc>=3?argv[2]:"");
    }
    else if(!kstrcmp(cmd,"whoami")){ term_out(w,user_current_name()); term_out(w,"\n"); }
    else if(!kstrcmp(cmd,"ifconfig")){
        if(net_is_up()){
            u32 ip=net_get_ip(); char b[4][5];
            kutoa((ip>>24)&0xff,b[0],10); kutoa((ip>>16)&0xff,b[1],10);
            kutoa((ip>>8)&0xff,b[2],10); kutoa(ip&0xff,b[3],10);
            term_out(w,"eth0: "); term_out(w,b[0]); term_out(w,".");
            term_out(w,b[1]); term_out(w,"."); term_out(w,b[2]);
            term_out(w,"."); term_out(w,b[3]); term_out(w,"\n");
        } else term_out(w,"eth0: down\nlo: 127.0.0.1\n");
    }
    else if(!kstrcmp(cmd,"uname"))  term_out(w,"CareOS v9.0 x86 2026\n");
    else if(!kstrcmp(cmd,"date")){
        rtc_time_t t; rtc_read(&t); char b[8];
        kutoa(t.year,b,10); term_out(w,b); term_out(w,"-");
        kutoa(t.month,b,10); if(t.month<10) term_out(w,"0"); term_out(w,b); term_out(w,"-");
        kutoa(t.day,b,10); if(t.day<10) term_out(w,"0"); term_out(w,b); term_out(w," ");
        kutoa(t.hour,b,10); if(t.hour<10) term_out(w,"0"); term_out(w,b); term_out(w,":");
        kutoa(t.minute,b,10); if(t.minute<10) term_out(w,"0"); term_out(w,b); term_out(w,"\n");
    }
    else if(!kstrcmp(cmd,"free")){
        char b[12]; term_out(w,"     total    used    free\nMem: ");
        kutoa((u32)(kheap_size()/1024),b,10); term_out(w,b); term_out(w,"K  ");
        kutoa(kmem_used()/1024,b,10); term_out(w,b); term_out(w,"K  ");
        kutoa(kmem_free_bytes()/1024,b,10); term_out(w,b); term_out(w,"K\n");
        term_out(w,"Peak: "); kutoa(kmem_peak()/1024,b,10); term_out(w,b);
        term_out(w,"K  largest free block: ");
        kutoa(kmem_largest_free()/1024,b,10); term_out(w,b); term_out(w,"K\n");
    }
    else if(!kstrcmp(cmd,"res")){
        char b[512]; res_status_text(b,sizeof(b)); term_out(w,b);
    }
    else if(!kstrcmp(cmd,"uptime")){
        u32 sec = timer_ms() / 1000;
        char b[12];
        term_out(w,"Uptime: "); kutoa(sec,b,10); term_out(w,b); term_out(w," seconds\n");
    }
    else if(!kstrcmp(cmd,"sysinfo")){
        if(argc>1 && !kstrcmp(argv[1],"--version")){
            term_out(w,"CareOS v9.0 x86 2026\n");
        } else if(argc>1 && !kstrcmp(argv[1],"--cpu")){
            term_out(w,"CPU: Generic x86\n");
        } else if(argc>1 && !kstrcmp(argv[1],"--mem")){
            char b[12]; term_out(w,"Memory Free: ");
            kutoa(kmem_free_bytes()/1024,b,10); term_out(w,b); term_out(w,"K\n");
        } else {
            term_out(w,"usage: sysinfo [--version|--cpu|--mem]\n");
        }
    }
    else if(!kstrcmp(cmd,"network")){
        if(argc>1 && !kstrcmp(argv[1],"--status")){
            term_out(w,net_is_up()?"Network is UP\n":"Network is DOWN\n");
        } else if(argc>1 && !kstrcmp(argv[1],"--ip")){
            if(net_is_up()){
                char ip[24]; term_ip_to_str(net_get_ip(),ip);
                term_out(w,"IP: "); term_out(w,ip); term_out(w,"\n");
            } else {
                term_out(w,"Not connected.\n");
            }
        } else {
            term_out(w,"usage: network [--status|--ip]\n");
        }
    }
    else if(!kstrcmp(cmd,"dmesg")){
        if(argc>1 && !kstrcmp(argv[1],"clear")){
            serial_log_clear();
            term_out(w,"dmesg: cleared\n");
            return;
        }
        char logbuf[1024];
        serial_log_tail(logbuf,sizeof(logbuf));
        if(logbuf[0]){ term_out(w,logbuf); if(logbuf[kstrlen(logbuf)-1] != '\n') term_out(w,"\n"); }
        else term_out(w,"(no logs)\n");
    }
    else if(!kstrcmp(cmd,"settings")){
        const careos_settings_t *cfg=settings_get();
        if(argc<2 || !kstrcmp(argv[1],"show")){
            char n[12];
            term_out(w,"theme="); kutoa(cfg->theme,n,10); term_out(w,n);
            term_out(w," mouse="); kutoa(cfg->mouse_sensitivity,n,10); term_out(w,n); term_out(w,"%\n");
            term_out(w,"boot_fast="); term_out(w,cfg->boot_fast?"1":"0");
            term_out(w," clock24="); term_out(w,cfg->clock_24h?"1":"0");
            term_out(w," wallpaper="); kutoa(cfg->wallpaper,n,10); term_out(w,n); term_out(w,"\n");
            return;
        }
        if(!kstrcmp(argv[1],"set")){
            if(argc<4){ term_out(w,"usage: settings set <theme|mouse|boot_fast|clock24|wallpaper|dns> <value>\n"); return; }
            if(!kstrcmp(argv[2],"theme")) settings_set_theme((u32)katoi(argv[3]));
            else if(!kstrcmp(argv[2],"mouse")) settings_set_mouse_sensitivity((u32)katoi(argv[3]));
            else if(!kstrcmp(argv[2],"boot_fast")) settings_set_boot_fast(!kstrcmp(argv[3],"1")||!kstrcmp(argv[3],"on")||!kstrcmp(argv[3],"true"));
            else if(!kstrcmp(argv[2],"clock24")) settings_set_clock_24h(!kstrcmp(argv[3],"1")||!kstrcmp(argv[3],"on")||!kstrcmp(argv[3],"true"));
            else if(!kstrcmp(argv[2],"wallpaper")) settings_set_wallpaper((u32)katoi(argv[3]));
            else if(!kstrcmp(argv[2],"dns")){
                u32 dns=0;
                if(term_parse_ipv4(argv[3],&dns)!=0){ term_out(w,"settings: invalid IPv4\n"); return; }
                net_set_dns_server(dns);
            } else { term_out(w,"settings: unknown key\n"); return; }
            term_out(w,"settings saved\n");
            return;
        }
        term_out(w,"usage: settings [show|set]\n");
    }
    else if(!kstrcmp(cmd,"wifi")){
        if(argc<2){ term_out(w,"usage: wifi <scan|status|connect|disconnect>\n"); return; }
        if(!kstrcmp(argv[1],"scan")){
            term_out(w,"CareHome-5G  WPA2  92%\nOfficeMesh  WPA2  81%\nCoffeeShopGuest  Open 58%\n");
            return;
        }
        if(!kstrcmp(argv[1],"status")){
            const careos_settings_t *cfg=settings_get();
            term_out(w,"wifi: "); term_out(w,cfg->wifi_connected?"connected":"disconnected");
            if(cfg->wifi_ssid[0]){ term_out(w," ssid="); term_out(w,cfg->wifi_ssid); }
            term_out(w,"\n");
            if(net_is_up()){
                char ip[24]; term_ip_to_str(net_get_ip(),ip);
                term_out(w,"ip: "); term_out(w,ip); term_out(w,"\n");
            }
            return;
        }
        if(!kstrcmp(argv[1],"connect")){
            if(argc<3){ term_out(w,"usage: wifi connect <ssid> [password]\n"); return; }
            const char *pw=(argc>=4)?argv[3]:"";
            settings_set_wifi_profile(argv[2],pw,true);
            if(net_dhcp_renew()==0){ term_out(w,"wifi connected\n"); }
            else term_out(w,"wifi profile saved; no link now\n");
            return;
        }
        if(!kstrcmp(argv[1],"disconnect")){
            settings_set_wifi_profile("","",false);
            net_set_ip((127u<<24)|(0u<<16)|(0u<<8)|1u,(255u<<24)|(0u<<16)|(0u<<8)|0u,0);
            net_set_dns_server(0);
            term_out(w,"wifi disconnected\n");
            return;
        }
        term_out(w,"wifi: unknown action\n");
    }
    else if(!kstrcmp(cmd,"care")){
        if(argc<2){term_out(w,"usage: care <file.cl>\n");return;}
        struct fs_node *f=argv[1][0]=='/'?vfs_resolve_path(argv[1]):vfs_find(term_cwd,argv[1]);
        if(!f||f->type!=FS_FILE){term_out(w,"care: file not found\n");return;}
        char outbuf[2048];
        if(care_lang_exec_buf(vfs_file_str(f),f->size,outbuf,sizeof(outbuf))!=0)
            term_out(w,"care: script error\n");
        else if(outbuf[0]) term_out(w,outbuf);
    }
    else if(!kstrcmp(cmd,"help")){
        term_out(w,"Available commands:\n");
        term_out(w,"- ls, cd, pwd, cat, mkdir, rm, touch\n");
        term_out(w,"- echo, ps, whoami, clear, help\n");
        term_out(w,"- theme [list|<name>|<n>]  color theme (Ctrl+T cycles)\n");
        term_out(w,"- opacity <0-100>  set window transparency\n");
        term_out(w,"- ifconfig, ping, curl, wifi\n");
        term_out(w,"- uname, date, free, uptime\n");
        term_out(w,"- carepkg, dmesg, settings\n");
        term_out(w,"- sysinfo (--version, --cpu, --mem)\n");
        term_out(w,"- res   icon theme, wallpaper and image cache status\n");
        term_out(w,"- network (--status, --ip)\n");
        term_out(w,"- care <file.cl>  run a Care language script\n");
    }
    else if(!kstrcmp(cmd,"ping")){
        if(argc<2){term_out(w,"usage: ping <host>\n");return;}
        term_out(w,"PING "); term_out(w,argv[1]); term_out(w,"\n");
        for(int i=0;i<3;i++){
            icmp_ping(net_get_ip(),i); timer_wait(200);
            term_out(w,"64 bytes: icmp_seq="); char n[4]; kutoa((u32)i,n,10);
            term_out(w,n); term_out(w," time=~1ms\n");
        }
    }
    else if(!kstrcmp(cmd,"curl")){
        if(argc<2){term_out(w,"usage: curl <url>\n");return;}
        term_out(w,"Fetching...\n");
        char resp[1024]; int n=http_get("example.com",80,"/",resp,sizeof(resp));
        if(n>0){term_out(w,resp);}else term_out(w,"Failed.\n");
    }
    else if(!kstrcmp(cmd,"perf")){
        if(argc >= 2 && !kstrcmp(argv[1], "on")) {
            perf_hud_set(true);
            term_out(w, "perf HUD on (Ctrl+Alt+P toggles)\n");
        } else if(argc >= 2 && !kstrcmp(argv[1], "off")) {
            perf_hud_set(false);
            term_out(w, "perf HUD off\n");
        } else if(argc >= 2 && !kstrcmp(argv[1], "bench")) {
            term_out(w, "running 10 s benchmark... result in notification and on serial\n");
            perf_bench_start(argc >= 3 ? argv[2] : NULL);
        } else {
            term_out(w, "usage: perf on|off|bench [url]\n");
        }
    }
    else {
        term_out(w,cmd); term_out(w,": command not found\n");
    }
}

void app_terminal_init(window_t *w){
    win_clear(w); term_cwd=term_default_home();
    term_invalidate(w);
    w->cursor_pos=0; s_hist_idx=s_hist_count;
    term_out(w,"CareOS v9 Terminal\nType 'help' for commands.\n");
    term_out(w,"Tip: 'theme list' or Ctrl+T to change color theme.\n");
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
    term_metrics_t m = term_metrics(w);
    i32 input_h = m.input_h;
    i32 text_h  = cr.h - input_h;

    /* ---- Scrollable, wrapped output ---- */
    term_state_t *st = term_st(w);
    if (st) {
        term_relayout(w, st, m.text_w);
        u32 total = st->n ? st->n : 1;
        u32 max_scroll = total > m.vis ? total - m.vis : 0;
        if (st->follow || w->scroll > max_scroll) w->scroll = max_scroll;
        if (w->scroll == max_scroll) st->follow = true;

        /* Prompt lines start "<user>@careos:" -- colour them for any user. */
        char pfx[64]; kstrncpy(pfx, user_current_name(), 40); pfx[40] = '\0';
        kstrcat(pfx, "@careos:");
        u32 pfx_len = (u32)kstrlen(pfx);

        gfx_set_clip(cr.x, cr.y, cr.w, text_h);
        i32 y = cr.y + 6;
        for (u32 li = w->scroll; li < st->n && li < w->scroll + m.vis; li++) {
            const term_vline_t *vl = &st->lines[li];
            char tmp[1024];
            u32 len = vl->len < sizeof(tmp) - 1 ? vl->len : (u32)sizeof(tmp) - 1;
            kmemcpy(tmp, w->text_buf + vl->off, len); tmp[len] = '\0';
            bool line_start = (vl->off == 0) || (w->text_buf[vl->off - 1] == '\n');
            u32 col = cur_theme->text;
            if (line_start && ((len >= pfx_len && !kstrncmp(tmp, pfx, pfx_len)) ||
                               !kstrncmp(tmp, "CareOS", 6) || tmp[0] == '$'))
                col = cur_theme->prompt;
            gfx_str(m.text_x, y, tmp, col, COL_TRANSPARENT);
            y += m.lh;
        }
        gfx_clear_clip();

        /* Scrollbar, only when there is something to scroll. */
        if (total > m.vis) {
            i32 sb_h = m.text_h;
            i32 th = sb_h * (i32)m.vis / (i32)total; if (th < 20) th = 20;
            i32 ty2 = cr.y + 6 + (i32)((i64)(sb_h - th) * (i64)w->scroll /
                                       (i64)(max_scroll ? max_scroll : 1));
            gfx_rect_rounded(cr.x + cr.w - 9, ty2, 4, th, 2, cur_theme->dim);
        }
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

    /* Theme indicator: active theme name + accent swatch on the right of the
     * input bar (Ctrl+T and the 'theme' command change it). */
    i32 sw   = FONT_H * sc;
    i32 sw_x = cr.x + cr.w - 12 - sw;
    i32 sw_y = iy + (input_h - sw) / 2;
    i32 name_w = gfx_str_width(cur_theme->name);
    i32 name_x = sw_x - 8 - name_w;
    i32 input_max_w = cr.w - 28;
    if(name_x > input_x + 120){ /* only when the bar is wide enough */
        gfx_str(name_x, ty, cur_theme->name, cur_theme->dim, COL_TRANSPARENT);
        gfx_rect_rounded(sw_x, sw_y, sw, sw, 3, cur_theme->prompt);
        gfx_rect_rounded_outline(sw_x, sw_y, sw, sw, 3, cur_theme->border);
        input_max_w = (name_x - 8) - input_x;
    }
    if(input_max_w < 20) input_max_w = 20;

    /* Keep the cursor visible: scroll the input horizontally when it is
     * longer than the bar. */
    u32 cp = w->cursor_pos > w->input_len ? w->input_len : w->cursor_pos;
    u32 first = 0;
    while (first < cp && gfx_str_width_n(w->input_buf + first, cp - first, TERM_FONT) > input_max_w - 8)
        first++;
    gfx_set_clip(input_x, iy + 1, input_max_w, input_h - 1);
    gfx_str(input_x, ty, w->input_buf + first, cur_theme->text, COL_TRANSPARENT);
    gfx_clear_clip();

    /* Animated blinking cursor */
    u32 phase = (timer_ms() / 10) % 50; /* 500ms cycle */
    u8 cursor_alpha;
    if (phase < 25) {
        cursor_alpha = (u8)(50 + (phase * 205) / 25);
    } else {
        cursor_alpha = (u8)(50 + ((50 - phase) * 205) / 25);
    }
    i32 cx = input_x + gfx_str_width_n(w->input_buf + first, cp - first, TERM_FONT);
    gfx_rect_blend(cx, iy + 4, 2 * sc, input_h - 8, cur_theme->cursor, cursor_alpha);
}
void app_terminal_key(window_t *w,char c){
    if(w->cursor_pos>w->input_len) w->cursor_pos=w->input_len;
    u8 k=(u8)c;
    if(k==KEY_NAV_UP){
        if(s_hist_count==0) return;
        u32 oldest=(s_hist_count>TERM_HIST)?s_hist_count-TERM_HIST:0;
        if(s_hist_idx>s_hist_count) s_hist_idx=s_hist_count;
        if(s_hist_idx==s_hist_count){ kstrncpy(s_hist_draft,w->input_buf,255); s_hist_draft[255]='\0'; }
        if(s_hist_idx>oldest) s_hist_idx--;
        term_set_input(w,s_term_hist[s_hist_idx%TERM_HIST]);
        return;
    }
    if(k==KEY_NAV_DOWN){
        if(s_hist_idx>=s_hist_count) return;
        s_hist_idx++;
        term_set_input(w,(s_hist_idx==s_hist_count)?s_hist_draft:s_term_hist[s_hist_idx%TERM_HIST]);
        return;
    }
    if(k==KEY_NAV_LEFT){ if(w->cursor_pos>0) w->cursor_pos--; return; }
    if(k==KEY_NAV_RIGHT){ if(w->cursor_pos<w->input_len) w->cursor_pos++; return; }
    if(k==KEY_NAV_HOME){ w->cursor_pos=0; return; }
    if(k==KEY_NAV_END){ w->cursor_pos=w->input_len; return; }
    if(k==KEY_NAV_DELETE){ term_erase_at(w,w->cursor_pos); return; }
    if(k==KEY_NAV_PGUP || k==KEY_NAV_PGDN){
        term_metrics_t m = term_metrics(w);
        i32 step = (i32)m.vis - 1; if (step < 1) step = 1;
        app_terminal_scroll(w, k==KEY_NAV_PGUP ? step : -step);
        return;
    }

    if(c=='\n'){
        term_out(w,w->input_buf); term_out(w,"\n");
        term_hist_push(w->input_buf);
        s_hist_idx=s_hist_count; s_hist_draft[0]='\0';
        term_exec(w,w->input_buf);
        w->input_len=0; w->input_buf[0]='\0'; w->cursor_pos=0;
        term_prompt(w);
        /* Running a command jumps back to the newest output. */
        { term_state_t *st = term_st(w); if (st) st->follow = true; }
    } else if(c=='\b'){
        if(w->cursor_pos>0){ w->cursor_pos--; term_erase_at(w,w->cursor_pos); }
    } else if(c==0x0C){ /* Ctrl+L: Clear */
        win_clear(w);
        term_invalidate(w);
        { term_state_t *st = term_st(w); if (st) st->follow = true; }
        term_prompt(w);
    } else if(c==0x14){ /* Ctrl+T: cycle terminal color theme */
        g_term_theme_idx = (g_term_theme_idx + 1) % TERM_THEME_COUNT;
        notify_push("Terminal Theme", g_term_themes[g_term_theme_idx].name,
                    g_term_themes[g_term_theme_idx].prompt);
    } else if(c==0x03){ /* Ctrl+C: Copy */
        if (w->input_len > 0) {
            kstrncpy(g_clipboard, w->input_buf, CLIPBOARD_SIZE-1);
            g_clipboard_len = w->input_len;
        } else {
            /* The scrollback can be far larger than the clipboard: copy its
             * newest part, and never report more than was copied. */
            u32 n = w->text_len < CLIPBOARD_SIZE - 1 ? w->text_len : CLIPBOARD_SIZE - 1;
            kmemcpy(g_clipboard, w->text_buf + (w->text_len - n), n);
            g_clipboard[n] = '\0';
            g_clipboard_len = n;
        }
        notify_push("Clipboard", "Copied terminal text", COL_ACCENT);
    } else if(c==0x16){ /* Ctrl+V: Paste */
        for (u32 i=0; i<g_clipboard_len; i++) {
            char cc = g_clipboard[i];
            if (cc >= 32 && cc < 127) term_insert(w, cc);
        }
    } else if(c>=32&&c<127){
        term_insert(w,c);
    }
}
