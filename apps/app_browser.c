/* CareOS v9 -- apps/app_browser.c -- Web Browser */
#include "apps_common.h"
#include "http_parse.h"
#include "browser_fetch.h"
#include "font.h"
#include "ui.h"
#include "../web/engine/web.h"

/* ── Layout ──────────────────────────────────────────────────────── */
#define BROW_BAR_H    56
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

/* ── Native engine (Weave) glue ──────────────────────────────────────
 * Pages are built by the fetch task (browser_fetch.c); the GUI only lays
 * out (on resize / image arrival) and paints. Pages go back to the fetch
 * module for freeing because its task may still be loading their images. */
static void browser_engine_drop(window_t *w) {
    if (w->browser_page) {
        browser_fetch_release_page(w->browser_page);
        w->browser_page = NULL;
    }
    w->bview_valid = false;
}

/* Viewport for page layout: the last painted content area, or a default. */
static void browser_viewport(window_t *w, i32 *vw, i32 *vh) {
    *vw = w->bview_w > 64 ? w->bview_w : 1024;
    *vh = w->bview_h > 64 ? w->bview_h : 700;
}

/* Install a page built from local HTML (error pages). It must not reference
 * external resources: this runs on the GUI task. */
static void browser_engine_load(window_t *w, const char *url, const char *html, u32 len) {
    browser_engine_drop(w);
    browser_engine_init();
    i32 vw, vh; browser_viewport(w, &vw, &vh);
    w->browser_page = web_page_load(url, html, len, vw, vh);
    if (w->browser_page && web_page_title(w->browser_page)[0])
        kstrncpy(w->browser_title, web_page_title(w->browser_page), 127);
}

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
static void app_browser_navigate(window_t *w, const char *url) {
    browser_engine_drop(w);
    kstrncpy(w->browser_url, url, 255);
    w->browser_scroll = 0;
    if (!s_redirecting) { w->browser_redirects = 0; }

    /* about: pages need no network; also stop any fetch still in flight. */
    if (kstrncmp(url, "about:", 6) == 0) {
        browser_fetch_cancel(w->instance_id);
        w->browser_content[0] = '\0';
        w->browser_content_ver++;
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
    i32 vw, vh; browser_viewport(w, &vw, &vh);
    browser_fetch_set_viewport(vw, vh);
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
/* Keep a copy of the current page with tab ts (nothing while it is still
 * loading or for about: pages). If the copy cannot be allocated the tab is
 * simply marked unloaded and will fetch again -- pages are never mixed. */
static void tab_store_content(window_t *w, u32 ts) {
    if (w->browser_tab_content[ts]) { kfree(w->browser_tab_content[ts]); w->browser_tab_content[ts] = NULL; }
    w->browser_tab_loaded[ts] = false;
    w->browser_tab_len[ts] = 0;
    if (w->browser_loading || !kstrncmp(w->browser_url, "about:", 6) || !w->browser_url[0]) return;
    u32 len = (u32)kstrlen(w->browser_content);
    char *c = (char *)kmalloc(len + 1);
    if (!c) return;
    kmemcpy(c, w->browser_content, len + 1);
    w->browser_tab_content[ts] = c;
    w->browser_tab_len[ts] = len;
    w->browser_tab_loaded[ts] = true;          /* an empty page is still a loaded page */
}

/* Show tab ts: its kept page if it has one, otherwise load it. */
static void tab_show(window_t *w, u32 ts) {
    kstrncpy(w->browser_url,   w->browser_tab_url[ts],   255);
    kstrncpy(w->browser_title, w->browser_tab_title[ts], 127);
    w->browser_scroll = w->browser_tab_scroll[ts];
    w->browser_content_ver++;
    /* Pages are rebuilt by the fetch task (stylesheets come from its cache),
     * never on the GUI task. */
    if (kstrncmp(w->browser_url, "about:", 6) != 0 && w->browser_url[0]) {
        i32 keep_scroll = w->browser_scroll;
        app_browser_navigate(w, w->browser_url);
        w->browser_scroll = keep_scroll;
    } else {
        w->browser_content[0] = '\0';
        w->browser_loading = false;
    }
}

static void browser_save_tab(window_t *w) {
    u32 ts = w->browser_tab_sel;
    kstrncpy(w->browser_tab_url[ts],   w->browser_url,   255);
    kstrncpy(w->browser_tab_title[ts], w->browser_title,  47);
    w->browser_tab_scroll[ts] = w->browser_scroll;
    tab_store_content(w, ts);
}

static void browser_switch_tab(window_t *w, u32 idx) {
    if (idx >= w->browser_tab_count || idx == w->browser_tab_sel) return;
    if (w->browser_loading) { browser_fetch_cancel(w->instance_id); w->browser_loading = false; }
    browser_save_tab(w);
    w->browser_tab_sel = idx;
    tab_show(w, idx);
}

static void browser_open_tab(window_t *w) {
    if (w->browser_tab_count >= 4) return;
    if (w->browser_loading) { browser_fetch_cancel(w->instance_id); w->browser_loading = false; }
    browser_save_tab(w);
    u32 ni = w->browser_tab_count++;
    kstrcpy(w->browser_tab_url[ni],   "about:home");
    kstrcpy(w->browser_tab_title[ni], "New Tab");
    w->browser_tab_scroll[ni] = 0;
    w->browser_tab_loaded[ni] = false;
    w->browser_tab_sel  = ni;
    kstrcpy(w->browser_url,   "about:home");
    kstrcpy(w->browser_title, "New Tab");
    w->browser_content[0]    = '\0';
    w->browser_content_ver++;
    w->browser_scroll        = 0;
    w->browser_url_active    = true;
}

static void browser_close_tab(window_t *w, u32 idx) {
    if (w->browser_tab_count <= 1 || idx >= w->browser_tab_count) return;
    if (idx == w->browser_tab_sel && w->browser_loading) {
        browser_fetch_cancel(w->instance_id); w->browser_loading = false;
    }
    if (idx != w->browser_tab_sel) browser_save_tab(w);   /* keep the page we stay on */
    if (w->browser_tab_content[idx]) { kfree(w->browser_tab_content[idx]); w->browser_tab_content[idx] = NULL; }
    for (u32 k = idx; k < w->browser_tab_count - 1; k++) {
        kstrncpy(w->browser_tab_url[k],   w->browser_tab_url[k+1],   255);
        kstrncpy(w->browser_tab_title[k], w->browser_tab_title[k+1],  47);
        w->browser_tab_scroll[k]  = w->browser_tab_scroll[k+1];
        w->browser_tab_content[k] = w->browser_tab_content[k+1];
        w->browser_tab_len[k]     = w->browser_tab_len[k+1];
        w->browser_tab_loaded[k]  = w->browser_tab_loaded[k+1];
    }
    u32 last = w->browser_tab_count - 1;
    w->browser_tab_content[last] = NULL; w->browser_tab_loaded[last] = false; w->browser_tab_len[last] = 0;
    w->browser_tab_count--;
    if (w->browser_tab_sel > idx || w->browser_tab_sel >= w->browser_tab_count)
        w->browser_tab_sel = (w->browser_tab_sel > 0) ? w->browser_tab_sel - 1 : 0;
    if (w->browser_tab_sel >= w->browser_tab_count) w->browser_tab_sel = w->browser_tab_count - 1;
    tab_show(w, w->browser_tab_sel);
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
    w->browser_page         = NULL;
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

/* ── v10 chrome state (w->app_state) ─────────────────────────────── */
enum { BA_NONE = 0, BA_TAB, BA_TAB_CLOSE, BA_NEW_TAB, BA_BACK, BA_FWD, BA_RELOAD, BA_URL,
       BA_BOOKMARK, BA_MENU, BA_M_FIND, BA_M_SOURCE, BA_M_HOME, BA_M_BM, BA_M_BM_DEL,
       BA_HOME_SEARCH, BA_ADD_SITE };
typedef struct { rect_t r; u8 act; i32 arg; } bhit_t;
typedef struct {
    bhit_t tb[12];   u32 ntb;      /* title-bar tabs */
    bhit_t hits[40]; u32 nhits;    /* toolbar, menu, home page */
    bool   menu_open;
} bstate_t;

static bstate_t *bst(window_t *w) {
    if (!w->app_state) {
        bstate_t *s = (bstate_t *)kmalloc(sizeof(bstate_t));
        if (!s) return NULL;
        kmemset(s, 0, sizeof *s);
        w->app_state = s;
    }
    return (bstate_t *)w->app_state;
}
static void bhit(bstate_t *s, rect_t r, u8 act, i32 arg) {
    if (s && s->nhits < 40) { s->hits[s->nhits].r = r; s->hits[s->nhits].act = act; s->hits[s->nhits].arg = arg; s->nhits++; }
}
static i32 ulh(u32 slot) { return gfx_role_line_h(FONT_ROLE_UI, slot); }

/* Chrome colours (the page itself keeps the renderer's colours). */
typedef struct { u32 page, strip, field, line, ink, ink2, muted; bool dark; } bpal_t;
static bpal_t bpal(void) {
    bpal_t p;
    p.dark = g_theme->is_dark;
    p.page  = p.dark ? rgb(0x12, 0x17, 0x29) : rgb(0xff, 0xff, 0xff);
    p.strip = p.dark ? rgb(0x0c, 0x10, 0x20) : rgb(0xee, 0xf0, 0xf6);
    p.field = p.dark ? rgb(0x1a, 0x20, 0x38) : rgb(0xf1, 0xf3, 0xf8);
    p.line  = p.dark ? rgb(0x1f, 0x25, 0x3d) : rgb(0xe1, 0xe4, 0xee);
    p.ink = g_theme->ink; p.ink2 = g_theme->ink2; p.muted = g_theme->muted;
    return p;
}

/* Host part of a URL ("" for about: pages). */
static void url_host(const char *url, char *out, u32 cap) {
    const char *h = url;
    out[0] = '\0';
    if (!kstrncmp(url, "https://", 8)) h = url + 8;
    else if (!kstrncmp(url, "http://", 7)) h = url + 7;
    else return;
    u32 i = 0;
    while (h[i] && h[i] != '/' && i + 1 < cap) { out[i] = h[i]; i++; }
    out[i] = '\0';
}

static const u32 FAV_COLS[6] = {
    rgb(0x8e,0x4d,0xf0), rgb(0x1f,0xa5,0x62), rgb(0x12,0x90,0xc2),
    rgb(0xe2,0x50,0x8f), rgb(0xf2,0x78,0x2e), rgb(0x4f,0x5e,0xf2)
};
static u32 fav_col(const char *s) { u32 h = 0; for (; *s; s++) h = h * 31u + (u8)*s; return FAV_COLS[h % 6u]; }

/* A lettered square for a site (the care heart for New Tab). */
static void favicon(i32 x, i32 y, i32 sz, const char *url, const char *title) {
    char host[96]; url_host(url, host, sizeof host);
    if (!host[0]) { ui_care_logo(x, y, sz); return; }
    const char *src = host;
    if (!kstrncmp(src, "www.", 4)) src += 4;
    char l[2] = { src[0] ? src[0] : (title[0] ? title[0] : '?'), '\0' };
    if (l[0] >= 'a' && l[0] <= 'z') l[0] = (char)(l[0] - 32);
    gfx_rect_rounded(x, y, sz, sz, sz / 4, fav_col(src));
    u32 slot = sz >= 40 ? 1 : 0;
    i32 tw = gfx_str_role_width(l, FONT_ROLE_DISPLAY, slot, FONT_W_BOLD);
    i32 th = gfx_role_line_h(FONT_ROLE_DISPLAY, slot);
    gfx_str_role(x + (sz - tw) / 2, y + (sz - th) / 2, l, rgb(0xff, 0xff, 0xff), FONT_ROLE_DISPLAY, slot, FONT_W_BOLD);
}

static bool is_bookmarked(window_t *w, i32 *idx) {
    for (u32 i = 0; i < w->browser_bm_count; i++)
        if (!kstrcmp(w->browser_bm_url[i], w->browser_url)) { if (idx) *idx = (i32)i; return true; }
    return false;
}
static void bookmark_remove(window_t *w, u32 bi) {
    if (bi >= w->browser_bm_count) return;
    for (u32 k = bi; k + 1 < w->browser_bm_count; k++) {
        kstrncpy(w->browser_bm_url[k],   w->browser_bm_url[k+1],   127);
        kstrncpy(w->browser_bm_title[k], w->browser_bm_title[k+1],  31);
    }
    w->browser_bm_count--;
    bookmarks_save(w);
}
static void bookmark_toggle(window_t *w) {
    i32 i;
    if (!kstrncmp(w->browser_url, "about:", 6) || !w->browser_url[0]) return;
    if (is_bookmarked(w, &i)) { bookmark_remove(w, (u32)i); notify_push("Browser", "Bookmark removed", COL_ACCENT); return; }
    if (w->browser_bm_count >= 8) { notify_push("Browser", "8 bookmarks max -- remove one from the menu first", g_theme->warning); return; }
    kstrncpy(w->browser_bm_title[w->browser_bm_count], w->browser_title[0] ? w->browser_title : w->browser_url, 31);
    w->browser_bm_title[w->browser_bm_count][31] = '\0';
    kstrncpy(w->browser_bm_url[w->browser_bm_count], w->browser_url, 127);
    w->browser_bm_url[w->browser_bm_count][127] = '\0';
    w->browser_bm_count++;
    bookmarks_save(w);
    notify_push("Browser", "Bookmarked -- it's on the New Tab page", g_theme->success);
}

static void begin_url_edit(window_t *w, bool clear) {
    if (!w->browser_url_active) { kstrncpy(s_url_before_edit, w->browser_url, 255); s_url_before_edit[255] = '\0'; }
    w->browser_url_active = true;
    w->browser_find_active = false;
    if (clear || !kstrncmp(w->browser_url, "about:", 6)) w->browser_url[0] = '\0';
}

/* ── Title-bar tabs ──────────────────────────────────────────────── */
bool app_browser_toolbar(window_t *w, rect_t bar) {
    bstate_t *s = bst(w);
    if (!s) return false;
    bpal_t p = bpal();
    s->ntb = 0;
    u32 n = w->browser_tab_count < 1 ? 1 : w->browser_tab_count;
    i32 plus = w->browser_tab_count < 4 ? 40 : 0;
    i32 tw = (bar.w - plus) / (i32)n;
    if (tw > 220) tw = 220;
    if (tw < 90) tw = 90;
    i32 x = bar.x, ty = bar.y + 10, th = bar.h - 10;
    for (u32 i = 0; i < w->browser_tab_count && x + tw <= bar.x + bar.w - plus; i++) {
        bool on = i == w->browser_tab_sel;
        const char *url = on ? w->browser_url : w->browser_tab_url[i];
        const char *title = on ? w->browser_title : w->browser_tab_title[i];
        if (on && w->browser_url_active) url = s_url_before_edit[0] ? s_url_before_edit : "about:home";
        if (on) gfx_rect_rounded(x, ty, tw, th + 14, 12, p.page);
        favicon(x + 12, ty + (th - 16) / 2, 16, url, title);
        bool can_close = w->browser_tab_count > 1;
        i32 lw = tw - 12 - 16 - 8 - (can_close ? 30 : 10);
        const char *label = (on && w->browser_loading) ? "Loading..." : (title[0] ? title : "New Tab");
        gfx_str_role_fit(x + 36, ty + (th - ulh(1)) / 2, lw, label, on ? p.ink : p.ink2, FONT_ROLE_UI, 1, on ? FONT_W_BOLD : FONT_W_REGULAR);
        if (can_close) {
            ui_glyph(UI_GLYPH_CLOSE, x + tw - 26, ty + (th - 12) / 2, 12, p.muted);
            s->tb[s->ntb].r = rect_make(x + tw - 32, ty, 28, th); s->tb[s->ntb].act = BA_TAB_CLOSE; s->tb[s->ntb].arg = (i32)i; s->ntb++;
        }
        s->tb[s->ntb].r = rect_make(x, ty, tw, th); s->tb[s->ntb].act = BA_TAB; s->tb[s->ntb].arg = (i32)i; s->ntb++;
        x += tw + 4;
    }
    if (plus) {
        ui_glyph(UI_GLYPH_PLUS, x + 8, ty + (th - 16) / 2, 16, p.ink2);
        s->tb[s->ntb].r = rect_make(x, ty, 32, th); s->tb[s->ntb].act = BA_NEW_TAB; s->tb[s->ntb].arg = 0; s->ntb++;
    }
    return true;
}

bool app_browser_toolbar_click(window_t *w, i32 x, i32 y) {
    bstate_t *s = bst(w);
    if (!s) return false;
    for (u32 i = 0; i < s->ntb; i++) {
        const bhit_t *h = &s->tb[i];
        if (!rect_contains(h->r, x, y)) continue;
        s->menu_open = false;
        if (h->act == BA_TAB_CLOSE) browser_close_tab(w, (u32)h->arg);
        else if (h->act == BA_TAB) browser_switch_tab(w, (u32)h->arg);
        else if (h->act == BA_NEW_TAB) browser_open_tab(w);
        return true;
    }
    return false;
}

/* ── New Tab page ────────────────────────────────────────────────── */
static void draw_home(window_t *w, rect_t cr) {
    bstate_t *s = bst(w);
    bpal_t p = bpal();
    gfx_rect(cr.x, cr.y, cr.w, cr.h, p.page);
    i32 cy = cr.y + (cr.h > 640 ? 64 : 28);
    const char *title = "Where to?";
    i32 tw = gfx_str_role_width(title, FONT_ROLE_DISPLAY, 3, FONT_W_BOLD);
    gfx_str_role(cr.x + (cr.w - tw) / 2, cy, title, p.ink, FONT_ROLE_DISPLAY, 3, FONT_W_BOLD);
    cy += gfx_role_line_h(FONT_ROLE_DISPLAY, 3) + 24;

    /* big search field: the same input as the address bar */
    i32 sw = cr.w - 80 < 640 ? cr.w - 80 : 640, sh = 58;
    i32 sx = cr.x + (cr.w - sw) / 2;
    gfx_shadow_soft(sx, cy, sw, sh, 20);
    gfx_rect_rounded(sx, cy, sw, sh, 20, p.field);
    gfx_rect_rounded_outline(sx, cy, sw, sh, 20, w->browser_url_active ? g_theme->care : p.line);
    ui_glyph(UI_GLYPH_SEARCH, sx + 20, cy + 19, 20, p.ink2);
    const char *q = (w->browser_url_active && kstrncmp(w->browser_url, "about:", 6)) ? w->browser_url : "";
    i32 qy = cy + (sh - ulh(3)) / 2;
    i32 bw = gfx_str_role_width("Search", FONT_ROLE_UI, 2, FONT_W_BOLD) + 36;
    if (q[0]) gfx_str_role_fit(sx + 52, qy, sw - 52 - bw - 20, q, p.ink, FONT_ROLE_UI, 3, FONT_W_REGULAR);
    else gfx_str_role_fit(sx + 52, qy, sw - 52 - bw - 20, "Search the web or type an address", p.muted, FONT_ROLE_UI, 3, FONT_W_REGULAR);
    if (w->browser_url_active && ((timer_ms() / 500) & 1) == 0) {
        i32 cx = sx + 52 + (q[0] ? gfx_str_role_width(q, FONT_ROLE_UI, 3, FONT_W_REGULAR) : 0);
        if (cx < sx + sw - bw - 16) gfx_rect(cx, qy, 2, ulh(3), g_theme->care);
    }
    gfx_rect_rounded(sx + sw - 8 - bw, cy + 8, bw, sh - 16, 14, g_theme->care);
    gfx_str_role(sx + sw - 8 - bw + 18, cy + (sh - ulh(2)) / 2, "Search", g_theme->on_care, FONT_ROLE_UI, 2, FONT_W_BOLD);
    bhit(s, rect_make(sx, cy, sw - bw - 16, sh), BA_HOME_SEARCH, 0);
    bhit(s, rect_make(sx + sw - 8 - bw, cy + 8, bw, sh - 16), BA_HOME_SEARCH, 1);
    cy += sh + 30;

    /* speed dial: real bookmarks, then Add site */
    u32 nt = w->browser_bm_count + 1;
    i32 tile = 108, gap = 14;
    i32 cols = (cr.w - 40 + gap) / (tile + gap); if (cols > 6) cols = 6; if (cols < 1) cols = 1;
    if ((u32)cols > nt) cols = (i32)nt;
    i32 gx = cr.x + (cr.w - (cols * tile + (cols - 1) * gap)) / 2;
    for (u32 i = 0; i < nt; i++) {
        i32 tx = gx + (i32)(i % (u32)cols) * (tile + gap), ty = cy + (i32)(i / (u32)cols) * (tile + gap);
        gfx_rect_rounded(tx, ty, tile, tile, 20, p.field);
        if (!p.dark) gfx_rect_rounded_outline(tx, ty, tile, tile, 20, p.line);
        if (i < w->browser_bm_count) {
            favicon(tx + (tile - 44) / 2, ty + 18, 44, w->browser_bm_url[i], w->browser_bm_title[i]);
            gfx_str_role_fit(tx + 10, ty + 18 + 44 + 10, tile - 20, w->browser_bm_title[i], p.ink, FONT_ROLE_UI, 1, FONT_W_BOLD);
            if (g_nlinks < BROW_LINK_MAX) {
                g_links[g_nlinks].x = tx; g_links[g_nlinks].y = ty; g_links[g_nlinks].w = tile; g_links[g_nlinks].h = tile;
                kstrncpy(g_links[g_nlinks].url, w->browser_bm_url[i], 191); g_nlinks++;
            }
        } else {
            for (i32 d = 0; d < tile - 24; d += 8) {                  /* dashed ring */
                gfx_hline(tx + 12 + d, ty + 1, 4, p.muted); gfx_hline(tx + 12 + d, ty + tile - 2, 4, p.muted);
                gfx_vline(tx + 1, ty + 12 + d, 4, p.muted); gfx_vline(tx + tile - 2, ty + 12 + d, 4, p.muted);
            }
            ui_glyph(UI_GLYPH_PLUS, tx + (tile - 24) / 2, ty + 28, 24, p.ink2);
            i32 aw = gfx_str_role_width("Add site", FONT_ROLE_UI, 1, FONT_W_BOLD);
            gfx_str_role(tx + (tile - aw) / 2, ty + 18 + 44 + 10, "Add site", p.ink2, FONT_ROLE_UI, 1, FONT_W_BOLD);
            bhit(s, rect_make(tx, ty, tile, tile), BA_ADD_SITE, 0);
        }
    }
    cy += (i32)((nt + (u32)cols - 1) / (u32)cols) * (tile + gap) + 16;

    /* recent pages + privacy */
    i32 cw = cr.w - 80 < 720 ? cr.w - 80 : 720;
    i32 cx0 = cr.x + (cr.w - cw) / 2;
    bool recent = w->browser_history_count > 0;
    i32 colw = recent ? (cw - 14) / 2 : cw, ch = 120;
    if (recent) {
        gfx_rect_rounded(cx0, cy, colw, ch, 20, p.field);
        gfx_str_role(cx0 + 18, cy + 18, "PICK UP WHERE YOU LEFT OFF", p.muted, FONT_ROLE_UI, 0, FONT_W_BOLD);
        i32 ry = cy + 18 + ulh(0) + 12;
        for (u32 k = 0; k < 2 && k < w->browser_history_count; k++) {
            const char *u = w->browser_history[w->browser_history_count - 1 - k];
            char host[96]; url_host(u, host, sizeof host);
            const char *path = u; if (!kstrncmp(u, "https://", 8)) path = u + 8; else if (!kstrncmp(u, "http://", 7)) path = u + 7;
            gfx_circle_fill(cx0 + 22, ry + ulh(2) / 2, 4, fav_col(host[0] ? host : u));
            gfx_str_role_fit(cx0 + 34, ry, colw - 52, path, p.ink, FONT_ROLE_UI, 2, FONT_W_BOLD);
            if (g_nlinks < BROW_LINK_MAX) {
                g_links[g_nlinks].x = cx0 + 12; g_links[g_nlinks].y = ry - 4; g_links[g_nlinks].w = colw - 24; g_links[g_nlinks].h = ulh(2) + 8;
                kstrncpy(g_links[g_nlinks].url, u, 191); g_nlinks++;
            }
            ry += ulh(2) + 12;
        }
    }
    i32 px = recent ? cx0 + colw + 14 : cx0;
    gfx_rect_rounded(px, cy, colw, ch, 20, p.field);
    gfx_str_role(px + 18, cy + 18, "PRIVACY", p.muted, FONT_ROLE_UI, 0, FONT_W_BOLD);
    gfx_str_role_wrap(px + 18, cy + 18 + ulh(0) + 10, colw - 36,
                      "History stays in this window and is cleared when you close it. Bookmarks are saved on this computer.",
                      p.ink2, FONT_ROLE_UI, 1, FONT_W_REGULAR, 3);
}

/* ── Draw ────────────────────────────────────────────────────────── */
static const char *browser_loading_text(void) {
    static char lb[64];
    u32 el = browser_fetch_elapsed_ms();
    ksprintf(lb, "Loading... %d.%d s  -  Esc stops", (int)(el / 1000), (int)(el / 100 % 10));
    return lb;
}

static bool s_draw_cached;

/* Keep this window's link boxes (with the window position they were drawn
 * at). g_links is shared by every browser window, so clicks used to follow
 * whichever browser window drew last. */
static void browser_save_links(window_t *w) {
    if (!w->browser_links) w->browser_links = kmalloc(sizeof(blink_t) * BROW_LINK_MAX);
    if (!w->browser_links) { w->browser_nlinks = 0; return; }
    kmemcpy(w->browser_links, g_links, sizeof(blink_t) * g_nlinks);
    w->browser_nlinks = g_nlinks;
    w->browser_links_wx = w->rect.x; w->browser_links_wy = w->rect.y;
}

static void browser_draw_impl(window_t *w);

void app_browser_draw(window_t *w) {
    s_draw_cached = false;
    browser_draw_impl(w);
    if (!s_draw_cached) browser_save_links(w);
}

/* A round toolbar button with a glyph. */
static void tb_button(bstate_t *s, const bpal_t *p, i32 x, i32 y, ui_glyph_t g, bool enabled, u8 act) {
    ui_glyph(g, x + 10, y + 10, 18, enabled ? p->ink : p->muted);
    if (enabled) bhit(s, rect_make(x, y, 38, 38), act, 0);
}

static void browser_draw_impl(window_t *w) {
    rect_t cr = wm_client_rect(w);
    bstate_t *s = bst(w);
    bpal_t p = bpal();
    g_nlinks = 0;
    if (s) s->nhits = 0;

    gfx_rect(cr.x, cr.y, cr.w, cr.h, p.page);

    /* ── Toolbar: back, forward, reload, address pill, menu ──────── */
    i32 bar_h = BROW_BAR_H;
    i32 by = cr.y + (bar_h - 38) / 2;
    bool can_back = w->browser_history_pos > 0;
    bool can_fwd  = (w->browser_history_count > 0 && w->browser_history_pos + 1 < w->browser_history_count);
    i32 bx = cr.x + 12;
    tb_button(s, &p, bx, by, UI_GLYPH_CHEVRON_L, can_back, BA_BACK); bx += 40;
    tb_button(s, &p, bx, by, UI_GLYPH_CHEVRON_R, can_fwd, BA_FWD);   bx += 40;
    tb_button(s, &p, bx, by, UI_GLYPH_RESTART, kstrncmp(w->browser_url, "about:", 6) != 0, BA_RELOAD); bx += 46;
    i32 menu_x = cr.x + cr.w - 12 - 38;
    /* menu "..." */
    for (i32 d = 0; d < 3; d++) gfx_circle_fill(menu_x + 12 + d * 7, by + 19, 2, p.ink);
    bhit(s, rect_make(menu_x, by, 38, 38), BA_MENU, 0);

    i32 ux = bx, uw = menu_x - 8 - bx, uh = 40, uy = cr.y + (bar_h - uh) / 2;
    gfx_rect_rounded(ux, uy, uw, uh, 14, p.field);
    if (w->browser_url_active) gfx_rect_rounded_outline(ux, uy, uw, uh, 14, g_theme->care);
    bool https = !kstrncmp(w->browser_url, "https://", 8);
    bool http  = !kstrncmp(w->browser_url, "http://", 7);
    bool about = !kstrncmp(w->browser_url, "about:", 6);
    ui_glyph(w->browser_url_active || about ? UI_GLYPH_SEARCH : https ? UI_GLYPH_LOCK : UI_GLYPH_WIFI,
             ux + 12, uy + 11, 18, https && !w->browser_url_active ? g_theme->success : p.ink2);
    i32 tx = ux + 40, tmax = uw - 40 - 40;
    i32 ty = uy + (uh - ulh(2)) / 2;
    if (w->browser_url_active) {
        /* full URL while editing, scrolled to keep the end in view */
        const char *u = kstrncmp(w->browser_url, "about:", 6) ? w->browser_url : "";
        while (*u && gfx_str_role_width(u, FONT_ROLE_UI, 2, FONT_W_REGULAR) > tmax - 6) u++;
        gfx_str_role(tx, ty, u, p.ink, FONT_ROLE_UI, 2, FONT_W_REGULAR);
        if (((timer_ms() / 500) & 1) == 0) gfx_rect(tx + gfx_str_role_width(u, FONT_ROLE_UI, 2, FONT_W_REGULAR) + 1, ty, 2, ulh(2), g_theme->care);
        if (!u[0]) gfx_str_role(tx + 6, ty, "Search or type an address", p.muted, FONT_ROLE_UI, 2, FONT_W_REGULAR);
    } else if (about) {
        gfx_str_role(tx, ty, "Search or type an address", p.muted, FONT_ROLE_UI, 2, FONT_W_REGULAR);
    } else {
        /* host in ink, the rest dimmed */
        char host[128]; url_host(w->browser_url, host, sizeof host);
        const char *rest = w->browser_url + (https ? 8 : http ? 7 : 0) + kstrlen(host);
        i32 hw = gfx_str_role_width(host, FONT_ROLE_UI, 2, FONT_W_BOLD);
        gfx_str_role_fit(tx, ty, tmax, host, p.ink, FONT_ROLE_UI, 2, FONT_W_BOLD);
        if (hw < tmax - 20 && rest[0] && kstrcmp(rest, "/")) gfx_str_role_fit(tx + hw, ty, tmax - hw, rest, p.muted, FONT_ROLE_UI, 2, FONT_W_REGULAR);
    }
    bhit(s, rect_make(ux, uy, uw - 36, uh), BA_URL, 0);
    if (!about && !w->browser_url_active) {
        bool marked = is_bookmarked(w, NULL);
        i32 bkx = ux + uw - 34, bky = uy + 8;
        /* bookmark ribbon */
        u32 c = marked ? g_theme->care : p.ink2;
        if (marked) gfx_rect_rounded(bkx + 6, bky + 2, 12, 16, 2, c);
        else gfx_rect_rounded_outline(bkx + 6, bky + 2, 12, 16, 2, c);
        for (i32 i = 0; i < 5; i++) gfx_hline(bkx + 12 - i, bky + 18 - i, 2 * i + 1, marked ? p.field : p.field);
        bhit(s, rect_make(bkx, uy, 32, uh), BA_BOOKMARK, 0);
    }
    gfx_hline(cr.x, cr.y + bar_h - 1, cr.w, p.line);

    /* ── Status line (only while something is happening) ─────────── */
    bool show_status = w->browser_loading || !kstrncmp(g_status, "Failed", 6) || !kstrncmp(g_status, "Stopped", 7);
    i32 stat_h = show_status ? BROW_STAT_H : 0;
    i32 stat_y = cr.y + cr.h - stat_h;
    if (stat_h) {
        gfx_rect(cr.x, stat_y, cr.w, stat_h, p.strip);
        gfx_str_role_fit(cr.x + 14, stat_y + (stat_h - ulh(0)) / 2, cr.w - 28,
                         w->browser_loading ? browser_loading_text() : g_status, p.muted, FONT_ROLE_UI, 0, FONT_W_REGULAR);
    }

    /* ── Find bar ────────────────────────────────────────────────── */
    i32 find_bar_h = w->browser_find_active ? 44 : 0;
    if (find_bar_h) {
        i32 fb_y = stat_y - find_bar_h;
        gfx_rect(cr.x, fb_y, cr.w, find_bar_h, p.strip);
        gfx_hline(cr.x, fb_y, cr.w, p.line);
        gfx_str_role(cr.x + 16, fb_y + (find_bar_h - ulh(1)) / 2, "Find", p.ink2, FONT_ROLE_UI, 1, FONT_W_BOLD);
        i32 fi_x = cr.x + 64, fi_w = cr.w - 64 - 150;
        gfx_rect_rounded(fi_x, fb_y + 6, fi_w, find_bar_h - 12, 10, p.field);
        gfx_rect_rounded_outline(fi_x, fb_y + 6, fi_w, find_bar_h - 12, 10, g_theme->care);
        gfx_str_role_fit(fi_x + 10, fb_y + (find_bar_h - ulh(1)) / 2, fi_w - 20, w->browser_find_buf, p.ink, FONT_ROLE_UI, 1, FONT_W_REGULAR);
        char cnt[40];
        if (!w->browser_find_count && w->browser_find_len) kstrcpy(cnt, "No matches");
        else ksprintf(cnt, "%u of %u", w->browser_find_count ? w->browser_find_cur + 1 : 0, w->browser_find_count);
        gfx_str_role(fi_x + fi_w + 12, fb_y + (find_bar_h - ulh(1)) / 2, cnt, p.muted, FONT_ROLE_UI, 1, FONT_W_REGULAR);
    }

    /* ── Content area ────────────────────────────────────────────── */
    i32 cont_y = cr.y + bar_h;
    i32 cont_h = cr.h - bar_h - stat_h - find_bar_h;
    gfx_set_clip(cr.x, cont_y, cr.w, cont_h);

    if (w->browser_loading) {
        const char *lt = browser_loading_text();
        i32 lw = gfx_str_role_width(lt, FONT_ROLE_UI, 2, FONT_W_REGULAR);
        char host[128]; url_host(w->browser_url, host, sizeof host);
        i32 hw = gfx_str_role_width(host, FONT_ROLE_DISPLAY, 1, FONT_W_BOLD);
        gfx_str_role(cr.x + (cr.w - hw) / 2, cont_y + cont_h / 2 - 40, host, p.ink, FONT_ROLE_DISPLAY, 1, FONT_W_BOLD);
        gfx_str_role(cr.x + (cr.w - lw) / 2, cont_y + cont_h / 2, lt, p.muted, FONT_ROLE_UI, 2, FONT_W_REGULAR);
        ui_progress(rect_make(cr.x + cr.w / 2 - 120, cont_y + cont_h / 2 + 34, 240, 4),
                    (browser_fetch_elapsed_ms() / 40) % 100, g_theme->care);
        gfx_clear_clip();
        goto menu;
    }

    if (kstrncmp(w->browser_url, "about:", 6)==0 || (w->browser_content[0]=='\0' && !w->browser_page)) {
        draw_home(w, rect_make(cr.x, cont_y, cr.w, cont_h));
        gfx_clear_clip();
        goto menu;
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
        goto menu;
    }

    /* Native Weave renderer. The page is painted into a window-owned canvas
     * that is only repainted when the page, scroll position, viewport or the
     * page's generation (layout change / image arrival) changes; otherwise
     * the cached pixels are blitted. Images load in the fetch task. */
    if (w->browser_page) {
        web_page_t *pg = w->browser_page;
        web_page_relayout_if_needed(pg, cr.w, cont_h);
        i32 total_h = web_page_height(pg);
        i32 max_sc = total_h > cont_h ? total_h - cont_h : 0;
        if (w->browser_scroll > max_sc) w->browser_scroll = max_sc;
        if (w->browser_scroll < 0) w->browser_scroll = 0;
        if (cr.w > 0 && cont_h > 0) {
            if (w->bview_px && (w->bview_w != cr.w || w->bview_h != cont_h)) {
                kfree(w->bview_px); w->bview_px = NULL; w->bview_valid = false;
            }
            if (!w->bview_px) {
                w->bview_px = (u32 *)kmalloc((size_t)cr.w * (size_t)cont_h * 4u);
                w->bview_valid = false;
            }
            if (w->bview_px) {
                u32 gen = web_page_generation(pg) ^ (u32)(uintptr_t)pg;
                if (!w->bview_valid || w->bview_scroll != w->browser_scroll || w->bview_ver != gen) {
                    web_page_paint(pg, w->bview_px, cr.w, cont_h, cr.w, 0, w->browser_scroll);
                    w->bview_w = cr.w; w->bview_h = cont_h;
                    w->bview_scroll = w->browser_scroll; w->bview_ver = gen;
                    w->bview_valid = true;
                }
                gfx_blit_argb(cr.x, cont_y, w->bview_px, cr.w, cont_h, cr.x, cont_y, cr.w, cont_h);
            }
        }
        if (max_sc > 0) {
            i32 sb_x = cr.x + cr.w - 7;
            i32 sb_h = (cont_h * cont_h) / (total_h > 1 ? total_h : 1);
            if (sb_h < 24) sb_h = 24;
            i32 rng = cont_h - sb_h;
            i32 sb_y = cont_y + (i32)((i64)w->browser_scroll * rng / max_sc);
            gfx_rect(sb_x, cont_y, 7, cont_h, COL_SURFACE2);
            gfx_rect_rounded(sb_x + 1, sb_y, 5, sb_h, 3, COL_DIM);
        }
        gfx_clear_clip();
        goto menu;
    }

    /* Page-area cache: re-laying out up to 64 KB of HTML every frame was the
     * browser's main cost. Reuse the last render while nothing changed. */
    u32 theme_id = (u32)(uintptr_t)g_theme ^ g_theme->text ^ (g_theme->primary << 1);
    u32 font_id  = font_active_index() ^ (GFX_FONT_SCALE << 8);
    bool cacheable = !w->browser_find_active;
    if (cacheable && w->bview_valid && w->bview_px && w->bview_w == cr.w &&
        w->bview_h == cont_h && w->bview_scroll == w->browser_scroll &&
        w->bview_ver == w->browser_content_ver && w->bview_theme == theme_id &&
        w->bview_font == font_id) {
        gfx_blit_argb(cr.x, cont_y, w->bview_px, cr.w, cont_h, cr.x, cont_y, cr.w, cont_h);
        s_draw_cached = true;                /* keep this window's saved links */
        gfx_clear_clip();
        goto menu;
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

    /* Capture the page area for reuse -- only when it is wholly on screen,
     * or the missing part would be wrong once the window moves. */
    w->bview_valid = false;
    if (cacheable && cr.x >= 0 && cont_y >= 0 && cr.w > 0 && cont_h > 0 &&
        cr.x + cr.w <= (i32)SCREEN_W && cont_y + cont_h <= (i32)SCREEN_H) {
        if (w->bview_px && (w->bview_w != cr.w || w->bview_h != cont_h)) {
            kfree(w->bview_px); w->bview_px = NULL;
        }
        if (!w->bview_px) w->bview_px = (u32 *)kmalloc((size_t)cr.w * (size_t)cont_h * 4u);
        if (w->bview_px) {
            gfx_snapshot_rect(cr.x, cont_y, cr.w, cont_h, w->bview_px);
            w->bview_w = cr.w; w->bview_h = cont_h; w->bview_scroll = w->browser_scroll;
            w->bview_ver = w->browser_content_ver; w->bview_theme = theme_id; w->bview_font = font_id;
            w->bview_valid = true;
        }
    }

    gfx_clear_clip();
menu:
    /* ── "..." menu: page tools and bookmarks ────────────────────── */
    if (s && s->menu_open) {
        i32 mw = 300, row = 40;
        u32 nb = w->browser_bm_count;
        i32 mh = 8 + 3 * row + 12 + ulh(0) + 8 + (nb ? (i32)nb * row : row) + 8;
        i32 mx = cr.x + cr.w - 12 - mw, my = cr.y + BROW_BAR_H - 4;
        gfx_shadow_soft(mx, my, mw, mh, 16);
        gfx_rect_rounded(mx, my, mw, mh, 16, p.dark ? rgb(0x1c, 0x24, 0x40) : rgb(0xff, 0xff, 0xff));
        gfx_rect_rounded_outline(mx, my, mw, mh, 16, p.line);
        static const char *const items[3] = { "Find in page", "View source", "New Tab page" };
        static const char *const keys[3]  = { "Ctrl+F", "Ctrl+U", "" };
        static const u8 acts[3] = { BA_M_FIND, BA_M_SOURCE, BA_M_HOME };
        i32 iy = my + 8;
        for (u32 q = 0; q < 3; q++) {
            gfx_str_role(mx + 18, iy + (row - ulh(2)) / 2, items[q], p.ink, FONT_ROLE_UI, 2,
                         (q == 1 && w->browser_source_view) ? FONT_W_BOLD : FONT_W_REGULAR);
            i32 kw = gfx_str_role_width(keys[q], FONT_ROLE_UI, 0, FONT_W_REGULAR);
            gfx_str_role(mx + mw - 18 - kw, iy + (row - ulh(0)) / 2, keys[q], p.muted, FONT_ROLE_UI, 0, FONT_W_REGULAR);
            bhit(s, rect_make(mx + 6, iy, mw - 12, row), acts[q], 0);
            iy += row;
        }
        gfx_hline(mx + 12, iy + 4, mw - 24, p.line);
        iy += 12;
        gfx_str_role(mx + 18, iy, "BOOKMARKS", p.muted, FONT_ROLE_UI, 0, FONT_W_BOLD);
        iy += ulh(0) + 8;
        if (!nb) {
            gfx_str_role_fit(mx + 18, iy + (row - ulh(1)) / 2, mw - 36, "None yet -- use the ribbon in the address bar",
                             p.muted, FONT_ROLE_UI, 1, FONT_W_REGULAR);
        }
        for (u32 b = 0; b < nb; b++) {
            favicon(mx + 18, iy + (row - 18) / 2, 18, w->browser_bm_url[b], w->browser_bm_title[b]);
            gfx_str_role_fit(mx + 46, iy + (row - ulh(2)) / 2, mw - 46 - 44, w->browser_bm_title[b], p.ink, FONT_ROLE_UI, 2, FONT_W_REGULAR);
            ui_glyph(UI_GLYPH_CLOSE, mx + mw - 32, iy + (row - 14) / 2, 14, p.muted);
            bhit(s, rect_make(mx + 6, iy, mw - 50, row), BA_M_BM, (i32)b);
            bhit(s, rect_make(mx + mw - 40, iy, 34, row), BA_M_BM_DEL, (i32)b);
            iy += row;
        }
    }
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

    /* Ctrl+Tab: next tab (kept pages make this instant). */
    if (c == '\t' && keyboard_ctrl_held() && !w->browser_url_active) {
        if (w->browser_tab_count > 1)
            browser_switch_tab(w, (w->browser_tab_sel + 1) % w->browser_tab_count);
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
        if (c == 4) { /* Ctrl+D -- bookmark this page (again: remove) */
            bookmark_toggle(w);
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
            if (!kstrncmp(w->browser_url, "about:", 6)) w->browser_url[0] = '\0';   /* typing replaces about:home */
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
    else if ((u8)c == KEY_NAV_PGDN) w->browser_scroll += FH * 20;
    else if ((u8)c == KEY_NAV_PGUP) w->browser_scroll -= FH * 20;
    else if ((u8)c == KEY_NAV_END)  w->browser_scroll = 0x3FFFFFFF;   /* clamped by the next render */
    if (w->browser_scroll < 0) w->browser_scroll = 0;
}

void app_browser_scroll(window_t *w, i32 delta) {
    w->browser_scroll -= delta * FH * 3;
    if (w->browser_scroll < 0) w->browser_scroll = 0;
}

void app_browser_click(window_t *w, i32 x, i32 y) {
    bstate_t *s = bst(w);
    if (!s) return;
    bool was_menu = s->menu_open;
    for (u32 i = s->nhits; i-- > 0;) {
        const bhit_t *h = &s->hits[i];
        if (!rect_contains(h->r, x, y)) continue;
        bool menu_item = h->act >= BA_M_FIND && h->act <= BA_M_BM_DEL;
        if (was_menu && !menu_item && h->act != BA_MENU) { s->menu_open = false; return; }
        s->menu_open = false;
        if (h->act != BA_URL && h->act != BA_HOME_SEARCH && h->act != BA_ADD_SITE && w->browser_url_active) {
            w->browser_url_active = false;
            if (s_url_before_edit[0]) kstrncpy(w->browser_url, s_url_before_edit, 255);
        }
        switch (h->act) {
        case BA_BACK:
            if (w->browser_history_pos > 0) {
                w->browser_history_pos--;
                app_browser_navigate(w, w->browser_history[w->browser_history_pos]);
            }
            break;
        case BA_FWD:
            if (w->browser_history_pos + 1 < w->browser_history_count) {
                w->browser_history_pos++;
                app_browser_navigate(w, w->browser_history[w->browser_history_pos]);
            }
            break;
        case BA_RELOAD:   w->browser_scroll = 0; app_browser_navigate(w, w->browser_url); break;
        case BA_URL:      begin_url_edit(w, false); break;
        case BA_BOOKMARK: bookmark_toggle(w); break;
        case BA_MENU:     s->menu_open = !was_menu; break;
        case BA_M_FIND:
            w->browser_find_active = true;
            w->browser_find_buf[0] = '\0'; w->browser_find_len = 0;
            w->browser_find_count  = 0; w->browser_find_cur = 0;
            break;
        case BA_M_SOURCE: w->browser_source_view = !w->browser_source_view; break;
        case BA_M_HOME:   app_browser_navigate(w, "about:home"); break;
        case BA_M_BM:     if ((u32)h->arg < w->browser_bm_count) smart_navigate(w, w->browser_bm_url[h->arg]); break;
        case BA_M_BM_DEL: bookmark_remove(w, (u32)h->arg); break;
        case BA_HOME_SEARCH:
            if (h->arg == 1 && w->browser_url_active && w->browser_url[0]) {
                w->browser_url_active = false;
                smart_navigate(w, w->browser_url);
            } else begin_url_edit(w, true);
            break;
        case BA_ADD_SITE:
            begin_url_edit(w, true);
            notify_push("Browser", "Open a site, then click the ribbon in the address bar to pin it here.", COL_ACCENT);
            break;
        default: break;
        }
        return;
    }
    if (was_menu) { s->menu_open = false; return; }

    /* content area */
    rect_t cr = wm_client_rect(w);
    i32 cont_y = cr.y + BROW_BAR_H;
    if (y < cont_y) return;
    if (w->browser_url_active) {
        w->browser_url_active = false;
        if (s_url_before_edit[0]) kstrncpy(w->browser_url, s_url_before_edit, 255);
    }
    const blink_t *links = (const blink_t *)w->browser_links;
    i32 lx = x - (w->rect.x - w->browser_links_wx), ly = y - (w->rect.y - w->browser_links_wy);
    for (u32 i = 0; links && i < w->browser_nlinks; i++) {
        const blink_t *l = &links[i];
        if (lx>=l->x&&lx<l->x+l->w&&ly>=l->y&&ly<l->y+l->h) {
            /* Resolve relative / protocol-relative hrefs against this page. */
            char abs[512]; http_url_resolve(w->browser_url, l->url, abs, sizeof(abs));
            w->browser_push_history = true;
            w->browser_autohttps = false;
            app_browser_navigate(w, abs);
            return;
        }
    }
    if (w->browser_page) {
        char abs[1024];
        if (web_page_link_at(w->browser_page, x - cr.x, y - cont_y + w->browser_scroll,
                             abs, sizeof(abs))) {
            w->browser_push_history = true;
            app_browser_navigate(w, abs);
            return;
        }
    }
}

/* Called every GUI loop iteration for a browser window that is loading:
 * applies the finished fetch, or notices the fetch was superseded. */
void app_browser_tick(window_t *w) {
    if (!w->browser_loading) return;
    char url[512]; int n = 0;
    web_page_t *pg = NULL;
    if (!browser_fetch_take(w->instance_id, w->browser_tab_sel, url, sizeof url, &n, &pg)) {
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
        if (pg) browser_fetch_release_page(pg);
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
        ksprintf(w->browser_content,
                 "<html><body style='font-family:sans-serif;margin:48px;color:#333'>"
                 "<h1 style='font-weight:600'>Can't reach this page</h1>"
                 "<p>Could not load <b>%s</b>.</p><p style='color:#777'>%s</p></body></html>",
                 host, why);
        kstrcpy(w->browser_title, "Error");
        w->browser_content_ver++;
        browser_engine_load(w, url, w->browser_content, (u32)kstrlen(w->browser_content));
        ksprintf(g_status, "Failed: %s", host);
        w->browser_loading = false;
        w->browser_push_history = false;
        return;
    }
    /* Success. The final URL may differ after redirects. */
    kstrncpy(w->browser_url, url, 255);
    const char *body = browser_fetch_buffer();
    u32 blen = (u32)kstrlen(body);
    if (blen >= BROW_CONT_MAX) blen = BROW_CONT_MAX - 1;
    kmemcpy(w->browser_content, body, blen);       /* source view / legacy renderer */
    w->browser_content[blen] = '\0';
    w->browser_content_ver++;
    browser_engine_drop(w);
    w->browser_page = pg;                          /* NULL: legacy text renderer */
    w->browser_title[0] = '\0';
    if (pg && web_page_title(pg)[0]) kstrncpy(w->browser_title, web_page_title(pg), 127);
    else extract_title(w->browser_content, w->browser_title, 127);
    if (!w->browser_title[0]) kstrncpy(w->browser_title, host, 127);
    /* #fragment: jump to the anchor */
    const char *frag = kstrchr(url, '#');
    if (pg && frag && frag[1]) {
        int ay = web_page_anchor_y(pg, frag + 1);
        if (ay >= 0) w->browser_scroll = ay;
    }
    browser_save_tab(w);
    ksprintf(g_status, "%s  %d KB%s", host, n / 1024,
             kstrncmp(url, "https://", 8) == 0 ? " (HTTPS)" : "");
    if (w->browser_push_history) { history_push(w, url); w->browser_push_history = false; }
    w->browser_loading = false;
}

void app_browser_open_url(window_t *w, const char *url) { smart_navigate(w, url); }

void app_browser_close(window_t *w) { browser_engine_drop(w); }
