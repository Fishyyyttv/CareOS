/* CareOS v9 -- apps/app_calc.c -- Calculator
 * The arithmetic lives in calc_engine.c (double precision), so 2/3 shows
 * 0.6666666667 instead of the old integer-only 0. This file is built with
 * FP_CFLAGS like the rest of apps/ (see Makefile). */
#include "apps_common.h"
#include "calc_engine.h"

_Static_assert(sizeof(calc_t) <= sizeof(((window_t *)0)->calc_state),
               "window_t.calc_state too small for calc_t");

static calc_t *calc_of(window_t *w) { return (calc_t *)(void *)w->calc_state; }

void app_calc_init(window_t *w){
    calc_reset(calc_of(w));
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
    calc_t *c = calc_of(w);
    /* Pending operation ("6 /") in small dim text above the number. */
    if (c->expr[0]) {
        i32 elen = gfx_str_width(c->expr);
        gfx_str(cr.x + cr.w - pad - 14 - elen, cr.y + pad + 10,
                c->expr, COL_DIM, COL_TRANSPARENT);
    }
    i32 dlen = gfx_str_width(c->disp);
    gfx_str(cr.x + cr.w - pad - 14 - dlen,
            cr.y + pad + d_h - (FONT_H * sc) - 14,
            c->disp, c->error ? COL_RED : COL_TEXT, COL_TRANSPARENT);

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

    /* Engine key for each button, row-major, matching the labels in draw. */
    static const char keys[20] = { 'c','n','%','/',
                                   '7','8','9','*',
                                   '4','5','6','-',
                                   '1','2','3','+',
                                   '0','0','.','=' };
    for (int i = 0; i < 20; i++) {
        i32 col = i % 4, row = i / 4;
        i32 bx = cr.x + pad + col * (bw + gap);
        i32 by = grid_y + row * (bh + gap);
        if (x >= bx && x < bx + bw && y >= by && y < by + bh) {
            calc_key(calc_of(w), keys[i]);
            return;
        }
    }
}

void app_calc_key(window_t *w, char c){
    calc_t *st = calc_of(w);
    if ((c >= '0' && c <= '9') || c == '.' || c == '+' || c == '-' ||
        c == '*' || c == '/' || c == '%' || c == '=') { calc_key(st, c); return; }
    if (c == 'x' || c == 'X')           { calc_key(st, '*'); return; }
    if (c == '\n')                      { calc_key(st, '='); return; }
    if (c == 'c' || c == 'C' || c == 0x1B) { calc_key(st, 'c'); return; }
    if (c == '\b')                      { calc_key(st, 'b'); return; }
    if (c == 'n' || c == 'N')           { calc_key(st, 'n'); return; }
}
