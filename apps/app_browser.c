/* CareOS v9 -- apps/app_browser.c -- Web Browser */
#include "apps_common.h"
#include "http_parse.h"
#include "browser_fetch.h"

/* ── Layout ──────────────────────────────────────────────────────── */
#define BROW_BAR_H    48
#define BROW_STAT_H   24
#define BROW_CONT_MAX 65536      /* must match window_t.browser_content[] */
#define BROW_RESP_SZ  131072     /* raw HTTP response incl. headers */
#define BROW_LINK_MAX 64
#define BROW_TAB_H    30
#define BROW_FIND_H   28
#define BROW_BM_BTN   32
#define FW  ((i32)(FONT_W * GFX_FONT_SCALE))
#define FH  ((i32)(FONT_H * GFX_FONT_SCALE))


/* ── Link hit areas (rebuilt each draw) ─────────────────────────── */
typedef struct { i32 x,y,w,h; char url[192]; } blink_t;
static blink_t g_links[BROW_LINK_MAX];
static u32     g_nlinks;

/* ── Static buffers ─────────────────────────────────────────────── */
/* Raw responses live in the fetch worker's buffer (apps/browser_fetch.c). */
#define g_resp (browser_fetch_buffer())
static char g_status[96] = "Ready";
static bool s_redirecting = false;  /* navigate() called for a redirect or fallback */
static char s_url_before_edit[256]; /* URL to restore if a Ctrl+L edit is cancelled */

/* ── Forward declarations ────────────────────────────────────────── */
static void app_browser_navigate(window_t *w, const char *url);
static void browser_save_tab(window_t *w);

/* ── String helpers ──────────────────────────────────────────────── */
static const char *bs_strstr(const char *hay, const char *needle) {
    if (!needle || !*needle) return hay;
    u32 nl = kstrlen(needle);
    while (*hay) {
        if (kstrncmp(hay, needle, nl) == 0) return hay;
        hay++;
    }
    return NULL;
}

static int bs_memcmp(const void *a, const void *b, u32 n) {
    const u8 *p = (const u8*)a, *q = (const u8*)b;
    for (u32 i = 0; i < n; i++) {
        if (p[i] < q[i]) return -1;
        if (p[i] > q[i]) return 1;
    }
    return 0;
}

static const char *bs_mfind(const char *hay, u32 hlen,
                             const char *needle, u32 nlen) {
    if (nlen == 0 || hlen < nlen) return NULL;
    for (u32 i = 0; i <= hlen - nlen; i++)
        if (bs_memcmp(hay + i, needle, nlen) == 0) return hay + i;
    return NULL;
}

