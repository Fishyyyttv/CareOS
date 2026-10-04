/* CareOS v9 -- apps/app_terminal.c -- Terminal application
 *
 * v10 look (Phase 3 spec §1): Mono 14 body, the prompt drawn as pills, help
 * as a grid of cards built from the command table, a "did you mean" card,
 * a Tab-completion popover, a status bar, and a title-bar toolbar (cwd pill,
 * colour-scheme menu). Rich lines are stored in the scrollback as marked
 * lines (TERM_MARK, see term_layout.h) and drawn here; plain output stays
 * plain text, and output can never forge a marker (term_out sanitises). */
#include "apps_common.h"
#include "term_layout.h"
#include "term_cmds.h"
#include "font.h"
#include "perf.h"
#include "ui.h"

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
        "Care Dark",           /* default -- the v10 canvas palette */
        rgb(0x0d, 0x11, 0x20), /* Window body #0D1120     */
        rgb(0xee, 0xf1, 0xfa), /* Ink                     */
        rgb(0xb4, 0xbb, 0xd2), /* Ink 2                   */
        rgb(0xff, 0x7a, 0x59), /* Care coral              */
        rgb(0x1c, 0x24, 0x40), /* Raised                  */
        rgb(0x7c, 0xf0, 0xb5), /* Terminal green          */
        rgb(0x8c, 0x9b, 0xff)  /* Calm                    */
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
        "Care Light",          /* legible bright theme     */
        rgb(0xfa, 0xfa, 0xf7), /* near-white background    */
        rgb(0x21, 0x25, 0x2b), /* near-black text          */
        rgb(0x6b, 0x72, 0x80), /* slate dim                */
        rgb(0xe8, 0x5d, 0x3c), /* coral accent prompt      */
        rgb(0xed, 0xee, 0xea), /* light-gray input BG      */
        rgb(0x1f, 0xa5, 0x62), /* green cursor             */
        rgb(0x2c, 0x3f, 0xa8)  /* calm                     */
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
#define TERM_THEME_LIGHT 4u

static u32 g_term_theme_idx = 0;   /* Default: Care Dark */
static u8  g_term_opacity   = 235; /* Default 92% (spec §1) */

/* Colours the renderer derives from the active theme. Care Dark / Care Light
 * use the canvas values; the classic palettes get blends of their own. */
typedef struct {
    u32 bg, text, dim, muted, accent, cursor, card;
    u32 pill_u, pill_u_ink, pill_c, pill_c_ink;
    u32 status, line, pop, pop_sel;
    bool light;
} term_pal_t;

static term_pal_t term_pal(void) {
    const term_theme_t *t = &g_term_themes[g_term_theme_idx];
    term_pal_t p;
    p.light = g_term_theme_idx == TERM_THEME_LIGHT;
    p.bg = t->bg; p.text = t->text; p.dim = t->dim; p.accent = t->prompt; p.cursor = t->prompt;
    if (g_term_theme_idx == 0) {
        p.muted = rgb(0x9a, 0xa3, 0xbd); p.card = rgb(0x16, 0x1c, 0x33);
        p.pill_u = rgb(0xff, 0x7a, 0x59); p.pill_u_ink = rgb(0x2a, 0x12, 0x0a);
        p.pill_c = rgb(0x26, 0x30, 0x5a); p.pill_c_ink = rgb(0xb7, 0xc1, 0xff);
        p.status = rgb(0x11, 0x16, 0x29); p.line = rgb(0x1e, 0x23, 0x35);
        p.pop = rgb(0x1c, 0x24, 0x40); p.pop_sel = rgb(0x2a, 0x33, 0x58);
        return p;
    }
    if (p.light) {
        p.muted = rgb(0x6b, 0x72, 0x80); p.card = rgb(0xee, 0xf0, 0xf5);
        p.pill_u = rgb(0xff, 0x7a, 0x59); p.pill_u_ink = rgb(0x2a, 0x12, 0x0a);
        p.pill_c = rgb(0xdd, 0xe3, 0xff); p.pill_c_ink = rgb(0x2c, 0x3f, 0xa8);
        p.status = rgb(0xf0, 0xf1, 0xee); p.line = rgb(0xdc, 0xde, 0xd8);
        p.pop = rgb(0xff, 0xff, 0xff); p.pop_sel = rgb(0xe6, 0xea, 0xf7);
        return p;
    }
    p.muted = ui_mix(t->bg, t->text, 150);
    p.card = ui_mix(t->bg, t->text, 22);
    p.pill_u = t->prompt; p.pill_u_ink = t->bg;
    p.pill_c = ui_mix(t->bg, t->cursor, 70); p.pill_c_ink = t->text;
    p.status = ui_mix(t->bg, t->text, 10); p.line = ui_mix(t->bg, t->text, 28);
    p.pop = ui_mix(t->bg, t->text, 30); p.pop_sel = ui_mix(t->bg, t->text, 55);
    return p;
}

static u32 term_group_col(u32 g, bool light) {
    static const u32 dark[TG_COUNT] = {
        rgb(0x7c,0xf0,0xb5), rgb(0x6c,0xb8,0xff), rgb(0xff,0xc2,0x4b),
        rgb(0xc0,0x8b,0xff), rgb(0xff,0x8f,0xa8), rgb(0xff,0x9a,0x76) };
    static const u32 lite[TG_COUNT] = {
        rgb(0x1f,0x9e,0x63), rgb(0x1a,0x6f,0xd8), rgb(0xb0,0x74,0x12),
        rgb(0x7b,0x4f,0xd6), rgb(0xd0,0x40,0x68), rgb(0xe0,0x5a,0x38) };
    return g < TG_COUNT ? (light ? lite[g] : dark[g]) : 0;
}

static struct fs_node *term_cwd=NULL;

/* -- Scrollback, wrapping and scrolling state (per Terminal window) --------
 * Output is capped at TERM_SCROLLBACK_MAX bytes (oldest lines dropped). The
 * buffer is wrapped into visual lines for the current width; that layout is
 * cached and rebuilt only when the text, width, font or scale changes. */
#define TERM_SCROLLBACK_MAX  (256u * 1024u)
#define TERM_SCROLLBACK_KEEP (192u * 1024u)
#define TERM_SLOT            2u          /* Mono 14 */
#define TERM_COMP_MAX        8u
#define TERM_RUN_MAX         6u

typedef struct {
    term_vline_t *lines;
    u32  cap, n;                   /* allocated / used visual lines */
    u32  key_len, key_font, key_scale;
    i32  key_width;
    bool follow;                   /* stick to the newest output */
    /* v10 interaction state */
    bool menu_open;                /* colour-scheme menu */
    rect_t menu_btn;               /* its toolbar button, as last drawn */
    bool comp_open;                /* Tab-completion popover */
    u32  comp_n, comp_sel;
    term_comp_t comp[TERM_COMP_MAX];
    char comp_text[TERM_COMP_MAX][96];
    char sugg[256];                /* Enter on an empty line runs this */
    u32  run_n;                    /* "Run it" buttons as last drawn */
    rect_t run_r[TERM_RUN_MAX];
    u32  run_off[TERM_RUN_MAX];
    u32  scroll_ms;                /* last wheel / page scroll */
} term_state_t;

