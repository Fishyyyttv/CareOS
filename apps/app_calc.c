/* CareOS v9 -- apps/app_calc.c -- Calculator (Widget-based) */
#include "apps_common.h"

void app_calc_init(window_t *w){
    kstrcpy(w->calc_display,"0"); kstrcpy(w->calc_expr,"");
    w->calc_val=0; w->calc_prev=0; w->calc_op=0; w->calc_new_num=true; w->calc_error=false;
    /* The keypad is drawn and hit-tested directly by app_calc_draw/_click. */
}

static void calc_layout(window_t *w, i32 *d_h, i32 *bw, i32 *bh, i32 *pad, i32 *gap) {
    rect_t cr = wm_client_rect(w);
    i32 sc = (i32)GFX_FONT_SCALE;
    *pad = 12 * sc;
    *gap = 8 * sc;
    *d_h = 70 * sc;
    *bw = (cr.w - 2 * (*pad) - 3 * (*gap)) / 4;
    *bh = (cr.h - (*pad) - (*d_h) - (*pad) - (*pad) - 4 * (*gap)) / 5;
}

void app_calc_draw(window_t *w){
    rect_t cr = wm_client_rect(w);
    i32 sc = (i32)GFX_FONT_SCALE;
    i32 d_h, bw, bh, pad, gap;
    calc_layout(w, &d_h, &bw, &bh, &pad, &gap);

    gfx_rect(cr.x, cr.y, cr.w, cr.h, COL_SURFACE2);

    gfx_rect_rounded(cr.x + pad, cr.y + pad, cr.w - 2 * pad, d_h, 12, COL_INPUT_BG);
    gfx_rect_rounded_outline(cr.x + pad, cr.y + pad, cr.w - 2 * pad, d_h, 12, COL_BORDER);
    i32 dlen = gfx_str_width(w->calc_display);
    gfx_str(cr.x + cr.w - pad - 14 - dlen,
            cr.y + pad + d_h - (FONT_H * sc) - 14,
            w->calc_display, w->calc_error ? COL_RED : COL_TEXT, COL_TRANSPARENT);

    const char *labels[] = {"C","+/-","%","/",
                            "7","8","9","x",
                            "4","5","6","-",
                            "1","2","3","+",
                            "0","0",".","="};
    i32 grid_y = cr.y + pad + d_h + pad;
    for (int i = 0; i < 20; i++) {
        i32 col = i % 4, row = i / 4;
        i32 bx = cr.x + pad + col * (bw + gap);
        i32 by = grid_y + row * (bh + gap);
        bool primary = (i == 19);
        bool op = (col == 3 || i < 3);
        u32 bg = primary ? COL_PRIMARY : (op ? g_theme->surface3 : COL_SURFACE);
        u32 fg = primary ? COL_WHITE : (op ? COL_ACCENT : COL_TEXT);
        gfx_rect_rounded(bx, by, bw, bh, 10, bg);
        gfx_rect_blend(bx + 1, by + 1, bw - 2, bh / 3, COL_WHITE, 8);
        gfx_rect_rounded_outline(bx, by, bw, bh, 10, COL_BORDER);
        gfx_str_centered(bx, by + (bh - (i32)(FONT_H * sc)) / 2, bw,
                         labels[i], fg, COL_TRANSPARENT);
    }
}

