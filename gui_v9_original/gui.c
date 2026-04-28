/* =============================================================================
 * CareOS gui/gui.c -- main GUI entry point with boot splash + login gate
 * ============================================================================= */
#include "kernel.h"
#include "gui.h"

void gui_init(u32 *fb, u32 w, u32 h, u32 pitch) {
    serial_write("  [gui_init] gfx_init\n");
    gfx_init(fb, w, h, pitch);
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

    gfx_gradient_rect(0, 0, sw, sh, rgb(0x05,0x08,0x14), rgb(0x12,0x18,0x30));

    for (i32 gy = 0; gy < sh; gy += 30)
        for (i32 gx = 0; gx < sw; gx += 30)
            if (((gx + gy) / 30) % 2 == 0)
                gfx_setpixel(gx, gy, rgb(0x1f,0x2a,0x4f));

    i32 cx = sw / 2;
    i32 cy = sh / 2 - 72;
    i32 pulse = 42 + (i32)(tick % 6);

    gfx_circle_fill(cx, cy, pulse, rgb(0x1f,0x2d,0x58));
    gfx_circle_fill(cx, cy, pulse - 6, rgb(0x0d,0x14,0x2c));
    gfx_circle(cx, cy, pulse + 2, COL_PRIMARY);
    gfx_circle(cx, cy, pulse + 8, COL_ACCENT);

    gfx_circle(cx, cy, 26, COL_PRIMARY);
    gfx_rect(cx, cy - 26, 22, 52, rgb(0x0d,0x14,0x2c));
    gfx_str(cx - 8, cy - 5, "OS", COL_ACCENT, COL_TRANSPARENT);

    gfx_str_centered(0, cy + 66, sw, "CareOS", COL_WHITE, COL_TRANSPARENT);
    gfx_str_centered(0, cy + 82, sw,
        "Performance focused desktop operating environment",
        COL_DIM, COL_TRANSPARENT);

    i32 bw = 380;
    i32 bh = 8;
    i32 bx = sw / 2 - bw / 2;
    i32 by = cy + 110;

    gfx_rect_rounded(bx, by, bw, bh, 4, rgb(0x0b,0x10,0x20));
    gfx_rect_rounded_outline(bx, by, bw, bh, 4, COL_BORDER);

    if (done > 0) {
        i32 fill = (bw - 2) * done / BOOT_STAGE_COUNT;
        if (fill < 0) fill = 0;
        gfx_rect_rounded(bx + 1, by + 1, fill, bh - 2, 3,
            done >= BOOT_STAGE_COUNT ? COL_GREEN : COL_PRIMARY);
    }

    for (int i = 0; i < BOOT_STAGE_COUNT; i++) {
        i32 dx = bx + (bw * i / (BOOT_STAGE_COUNT - 1));
        u32 dc = (i < done) ? COL_GREEN : (i == done ? COL_ACCENT : rgb(0x29,0x32,0x53));
        gfx_circle_fill(dx, by + bh / 2, 4, dc);
    }

    const char *label = (done >= BOOT_STAGE_COUNT)
        ? "Boot sequence complete"
        : BOOT_STAGES[done];
    gfx_str_centered(0, by + 16, sw, label, COL_TEXT, COL_TRANSPARENT);

    const char spin[] = "|/-\\";
    char spin_buf[24] = "Loader: [";
    u32 idx = tick % 4;
    spin_buf[9]  = spin[idx];
    spin_buf[10] = ']';
    spin_buf[11] = '\0';
    gfx_str_centered(0, by + 30, sw, spin_buf, COL_MUTED, COL_TRANSPARENT);

    gfx_str_centered(0, sh - 20, sw,
        "CareOS v9.0  |  April 2026  |  x86 32-bit",
        COL_MUTED, COL_TRANSPARENT);

    gfx_flip();
}

