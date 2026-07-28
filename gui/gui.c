/* =============================================================================
 * CareOS gui/gui.c -- main GUI entry point with splash, login, and desktop
 * ============================================================================= */
#include "kernel.h"
#include "gui.h"
#include "image.h"
#include "resource_cache.h"
#include "icon.h"

static void draw_glass_panel(rect_t r, i32 radius);

static i32 ui_clampi(i32 v, i32 lo, i32 hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

void gui_init(u32 *fb, u32 w, u32 h, u32 pitch) {
    serial_write("  [gui_init] gfx_init\n");
    gfx_init(fb, w, h, pitch);

    /* Load default theme based on settings */
    {
        const careos_settings_t *cfg = settings_get();
        theme_switch(cfg ? (cfg->theme == 0) : true);
    }

    /* Before mouse/wm so the first painted frame already has its artwork.
     * Publishes /system/icons and /system/wallpapers and starts the cache. */
    serial_write("  [gui_init] resources_init\n");
    resources_init();

    serial_write("  [gui_init] mouse_init\n");
    mouse_init();
    serial_write("  [gui_init] wm_init\n");
    wm_init();
    serial_write("  [gui_init] done\n");
}

static const char *BOOT_STAGES[] = {
    "Initializing graphics pipeline...",
    "Starting input and device services...",
    "Mounting virtual filesystem...",
    "Starting process scheduler...",
    "Starting desktop services...",
    "Preparing secure login session...",
};
#define BOOT_STAGE_COUNT 6

static void draw_boot_splash(int done, u32 tick) {
    i32 sw = (i32)SCREEN_W;
    i32 sh = (i32)SCREEN_H;
    i32 area_h = sh;
    i32 ring = ui_clampi(sw / 22, 38, 72);
    i32 bar_w = ui_clampi(sw * 34 / 100, 360, 560);
    i32 bar_h = ui_clampi(sh / 120, 8, 14);
    i32 by = sh / 2 + ring + 72;

    gfx_gradient_rect(0, 0, sw, area_h, rgb(0x04,0x09,0x14), rgb(0x12,0x1a,0x30));

    gfx_circle_fill(sw / 2 - sw / 5, sh / 3, sw / 7, rgb(0x11,0x22,0x40));
    gfx_circle_fill(sw / 2 + sw / 4, sh / 2, sw / 8, rgb(0x0c,0x1c,0x39));
    for (i32 gy = 0; gy < sh; gy += 34)
        for (i32 gx = 0; gx < sw; gx += 34)
            if ((((gx / 34) + (gy / 34)) + (i32)(tick % 2)) % 2 == 0)
                gfx_setpixel(gx, gy, rgb(0x1a,0x2b,0x4a));

    gfx_circle_fill(sw / 2, sh / 2 - 70, ring + 12, rgb(0x1d,0x2f,0x5b));
    gfx_circle_fill(sw / 2, sh / 2 - 70, ring + 2, rgb(0x0d,0x15,0x2b));
    gfx_circle(sw / 2, sh / 2 - 70, ring + 18, COL_PRIMARY);
    gfx_circle(sw / 2, sh / 2 - 70, ring + 28, COL_ACCENT);

    gfx_circle_fill(sw / 2, sh / 2 - 70, ring - 10, COL_PRIMARY);
    gfx_rect(sw / 2 - ring / 2, sh / 2 - 70 - ring + 10, ring, ring * 2 - 20, rgb(0x0d,0x15,0x2b));
    gfx_str_centered_ex(0, sh / 2 - 83, sw, "OS", COL_ACCENT, COL_TRANSPARENT, FONT_H2);

    gfx_str_centered_ex(0, sh / 2 + 12, sw, "CareOS", COL_WHITE, COL_TRANSPARENT, FONT_H1);
    gfx_str_centered(0, sh / 2 + 12 + gfx_line_h_ex(FONT_H1) + 2, sw,
        "Performance focused desktop operating environment",
        COL_DIM, COL_TRANSPARENT);

    gfx_rect_rounded(sw / 2 - bar_w / 2, by, bar_w, bar_h, bar_h / 2, rgb(0x0b,0x10,0x20));
    gfx_rect_rounded_outline(sw / 2 - bar_w / 2, by, bar_w, bar_h, bar_h / 2, COL_BORDER);

    if (done > 0) {
        i32 fill = (bar_w - 2) * done / BOOT_STAGE_COUNT;
        if (fill < 0) fill = 0;
        gfx_rect_rounded(sw / 2 - bar_w / 2 + 1, by + 1, fill, bar_h - 2, (bar_h - 2) / 2,
            done >= BOOT_STAGE_COUNT ? COL_GREEN : COL_PRIMARY);
    }

    for (int i = 0; i < BOOT_STAGE_COUNT; i++) {
        i32 dx = sw / 2 - bar_w / 2 + (bar_w * i / (BOOT_STAGE_COUNT - 1));
        u32 dc = (i < done) ? COL_GREEN : (i == done ? COL_ACCENT : rgb(0x2a,0x35,0x54));
        gfx_circle_fill(dx, by + bar_h / 2, 4, dc);
    }

    {
        i32 lh      = FONT_H * (i32)GFX_FONT_SCALE;
        i32 stage_y = by + bar_h + 11;
        gfx_str_centered(0, stage_y, sw,
            (done >= BOOT_STAGE_COUNT) ? "Boot sequence complete" : BOOT_STAGES[done],
            COL_TEXT, COL_TRANSPARENT);

        const char spin[] = "|/-\\";
        char spin_buf[24] = "Loader: [ ]";
        spin_buf[9] = spin[tick % 4];
        gfx_str_centered(0, stage_y + lh + 2, sw, spin_buf, COL_MUTED, COL_TRANSPARENT);
    }

    gfx_str_centered(0, sh - 22, sw,
        "CareOS v9.0  |  April 2026  |  x86_64",
        COL_MUTED, COL_TRANSPARENT);
}

static void draw_elite_wallpaper(void) {
    i32 sw = (i32)SCREEN_W;
    i32 sh = (i32)SCREEN_H;

    /* An installed /system/wallpapers/default image wins; the procedural
     * gradient below is the fallback, not the other way round. wallpaper_draw()
     * cover-fits, so the image needs no particular aspect ratio and survives a
     * resolution change without reloading. */
    if (wallpaper_draw(0, 0, sw, sh)) return;

    i32 cx = sw * 45 / 100;
    i32 cy = sh * 42 / 100;
    i32 r  = sw * 36 / 100;

    /* Deeper navy-to-midnight gradient matching the reference */
    gfx_gradient_rect(0, 0, sw, sh, rgb(0x09, 0x12, 0x2e), rgb(0x05, 0x07, 0x14));

    /* Angular depth planes */
    gfx_triangle_fill(sw / 4, 0, sw * 2 / 3, 0, sw / 7, sh, rgb(0x10, 0x24, 0x58));
    gfx_triangle_fill(sw * 2 / 3, sh, sw, sh * 2 / 5, sw, sh, rgb(0x19, 0x0b, 0x38));
    gfx_rect_blend(0, 0, sw, sh, rgb(0x04, 0x07, 0x12), 32);

    /* Main crescent circle */
    gfx_circle_fill(cx, cy, r, rgb(0x2a, 0x5e, 0xcc));
    gfx_circle_fill(cx + r / 4, cy - r / 10, r - 52, rgb(0x0a, 0x13, 0x2c));
    gfx_rect_blend(0, 0, sw, sh, rgb(0x06, 0x09, 0x18), 55);

    /* Bottom triangle accent (purple) */
    gfx_triangle_fill(sw / 5, sh, sw * 2 / 5, sh, sw * 3 / 10, sh * 3 / 4, rgb(0x50, 0x3e, 0xc0));
    gfx_rect_blend(0, sh * 3/5, sw, sh * 2/5, rgb(0x02, 0x03, 0x0a), 90);
}

/* Desktop backdrop for the main loop: blit the cached wallpaper when it is
 * valid (one memcpy), otherwise render it the slow way once and cache the
 * result. draw_elite_wallpaper() is still used directly on the login/lock
 * screens, which composite changing overlays on top and are not hot paths. */
static void draw_status_wifi(i32 x, i32 y, bool up) {
    u32 col = up ? COL_TEXT : COL_MUTED;
    gfx_hline(x + 2, y + 8, 12, col);
    gfx_line(x + 4, y + 5, x + 8, y + 1, col);
    gfx_line(x + 8, y + 1, x + 12, y + 5, col);
    gfx_line(x + 6, y + 6, x + 8, y + 4, col);
    gfx_line(x + 8, y + 4, x + 10, y + 6, col);
    gfx_circle_fill(x + 8, y + 10, 2, up ? COL_GREEN : COL_MUTED);
}

static void draw_status_speaker(i32 x, i32 y) {
    gfx_rect(x, y + 5, 4, 6, COL_TEXT);
    gfx_triangle_fill(x + 4, y + 5, x + 10, y + 1, x + 10, y + 15, COL_TEXT);
    gfx_line(x + 13, y + 5, x + 15, y + 7, COL_DIM);
    gfx_line(x + 15, y + 7, x + 15, y + 9, COL_DIM);
    gfx_line(x + 15, y + 9, x + 13, y + 11, COL_DIM);
}

static void draw_status_battery(i32 x, i32 y) {
    gfx_rect_rounded_outline(x, y + 4, 20, 10, 3, COL_DIM);
    gfx_rect(x + 20, y + 7, 2, 4, COL_DIM);
    gfx_rect_rounded(x + 2, y + 6, 14, 6, 2, COL_GREEN);
}

/* =============================================================================
 * CareOS Control Center & Notification Center State & Core Implementation
 * ============================================================================= */

/* Control Center State */
static bool s_control_center_open = false;
static bool s_wifi_enabled = true;
static bool s_bt_enabled = true;
static bool s_dnd_enabled = false;
static int  s_brightness = 100; /* 10% to 100% */
static int  s_volume = 80;     /* 0% to 100% */

/* Notification Center State */
typedef struct {
    char title[36];
    char body[128];
    char time_str[16];
    u32  color;
    bool active;
} nc_item_t;

#define NC_MAX_ITEMS 16
static nc_item_t s_nc_items[NC_MAX_ITEMS];
static int s_nc_count = 0;
static bool s_notif_center_open = false;
static i32 s_notif_center_anim_x = 0;

/* Honoured by the desktop loop: lock the session on the next iteration. */
volatile bool g_lock_request = false;
static bool s_nc_inited = false;

static void control_center_add_notif(const char *title, const char *body, u32 color) {
    if (s_nc_count < NC_MAX_ITEMS) {
        for (int i = s_nc_count; i > 0; i--) {
            s_nc_items[i] = s_nc_items[i - 1];
        }
        s_nc_count++;
    } else {
        for (int i = NC_MAX_ITEMS - 1; i > 0; i--) {
            s_nc_items[i] = s_nc_items[i - 1];
        }
    }
    kstrncpy(s_nc_items[0].title, title, 35);
    s_nc_items[0].title[35] = '\0';
    kstrncpy(s_nc_items[0].body, body, 127);
    s_nc_items[0].body[127] = '\0';
    kstrcpy(s_nc_items[0].time_str, "Just now");
    s_nc_items[0].color = color ? color : COL_ACCENT;
    s_nc_items[0].active = true;

    notify_push(title, body, color);
}

static void init_nc_default_items(void) {
    if (s_nc_inited) return;
    s_nc_inited = true;
    control_center_add_notif("Welcome to CareOS", "Wave 2 Control & Notification Center active.", COL_PRIMARY);
    control_center_add_notif("Wi-Fi Connected", "Joined high-speed network 'CareOS-5G'.", COL_GREEN);
    control_center_add_notif("System Status", "All background services operating normally.", COL_ACCENT);
}

static void draw_top_bar(const mouse_t *m) {
    i32 sw = (i32)SCREEN_W;
    i32 sc = (i32)GFX_FONT_SCALE;
    i32 fw = (i32)(FONT_W * GFX_FONT_SCALE);
    i32 ty = (TOPBAR_H - (i32)(FONT_H * sc)) / 2;

    /* Solid semi-transparent band */
    gfx_rect_blend(0, 0, sw, TOPBAR_H, g_theme->taskbar, 210);
    gfx_hline(0, TOPBAR_H - 1, sw, COL_BORDER);

    /* Left: logo badge + menu labels */
    i32 x = 8;
    /* Logo circle */
    gfx_circle_fill(x + 10, TOPBAR_H / 2, 10, COL_PRIMARY);
    gfx_circle_fill(x + 13, TOPBAR_H / 2,  7, g_theme->taskbar);
    gfx_str(x + 6, ty, "C", COL_ACCENT, COL_TRANSPARENT);
    x += 26;
    gfx_str(x, ty, "CareOS", COL_WHITE, COL_TRANSPARENT);
    x += gfx_str_width("CareOS") + fw * 3;
    gfx_str(x, ty, "Machine", COL_DIM, COL_TRANSPARENT);
    x += gfx_str_width("Machine") + fw * 3;
    gfx_str(x, ty, "View", COL_DIM, COL_TRANSPARENT);

    /* Right: status icons + clock + control/notification center triggers */
    rtc_time_t t; rtc_read(&t);
    int hour = (int)t.hour - 7;
    if (hour < 0) { hour += 24; if (t.day > 0) t.day--; }
    
    const careos_settings_t *cfg = settings_get();
    bool h24 = cfg && cfg->clock_24h;

    int h_disp = hour;
    const char *ampm = "";
    if (!h24) {
        ampm = (hour >= 12) ? " PM" : " AM";
        h_disp = hour % 12;
        if (h_disp == 0) h_disp = 12;
    }

    const char *months[] = {"Jan","Feb","Mar","Apr","May","Jun",
                            "Jul","Aug","Sep","Oct","Nov","Dec"};
    const char *mon = (t.month >= 1 && t.month <= 12) ? months[t.month - 1] : "---";
    char clock_s[64];
    if (h24) {
        ksprintf(clock_s, "%s %d, %02d:%02d", mon, (int)t.day, h_disp, (int)t.minute);
    } else {
        ksprintf(clock_s, "%s %d, %d:%02d%s", mon, (int)t.day, h_disp, (int)t.minute, ampm);
    }

    i32 clk_w = gfx_str_width(clock_s);
    i32 tx    = sw - clk_w - fw * 2;

    /* Notification Center trigger button (Bell / [N]) */
    i32 bell_w = 32;
    i32 bell_x = tx - bell_w - 8;
    bool nc_hover = m && rect_contains(rect_make(bell_x, 2, bell_w + clk_w + fw * 2, TOPBAR_H - 4), m->x, m->y);

    if (s_notif_center_open) {
        gfx_rect_rounded(bell_x, 4, bell_w + clk_w + fw * 2 - 4, TOPBAR_H - 8, 6, COL_PRIMARY);
    } else if (nc_hover) {
        gfx_rect_rounded(bell_x, 4, bell_w + clk_w + fw * 2 - 4, TOPBAR_H - 8, 6, COL_SURFACE2);
    }

    /* A little drawn bell instead of literal "[N]" text. */
    {
        u32 bc  = s_notif_center_open ? COL_WHITE : COL_ACCENT;
        i32 bx  = bell_x + 13;
        i32 byc = TOPBAR_H / 2;
        gfx_circle_fill(bx, byc - 8, 2, bc);          /* handle           */
        gfx_circle_fill(bx, byc - 1, 6, bc);          /* dome             */
        gfx_rect(bx - 6, byc - 1, 13, 5, bc);         /* body             */
        gfx_rect_rounded(bx - 8, byc + 3, 17, 3, 1, bc); /* flared rim    */
        gfx_circle_fill(bx, byc + 8, 2, bc);          /* clapper          */
    }
    if (s_nc_count > 0) {
        gfx_circle_fill(bell_x + bell_w - 6, 9, 4, COL_RED);
        gfx_circle(bell_x + bell_w - 6, 9, 4, g_theme->taskbar);
    }

    gfx_str(tx, ty, clock_s, COL_WHITE, COL_TRANSPARENT);

    /* Control Center trigger button (Status icons area) */
    i32 cc_btn_x = bell_x - 94;
    i32 cc_btn_w = 88;
    bool cc_hover = m && rect_contains(rect_make(cc_btn_x - 4, 2, cc_btn_w + 8, TOPBAR_H - 4), m->x, m->y);

    if (s_control_center_open) {
        gfx_rect_rounded(cc_btn_x - 4, 4, cc_btn_w + 8, TOPBAR_H - 8, 6, COL_PRIMARY);
    } else if (cc_hover) {
        gfx_rect_rounded(cc_btn_x - 4, 4, cc_btn_w + 8, TOPBAR_H - 8, 6, COL_SURFACE2);
    }

    draw_status_wifi(cc_btn_x, (TOPBAR_H - 16) / 2, s_wifi_enabled);
    draw_status_speaker(cc_btn_x + 28, (TOPBAR_H - 16) / 2);
    draw_status_battery(cc_btn_x + 58, (TOPBAR_H - 16) / 2);
}

static bool handle_top_bar_mouse(mouse_t *m) {
    if (!m || !m->left_clicked) return false;
    if (m->y >= TOPBAR_H) return false;

    i32 sw = (i32)SCREEN_W;
    i32 fw = (i32)(FONT_W * GFX_FONT_SCALE);
    i32 clk_w = gfx_str_width("Jan 00, 00:00 AM");
    i32 bell_w = 32;
    i32 bell_x = sw - clk_w - fw * 2 - bell_w - 16;
    i32 cc_btn_x = bell_x - 94;

    /* Click on Notification Center / Clock area */
    if (m->x >= bell_x - 10 && m->x <= sw) {
        s_notif_center_open = !s_notif_center_open;
        if (s_notif_center_open) s_control_center_open = false;
        return true;
    }

    /* Click on Control Center / Status Icons area */
    if (m->x >= cc_btn_x - 10 && m->x < bell_x - 10) {
        s_control_center_open = !s_control_center_open;
        if (s_control_center_open) s_notif_center_open = false;
        return true;
    }

    return false;
}

static void draw_control_center(mouse_t *m) {
    if (!s_control_center_open) return;

    i32 sw = (i32)SCREEN_W;
    i32 pw = 340;
    i32 ph = 330;
    i32 px = sw - pw - 14;
    i32 py = TOPBAR_H + 8;
    rect_t panel_r = rect_make(px, py, pw, ph);

    /* Glass Panel background & soft shadow */
    gfx_shadow_soft(px, py, pw, ph, 16);
    draw_glass_panel(panel_r, 16);

    /* Header */
    gfx_str_ex(px + 18, py + 16, "Control Center", COL_TEXT, COL_TRANSPARENT, FONT_H2);
    /* Close X button */
    rect_t close_r = rect_make(px + pw - 34, py + 14, 20, 20);
    bool close_hov = m && rect_contains(close_r, m->x, m->y);
    gfx_circle_fill(close_r.x + 10, close_r.y + 10, 10, close_hov ? COL_RED : COL_SURFACE2);
    gfx_str(close_r.x + 6, close_r.y + 3, "x", COL_WHITE, COL_TRANSPARENT);

    /* --- Tile Row 1: Wi-Fi & Bluetooth --- */
    /* Wi-Fi Tile */
    rect_t wifi_r = rect_make(px + 16, py + 48, 146, 56);
    bool wifi_hov = m && rect_contains(wifi_r, m->x, m->y);
    u32 wifi_bg = s_wifi_enabled ? COL_PRIMARY : (wifi_hov ? COL_SURFACE3 : COL_SURFACE2);
    gfx_rect_rounded(wifi_r.x, wifi_r.y, wifi_r.w, wifi_r.h, 12, wifi_bg);
    gfx_rect_rounded_outline(wifi_r.x, wifi_r.y, wifi_r.w, wifi_r.h, 12, wifi_hov ? COL_ACCENT : COL_BORDER);
    draw_status_wifi(wifi_r.x + 14, wifi_r.y + 20, s_wifi_enabled);
    gfx_str_bold(wifi_r.x + 40, wifi_r.y + 12, "Wi-Fi", s_wifi_enabled ? COL_WHITE : COL_TEXT, COL_TRANSPARENT);
    gfx_str(wifi_r.x + 40, wifi_r.y + 30, s_wifi_enabled ? "CareOS-5G" : "Off", s_wifi_enabled ? rgb(0xe0,0xf2,0xfe) : COL_MUTED, COL_TRANSPARENT);

    /* Bluetooth Tile */
    rect_t bt_r = rect_make(px + 178, py + 48, 146, 56);
    bool bt_hov = m && rect_contains(bt_r, m->x, m->y);
    u32 bt_bg = s_bt_enabled ? rgb(0x7c, 0x3a, 0xed) : (bt_hov ? COL_SURFACE3 : COL_SURFACE2);
    gfx_rect_rounded(bt_r.x, bt_r.y, bt_r.w, bt_r.h, 12, bt_bg);
    gfx_rect_rounded_outline(bt_r.x, bt_r.y, bt_r.w, bt_r.h, 12, bt_hov ? COL_ACCENT : COL_BORDER);
    gfx_str_bold(bt_r.x + 14, bt_r.y + 18, "BT", s_bt_enabled ? COL_WHITE : COL_MUTED, COL_TRANSPARENT);
    gfx_str_bold(bt_r.x + 40, bt_r.y + 12, "Bluetooth", s_bt_enabled ? COL_WHITE : COL_TEXT, COL_TRANSPARENT);
    gfx_str(bt_r.x + 40, bt_r.y + 30, s_bt_enabled ? "On" : "Off", s_bt_enabled ? rgb(0xed,0xe9,0xfe) : COL_MUTED, COL_TRANSPARENT);

    /* --- Tile Row 2: Do Not Disturb & Theme --- */
    /* DND Tile */
    rect_t dnd_r = rect_make(px + 16, py + 112, 146, 48);
    bool dnd_hov = m && rect_contains(dnd_r, m->x, m->y);
    u32 dnd_bg = s_dnd_enabled ? rgb(0xdb, 0x27, 0x77) : (dnd_hov ? COL_SURFACE3 : COL_SURFACE2);
    gfx_rect_rounded(dnd_r.x, dnd_r.y, dnd_r.w, dnd_r.h, 12, dnd_bg);
    gfx_rect_rounded_outline(dnd_r.x, dnd_r.y, dnd_r.w, dnd_r.h, 12, dnd_hov ? COL_ACCENT : COL_BORDER);
    gfx_str_bold(dnd_r.x + 14, dnd_r.y + 15, "DND", s_dnd_enabled ? COL_WHITE : COL_TEXT, COL_TRANSPARENT);
    gfx_str(dnd_r.x + 50, dnd_r.y + 15, s_dnd_enabled ? "On" : "Off", s_dnd_enabled ? COL_WHITE : COL_MUTED, COL_TRANSPARENT);

    /* Theme Tile */
    rect_t theme_r = rect_make(px + 178, py + 112, 146, 48);
    bool theme_hov = m && rect_contains(theme_r, m->x, m->y);
    u32 theme_bg = theme_hov ? COL_SURFACE3 : COL_SURFACE2;
    gfx_rect_rounded(theme_r.x, theme_r.y, theme_r.w, theme_r.h, 12, theme_bg);
    gfx_rect_rounded_outline(theme_r.x, theme_r.y, theme_r.w, theme_r.h, 12, theme_hov ? COL_ACCENT : COL_BORDER);
    gfx_str_bold(theme_r.x + 14, theme_r.y + 15, "Theme", COL_TEXT, COL_TRANSPARENT);
    gfx_str(theme_r.x + 65, theme_r.y + 15, g_theme->is_dark ? "Dark" : "Light", COL_ACCENT, COL_TRANSPARENT);

    /* --- Row 3: Brightness Slider --- */
    rect_t bright_box = rect_make(px + 16, py + 168, 308, 64);
    gfx_rect_rounded(bright_box.x, bright_box.y, bright_box.w, bright_box.h, 12, COL_SURFACE2);
    gfx_rect_rounded_outline(bright_box.x, bright_box.y, bright_box.w, bright_box.h, 12, COL_BORDER);

    gfx_str_bold(bright_box.x + 14, bright_box.y + 10, "Display Brightness", COL_TEXT, COL_TRANSPARENT);
    char b_str[16];
    ksprintf(b_str, "%d%%", s_brightness);
    gfx_str_right(bright_box.x, bright_box.y + 10, bright_box.w - 14, b_str, COL_ACCENT, COL_TRANSPARENT);

    rect_t b_minus = rect_make(bright_box.x + 12, bright_box.y + 32, 28, 22);
    rect_t b_plus  = rect_make(bright_box.x + bright_box.w - 40, bright_box.y + 32, 28, 22);
    rect_t b_bar   = rect_make(bright_box.x + 48, bright_box.y + 37, bright_box.w - 96, 12);

    gfx_rect_rounded(b_minus.x, b_minus.y, b_minus.w, b_minus.h, 6, m && rect_contains(b_minus, m->x, m->y) ? COL_SURFACE3 : COL_SURFACE);
    gfx_str_centered(b_minus.x, b_minus.y + 3, b_minus.w, "-", COL_TEXT, COL_TRANSPARENT);

    gfx_rect_rounded(b_plus.x, b_plus.y, b_plus.w, b_plus.h, 6, m && rect_contains(b_plus, m->x, m->y) ? COL_SURFACE3 : COL_SURFACE);
    gfx_str_centered(b_plus.x, b_plus.y + 3, b_plus.w, "+", COL_TEXT, COL_TRANSPARENT);

    gfx_rect_rounded(b_bar.x, b_bar.y, b_bar.w, b_bar.h, 6, COL_SURFACE);
    i32 b_fill = b_bar.w * s_brightness / 100;
    if (b_fill > 0) {
        gfx_rect_rounded(b_bar.x, b_bar.y, b_fill, b_bar.h, 6, COL_YELLOW);
    }

    /* --- Row 4: Volume Slider --- */
    rect_t vol_box = rect_make(px + 16, py + 240, 308, 64);
    gfx_rect_rounded(vol_box.x, vol_box.y, vol_box.w, vol_box.h, 12, COL_SURFACE2);
    gfx_rect_rounded_outline(vol_box.x, vol_box.y, vol_box.w, vol_box.h, 12, COL_BORDER);

    gfx_str_bold(vol_box.x + 14, vol_box.y + 10, "Sound Volume", COL_TEXT, COL_TRANSPARENT);
    char v_str[16];
    ksprintf(v_str, "%d%%", s_volume);
    gfx_str_right(vol_box.x, vol_box.y + 10, vol_box.w - 14, v_str, COL_ACCENT, COL_TRANSPARENT);

    rect_t v_minus = rect_make(vol_box.x + 12, vol_box.y + 32, 28, 22);
    rect_t v_plus  = rect_make(vol_box.x + vol_box.w - 40, vol_box.y + 32, 28, 22);
    rect_t v_bar   = rect_make(vol_box.x + 48, vol_box.y + 37, vol_box.w - 96, 12);

    gfx_rect_rounded(v_minus.x, v_minus.y, v_minus.w, v_minus.h, 6, m && rect_contains(v_minus, m->x, m->y) ? COL_SURFACE3 : COL_SURFACE);
    gfx_str_centered(v_minus.x, v_minus.y + 3, v_minus.w, "-", COL_TEXT, COL_TRANSPARENT);

    gfx_rect_rounded(v_plus.x, v_plus.y, v_plus.w, v_plus.h, 6, m && rect_contains(v_plus, m->x, m->y) ? COL_SURFACE3 : COL_SURFACE);
    gfx_str_centered(v_plus.x, v_plus.y + 3, v_plus.w, "+", COL_TEXT, COL_TRANSPARENT);

    gfx_rect_rounded(v_bar.x, v_bar.y, v_bar.w, v_bar.h, 6, COL_SURFACE);
    i32 v_fill = v_bar.w * s_volume / 100;
    if (v_fill > 0) {
        gfx_rect_rounded(v_bar.x, v_bar.y, v_fill, v_bar.h, 6, COL_GREEN);
    }
}

static bool handle_control_center_mouse(mouse_t *m) {
    if (!s_control_center_open || !m || !m->left_clicked) return false;

    i32 sw = (i32)SCREEN_W;
    i32 pw = 340;
    i32 ph = 330;
    i32 px = sw - pw - 14;
    i32 py = TOPBAR_H + 8;
    rect_t panel_r = rect_make(px, py, pw, ph);

    if (!rect_contains(panel_r, m->x, m->y)) {
        if (m->y > TOPBAR_H) {
            s_control_center_open = false;
        }
        return false;
    }

    /* Close button */
    rect_t close_r = rect_make(px + pw - 34, py + 14, 20, 20);
    if (rect_contains(close_r, m->x, m->y)) {
        s_control_center_open = false;
        return true;
    }

    /* Wi-Fi Tile */
    rect_t wifi_r = rect_make(px + 16, py + 48, 146, 56);
    if (rect_contains(wifi_r, m->x, m->y)) {
        s_wifi_enabled = !s_wifi_enabled;
        control_center_add_notif("Wi-Fi", s_wifi_enabled ? "Wi-Fi enabled and connected." : "Wi-Fi disabled.", COL_PRIMARY);
        return true;
    }

    /* Bluetooth Tile */
    rect_t bt_r = rect_make(px + 178, py + 48, 146, 56);
    if (rect_contains(bt_r, m->x, m->y)) {
        s_bt_enabled = !s_bt_enabled;
        control_center_add_notif("Bluetooth", s_bt_enabled ? "Bluetooth enabled." : "Bluetooth turned off.", rgb(0x7c, 0x3a, 0xed));
        return true;
    }

    /* DND Tile */
    rect_t dnd_r = rect_make(px + 16, py + 112, 146, 48);
    if (rect_contains(dnd_r, m->x, m->y)) {
        s_dnd_enabled = !s_dnd_enabled;
        control_center_add_notif("Do Not Disturb", s_dnd_enabled ? "DND activated." : "DND turned off.", rgb(0xdb, 0x27, 0x77));
        return true;
    }

    /* Theme Tile */
    rect_t theme_r = rect_make(px + 178, py + 112, 146, 48);
    if (rect_contains(theme_r, m->x, m->y)) {
        theme_switch(!g_theme->is_dark);
        control_center_add_notif("Theme Changed", g_theme->is_dark ? "Switched to Dark mode." : "Switched to Light mode.", COL_ACCENT);
        return true;
    }

    /* Brightness Controls */
    rect_t bright_box = rect_make(px + 16, py + 168, 308, 64);
    rect_t b_minus = rect_make(bright_box.x + 12, bright_box.y + 32, 28, 22);
    rect_t b_plus  = rect_make(bright_box.x + bright_box.w - 40, bright_box.y + 32, 28, 22);
    rect_t b_bar   = rect_make(bright_box.x + 48, bright_box.y + 37, bright_box.w - 96, 12);

    if (rect_contains(b_minus, m->x, m->y)) {
        if (s_brightness > 10) s_brightness -= 10;
        return true;
    }
    if (rect_contains(b_plus, m->x, m->y)) {
        if (s_brightness < 100) s_brightness += 10;
        return true;
    }
    if (rect_contains(b_bar, m->x, m->y)) {
        i32 rel_x = m->x - b_bar.x;
        s_brightness = ui_clampi((rel_x * 100) / b_bar.w, 10, 100);
        return true;
    }

    /* Volume Controls */
    rect_t vol_box = rect_make(px + 16, py + 240, 308, 64);
    rect_t v_minus = rect_make(vol_box.x + 12, vol_box.y + 32, 28, 22);
    rect_t v_plus  = rect_make(vol_box.x + vol_box.w - 40, vol_box.y + 32, 28, 22);
    rect_t v_bar   = rect_make(vol_box.x + 48, vol_box.y + 37, vol_box.w - 96, 12);

    if (rect_contains(v_minus, m->x, m->y)) {
        if (s_volume > 0) s_volume -= 10;
        speaker_beep(400, 20);
        return true;
    }
    if (rect_contains(v_plus, m->x, m->y)) {
        if (s_volume < 100) s_volume += 10;
        speaker_beep(800, 20);
        return true;
    }
    if (rect_contains(v_bar, m->x, m->y)) {
        i32 rel_x = m->x - v_bar.x;
        s_volume = ui_clampi((rel_x * 100) / v_bar.w, 0, 100);
        speaker_beep(600, 20);
        return true;
    }

    return true;
}

static void draw_notification_center(mouse_t *m) {
    init_nc_default_items();

    i32 sw = (i32)SCREEN_W;
    i32 sh = (i32)SCREEN_H;
    i32 pw = 340;
    i32 ph = sh - TOPBAR_H;

    i32 target_x = s_notif_center_open ? (sw - pw) : sw;

    if (s_notif_center_anim_x < target_x) {
        s_notif_center_anim_x += 40;
        if (s_notif_center_anim_x > target_x) s_notif_center_anim_x = target_x;
    } else if (s_notif_center_anim_x > target_x) {
        s_notif_center_anim_x -= 40;
        if (s_notif_center_anim_x < target_x) s_notif_center_anim_x = target_x;
    }

    if (s_notif_center_anim_x >= sw) return;

    i32 px = s_notif_center_anim_x;
    i32 py = TOPBAR_H;

    gfx_shadow_soft(px - 10, py, pw + 10, ph, 0);
    gfx_rect_rounded(px, py, pw, ph, 0, COL_SURFACE);
    gfx_rect_blend(px, py, pw, ph, COL_GLASS_TINT, g_theme->is_dark ? 45 : 25);
    gfx_vline(px, py, ph, COL_BORDER);

    gfx_str_ex(px + 18, py + 16, "Notifications", COL_TEXT, COL_TRANSPARENT, FONT_H2);

    rect_t clear_r = rect_make(px + pw - 130, py + 16, 80, 24);
    bool clear_hov = m && rect_contains(clear_r, m->x, m->y);
    gfx_rect_rounded(clear_r.x, clear_r.y, clear_r.w, clear_r.h, 6, clear_hov ? COL_SURFACE3 : COL_SURFACE2);
    gfx_str_centered(clear_r.x, clear_r.y + 4, clear_r.w, "Clear All", COL_TEXT, COL_TRANSPARENT);

    rect_t close_r = rect_make(px + pw - 34, py + 18, 20, 20);
    bool close_hov = m && rect_contains(close_r, m->x, m->y);
    gfx_circle_fill(close_r.x + 10, close_r.y + 10, 10, close_hov ? COL_RED : COL_SURFACE2);
    gfx_str(close_r.x + 6, close_r.y + 3, "x", COL_WHITE, COL_TRANSPARENT);

    gfx_hline(px + 16, py + 48, pw - 32, COL_BORDER);

    i32 item_y = py + 58;
    int visible_count = 0;

    for (int i = 0; i < s_nc_count; i++) {
        if (!s_nc_items[i].active) continue;
        visible_count++;

        rect_t item_r = rect_make(px + 16, item_y, pw - 32, 74);
        if (item_y + item_r.h > py + ph - 16) break;

        bool item_hov = m && rect_contains(item_r, m->x, m->y);

        gfx_rect_rounded(item_r.x, item_r.y, item_r.w, item_r.h, 10, item_hov ? COL_SURFACE3 : COL_SURFACE2);
        gfx_rect_rounded_outline(item_r.x, item_r.y, item_r.w, item_r.h, 10, COL_BORDER);

        gfx_rect_rounded(item_r.x + 2, item_r.y + 6, 4, item_r.h - 12, 2, s_nc_items[i].color);

        gfx_str_bold(item_r.x + 14, item_r.y + 10, s_nc_items[i].title, COL_TEXT, COL_TRANSPARENT);
        gfx_str_right(item_r.x, item_r.y + 10, item_r.w - 30, s_nc_items[i].time_str, COL_MUTED, COL_TRANSPARENT);

        gfx_str_clipped(item_r.x + 14, item_r.y + 32, item_r.w - 44, s_nc_items[i].body, COL_DIM, COL_TRANSPARENT);

        rect_t del_r = rect_make(item_r.x + item_r.w - 24, item_r.y + 8, 16, 16);
        if (m && rect_contains(del_r, m->x, m->y)) {
            gfx_circle_fill(del_r.x + 8, del_r.y + 8, 8, COL_RED);
        }

        item_y += item_r.h + 10;
    }

    if (visible_count == 0) {
        gfx_str_centered(px, py + ph / 2 - 10, pw, "No Notifications", COL_MUTED, COL_TRANSPARENT);
    }
}

static bool handle_notification_center_mouse(mouse_t *m) {
    if (!s_notif_center_open || !m || !m->left_clicked) return false;

    i32 sw = (i32)SCREEN_W;
    i32 sh = (i32)SCREEN_H;
    i32 pw = 340;
    i32 ph = sh - TOPBAR_H;
    i32 px = sw - pw;
    i32 py = TOPBAR_H;
    rect_t panel_r = rect_make(px, py, pw, ph);

    if (!rect_contains(panel_r, m->x, m->y)) {
        if (m->y > TOPBAR_H) {
            s_notif_center_open = false;
        }
        return false;
    }

    /* Clear All Button */
    rect_t clear_r = rect_make(px + pw - 130, py + 16, 80, 24);
    if (rect_contains(clear_r, m->x, m->y)) {
        s_nc_count = 0;
        return true;
    }

    /* Close Button */
    rect_t close_r = rect_make(px + pw - 34, py + 18, 20, 20);
    if (rect_contains(close_r, m->x, m->y)) {
        s_notif_center_open = false;
        return true;
    }

    /* Individual item dismiss */
    i32 item_y = py + 58;
    for (int i = 0; i < s_nc_count; i++) {
        if (!s_nc_items[i].active) continue;
        rect_t item_r = rect_make(px + 16, item_y, pw - 32, 74);
        if (item_y + item_r.h > py + ph - 16) break;

        rect_t del_r = rect_make(item_r.x + item_r.w - 24, item_r.y + 8, 16, 16);
        if (rect_contains(del_r, m->x, m->y)) {
            for (int j = i; j < s_nc_count - 1; j++) {
                s_nc_items[j] = s_nc_items[j + 1];
            }
            s_nc_count--;
            return true;
        }

        item_y += item_r.h + 10;
    }

    return true;
}

typedef enum {
    LOGIN_MODE_SIGNIN = 0,
    LOGIN_MODE_SIGNUP = 1,
    /* Credentials were correct, but the account still holds a shipped
     * bootstrap password. The desktop stays unreachable until it is replaced. */
    LOGIN_MODE_MUST_CHANGE = 2,
} login_mode_t;

typedef struct {
    char username[32];
    char password[64];
    u32  user_len;
    u32  pass_len;
    u32  field; /* 0=username, 1=password (0=new, 1=confirm when MUST_CHANGE) */
    char status[96];
    u32  status_color;
    u32  failed_attempts;
    u32  lock_until_tick;
    login_mode_t mode;

    /* MUST_CHANGE mode only. username still holds the authenticated account. */
    char newpass[64];
    u32  newpass_len;
    char confirm[64];
    u32  confirm_len;
    char verified_pass[64];   /* the old password, to authorise the change */
} login_state_t;

typedef struct {
    rect_t panel;
    rect_t avatar;
    rect_t user_field;
    rect_t pass_field;
    rect_t status_bar;
    button_t primary_btn;
    button_t secondary_btn;
    /* Text baselines derived from the live line height, so draw_login_screen
     * and the hit-testing layout can never drift apart. */
    i32 wordmark_y;
    i32 title_y;
    i32 subtitle_y;
    i32 label_dy;     /* how far a field's label sits above the field top */
    i32 status_ty;
    i32 footer_y;
} login_layout_t;

static void login_set_status(login_state_t *s, const char *msg, u32 color) {
    kstrncpy(s->status, msg, sizeof(s->status) - 1);
    s->status[sizeof(s->status) - 1] = '\0';
    s->status_color = color;
}

static void login_mask_n(char *out, u32 max, u32 n) {
    if (n >= max) n = max - 1;
    for (u32 i = 0; i < n; i++) out[i] = '*';
    out[n] = '\0';
}

static void login_mask_password(const login_state_t *s, char *out, u32 max) {
    login_mask_n(out, max, s->pass_len);
}

static login_layout_t login_make_layout(const login_state_t *s) {
    login_layout_t l;
    i32 sw = (i32)SCREEN_W;
    i32 sh = (i32)SCREEN_H;
    i32 pw = ui_clampi(sw * 32 / 100, 430, 560);
    i32 ph = ui_clampi(sh * 52 / 100, 420, 520);
    i32 px = (sw - pw) / 2;
    i32 py = (sh - ph) / 2;
    i32 btn_gap = 12;
    i32 btn_w = (pw - 84 - btn_gap) / 2;

    if (sw < 520) {
        pw = sw - 32;
        px = 16;
        btn_w = (pw - 84 - btn_gap) / 2;
    }
    if (sh < 560) {
        ph = sh - 56;
        py = 38;
    }

    /* Every text row in the card is stacked from the live line heights. With
     * the old 13px body face the numbers came out at the hand-tuned values
     * this layout used to hardcode (188/252 fields, 136/156 headings); with a
     * 17px face they spread instead of the subtitle descenders landing in the
     * "Username" label and the footer sitting on the card's bottom edge. */
    i32 lh       = FONT_H * (i32)GFX_FONT_SCALE;
    i32 status_h = lh + 9;

    l.panel = rect_make(px, py, pw, ph);
    l.avatar = rect_make(px + pw / 2 - 30, py + 28, 60, 60);

    l.wordmark_y = py + 98;
    l.title_y    = l.wordmark_y + gfx_line_h_ex(FONT_H2) + 12;
    l.subtitle_y = l.title_y + lh + 3;
    l.label_dy   = lh + 5;

    l.user_field = rect_make(px + 42, l.subtitle_y + lh + 4 + l.label_dy, pw - 84, 38);
    l.pass_field = rect_make(px + 42, l.user_field.y + 64, pw - 84, 38);

    l.footer_y   = py + ph - lh - 8;
    l.status_bar = rect_make(px + 42, l.footer_y - status_h - 8, pw - 84, status_h);
    l.status_ty  = l.status_bar.y + (status_h - lh) / 2;

    l.primary_btn = (button_t){
        .rect = rect_make(px + 42, l.status_bar.y - 48, btn_w, 36),
        .hover = false,
        .pressed = false,
        .active = true,
        .bg = COL_PRIMARY,
        .fg = COL_WHITE,
    };
    l.secondary_btn = (button_t){
        .rect = rect_make(px + 42 + btn_w + btn_gap, l.status_bar.y - 48, btn_w, 36),
        .hover = false,
        .pressed = false,
        .active = false,
        .bg = COL_SURFACE2,
        .fg = COL_TEXT,
    };
    if (s->mode == LOGIN_MODE_MUST_CHANGE) {
        kstrcpy(l.primary_btn.label, "Set Password");
        kstrcpy(l.secondary_btn.label, "Cancel");
    } else {
        kstrcpy(l.primary_btn.label, s->mode == LOGIN_MODE_SIGNIN ? "Sign In" : "Create Account");
        kstrcpy(l.secondary_btn.label, s->mode == LOGIN_MODE_SIGNIN ? "Create Account" : "Back To Sign In");
    }
    return l;
}

static void draw_glass_panel(rect_t r, i32 radius) {
    /* Real frosted glass (blurred backdrop + tint + sheen + hairline), matching
     * the dock/sidebar/widgets. Callers add their own soft shadow. */
    gfx_glass_panel(r.x, r.y, r.w, r.h, radius);
}

static void draw_login_screen(const login_state_t *s, mouse_t *mouse) {
    i32 sw = (i32)SCREEN_W;
    i32 sh = (i32)SCREEN_H;
    login_layout_t l = login_make_layout(s);
    char pass_mask[64];
    char title[40];
    char subtitle[80];

    draw_elite_wallpaper();
    gfx_rect_blend(0, 0, sw, sh, COL_BLACK, 96);

    gfx_shadow_soft(l.panel.x, l.panel.y, l.panel.w, l.panel.h, CDL_R_CARD);
    draw_glass_panel(l.panel, CDL_R_CARD);

    /* Branding Header */
    gfx_circle_fill(l.avatar.x + l.avatar.w / 2, l.avatar.y + l.avatar.h / 2, l.avatar.w / 2, COL_PRIMARY);
    gfx_circle_fill(l.avatar.x + l.avatar.w / 2 + 4, l.avatar.y + l.avatar.h / 2,
                    l.avatar.w / 2 - 12, COL_SURFACE);
    gfx_str_centered_ex(l.avatar.x, l.avatar.y + 15, l.avatar.w, "C", COL_ACCENT, COL_TRANSPARENT, FONT_H2);
    gfx_str_centered_ex(l.panel.x, l.wordmark_y, l.panel.w, "CareOS", COL_TEXT, COL_TRANSPARENT, FONT_H2);

    if (s->mode == LOGIN_MODE_MUST_CHANGE) {
        kstrcpy(title, "Change Your Password");
        kstrcpy(subtitle, "This account uses a default password");
    } else {
        kstrcpy(title, s->mode == LOGIN_MODE_SIGNIN ? "Welcome Back" : "Create Account");
        kstrcpy(subtitle, s->mode == LOGIN_MODE_SIGNIN
            ? "Please sign in to access your desktop"
            : "Set up a new secure local profile");
    }

    gfx_str_centered(l.panel.x, l.title_y, l.panel.w, title, COL_TEXT, COL_TRANSPARENT);
    gfx_str_centered(l.panel.x, l.subtitle_y, l.panel.w, subtitle, COL_DIM, COL_TRANSPARENT);

    /* Inputs */
    if (s->mode == LOGIN_MODE_MUST_CHANGE) {
        gfx_str(l.user_field.x, l.user_field.y - l.label_dy, "New Password", COL_MUTED, COL_TRANSPARENT);
        gfx_str(l.pass_field.x, l.pass_field.y - l.label_dy, "Confirm Password", COL_MUTED, COL_TRANSPARENT);
    } else {
        gfx_str(l.user_field.x, l.user_field.y - l.label_dy, "Username", COL_MUTED, COL_TRANSPARENT);
        gfx_str(l.pass_field.x, l.pass_field.y - l.label_dy, "Password", COL_MUTED, COL_TRANSPARENT);
    }

    {
        textinput_t u_box, p_box;
        kmemset(&u_box, 0, sizeof(u_box)); kmemset(&p_box, 0, sizeof(p_box));
        
        u_box.rect = l.user_field;
        u_box.focused = (s->field == 0);
        u_box.hover = rect_contains(l.user_field, mouse->x, mouse->y);
        p_box.rect = l.pass_field;
        p_box.focused = (s->field == 1);
        p_box.hover = rect_contains(l.pass_field, mouse->x, mouse->y);

        if (s->mode == LOGIN_MODE_MUST_CHANGE) {
            /* Both fields are secrets here, so both are masked. */
            char new_mask[64];
            login_mask_n(new_mask, sizeof(new_mask), s->newpass_len);
            kstrcpy(u_box.buf, new_mask);
            u_box.len = (u32)kstrlen(new_mask); u_box.cursor = u_box.len;
            kstrcpy(u_box.placeholder, "New password");

            login_mask_n(pass_mask, sizeof(pass_mask), s->confirm_len);
            kstrcpy(p_box.buf, pass_mask);
            p_box.len = (u32)kstrlen(pass_mask); p_box.cursor = p_box.len;
            kstrcpy(p_box.placeholder, "Repeat password");
        } else {
            kstrcpy(u_box.buf, s->username);
            u_box.len = s->user_len; u_box.cursor = s->user_len;
            kstrcpy(u_box.placeholder, "User ID");

            login_mask_password(s, pass_mask, sizeof(pass_mask));
            kstrcpy(p_box.buf, pass_mask);
            p_box.len = (u32)kstrlen(pass_mask); p_box.cursor = p_box.len;
            kstrcpy(p_box.placeholder, "Password");
        }

        textinput_draw(&u_box);
        textinput_draw(&p_box);
    }

    button_update(&l.primary_btn, mouse);
    button_update(&l.secondary_btn, mouse);
    button_draw(&l.primary_btn);
    button_draw(&l.secondary_btn);

    /* Footer / Status — plain centred text, not a button-like filled bar. */
    gfx_str_centered(l.status_bar.x, l.status_ty, l.status_bar.w, s->status, s->status_color, COL_TRANSPARENT);

    gfx_str_centered(l.panel.x, l.footer_y, l.panel.w,
                     "CareOS v9 secure desktop", COL_MUTED, COL_TRANSPARENT);
}

static bool login_try(login_state_t *s) {
    u32 now = timer_get_ticks();
    if (s->lock_until_tick > now) {
        char wait_s[12];
        char msg[96] = "Too many attempts. Wait ";
        u32 left = (s->lock_until_tick - now + PIT_HZ - 1) / PIT_HZ;
        kutoa(left, wait_s, 10);
        kstrcat(msg, wait_s);
        kstrcat(msg, "s");
        login_set_status(s, msg, COL_YELLOW);
        return false;
    }

    if (s->user_len == 0 || s->pass_len == 0) {
        login_set_status(s, "Please enter username and password", COL_YELLOW);
        return false;
    }

    if (user_login(s->username, s->password) == 0) {
        s->failed_attempts = 0;

        /* Correct credentials, but a shipped bootstrap password: divert to a
         * mandatory change instead of reaching the desktop. */
        if (user_must_change_password()) {
            kstrncpy(s->verified_pass, s->password, sizeof(s->verified_pass) - 1);
            s->verified_pass[sizeof(s->verified_pass) - 1] = '\0';
            s->password[0] = '\0';
            s->pass_len = 0;
            s->newpass[0] = '\0'; s->newpass_len = 0;
            s->confirm[0] = '\0'; s->confirm_len = 0;
            s->mode = LOGIN_MODE_MUST_CHANGE;
            s->field = 0;
            login_set_status(s, "Default password must be changed", COL_YELLOW);
            serial_write("[login] '");
            serial_write(s->username);
            serial_write("' uses a default password, forcing change\n");
            return false;
        }

        login_set_status(s, "Login successful. Launching desktop...", COL_GREEN);
        return true;
    }

    s->failed_attempts++;
    s->pass_len = 0;
    s->password[0] = '\0';

    if (s->failed_attempts >= 5) {
        s->lock_until_tick = now + (10 * PIT_HZ);
        s->failed_attempts = 0;
        login_set_status(s, "Locked for 10s after repeated failures", COL_RED);
    } else {
        login_set_status(s, "Invalid credentials. Try again", COL_RED);
    }
    return false;
}

/* Returns true when the password was changed and the desktop may be entered. */
static bool login_apply_password_change(login_state_t *s) {
    if (s->newpass_len == 0 || s->confirm_len == 0) {
        login_set_status(s, "Enter the new password twice", COL_YELLOW);
        return false;
    }
    if (kstrcmp(s->newpass, s->confirm) != 0) {
        login_set_status(s, "Passwords do not match", COL_RED);
        s->confirm[0] = '\0';
        s->confirm_len = 0;
        s->field = 1;
        return false;
    }
    if (kstrcmp(s->newpass, s->verified_pass) == 0) {
        login_set_status(s, "Choose a password you have not used", COL_YELLOW);
        return false;
    }

    int rc = user_change_password(s->username, s->verified_pass, s->newpass);
    if (rc == 0) {
        /* Clear the secrets we were holding in this frame's state. */
        kmemset(s->newpass, 0, sizeof(s->newpass));
        kmemset(s->confirm, 0, sizeof(s->confirm));
        kmemset(s->verified_pass, 0, sizeof(s->verified_pass));
        s->newpass_len = 0;
        s->confirm_len = 0;
        login_set_status(s, "Password updated. Launching desktop...", COL_GREEN);
        serial_write("[login] password changed, entering desktop\n");
        return true;
    }

    if (rc == -2)
        login_set_status(s, "Need 8+ chars with upper, lower and a number", COL_YELLOW);
    else if (rc == -4)
        login_set_status(s, "Current password no longer valid. Sign in again", COL_RED);
    else
        login_set_status(s, "Could not change password", COL_RED);
    return false;
}

static bool login_create_account(login_state_t *s) {
    int rc;
    if (s->user_len == 0 || s->pass_len == 0) {
        login_set_status(s, "Enter a username and strong password", COL_YELLOW);
        return false;
    }

    rc = user_register(s->username, s->password);
    if (rc == 0) {
        login_set_status(s, "Account created. Sign in with your new credentials.", COL_GREEN);
        s->pass_len = 0;
        s->password[0] = '\0';
        s->field = 1;
        s->mode = LOGIN_MODE_SIGNIN;
        return false;
    }

    if (rc == -2)
        login_set_status(s, "Password must include upper/lowercase letters and a number", COL_YELLOW);
    else if (rc == -1)
        login_set_status(s, "That username is unavailable", COL_RED);
    else
        login_set_status(s, "Unable to create account right now", COL_RED);
    return false;
}

static void login_fade_out(const login_state_t *s, mouse_t *mouse) {
    for (int step = 0; step <= 10; step++) {
        draw_login_screen(s, mouse);
        gfx_rect_blend(0, 0, (i32)SCREEN_W, (i32)SCREEN_H, COL_BLACK, (u8)(step * 20));
        mouse_draw_cursor(mouse->x, mouse->y);
        gfx_flip();
        timer_wait(14);
    }
}

static void desktop_fade_in(mouse_t *mouse) {
    for (int step = 10; step >= 0; step--) {
        gfx_clear(COL_BG);
        desktop_draw();
        wm_draw_all();
        taskbar_draw();
        gfx_rect_blend(0, 0, (i32)SCREEN_W, (i32)SCREEN_H, COL_BLACK, (u8)(step * 22));
        mouse_draw_cursor(mouse->x, mouse->y);
        gfx_flip();
        timer_wait(14);
    }
}

static void run_matrix_screensaver(mouse_t *mouse) {
    i32 sw = (i32)SCREEN_W;
    i32 sh = (i32)SCREEN_H;
    /* The rain falls one text line per frame and the green trail sits exactly
     * one line above the white head, so the vertical step is the live line
     * height. The 14px column pitch stays put -- that is horizontal spacing
     * and the body advance has not changed. */
    i32 cell = FONT_H * (i32)GFX_FONT_SCALE;
    u32 cols = sw / 14;
    if (cols > 256) cols = 256;
    static i32 drops[256];
    for (u32 i = 0; i < cols; i++) drops[i] = -((i32)((timer_get_ticks() + i * 17) % sh) / cell);

    mouse_update(mouse);
    keyboard_flush();
    i32 last_lx = mouse->x, last_ly = mouse->y;

    gfx_clear(COL_BLACK);

    while (1) {
        __asm__ volatile("sti; hlt");
        if (keyboard_haschar()) { keyboard_flush(); break; }
        mouse_update(mouse);
        if (mouse->left_clicked || mouse->right_clicked || kabs(mouse->x - last_lx) > 10 || kabs(mouse->y - last_ly) > 10) break;
        
        gfx_rect_blend(0, 0, sw, sh, COL_BLACK, 25); /* Trail effect */

        for (u32 i = 0; i < cols; i++) {
            char str[2]; 
            str[0] = 33 + (timer_get_ticks() / 10 + i * 17) % 94; 
            str[1] = 0;
            
            i32 dy = drops[i] * cell;
            if (dy > 0) {
                gfx_str_bg_none((i32)i * 14, dy, str, COL_WHITE); /* Leading char is white */
                gfx_str_bg_none((i32)i * 14, dy - cell, str, COL_GREEN); /* Trail is green */
            }
            drops[i]++;
            if (dy > sh && ((timer_get_ticks() + i) % 100 > 95)) drops[i] = 0;
        }
        
        gfx_flip();
        timer_wait(35);
    }
}

static bool run_login_flow(mouse_t *mouse) {
    login_state_t login;
    kmemset(&login, 0, sizeof(login));
    login.field = 0;
    login.mode = LOGIN_MODE_SIGNIN;
    login_set_status(&login, "Sign in to continue", COL_DIM);

    keyboard_flush();
    mouse->x = (i32)SCREEN_W / 2;
    mouse->y = (i32)SCREEN_H / 2;
    serial_write("[login] login screen ready\n");

    while (1) {
        login_layout_t layout = login_make_layout(&login);

        while (keyboard_haschar()) {
            char c = keyboard_getchar();

            if (c == '\t') {
                login.field = 1 - login.field;
                continue;
            }
            if (c == '\n') {
                if (login.mode == LOGIN_MODE_MUST_CHANGE) {
                    if (login_apply_password_change(&login)) {
                        login_fade_out(&login, mouse);
                        return true;
                    }
                } else if (login.mode == LOGIN_MODE_SIGNIN) {
                    if (login_try(&login)) {
                        login_fade_out(&login, mouse);
                        return true;
                    }
                } else {
                    login_create_account(&login);
                }
                continue;
            }
            if (c == '\b') {
                if (login.mode == LOGIN_MODE_MUST_CHANGE) {
                    if (login.field == 0 && login.newpass_len > 0) {
                        login.newpass_len--;
                        login.newpass[login.newpass_len] = '\0';
                    } else if (login.field == 1 && login.confirm_len > 0) {
                        login.confirm_len--;
                        login.confirm[login.confirm_len] = '\0';
                    }
                } else if (login.field == 0 && login.user_len > 0) {
                    login.user_len--;
                    login.username[login.user_len] = '\0';
                } else if (login.field == 1 && login.pass_len > 0) {
                    login.pass_len--;
                    login.password[login.pass_len] = '\0';
                }
                continue;
            }
            if (c < 32 || c > 126) continue;

            if (login.mode == LOGIN_MODE_MUST_CHANGE) {
                if (login.field == 0) {
                    if (login.newpass_len < sizeof(login.newpass) - 1) {
                        login.newpass[login.newpass_len++] = c;
                        login.newpass[login.newpass_len] = '\0';
                    }
                } else {
                    if (login.confirm_len < sizeof(login.confirm) - 1) {
                        login.confirm[login.confirm_len++] = c;
                        login.confirm[login.confirm_len] = '\0';
                    }
                }
            } else if (login.field == 0) {
                if (login.user_len < sizeof(login.username) - 1) {
                    login.username[login.user_len++] = c;
                    login.username[login.user_len] = '\0';
                }
            } else {
                if (login.pass_len < sizeof(login.password) - 1) {
                    login.password[login.pass_len++] = c;
                    login.password[login.pass_len] = '\0';
                }
            }
        }

        mouse_update(mouse);

        if (mouse->left_clicked) {
            if (rect_contains(layout.user_field, mouse->x, mouse->y))
                login.field = 0;
            else if (rect_contains(layout.pass_field, mouse->x, mouse->y))
                login.field = 1;
            else if (button_take_click(&layout.primary_btn, mouse)) {
                if (login.mode == LOGIN_MODE_MUST_CHANGE) {
                    if (login_apply_password_change(&login)) {
                        login_fade_out(&login, mouse);
                        return true;
                    }
                } else if (login.mode == LOGIN_MODE_SIGNIN) {
                    if (login_try(&login)) {
                        login_fade_out(&login, mouse);
                        return true;
                    }
                } else {
                    login_create_account(&login);
                }
            } else if (button_take_click(&layout.secondary_btn, mouse)) {
                if (login.mode == LOGIN_MODE_MUST_CHANGE) {
                    /* Cancel drops the session and returns to sign-in. The
                     * must_change flag stays set, so there is no way past it. */
                    user_logout();
                    kmemset(&login.newpass, 0, sizeof(login.newpass));
                    kmemset(&login.confirm, 0, sizeof(login.confirm));
                    kmemset(&login.verified_pass, 0, sizeof(login.verified_pass));
                    login.newpass_len = 0;
                    login.confirm_len = 0;
                    login.pass_len = 0;
                    login.password[0] = '\0';
                    login.mode = LOGIN_MODE_SIGNIN;
                    login.field = 1;
                    login_set_status(&login, "Sign in to continue", COL_DIM);
                } else {
                    login.mode = (login.mode == LOGIN_MODE_SIGNIN) ? LOGIN_MODE_SIGNUP : LOGIN_MODE_SIGNIN;
                    login.pass_len = 0;
                    login.password[0] = '\0';
                    login.field = (login.mode == LOGIN_MODE_SIGNIN) ? 1 : 0;
                    login_set_status(&login,
                        login.mode == LOGIN_MODE_SIGNIN
                            ? "Sign in with an existing account"
                            : "Create a strong local account",
                        COL_DIM);
                }
            }
        }

        draw_login_screen(&login, mouse);
        mouse_draw_cursor(mouse->x, mouse->y);
        gfx_flip();
        __asm__ volatile("sti; hlt");
    }
}

void gui_run(void) {
    const careos_settings_t *cfg = settings_get();
    bool fast_boot = cfg && cfg->boot_fast;
    mouse_t mouse;
    i32 sw, sh, tw, th;

    serial_write("  [gui_run] splash start\n");
    if (fast_boot) {
        draw_boot_splash(BOOT_STAGE_COUNT, timer_get_ticks());
        timer_wait(20);
    } else {
        for (int i = 0; i <= BOOT_STAGE_COUNT; i++) {
            draw_boot_splash(i, timer_get_ticks());
            timer_wait(90);
        }
    }
    serial_write("  [gui_run] splash done\n");

    serial_write("  [gui_run] login gate\n");
    kmemset(&mouse, 0, sizeof(mouse));
    if (!run_login_flow(&mouse)) {
        serial_write("  [gui_run] login flow returned failure\n");
    }

    rc_care_run_startup();

    sw = (i32)SCREEN_W;
    sh = (i32)SCREEN_H;
    tw = sw * 62 / 100;
    th = sh * 66 / 100;

    wm_open(APP_TERMINAL, "Terminal", (sw - tw) / 2, (sh - th) / 2 - 24, tw, th);
    notify_push("CareOS", "Desktop ready.", COL_PRIMARY);
    speaker_startup(); /* Audible startup melody */
    desktop_fade_in(&mouse);

    serial_write("  [gui_run] entering main loop\n");

    widgets_init();

    mouse.x = sw / 2;
    mouse.y = sh / 2;

    u32 last_sysmon = 0, last_netmon = 0, last_notify = 0, last_slow = 0;
    bool needs_redraw = true;
    u32 lx = 0, ly = 0;
    bool lb = false;

    s_notif_center_anim_x = sw;

    while (1) {
            bool activity = false;

            while (keyboard_haschar()) {
                activity = true;
                char c = keyboard_getchar();
                char kl = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
                
                /* Global Activity Trigger */
                g_last_activity_tick = timer_get_ticks();

                /* Global Hotkeys */
                if (keyboard_alt_held() && c == '\t') { wm_cycle_focus(1); continue; }
                if (keyboard_alt_held() && c == 0x1B) { /* Alt+F4 (handled as ESC scan in some drivers) */
                     window_t *fw = wm_focused();
                     if (fw) wm_close(fw);
                     continue;
                }
                /* Alt+F4 detection if scan is mapped to F4 */
                // if (keyboard_alt_held() && is_f4(c)) ...

                /* Multi-desktop Switch: Ctrl + 1-4 */
                if (keyboard_ctrl_held() && c >= '1' && c <= '4') {
                    g_current_desktop = (u32)(c - '1');
                    notify_push("Workspace", (c == '1' ? "Desktop 1" : (c == '2' ? "Desktop 2" : (c == '3' ? "Desktop 3" : "Desktop 4"))), COL_ACCENT);
                    continue;
                }

                if (keyboard_alt_held() && keyboard_ctrl_held()) {
                    if (kl == 'h' || kl == 'a') { wm_snap_focused(SNAP_LEFT); continue; }
                    if (kl == 'l' || kl == 'd') { wm_snap_focused(SNAP_RIGHT); continue; }
                    if (kl == 'k' || kl == 'w') { wm_snap_focused(SNAP_TOP); continue; }
                    if (kl == 'j' || kl == 's') { wm_snap_focused(SNAP_BOTTOM); continue; }
                    if (kl == 'm' || kl == 'f') { wm_snap_focused(SNAP_FULL); continue; }
                }
                if (launcher_open) launcher_handle_key(c);
                else { window_t *fw = wm_focused(); if (fw) wm_handle_key(c, fw); }
            }

            /* Tap Super to summon / dismiss the Spotlight launcher, like GNOME
             * or Windows. Polled every frame (it is a modifier, not a char). */
            if (keyboard_super_tapped()) {
                launcher_open = !launcher_open;
                if (launcher_open) { s_notif_center_open = false; s_control_center_open = false; }
                g_last_activity_tick = timer_get_ticks();
                activity = true;
            }

            mouse_update(&mouse);
            if (mouse.x != lx || mouse.y != ly || mouse.left != lb || mouse.left_clicked || mouse.right_clicked || mouse.scroll_delta != 0) {
                activity = true; lx = mouse.x; ly = mouse.y; lb = mouse.left;
                g_last_activity_tick = timer_get_ticks();
            }
            /* A left click ripples out from the cursor, everywhere. */
            if (mouse.left_clicked) { gfx_ripple(mouse.x, mouse.y, COL_ACCENT); activity = true; }
            /* Keep redrawing while any ripple is still blooming. */
            if (gfx_ripples_active()) activity = true;

            /* Explicit lock request (Spotlight "Lock Screen", a power menu, ...). */
            if (g_lock_request) {
                g_lock_request = false;
                run_login_flow(&mouse);
                g_last_activity_tick = timer_get_ticks();
                needs_redraw = true;
            }

            /* Idle / Screensaver Check (10 minutes = 600,000 ms, screensaver at 30,000 ms) */
            u32 now = timer_get_ticks();
            if (now - g_last_activity_tick > 600000) {
                /* Auto-lock the screen if idle for 10 mins */
                serial_write("  [gui] auto-locking due to idle\n");
                run_login_flow(&mouse);
                g_last_activity_tick = timer_get_ticks();
                needs_redraw = true;
            } else if (now - g_last_activity_tick > 30000) {
                /* Show Matrix Screensaver after 30 seconds */
                run_matrix_screensaver(&mouse);
                g_last_activity_tick = timer_get_ticks();
                needs_redraw = true;
            }
            /* Step window open/close/minimise animations every frame so they
             * read as smooth motion instead of the 5 Hz steps the old 200 ms
             * gate produced. */
            if (wm_animate_all()) activity = true;

            /* Check notification center sliding animation */
            i32 nc_target_x = s_notif_center_open ? ((i32)SCREEN_W - 340) : (i32)SCREEN_W;
            if (s_notif_center_anim_x != nc_target_x) {
                activity = true;
            }

            if (now - last_sysmon >= 20) {
                window_t *sm = wm_find_app(APP_SYSMON);
                window_t *nm = wm_find_app(APP_NETMON);
                if (sm) app_sysmon_tick(sm);
                if (nm) app_netmon_tick(nm);
                last_sysmon = now;
                if (sm || nm) activity = true;   /* live graphs, only while a monitor is open */
            }
            if (now - last_netmon >= 100) { net_poll(); last_netmon = now; }
            if (now - last_notify >= 10)  { notify_tick(); last_notify = now; }

            /* Low-rate heartbeat: keeps on-screen clocks/meters ticking without
             * recompositing the whole glass desktop at 100 Hz while idle. Input
             * and animations (above) still drive full-rate redraws when needed. */
            if (now - last_slow >= 250) { last_slow = now; activity = true; }

            if (activity) needs_redraw = true;

            if (needs_redraw) {
                /* The desktop backdrop (wallpaper + frosted sidebar + widget
                 * panels) is expensive to composite -- a per-frame blur and soft
                 * shadow. It only changes when the chrome does: the clock/meters
                 * tick (~2 Hz here), the pointer hovers the sidebar or widgets,
                 * or the theme changes. Cache the composited backdrop and blit it
                 * (one memcpy) on every other frame; windows/taskbar/cursor still
                 * draw on top at full rate. This is what makes dragging and
                 * typing smooth instead of paying ~700 Mcyc of blur each frame. */
                static u32 last_backdrop_ms = 0;
                bool over_chrome = (mouse.x < (i32)SIDEBAR_W) ||
                                   (mouse.x > (i32)SCREEN_W - 360);
                bool rebuild = over_chrome || (now - last_backdrop_ms >= 500);

                if (rebuild || !gfx_desktop_cache_blit()) {
                    if (!gfx_wallpaper_cache_blit()) {
                        draw_elite_wallpaper();
                        gfx_wallpaper_cache_capture();
                    }
                    desktop_draw();
                    widgets_draw(&mouse);
                    gfx_desktop_cache_capture();
                    last_backdrop_ms = now;
                }

                wm_draw_all();
                taskbar_draw();
                draw_top_bar(&mouse);
                if (launcher_open) launcher_draw(&mouse);

                draw_notification_center(&mouse);
                draw_control_center(&mouse);

                if (s_brightness < 100) {
                    u8 dim = (u8)((100 - s_brightness) * 1.8);
                    gfx_rect_blend(0, 0, (i32)SCREEN_W, (i32)SCREEN_H, COL_BLACK, dim);
                }

                gfx_ripples_draw();
                mouse_draw_cursor(mouse.x, mouse.y);
                gfx_flip();
                needs_redraw = false;
            }

            /* Default the pointer to the arrow each frame; wm_handle_mouse
             * upgrades it to a resize/move shape when over a window edge. */
            g_cursor_shape = CURSOR_ARROW;

            /* Handle input every frame to ensure responsiveness */
            if (!notify_handle_mouse(&mouse)) {
                if (handle_control_center_mouse(&mouse)) {
                    needs_redraw = true;
                } else if (handle_notification_center_mouse(&mouse)) {
                    needs_redraw = true;
                } else if (handle_top_bar_mouse(&mouse)) {
                    needs_redraw = true;
                } else if (launcher_open) {
                    launcher_handle_mouse(&mouse);
                } else if (widgets_handle_mouse(&mouse)) {
                    needs_redraw = true;
                } else {
                    taskbar_handle_mouse(&mouse);
                    desktop_handle_mouse(&mouse);
                    wm_handle_mouse(&mouse);
                }
            }
            __asm__ volatile("sti; hlt");
    }
}