/* ── URL utilities ───────────────────────────────────────────────── */
static void url_encode(const char *s, char *d, u32 max) {
    static const char hex[] = "0123456789ABCDEF";
    u32 n = 0;
    while (*s && n + 4 < max) {
        u8 c = (u8)*s++;
        if ((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')
            ||c=='-'||c=='_'||c=='.'||c=='~') { d[n++]=(char)c; }
        else if (c == ' ') { d[n++] = '+'; }
        else { d[n++]='%'; d[n++]=hex[c>>4]; d[n++]=hex[c&15]; }
    }
    d[n] = '\0';
}

static void extract_href(const char *attrs, char *href, u32 max) {
    href[0] = '\0';
    const char *p = attrs;
    while (*p) {
        if (kstrncmp(p,"href=",5)==0 || kstrncmp(p,"HREF=",5)==0) {
            p += 5;
            while (*p == ' ') p++;
            char q = 0;
            if (*p == '"' || *p == '\'') q = *p++;
            u32 i = 0;
            while (*p && i < max-1) {
                if (q && *p == q) break;
                if (!q && (*p == ' ' || *p == '>')) break;
                href[i++] = *p++;
            }
            href[i] = '\0';
            return;
        }
        p++;
    }
}

/* Case-insensitive lookup of a named attribute's value within a tag's attribute
 * string. Matches only at a token boundary so "type" won't match inside another
 * attribute name. Returns true and fills `out` when found. */
static bool extract_attr(const char *attrs, const char *key, char *out, u32 max) {
    u32 klen = (u32)kstrlen(key);
    const char *p = attrs;
    while (*p) {
        bool boundary = (p == attrs) || p[-1]==' ' || p[-1]=='\t' || p[-1]=='\n';
        if (boundary) {
            u32 i = 0;
            while (i < klen && p[i]) {
                char a=p[i], b=key[i];
                if (a>='A'&&a<='Z') a+=32;
                if (b>='A'&&b<='Z') b+=32;
                if (a!=b) break;
                i++;
            }
            if (i == klen && p[klen]=='=') {
                const char *v = p + klen + 1;
                char q = 0;
                if (*v=='"'||*v=='\'') q=*v++;
                u32 o = 0;
                while (*v && o < max-1) {
                    if (q && *v==q) break;
                    if (!q && (*v==' '||*v=='>')) break;
                    /* Decode the handful of entities that show up in visible
                     * attribute values (e.g. value="Next Page &gt;"). */
                    if (*v=='&') {
                        if      (kstrncmp(v,"&gt;",4)==0)   { out[o++]='>'; v+=4; continue; }
                        else if (kstrncmp(v,"&lt;",4)==0)   { out[o++]='<'; v+=4; continue; }
                        else if (kstrncmp(v,"&amp;",5)==0)  { out[o++]='&'; v+=5; continue; }
                        else if (kstrncmp(v,"&quot;",6)==0) { out[o++]='"'; v+=6; continue; }
                        else if (kstrncmp(v,"&#39;",5)==0)  { out[o++]='\'';v+=5; continue; }
                        else if (kstrncmp(v,"&nbsp;",6)==0) { out[o++]=' '; v+=6; continue; }
                    }
                    out[o++]=*v++;
                }
                out[o]='\0';
                return true;
            }
        }
        p++;
    }
    out[0]='\0';
    return false;
}

static void extract_title(const char *html, char *title, u32 max) {
    title[0] = '\0';
    const char *p = html;
    while (*p) {
        if (p[0]=='<' && (p[1]=='t'||p[1]=='T') && (p[2]=='i'||p[2]=='I')
            && (p[3]=='t'||p[3]=='T') && (p[4]=='l'||p[4]=='L')
            && (p[5]=='e'||p[5]=='E')) {
            while (*p && *p != '>') p++;
            if (*p == '>') p++;
            u32 i = 0;
            while (*p && *p != '<' && i < max-1) title[i++] = *p++;
            title[i] = '\0';
            return;
        }
        p++;
    }
}

/* ── History ─────────────────────────────────────────────────────── */
static void history_push(window_t *w, const char *url) {
    /* don't push duplicates */
    if (w->browser_history_count > 0 &&
        kstrcmp(w->browser_history[w->browser_history_count-1], url) == 0)
        return;
    if (w->browser_history_count >= 10) {
        for (int i = 0; i < 9; i++)
            kstrncpy(w->browser_history[i], w->browser_history[i+1], 255);
        w->browser_history_count = 9;
    }
    kstrncpy(w->browser_history[w->browser_history_count], url, 255);
    w->browser_history_count++;
    w->browser_history_pos = w->browser_history_count - 1;
}

/* ── HTML renderer ───────────────────────────────────────────────── */
typedef struct {
    i32  cx, cy;
    i32  base_lm, lm, rm;
    i32  ct, cb;
    u32  fg;
    bool bold, link, pre, skip;
    int  hlevel, indent;
    char href[192];
    i32  lsx, lsy;
    /* Table support */
    bool in_table;
    i32  col_idx, ncols, table_col_w;
    /* Form support */
    bool in_form;
    char form_action[192];
} rctx_t;

static i32 r_lh(rctx_t *c) {
    if (c->hlevel == 1) return FH * 2 + 6;
    if (c->hlevel == 2) return FH + 10;
    if (c->hlevel >= 3) return FH + 6;
    return FH + 4;
}

static void r_nl(rctx_t *c) { c->cy += r_lh(c); c->cx = c->lm; }

static void r_char(rctx_t *c, char ch) {
    if (c->skip) return;
    if (ch == '\n') { r_nl(c); return; }
    if (c->cx + FW > c->rm) { c->cy += r_lh(c); c->cx = c->lm; }
    if (c->cy >= c->ct && c->cy + FH <= c->cb) {
        u32 col;
        if (c->link)           col = COL_PRIMARY;
        else if (c->bold)      col = COL_ACCENT;
        else if (c->hlevel==1) col = COL_TEXT;
        else if (c->hlevel==2) col = COL_ACCENT;
        else                   col = c->fg;
        gfx_char(c->cx, c->cy, ch, col, COL_TRANSPARENT);
    }
    c->cx += FW;
}

static void r_word(rctx_t *c, const char *w, int len) {
    if (c->skip || len == 0) return;
    i32 ww = (i32)len * FW;
    if (c->cx + ww > c->rm && c->cx > c->lm) { c->cy += r_lh(c); c->cx = c->lm; }
    for (int i = 0; i < len; i++) r_char(c, w[i]);
}

static bool tag_eq(const char *a, const char *b) { return kstrcmp(a,b)==0; }

static i32 render_html(rctx_t *c, const char *html) {
    const char *p = html;
    char word[128]; int wi = 0;

#define FLUSH() do { if (wi>0) { r_word(c,word,wi); wi=0; } } while(0)

    while (*p) {
        if (*p == '<') {
            FLUSH();
            p++;
            /* comment */
            if (p[0]=='!'&&p[1]=='-'&&p[2]=='-') {
                p += 3;
                while (*p && !(p[0]=='-'&&p[1]=='-'&&p[2]=='>')) p++;
                if (*p) p += 3;
                continue;
            }
            /* doctype / PI */
            if (*p == '!' || *p == '?') {
                while (*p && *p != '>') p++;
                if (*p) p++;
                continue;
            }
            bool closing = (*p == '/');
            if (closing) p++;

            /* tag name (lowercased) */
            char name[32]={0}; int ni=0;
            while (*p&&*p!='>'&&*p!=' '&&*p!='\t'&&*p!='\n'&&*p!='/') {
                char ch = *p++;
                if (ch>='A'&&ch<='Z') ch+=32;
                if (ni<31) name[ni++]=ch;
            }
            name[ni]='\0';

            /* attributes (preserve case) */
            char attrs[256]={0}; int ai=0;
            while (*p && *p != '>') { if (ai<255) attrs[ai++]=*p; p++; }
            if (*p=='>') p++;
            attrs[ai]='\0';

            if (!closing) {
                if (tag_eq(name,"head")||tag_eq(name,"script")||tag_eq(name,"style"))
                    { c->skip=true; continue; }
                if (c->skip) continue;

                if (name[0]=='h'&&name[1]>='1'&&name[1]<='6'&&name[2]=='\0') {
                    if (c->cx>c->lm) r_nl(c);
                    c->cy += (name[1]<='2' ? 8 : 4);
                    c->hlevel = name[1]-'0';
                    c->cx = c->lm;
                }
                else if (tag_eq(name,"b")||tag_eq(name,"strong")) { c->bold=true; }
                else if (tag_eq(name,"i")||tag_eq(name,"em"))      { c->fg=COL_DIM; }
                else if (tag_eq(name,"a")) {
                    c->link=true; c->lsx=c->cx; c->lsy=c->cy;
                    extract_href(attrs, c->href, sizeof(c->href));
                }
                else if (tag_eq(name,"p")||tag_eq(name,"div")||
                         tag_eq(name,"article")||tag_eq(name,"section")||
                         tag_eq(name,"main")||tag_eq(name,"header")) {
                    if (c->cx>c->lm) r_nl(c);
                    if (c->cy>c->ct) { c->cy+=FH/2; }
                    c->cx=c->lm;
                }
                else if (tag_eq(name,"br"))  { r_nl(c); }
                else if (tag_eq(name,"hr")) {
                    if (c->cx>c->lm) r_nl(c);
                    c->cy+=4;
                    if (c->cy>=c->ct && c->cy<c->cb)
                        gfx_hline(c->lm, c->cy, c->rm-c->lm, COL_BORDER);
                    c->cy+=8; c->cx=c->lm;
                }
                else if (tag_eq(name,"li")) {
                    if (c->cx>c->lm) r_nl(c);
                    c->cy+=2;
                    i32 bx = c->lm - FW;
                    if (bx >= c->base_lm && c->cy>=c->ct && c->cy<c->cb)
                        gfx_circle_fill(bx+FW/2, c->cy+FH/2, 3, COL_PRIMARY);
                }
                else if (tag_eq(name,"ul")||tag_eq(name,"ol")) {
                    if (c->cx>c->lm) r_nl(c);
                    if (c->indent < 6) c->indent++;
                    c->lm = c->base_lm + c->indent*FW*3;
                    c->cx = c->lm;
                }
                else if (tag_eq(name,"blockquote")) {
                    if (c->cx>c->lm) r_nl(c);
                    if (c->indent < 6) c->indent++;
                    c->lm = c->base_lm + c->indent*FW*3;
                    c->cx = c->lm;
                    if (c->cy>=c->ct && c->cy<c->cb)
                        gfx_rect(c->lm-FW/2, c->cy, 3, FH*4, COL_PRIMARY);
                }
                else if (tag_eq(name,"pre")||tag_eq(name,"code")) {
                    c->pre=true; c->fg=COL_DIM; r_nl(c);
                }
                else if (tag_eq(name,"img")) {
                    i32 iw=60, ih=44;
                    if (c->cx+iw>c->rm) r_nl(c);
                    if (c->cy>=c->ct && c->cy+ih<=c->cb) {
                        gfx_rect_rounded(c->cx, c->cy, iw, ih, 4, COL_SURFACE2);
                        gfx_rect_rounded_outline(c->cx, c->cy, iw, ih, 4, COL_BORDER);
                        gfx_str(c->cx+4, c->cy+ih/2-FH/2, "[img]", COL_DIM, COL_TRANSPARENT);
                    }
                    c->cx += iw+4;
                }
                else if (tag_eq(name,"table")) {
                    if (c->cx>c->lm) r_nl(c);
                    c->in_table=true; c->col_idx=0; c->ncols=3;
                    c->table_col_w=(c->rm-c->lm)/3;
                }
                else if (tag_eq(name,"tr")) {
                    if (c->cx>c->lm) r_nl(c);
                    c->col_idx=0; c->cy+=2; c->cx=c->lm;
                }
                else if (tag_eq(name,"td")||tag_eq(name,"th")) {
                    /* Flow cells inline with a small gap rather than snapping to
                     * fixed column x-positions. The old grid math
                     * (lm + col_idx*table_col_w) pushed any table wider than 3
                     * columns -- e.g. DuckDuckGo Lite's region selector -- off
                     * the right margin, which is what scattered the layout. */
                    if (c->col_idx>0 && c->cx>c->lm) { r_char(c,' '); r_char(c,' '); }
                    c->col_idx++;
                    if (tag_eq(name,"th")) c->bold=true;
                }
                else if (tag_eq(name,"form")) {
                    c->in_form=true;
                    const char *ap=bs_strstr(attrs,"action=");
                    if(ap){ ap+=7; char q2=0;
                        if(*ap=='"'||*ap=='\'') q2=*ap++;
                        u32 ai=0;
                        while(*ap&&ai<191){
                            if(q2&&*ap==q2) break;
                            if(!q2&&(*ap==' '||*ap=='>')) break;
                            c->form_action[ai++]=*ap++;
                        }
                        c->form_action[ai]='\0';
                    }
                }
                else if (tag_eq(name,"input")) {
                    char itype[16]; extract_attr(attrs,"type",itype,sizeof(itype));
                    for(char*t=itype;*t;t++) if(*t>='A'&&*t<='Z') *t+=32;
                    if(itype[0]=='\0') kstrcpy(itype,"text");

                    if (tag_eq(itype,"hidden")) {
                        /* Invisible control: draw nothing (these were showing up
                         * as a scatter of empty boxes on form-heavy pages). */
                    }
                    else if (tag_eq(itype,"submit")||tag_eq(itype,"button")||tag_eq(itype,"reset")) {
                        char val[48]; if(!extract_attr(attrs,"value",val,sizeof(val))) kstrcpy(val,"Submit");
                        i32 bw2=(i32)kstrlen(val)*FW+16; if(bw2<40) bw2=40;
                        i32 bh2=FH+8;
                        if(c->cx+bw2>c->rm) r_nl(c);
                        if(c->cy>=c->ct&&c->cy+bh2<=c->cb){
                            gfx_rect_rounded(c->cx,c->cy,bw2,bh2,4,COL_PRIMARY);
                            gfx_str_clipped(c->cx+8,c->cy+4,bw2-12,val,COL_WHITE,COL_TRANSPARENT);
                        }
                        c->cx+=bw2+6;
                    }
                    else if (tag_eq(itype,"checkbox")||tag_eq(itype,"radio")) {
                        bool is_radio = tag_eq(itype,"radio");
                        bool checked  = bs_strstr(attrs,"checked") != 0;
                        i32 sz=FH+2;
                        if(c->cx+sz>c->rm) r_nl(c);
                        if(c->cy>=c->ct&&c->cy+sz<=c->cb){
                            gfx_rect_rounded_outline(c->cx,c->cy,sz,sz, is_radio?sz/2:3, COL_BORDER);
                            if(checked) gfx_rect_rounded(c->cx+3,c->cy+3,sz-6,sz-6,2,COL_PRIMARY);
                        }
                        c->cx+=sz+4;
                    }
                    else { /* text / search / email / url / password / number ... */
                        char val[48];
                        if(!extract_attr(attrs,"value",val,sizeof(val)))
                            extract_attr(attrs,"placeholder",val,sizeof(val));
                        i32 iw2=150, ih2=FH+6;
                        if(c->cx+iw2>c->rm) r_nl(c);
                        if(c->cy>=c->ct&&c->cy+ih2<=c->cb){
                            gfx_rect_rounded(c->cx,c->cy,iw2,ih2,4,COL_INPUT_BG);
                            gfx_rect_rounded_outline(c->cx,c->cy,iw2,ih2,4,COL_BORDER);
                            if(val[0]) gfx_str_clipped(c->cx+5,c->cy+3,iw2-10,val,COL_DIM,COL_TRANSPARENT);
                        }
                        c->cx+=iw2+6;
                    }
                }
                else if (tag_eq(name,"button")) {
                    /* Draw a pill; the button's inner label renders on top of it
                     * as normal flow text, so don't hardcode a caption. */
                    i32 bw2=60, bh2=FH+8;
                    if(c->cx+bw2>c->rm) r_nl(c);
                    if(c->cy>=c->ct&&c->cy+bh2<=c->cb)
                        gfx_rect_rounded(c->cx,c->cy,bw2,bh2,4,COL_SURFACE2);
                }
                else if (tag_eq(name,"select")||tag_eq(name,"textarea")) {
                    /* Collapse into one control. Without this, a <select>'s dozens
                     * of <option> labels (e.g. DuckDuckGo Lite's country list) all
                     * spilled out as plain text. Skip the inner content. */
                    i32 iw2=150, ih2=FH+6;
                    if(c->cx+iw2>c->rm) r_nl(c);
                    if(c->cy>=c->ct&&c->cy+ih2<=c->cb){
                        gfx_rect_rounded(c->cx,c->cy,iw2,ih2,4,COL_INPUT_BG);
                        gfx_rect_rounded_outline(c->cx,c->cy,iw2,ih2,4,COL_BORDER);
                        if(tag_eq(name,"select"))
                            gfx_str(c->cx+iw2-13,c->cy+3,"v",COL_DIM,COL_TRANSPARENT);
                    }
                    c->cx+=iw2+6;
                    c->skip=true;   /* hide <option>/<textarea> body */
                }
            } else { /* closing */
                if (tag_eq(name,"head")||tag_eq(name,"script")||tag_eq(name,"style"))
                    { c->skip=false; continue; }
                /* Must clear the select/textarea skip BEFORE the generic skip
                 * guard below, or the guard would swallow this closing tag and
                 * leave the rest of the page hidden. */
                if (tag_eq(name,"select")||tag_eq(name,"textarea"))
                    { c->skip=false; continue; }
                if (c->skip) continue;

                if (name[0]=='h'&&name[1]>='1'&&name[1]<='6'&&name[2]=='\0') {
                    r_nl(c); c->cy+=6; c->cx=c->lm; c->hlevel=0;
                }
                else if (tag_eq(name,"b")||tag_eq(name,"strong")) { c->bold=false; }
                else if (tag_eq(name,"i")||tag_eq(name,"em"))      { c->fg=COL_TEXT; }
                else if (tag_eq(name,"a")) {
                    if (c->link && c->href[0] && g_nlinks < BROW_LINK_MAX) {
                        blink_t *bl = &g_links[g_nlinks++];
                        bl->x = c->lsx; bl->y = c->lsy;
                        bl->w = (c->lsy==c->cy) ? (c->cx-c->lsx) : (c->rm-c->lsx);
                        bl->h = c->cy+FH-c->lsy+4;
                        kstrncpy(bl->url, c->href, 191);
                    }
                    c->link=false; c->fg=COL_TEXT;
                }
                else if (tag_eq(name,"p")||tag_eq(name,"div")||
                         tag_eq(name,"article")||tag_eq(name,"section")||
                         tag_eq(name,"main")||tag_eq(name,"header")) {
                    if (c->cx>c->lm) r_nl(c);
                }
                else if (tag_eq(name,"ul")||tag_eq(name,"ol")||
                         tag_eq(name,"blockquote")) {
                    if (c->indent > 0) c->indent--;
                    c->lm = c->base_lm + c->indent*FW*3;
                    if (c->cx>c->lm) r_nl(c); else c->cx=c->lm;
                }
                else if (tag_eq(name,"pre")||tag_eq(name,"code")) {
                    c->pre=false; c->fg=COL_TEXT; r_nl(c);
                }
                else if (tag_eq(name,"table")) { c->in_table=false; if(c->cx>c->lm) r_nl(c); }
                else if (tag_eq(name,"td")||tag_eq(name,"th")) { if(tag_eq(name,"th")) c->bold=false; }
                else if (tag_eq(name,"form")) { c->in_form=false; }
            }
            continue;
        }

        /* HTML entities */
        if (*p == '&' && !c->skip) {
            const char *semi = p+1;
            while (*semi && *semi!=';' && (semi-p)<10) semi++;
            if (*semi == ';') {
                u32 el=(u32)(semi-p-1);
                char ent[10]={0};
                if (el<10) { kmemcpy(ent,p+1,el); ent[el]='\0'; }
                char ec=0;
                if (kstrcmp(ent,"amp")==0)   ec='&';
                else if (kstrcmp(ent,"lt")==0)    ec='<';
                else if (kstrcmp(ent,"gt")==0)    ec='>';
                else if (kstrcmp(ent,"quot")==0)  ec='"';
                else if (kstrcmp(ent,"apos")==0)  ec='\'';
                else if (kstrcmp(ent,"nbsp")==0)  ec=' ';
                else if (kstrcmp(ent,"mdash")==0||kstrcmp(ent,"ndash")==0) ec='-';
                else if (kstrcmp(ent,"copy")==0)  ec='c';
                else if (kstrcmp(ent,"reg")==0)   ec='R';
                else if (kstrcmp(ent,"laquo")==0) ec='<';
                else if (kstrcmp(ent,"raquo")==0) ec='>';
                else if (kstrcmp(ent,"hellip")==0) ec='.';
                else if (kstrcmp(ent,"trade")==0) ec='T';
                else if (kstrcmp(ent,"euro")==0)  ec='E';
                else if (ent[0]=='#'&&ent[1]>='0'&&ent[1]<='9') {
                    u32 n2=0;
                    for(u32 ei=1;ent[ei]>='0'&&ent[ei]<='9';ei++) n2=n2*10+(u32)(ent[ei]-'0');
                    if(n2>=32&&n2<127) ec=(char)n2;
                }
                if (ec && wi<127) word[wi++]=ec;
                p=semi+1; continue;
            }
        }

        /* whitespace */
        if (*p==' '||*p=='\t') {
            FLUSH();
            if (!c->skip && c->cx>c->lm) r_char(c,' ');
            p++; continue;
        }
        if (*p=='\n'||*p=='\r') {
            FLUSH();
            if (!c->skip && c->pre) r_nl(c);
            p++; continue;
        }

        /* regular char */
        if (!c->skip) {
            if (wi<127) word[wi++]=*p;
            else { FLUSH(); word[wi++]=*p; }
        }
        p++;
    }
    FLUSH();
#undef FLUSH
    return c->cy;
}

/* ── Navigation ──────────────────────────────────────────────────── */
/* Apply a raw HTTP(S) response to the window: redirects, chunked bodies (for
 * HTTPS too -- it was never de-chunked, so pages showed hex chunk sizes),
 * case-insensitive headers, title, content, status and history. */
static void browser_apply_response(window_t *w, const char *url, const char *host, int n) {
    g_resp[n] = '\0';
    int code = http_status_code(g_resp, (u32)n);
    if ((code == 301 || code == 302 || code == 303 || code == 307 || code == 308) &&
        w->browser_redirects < 6) {
        char loc[512];
        if (http_header(g_resp, (u32)n, "Location", loc, sizeof loc)) {
            char next[512]; http_url_resolve(url, loc, next, sizeof next);
            w->browser_redirects++;
            s_redirecting = true; app_browser_navigate(w, next); s_redirecting = false;
            return;                              /* still loading: the next hop */
        }
    }
    u32 off = http_body_offset(g_resp, (u32)n);
    char *body = g_resp + off;
    u32 blen = (u32)n - off;
    if (http_is_chunked(g_resp, (u32)n)) blen = http_dechunk(body, blen);
    if (blen >= BROW_CONT_MAX) blen = BROW_CONT_MAX - 1;
    extract_title(body, w->browser_title, 127);
    if (!w->browser_title[0]) kstrncpy(w->browser_title, host, 127);
    kmemcpy(w->browser_content, body, blen);
    w->browser_content[blen] = '\0';
    browser_save_tab(w);
    ksprintf(g_status, "%s  %d bytes%s", host, (int)blen,
             kstrncmp(url, "https://", 8) == 0 ? " (HTTPS)" : "");
    if (w->browser_push_history) { history_push(w, url); w->browser_push_history = false; }
    w->browser_loading = false;
}

static void app_browser_navigate(window_t *w, const char *url) {
    kstrncpy(w->browser_url, url, 255);
    w->browser_scroll = 0;
    if (!s_redirecting) { w->browser_redirects = 0; }

    /* about: pages need no network; also stop any fetch still in flight. */
    if (kstrncmp(url, "about:", 6) == 0) {
        browser_fetch_cancel(w->instance_id);
        w->browser_content[0] = '\0';
        kstrcpy(w->browser_title, "New Tab");
        kstrcpy(g_status, "about:home");
        w->browser_loading = false;
        return;
    }

    /* Hand the request to the fetch worker; app_browser_tick() applies the
     * result, so the desktop keeps running while the page loads. */
    char host[128] = {0};
    const char *h = url;
    if (!kstrncmp(url, "https://", 8)) h = url + 8;
    else if (!kstrncmp(url, "http://", 7)) h = url + 7;
    for (u32 i = 0; i < 127 && h[i] && h[i] != '/'; i++) host[i] = h[i];
    ksprintf(g_status, "Connecting to %s...", host);
    w->browser_loading = true;
    browser_fetch_submit(w->instance_id, w->browser_tab_sel, url);
}

/* smart dispatch: detect domain vs search query */
static void smart_navigate(window_t *w, const char *input) {
    if (!input || !*input) return;
    char url[512];
    w->browser_autohttps = false;

    if (kstrncmp(input,"http://",7)==0 || kstrncmp(input,"https://",8)==0
        || kstrncmp(input,"about:",6)==0) {
        kstrncpy(url, input, 511);
    } else {
        /* check if it looks like a domain (has dot, no spaces) */
        bool has_dot=false, has_space=false;
        for (const char *q=input; *q; q++) {
            if (*q=='.') has_dot=true;
            if (*q==' ') has_space=true;
        }
        if (has_dot && !has_space) {
            ksprintf(url, "https://%s", input);
            w->browser_autohttps = true;     /* fall back to http:// if it fails */
        } else {
            char enc[256];
            url_encode(input, enc, sizeof(enc));
            /* DuckDuckGo Lite: a JS-free, text-first results page built for
             * exactly this kind of browser. Served over HTTPS. */
            ksprintf(url, "https://lite.duckduckgo.com/lite/?q=%s", enc);
        }
    }

    w->browser_push_history = true;          /* recorded once the page arrives */
    app_browser_navigate(w, url);
}

/* ── Tab helpers ─────────────────────────────────────────────────── */
static void browser_save_tab(window_t *w) {
    u32 ts = w->browser_tab_sel;
    kstrncpy(w->browser_tab_url[ts],   w->browser_url,   255);
    kstrncpy(w->browser_tab_title[ts], w->browser_title,  47);
    w->browser_tab_scroll[ts] = w->browser_scroll;
}

static void browser_switch_tab(window_t *w, u32 idx) {
    if (w->browser_loading) { browser_fetch_cancel(w->instance_id); w->browser_loading = false; }
    if (idx >= w->browser_tab_count) return;
    browser_save_tab(w);
    w->browser_tab_sel = idx;
    kstrncpy(w->browser_url,   w->browser_tab_url[idx],   255);
    kstrncpy(w->browser_title, w->browser_tab_title[idx],  127);
    w->browser_scroll  = w->browser_tab_scroll[idx];
    /* Re-navigate so content is fresh */
    if (kstrncmp(w->browser_url, "about:", 6) != 0 && w->browser_url[0])
        app_browser_navigate(w, w->browser_url);
    else
        w->browser_content[0] = '\0';
}

static void browser_open_tab(window_t *w) {
    if (w->browser_tab_count >= 4) return;
    browser_save_tab(w);
    u32 ni = w->browser_tab_count++;
    kstrcpy(w->browser_tab_url[ni],   "about:home");
    kstrcpy(w->browser_tab_title[ni], "New Tab");
    w->browser_tab_scroll[ni] = 0;
    w->browser_tab_sel  = ni;
    kstrcpy(w->browser_url,   "about:home");
    kstrcpy(w->browser_title, "New Tab");
    w->browser_content[0]    = '\0';
    w->browser_scroll        = 0;
    w->browser_url_active    = true;
}

static void browser_close_tab(window_t *w, u32 idx) {
    if (w->browser_tab_count <= 1) return;
    for (u32 k = idx; k < w->browser_tab_count - 1; k++) {
        kstrncpy(w->browser_tab_url[k],   w->browser_tab_url[k+1],   255);
        kstrncpy(w->browser_tab_title[k], w->browser_tab_title[k+1],  47);
        w->browser_tab_scroll[k] = w->browser_tab_scroll[k+1];
    }
    w->browser_tab_count--;
    if (w->browser_tab_sel >= w->browser_tab_count)
        w->browser_tab_sel = w->browser_tab_count - 1;
    /* Load the now-active tab */
    u32 ts = w->browser_tab_sel;
    kstrncpy(w->browser_url,   w->browser_tab_url[ts],   255);
    kstrncpy(w->browser_title, w->browser_tab_title[ts], 127);
    w->browser_scroll = w->browser_tab_scroll[ts];
    if (kstrncmp(w->browser_url, "about:", 6) != 0 && w->browser_url[0])
        app_browser_navigate(w, w->browser_url);
    else
        w->browser_content[0] = '\0';
}

static void bookmarks_save(window_t *w) {
    char buf[2048]; u32 pos = 0;
    for (u32 bi = 0; bi < w->browser_bm_count; bi++) {
        const char *t = w->browser_bm_title[bi];
        const char *u = w->browser_bm_url[bi];
        u32 tl = (u32)kstrlen(t), ul = (u32)kstrlen(u);
        if (pos + tl + ul + 3 < 2047) {
            kmemcpy(buf+pos,t,tl); pos+=tl;
            buf[pos++]='|';
            kmemcpy(buf+pos,u,ul); pos+=ul;
            buf[pos++]='\n';
        }
    }
    buf[pos]='\0';
    char bm_path[64];
    ksprintf(bm_path, "/home/%s/.bookmarks", user_current_name());
    fs_node_t *f = vfs_resolve_path(bm_path);
    if (!f) {
        char home_path[48]; ksprintf(home_path, "/home/%s", user_current_name());
        fs_node_t *home = vfs_resolve_path(home_path);
        if (home) f = vfs_mkfile(home, ".bookmarks");
    }
    if (f) vfs_write(f, buf, pos);
}

/* ── Init ────────────────────────────────────────────────────────── */
void app_browser_init(window_t *w) {
    kstrcpy(w->browser_url,   "about:home");
    kstrcpy(w->browser_title, "New Tab");
    w->browser_content[0]    = '\0';
    w->browser_loading       = false;
    w->browser_url_active    = true;
    w->browser_scroll        = 0;
    w->browser_history_pos   = 0;
    w->browser_history_count = 0;
    /* Tabs */
    w->browser_tab_count = 1;
    w->browser_tab_sel   = 0;
    kstrcpy(w->browser_tab_url[0],   "about:home");
    kstrcpy(w->browser_tab_title[0], "New Tab");
    w->browser_tab_scroll[0] = 0;
    /* Bookmarks */
    w->browser_bm_count = 0;
    w->browser_bm_open  = false;
    /* Find */
    w->browser_find_active  = false;
    w->browser_find_len     = 0;
    w->browser_find_count   = 0;
    w->browser_find_cur     = 0;
    /* Source view */
    w->browser_source_view  = false;
    /* Load bookmarks from VFS */
    char bm_path[64];
    ksprintf(bm_path, "/home/%s/.bookmarks", user_current_name());
    fs_node_t *bmf = vfs_resolve_path(bm_path);
    if (bmf) {
        char buf[1024]; vfs_read(bmf, buf, sizeof(buf)-1); buf[1023]='\0';
        char *line = buf;
        while (*line && w->browser_bm_count < 8) {
            char *nl = kstrchr(line, '\n');
            if (nl) *nl = '\0';
            char *bar = kstrchr(line, '|');
            if (bar) {
                *bar = '\0';
                kstrncpy(w->browser_bm_title[w->browser_bm_count], line, 31);
                kstrncpy(w->browser_bm_url[w->browser_bm_count],   bar+1, 127);
                w->browser_bm_count++;
            }
            if (nl) line = nl+1; else break;
        }
    }
    kstrcpy(g_status, "Ready");
}

/* ── Home page ───────────────────────────────────────────────────── */
static void draw_home(window_t *w, rect_t cr) {
    i32 cy = cr.y + 32;

    gfx_str_centered(cr.x, cy, cr.w, "CareOS Browser", COL_TEXT, COL_TRANSPARENT);
    cy += FH + 6;
    gfx_str_centered(cr.x, cy, cr.w,
        "Type a URL or search in the bar above", COL_DIM, COL_TRANSPARENT);
    cy += FH + 20;

    gfx_hline(cr.x + cr.w/5, cy, cr.w*3/5, COL_BORDER);
    cy += 16;

    gfx_str(cr.x+20, cy, "Quick Links", COL_ACCENT, COL_TRANSPARENT);
    cy += FH + 10;

    const char *labels[] = {
        "DuckDuckGo Search", "example.com (HTTPS test)", "Wikipedia: CareOS", "info.cern.ch (first website)"
    };
    const char *urls[] = {
        "https://lite.duckduckgo.com/lite/?q=CareOS", "https://example.com",
        "https://en.wikipedia.org/wiki/CareOS", "http://info.cern.ch"
    };
    for (int i = 0; i < 4; i++) {
        i32 rh = FH + 12;
        gfx_rect_rounded(cr.x+16, cy, cr.w-32, rh, 6, COL_SURFACE2);
        gfx_rect_rounded_outline(cr.x+16, cy, cr.w-32, rh, 6, COL_BORDER);
        gfx_str(cr.x+28, cy+6, labels[i], COL_PRIMARY, COL_TRANSPARENT);
        if (g_nlinks < BROW_LINK_MAX) {
            g_links[g_nlinks].x = cr.x+16; g_links[g_nlinks].y = cy;
            g_links[g_nlinks].w = cr.w-32; g_links[g_nlinks].h = rh;
            kstrncpy(g_links[g_nlinks].url, urls[i], 191);
            g_nlinks++;
        }
        cy += rh + 6;
    }

    cy += 12;
    gfx_str_centered(cr.x, cy, cr.w,
        "HTTP ready. HTTPS is experimental.", COL_DIM, COL_TRANSPARENT);
    (void)w;
}

/* ── Draw ────────────────────────────────────────────────────────── */
static const char *browser_loading_text(void) {
    static char lb[64];
    static const char spin[] = "|/-\\";
    u32 el = browser_fetch_elapsed_ms();
    ksprintf(lb, "%c  Loading... %d.%d s   (Esc to stop)",
             spin[(timer_ms() / 120) % 4], (int)(el / 1000), (int)(el / 100 % 10));
    return lb;
}

void app_browser_draw(window_t *w) {
    rect_t cr = wm_client_rect(w);
    g_nlinks = 0;

    gfx_rect(cr.x, cr.y, cr.w, cr.h, COL_SURFACE);

    /* ── Tab strip ───────────────────────────────────────────────── */
    {
        gfx_rect(cr.x, cr.y, cr.w, BROW_TAB_H, COL_SURFACE3);
        gfx_hline(cr.x, cr.y + BROW_TAB_H - 1, cr.w, COL_BORDER);
        u32 ntabs = w->browser_tab_count < 1 ? 1 : w->browser_tab_count;
        i32 tab_w = (cr.w - 34) / (i32)ntabs;
        if (tab_w > 180) tab_w = 180;
        for (u32 ti = 0; ti < w->browser_tab_count; ti++) {
            i32 tx = cr.x + (i32)ti * (tab_w + 2);
            bool active = (ti == w->browser_tab_sel);
            gfx_rect(tx, cr.y, tab_w, BROW_TAB_H, active ? COL_SURFACE : COL_SURFACE2);
            if (active) gfx_hline(tx, cr.y + BROW_TAB_H - 2, tab_w, COL_PRIMARY);
            gfx_str_clipped(tx + 6, cr.y + (BROW_TAB_H - FH)/2, tab_w - 22,
                            w->browser_tab_title[ti], COL_TEXT, COL_TRANSPARENT);
            if (w->browser_tab_count > 1)
                gfx_str(tx + tab_w - 16, cr.y + (BROW_TAB_H - FH)/2,
                        "x", COL_MUTED, COL_TRANSPARENT);
        }
        i32 new_tab_x = cr.x + (i32)w->browser_tab_count * (tab_w + 2);
        if (w->browser_tab_count < 4)
            gfx_str(new_tab_x + 4, cr.y + (BROW_TAB_H - FH)/2, "+", COL_DIM, COL_TRANSPARENT);
        cr.y += BROW_TAB_H; cr.h -= BROW_TAB_H;
    }

    /* ── Toolbar ─────────────────────────────────────────────────── */
    i32 bar_h  = BROW_BAR_H;
    i32 btn_sz = bar_h - 12;   /* square button size */
    i32 btn_y  = cr.y + (bar_h - btn_sz) / 2;

    gfx_rect(cr.x, cr.y, cr.w, bar_h, COL_SURFACE2);
    gfx_hline(cr.x, cr.y + bar_h - 1, cr.w, COL_BORDER);

    /* Helper for a nav button */
#define NAV_BTN(bx, label, can) do { \
    bool hover_ = false; \
    gfx_rect_rounded((bx), btn_y, btn_sz, btn_sz, 6, (can) ? COL_SURFACE3 : COL_SURFACE2); \
    if (can) gfx_rect_rounded_outline((bx), btn_y, btn_sz, btn_sz, 6, COL_BORDER); \
    gfx_str_centered((bx), btn_y + (btn_sz - FH) / 2, btn_sz, (label), \
                     (can) ? COL_TEXT : COL_MUTED, COL_TRANSPARENT); \
    (void)hover_; } while(0)

    bool can_back = w->browser_history_pos > 0;
    bool can_fwd  = (w->browser_history_count > 0 &&
                     w->browser_history_pos+1 < w->browser_history_count);

    i32 bx = cr.x + 6;
    NAV_BTN(bx, "<", can_back);  bx += btn_sz + 4;
    NAV_BTN(bx, ">", can_fwd);   bx += btn_sz + 4;
    NAV_BTN(bx, "R", true);      bx += btn_sz + 4;
    NAV_BTN(bx, "H", true);      bx += btn_sz + 8;
#undef NAV_BTN

    /* URL bar (leave room for BM button + Go button) */
    i32 go_w  = 42;
    i32 bm_w  = BROW_BM_BTN;
    i32 url_x = bx;
    i32 url_w = cr.w - (bx - cr.x) - bm_w - 6 - go_w - 8;
    bool is_https = kstrncmp(w->browser_url, "https://", 8) == 0;
    bool is_http  = kstrncmp(w->browser_url, "http://",  7) == 0;

    gfx_rect_rounded(url_x, btn_y, url_w, btn_sz, 8, COL_INPUT_BG);
    gfx_rect_rounded_outline(url_x, btn_y, url_w, btn_sz, 8,
                             w->browser_url_active ? COL_PRIMARY : COL_BORDER);

    i32 txt_y  = btn_y + (btn_sz - FH) / 2;
    i32 icon_r = 7;
    /* Security indicator */
    if (is_https) {
        gfx_circle_fill(url_x + 12, btn_y + btn_sz/2, icon_r - 2, g_theme->success);
        gfx_str_clipped(url_x + 24, txt_y, url_w - 34, w->browser_url, g_theme->success, COL_TRANSPARENT);
    } else if (is_http) {
        gfx_circle_fill(url_x + 12, btn_y + btn_sz/2, icon_r - 2, COL_DIM);
        gfx_str_clipped(url_x + 24, txt_y, url_w - 34, w->browser_url, COL_ACCENT, COL_TRANSPARENT);
    } else {
        gfx_str_clipped(url_x + 10, txt_y, url_w - 20, w->browser_url, COL_ACCENT, COL_TRANSPARENT);
    }
    /* Text cursor */
    if (w->browser_url_active) {
        i32 cx_off = (is_https || is_http) ? 24 : 10;
        i32 cur_x  = url_x + cx_off + gfx_str_width(w->browser_url);
        if (cur_x < url_x + url_w - 4)
            gfx_rect(cur_x, btn_y + 4, 2, btn_sz - 8, COL_PRIMARY);
    }

    /* ★ Bookmarks button */
    i32 bm_x = url_x + url_w + 6;
    gfx_rect_rounded(bm_x, btn_y, bm_w, btn_sz, 6, COL_SURFACE3);
    gfx_rect_rounded_outline(bm_x, btn_y, bm_w, btn_sz, 6, COL_BORDER);
    gfx_str_centered(bm_x, btn_y + (btn_sz - FH) / 2, bm_w, "*", COL_ACCENT, COL_TRANSPARENT);

    /* Go button */
    i32 go_x = cr.x + cr.w - go_w - 4;
    gfx_rect_rounded(go_x, btn_y, go_w, btn_sz, 8, COL_PRIMARY);
    gfx_str_centered(go_x, btn_y + (btn_sz - FH) / 2, go_w, "Go", COL_WHITE, COL_TRANSPARENT);

    /* ── Find bar ────────────────────────────────────────────────── */
    i32 find_bar_h = w->browser_find_active ? BROW_FIND_H : 0;

    /* ── Status bar ──────────────────────────────────────────────── */
    i32 stat_y = cr.y + cr.h - BROW_STAT_H;
    gfx_rect(cr.x, stat_y, cr.w, BROW_STAT_H, COL_SURFACE2);
    gfx_hline(cr.x, stat_y, cr.w, COL_BORDER);
    gfx_str_clipped(cr.x + 10, stat_y + (BROW_STAT_H - FH) / 2, cr.w - 20,
                    g_status, COL_DIM, COL_TRANSPARENT);

    /* Draw find bar above status */
    if (w->browser_find_active) {
        i32 fb_y = stat_y - BROW_FIND_H;
        gfx_rect(cr.x, fb_y, cr.w, BROW_FIND_H, COL_SURFACE2);
        gfx_hline(cr.x, fb_y, cr.w, COL_BORDER);
        gfx_str(cr.x + 8, fb_y + (BROW_FIND_H - FH)/2, "Find:", COL_DIM, COL_TRANSPARENT);
        i32 fi_x = cr.x + 50, fi_w = cr.w - 140;
        gfx_rect_rounded(fi_x, fb_y + 4, fi_w, BROW_FIND_H - 8, 4, COL_INPUT_BG);
        gfx_rect_rounded_outline(fi_x, fb_y + 4, fi_w, BROW_FIND_H - 8, 4, COL_PRIMARY);
        gfx_str_clipped(fi_x + 4, fb_y + (BROW_FIND_H - FH)/2, fi_w - 8,
                        w->browser_find_buf, COL_TEXT, COL_TRANSPARENT);
        char cnt[24];
        ksprintf(cnt, "%u/%u",
                 w->browser_find_count ? w->browser_find_cur + 1 : 0,
                 w->browser_find_count);
        gfx_str(fi_x + fi_w + 6, fb_y + (BROW_FIND_H - FH)/2, cnt, COL_DIM, COL_TRANSPARENT);
    }

    /* ── Bookmark dropdown ───────────────────────────────────────── */
    if (w->browser_bm_open) {
        i32 bd_x = bm_x - 100;
        i32 bd_y = cr.y + bar_h;
        i32 bd_w = 150;
        i32 row_h = FH + 10;
        i32 bd_h  = (i32)w->browser_bm_count * row_h + 8;
        if (bd_h < 28) bd_h = 28;
        gfx_rect_rounded(bd_x, bd_y, bd_w, bd_h, 6, COL_SURFACE2);
        gfx_rect_rounded_outline(bd_x, bd_y, bd_w, bd_h, 6, COL_BORDER);
        if (w->browser_bm_count == 0) {
            gfx_str(bd_x + 8, bd_y + 8, "No bookmarks", COL_MUTED, COL_TRANSPARENT);
        }
        for (u32 bi = 0; bi < w->browser_bm_count; bi++) {
            i32 by = bd_y + 4 + (i32)bi * row_h;
            gfx_str_clipped(bd_x + 6, by + 4, bd_w - 26,
                            w->browser_bm_title[bi], COL_TEXT, COL_TRANSPARENT);
            gfx_str(bd_x + bd_w - 18, by + 4, "x", COL_MUTED, COL_TRANSPARENT);
        }
    }

    /* ── Content area ────────────────────────────────────────────── */
    i32 cont_y = cr.y + bar_h;
    i32 cont_h = cr.h - bar_h - BROW_STAT_H - find_bar_h;
    gfx_set_clip(cr.x, cont_y, cr.w, cont_h);

    if (w->browser_loading) {
        gfx_str_centered(cr.x, cont_y + cont_h/2 - FH/2, cr.w,
                         browser_loading_text(), COL_DIM, COL_TRANSPARENT);
        gfx_clear_clip();
        return;
    }

    if (kstrncmp(w->browser_url, "about:", 6)==0 || w->browser_content[0]=='\0') {
        draw_home(w, rect_make(cr.x, cont_y, cr.w, cont_h));
        gfx_clear_clip();
        return;
    }

    if (w->browser_source_view) {
        /* Raw content as preformatted text */
        rctx_t ctx = {0};
        ctx.cx = cr.x + 8; ctx.cy = cont_y + 8 - w->browser_scroll;
        ctx.base_lm = cr.x + 8; ctx.lm = cr.x + 8;
        ctx.rm = cr.x + cr.w - 8; ctx.ct = cont_y; ctx.cb = cont_y + cont_h;
        ctx.fg = COL_DIM; ctx.pre = true;
        render_html(&ctx, w->browser_content);
        gfx_clear_clip();
        return;
    }

    rctx_t ctx = {0};
    ctx.cx      = cr.x + 16;
    ctx.cy      = cont_y + 12 - w->browser_scroll;
    ctx.base_lm = cr.x + 16;
    ctx.lm      = cr.x + 16;
    ctx.rm      = cr.x + cr.w - 24;
    ctx.ct      = cont_y;
    ctx.cb      = cont_y + cont_h;
    ctx.fg      = COL_TEXT;

    i32 end_cy = render_html(&ctx, w->browser_content);

    /* Scrollbar */
    i32 total_h = end_cy - (cont_y + 12 - w->browser_scroll);
    if (total_h > cont_h) {
        i32 max_sc = total_h - cont_h;
        if (w->browser_scroll > max_sc) w->browser_scroll = max_sc;
        i32 sb_x = cr.x + cr.w - 7;
        i32 sb_h = (cont_h * cont_h) / (total_h > 1 ? total_h : 1);
        if (sb_h < 24) sb_h = 24;
        i32 rng  = cont_h - sb_h;
        i32 sb_y = cont_y + (max_sc > 0
            ? (i32)((i64)w->browser_scroll * rng / max_sc) : 0);
        gfx_rect(sb_x, cont_y, 7, cont_h, COL_SURFACE2);
        gfx_rect_rounded(sb_x + 1, sb_y, 5, sb_h, 3, COL_DIM);
    }

    gfx_clear_clip();
}

