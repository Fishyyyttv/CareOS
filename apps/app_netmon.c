/* CareOS v9 -- apps/app_netmon.c -- Network Monitor */
#include "apps_common.h"

void app_netmon_tick(window_t *w){
    u32 pos = w->scroll % 60;
    w->text_buf[pos]  = (u32)(timer_get_ticks() % 80);  /* rx_hist */
    w->input_buf[pos] = (u32)(timer_get_ticks() % 40);  /* tx_hist */
    w->scroll++;
}

void app_netmon_draw(window_t *w){
    rect_t cr = wm_client_rect(w);
    i32 sc = (i32)GFX_FONT_SCALE;

    gfx_rect(cr.x, cr.y, cr.w, cr.h, COL_SURFACE);

    /* Header */
    i32 hdr_h = 40 + 6 * sc;
    gfx_gradient_rect(cr.x, cr.y, cr.w, hdr_h, g_theme->surface3, COL_SURFACE);
    gfx_hline(cr.x, cr.y + hdr_h, cr.w, COL_BORDER);
    gfx_str_ex(cr.x + 14, cr.y + 10, "Network Monitor", COL_TEXT, COL_TRANSPARENT, FONT_H2);

    /* Connection status badge */
    bool up = net_is_up();
    u32 badge_col = up ? rgb(0x06, 0x2e, 0x14) : rgb(0x2e, 0x08, 0x08);
    u32 badge_txt = up ? COL_GREEN : COL_RED;
    i32 badge_w = 80 + 4 * sc;
    i32 badge_x = cr.x + cr.w - badge_w - 12;
    i32 badge_y = cr.y + hdr_h / 2 - 9;
    gfx_rect_rounded(badge_x, badge_y, badge_w, 18, 9, badge_col);
    gfx_set_clip(badge_x + 2, badge_y, badge_w - 4, 18);
    gfx_str_centered(badge_x, badge_y + 4, badge_w, up ? "Connected" : "Offline",
                     badge_txt, COL_TRANSPARENT);
    gfx_clear_clip();

    /* Info row */
    i32 y = cr.y + hdr_h + 10;
    gfx_str(cr.x + 14, y, "Interface:", g_theme->dim, COL_TRANSPARENT);
    gfx_str(cr.x + 92, y, "eth0", COL_TEXT, COL_TRANSPARENT);

    if (up) {
        u32 ip = net_get_ip();
        char ip_s[24];
        ksprintf(ip_s, "%d.%d.%d.%d", (int)(ip>>24)&0xff, (int)(ip>>16)&0xff, (int)(ip>>8)&0xff, (int)ip&0xff);
        
        /* Move IP to a much wider position to avoid "eth0" overlap */
        i32 ip_lbl_x = cr.x + 160 + 30 * sc;
        gfx_set_clip(ip_lbl_x, y, cr.w - (ip_lbl_x - cr.x) - 10, 20);
        gfx_str(ip_lbl_x, y, "IP:", g_theme->dim, COL_TRANSPARENT);
        gfx_str(ip_lbl_x + 32, y, ip_s, COL_ACCENT, COL_TRANSPARENT);
        gfx_clear_clip();
    }
    y += 18 + 6 * sc;

    /* Graph area: split remaining height between RX and TX */
    i32 pad = 14;
    i32 gw  = cr.w - pad * 2;
    i32 lbl_h = 16 + 4 * sc;
    i32 remaining = cr.h - (y - cr.y) - 8;
    i32 gh  = remaining / 2 - lbl_h - 8;
    if (gh < 30) gh = 30;

    /* ---- RX graph ---- */
    gfx_set_clip(cr.x + pad, y, gw / 2, lbl_h);
    gfx_str_ex(cr.x + pad, y, "Download  (RX)", COL_GREEN, COL_TRANSPARENT, FONT_H2);
    gfx_clear_clip();
    /* peak label right-aligned */
    u32 rx_peak = 0;
    for (int i = 0; i < 60; i++) { u32 v = (u8)w->text_buf[i]; if (v > rx_peak) rx_peak = v; }
    char pk[16]; kutoa(rx_peak, pk, 10); kstrcat(pk, " KB/s");
    gfx_set_clip(cr.x + pad + gw / 2, y, gw / 2 - 2, lbl_h);
    gfx_str_right(cr.x + pad, y, gw - 2, pk, g_theme->muted, COL_TRANSPARENT);
    gfx_clear_clip();
    y += lbl_h;

    gfx_rect(cr.x + pad, y, gw, gh, g_theme->surface2);
    gfx_rect_outline(cr.x + pad, y, gw, gh, COL_BORDER);
    /* Horizontal grid */
    for (int g = 1; g < 4; g++)
        gfx_hline(cr.x + pad + 1, y + gh * g / 4, gw - 2, COL_BORDER);

    for (int i = 1; i < 60; i++) {
        u32 v1 = (u8)w->text_buf[(w->scroll + i - 1) % 60];
        u32 v2 = (u8)w->text_buf[(w->scroll + i) % 60];
        i32 x1 = cr.x + pad + (i-1) * gw / 60;
        i32 x2 = cr.x + pad + i * gw / 60;
        i32 y1 = y + gh - (i32)(v1 * (u32)gh / 100);
        i32 y2 = y + gh - (i32)(v2 * (u32)gh / 100);
        /* Area fill */
        i32 fill_h = y + gh - y1;
        if (fill_h > 0)
            gfx_rect_blend(x1, y1, x2 - x1 + 1, fill_h, COL_GREEN, 28);
        gfx_line(x1, y1, x2, y2, COL_GREEN);
    }
    y += gh + 14;

    /* ---- TX graph ---- */
    gfx_str_ex(cr.x + pad, y, "Upload    (TX)", COL_CYAN, COL_TRANSPARENT, FONT_H2);
    u32 tx_peak = 0;
    for (int i = 0; i < 60; i++) { u32 v = (u8)w->input_buf[i]; if (v > tx_peak) tx_peak = v; }
    char tpk[16]; kutoa(tx_peak, tpk, 10); kstrcat(tpk, " KB/s");
    gfx_str_right(cr.x + pad, y, gw - 2, tpk, g_theme->muted, COL_TRANSPARENT);
    y += lbl_h;

    gfx_rect(cr.x + pad, y, gw, gh, g_theme->surface2);
    gfx_rect_outline(cr.x + pad, y, gw, gh, COL_BORDER);
    for (int g = 1; g < 4; g++)
        gfx_hline(cr.x + pad + 1, y + gh * g / 4, gw - 2, COL_BORDER);

    for (int i = 1; i < 60; i++) {
        u32 v1 = (u8)w->input_buf[(w->scroll + i - 1) % 60];
        u32 v2 = (u8)w->input_buf[(w->scroll + i) % 60];
        i32 x1 = cr.x + pad + (i-1) * gw / 60;
        i32 x2 = cr.x + pad + i * gw / 60;
        i32 y1 = y + gh - (i32)(v1 * (u32)gh / 100);
        i32 y2 = y + gh - (i32)(v2 * (u32)gh / 100);
        i32 fill_h = y + gh - y1;
        gfx_set_clip(cr.x + pad, y, gw, gh);
        if (fill_h > 0)
            gfx_rect_blend(x1, y1, x2 - x1 + 1, fill_h, COL_CYAN, 28);
        gfx_line(x1, y1, x2, y2, COL_CYAN);
        gfx_clear_clip();
    }
}
