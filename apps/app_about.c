/* CareOS v9 -- apps/app_about.c -- About screen refactored to Widgets */
#include "apps_common.h"

void app_about_init(window_t *w) {
    if (!w->root) return;
    i32 sc = (i32)GFX_FONT_SCALE;
    
    /* Header Container */
    widget_t *header = widget_create(WIDGET_PANEL, 0, 0, w->root->rect.w, 140 * sc);
    header->bg_color = COL_TRANSPARENT;
    widget_add_child(w->root, header);
    
    /* Name Label */
    widget_t *title = widget_create(WIDGET_LABEL, 0, 80, header->rect.w, 20 * sc);
    kstrcpy(title->text, "CareOS v9.0");
    title->color = COL_TEXT;
    widget_add_child(header, title);
    
    /* Info Grid */
    widget_t *grid = widget_create(WIDGET_PANEL, 20, 160, w->root->rect.w - 40, 200);
    grid->bg_color = g_theme->surface2;
    widget_add_child(w->root, grid);
    
    const char *labels[] = {"Kernel: CareOS 9.0", "Arch: x86", "GFX: 32bpp", "Net: TCP/IP"};
    for (int i = 0; i < 4; i++) {
        widget_t *l = widget_create(WIDGET_LABEL, 10, 10 + i * 30, grid->rect.w - 20, 20);
        kstrcpy(l->text, labels[i]);
        l->color = COL_TEXT;
        widget_add_child(grid, l);
    }
}

void app_about_draw(window_t *w) {
    /* The widget tree is drawn automatically by the Window Manager.
       We only need to draw things that aren't widgets yet (like the custom logo ring). */
    rect_t cr = wm_client_rect(w);
    i32 sc = (i32)GFX_FONT_SCALE;
    
    /* Custom Logo Ring (Special rendering) */
    i32 logo_cx = cr.x + cr.w / 2;
    i32 logo_cy = cr.y + 40 * sc;
    gfx_circle_fill(logo_cx, logo_cy, 30 * sc, g_theme->surface3);
    gfx_circle_fill(logo_cx, logo_cy, 26 * sc, COL_PRIMARY);
    gfx_str_centered_ex(cr.x, logo_cy - 10 * sc, cr.w, "C", COL_WHITE, COL_TRANSPARENT, FONT_H1);
}