typedef struct { i32 lh, mono_lh, pad_x, pad_y, status_h, text_y, text_h, text_x, text_w; u32 vis; } term_metrics_t;

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

/* Append raw bytes (markers allowed), then cap the scrollback. */
static void term_raw(window_t *w, const char *s) {
    win_append(w, s);
    if (w->text_len > TERM_SCROLLBACK_MAX) {
        w->text_len = term_trim(w->text_buf, w->text_len, TERM_SCROLLBACK_MAX, TERM_SCROLLBACK_KEEP);
        term_invalidate(w);            /* content shifted; same length can hide it */
    }
}

/* All ordinary output goes through here: marker bytes in file contents or
 * logs become '?', so only the shell itself can start a rich line. */
static void term_out(window_t *w, const char *s) {
    char tmp[128]; u32 n = 0;
    for (; *s; s++) {
        char c = *s;
        if (c == TERM_MARK || c == TERM_SEP) c = '?';
        tmp[n++] = c;
        if (n == sizeof tmp - 1) { tmp[n] = '\0'; term_raw(w, tmp); n = 0; }
    }
    if (n) { tmp[n] = '\0'; term_raw(w, tmp); }
}

/* The ONE place Terminal geometry is computed (draw, Enter, wheel and
 * PgUp/PgDn used to disagree, which hid the newest lines after Enter). */
static term_metrics_t term_metrics(window_t *w) {
    rect_t cr = wm_client_rect(w);
    term_metrics_t m;
    i32 sc = (i32)GFX_FONT_SCALE;
    m.mono_lh  = gfx_role_line_h(FONT_ROLE_MONO, TERM_SLOT);
    m.lh       = (m.mono_lh * 4 + 2) / 3;             /* ~1.7 x 14 px */
    m.pad_x    = (cr.w >= 560 ? 26 : 14) * sc;
    m.pad_y    = (cr.h >= 360 ? 22 : 10) * sc;
    m.status_h = cr.h >= 220 ? 28 * sc : 0;
    m.text_x   = cr.x + m.pad_x;
    m.text_y   = cr.y + m.pad_y;
    m.text_w   = cr.w - m.pad_x * 2;
    m.text_h   = cr.h - m.pad_y - m.status_h - 6 * sc;
    if (m.text_w < 40) m.text_w = 40;
    if (m.text_h < m.lh) m.text_h = m.lh;
    m.vis = (u32)(m.text_h / m.lh);
    if (m.vis == 0) m.vis = 1;
    return m;
}

static i32 term_char_w(char c) { return gfx_str_role_width_n(&c, 1, FONT_ROLE_MONO, TERM_SLOT, FONT_W_REGULAR); }

/* -- Rich-line geometry ---------------------------------------------------- */
#define HELP_GAP 10
static i32 term_help_cols(i32 width) { return width >= 600 ? 3 : width >= 380 ? 2 : 1; }

/* Height of each card row of the help grid for this width; returns the
 * grid height. row_h gets one entry per row (at most TG_COUNT). */
static i32 term_help_layout(i32 width, i32 *card_w, i32 *row_h, i32 *rows) {
    i32 cols = term_help_cols(width);
    i32 cw = (width - (cols - 1) * HELP_GAP) / cols;
    i32 title_h = gfx_role_line_h(FONT_ROLE_UI, 1);
    i32 total = 0, nrows = (TG_COUNT + cols - 1) / cols;
    for (i32 r = 0; r < nrows; r++) {
        i32 mh = 0;
        for (i32 c = 0; c < cols; c++) {
            u32 g = (u32)(r * cols + c);
            if (g >= TG_COUNT) break;
            char line[160]; term_help_line(g, line, sizeof line);
            i32 h = 12 + title_h + 4 +
                    gfx_str_role_wrap(0, 0, cw - 28, line, COL_TRANSPARENT, FONT_ROLE_MONO, 1, FONT_W_REGULAR, 2) + 12;
            if (h > mh) mh = h;
        }
        row_h[r] = mh;
        total += mh + (r ? HELP_GAP : 0);
    }
    *card_w = cw; *rows = nrows;
    return total;
}

/* The card fills two rows (36 px minimum), with 4 px above and below. */
#define SUGG_ROWS 2
static u32 term_rows_cb(const char *line, u32 len, void *ctx) {
    const term_metrics_t *m = (const term_metrics_t *)ctx;
    if (len < 2) return 1;
    i32 h = m->lh;
    if (line[1] == 'H') {
        i32 cw, rh[TG_COUNT], nr;
        h = 6 + term_help_layout(m->text_w, &cw, rh, &nr) + 6;
    } else if (line[1] == 'D') {
        return SUGG_ROWS;
    }
    return (u32)((h + m->lh - 1) / m->lh);
}

/* Re-wrap only when the text, width, font or scale changed. */
static void term_relayout(window_t *w, term_state_t *st, const term_metrics_t *m) {
    u32 font = font_active_index(), scale = GFX_FONT_SCALE;
    i32 width = m->text_w;
    if (st->lines && st->key_len == w->text_len && st->key_width == width &&
        st->key_font == font && st->key_scale == scale) return;
    term_metrics_t mc = *m;
    u32 need = term_wrap_rich(w->text_buf, w->text_len, width, term_char_w, term_rows_cb, &mc, st->lines, st->cap);
    if (need > st->cap) {
        u32 cap = need + need / 2 + 64;
        term_vline_t *nl = (term_vline_t *)kmalloc(cap * sizeof(term_vline_t));
        if (nl) {
            if (st->lines) kfree(st->lines);
            st->lines = nl; st->cap = cap;
            need = term_wrap_rich(w->text_buf, w->text_len, width, term_char_w, term_rows_cb, &mc, st->lines, cap);
        }
    }
    st->n = need < st->cap ? need : st->cap;
    st->key_len = w->text_len; st->key_width = width; st->key_font = font; st->key_scale = scale;
}

/* Visual rows including the live prompt: it sits on the empty line after
 * the final '\n' (or on its own row when output ends mid-line). */
static u32 term_total_rows(window_t *w, const term_state_t *st) {
    u32 n = st->n ? st->n : 1;
    bool ends_nl = w->text_len == 0 || w->text_buf[w->text_len - 1] == '\n';
    return ends_nl ? n : n + 1;
}

