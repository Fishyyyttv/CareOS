/* CareOS v9 -- apps/app_settings.c -- functional settings suite */
#include "apps_common.h"

extern void term_ip_to_str(u32 ip, char *out);

static void settings_set_status(window_t *w, const char *msg, u32 color) {
    kstrncpy(w->settings_status, msg, sizeof(w->settings_status) - 1);
    w->settings_status[sizeof(w->settings_status) - 1] = '\0';
    w->settings_status_color = color;
}

static void settings_mask(const char *src, char *out, u32 max) {
    u32 len = (u32)kstrlen(src);
    if (len >= max) len = max - 1;
    for (u32 i = 0; i < len; i++) out[i] = '*';
    out[len] = '\0';
}

static button_t settings_button(rect_t rect, const char *label, bool active, u32 bg, u32 fg) {
    button_t b;
    kmemset(&b, 0, sizeof(b));
    b.rect = rect;
    b.active = active;
    b.bg = bg;
    b.fg = fg;
    kstrncpy(b.label, label, sizeof(b.label) - 1);
    b.label[sizeof(b.label) - 1] = '\0';
    return b;
}

void app_settings_init(window_t *w){
    w->settings_tab = 0;
    w->settings_field = 0;
    w->settings_old_pass[0] = '\0';
    w->settings_new_pass[0] = '\0';
    settings_set_status(w, "Select a category to customize CareOS", COL_DIM);
}

