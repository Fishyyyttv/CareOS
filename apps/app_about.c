/* CareOS v9 -- apps/app_about.c -- About screen */
#include "apps_common.h"

void app_about_init(window_t *w) { (void)w; }

void app_about_draw(window_t *w) {
    rect_t cr = wm_client_rect(w);
    i32 sc    = (i32)GFX_FONT_SCALE;

    gfx_gradient_rect(cr.x, cr.y, cr.w, cr.h, COL_SURFACE, COL_BG);

    /* Large logo ring */
    i32 logo_r  = 36 * sc;
    i32 logo_cx = cr.x + cr.w / 2;
    i32 logo_cy = cr.y + logo_r + 24;

    gfx_circle_fill(logo_cx, logo_cy, logo_r + 10, COL_SURFACE2);
    gfx_circle(logo_cx, logo_cy, logo_r + 10, COL_BORDER);
    gfx_circle_fill(logo_cx, logo_cy, logo_r, COL_PRIMARY);
    gfx_circle_fill(logo_cx + logo_r / 4, logo_cy, logo_r - 14, COL_SURFACE);
    gfx_str_centered_ex(cr.x, logo_cy - 8 * sc, cr.w, "C", COL_ACCENT, COL_TRANSPARENT, FONT_H1);

    /* Title */
    i32 y = logo_cy + logo_r + 18;
    gfx_str_centered_ex(cr.x, y, cr.w, "CareOS", COL_TEXT, COL_TRANSPARENT, FONT_H2);
    y += FONT_H * sc * 2 + 4;
    gfx_str_centered(cr.x, y, cr.w, "Version 9.0  |  x86_64  |  April 2026", COL_DIM, COL_TRANSPARENT);
    y += FONT_H * sc + 16;

    /* Separator */
    gfx_hline(cr.x + 30, y, cr.w - 60, COL_BORDER);
    y += 14;

    /* Info rows */
    struct { const char *label; const char *value; u32 vcol; } rows[] = {
        { "Kernel:",   "CareOS 9.0 monolithic",        COL_ACCENT },
        { "Arch:",     "x86_64 protected mode",         COL_ACCENT },
        { "Graphics:", "32bpp linear framebuffer",      COL_ACCENT },
        { "Network:",  "TCP/IP + e1000 driver",         COL_ACCENT },
        { "Storage:",  "ATA PIO + ext2",                COL_ACCENT },
        { "Auth:",     "bcrypt-style local accounts",   COL_ACCENT },
    };
    i32 lw = gfx_str_width("Graphics:");
    i32 lh = FONT_H * sc + 8;
    for (int i = 0; i < 6; i++) {
        i32 rx = cr.x + 24;
        gfx_str(rx, y, rows[i].label, COL_MUTED, COL_TRANSPARENT);
        gfx_str(rx + lw + FONT_W * sc * 2, y, rows[i].value, rows[i].vcol, COL_TRANSPARENT);
        y += lh;
    }

    y += 8;
    gfx_hline(cr.x + 30, y, cr.w - 60, COL_BORDER);
    y += 12;
    gfx_str_centered(cr.x, y, cr.w, "Built with love for the CareOS project.", COL_MUTED, COL_TRANSPARENT);
}