/* Scroll by delta visual lines (positive = back in history, like the wheel). */
void app_terminal_scroll(window_t *w, i32 delta) {
    term_state_t *st = term_st(w);
    if (!st) return;
    term_metrics_t m = term_metrics(w);
    term_relayout(w, st, &m);
    u32 total = term_total_rows(w, st);
    u32 max_scroll = total > m.vis ? total - m.vis : 0;
    i32 ns = (i32)w->scroll - delta;
    if (ns < 0) ns = 0;
    if ((u32)ns >= max_scroll) { ns = (i32)max_scroll; st->follow = true; }
    else st->follow = false;
    w->scroll = (u32)ns;
    st->scroll_ms = timer_ms();
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

/* -- Rich lines ------------------------------------------------------------ */
/* "~" for the home folder, "~/x" below it, otherwise the absolute path. */
static void term_cwd_display(struct fs_node *n, char *out, u32 cap) {
    char p[64], home[64];
    term_path(n, p);
    app_home_path(home);
    u32 hl = (u32)kstrlen(home);
    if (!kstrncmp(p, home, hl) && (p[hl] == '\0' || p[hl] == '/')) {
        out[0] = '~'; out[1] = '\0';
        kstrncpy(out + 1, p + hl, cap - 2); out[cap - 1] = '\0';
    } else {
        kstrncpy(out, p, cap - 1); out[cap - 1] = '\0';
    }
}

/* The prompt as a rich line: user pill, cwd pill, then the command. */
static void term_emit_prompt(window_t *w, const char *cmdline) {
    char cwd[64]; term_cwd_display(term_cwd, cwd, sizeof cwd);
    char head[4] = { TERM_MARK, 'P', '\0', '\0' };
    char sep[2] = { TERM_SEP, '\0' };
    char safe[256]; u32 k = 0;
    for (; cmdline[k] && k < sizeof safe - 1; k++)
        safe[k] = (cmdline[k] == TERM_MARK || cmdline[k] == TERM_SEP) ? '?' : cmdline[k];
    safe[k] = '\0';
    term_raw(w, head); term_raw(w, user_current_name()); term_raw(w, sep);
    term_raw(w, cwd); term_raw(w, sep); term_raw(w, safe); term_raw(w, "\n");
}

/* "Did you mean" card. kind 'c' = command, 'f' = file. display is what the
 * card shows, command what Run it (or Enter on an empty line) executes. */
static void term_emit_suggest(window_t *w, char kind, const char *typed,
                              const char *display, const char *command) {
    char head[4] = { TERM_MARK, 'D', kind, '\0' };
    char sep[2] = { TERM_SEP, '\0' };
    term_raw(w, head); term_out(w, typed); term_raw(w, sep);
    term_out(w, display); term_raw(w, sep); term_out(w, command); term_raw(w, "\n");
    term_state_t *st = term_st(w);
    if (st) { kstrncpy(st->sugg, command, sizeof st->sugg - 1); st->sugg[sizeof st->sugg - 1] = '\0'; }
}

/* Depth-limited search for a file or folder called name under dir. */
static struct fs_node *term_find_named(struct fs_node *dir, const char *name, bool want_dir, u32 depth) {
    if (!dir || dir->type != FS_DIR || depth == 0) return NULL;
    char p[64]; vfs_get_path(dir, p, sizeof p);
    if (p[0]) vfs_resolve_path(p);                 /* loads ext2 children */
    for (u32 i = 0; i < dir->child_count; i++) {
        struct fs_node *c = dir->children[i];
        if (!kstrcmp(c->name, name) && ((c->type == FS_DIR) == want_dir)) return c;
    }
    for (u32 i = 0; i < dir->child_count; i++) {
        struct fs_node *c = dir->children[i];
        if (c->type != FS_DIR || c->name[0] == '.') continue;
        struct fs_node *f = term_find_named(c, name, want_dir, depth - 1);
        if (f) return f;
    }
    return NULL;
}

/* "cmd x" failed because x does not exist here: offer the same name found
 * under ~. Returns true when a card was shown. */
static bool term_suggest_path(window_t *w, const char *cmd, const char *arg, bool want_dir) {
    const char *base = arg;
    for (const char *q = arg; *q; q++) if (*q == '/' && q[1]) base = q + 1;
    struct fs_node *f = term_find_named(term_default_home(), base, want_dir, 5);
    if (!f) return false;
    char abs[64]; term_path(f, abs);
    if (!kstrcmp(abs, arg)) return false;
    char disp[96], run[128];
    term_cwd_display(f, disp, sizeof disp);
    kstrcpy(run, cmd); kstrcat(run, " "); kstrcat(run, abs);
    term_emit_suggest(w, 'f', arg, disp, run);
    return true;
}


/* "theme <x>": a number, the whole name without spaces ("caredark"), or its
 * first word ("solarized"), case-insensitively. -1 when nothing matches. */
static int term_theme_lookup(const char *arg) {
    if (arg[0] >= '0' && arg[0] <= '9') {
        int num = katoi(arg);
        return (num >= 1 && num <= (int)TERM_THEME_COUNT) ? num - 1 : -1;
    }
    for (int pass = 0; pass < 2; pass++) {
        for (u32 i = 0; i < TERM_THEME_COUNT; i++) {
            const char *nm = g_term_themes[i].name;
            u32 a = 0, b = 0; bool ok = true;
            while (arg[a] && nm[b]) {
                if (nm[b] == ' ') { if (pass == 1) break; b++; continue; }
                char x = arg[a], y = nm[b];
                if (x >= 'A' && x <= 'Z') x += 32;
                if (y >= 'A' && y <= 'Z') y += 32;
                if (x != y) { ok = false; break; }
                a++; b++;
            }
            if (!ok || arg[a]) continue;
            if (pass == 0 && nm[b] == '\0') return (int)i;
            if (pass == 1 && (nm[b] == '\0' || nm[b] == ' ')) return (int)i;
        }
    }
    return -1;
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
            term_out(w,">   (or a name: care, solarized, nord, gruvbox, carelight...)\n");
            term_out(w,"tip: Ctrl+T cycles themes\n");
            return;
        }
        int chosen = term_theme_lookup(argv[1]);
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
        else if(!term_suggest_path(w,"cd",dest,true)) term_out(w,"cd: not found\n");
    }
    else if(!kstrcmp(cmd,"pwd")){char pp[64];term_path(term_cwd,pp);term_out(w,pp);term_out(w,"\n");}
    else if(!kstrcmp(cmd,"cat")){
        if(argc<2){term_out(w,"usage: cat <file>\n");return;}
        struct fs_node *f=argv[1][0]=='/'?vfs_resolve_path(argv[1]):vfs_find(term_cwd,argv[1]);
        if(!f){ if(!term_suggest_path(w,"cat",argv[1],false)) term_out(w,"cat: not found\n"); return;}
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
            if(!net_trylock()){ term_out(w,"network busy (page loading) - try again\n"); return; }
            int rc=net_dhcp_renew(); net_unlock();
            if(rc==0){ term_out(w,"wifi connected\n"); }
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
        if(!f||f->type!=FS_FILE){ if(!term_suggest_path(w,"care",argv[1],false)) term_out(w,"care: file not found\n"); return;}
        char outbuf[2048];
        if(care_lang_exec_buf(vfs_file_str(f),f->size,outbuf,sizeof(outbuf))!=0)
            term_out(w,"care: script error\n");
        else if(outbuf[0]) term_out(w,outbuf);
    }
    else if(!kstrcmp(cmd,"help")){
        /* The card grid is drawn from the command table (term_cmds.c). */
        char head[3] = { TERM_MARK, 'H', '\0' };
        term_raw(w, head); term_raw(w, "\n");
        term_out(w,"More: theme list, opacity <0-100>, sysinfo --cpu, perf on|off.\n");
        term_out(w,"Tab completes commands and paths; Ctrl+T cycles themes.\n");
    }
    else if(!kstrcmp(cmd,"ping")){
        if(argc<2){term_out(w,"usage: ping <host>\n");return;}
        if(!net_trylock()){ term_out(w,"network busy (page loading) - try again\n"); return; }
        term_out(w,"PING "); term_out(w,argv[1]); term_out(w,"\n");
        for(int i=0;i<3;i++){
            icmp_ping(net_get_ip(),i); timer_wait(200);
            term_out(w,"64 bytes: icmp_seq="); char n[4]; kutoa((u32)i,n,10);
            term_out(w,n); term_out(w," time=~1ms\n");
        }
        net_unlock();
    }
    else if(!kstrcmp(cmd,"curl")){
        if(argc<2){term_out(w,"usage: curl <url>\n");return;}
        if(!net_trylock()){ term_out(w,"network busy (page loading) - try again\n"); return; }
        term_out(w,"Fetching...\n");
        char resp[1024]; int n=http_get("example.com",80,"/",resp,sizeof(resp));
        net_unlock();
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
        const char *s = term_suggest_cmd(cmd);
        if (s) {
            char run[256]; kstrcpy(run, s);
            for (int i = 1; i < argc; i++) {
                if (kstrlen(run) + kstrlen(argv[i]) + 2 >= sizeof run) break;
                kstrcat(run, " "); kstrcat(run, argv[i]);
            }
            term_emit_suggest(w, 'c', cmd, run, run);
        } else {
            term_out(w,cmd); term_out(w,": command not found\n");
        }
    }
}

