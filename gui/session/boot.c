/* gui/session/boot.c -- the boot splash (Boot artboard).
 *
 * Ground #070A16 with a soft coral glow and two faint calm rings (rendered
 * once), the beating heart, "CareOS", a progress track and a status line.
 * Progress is real: each step is work the desktop needs before the login
 * screen -- rendering the wallpaper, rasterising the app icons, checking the
 * network -- so the bar moves as that work completes. */
#include "kernel.h"
#include "session/session.h"
#include "glass.h"
#include "spring.h"

static u32 *s_bg;                      /* ground + glow + rings, screen-sized */

static void build_backdrop(void) {
    i32 sw = (i32)SCREEN_W, sh = (i32)SCREEN_H;
    u32 n = (u32)sw * (u32)sh;
    s_bg = (u32 *)kmalloc((size_t)n * 4u);
    u32 *tmp = (u32 *)kmalloc((size_t)n * 4u);
    u32 *scratch = (u32 *)kmalloc((size_t)glass_scratch_words((u32)sw, (u32)sh) * 4u);
    if (!s_bg || !tmp || !scratch) {
        if (tmp) kfree(tmp);
        if (scratch) kfree(scratch);
        return;
    }
    /* Positions are the 1440x900 artboard's, scaled to the screen. */
    i32 S = sh * 1024 / 900;
    i32 cx = sw / 2, cy = (130 + 320) * sh / 900;
    for (u32 i = 0; i < n; i++) tmp[i] = 0x070A16;
    gfx_buffer_t off = { tmp, (u32)sw, (u32)sh, (u32)sw * 4u };
    gfx_buffer_t *prev = g_target;
    gfx_set_target(&off);
    gfx_circle_fill(cx, cy, 320 * S / 1024, ui_mix(0x070A16, 0xFF7A59, 36));   /* .14 */
    gfx_set_target(prev);
    glass_blur(tmp, (u32)sw, (u32)sw, (u32)sh, (u32)(110 * S / 1024), s_bg, scratch);
    kfree(scratch);
    kfree(tmp);

    off.pixels = s_bg;
    gfx_set_target(&off);
    gfx_circle(cx, 450 * sh / 900, 450 * S / 1024, ui_mix(0x070A16, 0x8C9BFF, 26));   /* .10 */
    gfx_circle(cx, cy, 320 * S / 1024, ui_mix(0x070A16, 0x8C9BFF, 20));               /* .08 */
    gfx_set_target(prev);
}

/* 0..256 heartbeat: 1.0 -> 1.06 -> 1.0 over 1.6 s, eased in and out. */
static u32 beat(u32 ms) {
    u32 p = ms % 1600u, t = p < 800u ? p * 256u / 800u : (1600u - p) * 256u / 800u;
    return t * t * (768u - 2u * t) / 65536u;        /* smoothstep */
}

static void frame(u32 done, u32 steps, const char *status) {
    i32 sw = (i32)SCREEN_W, sh = (i32)SCREEN_H;
    if (s_bg) gfx_blit_argb(0, 0, s_bg, sw, sh, 0, 0, sw, sh);
    else gfx_clear(0x070A16);

    i32 logo = 112, gap = 28;
    i32 title_h = gfx_role_line_h(FONT_ROLE_DISPLAY, 4), status_h = gfx_role_line_h(FONT_ROLE_UI, 2);
    i32 total = logo + gap + title_h + gap + 6 + gap + status_h;
    i32 y = (sh - total) / 2;

    /* The heart beats about its centre. */
    i32 size = logo + (i32)(beat(timer_ms()) * 7u / 256u);
    ui_care_logo(sw / 2 - size / 2, y + logo / 2 - size / 2, size);
    y += logo + gap;

    i32 tw = gfx_str_role_width("CareOS", FONT_ROLE_DISPLAY, 4, FONT_W_BOLD);
    gfx_str_role(sw / 2 - tw / 2, y, "CareOS", 0xEEF1FA, FONT_ROLE_DISPLAY, 4, FONT_W_BOLD);
    y += title_h + gap;

    i32 bx = sw / 2 - 120;
    gfx_rect_rounded(bx, y, 240, 6, 3, 0x1C2440);
    i32 fill = steps ? (i32)(240u * done / steps) : 240;
    if (fill > 6) gfx_rect_rounded(bx, y, fill, 6, 3, 0xFF7A59);
    y += 6 + gap;

    i32 sw2 = gfx_str_role_width(status, FONT_ROLE_UI, 2, FONT_W_REGULAR);
    gfx_str_role(sw / 2 - sw2 / 2, y, status, 0x9AA3BD, FONT_ROLE_UI, 2, FONT_W_REGULAR);

    /* Footer. (The artboard's "Hold Shift for recovery options" waits for a
     * recovery mode to exist.) */
    i32 fy = sh - 40 - gfx_role_line_h(FONT_ROLE_MONO, 1);
    gfx_str_role(48, fy, "v9.0", 0x7D86A3, FONT_ROLE_MONO, 1, FONT_W_REGULAR);
    i32 vx = 48 + gfx_str_role_width("v9.0", FONT_ROLE_MONO, 1, FONT_W_REGULAR);
    gfx_circle_fill(vx + 9, fy + gfx_role_line_h(FONT_ROLE_MONO, 1) / 2, 1, 0x7D86A3);
    gfx_str_role(vx + 18, fy, "x86_64", 0x7D86A3, FONT_ROLE_MONO, 1, FONT_W_REGULAR);
    gfx_flip();
}

/* ---- The real start-up work ------------------------------------------------- */
static void with_offscreen(void (*fn)(void)) {
    i32 sw = (i32)SCREEN_W, sh = (i32)SCREEN_H;
    u32 *tmp = (u32 *)kmalloc((size_t)sw * (size_t)sh * 4u);
    if (!tmp) { fn(); return; }
    gfx_buffer_t off = { tmp, (u32)sw, (u32)sh, (u32)sw * 4u };
    gfx_buffer_t *prev = g_target;
    gfx_set_target(&off);
    fn();
    gfx_set_target(prev);
    kfree(tmp);
}

static void work_wallpaper(void) { gui_wallpaper_draw(); }   /* fills the render cache */

static void work_icons(void) {
    static const app_id_t APPS[] = { APP_TERMINAL, APP_FILES, APP_BROWSER, APP_NOTES, APP_EDITOR,
                                     APP_PKGMGR, APP_SETTINGS };
    ui_app_icon(UI_ICON_LAUNCHER, 0, 0, 52);
    for (u32 i = 0; i < sizeof(APPS) / sizeof(APPS[0]); i++) {
        ui_app_icon(ui_icon_for_app(APPS[i]), 0, 0, 52);
        ui_app_icon(ui_icon_for_app(APPS[i]), 0, 0, 76);
    }
}

void session_boot(bool fast) {
    build_backdrop();
    const u32 STEPS = 4;
    frame(0, STEPS, "Preparing your desktop...");
    with_offscreen(work_wallpaper);
    frame(1, STEPS, "Loading your apps...");
    with_offscreen(work_icons);
    frame(2, STEPS, "Checking the network...");
    bool up = net_is_up();
    frame(3, STEPS, up ? "Connected to the network" : "No network yet");
    if (!fast) timer_wait(300);
    /* Done: let the heart beat for a moment so the splash reads, not flashes. */
    u32 t0 = timer_ms();
    do {
        frame(STEPS, STEPS, "Waking things up...");
        __asm__ volatile("sti; hlt");
    } while (!fast && (u32)(timer_ms() - t0) < 900u);
    if (s_bg) { kfree(s_bg); s_bg = NULL; }
}