void app_settings_draw(window_t *w){
    rect_t cr = wm_client_rect(w);
    const careos_settings_t *cfg = settings_get();
    const user_t *cu = (const user_t*)user_get_by_uid(user_current_uid());
    i32 sc = (i32)GFX_FONT_SCALE;
    i32 sb = 120 + 64 * sc;
    i32 cx = cr.x + sb + 26;
    i32 cy = cr.y + 22;
    i32 cw = cr.w - sb - 52;
    const char *tabs[] = { "Account", "Display", "Personalize", "Network", "System", NULL };
    char buf[64];
    char aux[32];

    gfx_rect(cr.x, cr.y, cr.w, cr.h, COL_SURFACE);
    gfx_rect(cr.x, cr.y, sb, cr.h, COL_SURFACE2);
    gfx_rect_blend(cr.x, cr.y, sb, cr.h, COL_GLASS_TINT, 14);
    gfx_vline(cr.x + sb, cr.y, cr.h, COL_BORDER);

    gfx_str_ex(cr.x + 20, cr.y + 18, "Settings", COL_TEXT, COL_TRANSPARENT, FONT_H2);
    gfx_str(cr.x + 20, cr.y + 44, "System control center", COL_DIM, COL_TRANSPARENT);

    i32 tab_h = 24 + 10 * sc;
    for (int i = 0; tabs[i]; i++) {
        button_t tab = settings_button(
            rect_make(cr.x + 14, cr.y + (30 + 50 * sc) + i * (tab_h + 12), sb - 28, tab_h),
            tabs[i], w->settings_tab == (u32)i,
            w->settings_tab == (u32)i ? COL_PRIMARY : COL_SURFACE3,
            w->settings_tab == (u32)i ? COL_WHITE : COL_TEXT);
        button_draw(&tab);
    }
    i32 h1_h = 28 * sc;
    gfx_set_clip(cx, cy, cw, h1_h + 10);
    gfx_str_ex(cx, cy, tabs[w->settings_tab], COL_TEXT, COL_TRANSPARENT, FONT_H1);
    gfx_clear_clip();
    gfx_str(cx, cy + h1_h + 8, "Changes apply immediately and are saved to disk", COL_DIM, COL_TRANSPARENT);
    cy += h1_h + 40;

    switch (w->settings_tab) {
    case 0: {
        textinput_t old_box, new_box;
        char old_mask[32], new_mask[32], last_login[32];
        settings_mask(w->settings_old_pass, old_mask, sizeof(old_mask));
        settings_mask(w->settings_new_pass, new_mask, sizeof(new_mask));
        kmemset(&old_box, 0, sizeof(old_box));
        kmemset(&new_box, 0, sizeof(new_box));

        if (cu && cu->last_login_year) {
            char year[8];
            char hour[8];
            char minute[8];
            last_login[0] = '\0';
            kutoa(cu->last_login_year, year, 10);
            kutoa(cu->last_login_hour, hour, 10);
            kutoa(cu->last_login_minute, minute, 10);
            kstrcpy(last_login, year);
            kstrcat(last_login, "-");
            kutoa(cu->last_login_month, aux, 10); if (cu->last_login_month < 10) kstrcat(last_login, "0"); kstrcat(last_login, aux);
            kstrcat(last_login, "-");
            kutoa(cu->last_login_day, aux, 10); if (cu->last_login_day < 10) kstrcat(last_login, "0"); kstrcat(last_login, aux);
            kstrcat(last_login, " ");
            if (cu->last_login_hour < 10) kstrcat(last_login, "0");
            kstrcat(last_login, hour);
            kstrcat(last_login, ":");
            if (cu->last_login_minute < 10) kstrcat(last_login, "0");
            kstrcat(last_login, minute);
        } else {
            kstrcpy(last_login, "No previous login recorded");
        }

        i32 info_h = 40 + 36 * sc;
        gfx_rect_rounded(cx, cy, cw, info_h, 16, COL_SURFACE2);
        gfx_rect_blend(cx, cy, cw, info_h, COL_GLASS_TINT, 10);
        
        gfx_set_clip(cx + 20, cy, cw / 2 - 40, info_h);
        gfx_str(cx + 20, cy + 18, "Signed in as", COL_DIM, COL_TRANSPARENT);
        i32 h2_h = 20 * sc;
        gfx_str_ex(cx + 20, cy + 18 + 14 * sc, user_current_name(), COL_TEXT, COL_TRANSPARENT, FONT_H2);
        gfx_str(cx + 20, cy + 18 + 14 * sc + h2_h + 4, cu && cu->is_root ? "Administrator" : "Standard user", COL_ACCENT, COL_TRANSPARENT);
        gfx_clear_clip();

        gfx_set_clip(cx + cw / 2, cy, cw / 2 - 20, info_h);
        gfx_str(cx + cw / 2, cy + 18, "Home", COL_DIM, COL_TRANSPARENT);
        gfx_str(cx + cw / 2, cy + 18 + 14 * sc, cu ? cu->home : "/home", COL_TEXT, COL_TRANSPARENT);
        gfx_str(cx + cw / 2, cy + 18 + 14 * sc + h2_h + 4, last_login, COL_MUTED, COL_TRANSPARENT);
        gfx_clear_clip();

        cy += info_h + 24;
        gfx_str(cx, cy, "Change Password", COL_TEXT, COL_TRANSPARENT);
        gfx_str(cx + 150, cy, cu && cu->is_root ? "Current password optional for root" : "Current password required", COL_MUTED, COL_TRANSPARENT);
        cy += 24;

        old_box.rect = rect_make(cx, cy, cw / 2 - 12, 38);
        old_box.focused = (w->settings_field == 0);
        old_box.hover = false;
        kstrcpy(old_box.placeholder, "Current password");
        kstrcpy(old_box.buf, old_mask);
        old_box.len = (u32)kstrlen(old_mask);
        old_box.cursor = old_box.len;
        textinput_draw(&old_box);

        new_box.rect = rect_make(cx + cw / 2 + 12, cy, cw / 2 - 12, 38);
        new_box.focused = (w->settings_field == 1);
        new_box.hover = false;
        kstrcpy(new_box.placeholder, "New strong password");
        kstrcpy(new_box.buf, new_mask);
        new_box.len = (u32)kstrlen(new_mask);
        new_box.cursor = new_box.len;
        textinput_draw(&new_box);

        {
            button_t apply = settings_button(rect_make(cx, cy + 52, 156, 34), "Update Password", true, COL_PRIMARY, COL_WHITE);
            button_draw(&apply);
        }
        break;
    }

    case 1: {
        u32 fb_kb = (SCREEN_W * SCREEN_H * 4u) / 1024u;
        button_t minus = settings_button(rect_make(cx + 168, cy + 78, 34, 30), "-", false, COL_SURFACE3, COL_TEXT);
        button_t plus = settings_button(rect_make(cx + 210, cy + 78, 34, 30), "+", false, COL_SURFACE3, COL_TEXT);

        gfx_rect_rounded(cx, cy, cw, 128, 16, COL_SURFACE2);
        gfx_str(cx + 20, cy + 18, "Current resolution", COL_DIM, COL_TRANSPARENT);
        buf[0] = '\0';
        kutoa(SCREEN_W, buf, 10); kstrcat(buf, " x "); kutoa(SCREEN_H, aux, 10); kstrcat(buf, aux);
        gfx_str_ex(cx + 20, cy + 42, buf, COL_TEXT, COL_TRANSPARENT, FONT_H2);
        gfx_str(cx + 20, cy + 74, "Color depth", COL_DIM, COL_TRANSPARENT);
        gfx_str(cx + 116, cy + 74, "32-bit framebuffer", COL_ACCENT, COL_TRANSPARENT);
        gfx_str(cx + cw / 2, cy + 18, "Backbuffer estimate", COL_DIM, COL_TRANSPARENT);
        kutoa(fb_kb / 1024u, buf, 10); kstrcat(buf, " MB");
        gfx_str(cx + cw / 2, cy + 42, buf, COL_TEXT, COL_TRANSPARENT);
        gfx_str(cx + cw / 2, cy + 74, "GRUB/QEMU fallback may choose a lower mode", COL_MUTED, COL_TRANSPARENT);

        cy += 148;
        gfx_str(cx, cy, "Mouse sensitivity", COL_TEXT, COL_TRANSPARENT);
        button_draw(&minus);
        button_draw(&plus);
        kutoa(cfg->mouse_sensitivity, buf, 10);
        gfx_str(cx + 256, cy + 86, buf, COL_ACCENT, COL_TRANSPARENT);
        gfx_str(cx + 286, cy + 86, "%", COL_ACCENT, COL_TRANSPARENT);

        cy += 110;
        gfx_str(cx, cy, "Screen Resolution", COL_TEXT, COL_TRANSPARENT);
        gfx_str(cx + 180, cy, "(BGA — applies immediately)", COL_MUTED, COL_TRANSPARENT);
        {
            u32 mc = vesa_mode_count();
            u16 cur_w = vesa_current_w(), cur_h = vesa_current_h();
            i32 mw = (cw - 12) / 2;
            for (u32 i = 0; i < mc; i++) {
                vesa_mode_t vm = vesa_mode_get(i);
                bool is_cur = (vm.w == cur_w && vm.h == cur_h);
                i32 col_idx = (i32)(i % 2);
                i32 row_idx = (i32)(i / 2);
                button_t mb = settings_button(
                    rect_make(cx + col_idx * (mw + 12), cy + 22 + row_idx * 42, mw, 34),
                    vm.label, is_cur,
                    is_cur ? COL_PRIMARY : COL_SURFACE3,
                    is_cur ? COL_WHITE : COL_TEXT);
                button_draw(&mb);
            }
        }
        break;
    }

    case 2: {
        gfx_str(cx, cy, "Theme", COL_TEXT, COL_TRANSPARENT);
        {
            button_t dark = settings_button(rect_make(cx, cy + 22, 120, 34), "Dark", cfg->theme == 0, cfg->theme == 0 ? COL_PRIMARY : COL_SURFACE3, cfg->theme == 0 ? COL_WHITE : COL_TEXT);
            button_t light = settings_button(rect_make(cx + 132, cy + 22, 120, 34), "Light", cfg->theme == 1, cfg->theme == 1 ? COL_PRIMARY : COL_SURFACE3, cfg->theme == 1 ? COL_WHITE : COL_TEXT);
            button_draw(&dark);
            button_draw(&light);
        }

        cy += 82;
        gfx_str(cx, cy, "Wallpaper", COL_TEXT, COL_TRANSPARENT);
        for (u32 i = 0; i < 6; i++) {
            i32 wx = cx + (i32)(i % 3) * 148;
            i32 wy = cy + 18 + (i32)(i / 3) * 82;
            gfx_rect_rounded(wx, wy, 132, 64, 12, COL_SURFACE2);
            gfx_rect_blend(wx, wy, 132, 64, COL_GLASS_TINT, (u8)(12 + i * 4));
            if (cfg->wallpaper == i) gfx_rect_rounded_outline(wx, wy, 132, 64, 12, COL_PRIMARY);
            buf[0] = '#'; buf[1] = '0' + (char)i; buf[2] = '\0';
            gfx_str(wx + 10, wy + 8, buf, COL_TEXT, COL_TRANSPARENT);
        }

        cy += 190;
        gfx_str(cx, cy, "Taskbar layout", COL_TEXT, COL_TRANSPARENT);
        {
            button_t center = settings_button(rect_make(cx, cy + 20, 180, 34), cfg->taskbar_centered ? "Centered Icons" : "Left Aligned Icons", cfg->taskbar_centered, cfg->taskbar_centered ? COL_PRIMARY : COL_SURFACE3, cfg->taskbar_centered ? COL_WHITE : COL_TEXT);
            button_draw(&center);
        }
        break;
    }

    case 3: {
        char ip[24], dns_s[24];
        term_ip_to_str(net_get_ip(), ip);
        if (net_get_dns_server()) term_ip_to_str(net_get_dns_server(), dns_s);
        else kstrcpy(dns_s, "not set");

        gfx_rect_rounded(cx, cy, cw, 124, 16, COL_SURFACE2);
        gfx_str(cx + 20, cy + 18, "Link status", COL_DIM, COL_TRANSPARENT);
        gfx_str(cx + 20, cy + 42, net_is_up() ? "Connected" : "Disconnected", net_is_up() ? COL_GREEN : COL_RED, COL_TRANSPARENT);
        gfx_str(cx + 20, cy + 72, "IP", COL_DIM, COL_TRANSPARENT);
        gfx_str(cx + 64, cy + 72, ip, COL_TEXT, COL_TRANSPARENT);
        gfx_str(cx + cw / 2, cy + 72, "DNS", COL_DIM, COL_TRANSPARENT);
        gfx_str(cx + cw / 2 + 48, cy + 72, dns_s, COL_TEXT, COL_TRANSPARENT);

        {
            button_t wifi = settings_button(rect_make(cx, cy + 144, 164, 34),
                cfg->wifi_connected ? "Disconnect Wi-Fi" : "Connect Wi-Fi",
                false, cfg->wifi_connected ? COL_RED : COL_PRIMARY, COL_WHITE);
            button_t dhcp = settings_button(rect_make(cx + 176, cy + 144, 138, 34),
                "Renew DHCP", false, COL_SURFACE3, COL_TEXT);
            button_draw(&wifi);
            button_draw(&dhcp);
        }
        break;
    }

    default: {
        button_t fast_boot = settings_button(rect_make(cx, cy + 22, 164, 34),
            cfg->boot_fast ? "Fast Boot Enabled" : "Fast Boot Disabled",
            cfg->boot_fast, cfg->boot_fast ? COL_PRIMARY : COL_SURFACE3,
            cfg->boot_fast ? COL_WHITE : COL_TEXT);
        button_t clock = settings_button(rect_make(cx + 176, cy + 22, 172, 34),
            cfg->clock_24h ? "24 Hour Clock" : "12 Hour Clock",
            cfg->clock_24h, cfg->clock_24h ? COL_PRIMARY : COL_SURFACE3,
            cfg->clock_24h ? COL_WHITE : COL_TEXT);

        gfx_str(cx, cy, "Boot and system behavior", COL_TEXT, COL_TRANSPARENT);
        button_draw(&fast_boot);
        button_draw(&clock);
        gfx_str(cx, cy + 86, "Procedural wallpapers are used for performance and instant theme updates.", COL_MUTED, COL_TRANSPARENT);
        gfx_str(cx, cy + 104, "Shell fallback remains available for advanced settings changes.", COL_MUTED, COL_TRANSPARENT);
        break;
    }
    }

    gfx_rect_rounded(cx, cr.y + cr.h - 38, cw, 24, 8, COL_SURFACE2);
    gfx_str_clipped(cx + 10, cr.y + cr.h - 31, cw - 20, w->settings_status,
        w->settings_status_color ? w->settings_status_color : COL_DIM, COL_TRANSPARENT);
}

