/* CareOS v9 -- apps/app_calc.c -- Calculator (Widget-based) */
#include "apps_common.h"

void app_calc_init(window_t *w){
    kstrcpy(w->calc_display,"0"); kstrcpy(w->calc_expr,"");
    w->calc_val=0; w->calc_prev=0; w->calc_op=0; w->calc_new_num=true; w->calc_error=false;
    
    if (!w->root) return;
    i32 sc = (i32)GFX_FONT_SCALE;
    
    /* Display Panel */
    widget_t *disp = widget_create(WIDGET_PANEL, 0, 0, w->root->rect.w, 80 * sc);
    disp->bg_color = COL_INPUT_BG;
    widget_add_child(w->root, disp);
    
    /* Button Grid Container */
    widget_t *grid = widget_create(WIDGET_PANEL, 0, 80 * sc, w->root->rect.w, w->root->rect.h - 80 * sc);
    grid->bg_color = COL_TRANSPARENT;
    widget_add_child(w->root, grid);
    
    const char *btns[] = {"7","8","9","/", "4","5","6","*", "1","2","3","-", "C","0","=","+"};
    for (int r = 0; r < 4; r++) {
        widget_t *row = widget_create(WIDGET_PANEL, 0, 0, grid->rect.w, 50 * sc);
        widget_add_child(grid, row);
        for (int c = 0; c < 4; c++) {
            widget_t *b = widget_create(WIDGET_BUTTON, 0, 0, 60, 40);
            kstrcpy(b->text, btns[r * 4 + c]);
            b->bg_color = (r * 4 + c == 14) ? COL_PRIMARY : g_theme->surface3;
            b->color = COL_TEXT;
            widget_add_child(row, b);
        }
        layout_hbox(row, 8, 8);
    }
    layout_vbox(grid, 8, 8);
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
    gfx_rect(cr.x, cr.y, cr.w, cr.h, COL_SURFACE2);
    
    /* Main value — right-aligned (Dynamic text) */
    i32 dlen = gfx_str_width(w->calc_display);
    gfx_str(cr.x + cr.w - 20 - dlen, cr.y + 40 * sc, 
            w->calc_display, w->calc_error ? COL_RED : COL_TEXT, COL_TRANSPARENT);
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

    if (key=='C'){ app_calc_init(w); return; }
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

void app_calc_key(window_t *w, char c){
    if (c>='0'&&c<='9'){
        rect_t cr = wm_client_rect(w);
        i32 d_h, bw, bh, pad, gap;
        calc_layout(w, &d_h, &bw, &bh, &pad, &gap);
        i32 grid_y = cr.y + pad + d_h + pad;
        /* Map digit to button index and click center (absolute coords) */
        int idx = 0;
        if      (c=='7') idx=4;  else if(c=='8') idx=5;  else if(c=='9') idx=6;
        else if (c=='4') idx=8;  else if(c=='5') idx=9;  else if(c=='6') idx=10;
        else if (c=='1') idx=12; else if(c=='2') idx=13; else if(c=='3') idx=14;
        else if (c=='0') idx=16;
        i32 col = idx%4, row = idx/4;
        i32 cx = cr.x + pad + col*(bw+gap) + bw/2;
        i32 cy = grid_y + row*(bh+gap) + bh/2;
        app_calc_click(w, cx, cy);
    }
}
