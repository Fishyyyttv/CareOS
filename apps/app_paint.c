/* CareOS v9 -- apps/app_paint.c -- Paint application */
#include "apps_common.h"

/* Shared toolbar constants — keep draw and click in sync */
#define PT_TB_H        38
#define PT_BTN_Y        7
#define PT_BTN_W       52
#define PT_BTN_H       24
#define PT_BTN_GAP      4
#define PT_PAL_R        7    /* palette swatch radius */
#define PT_PAL_GAP     18   /* center-to-center spacing */

static const u32 pt_palette[10] = {
    0x000000, 0xffffff, 0xff3030, 0x22cc55,
    0x4a6fff, 0xfbba00, 0x22d3ee, 0xa78bfa,
    0xf87171, 0xfb923c
};

static i32 pt_pal_start(rect_t cr) {
    return cr.x + cr.w - 10 * PT_PAL_GAP - PT_PAL_R - 8;
}

void app_paint_init(window_t *w){
    kmemset(w->text_buf, 0xff, 64 * 64);  /* white canvas */
    w->paint_color = 0;
    w->input_len = 2;  /* brush size */
}

void app_paint_draw(window_t *w){
    rect_t cr = wm_client_rect(w);
    i32 sc = (i32)GFX_FONT_SCALE;
    (void)sc;

    /* Toolbar */
    gfx_gradient_rect(cr.x, cr.y, cr.w, PT_TB_H, g_theme->surface3, COL_SURFACE2);
    gfx_hline(cr.x, cr.y + PT_TB_H, cr.w, COL_BORDER);

    /* Tool buttons */
    const char *tools[] = { "Pen", "Fill", "Erase", "Line" };
    i32 tool_zone_w = pt_pal_start(cr) - cr.x - 16;
    gfx_set_clip(cr.x + 4, cr.y, tool_zone_w, PT_TB_H);
    for (int i = 0; i < 4; i++) {
        i32 bx = cr.x + 8 + i * (PT_BTN_W + PT_BTN_GAP);
        bool sel = ((u32)i == w->paint_color);
        u32 bg = sel ? COL_PRIMARY : g_theme->surface3;
        gfx_rect_rounded(bx, cr.y + PT_BTN_Y, PT_BTN_W, PT_BTN_H, 6, bg);
        if (sel)
            gfx_rect_rounded_outline(bx, cr.y + PT_BTN_Y, PT_BTN_W, PT_BTN_H, 6, COL_ACCENT);
        gfx_str_centered(bx, cr.y + PT_BTN_Y + (PT_BTN_H - FONT_H) / 2,
                         PT_BTN_W, tools[i], sel ? COL_WHITE : COL_TEXT, bg);
    }
    gfx_clear_clip();

    /* Color palette swatches */
    i32 psx = pt_pal_start(cr);
    i32 psy = cr.y + PT_TB_H / 2;
    for (int i = 0; i < 10; i++) {
        i32 px = psx + i * PT_PAL_GAP + PT_PAL_R;
        gfx_circle_fill(px, psy, PT_PAL_R, pt_palette[i]);
        gfx_circle(px, psy, PT_PAL_R, COL_BORDER);
        if ((u32)i == w->paint_color)
            gfx_circle(px, psy, PT_PAL_R + 3, COL_WHITE);
    }

    /* Brush size indicator */
    char bs[8]; kstrcpy(bs, "sz:"); kutoa(w->input_len, bs + 3, 10);
    gfx_str(cr.x + 8 + 4 * (PT_BTN_W + PT_BTN_GAP) + 4, cr.y + PT_BTN_Y + 7,
            bs, g_theme->muted, COL_TRANSPARENT);

    /* Canvas area */
    i32 cw = cr.w - 2, ch = cr.h - PT_TB_H - 1;
    i32 cx = cr.x + 1, cy = cr.y + PT_TB_H + 1;
    i32 px_w = cw / 64, px_h = ch / 64;
    if (px_w < 1) px_w = 1;
    if (px_h < 1) px_h = 1;

    /* Canvas background */
    gfx_rect(cx, cy, cw, ch, 0xd8d8d8);

    for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 64; x++) {
            u8 ci = (u8)w->text_buf[y * 64 + x];
            if (ci == 0xff) continue;
            u32 col = (ci < 10) ? pt_palette[ci] : 0xffffff;
            gfx_rect(cx + x * px_w, cy + y * px_h, px_w, px_h, col);
        }
    }
}

void app_paint_key(window_t *w, char c){
    if (c == 'c' || c == 'C') { app_paint_init(w); return; }
    if (c >= '1' && c <= '4') { w->paint_color = (u32)(c - '1'); return; }
    if (c == '+' || c == '=') { if (w->input_len < 16) w->input_len++; }
    if (c == '-') { if (w->input_len > 1) w->input_len--; }
}

void app_paint_click(window_t *w, i32 x, i32 y, mouse_t *m){
    rect_t cr = wm_client_rect(w);
    (void)m;

    if (y < cr.y + PT_TB_H) {
        /* Palette swatches */
        i32 psx = pt_pal_start(cr);
        i32 psy = cr.y + PT_TB_H / 2;
        for (int i = 0; i < 10; i++) {
            i32 px = psx + i * PT_PAL_GAP + PT_PAL_R;
            if (x >= px - PT_PAL_R - 3 && x < px + PT_PAL_R + 3 &&
                y >= psy - PT_PAL_R - 3 && y < psy + PT_PAL_R + 3) {
                w->paint_color = (u32)i;
                return;
            }
        }
        /* Tool buttons */
        for (int i = 0; i < 4; i++) {
            i32 bx = cr.x + 8 + i * (PT_BTN_W + PT_BTN_GAP);
            if (x >= bx && x < bx + PT_BTN_W &&
                y >= cr.y + PT_BTN_Y && y < cr.y + PT_BTN_Y + PT_BTN_H) {
                w->paint_color = (u32)i;
                return;
            }
        }
        return;
    }

    /* Canvas draw */
    i32 cw = cr.w - 2, ch = cr.h - PT_TB_H - 1;
    i32 canvas_x = cr.x + 1, canvas_y = cr.y + PT_TB_H + 1;
    i32 px_w = cw / 64, px_h = ch / 64;
    if (px_w < 1) px_w = 1;
    if (px_h < 1) px_h = 1;
    i32 gx = (x - canvas_x) / px_w, gy = (y - canvas_y) / px_h;
    if (gx >= 0 && gx < 64 && gy >= 0 && gy < 64) {
        u8 c2 = (w->paint_color == 2) ? 0xff : (u8)w->paint_color;
        i32 half = (i32)w->input_len / 2;
        for (i32 dy = -half; dy <= half; dy++)
            for (i32 dx = -half; dx <= half; dx++) {
                i32 nx = gx + dx, ny = gy + dy;
                if (nx >= 0 && nx < 64 && ny >= 0 && ny < 64)
                    w->text_buf[ny * 64 + nx] = c2;
            }
    }
}