void app_calc_click(window_t *w, i32 x, i32 y){
    rect_t cr = wm_client_rect(w);
    i32 d_h, bw, bh, pad, gap;
    calc_layout(w, &d_h, &bw, &bh, &pad, &gap);

    i32 grid_y = cr.y + pad + d_h + pad;  /* absolute grid start */

    /* Find which button was hit using exact visual rects */
    const char *btns[] = {"C","+/-","%","div","7","8","9","mul",
                          "4","5","6","-","1","2","3","+","0","0",".","="};
    int hit_idx = -1;
    for (int i = 0; i < 20; i++) {
        i32 col = i % 4, row = i / 4;
        i32 bx = cr.x + pad + col * (bw + gap);
        i32 by = grid_y + row * (bh + gap);
        if (x >= bx && x < bx + bw && y >= by && y < by + bh) {
            hit_idx = i;
            break;
        }
    }
    if (hit_idx < 0) return;

    const char *btnkey = btns[hit_idx];
    char key = btnkey[0];
    bool is_div = kstrcmp(btnkey,"div")==0;
    bool is_mul = kstrcmp(btnkey,"mul")==0;
    char op_char = is_div ? '/' : (is_mul ? '*' : key);

    if (key=='C'){ app_calc_init(w); return; }
    if (kstrcmp(btnkey, "+/-") == 0) {
        char tmp[32];
        if (w->calc_display[0] == '-') {
            kstrcpy(tmp, w->calc_display + 1);
            kstrcpy(w->calc_display, tmp);
        } else if (kstrcmp(w->calc_display, "0") != 0 && kstrlen(w->calc_display) < 30) {
            kstrcpy(tmp, "-");
            kstrcat(tmp, w->calc_display);
            kstrcpy(w->calc_display, tmp);
        }
        w->calc_val = (i32)katoi(w->calc_display);
        return;
    }
    if (key=='%') {
        char tmp[32];
        w->calc_val = (i32)katoi(w->calc_display) / 100;
        kitoa(w->calc_val, tmp, 10);
        kstrcpy(w->calc_display, tmp);
        return;
    }
    if ((key>='0'&&key<='9')||key=='.'){
        if (w->calc_new_num||kstrcmp(w->calc_display,"0")==0){
            kstrcpy(w->calc_display,""); w->calc_new_num=false;
        }
        if (kstrlen(w->calc_display)<14){
            u32 l=kstrlen(w->calc_display);
            w->calc_display[l]=key; w->calc_display[l+1]='\0';
        }
        w->calc_val=(i32)katoi(w->calc_display);
    } else if (key=='='||is_div||is_mul||key=='+'||key=='-'){
        if (w->calc_op&&!w->calc_new_num){
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
            w->calc_op=op_char;
            w->calc_new_num=true;
            char e[24]; kstrcpy(e,w->calc_display); kstrcat(e," ");
            w->calc_display[0]=op_char; w->calc_display[1]='\0';
            kstrcpy(w->calc_expr,e);
        } else {
            w->calc_op=0; w->calc_new_num=true;
        }
    }
}

/* Press keypad button idx (row-major index into the 4x5 grid) by synthesising
 * a click at its centre, so keyboard and mouse share one code path. */
static void calc_press(window_t *w, int idx){
    rect_t cr = wm_client_rect(w);
    i32 d_h, bw, bh, pad, gap;
    calc_layout(w, &d_h, &bw, &bh, &pad, &gap);
    i32 grid_y = cr.y + pad + d_h + pad;
    i32 col = idx%4, row = idx/4;
    app_calc_click(w, cr.x + pad + col*(bw+gap) + bw/2, grid_y + row*(bh+gap) + bh/2);
}

void app_calc_key(window_t *w, char c){
    /* Digits: '7'..'9' -> 4..6, '4'..'6' -> 8..10, '1'..'3' -> 12..14, '0' -> 16 */
    if (c>='1'&&c<='9'){ int d=c-'1'; calc_press(w, 12 - (d/3)*4 + d%3); return; }
    if (c=='0')                 { calc_press(w, 16); return; }
    if (c=='.')                 { calc_press(w, 18); return; }
    if (c=='+')                 { calc_press(w, 15); return; }
    if (c=='-')                 { calc_press(w, 11); return; }
    if (c=='*'||c=='x')         { calc_press(w, 7);  return; }
    if (c=='/')                 { calc_press(w, 3);  return; }
    if (c=='%')                 { calc_press(w, 2);  return; }
    if (c=='\n'||c=='=')        { calc_press(w, 19); return; }
    if (c=='c'||c=='C'||c==0x1B){ app_calc_init(w);  return; }
    if (c=='\b'){
        /* Only edit a number the user is still typing; after an operator or
         * "=" the display holds a result/operator, not an editable entry. */
        if (w->calc_error){ app_calc_init(w); return; }
        if (w->calc_new_num) return;
        u32 l = kstrlen(w->calc_display);
        if (l > 0) w->calc_display[--l] = '\0';
        if (l == 0 || (l == 1 && w->calc_display[0] == '-'))
            kstrcpy(w->calc_display, "0");
        w->calc_val = (i32)katoi(w->calc_display);
    }
}