/* -- Session --------------------------------------------------------------- */
void app_terminal_init(window_t *w){
    win_clear(w); term_cwd=term_default_home();
    term_invalidate(w);
    w->cursor_pos=0; s_hist_idx=s_hist_count;
    const char *nm = user_current_name();
    user_profile_t prof;
    if (user_profile_get(nm, &prof) && prof.display_name[0]) nm = prof.display_name;
    char head[3] = { TERM_MARK, 'W', '\0' };
    term_raw(w, head); term_out(w, nm); term_raw(w, "\n\n");
}

/* Run one command line as if typed: echo it as a prompt line, execute, then
 * leave one blank row before the next prompt. */
static void term_submit(window_t *w, const char *line_in) {
    char line[256]; kstrncpy(line, line_in, 255); line[255] = '\0';
    term_state_t *st = term_st(w);
    term_emit_prompt(w, line);
    term_hist_push(line);
    s_hist_idx = s_hist_count; s_hist_draft[0] = '\0';
    if (st) { st->sugg[0] = '\0'; st->comp_open = false; }
    if (line[0]) {
        term_exec(w, line);
        if (w->text_len && w->text_buf[w->text_len - 1] != '\n') term_raw(w, "\n");
        if (w->text_len) term_raw(w, "\n");
    }
    w->input_len = 0; w->input_buf[0] = '\0'; w->cursor_pos = 0;
    if (st) st->follow = true;
}

/* Field idx (0-based) of a rich line's TERM_SEP-separated payload. */
static void term_field(const char *s, u32 len, u32 idx, char *out, u32 cap) {
    u32 f = 0, n = 0;
    for (u32 i = 0; i < len; i++) {
        if (s[i] == TERM_SEP) { if (f == idx) break; f++; continue; }
        if (f == idx && n + 1 < cap) out[n++] = s[i];
    }
    out[n] = '\0';
}

/* -- Completion ------------------------------------------------------------ */
static bool term_is_path_cmd(const char *in) {
    static const char *const cmds[] = { "cd ", "ls ", "cat ", "rm ", "care " };
    for (u32 i = 0; i < sizeof cmds / sizeof cmds[0]; i++)
        if (!kstrncmp(in, cmds[i], kstrlen(cmds[i]))) return true;
    return false;
}

static void term_complete(window_t *w) {
    term_state_t *st = term_st(w);
    if (!st) return;
    if (st->comp_open && st->comp_n) { st->comp_sel = (st->comp_sel + 1) % st->comp_n; return; }
    char in[256]; kstrncpy(in, w->input_buf, 255); in[255] = '\0';
    u32 n = 0;
    i32 sp = -1;
    for (i32 i = 0; in[i]; i++) if (in[i] == ' ') sp = i;
    if (sp >= 0 && term_is_path_cmd(in)) {
        const char *tok = in + sp + 1;
        i32 slash = -1;
        for (i32 i = 0; tok[i]; i++) if (tok[i] == '/') slash = i;
        char dirpart[96] = "", base[64];
        if (slash >= 0) { kstrncpy(dirpart, tok, (u32)slash + 1); dirpart[slash + 1] = '\0'; }
        kstrncpy(base, tok + slash + 1, 63); base[63] = '\0';
        char dpath[128];
        if (dirpart[0] == '/') kstrcpy(dpath, dirpart);
        else { term_path(term_cwd, dpath); if (kstrcmp(dpath, "/")) kstrcat(dpath, "/"); kstrcat(dpath, dirpart); }
        u32 dl = (u32)kstrlen(dpath);
        if (dl > 1 && dpath[dl - 1] == '/') dpath[dl - 1] = '\0';
        struct fs_node *d = vfs_resolve_path(dpath);
        u32 bl = (u32)kstrlen(base);
        if (d && d->type == FS_DIR) {
            for (u32 i = 0; i < d->child_count && n < TERM_COMP_MAX; i++) {
                struct fs_node *c = d->children[i];
                if (kstrncmp(c->name, base, bl)) continue;
                if (c->name[0] == '.' && base[0] != '.') continue;
                char *t = st->comp_text[n];
                u32 pre = (u32)(tok - in);
                if (pre > 60) pre = 60;
                kstrncpy(t, in, pre); t[pre] = '\0';
                kstrcat(t, dirpart);
                u32 tl = (u32)kstrlen(t);
                kstrncpy(t + tl, c->name, 94 - tl); t[94] = '\0';
                if (c->type == FS_DIR) kstrcat(t, "/");
                st->comp[n].text = t;
                st->comp[n].hint = c->type == FS_DIR ? "folder" : "file";
                n++;
            }
        }
    } else {
        u32 idx[TERM_COMP_MAX];
        u32 total = term_comp_match(in, TERM_COMPS, TERM_COMP_COUNT, idx, TERM_COMP_MAX);
        n = total < TERM_COMP_MAX ? total : TERM_COMP_MAX;
        for (u32 i = 0; i < n; i++) {
            kstrncpy(st->comp_text[i], TERM_COMPS[idx[i]].text, 95); st->comp_text[i][95] = '\0';
            st->comp[i].text = st->comp_text[i];
            st->comp[i].hint = TERM_COMPS[idx[i]].hint;
        }
    }
    if (n == 0) return;
    if (n == 1) { term_set_input(w, st->comp_text[0]); return; }
    u32 all[TERM_COMP_MAX];
    for (u32 i = 0; i < n; i++) all[i] = i;
    u32 common = term_comp_common(st->comp, all, n);
    if (common > (u32)kstrlen(in) && common < 256) {
        char pre[256]; kstrncpy(pre, st->comp_text[0], common); pre[common] = '\0';
        term_set_input(w, pre);
    }
    st->comp_n = n; st->comp_sel = 0; st->comp_open = true;
}

