/* =============================================================================
 * CareOS gui/gui.c -- main GUI entry point with splash, login, and desktop
 * ============================================================================= */
#include "kernel.h"
#include "gui.h"

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
    gfx_str_centered(0, sh / 2 + 48, sw,
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

    gfx_str_centered(0, by + 20, sw,
        (done >= BOOT_STAGE_COUNT) ? "Boot sequence complete" : BOOT_STAGES[done],
        COL_TEXT, COL_TRANSPARENT);

    {
        const char spin[] = "|/-\\";
        char spin_buf[24] = "Loader: [ ]";
        spin_buf[9] = spin[tick % 4];
        gfx_str_centered(0, by + 34, sw, spin_buf, COL_MUTED, COL_TRANSPARENT);
    }

    gfx_str_centered(0, sh - 22, sw,
        "CareOS v9.0  |  April 2026  |  x86_64",
        COL_MUTED, COL_TRANSPARENT);
}

static void draw_elite_wallpaper(void) {
    i32 sw = (i32)SCREEN_W;
    i32 sh = (i32)SCREEN_H;
    /* Deep navy base, lighter at top, darker at bottom */
    gfx_gradient_rect(0, 0, sw, sh, rgb(0x10,0x1c,0x3d), rgb(0x06,0x0b,0x1a));
    /* Subtle blue radial glow offset right of center */
    gfx_circle_fill(sw * 3/5, sh * 2/5, sw * 2/5, rgb(0x13,0x23,0x52));
    gfx_circle_fill(sw * 3/5, sh * 2/5, sw * 2/5 - 40, rgb(0x0c,0x17,0x35));
    /* Bottom vignette */
    gfx_rect_blend(0, sh * 3/5, sw, sh * 2/5, rgb(0x02,0x04,0x0c), 100);
}

static void draw_top_bar(void) {
    i32 sw = (i32)SCREEN_W;
    i32 sc = (i32)GFX_FONT_SCALE;
    i32 fw = (i32)(FONT_W * GFX_FONT_SCALE);
    i32 ty = (TOPBAR_H - (i32)(FONT_H * sc)) / 2;

    u32 bg = g_theme->taskbar;
    gfx_rect_blend(0, 0, sw, TOPBAR_H, bg, 180);
    gfx_hline(0, TOPBAR_H - 1, sw, COL_BORDER);

    /* Left: logo icon + menu labels, spaced by actual rendered width */
    i32 x = 8;
    gfx_draw_icon(APP_NONE, x, (TOPBAR_H - 16) / 2, 16, COL_ACCENT);
    x += 20;
    gfx_str(x, ty, "CareOS", COL_WHITE, COL_TRANSPARENT);
    x += gfx_str_width("CareOS") + fw * 2;
    gfx_str(x, ty, "Machine", COL_DIM, COL_TRANSPARENT);
    x += gfx_str_width("Machine") + fw * 2;
    gfx_str(x, ty, "View", COL_DIM, COL_TRANSPARENT);

    /* Right: date + time from RTC */
    char clock_s[48];
    rtc_time_t t; rtc_read(&t);
    int h = (int)t.hour % 12;
    if (h == 0) h = 12;
    const char *months[] = {"Jan","Feb","Mar","Apr","May","Jun",
                            "Jul","Aug","Sep","Oct","Nov","Dec"};
    const char *mon = (t.month >= 1 && t.month <= 12) ? months[t.month - 1] : "---";
    ksprintf(clock_s, "%s %d, %02d:%02d %s",
             mon, (int)t.day, h, (int)t.minute, (t.hour >= 12) ? "PM" : "AM");

    i32 tx = sw - gfx_str_width(clock_s) - fw;
    gfx_str(tx, ty, clock_s, COL_WHITE, COL_TRANSPARENT);

    /* Net status dot */
    gfx_circle_fill(tx - fw * 2, TOPBAR_H / 2, 3, net_is_up() ? COL_GREEN : COL_RED);
}

