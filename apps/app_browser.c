/* CareOS v9 -- apps/app_browser.c -- Web browser */
#include "apps_common.h"

void app_browser_init(window_t *w){
    kstrcpy(w->browser_url,"about:home"); w->text_len  /* browser_content_len */=0;
    kstrcpy(w->browser_content,
        "<html><head><title>CareOS Browser</title></head><body>"
        "<h1>Welcome to CareOS Browser</h1>"
        "<p>Enter a URL above to browse the web.</p>"
        "<p>Supports: HTTP/1.0, basic HTML rendering</p>"
        "<ul><li><a>example.com</a></li>"
        "<li><a>careos.dev</a></li></ul>"
        "</body></html>");
    w->text_len  /* browser_content_len */=kstrlen(w->browser_content);
    w->browser_loading=false;
}
static void render_simple_html(window_t *w,rect_t cr){
    /* Very basic HTML renderer */
    const char *p=w->browser_content;
    i32 x=cr.x+8, y=cr.y+30, max_x=cr.x+cr.w-8;
    u32 fg=COL_TEXT; bool in_tag=false;
    char tag[32]; int ti=0;
    bool bold=false, heading=false;
    while(*p&&y<cr.y+cr.h-4){
        if(*p=='<'){ in_tag=true; ti=0; p++; continue; }
        if(in_tag){
            if(*p=='>'){ tag[ti]='\0'; in_tag=false;
                if(kstrncmp(tag,"h1",2)==0||kstrncmp(tag,"h2",2)==0){heading=true;fg=COL_TEXT;}
                else if(kstrncmp(tag,"/h",2)==0){heading=false;y+=heading?22:14;x=cr.x+8;}
                else if(kstrcmp(tag,"p")==0||kstrcmp(tag,"/p")==0){y+=16;x=cr.x+8;}
                else if(kstrcmp(tag,"li")==0){y+=14;x=cr.x+8;gfx_circle_fill(x,y+5,3,COL_DIM);x+=10;}
                else if(kstrcmp(tag,"br")==0){y+=14;x=cr.x+8;}
                else if(kstrcmp(tag,"b")==0){bold=true;}
                else if(kstrcmp(tag,"/b")==0){bold=false;}
                else if(kstrncmp(tag,"a",1)==0){fg=COL_PRIMARY;}
                else if(kstrcmp(tag,"/a")==0){fg=COL_TEXT;}
            } else { if(ti<31) tag[ti++]=(char)*p; }
            p++; continue;
        }
        /* Render character */
        u32 col=bold?COL_ACCENT:fg;
        i32 sz=heading?FONT_W+2:FONT_W;
        if(*p==' '){ x+=sz; if(x>max_x-50){y+=heading?22:14;x=cr.x+8;} }
        else if(*p=='\n'){ y+=heading?22:14; x=cr.x+8; }
        else {
            gfx_char(x,y,*p,col,COL_SURFACE);
            x+=sz;
            if(x>max_x){ y+=heading?22:14; x=cr.x+8; }
        }
        p++;
    }
}
#define BROWSER_BAR_H 36