/* ── Input ───────────────────────────────────────────────────────── */
void app_browser_key(window_t *w, char c) {
    extern bool keyboard_ctrl_held(void);

    if (c == 27 && w->browser_loading && !w->browser_url_active && !w->browser_find_active) {
        browser_fetch_cancel(w->instance_id);
        w->browser_loading = false;
        kstrcpy(g_status, "Stopped");
        return;
    }

    /* Find bar mode */
    if (w->browser_find_active) {
        if (c == 27) { w->browser_find_active = false; return; }
        if (c == '\n') {
            if (w->browser_find_count > 0)
                w->browser_find_cur = (w->browser_find_cur + 1) % w->browser_find_count;
            return;
        }
        if (c == '\b' && w->browser_find_len > 0)
            w->browser_find_buf[--w->browser_find_len] = '\0';
        else if (c >= 32 && c < 127 && w->browser_find_len < 63) {
            w->browser_find_buf[w->browser_find_len++] = c;
            w->browser_find_buf[w->browser_find_len]   = '\0';
        }
        /* Recount */
        w->browser_find_count = 0; w->browser_find_cur = 0;
        u32 pl = (u32)kstrlen(w->browser_find_buf);
        u32 clen = (u32)kstrlen(w->browser_content);
        if (pl > 0) {
            for (u32 i = 0; i + pl <= clen; i++) {
                if (kstrncmp(w->browser_content + i, w->browser_find_buf, pl) == 0)
                    w->browser_find_count++;
            }
        }
        return;
    }

    /* Ctrl shortcuts. The control code itself (Ctrl+L = 12, ...) is the
     * evidence of Ctrl: keys are queued, and by the time this runs the user
     * may already have released Ctrl, which made keyboard_ctrl_held() drop
     * shortcuts. None of these codes can be typed any other way. */
    if (c == 4 || c == 6 || c == 12 || c == 18 || c == 20 || c == 21 || c == 23) {
        if (c == 6) { /* Ctrl+F */
            w->browser_find_active = true;
            w->browser_find_buf[0] = '\0'; w->browser_find_len = 0;
            w->browser_find_count  = 0; w->browser_find_cur = 0;
            return;
        }
        if (c == 4) { /* Ctrl+D — add bookmark */
            if (w->browser_bm_count < 8) {
                kstrncpy(w->browser_bm_title[w->browser_bm_count], w->browser_title, 31);
                kstrncpy(w->browser_bm_url[w->browser_bm_count],   w->browser_url,   127);
                w->browser_bm_count++;
                bookmarks_save(w);
                notify_push("Browser", "Bookmarked!", COL_GREEN);
            }
            return;
        }
        if (c == 20) { /* Ctrl+T — new tab */
            browser_open_tab(w);
            return;
        }
        if (c == 23) { /* Ctrl+W — close tab */
            browser_close_tab(w, w->browser_tab_sel);
            return;
        }
        if (c == 21) { /* Ctrl+U — source view */
            w->browser_source_view = !w->browser_source_view;
            return;
        }
        if (c == 12) { /* Ctrl+L — focus address bar with a fresh, empty URL */
            if (!w->browser_url_active) {
                kstrncpy(s_url_before_edit, w->browser_url, 255);
                s_url_before_edit[255] = '\0';
            }
            w->browser_url_active = true;
            w->browser_find_active = false;
            w->browser_url[0] = '\0';
            return;
        }
        if (c == 18) { /* Ctrl+R — reload */
            if (w->browser_url_active) {   /* reload what is loaded, not the draft */
                kstrncpy(w->browser_url, s_url_before_edit, 255);
                w->browser_url_active = false;
            }
            if (w->browser_url[0]) app_browser_navigate(w, w->browser_url);
            return;
        }
    }

    /* URL bar editing */
    if (w->browser_url_active) {
        if (c == '\n') { w->browser_url_active = false; smart_navigate(w, w->browser_url); return; }
        if (c == 27)   {   /* cancel: put back the page's URL if Ctrl+L cleared it */
            w->browser_url_active = false;
            if (!w->browser_url[0] && s_url_before_edit[0])
                kstrncpy(w->browser_url, s_url_before_edit, 255);
            return;
        }
        if (c == '\b') {
            u32 l = kstrlen(w->browser_url);
            if (l > 0) w->browser_url[l-1] = '\0';
            return;
        }
        if (c >= 32 && c < 127) {
            u32 l = kstrlen(w->browser_url);
            if (l < 254) { w->browser_url[l]=c; w->browser_url[l+1]='\0'; }
        }
        return;
    }

    /* Scroll */
    if (c == ' ' || c == '\n') w->browser_scroll += FH * 10;
    else if (c == '\b')        w->browser_scroll -= FH * 10;
    else if (c == 'j')         w->browser_scroll += FH * 3;
    else if (c == 'k')         w->browser_scroll -= FH * 3;
    else if ((u8)c == KEY_NAV_DOWN) w->browser_scroll += FH * 3;
    else if ((u8)c == KEY_NAV_UP)   w->browser_scroll -= FH * 3;
    else if ((u8)c == KEY_NAV_HOME) w->browser_scroll = 0;
    if (w->browser_scroll < 0) w->browser_scroll = 0;
}