typedef struct {
    char username[32];
    char password[64];
    u32  user_len;
    u32  pass_len;
    u32  field;       /* 0=username, 1=password */
    char status[96];
    u32  status_color;
    u32  failed_attempts;
    u32  lock_until_tick;
} login_state_t;

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

static void draw_login_screen(const login_state_t *s) {
    i32 sw = (i32)SCREEN_W;
    i32 sh = (i32)SCREEN_H;

    gfx_gradient_rect(0, 0, sw, sh, rgb(0x0a,0x12,0x22), rgb(0x16,0x25,0x3f));

    for (i32 gy = 0; gy < sh; gy += 36)
        for (i32 gx = 0; gx < sw; gx += 36)
            if (((gx / 36) + (gy / 36)) % 2 == 0)
                gfx_setpixel(gx, gy, rgb(0x1c,0x2d,0x52));

    i32 pw = 440;
    i32 ph = 276;
    i32 px = sw / 2 - pw / 2;
    i32 py = sh / 2 - ph / 2;

    gfx_shadow(px, py, pw, ph);
    gfx_rect_rounded(px, py, pw, ph, 10, COL_SURFACE);
    gfx_rect_rounded_outline(px, py, pw, ph, 10, COL_BORDER);

    gfx_gradient_rect(px + 2, py + 2, pw - 4, 42, COL_PRIMARY, COL_ACCENT);
    gfx_str_centered(px, py + 14, pw, "CareOS Secure Login", COL_WHITE, COL_TRANSPARENT);

    gfx_str(px + 28, py + 68, "Username", COL_DIM, COL_SURFACE);
    gfx_str(px + 28, py + 126, "Password", COL_DIM, COL_SURFACE);

    i32 fx = px + 28;
    i32 fw = pw - 56;

    u32 u_bg = s->field == 0 ? COL_INPUT_BG : COL_SURFACE2;
    u32 p_bg = s->field == 1 ? COL_INPUT_BG : COL_SURFACE2;

    gfx_rect_rounded(fx, py + 84, fw, 30, 5, u_bg);
    gfx_rect_rounded_outline(fx, py + 84, fw, 30, 5,
        s->field == 0 ? COL_PRIMARY : COL_BORDER);
    gfx_str_clipped(fx + 8, py + 95, fw - 16, s->username, COL_TEXT, u_bg);

    gfx_rect_rounded(fx, py + 142, fw, 30, 5, p_bg);
    gfx_rect_rounded_outline(fx, py + 142, fw, 30, 5,
        s->field == 1 ? COL_PRIMARY : COL_BORDER);

    char pass_mask[64];
    login_mask_password(s, pass_mask, sizeof(pass_mask));
    gfx_str_clipped(fx + 8, py + 153, fw - 16, pass_mask, COL_TEXT, p_bg);

    gfx_str(px + 28, py + 188, "Tab: switch field", COL_MUTED, COL_SURFACE);
    gfx_str(px + 180, py + 188, "Enter: sign in", COL_MUTED, COL_SURFACE);

    gfx_rect_rounded(px + 28, py + 206, pw - 56, 28, 5, COL_SURFACE2);
    gfx_rect_rounded_outline(px + 28, py + 206, pw - 56, 28, 5, COL_BORDER);
    gfx_str_clipped(px + 36, py + 215, pw - 72, s->status, s->status_color, COL_SURFACE2);

    gfx_str_centered(px, py + ph - 16, pw,
        "Default users: user/CareOS123 and root/root",
        COL_MUTED, COL_TRANSPARENT);
}