static void term_comp_accept(window_t *w, term_state_t *st) {
    if (st->comp_sel < st->comp_n) term_set_input(w, st->comp_text[st->comp_sel]);
    st->comp_open = false;
}

/* -- Drawing --------------------------------------------------------------- */
static i32 term_pill(const term_metrics_t *m, i32 x, i32 y, const char *s, u32 bg, u32 ink, font_weight_t wt) {
    i32 sc = (i32)GFX_FONT_SCALE;
    i32 tw = gfx_str_role_width(s, FONT_ROLE_MONO, TERM_SLOT, wt);
    gfx_rect_rounded(x, y, tw + 16 * sc, m->mono_lh, 6 * sc, bg);
    gfx_str_role(x + 8 * sc, y, s, ink, FONT_ROLE_MONO, TERM_SLOT, wt);
    return tw + 16 * sc;
}

/* User and cwd pills on the row at y; returns where the command starts. */
static i32 term_draw_pills(const term_metrics_t *m, const term_pal_t *p, i32 y,
                           const char *user, const char *cwd) {
    i32 sc = (i32)GFX_FONT_SCALE;
    i32 py = y + (m->lh - m->mono_lh) / 2, x = m->text_x;
    x += term_pill(m, x, py, user, p->pill_u, p->pill_u_ink, FONT_W_BOLD) + 8 * sc;
    x += term_pill(m, x, py, cwd, p->pill_c, p->pill_c_ink, FONT_W_REGULAR) + 8 * sc;
    return x;
}

/* Draw s from *x if it fits before limit (else as much as fits, then stop). */
static void term_seg(i32 *x, i32 limit, i32 y, const char *s, u32 col, font_role_t role, u32 slot, font_weight_t wt) {
    if (*x >= limit) return;
    i32 tw = gfx_str_role_width(s, role, slot, wt);
    if (*x + tw <= limit) { gfx_str_role(*x, y, s, col, role, slot, wt); *x += tw; return; }
    gfx_str_role_fit(*x, y, limit - *x, s, col, role, slot, wt);
    *x = limit;
}

static void term_draw_help(const term_metrics_t *m, const term_pal_t *p, i32 y) {
    i32 cw, rh[TG_COUNT], nr;
    term_help_layout(m->text_w, &cw, rh, &nr);
    i32 cols = term_help_cols(m->text_w), yy = y + 6;
    i32 title_h = gfx_role_line_h(FONT_ROLE_UI, 1);
    u32 cmd_col = ui_mix(p->card, p->text, 215);
    for (i32 r = 0; r < nr; r++) {
        for (i32 c = 0; c < cols; c++) {
            u32 g = (u32)(r * cols + c);
            if (g >= TG_COUNT) break;
            i32 x = m->text_x + c * (cw + HELP_GAP);
            gfx_rect_rounded(x, yy, cw, rh[r], 14, p->card);
            gfx_str_role_fit(x + 14, yy + 12, cw - 28, term_group_name(g), term_group_col(g, p->light),
                             FONT_ROLE_UI, 1, FONT_W_BOLD);
            char line[160]; term_help_line(g, line, sizeof line);
            gfx_str_role_wrap(x + 14, yy + 12 + title_h + 4, cw - 28, line, cmd_col,
                              FONT_ROLE_MONO, 1, FONT_W_REGULAR, 2);
        }
        yy += rh[r] + HELP_GAP;
    }
}

static void term_draw_suggest(term_state_t *st, const term_metrics_t *m, const term_pal_t *p,
                              const char *s, u32 len, u32 off, i32 y) {
    char typed[64], disp[96];
    char kind = len > 2 ? s[2] : 'c';
    term_field(s + 3, len > 3 ? len - 3 : 0, 0, typed, sizeof typed);
    term_field(s + 3, len > 3 ? len - 3 : 0, 1, disp, sizeof disp);
    const u32 red = rgb(0xff, 0x5f, 0x57);
    i32 cx = m->text_x, cy = y + 4, cw = m->text_w, ch = SUGG_ROWS * m->lh - 8;
    if (ch < 36) ch = 36;
    gfx_rect_rounded(cx, cy, cw, ch, 12, ui_mix(p->bg, red, p->light ? 34 : 26));
    gfx_rect_rounded_outline(cx, cy, cw, ch, 12, ui_mix(p->bg, red, 77));
    u32 icol = p->light ? rgb(0xd0, 0x3a, 0x32) : rgb(0xff, 0x8a, 0x84);
    i32 icx = cx + 14 + 9, icy = cy + ch / 2;
    gfx_circle(icx, icy, 8, icol);
    gfx_rect(icx - 1, icy - 5, 2, 6, icol);
    gfx_rect(icx - 1, icy + 3, 2, 2, icol);
    /* Run it */
    const char *lbl = "Run it";
    i32 bw = gfx_str_role_width(lbl, FONT_ROLE_UI, 1, FONT_W_BOLD) + 24, bh = 28;
    i32 bx = cx + cw - 8 - bw, by = cy + (ch - bh) / 2;
    gfx_rect_rounded(bx, by, bw, bh, 9, rgb(0xff, 0x7a, 0x59));
    gfx_str_role(bx + 12, by + (bh - gfx_role_line_h(FONT_ROLE_UI, 1)) / 2, lbl,
                 rgb(0x2a, 0x12, 0x0a), FONT_ROLE_UI, 1, FONT_W_BOLD);
    if (st->run_n < TERM_RUN_MAX) {
        st->run_r[st->run_n] = rect_make(bx, by, bw, bh);
        st->run_off[st->run_n] = off;
        st->run_n++;
    }
    /* Message */
    i32 x = icx + 9 + 10, limit = bx - 10;
    i32 uy = cy + (ch - gfx_role_line_h(FONT_ROLE_UI, 1)) / 2;
    i32 my = cy + (ch - gfx_role_line_h(FONT_ROLE_MONO, 1)) / 2;
    u32 accent = p->light ? rgb(0xe0, 0x5a, 0x38) : rgb(0xff, 0x9a, 0x76);
    if (kind == 'f') term_seg(&x, limit, uy, "Can't find ", p->text, FONT_ROLE_UI, 1, FONT_W_REGULAR);
    term_seg(&x, limit, my, typed, p->text, FONT_ROLE_MONO, 1, FONT_W_BOLD);
    term_seg(&x, limit, uy, kind == 'f' ? " here. Did you mean " : " isn't a command. Did you mean ",
             p->text, FONT_ROLE_UI, 1, FONT_W_REGULAR);
    term_seg(&x, limit, my, disp, accent, FONT_ROLE_MONO, 1, FONT_W_REGULAR);
    term_seg(&x, limit, uy, "?", p->text, FONT_ROLE_UI, 1, FONT_W_REGULAR);
}

