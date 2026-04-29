/* CareOS v9 -- apps/app_clock.c -- Clock and calendar */
#include "apps_common.h"

/* sin_table[i] = sin(i * 30 deg) * 1024 (12-step for clock hands) */
static const i32 sin12[12] = { 0, 512, 886, 1024, 886, 512, 0, -512, -886, -1024, -886, -512 };
static const i32 cos12[12] = { 1024, 886, 512, 0, -512, -886, -1024, -886, -512, 0, 512, 886 };

void app_clock_draw(window_t *w){
    rect_t cr = wm_client_rect(w);
    (void)w;

    /* Gradient bg */
    gfx_gradient_rect(cr.x, cr.y, cr.w, cr.h, COL_SURFACE, COL_BG);

    rtc_time_t t; rtc_read(&t);

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

    /* Hour tick marks */
    for (int i = 0; i < 12; i++) {
        bool major = (i % 3 == 0);
        i32 tick_in  = major ? 8 : 5;
        i32 ox_out  = sin12[i] * r / 1024;
        i32 oy_out  = -cos12[i] * r / 1024;
        i32 ox_in   = sin12[i] * (r - tick_in) / 1024;
        i32 oy_in   = -cos12[i] * (r - tick_in) / 1024;
        gfx_line(cx + ox_in, cy + oy_in, cx + ox_out, cy + oy_out,
                 major ? COL_TEXT : COL_MUTED);
    }

    /* Hour hand */
    {
        int hi = ((int)t.hour % 12) + (int)t.minute / 10;
        if (hi > 11) hi = 11;
        i32 hx = sin12[hi] * (r * 55 / 100) / 1024;
        i32 hy = -cos12[hi] * (r * 55 / 100) / 1024;
        gfx_line(cx, cy, cx + hx, cy + hy, COL_TEXT);
        gfx_line(cx + 1, cy, cx + hx + 1, cy + hy, COL_TEXT);
        gfx_line(cx - 1, cy, cx + hx - 1, cy + hy, COL_MUTED);
    }

    /* Minute hand */
    {
        int mi = (int)t.minute / 5;
        i32 mx = sin12[mi] * (r * 78 / 100) / 1024;
        i32 my = -cos12[mi] * (r * 78 / 100) / 1024;
        gfx_line(cx, cy, cx + mx, cy + my, COL_DIM);
        gfx_line(cx + 1, cy, cx + mx + 1, cy + my, COL_DIM);
    }

    /* Center cap */
    gfx_circle_fill(cx, cy, 6, COL_PRIMARY);
    gfx_circle_fill(cx, cy, 3, COL_SURFACE);

    /* Digital time */
    char h_s[3], m_s[3], s_s[3];
    kutoa(t.hour,   h_s, 10); if(t.hour   < 10){h_s[1]=h_s[0];h_s[0]='0';h_s[2]='\0';}
    kutoa(t.minute, m_s, 10); if(t.minute < 10){m_s[1]=m_s[0];m_s[0]='0';m_s[2]='\0';}
    kutoa(t.second, s_s, 10); if(t.second < 10){s_s[1]=s_s[0];s_s[0]='0';s_s[2]='\0';}
    char time_str[12];
    kstrcpy(time_str, h_s); kstrcat(time_str, ":"); kstrcat(time_str, m_s);
    kstrcat(time_str, ":"); kstrcat(time_str, s_s);
    gfx_str_centered_ex(cr.x, cy + r + 18, cr.w, time_str, COL_TEXT, COL_TRANSPARENT, FONT_H3);

    /* Date below */
    const char *day_names[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    const char *mon_names[] = {"Jan","Feb","Mar","Apr","May","Jun",
                               "Jul","Aug","Sep","Oct","Nov","Dec"};
    char date_str[32];
    const char *mn = (t.month >= 1 && t.month <= 12) ? mon_names[t.month - 1] : "---";
    char yr[6]; kutoa(t.year, yr, 10);
    ksprintf(date_str, "%s %d, %s", mn, (int)t.day, yr);
    gfx_str_centered(cr.x, cy + r + 36, cr.w, date_str, COL_DIM, COL_TRANSPARENT);
}
