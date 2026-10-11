/* CareOS v9 -- apps/app_clock.c -- Clock and calendar */
#include "apps_common.h"

/* sin(i * 6 deg) * 1024 for i = 0..15 (one quadrant of a 60-step dial). */
static const i32 sin_q[16] = {
    0, 107, 213, 316, 416, 512, 602, 685, 761, 828, 887, 935, 974, 1002, 1018, 1024
};

/* sin/cos of dial position i (0..59, 6 deg per step), scaled by 1024. */
static i32 dial_sin(i32 i) {
    i %= 60; if (i < 0) i += 60;
    if (i <= 15) return  sin_q[i];
    if (i <= 30) return  sin_q[30 - i];
    if (i <= 45) return -sin_q[i - 30];
    return -sin_q[60 - i];
}
static i32 dial_cos(i32 i) { return dial_sin(i + 15); }

/* Endpoint of a hand at dial position pos + frac/1000 (linear between the two
 * neighbouring steps -- visually indistinguishable at 6 deg), length len. */
static void dial_point(i32 pos, i32 frac, i32 len, i32 *dx, i32 *dy) {
    i32 s = dial_sin(pos) + (dial_sin(pos + 1) - dial_sin(pos)) * frac / 1000;
    i32 c = dial_cos(pos) + (dial_cos(pos + 1) - dial_cos(pos)) * frac / 1000;
    *dx =  s * len / 1024;
    *dy = -c * len / 1024;
}

/* Milliseconds into the current RTC second. The RTC only has 1 s resolution,
 * so latch the PIT tick when the second changes and measure from there. */
static u32 sub_second_ms(u8 sec) {
    static u8  last_sec = 0xFF;
    static u32 sec_tick = 0;
    u32 now = timer_ms();
    if (sec != last_sec) { last_sec = sec; sec_tick = now; }
    u32 ms = now - sec_tick;
    return ms > 999 ? 999 : ms;
}

void app_clock_draw(window_t *w){
    rect_t cr = wm_client_rect(w);

    /* Gradient bg */
    gfx_gradient_rect(cr.x, cr.y, cr.w, cr.h, COL_SURFACE, COL_BG);

    /* Same local time as the top bar and desktop widgets. */
    rtc_time_t t; clock_read_local(&t);
    i32 ms = (i32)sub_second_ms(t.second);

    /* Clock radius — use available space sensibly */
    i32 avail  = (cr.h < cr.w ? cr.h : cr.w);
    i32 r      = avail * 40 / 100;
    if (r > 100) r = 100;
    if (r < 30)  r = 30;
    i32 cx = cr.x + cr.w / 2;
    i32 cy = cr.y + cr.h / 2 - 16;

    /* Outer glow ring */
    gfx_circle_fill(cx, cy, r + 8, COL_SURFACE2);
    gfx_circle(cx, cy, r + 8, COL_BORDER);

    /* Clock face */
    gfx_circle_fill(cx, cy, r, COL_SURFACE3);
    gfx_circle(cx, cy, r, COL_BORDER);
    gfx_circle(cx, cy, r - 1, COL_SURFACE2);

    /* Tick marks: long at the quarters, medium at the hours, dots per minute */
    for (i32 i = 0; i < 60; i++) {
        i32 ox, oy, ix, iy;
        dial_point(i, 0, r - 3, &ox, &oy);
        if (i % 5) {
            if (r >= 60) gfx_rect(cx + ox, cy + oy, 1, 1, COL_MUTED);
            continue;
        }
        bool major = (i % 15 == 0);
        dial_point(i, 0, r - 3 - (major ? 9 : 5), &ix, &iy);
        gfx_line(cx + ix, cy + iy, cx + ox, cy + oy, major ? COL_TEXT : COL_MUTED);
    }

    /* Hour hand: 5 dial steps per hour, creeping with the minutes. */
    {
        i32 pos  = (i32)(t.hour % 12) * 5 + (i32)t.minute / 12;
        i32 frac = ((i32)t.minute % 12) * 1000 / 12;
        i32 hx, hy; dial_point(pos, frac, r * 52 / 100, &hx, &hy);
        gfx_line(cx, cy, cx + hx, cy + hy, COL_TEXT);
        gfx_line(cx + 1, cy, cx + hx + 1, cy + hy, COL_TEXT);
        gfx_line(cx, cy + 1, cx + hx, cy + hy + 1, COL_TEXT);
    }

    /* Minute hand, creeping with the seconds. */
    {
        i32 mx, my; dial_point((i32)t.minute, (i32)t.second * 1000 / 60, r * 78 / 100, &mx, &my);
        gfx_line(cx, cy, cx + mx, cy + my, COL_DIM);
        gfx_line(cx + 1, cy, cx + mx + 1, cy + my, COL_DIM);
    }

    /* Second hand: sweeps smoothly between RTC ticks, with a short tail and a
     * counterweight dot, in a warm accent that stands out from the other hands. */
    {
        const u32 sec_col = rgb(0xff, 0x5a, 0x4f);
        i32 sx, sy, tx, ty;
        dial_point((i32)t.second, ms, r * 88 / 100, &sx, &sy);
        dial_point((i32)t.second + 30, ms, r * 18 / 100, &tx, &ty);
        gfx_line(cx + tx, cy + ty, cx + sx, cy + sy, sec_col);
        gfx_circle_fill(cx + tx, cy + ty, 3, sec_col);
    }

    /* Center cap */
    gfx_circle_fill(cx, cy, 6, COL_PRIMARY);
    gfx_circle_fill(cx, cy, 3, COL_SURFACE);

    /* Digital time, honouring the same 12/24h setting as the top bar. */
    const careos_settings_t *cfg = settings_get();
    bool h24 = cfg && cfg->clock_24h;
    int hd = t.hour;
    if (!h24) { hd = t.hour % 12; if (hd == 0) hd = 12; }
    char time_str[16];
    ksprintf(time_str, "%d:%s%d:%s%d%s", hd,
             t.minute < 10 ? "0" : "", (int)t.minute,
             t.second < 10 ? "0" : "", (int)t.second,
             h24 ? "" : (t.hour >= 12 ? " PM" : " AM"));
    if (h24 && t.hour < 10) {   /* 24h clocks read best zero-padded: 07:05:09 */
        char tmp[16]; kstrcpy(tmp, "0"); kstrcat(tmp, time_str); kstrcpy(time_str, tmp);
    }
    gfx_str_centered_ex(cr.x, cy + r + 18, cr.w, time_str, COL_TEXT, COL_TRANSPARENT, FONT_H3);

    /* Date below, with the day of the week */
    static const char *day_names[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    static const char *mon_names[] = {"Jan","Feb","Mar","Apr","May","Jun",
                                      "Jul","Aug","Sep","Oct","Nov","Dec"};
    char date_str[32];
    bool valid = t.month >= 1 && t.month <= 12;
    const char *mn = valid ? mon_names[t.month - 1] : "---";
    const char *dn = valid ? day_names[clock_day_of_week((i32)t.year, (i32)t.month, (i32)t.day)] : "---";
    char yr[6]; kutoa(t.year, yr, 10);
    ksprintf(date_str, "%s, %s %d, %s", dn, mn, (int)t.day, yr);
    /* The digital time is FONT_H3; the date has to clear that line height, not
     * the 18px the old bitmap H3 happened to occupy. */
    gfx_str_centered(cr.x, cy + r + 18 + gfx_line_h_ex(FONT_H3) + 2, cr.w,
                     date_str, COL_DIM, COL_TRANSPARENT);
}