void app_settings_key(window_t *w,char c){
    char *target;
    u32 max;

    if (w->settings_tab != 0) return;
    target = (w->settings_field == 0) ? w->settings_old_pass : w->settings_new_pass;
    max = 31;

    if (c == '\t') {
        w->settings_field = 1 - w->settings_field;
        return;
    }
    if (c == '\n') {
        int rc = user_change_password(user_current_name(),
            w->settings_old_pass[0] ? w->settings_old_pass : NULL,
            w->settings_new_pass);
        if (rc == 0) {
            settings_set_status(w, "Password updated successfully", COL_GREEN);
            w->settings_old_pass[0] = '\0';
            w->settings_new_pass[0] = '\0';
        } else if (rc == -2) {
            settings_set_status(w, "New password must be strong", COL_YELLOW);
        } else if (rc == -4) {
            settings_set_status(w, "Current password did not match", COL_RED);
        } else {
            settings_set_status(w, "Unable to update password", COL_RED);
        }
        return;
    }
    if (c == '\b') {
        u32 len = (u32)kstrlen(target);
        if (len > 0) target[len - 1] = '\0';
        return;
    }
    if (c >= 32 && c < 127) {
        u32 len = (u32)kstrlen(target);
        if (len < max) {
            target[len] = c;
            target[len + 1] = '\0';
        }
    }
}