static bool login_try(login_state_t *s) {
    u32 now = timer_get_ticks();
    if (s->lock_until_tick > now) {
        char wait_s[12];
        u32 left = (s->lock_until_tick - now + PIT_HZ - 1) / PIT_HZ;
        kutoa(left, wait_s, 10);
        char msg[96] = "Too many attempts. Wait ";
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

static bool run_login_flow(void) {
    login_state_t login;
    kmemset(&login, 0, sizeof(login));

    kstrcpy(login.username, "user");
    login.user_len = 4;
    login.field = 1;
    login_set_status(&login, "Sign in to continue", COL_DIM);

    keyboard_flush();

    mouse_t mouse = {0};
    mouse.x = (i32)SCREEN_W / 2;
    mouse.y = (i32)SCREEN_H / 2;

    while (1) {
        while (keyboard_haschar()) {
            char c = keyboard_getchar();

            if (c == '\t') {
                login.field = 1 - login.field;
                continue;
            }

            if (c == '\n') {
                if (login_try(&login)) {
                    draw_login_screen(&login);
                    mouse_draw_cursor(mouse.x, mouse.y);
                    gfx_flip();
                    return true;
                }
                continue;
            }

            if (c == '\b') {
                if (login.field == 0) {
                    if (login.user_len > 0) {
                        login.user_len--;
                        login.username[login.user_len] = '\0';
                    }
                } else {
                    if (login.pass_len > 0) {
                        login.pass_len--;
                        login.password[login.pass_len] = '\0';
                    }
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

        mouse_update(&mouse);

        draw_login_screen(&login);
        mouse_draw_cursor(mouse.x, mouse.y);
        gfx_flip();

        __asm__ volatile("sti; hlt");
    }
    return false;
}

void gui_run(void) {
    serial_write("  [gui_run] splash start\n");
    const careos_settings_t *cfg = settings_get();
    bool fast_boot = cfg && cfg->boot_fast;

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
    if (!run_login_flow()) {
        serial_write("  [gui_run] login flow returned failure\n");
    }

    i32 sw = (i32)SCREEN_W;
    i32 sh = (i32)SCREEN_H;
    i32 tw = sw * 62 / 100;
    i32 th = sh * 66 / 100;

    wm_open(APP_TERMINAL, "Terminal", (sw - tw) / 2, (sh - th) / 2 - 24, tw, th);
    notify_push("CareOS", "Desktop ready.", COL_PRIMARY);

    serial_write("  [gui_run] entering main loop\n");

    mouse_t mouse = {0};
    mouse.x = sw / 2;
    mouse.y = sh / 2;

    u32 last_sysmon = 0, last_netmon = 0, last_notify = 0;

    while (1) {
        while (keyboard_haschar()) {
            char c = keyboard_getchar();
            char kl = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;

            if (keyboard_alt_held() && c == '\t') {
                wm_cycle_focus(1);
                continue;
            }

            if (keyboard_alt_held() && keyboard_ctrl_held()) {
                if (kl == 'h') { wm_snap_focused(0); continue; }
                if (kl == 'l') { wm_snap_focused(1); continue; }
                if (kl == 'k') { wm_snap_focused(2); continue; }
                if (kl == 'j') { wm_snap_focused(3); continue; }
                if (kl == 'm') { wm_snap_focused(4); continue; }
            }

            window_t *fw = wm_focused();
            if (fw) wm_handle_key(c, fw);
        }
        mouse_update(&mouse);

        u32 now = timer_get_ticks();
        if (now - last_sysmon >= 20) {
            window_t *sm = wm_find_app(APP_SYSMON); if (sm) app_sysmon_tick(sm);
            window_t *nm = wm_find_app(APP_NETMON); if (nm) app_netmon_tick(nm);
            last_sysmon = now;
        }
        if (now - last_netmon >= 100) { net_poll(); last_netmon = now; }
        if (now - last_notify >= 10)  { notify_tick(); last_notify = now; }

        gfx_clear(COL_BG);
        desktop_draw();
        taskbar_draw();
        wm_draw_all();
        mouse_draw_cursor(mouse.x, mouse.y);
        gfx_flip();

        if (!notify_handle_mouse(&mouse)) {
            taskbar_handle_mouse(&mouse);
            desktop_handle_mouse(&mouse);
            wm_handle_mouse(&mouse);
        }

        __asm__ volatile("sti; hlt");
    }
}