void app_browser_scroll(window_t *w, i32 delta) {
    w->browser_scroll -= delta * FH * 3;
    if (w->browser_scroll < 0) w->browser_scroll = 0;
}

void app_browser_click(window_t *w, i32 x, i32 y) {
    rect_t cr = wm_client_rect(w);

    /* ── Tab strip ───────────────────────────────────────────────── */
    if (y >= cr.y && y < cr.y + BROW_TAB_H) {
        u32 ntabs = w->browser_tab_count < 1 ? 1 : w->browser_tab_count;
        i32 tab_w = (cr.w - 34) / (i32)ntabs;
        if (tab_w > 180) tab_w = 180;
        /* New-tab button */
        i32 new_tab_x = cr.x + (i32)w->browser_tab_count * (tab_w + 2);
        if (x >= new_tab_x && x < new_tab_x + 24 && w->browser_tab_count < 4) {
            browser_open_tab(w);
            return;
        }
        /* Tab click / close */
        for (u32 ti = 0; ti < w->browser_tab_count; ti++) {
            i32 tx = cr.x + (i32)ti * (tab_w + 2);
            if (x >= tx && x < tx + tab_w) {
                if (x >= tx + tab_w - 16 && w->browser_tab_count > 1)
                    browser_close_tab(w, ti);
                else if (ti != w->browser_tab_sel)
                    browser_switch_tab(w, ti);
                return;
            }
        }
        return;
    }
    cr.y += BROW_TAB_H; cr.h -= BROW_TAB_H;

    i32 bar_h  = BROW_BAR_H;
    i32 btn_sz = bar_h - 12;
    i32 btn_y  = cr.y + (bar_h - btn_sz) / 2;
    i32 cont_y = cr.y + bar_h;
    i32 cont_h = cr.h - bar_h - BROW_STAT_H - (w->browser_find_active ? BROW_FIND_H : 0);

    /* Close bookmarks dropdown on any click outside */
    if (w->browser_bm_open) {
        i32 bm_x2 = cr.x + cr.w - 42 - BROW_BM_BTN - 6;
        i32 bd_x  = bm_x2 - 100, bd_y = cr.y + bar_h, bd_w = 150;
        i32 bd_h  = (i32)w->browser_bm_count * (FH + 10) + 8;
        if (bd_h < 28) bd_h = 28;
        if (x >= bd_x && x < bd_x + bd_w && y >= bd_y && y < bd_y + bd_h) {
            /* Inside dropdown */
            i32 row_h = FH + 10;
            for (u32 bi = 0; bi < w->browser_bm_count; bi++) {
                i32 by = bd_y + 4 + (i32)bi * row_h;
                if (y >= by && y < by + row_h) {
                    if (x >= bd_x + bd_w - 18) {
                        for (u32 k = bi; k < w->browser_bm_count - 1; k++) {
                            kstrncpy(w->browser_bm_url[k],   w->browser_bm_url[k+1],   127);
                            kstrncpy(w->browser_bm_title[k], w->browser_bm_title[k+1],  31);
                        }
                        w->browser_bm_count--;
                        bookmarks_save(w);
                    } else {
                        smart_navigate(w, w->browser_bm_url[bi]);
                        w->browser_bm_open = false;
                    }
                    return;
                }
            }
        }
        w->browser_bm_open = false;
    }

    /* Toolbar buttons */
    i32 bx = cr.x + 6;
    /* back */
    if (y>=btn_y&&y<btn_y+btn_sz&&x>=bx&&x<bx+btn_sz) {
        if (w->browser_history_pos > 0) {
            w->browser_history_pos--;
            app_browser_navigate(w, w->browser_history[w->browser_history_pos]);
        }
        return;
    }
    bx += btn_sz + 4;
    /* forward */
    if (y>=btn_y&&y<btn_y+btn_sz&&x>=bx&&x<bx+btn_sz) {
        if (w->browser_history_pos+1 < w->browser_history_count) {
            w->browser_history_pos++;
            app_browser_navigate(w, w->browser_history[w->browser_history_pos]);
        }
        return;
    }
    bx += btn_sz + 4;
    /* refresh */
    if (y>=btn_y&&y<btn_y+btn_sz&&x>=bx&&x<bx+btn_sz) {
        w->browser_scroll = 0;
        app_browser_navigate(w, w->browser_url);
        return;
    }
    bx += btn_sz + 4;
    /* home */
    if (y>=btn_y&&y<btn_y+btn_sz&&x>=bx&&x<bx+btn_sz) {
        app_browser_navigate(w, "about:home");
        return;
    }
    bx += btn_sz + 8;

    /* URL + BM button + Go layout must match draw */
    i32 go_w  = 42;
    i32 bm_w  = BROW_BM_BTN;
    i32 url_x = bx;
    i32 url_w = cr.w - (bx - cr.x) - bm_w - 6 - go_w - 8;
    i32 bm_x  = url_x + url_w + 6;
    i32 go_x  = cr.x + cr.w - go_w - 4;

    /* ★ bookmark toggle */
    if (y>=btn_y&&y<btn_y+btn_sz&&x>=bm_x&&x<bm_x+bm_w) {
        w->browser_bm_open = !w->browser_bm_open;
        return;
    }
    /* go */
    if (y>=btn_y&&y<btn_y+btn_sz&&x>=go_x&&x<go_x+go_w) {
        w->browser_url_active = false;
        smart_navigate(w, w->browser_url);
        return;
    }
    /* URL bar */
    if (y>=btn_y&&y<btn_y+btn_sz&&x>=url_x&&x<url_x+url_w) {
        w->browser_url_active = true;
        return;
    }

    /* content area */
    if (y >= cont_y && y < cont_y+cont_h) {
        w->browser_url_active = false;
        w->browser_bm_open    = false;

        /* check link clicks */
        for (u32 i = 0; i < g_nlinks; i++) {
            blink_t *l = &g_links[i];
            if (x>=l->x&&x<l->x+l->w&&y>=l->y&&y<l->y+l->h) {
                /* Resolve relative / protocol-relative hrefs against this page. */
                char abs[512]; http_url_resolve(w->browser_url, l->url, abs, sizeof(abs));
                w->browser_push_history = true;
                w->browser_autohttps = false;
                app_browser_navigate(w, abs);
                return;
            }
        }

        /* scroll zones */
        i32 zone = cont_h * 2 / 5;
        if (y > cont_y + cont_h - zone)  w->browser_scroll += FH*4;
        else if (y < cont_y + zone)      w->browser_scroll -= FH*4;
        if (w->browser_scroll < 0) w->browser_scroll = 0;
    }
}