void app_browser_draw(window_t *w){
    rect_t cr = wm_client_rect(w);
    i32 sc = (i32)GFX_FONT_SCALE;
    gfx_rect(cr.x, cr.y, cr.w, cr.h, COL_SURFACE);

    /* Address bar background */
    gfx_rect(cr.x, cr.y, cr.w, BROWSER_BAR_H, COL_SURFACE2);
    gfx_hline(cr.x, cr.y + BROWSER_BAR_H, cr.w, COL_BORDER);

    /* Nav buttons */
    i32 btn_h = BROWSER_BAR_H - 10;
    i32 btn_y = cr.y + 5;
    gfx_rect_rounded(cr.x + 5,  btn_y, 28, btn_h, 5, COL_SURFACE3);
    gfx_str_centered(cr.x + 5,  btn_y + (btn_h-(FONT_H*sc))/2, 28, "<", COL_TEXT, COL_TRANSPARENT);
    gfx_rect_rounded(cr.x + 36, btn_y, 28, btn_h, 5, COL_SURFACE3);
    gfx_str_centered(cr.x + 36, btn_y + (btn_h-(FONT_H*sc))/2, 28, ">", COL_TEXT, COL_TRANSPARENT);

    /* URL bar */
    i32 url_x = cr.x + 70;
    i32 url_w = cr.w - 70 - 52;
    gfx_rect_rounded(url_x, btn_y, url_w, btn_h, 5, COL_INPUT_BG);
    gfx_rect_rounded_outline(url_x, btn_y, url_w, btn_h, 5,
        w->focused ? COL_PRIMARY : COL_BORDER);
    gfx_str_clipped(url_x + 8, btn_y + (btn_h-(FONT_H*sc))/2, url_w - 16,
        w->browser_url, COL_ACCENT, COL_TRANSPARENT);

    /* Go button */
    i32 go_x = cr.x + cr.w - 48;
    gfx_rect_rounded(go_x, btn_y, 44, btn_h, 5, COL_PRIMARY);
    gfx_str_centered(go_x, btn_y + (btn_h-(FONT_H*sc))/2, 44, "Go", COL_WHITE, COL_TRANSPARENT);

    if (w->browser_loading) {
        gfx_str_centered(cr.x, cr.y + cr.h/2, cr.w, "Loading...", COL_DIM, COL_TRANSPARENT);
        return;
    }

    /* Content area */
    gfx_rect(cr.x, cr.y + BROWSER_BAR_H + 1, cr.w, cr.h - BROWSER_BAR_H - 1, COL_SURFACE);
    gfx_set_clip(cr.x, cr.y + BROWSER_BAR_H + 1, cr.w, cr.h - BROWSER_BAR_H - 1);
    render_simple_html(w, rect_make(cr.x, cr.y + BROWSER_BAR_H + 1, cr.w, cr.h - BROWSER_BAR_H - 1));
    gfx_clear_clip();
}
void app_browser_navigate(window_t *w,const char *url){
    kstrncpy(w->browser_url,url,255); w->browser_loading=true;
    /* Parse host and path */
    const char *host=url; if(kstrncmp(url,"http://",7)==0) host=url+7;
    const char *slash=kstrchr(host,'/');
    char host_buf[64]; const char *path="/";
    if(slash){ u32 hl=(u32)(slash-host); if(hl>63) hl=63; kmemcpy(host_buf,host,hl); host_buf[hl]='\0'; path=slash; }
    else kstrncpy(host_buf,host,63);
    char resp[4096];
    int n=http_get(host_buf,80,path,resp,sizeof(resp));
    if(n>0){
        /* Skip HTTP headers */
        char *body=kstrchr(resp,'\n'); if(body) body=kstrchr(body+1,'\n');
        while(body&&*body=='\r') body++;
        if(body) kstrncpy(w->browser_content,body,WIN_TEXT_BUF-1);
        else kstrcpy(w->browser_content,resp);
        w->text_len  /* browser_content_len */=kstrlen(w->browser_content);
    } else {
        kstrcpy(w->browser_content,"<h1>Error</h1><p>Could not connect to server.</p>");
        w->text_len  /* browser_content_len */=kstrlen(w->browser_content);
    }
    w->browser_loading=false;
}
void app_browser_key(window_t *w,char c){
    if(c=='\n'){ app_browser_navigate(w,w->browser_url); return; }
    if(c=='\b'){ u32 l=kstrlen(w->browser_url); if(l>0) w->browser_url[l-1]='\0'; return; }
    if(c>=32&&c<127){ u32 l=kstrlen(w->browser_url); if(l<254){w->browser_url[l]=c;w->browser_url[l+1]='\0';} }
}
void app_browser_click(window_t *w,i32 x,i32 y){
    rect_t cr=wm_client_rect(w);
    if(y<cr.y+BROWSER_BAR_H&&x>cr.x+cr.w-48) app_browser_navigate(w,w->browser_url);
    if(y<cr.y+BROWSER_BAR_H&&x>cr.x+70) w->focused=true;
}
