/* CareOS v9 -- apps/app_calc.c -- Calculator */
#include "apps_common.h"

void app_calc_init(window_t *w){
    kstrcpy(w->calc_display,"0"); kstrcpy(w->calc_expr,"");
    w->calc_val=0; w->calc_prev=0; w->calc_op=0; w->calc_new_num=true; w->calc_error=false;
}
void app_calc_draw(window_t *w){
    rect_t cr=wm_client_rect(w);
    gfx_rect(cr.x,cr.y,cr.w,cr.h,COL_SURFACE2);
    /* Display */
    gfx_rect(cr.x+4,cr.y+4,cr.w-8,46,COL_INPUT_BG);
    gfx_rect_outline(cr.x+4,cr.y+4,cr.w-8,46,COL_BORDER);
    gfx_str_clipped(cr.x+8,cr.y+8,cr.w-16,w->calc_expr,COL_DIM,COL_INPUT_BG);
    i32 dlen=gfx_str_width(w->calc_display);
    gfx_str(cr.x+cr.w-8-dlen,cr.y+24,w->calc_display,w->calc_error?COL_RED:COL_TEXT,COL_INPUT_BG);
    /* Button grid */
    const char *btns[]={"C","+/-","%","div","7","8","9","mul","4","5","6","-","1","2","3","+","0","0",".","="};
    u32 cols[]={COL_RED,COL_DIM,COL_DIM,COL_ORANGE,COL_SURFACE3,COL_SURFACE3,COL_SURFACE3,COL_ORANGE,
                COL_SURFACE3,COL_SURFACE3,COL_SURFACE3,COL_ORANGE,COL_SURFACE3,COL_SURFACE3,COL_SURFACE3,COL_ORANGE,
                COL_SURFACE3,COL_SURFACE3,COL_SURFACE3,COL_PRIMARY};
    i32 bw=(cr.w-10)/4, bh=36, bx=cr.x+4, by=cr.y+54;
    for(int i=0;i<20;i++){
        i32 col=i%4, row=i/4;
        i32 x=bx+col*bw+2, y=by+row*(bh+3);
        gfx_rect_rounded(x,y,bw-4,bh-2,6,cols[i]);
        gfx_str_centered(x,y+(bh-FONT_H)/2,bw-4,btns[i],COL_TEXT,cols[i]);
    }
}
void app_calc_click(window_t *w,i32 x,i32 y){
    rect_t cr=wm_client_rect(w);
    i32 bw=(cr.w-10)/4,bh=36,bx=cr.x+4-cr.x,by=cr.y+54-cr.y;
    /* Adjust for client-relative coords */
    i32 col=(x-4)/(bw), row=(y-54)/(bh+3);
    if(col<0||col>3||row<0||row>4) return;
    int idx=row*4+col;
    const char *btns[]={"C","\xC2\xB1","%","\xC3\xB7","7","8","9","\xC3\x97","4","5","6","-","1","2","3","+","0","0",".","="};
    if(idx>=20) return;
    const char *btns2[]={"C","+/-","%","div","7","8","9","mul","4","5","6","-","1","2","3","+","0","0",".","="};
    const char *btnkey=btns2[idx];
    char key=btnkey[0];
    bool is_div=kstrcmp(btnkey,"div")==0;
    bool is_mul=kstrcmp(btnkey,"mul")==0;
    (void)bx;(void)by;(void)x;(void)y;(void)btns;
    if(key=='C'){ app_calc_init(w); return; }
    if((key>='0'&&key<='9')||key=='.'){
        if(w->calc_new_num||kstrcmp(w->calc_display,"0")==0){
            kstrcpy(w->calc_display,""); w->calc_new_num=false;
        }
        if(kstrlen(w->calc_display)<14){
            u32 l=kstrlen(w->calc_display);
            w->calc_display[l]=key; w->calc_display[l+1]='\0';
        }
        w->calc_val=(i32)katoi(w->calc_display);
    } else if(key=='='||is_div||is_mul||key=='+'||key=='-'){
        if(w->calc_op&&!w->calc_new_num){
            i32 r=(i32)w->calc_prev;
            if(w->calc_op=='+') r+=w->calc_val;
            else if(w->calc_op=='-') r-=w->calc_val;
            else if(w->calc_op=='*') r*=w->calc_val;
            else if(w->calc_op=='/'){ if(w->calc_val==0){kstrcpy(w->calc_display,"Error");w->calc_error=true;return;} r/=w->calc_val; }
            w->calc_val=r; w->calc_prev=r;
            char buf[20]; kitoa(r,buf,10); kstrcpy(w->calc_display,buf);
        } else {
            w->calc_prev=w->calc_val;
        }
        if(key!='='){
            w->calc_op=is_div?'/':is_mul?'*':key;
            w->calc_new_num=true;
            char e[24]; kstrcpy(e,w->calc_display); kstrcat(e," ");
            w->calc_display[0]=(char)key; w->calc_display[1]='\0';
            kstrcpy(w->calc_expr,e);
        } else {
            w->calc_op=0; w->calc_new_num=true;
        }
    }
}
void app_calc_key(window_t *w,char c){
    /* Keyboard shortcuts */
    if(c>='0'&&c<='9'){
        i32 col=(c-'0')%4,row=c=='0'?4:(9-(c-'0'))/4;
        app_calc_click(w,4+col*((wm_client_rect(w).w-10)/4),54+row*39);
    }
}