typedef enum {
    LOGIN_MODE_SIGNIN = 0,
    LOGIN_MODE_SIGNUP = 1,
} login_mode_t;

typedef struct {
    char username[32];
    char password[64];
    u32  user_len;
    u32  pass_len;
    u32  field; /* 0=username, 1=password */
    char status[96];
    u32  status_color;
    u32  failed_attempts;
    u32  lock_until_tick;
    login_mode_t mode;
} login_state_t;

typedef struct {
    rect_t panel;
    rect_t avatar;
    rect_t user_field;
    rect_t pass_field;
    rect_t status_bar;
    button_t primary_btn;
    button_t secondary_btn;
} login_layout_t;

static void login_set_status(login_state_t *s, const char *msg, u32 color) {
    kstrncpy(s->status, msg, sizeof(s->status) - 1);
    s->status[sizeof(s->status) - 1] = '\0';
    s->status_color = color;
}

static void login_mask_password(const login_state_t *s, char *out, u32 max) {
    u32 n = s->pass_len;
    if (n >= max) n = max - 1;
    for (u32 i = 0; i < n; i++) out[i] = '*';
    out[n] = '\0';
}

static login_layout_t login_make_layout(const login_state_t *s) {
    login_layout_t l;
    i32 sw = (i32)SCREEN_W;
    i32 sh = (i32)SCREEN_H;
    i32 pw = ui_clampi(sw * 40 / 100, 500, 700);
    i32 ph = ui_clampi(sh * 60 / 100, 560, 720);
    i32 px = (sw - pw) / 2;
    i32 py = (sh - ph) / 2;
    i32 btn_w = (pw - 80) / 2;
    i32 sc = (i32)GFX_FONT_SCALE;
    i32 v_space = 40 * sc;

    l.panel = rect_make(px, py, pw, ph);
    l.avatar = rect_make(px + pw / 2 - 34, py + 26, 68, 68);
    l.user_field = rect_make(px + 40, py + 140 + v_space, pw - 80, 22 + 12 * sc);
    l.pass_field = rect_make(px + 40, py + 200 + v_space * 2, pw - 80, 22 + 12 * sc);
    l.primary_btn = (button_t){
        .rect = rect_make(px + 40, py + ph - (60 + 20 * sc), btn_w, 24 + 8 * sc),
        .hover = false,
        .pressed = false,
        .active = true,
        .bg = COL_PRIMARY,
        .fg = COL_WHITE,
    };
    l.secondary_btn = (button_t){
        .rect = rect_make(px + 40 + btn_w + 10, py + ph - (60 + 20 * sc), btn_w, 24 + 8 * sc),
        .hover = false,
        .pressed = false,
        .active = false,
        .bg = COL_SURFACE2,
        .fg = COL_TEXT,
    };
    kstrcpy(l.primary_btn.label, s->mode == LOGIN_MODE_SIGNIN ? "Sign In" : "Create Account");
    kstrcpy(l.secondary_btn.label, s->mode == LOGIN_MODE_SIGNIN ? "Create Account" : "Back To Sign In");
    l.status_bar = rect_make(px + 30, py + ph - 42, pw - 60, 24);
    return l;
}

static void draw_glass_panel(rect_t r, i32 radius) {
    /* Cleaner, more solid professional surface */
    gfx_shadow_ext(r.x, r.y, r.w, r.h, 10);
    gfx_rect_rounded(r.x, r.y, r.w, r.h, radius, COL_SURFACE);
    gfx_rect_blend(r.x, r.y, r.w, r.h, COL_GLASS_TINT, g_theme->is_dark ? 40 : 25);
    gfx_rect_rounded_outline(r.x, r.y, r.w, r.h, radius, COL_BORDER);
}