void app_settings_click(window_t *w,i32 x,i32 y,mouse_t *m){
    rect_t cr = wm_client_rect(w);
    const careos_settings_t *cfg = settings_get();
    i32 sc = (i32)GFX_FONT_SCALE;
    /* Match draw exactly: sb, cx, cy_start */
    i32 sb = 120 + 64 * sc;
    i32 cx = cr.x + sb + 26;
    i32 cw = cr.w - sb - 52;
    /* cy_start = cr.y+22, after H1 title (28*sc) + gap (40) */
    i32 cy = cr.y + 22 + 28 * sc + 40;
    (void)m;

    if (x < cr.x + sb) {
        i32 tab_h = 24 + 10 * sc;
        int idx = (y - (cr.y + (30 + 50 * sc))) / (tab_h + 12);
        if (idx >= 0 && idx < 5) w->settings_tab = (u32)idx;
        return;
    }

    switch (w->settings_tab) {
    case 0: {
        /* Match draw: info panel then password section */
        i32 info_h = 40 + 36 * sc;
        i32 pwd_cy  = cy + info_h + 24 + 24;   /* matches draw cy after panel+labels */
        if (rect_contains(rect_make(cx, pwd_cy, cw/2 - 12, 38), x, y)) w->settings_field = 0;
        if (rect_contains(rect_make(cx + cw/2 + 12, pwd_cy, cw/2 - 12, 38), x, y)) w->settings_field = 1;
        if (rect_contains(rect_make(cx, pwd_cy + 52, 156, 34), x, y)) app_settings_key(w, '\n');
        break;
    }

    case 1: {
        bool display_acted = false;
        if (rect_contains(rect_make(cx + 168, cy + 78, 34, 30), x, y)) {
            settings_set_mouse_sensitivity(cfg->mouse_sensitivity > 40 ? cfg->mouse_sensitivity - 10 : 40);
            display_acted = true;
        }
        if (rect_contains(rect_make(cx + 210, cy + 78, 34, 30), x, y)) {
            settings_set_mouse_sensitivity(cfg->mouse_sensitivity < 200 ? cfg->mouse_sensitivity + 10 : 200);
            display_acted = true;
        }
        {
            /* BGA mode list: cy_draw = cy_start+148+110 = cy+258 */
            i32 modes_cy = cy + 258;
            u32 mc = vesa_mode_count();
            i32 mw = (cw - 12) / 2;
            for (u32 i = 0; i < mc; i++) {
                vesa_mode_t vm = vesa_mode_get(i);
                i32 col_idx = (i32)(i % 2);
                i32 row_idx = (i32)(i / 2);
                rect_t r = rect_make(cx + col_idx * (mw + 12), modes_cy + 22 + row_idx * 42, mw, 34);
                if (rect_contains(r, x, y)) {
                    display_acted = true;
                    if (vesa_set_mode(vm.w, vm.h) == 0) {
                        settings_set_vesa_mode((u32)vm.w, (u32)vm.h);
                        settings_set_status(w, "Resolution changed", COL_GREEN);
                    } else {
                        settings_set_status(w, "BGA not available — GRUB resolution in use", COL_YELLOW);
                    }
                }
            }
        }
        if (!display_acted)
            settings_set_status(w, "Display metrics are read from the live framebuffer", COL_DIM);
        break;
    }

    case 2:
        if (rect_contains(rect_make(cx, cy + 22, 120, 34), x, y)) {
            settings_set_theme(0);
            theme_switch(true);
            user_set_current_theme_preference(0);
            settings_set_status(w, "Dark theme applied", COL_GREEN);
        }
        if (rect_contains(rect_make(cx + 132, cy + 22, 120, 34), x, y)) {
            settings_set_theme(1);
            theme_switch(false);
            user_set_current_theme_preference(1);
            settings_set_status(w, "Light theme applied", COL_GREEN);
        }
        for (u32 i = 0; i < 6; i++) {
            i32 wx = cx + (i32)(i % 3) * 148;
            i32 wy = cy + 100 + (i32)(i / 3) * 82;
            if (rect_contains(rect_make(wx, wy, 132, 64), x, y)) {
                settings_set_wallpaper(i);
                settings_set_status(w, "Wallpaper updated", COL_GREEN);
            }
        }
        if (rect_contains(rect_make(cx, cy + 292, 180, 34), x, y)) {
            settings_set_taskbar_centered(!cfg->taskbar_centered);
            settings_set_status(w, cfg->taskbar_centered ? "Taskbar moved left" : "Taskbar centered", COL_GREEN);
        }
        break;

    case 3:
        if (rect_contains(rect_make(cx, cy + 144, 164, 34), x, y)) {
            if (cfg->wifi_connected) {
                settings_set_wifi_profile("", "", false);
                settings_set_status(w, "Wi-Fi profile disconnected", COL_YELLOW);
            } else {
                settings_set_wifi_profile(cfg->wifi_ssid[0] ? cfg->wifi_ssid : "CareHome-5G", cfg->wifi_pass, true);
                settings_set_status(w, "Wi-Fi profile activated", COL_GREEN);
            }
        }
        if (rect_contains(rect_make(cx + 176, cy + 144, 138, 34), x, y)) {
            if (net_dhcp_renew() == 0) settings_set_status(w, "DHCP lease renewed", COL_GREEN);
            else settings_set_status(w, "DHCP request failed", COL_RED);
        }
        break;

    default:
        if (rect_contains(rect_make(cx, cy + 22, 164, 34), x, y)) {
            settings_set_boot_fast(!cfg->boot_fast);
            settings_set_status(w, cfg->boot_fast ? "Fast boot disabled" : "Fast boot enabled", COL_GREEN);
        }
        if (rect_contains(rect_make(cx + 176, cy + 22, 172, 34), x, y)) {
            settings_set_clock_24h(!cfg->clock_24h);
            settings_set_status(w, cfg->clock_24h ? "Clock switched to 12-hour mode" : "Clock switched to 24-hour mode", COL_GREEN);
        }
        break;
    }
}