/* A rich line whose block starts at row top y. */
static void term_draw_rich(window_t *w, term_state_t *st, const term_metrics_t *m,
                           const term_pal_t *p, const term_vline_t *vl, i32 y) {
    const char *s = w->text_buf + vl->off;
    u32 len = vl->len;
    i32 ty = y + (m->lh - m->mono_lh) / 2;
    switch (s[1]) {
    case 'P': {
        char user[48], cwd[64], cmd[256];
        term_field(s + 2, len - 2, 0, user, sizeof user);
        term_field(s + 2, len - 2, 1, cwd, sizeof cwd);
        term_field(s + 2, len - 2, 2, cmd, sizeof cmd);
        i32 x = term_draw_pills(m, p, y, user, cwd);
        term_seg(&x, m->text_x + m->text_w, ty, cmd, p->text, FONT_ROLE_MONO, TERM_SLOT, FONT_W_REGULAR);
        break;
    }
    case 'W': {
        char name[48]; term_field(s + 2, len - 2, 0, name, sizeof name);
        i32 x = m->text_x, lim = m->text_x + m->text_w;
        term_seg(&x, lim, ty, "Welcome back, ", p->dim, FONT_ROLE_MONO, TERM_SLOT, FONT_W_REGULAR);
        term_seg(&x, lim, ty, name, p->dim, FONT_ROLE_MONO, TERM_SLOT, FONT_W_REGULAR);
        term_seg(&x, lim, ty, ". CareOS v9 - Type ", p->dim, FONT_ROLE_MONO, TERM_SLOT, FONT_W_REGULAR);
        term_seg(&x, lim, ty, "help", p->light ? rgb(0xe0, 0x5a, 0x38) : rgb(0xff, 0x9a, 0x76),
                 FONT_ROLE_MONO, TERM_SLOT, FONT_W_REGULAR);
        term_seg(&x, lim, ty, " to see what I can do.", p->dim, FONT_ROLE_MONO, TERM_SLOT, FONT_W_REGULAR);
        break;
    }
    case 'H': term_draw_help(m, p, y); break;
    case 'D': term_draw_suggest(st, m, p, s, len, vl->off, y); break;
    default: break;
    }
}

/* Colour-scheme menu geometry (shared by draw and click). */
#define TMENU_W   232
#define TMENU_IH  34
static rect_t term_menu_rect(window_t *w, const term_state_t *st) {
    rect_t cr = wm_client_rect(w);
    i32 h = (i32)TERM_THEME_COUNT * TMENU_IH + 12;
    i32 x = st->menu_btn.x + st->menu_btn.w - TMENU_W;
    if (x + TMENU_W > cr.x + cr.w - 8) x = cr.x + cr.w - 8 - TMENU_W;
    if (x < cr.x + 8) x = cr.x + 8;
    return rect_make(x, cr.y + 6, TMENU_W, h);
}

static void term_swatches(i32 x, i32 y, const term_theme_t *t) {
    gfx_rect_rounded(x, y, 10, 10, 3, t->cursor);
    gfx_rect_rounded(x + 13, y, 10, 10, 3, t->prompt);
    gfx_rect_rounded(x + 26, y, 10, 10, 3, t->border);
}

static void term_draw_menu(window_t *w, term_state_t *st) {
    rect_t r = term_menu_rect(w, st);
    gfx_shadow_soft(r.x, r.y, r.w, r.h, 14);
    gfx_rect_rounded(r.x, r.y, r.w, r.h, 14, g_theme->raised);
    gfx_rect_rounded_outline(r.x, r.y, r.w, r.h, 14, ui_mix(g_theme->raised, g_theme->ink, 30));
    i32 lh = gfx_role_line_h(FONT_ROLE_UI, 1);
    for (u32 i = 0; i < TERM_THEME_COUNT; i++) {
        i32 iy = r.y + 6 + (i32)i * TMENU_IH;
        if (i == g_term_theme_idx)
            gfx_rect_rounded(r.x + 6, iy, r.w - 12, TMENU_IH, 9, ui_mix(g_theme->raised, g_theme->ink, 26));
        term_swatches(r.x + 16, iy + (TMENU_IH - 10) / 2, &g_term_themes[i]);
        gfx_str_role(r.x + 62, iy + (TMENU_IH - lh) / 2, g_term_themes[i].name, g_theme->ink,
                     FONT_ROLE_UI, 1, i == g_term_theme_idx ? FONT_W_BOLD : FONT_W_REGULAR);
        if (i == g_term_theme_idx) ui_glyph(UI_GLYPH_CHECK, r.x + r.w - 32, iy + (TMENU_IH - 16) / 2, 16, g_theme->care);
    }
}

static void term_draw_popover(term_state_t *st, const term_metrics_t *m, const term_pal_t *p,
                              rect_t cr, i32 x, i32 live_y) {
    i32 sc = (i32)GFX_FONT_SCALE;
    i32 ih = 30 * sc, pw = 320 * sc, ph = (i32)st->comp_n * ih + 12 * sc;
    i32 px = x - 12 * sc, py = live_y + m->lh + 4 * sc;
    if (px + pw > cr.x + cr.w - 8) px = cr.x + cr.w - 8 - pw;
    if (px < cr.x + 8) px = cr.x + 8;
    if (py + ph > cr.y + cr.h - m->status_h - 4) py = live_y - ph - 4 * sc;
    gfx_shadow_soft(px, py, pw, ph, 14);
    gfx_rect_rounded(px, py, pw, ph, 14 * sc, p->pop);
    gfx_rect_rounded_outline(px, py, pw, ph, 14 * sc, ui_mix(p->pop, p->text, 30));
    i32 mlh = gfx_role_line_h(FONT_ROLE_MONO, 1), ulh = gfx_role_line_h(FONT_ROLE_UI, 0);
    for (u32 i = 0; i < st->comp_n; i++) {
        i32 iy = py + 6 * sc + (i32)i * ih;
        if (i == st->comp_sel) gfx_rect_rounded(px + 6 * sc, iy, pw - 12 * sc, ih, 9 * sc, p->pop_sel);
        const char *hint = i == st->comp_sel ? "Enter" : st->comp[i].hint;
        i32 hw = gfx_str_role_width(hint, FONT_ROLE_UI, 0, FONT_W_REGULAR);
        gfx_str_role(px + pw - 16 * sc - hw, iy + (ih - ulh) / 2, hint, p->muted, FONT_ROLE_UI, 0, FONT_W_REGULAR);
        gfx_str_role_fit(px + 16 * sc, iy + (ih - mlh) / 2, pw - 40 * sc - hw, st->comp[i].text,
                         i == st->comp_sel ? p->text : ui_mix(p->pop, p->text, 215), FONT_ROLE_MONO, 1, FONT_W_REGULAR);
    }
}