/* Called every GUI loop iteration for a browser window that is loading:
 * applies the finished fetch, or notices the fetch was superseded. */
void app_browser_tick(window_t *w) {
    if (!w->browser_loading) return;
    char url[512]; int n = 0;
    if (!browser_fetch_take(w->instance_id, w->browser_tab_sel, url, sizeof url, &n)) {
        if (!browser_fetch_busy(w->instance_id, w->browser_tab_sel)) {
            w->browser_loading = false;          /* another window/tab took the worker */
            kstrcpy(g_status, "Stopped");
        }
        return;
    }
    char host[128] = {0};
    const char *h = url;
    if (!kstrncmp(url, "https://", 8)) h = url + 8;
    else if (!kstrncmp(url, "http://", 7)) h = url + 7;
    for (u32 i = 0; i < 127 && h[i] && h[i] != '/'; i++) host[i] = h[i];
    if (n <= 0) {
        if (w->browser_autohttps && !kstrncmp(url, "https://", 8)) {
            /* Typed a bare domain and HTTPS failed: try plain HTTP. */
            char alt[512]; ksprintf(alt, "http://%s", url + 8);
            w->browser_autohttps = false;
            s_redirecting = true; app_browser_navigate(w, alt); s_redirecting = false;
            return;
        }
        /* g_net_diag describes the last TLS attempt; for plain HTTP use the
         * TCP layer's error so a fallback shows why IT failed. */
        extern char g_net_diag[];
        const char *err = net_last_error();
        bool tls = !kstrncmp(url, "https://", 8);
        const char *why = (tls && g_net_diag[0]) ? g_net_diag
                        : (err && err[0]) ? err : "No response (timeout)";
        ksprintf(w->browser_content, "<h1>Connection Failed</h1><p>Could not load %s.</p><p>%s</p>",
                 host, why);
        kstrcpy(w->browser_title, "Error");
        ksprintf(g_status, "Failed: %s", host);
        w->browser_loading = false;
        w->browser_push_history = false;
        return;
    }
    if ((u32)n >= browser_fetch_buffer_size()) n = (int)browser_fetch_buffer_size() - 1;
    browser_apply_response(w, url, host, n);
}

void app_browser_open_url(window_t *w, const char *url) { smart_navigate(w, url); }