static void draw_login_screen(const login_state_t *s, mouse_t *mouse) {
    i32 sw = (i32)SCREEN_W;
    i32 sh = (i32)SCREEN_H;
    i32 sc = (i32)GFX_FONT_SCALE;
    login_layout_t l = login_make_layout(s);
    char pass_mask[64];
    char title[40];
    char subtitle[80];

    /* Professional Mesh Background with Depth */
    gfx_gradient_rect(0, 0, sw, sh, rgb(0x0a,0x0f,0x1a), rgb(0x1a,0x24,0x3a));
    for (i32 i = 0; i < 40; i++) {
        u32 color = g_theme->is_dark ? rgb(0x1e, 0x29, 0x3b) : rgb(0x33, 0x41, 0x55);
        i32 rx = (i * 37) % sw, ry = (i * 13) % sh;
        gfx_rect_blend(rx, ry, sw/4, 2, color, 12);
        gfx_rect_blend(rx, ry, 2, sh/4, color, 12);
    }

    draw_glass_panel(l.panel, 24);

    /* Branding Header */
    gfx_str_centered_ex(l.panel.x, l.panel.y + 30, l.panel.w, "CARE OS", COL_PRIMARY, COL_TRANSPARENT, FONT_H1);
    gfx_rect(l.panel.x + l.panel.w/2 - 30, l.panel.y + 65, 60, 2, COL_ACCENT);

    kstrcpy(title, s->mode == LOGIN_MODE_SIGNIN ? "Welcome Back" : "Create Account");
    kstrcpy(subtitle, s->mode == LOGIN_MODE_SIGNIN
        ? "Please sign in to access your desktop"
        : "Set up a new secure local profile");
    
    gfx_str_centered_ex(l.panel.x, l.panel.y + 110, l.panel.w, title, COL_TEXT, COL_TRANSPARENT, FONT_H2);
    gfx_str_centered(l.panel.x, l.panel.y + 145, l.panel.w, subtitle, COL_DIM, COL_TRANSPARENT);

    /* Inputs */
    gfx_str(l.user_field.x, l.user_field.y - 20, "Username", COL_MUTED, COL_TRANSPARENT);
    gfx_str(l.pass_field.x, l.pass_field.y - 20, "Password", COL_MUTED, COL_TRANSPARENT);

    {
        textinput_t u_box, p_box;
        kmemset(&u_box, 0, sizeof(u_box)); kmemset(&p_box, 0, sizeof(p_box));
        
        u_box.rect = l.user_field;
        u_box.focused = (s->field == 0);
        u_box.hover = rect_contains(l.user_field, mouse->x, mouse->y);
        kstrcpy(u_box.buf, s->username);
        u_box.len = s->user_len; u_box.cursor = s->user_len;
        kstrcpy(u_box.placeholder, "User ID");

        login_mask_password(s, pass_mask, sizeof(pass_mask));
        p_box.rect = l.pass_field;
        p_box.focused = (s->field == 1);
        p_box.hover = rect_contains(l.pass_field, mouse->x, mouse->y);
        kstrcpy(p_box.buf, pass_mask);
        p_box.len = (u32)kstrlen(pass_mask); p_box.cursor = p_box.len;
        kstrcpy(p_box.placeholder, "••••••••");

        textinput_draw(&u_box);
        textinput_draw(&p_box);
    }

    button_update(&l.primary_btn, mouse);
    button_update(&l.secondary_btn, mouse);
    button_draw(&l.primary_btn);
    button_draw(&l.secondary_btn);

    /* Footer / Status */
    gfx_rect_rounded(l.status_bar.x, l.status_bar.y, l.status_bar.w, l.status_bar.h, 12, COL_SURFACE2);
    gfx_str_centered(l.status_bar.x, l.status_bar.y + 7, l.status_bar.w, s->status, s->status_color, COL_TRANSPARENT);

    gfx_str_centered(l.panel.x, l.panel.y + l.panel.h - 30, l.panel.w, "© 2026 CareOS Core Team", rgb(0x40,0x40,0x40), COL_TRANSPARENT);
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
        login_set_status(s, "Login successful. Launching desktop...", COL_GREEN);
        s->failed_attempts = 0;
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

static bool run_login_flow(mouse_t *mouse) {
    login_state_t login;
    kmemset(&login, 0, sizeof(login));
    login.field = 0;
    login.mode = LOGIN_MODE_SIGNIN;
    login_set_status(&login, "Sign in to continue", COL_DIM);

    keyboard_flush();
    mouse->x = (i32)SCREEN_W / 2;
    mouse->y = (i32)SCREEN_H / 2;

    while (1) {
        login_layout_t layout = login_make_layout(&login);

        while (keyboard_haschar()) {
            char c = keyboard_getchar();

            if (c == '\t') {
                login.field = 1 - login.field;
                continue;
            }
            if (c == '\n') {
                if (login.mode == LOGIN_MODE_SIGNIN) {
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
                if (login.field == 0 && login.user_len > 0) {
                    login.user_len--;
                    login.username[login.user_len] = '\0';
                } else if (login.field == 1 && login.pass_len > 0) {
                    login.pass_len--;
                    login.password[login.pass_len] = '\0';
                }
                continue;
            }
            if (c < 32 || c > 126) continue;

            if (login.field == 0) {
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
                if (login.mode == LOGIN_MODE_SIGNIN) {
                    if (login_try(&login)) {
                        login_fade_out(&login, mouse);
                        return true;
                    }
                } else {
                    login_create_account(&login);
                }
            } else if (button_take_click(&layout.secondary_btn, mouse)) {
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

    sw = (i32)SCREEN_W;
    sh = (i32)SCREEN_H;
    tw = sw * 62 / 100;
    th = sh * 66 / 100;

    wm_open(APP_TERMINAL, "Terminal", (sw - tw) / 2, (sh - th) / 2 - 24, tw, th);
    notify_push("CareOS", "Desktop ready.", COL_PRIMARY);
    desktop_fade_in(&mouse);

    serial_write("  [gui_run] entering main loop\n");

    mouse.x = sw / 2;
    mouse.y = sh / 2;

    u32 last_sysmon = 0, last_netmon = 0, last_notify = 0;
    bool needs_redraw = true;
    u32 lx = 0, ly = 0;
    bool lb = false;

    while (1) {
            bool activity = false;

            while (keyboard_haschar()) {
                activity = true;
                char c = keyboard_getchar();
                char kl = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
                if (keyboard_alt_held() && c == '\t') { wm_cycle_focus(1); continue; }
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

            mouse_update(&mouse);
            if (mouse.x != lx || mouse.y != ly || mouse.left != lb || mouse.left_clicked) {
                activity = true; lx = mouse.x; ly = mouse.y; lb = mouse.left;
            }

            u32 now = timer_get_ticks();
            if (now - last_sysmon >= 20) {
                window_t *sm = wm_find_app(APP_SYSMON);
                window_t *nm = wm_find_app(APP_NETMON);
                if (sm) app_sysmon_tick(sm);
                if (nm) app_netmon_tick(nm);
                if (wm_animate_all()) activity = true;
                last_sysmon = now;
                activity = true; /* For clock update */
            }
            if (now - last_netmon >= 100) { net_poll(); last_netmon = now; activity = true; }
            if (now - last_notify >= 10) { notify_tick(); last_notify = now; activity = true; }

            if (activity) needs_redraw = true;

            if (needs_redraw) {
                draw_elite_wallpaper();
                desktop_draw();
                wm_draw_all();
                taskbar_draw();
                draw_top_bar();
                if (launcher_open) launcher_draw(&mouse);
                mouse_draw_cursor(mouse.x, mouse.y);
                gfx_flip();
                needs_redraw = false;
            }

            /* Handle input every frame to ensure responsiveness */
            if (!notify_handle_mouse(&mouse)) {
                if (launcher_open) launcher_handle_mouse(&mouse);
                else {
                    taskbar_handle_mouse(&mouse);
                    desktop_handle_mouse(&mouse);
                    wm_handle_mouse(&mouse);
                }
            }
            __asm__ volatile("sti; hlt");
    }
}