void app_terminal_draw(window_t *w){
    rect_t cr = wm_client_rect(w);
    term_pal_t p = term_pal();
    term_metrics_t m = term_metrics(w);
    i32 sc = (i32)GFX_FONT_SCALE;

    /* Body with configurable transparency (92 % by default). */
    if (g_term_opacity >= 253) gfx_rect(cr.x, cr.y, cr.w, cr.h, p.bg);
    else if (g_term_opacity > 0) gfx_rect_blend(cr.x, cr.y, cr.w, cr.h, p.bg, g_term_opacity);

    term_state_t *st = term_st(w);
    if (!st) return;
    term_relayout(w, st, &m);
    u32 total = term_total_rows(w, st);
    u32 max_scroll = total > m.vis ? total - m.vis : 0;
    if (st->follow || w->scroll > max_scroll) w->scroll = max_scroll;
    if (w->scroll == max_scroll) st->follow = true;
    st->run_n = 0;

    gfx_set_clip(cr.x, m.text_y, cr.w, m.text_h);
    for (u32 li = w->scroll; li < st->n && li < w->scroll + m.vis; li++) {
        i32 y = m.text_y + (i32)(li - w->scroll) * m.lh;
        const term_vline_t *vl = &st->lines[li];
        if (vl->len == TERM_VL_CONT) {
            if (li != w->scroll) continue;           /* drawn by its head */
            u32 h = li;
            while (h > 0 && st->lines[h].len == TERM_VL_CONT) h--;
            term_draw_rich(w, st, &m, &p, &st->lines[h], y - (i32)(li - h) * m.lh);
            continue;
        }
        if (vl->len >= 2 && w->text_buf[vl->off] == TERM_MARK) {
            term_draw_rich(w, st, &m, &p, vl, y);
            continue;
        }
        char tmp[1024];
        u32 len = vl->len < sizeof(tmp) - 1 ? vl->len : (u32)sizeof(tmp) - 1;
        kmemcpy(tmp, w->text_buf + vl->off, len); tmp[len] = '\0';
        gfx_str_role(m.text_x, y + (m.lh - m.mono_lh) / 2, tmp, p.text, FONT_ROLE_MONO, TERM_SLOT, FONT_W_REGULAR);
    }

    /* The live prompt: pills, the input (scrolled to keep the cursor in
     * view) and a block cursor. */
    u32 live = total - 1;
    i32 live_y = 0, cmd_x = 0;
    bool live_vis = live >= w->scroll && live < w->scroll + m.vis;
    if (live_vis) {
        live_y = m.text_y + (i32)(live - w->scroll) * m.lh;
        char cwd[64]; term_cwd_display(term_cwd, cwd, sizeof cwd);
        cmd_x = term_draw_pills(&m, &p, live_y, user_current_name(), cwd);
        i32 cw_ch = term_char_w('M');
        i32 avail = m.text_x + m.text_w - cmd_x - cw_ch;
        if (avail < cw_ch * 4) avail = cw_ch * 4;
        u32 cp = w->cursor_pos > w->input_len ? w->input_len : w->cursor_pos;
        u32 first = 0;
        while (first < cp && gfx_str_role_width_n(w->input_buf + first, cp - first, FONT_ROLE_MONO, TERM_SLOT, FONT_W_REGULAR) > avail)
            first++;
        i32 ty = live_y + (m.lh - m.mono_lh) / 2;
        i32 cx = cmd_x + gfx_str_role_width_n(w->input_buf + first, cp - first, FONT_ROLE_MONO, TERM_SLOT, FONT_W_REGULAR);
        u32 phase = (timer_ms() / 10) % 100;     /* 1 s blink, eased */
        u8 ca = (u8)(phase < 50 ? 255 - phase * 3 : 105 + (phase - 50) * 3);
        if (w->focused) gfx_rect_blend(cx, ty, cw_ch, m.mono_lh, p.cursor, ca);
        else gfx_rect_rounded_outline(cx, ty, cw_ch, m.mono_lh, 2, p.cursor);
        gfx_set_clip(cmd_x, m.text_y, avail + cw_ch, m.text_h);
        gfx_str_role(cmd_x, ty, w->input_buf + first, p.text, FONT_ROLE_MONO, TERM_SLOT, FONT_W_REGULAR);
        gfx_set_clip(cr.x, m.text_y, cr.w, m.text_h);
    }
    gfx_clear_clip();

    /* Thin rounded scrollbar while scrolled back or just after scrolling. */
    if (total > m.vis && (!st->follow || timer_ms() - st->scroll_ms < 1200)) {
        i32 th = m.text_h * (i32)m.vis / (i32)total; if (th < 24) th = 24;
        i32 ty2 = m.text_y + (i32)((i64)(m.text_h - th) * (i64)w->scroll / (i64)(max_scroll ? max_scroll : 1));
        gfx_rect_rounded_blend(cr.x + cr.w - 8 * sc, ty2, 4 * sc, th, 2 * sc, p.text, 90);
    }

    /* Status bar */
    if (m.status_h) {
        i32 sy = cr.y + cr.h - m.status_h;
        if (g_term_opacity >= 253) gfx_rect(cr.x, sy, cr.w, m.status_h, p.status);
        else gfx_rect_blend(cr.x, sy, cr.w, m.status_h, p.status, g_term_opacity);
        gfx_hline(cr.x, sy, cr.w, p.line);
        i32 ty = sy + (m.status_h - gfx_role_line_h(FONT_ROLE_UI, 0)) / 2;
        i32 x = cr.x + 16 * sc;
        gfx_str_role(x, ty, "CareOS shell 9.0", p.muted, FONT_ROLE_UI, 0, FONT_W_REGULAR);
        x += gfx_str_role_width("CareOS shell 9.0", FONT_ROLE_UI, 0, FONT_W_REGULAR) + 16 * sc;
        static const char *const chips[] = { "Tab complete", "Ctrl+T theme" };
        i32 rx = cr.x + cr.w - 16 * sc;
        for (u32 i = 0; i < 2; i++) {
            i32 cw2 = gfx_str_role_width(chips[i], FONT_ROLE_UI, 0, FONT_W_REGULAR);
            if (rx - cw2 < x + 120 * sc) break;
            rx -= cw2;
            gfx_str_role(rx, ty, chips[i], p.muted, FONT_ROLE_UI, 0, FONT_W_REGULAR);
            rx -= 16 * sc;
        }
        char path[64]; term_path(term_cwd, path);
        gfx_str_role_fit(x, ty, rx - x - 8 * sc, path, p.muted, FONT_ROLE_UI, 0, FONT_W_REGULAR);
    }

    if (st->comp_open && live_vis && st->comp_n) term_draw_popover(st, &m, &p, cr, cmd_x, live_y);
    if (st->menu_open) term_draw_menu(w, st);
}

