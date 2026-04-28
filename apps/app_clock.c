/* CareOS v9 -- apps/app_clock.c -- Clock and calendar */
#include "apps_common.h"

/* Integer sine/cosine table: sin_table[i] = sin(i * 30 deg) * 1024 (12 steps for clock) */
static const i32 sin12[12] = { 0, 512, 886, 1024, 886, 512, 0, -512, -886, -1024, -886, -512 };
static const i32 cos12[12] = { 1024, 886, 512, 0, -512, -886, -1024, -886, -512, 0, 512, 886 };

void app_clock_draw(window_t *w){
    rect_t cr = wm_client_rect(w);
    (void)w;
    gfx_gradient_rect(cr.x, cr.y, cr.w, cr.h, COL_SURFACE, COL_BG);

    rtc_time_t t; rtc_read(&t);

    i32 r  = (cr.h < cr.w ? cr.h : cr.w) * 38 / 100;
    if (r > 90) r = 90;
    i32 cx = cr.x + cr.w / 2;
    i32 cy = cr.y + cr.h / 2 - 20;

    /* Clock face */
    gfx_circle_fill(cx, cy, r, COL_SURFACE2);
    gfx_circle(cx, cy, r, COL_BORDER);
    gfx_circle(cx, cy, r - 1, COL_SURFACE3);

    /* Hour tick marks */
    for (int i = 0; i < 12; i++) {
        i32 ox = sin12[i] * r / 1024;
        i32 oy = -cos12[i] * r / 1024;
        /* Outer point */
        i32 ox2 = sin12[i] * (r - 6) / 1024;
        i32 oy2 = -cos12[i] * (r - 6) / 1024;
        gfx_line(cx + ox2, cy + oy2, cx + ox, cy + oy,
                 (i % 3 == 0) ? COL_TEXT : COL_MUTED);
    }

    /* Hour hand */
    {
        int hi = (t.hour % 12) * 1 + t.minute / 10;
        if (hi > 11) hi = 11;
        i32 hx = sin12[hi] * (r * 55 / 100) / 1024;
        i32 hy = -cos12[hi] * (r * 55 / 100) / 1024;
        gfx_line(cx, cy, cx + hx, cy + hy, COL_TEXT);
        gfx_line(cx + 1, cy, cx + hx + 1, cy + hy, COL_TEXT);
    }

    /* Minute hand */
    {
        int mi = t.minute / 5;
        i32 mx = sin12[mi] * (r * 78 / 100) / 1024;
        i32 my = -cos12[mi] * (r * 78 / 100) / 1024;
        gfx_line(cx, cy, cx + mx, cy + my, COL_DIM);
    }

    /* Center dot */
    gfx_circle_fill(cx, cy, 5, COL_PRIMARY);
    gfx_circle_fill(cx, cy, 2, COL_SURFACE);

    /* Digital time below clock */
    char h_s[3], m_s[3], s_s[3];
    kutoa(t.hour,   h_s, 10); if(t.hour   < 10){h_s[1]=h_s[0];h_s[0]='0';h_s[2]='\0';}
    kutoa(t.minute, m_s, 10); if(t.minute < 10){m_s[1]=m_s[0];m_s[0]='0';m_s[2]='\0';}
    kutoa(t.second, s_s, 10); if(t.second < 10){s_s[1]=s_s[0];s_s[0]='0';s_s[2]='\0';}
    char time_str[12];
    kstrcpy(time_str, h_s); kstrcat(time_str, ":"); kstrcat(time_str, m_s);
    kstrcat(time_str, ":"); kstrcat(time_str, s_s);
    gfx_str_centered(cr.x, cy + r + 14, cr.w, time_str, COL_TEXT, COL_TRANSPARENT);

    char dy[3], mo[3], yr[5];
    kutoa(t.day,   dy, 10); if(t.day   < 10){dy[1]=dy[0];dy[0]='0';dy[2]='\0';}
    kutoa(t.month, mo, 10); if(t.month < 10){mo[1]=mo[0];mo[0]='0';mo[2]='\0';}
    kutoa(t.year,  yr, 10);
    char date_str[12];
    kstrcpy(date_str, yr); kstrcat(date_str, "-"); kstrcat(date_str, mo);
    kstrcat(date_str, "-"); kstrcat(date_str, dy);
    gfx_str_centered(cr.x, cy + r + 28, cr.w, date_str, COL_DIM, COL_TRANSPARENT);
}
