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

/* Tool lives in w->tab, colour (palette index) in w->paint_color -- they used
 * to share paint_color, so picking Red (index 2) silently became Erase. The
 * Line tool's first endpoint is parked in w->cursor_pos as (gy*64+gx)+1, with
 * 0 meaning "no anchor yet". Canvas cells hold a palette index, 0xff = blank. */
enum { PT_PEN = 0, PT_FILL = 1, PT_ERASE = 2, PT_LINE = 3 };
#define PT_BLANK 0xff

static void pt_stamp(window_t *w, i32 gx, i32 gy, u8 v) {
    i32 half = (i32)w->input_len / 2;
    for (i32 dy = -half; dy <= half; dy++)
        for (i32 dx = -half; dx <= half; dx++) {
            i32 nx = gx + dx, ny = gy + dy;
            if (nx >= 0 && nx < 64 && ny >= 0 && ny < 64)
                w->text_buf[ny * 64 + nx] = (char)v;
        }
}

static void pt_line(window_t *w, i32 x0, i32 y0, i32 x1, i32 y1, u8 v) {
    i32 dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    i32 dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    i32 err = dx + dy;
    for (;;) {
        pt_stamp(w, x0, y0, v);
        if (x0 == x1 && y0 == y1) break;
        i32 e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

/* 4-way flood fill with an explicit stack; each cell is recoloured before it
 * is pushed, so it is pushed at most once and the stack cannot overflow. */
static void pt_fill(window_t *w, i32 gx, i32 gy, u8 v) {
    static u16 stack[64 * 64];
    u8 from = (u8)w->text_buf[gy * 64 + gx];
    if (from == v) return;
    u32 sp = 0;
    w->text_buf[gy * 64 + gx] = (char)v;
    stack[sp++] = (u16)(gy * 64 + gx);
    while (sp) {
        u16 i = stack[--sp];
        i32 x = i % 64, y = i / 64;
        const i32 nx[4] = { x - 1, x + 1, x, x };
        const i32 ny[4] = { y, y, y - 1, y + 1 };
        for (int k = 0; k < 4; k++) {
            if (nx[k] < 0 || nx[k] >= 64 || ny[k] < 0 || ny[k] >= 64) continue;
            u32 j = (u32)(ny[k] * 64 + nx[k]);
            if ((u8)w->text_buf[j] != from) continue;
            w->text_buf[j] = (char)v;
            stack[sp++] = (u16)j;
        }
    }
}

void app_paint_init(window_t *w){
    kmemset(w->text_buf, 0xff, 64 * 64);  /* white canvas */
    w->paint_color = 0;   /* black */
    w->tab = PT_PEN;
    w->cursor_pos = 0;    /* no pending line anchor */
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
        bool sel = ((u32)i == w->tab);
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
        if ((u32)i == w->paint_color) {
            gfx_circle(px, psy, PT_PAL_R + 3, COL_ACCENT);
            gfx_circle(px, psy, PT_PAL_R + 2, COL_WHITE);
        }
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

    /* Pending Line anchor: outline the start cell until the second click. */
    if (w->tab == PT_LINE && w->cursor_pos) {
        i32 a = (i32)w->cursor_pos - 1;
        gfx_rect_outline(cx + (a % 64) * px_w - 1, cy + (a / 64) * px_h - 1,
                         px_w + 2, px_h + 2, COL_ACCENT);
    }
}

void app_paint_key(window_t *w, char c){
    if (c == 'c' || c == 'C') { app_paint_init(w); return; }
    if (c >= '1' && c <= '4') { w->tab = (u32)(c - '1'); w->cursor_pos = 0; return; }
    if (c == '+' || c == '=') { if (w->input_len < 16) w->input_len++; }
    if (c == '-') { if (w->input_len > 1) w->input_len--; }
}

void app_paint_click(window_t *w, i32 x, i32 y, mouse_t *m){
    rect_t cr = wm_client_rect(w);

    if (y < cr.y + PT_TB_H) {
        /* Palette swatches */
        i32 psx = pt_pal_start(cr);
        i32 psy = cr.y + PT_TB_H / 2;
        for (int i = 0; i < 10; i++) {
            i32 px = psx + i * PT_PAL_GAP + PT_PAL_R;
            if (x >= px - PT_PAL_R - 3 && x < px + PT_PAL_R + 3 &&
                y >= psy - PT_PAL_R - 3 && y < psy + PT_PAL_R + 3) {
                w->paint_color = (u32)i;
                if (w->tab == PT_ERASE) w->tab = PT_PEN;   /* picking a colour means "draw" */
                return;
            }
        }
        /* Tool buttons */
        for (int i = 0; i < 4; i++) {
            i32 bx = cr.x + 8 + i * (PT_BTN_W + PT_BTN_GAP);
            if (x >= bx && x < bx + PT_BTN_W &&
                y >= cr.y + PT_BTN_Y && y < cr.y + PT_BTN_Y + PT_BTN_H) {
                w->tab = (u32)i;
                w->cursor_pos = 0;
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
    if (x < canvas_x || y < canvas_y) return;   /* -0 would round into cell 0 */
    if (gx < 0 || gx >= 64 || gy < 0 || gy >= 64) return;

    u8 col = (u8)(w->paint_color < 10 ? w->paint_color : 0);
    bool fresh = m && m->left_clicked;   /* false on held-button repeats */
    switch (w->tab) {
    case PT_ERASE: pt_stamp(w, gx, gy, PT_BLANK); break;
    case PT_FILL:  if (fresh) pt_fill(w, gx, gy, col); break;
    case PT_LINE:
        if (!fresh) break;
        if (!w->cursor_pos) {
            w->cursor_pos = (u32)(gy * 64 + gx) + 1;
        } else {
            i32 a = (i32)w->cursor_pos - 1;
            pt_line(w, a % 64, a / 64, gx, gy, col);
            w->cursor_pos = 0;
        }
        break;
    default:       pt_stamp(w, gx, gy, col); break;
    }
}