/* Title-bar toolbar: the session pill (one session: no tabs exist) and the
 * colour scheme, which opens the theme menu. */
bool app_terminal_toolbar(window_t *w, rect_t bar) {
    term_state_t *st = term_st(w);
    if (!st) return false;
    i32 h = 32, y = bar.y + (bar.h - h) / 2;
    i32 lh = gfx_role_line_h(FONT_ROLE_UI, 1);
    const char *tn = g_term_themes[g_term_theme_idx].name;
    i32 bw = 12 + 36 + 8 + gfx_str_role_width(tn, FONT_ROLE_UI, 1, FONT_W_BOLD) + 12;
    i32 bx = bar.x + bar.w - bw;
    if (bx < bar.x + 140) {
        st->menu_btn = rect_make(0, 0, 0, 0);
        bx = bar.x + bar.w + 8;
    } else {
        st->menu_btn = rect_make(bx, y, bw, h);
        if (st->menu_open) gfx_rect_rounded(bx, y, bw, h, 10, g_theme->raised);
        term_swatches(bx + 12, y + (h - 10) / 2, &g_term_themes[g_term_theme_idx]);
        gfx_str_role(bx + 12 + 36 + 8, y + (h - lh) / 2, tn, g_theme->ink2, FONT_ROLE_UI, 1, FONT_W_BOLD);
    }
    /* "~ <basename>" inside the home folder (home itself: "~ <user>"),
     * otherwise the folder name, or "/" for the root. */
    char cwd[64], lbl[72];
    term_cwd_display(term_cwd, cwd, sizeof cwd);
    const char *base = cwd;
    for (const char *q = cwd; *q; q++) if (*q == '/' && q[1]) base = q + 1;
    if (!kstrcmp(cwd, "~")) { kstrcpy(lbl, "~ "); kstrncpy(lbl + 2, user_current_name(), 40); lbl[42] = '\0'; }
    else if (cwd[0] == '~') { kstrcpy(lbl, "~ "); kstrcat(lbl, base); }
    else kstrcpy(lbl, base);
    i32 pw = 28 + gfx_str_role_width(lbl, FONT_ROLE_UI, 1, FONT_W_BOLD) + 12;
    if (pw > bx - 8 - bar.x) pw = bx - 8 - bar.x;
    if (pw >= 40) {
        gfx_rect_rounded(bar.x, y, pw, h, 10, g_theme->raised);
        gfx_circle_fill(bar.x + 16, y + h / 2, 4, g_theme->term_green);
        gfx_str_role_fit(bar.x + 28, y + (h - lh) / 2, pw - 36, lbl, g_theme->ink, FONT_ROLE_UI, 1, FONT_W_BOLD);
    }
    return true;
}

bool app_terminal_toolbar_click(window_t *w, i32 x, i32 y) {
    term_state_t *st = term_st(w);
    if (!st) return false;
    if (st->menu_btn.w && rect_contains(st->menu_btn, x, y)) {
        st->menu_open = !st->menu_open;
        st->comp_open = false;
        return true;
    }
    st->menu_open = false;
    return false;
}

void app_terminal_click(window_t *w, i32 x, i32 y) {
    term_state_t *st = term_st(w);
    if (!st) return;
    if (st->menu_open) {
        rect_t r = term_menu_rect(w, st);
        if (rect_contains(r, x, y)) {
            i32 i = (y - r.y - 6) / TMENU_IH;
            if (i >= 0 && (u32)i < TERM_THEME_COUNT) g_term_theme_idx = (u32)i;
        }
        st->menu_open = false;
        return;
    }
    for (u32 i = 0; i < st->run_n; i++) {
        if (!rect_contains(st->run_r[i], x, y)) continue;
        u32 off = st->run_off[i];
        if (off + 3 >= w->text_len || w->text_buf[off] != TERM_MARK) return;
        u32 len = 0;
        while (off + len < w->text_len && w->text_buf[off + len] != '\n') len++;
        char cmd[256];
        term_field(w->text_buf + off + 3, len - 3, 2, cmd, sizeof cmd);
        if (cmd[0]) term_submit(w, cmd);
        return;
    }
    st->comp_open = false;
}

void app_terminal_key(window_t *w,char c){
    if(w->cursor_pos>w->input_len) w->cursor_pos=w->input_len;
    u8 k=(u8)c;
    term_state_t *st = term_st(w);
    if (st && st->menu_open) {
        if (k == 0x1B) { st->menu_open = false; return; }
        if (k == KEY_NAV_UP || k == KEY_NAV_DOWN) {
            g_term_theme_idx = (g_term_theme_idx + (k == KEY_NAV_UP ? TERM_THEME_COUNT - 1 : 1)) % TERM_THEME_COUNT;
            return;
        }
        if (c == '\n') { st->menu_open = false; return; }
        st->menu_open = false;
    }
    if (st && st->comp_open) {
        if (k == KEY_NAV_UP)   { st->comp_sel = (st->comp_sel + st->comp_n - 1) % st->comp_n; return; }
        if (k == KEY_NAV_DOWN || c == '\t') { st->comp_sel = (st->comp_sel + 1) % st->comp_n; return; }
        if (c == '\n') { term_comp_accept(w, st); return; }
        if (k == 0x1B) { st->comp_open = false; return; }
        st->comp_open = false;                       /* typing closes it */
    }
    if(c=='\t'){ term_complete(w); return; }
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
        if (w->input_len == 0 && st && st->sugg[0]) {
            char run[256]; kstrcpy(run, st->sugg);
            term_submit(w, run);                     /* Enter runs the suggestion */
        } else {
            char line[256]; kstrcpy(line, w->input_buf);
            term_submit(w, line);
        }
    } else if(c=='\b'){
        if(w->cursor_pos>0){ w->cursor_pos--; term_erase_at(w,w->cursor_pos); }
    } else if(c==0x0C){ /* Ctrl+L: Clear */
        win_clear(w);
        term_invalidate(w);
        if (st) { st->follow = true; st->sugg[0] = '\0'; }
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
