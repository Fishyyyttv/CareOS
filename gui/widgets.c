/* CareOS gui/widgets.c -- movable desktop widgets.
 *
 * Three frosted-glass cards live on the desktop, behind every window: a live
 * Clock, a Calendar for the current month, and a System meter. They are drawn
 * each frame right after the wallpaper and before the windows, and they can be
 * dragged anywhere between the top bar and the dock.
 *
 * Everything here is integer-only (freestanding kernel: no libc, no FPU). Time
 * and date come from the RTC; the system meter reads the same real kernel stats
 * the System Monitor app uses (kmem_used / kheap_size, timer_ms,
 * task_count_active) -- no fabricated "live" numbers.
 */
#include "kernel.h"
#include "gui.h"
#include "widgets.h"
#include "ui.h"
#include "perf.h"
#include "../kernel/ext2.h"

/* -- Layout constants (all padding is a multiple of CDL_SP = 8) ------------ */
#define WGT_W        300      /* shared card width (Desktop artboard)        */
#define WGT_GAP      14       /* gap between cards                           */
#define WGT_MARGIN   24       /* clearance from the screen edge / menu bar   */

/* -- State ----------------------------------------------------------------- */
static dwidget_t s_widget[WIDGET_KIND_COUNT];
static i32       s_dragging = -1;   /* index of card being dragged, or -1    */
static bool      s_ready    = false;

/* ============================================================================
 * Date / time helpers (integer only)
 * ==========================================================================*/

static const char *k_month_full[] = {
    "January","February","March","April","May","June",
    "July","August","September","October","November","December"
};
static const char *k_weekday_full[] = {
    "Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"
};

static bool is_leap(i32 y) {
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

static i32 days_in_month(i32 m, i32 y) {
    static const i32 d[] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
    if (m == 2 && is_leap(y)) return 29;
    if (m < 1 || m > 12) return 30;
    return d[m - 1];
}

/* Sakamoto's algorithm: 0 = Sunday .. 6 = Saturday, for a Gregorian date. */
static i32 day_of_week(i32 y, i32 m, i32 d) {
    static const i32 t[] = { 0,3,2,5,0,3,5,1,4,6,2,4 };
    if (m < 3) y -= 1;
    return (y + y/4 - y/100 + y/400 + t[m - 1] + d) % 7;
}

/* Read the RTC and apply the same -7h "timezone" offset the top bar uses, so
 * the widgets always agree with the clock in the menu bar. Unlike the top bar's
 * naive day-- , we fully normalise the borrow across month and year boundaries
 * so the Calendar's "today" and month grid stay correct near midnight. */
static void read_local_time(rtc_time_t *out) {
    rtc_read(out);
    i32 h = (i32)out->hour - 7;
    if (h < 0) {
        h += 24;
        if (out->day > 1) {
            out->day--;
        } else {
            i32 m = (i32)out->month - 1;
            i32 y = (i32)out->year;
            if (m < 1) { m = 12; y--; }
            out->month = (u8)m;
            out->year  = (u16)y;
            out->day   = (u8)days_in_month(m, y);
        }
    }
    out->hour = (u8)h;
}

/* Public wrappers so apps (e.g. Clock) show exactly the widgets' local time. */
void clock_read_local(rtc_time_t *out) { read_local_time(out); }
i32  clock_day_of_week(i32 y, i32 m, i32 d) { return day_of_week(y, m, d); }

/* ============================================================================
 * Init -- default positions: the artboard's right column (right 24, top 64,
 * width 300, gap 14)
 * ==========================================================================*/

#define CARD_R     24
#define CAL_CELL   30
#define CAL_ROW    34            /* 30 px day + 4 px row gap */

static i32 clock_card_h(void) {
    return 20 + gfx_role_line_h(FONT_ROLE_UI, 0) + 2 + gfx_role_line_h(FONT_ROLE_DISPLAY, 3)
         + gfx_role_line_h(FONT_ROLE_UI, 1) + 20;
}
/* Rows the current month needs (4..6), so the card has no empty band. */
static i32 calendar_card_h(void) {
    rtc_time_t t;
    read_local_time(&t);
    i32 m = (t.month >= 1 && t.month <= 12) ? (i32)t.month : 1;
    i32 rows = (day_of_week((i32)t.year, m, 1) + days_in_month(m, (i32)t.year) + 6) / 7;
    return 18 + gfx_role_line_h(FONT_ROLE_DISPLAY, 0) + 10 + 20 + rows * CAL_ROW - 4 + 14;
}
static i32 system_card_h(u32 rows) {
    i32 row = gfx_role_line_h(FONT_ROLE_UI, 1) + 5 + 6;
    return 18 + gfx_role_line_h(FONT_ROLE_DISPLAY, 0) + (i32)rows * (12 + row) + 18;
}

static bool disk_pct(u32 *pct) { return ext2_usage(pct); }

void widgets_init(void) {
    u32 dp;
    i32 x = (i32)SCREEN_W - WGT_W - WGT_MARGIN;
    if (x < WGT_MARGIN) x = WGT_MARGIN;
    i32 y = (i32)TOPBAR_H + WGT_MARGIN;

    s_widget[WIDGET_CLOCK].rect = rect_make(x, y, WGT_W, clock_card_h());
    y += s_widget[WIDGET_CLOCK].rect.h + WGT_GAP;
    s_widget[WIDGET_CALENDAR].rect = rect_make(x, y, WGT_W, calendar_card_h());
    y += s_widget[WIDGET_CALENDAR].rect.h + WGT_GAP;
    s_widget[WIDGET_SYSTEM].rect = rect_make(x, y, WGT_W, system_card_h(disk_pct(&dp) ? 3 : 2));

    for (i32 i = 0; i < WIDGET_KIND_COUNT; i++) {
        s_widget[i].grab_dx = 0;
        s_widget[i].grab_dy = 0;
    }
    s_dragging = -1;
    s_ready    = true;
}

/* ============================================================================
 * Individual widget drawing
 * ==========================================================================*/

static void draw_card_base(rect_t r) {
    ui_glass(16, r, CARD_R, g_theme->panel_tint, g_theme->is_dark ? 168 : 199, 24, UI_GLASS_HAIRLINE);
}

static void upper(char *dst, const char *src, u32 max) {
    u32 i = 0;
    for (; src[i] && i + 1 < max; i++) dst[i] = (src[i] >= 'a' && src[i] <= 'z') ? (char)(src[i] - 32) : src[i];
    dst[i] = '\0';
}

static void draw_clock(rect_t r, const rtc_time_t *t) {
    i32 wd = day_of_week((i32)t->year, (i32)t->month, (i32)t->day);
    i32 x = r.x + 20, y = r.y + 20;

    /* "SATURDAY" in care-soft caps */
    char caps[16];
    upper(caps, k_weekday_full[wd], sizeof(caps));
    gfx_str_role(x, y, caps, g_theme->care_soft, FONT_ROLE_UI, 0, FONT_W_BOLD);
    y += gfx_role_line_h(FONT_ROLE_UI, 0) + 2;

    /* Time, Display 52, 12/24 h from settings. */
    char time_s[16];
    const careos_settings_t *cfg = settings_get();
    bool h24 = cfg && cfg->clock_24h;
    if (h24) ksprintf(time_s, "%02d:%02d", (int)t->hour, (int)t->minute);
    else {
        int h = (int)t->hour % 12;
        if (h == 0) h = 12;
        ksprintf(time_s, "%d:%02d", h, (int)t->minute);
    }
    gfx_str_role(x, y, time_s, g_theme->ink, FONT_ROLE_DISPLAY, 3, FONT_W_REGULAR);
    if (!h24) {
        i32 tw = gfx_str_role_width(time_s, FONT_ROLE_DISPLAY, 3, FONT_W_REGULAR);
        gfx_str_role(x + tw + 6, y + gfx_role_line_h(FONT_ROLE_DISPLAY, 3) - gfx_role_line_h(FONT_ROLE_UI, 2) - 8,
                     t->hour >= 12 ? "PM" : "AM", g_theme->ink2, FONT_ROLE_UI, 2, FONT_W_BOLD);
    }
    y += gfx_role_line_h(FONT_ROLE_DISPLAY, 3);

    /* "October 3, 2026" */
    const char *mon = (t->month >= 1 && t->month <= 12) ? k_month_full[t->month - 1] : "---";
    char date_s[32];
    ksprintf(date_s, "%s %d, %d", mon, (int)t->day, (int)t->year);
    gfx_str_role(x, y, date_s, g_theme->ink2, FONT_ROLE_UI, 1, FONT_W_REGULAR);
}

static void draw_calendar(rect_t r, const rtc_time_t *t) {
    i32 x = r.x + 18, y = r.y + 18, w = r.w - 36;
    i32 month = (t->month >= 1 && t->month <= 12) ? (i32)t->month : 1;
    i32 year  = (i32)t->year, today = (i32)t->day;

    /* "October" (Display 17) and the year, muted, on the right */
    gfx_str_role(x, y, k_month_full[month - 1], g_theme->ink, FONT_ROLE_DISPLAY, 0, FONT_W_REGULAR);
    char ys[8];
    ksprintf(ys, "%d", year);
    i32 yw = gfx_str_role_width(ys, FONT_ROLE_UI, 0, FONT_W_REGULAR);
    i32 dh = gfx_role_line_h(FONT_ROLE_DISPLAY, 0), ch = gfx_role_line_h(FONT_ROLE_UI, 0);
    gfx_str_role(x + w - yw, y + (dh - ch) / 2, ys, g_theme->muted, FONT_ROLE_UI, 0, FONT_W_REGULAR);
    y += dh + 10;

    static const char *const DOW[7] = { "S", "M", "T", "W", "T", "F", "S" };
    for (i32 c = 0; c < 7; c++) {
        i32 cx = x + c * w / 7 + w / 14;
        i32 tw = gfx_str_role_width(DOW[c], FONT_ROLE_UI, 0, FONT_W_BOLD);
        gfx_str_role(cx - tw / 2, y, DOW[c], g_theme->muted, FONT_ROLE_UI, 0, FONT_W_BOLD);
    }
    y += 20;

    i32 first = day_of_week(year, month, 1), dim = days_in_month(month, year);
    for (i32 d = 1; d <= dim; d++) {
        i32 slot = first + d - 1, col = slot % 7, row = slot / 7;
        i32 cx = x + col * w / 7 + w / 14, cy = y + row * CAL_ROW + CAL_CELL / 2;
        char num[4];
        ksprintf(num, "%d", (int)d);
        bool is_today = d == today;
        if (is_today) gfx_circle_fill(cx, cy, CAL_CELL / 2, g_theme->care);
        font_weight_t fw = is_today ? FONT_W_BOLD : FONT_W_REGULAR;
        i32 tw = gfx_str_role_width(num, FONT_ROLE_UI, 0, fw);
        gfx_str_role(cx - tw / 2, cy - ch / 2, num, is_today ? g_theme->on_care : g_theme->ink,
                     FONT_ROLE_UI, 0, fw);
    }
}

static void meter_row(i32 x, i32 *y, i32 w, const char *label, u32 pct, u32 color) {
    char v[8];
    ksprintf(v, "%d%%", (int)pct);
    gfx_str_role(x, *y, label, g_theme->ink2, FONT_ROLE_UI, 1, FONT_W_REGULAR);
    i32 vw = gfx_str_role_width(v, FONT_ROLE_UI, 1, FONT_W_BOLD);
    gfx_str_role(x + w - vw, *y, v, g_theme->ink, FONT_ROLE_UI, 1, FONT_W_BOLD);
    *y += gfx_role_line_h(FONT_ROLE_UI, 1) + 5;
    ui_progress(rect_make(x, *y, w, 6), pct, color);
    *y += 6 + 12;
}

static void draw_system(rect_t r) {
    i32 x = r.x + 18, y = r.y + 18, w = r.w - 36;

    /* Real numbers only: the GUI loop's busy share, kernel heap, ext2 usage. */
    u32 cpu = perf_cpu_busy_pct();
    u32 mem = (u32)((u64)kmem_used() * 100u / (u64)kheap_size());
    u32 disk = 0;
    bool has_disk = disk_pct(&disk);
    if (cpu > 100) cpu = 100;

    bool busy = cpu > 85 || mem > 85 || (has_disk && disk > 85);
    gfx_str_role(x, y, "System", g_theme->ink, FONT_ROLE_DISPLAY, 0, FONT_W_REGULAR);
    const char *st = busy ? "Busy" : "All good";
    i32 sw = gfx_str_role_width(st, FONT_ROLE_UI, 0, FONT_W_BOLD);
    i32 dh = gfx_role_line_h(FONT_ROLE_DISPLAY, 0);
    gfx_str_role(x + w - sw, y + (dh - gfx_role_line_h(FONT_ROLE_UI, 0)) / 2, st,
                 busy ? g_theme->warning : g_theme->success, FONT_ROLE_UI, 0, FONT_W_BOLD);
    y += dh + 12;

    meter_row(x, &y, w, "CPU", cpu, g_theme->calm);
    meter_row(x, &y, w, "Memory", mem, g_theme->care);
    if (has_disk) meter_row(x, &y, w, "Disk", disk, g_theme->success);
}

/* ============================================================================
 * Frame draw -- desktop layer, behind windows
 * ==========================================================================*/

static void draw_one(dwidget_kind_t k, const rtc_time_t *t) {
    rect_t r = s_widget[k].rect;
    draw_card_base(r);
    switch (k) {
        case WIDGET_CLOCK:    draw_clock(r, t);    break;
        case WIDGET_CALENDAR: draw_calendar(r, t); break;
        case WIDGET_SYSTEM:   draw_system(r);      break;
        default: break;
    }
}

void widgets_draw(mouse_t *m) {
    (void)m;
    if (!s_ready) widgets_init();

    rtc_time_t t;
    read_local_time(&t);

    /* Draw non-dragged cards first, then the dragged one on top so it wins any
     * overlap while being moved across a neighbour. */
    for (i32 i = 0; i < WIDGET_KIND_COUNT; i++)
        if (i != s_dragging) draw_one((dwidget_kind_t)i, &t);
    if (s_dragging >= 0)
        draw_one((dwidget_kind_t)s_dragging, &t);
}

/* ============================================================================
 * Mouse -- hit-testing and dragging
 * ==========================================================================*/

/* Clamp a card so it stays fully on-screen, below the top bar and above the
 * dock. Mutates the card's rect in place. */
static void clamp_widget(rect_t *r) {
    i32 min_x = 0;
    i32 max_x = (i32)SCREEN_W - r->w;
    i32 min_y = (i32)TOPBAR_H;
    i32 max_y = (i32)SCREEN_H - (i32)TASKBAR_H - r->h;

    if (max_x < min_x) max_x = min_x;
    if (max_y < min_y) max_y = min_y;
    if (r->x < min_x) r->x = min_x;
    if (r->x > max_x) r->x = max_x;
    if (r->y < min_y) r->y = min_y;
    if (r->y > max_y) r->y = max_y;
}

bool widgets_handle_mouse(mouse_t *m) {
    if (!s_ready) widgets_init();

    /* Already dragging: follow the cursor until the button is released. */
    if (s_dragging >= 0) {
        if (m->left) {
            rect_t *r = &s_widget[s_dragging].rect;
            r->x = m->x - s_widget[s_dragging].grab_dx;
            r->y = m->y - s_widget[s_dragging].grab_dy;
            clamp_widget(r);
        } else {
            s_dragging = -1;   /* released: end the drag */
        }
        return true;           /* consumed: keep the click away from windows */
    }

    /* Not dragging: begin a drag on a fresh left-press inside a card. Iterate
     * topmost-first (reverse of draw order) so the visually-front card wins. */
    if (m->left_clicked) {
        for (i32 i = WIDGET_KIND_COUNT - 1; i >= 0; i--) {
            if (rect_contains(s_widget[i].rect, m->x, m->y)) {
                s_dragging = i;
                s_widget[i].grab_dx = m->x - s_widget[i].rect.x;
                s_widget[i].grab_dy = m->y - s_widget[i].rect.y;
                return true;
            }
        }
        return false;   /* press missed every card */
    }

    /* Passive frame: still swallow the event if the cursor is hovering a card
     * with the button held, so a stray drag never leaks to the desktop. */
    if (m->left) {
        for (i32 i = 0; i < WIDGET_KIND_COUNT; i++)
            if (rect_contains(s_widget[i].rect, m->x, m->y)) return true;
    }
    return false;
}

/* What the cached desktop layer shows from the widgets, so gui.c rebuilds that
 * layer only when it would look different: the minute, the day, and the meters
 * (CPU in 5 % steps). */
u32 widgets_layer_key(void) {
    rtc_time_t t;
    read_local_time(&t);
    u32 cpu = perf_cpu_busy_pct() / 5u, mem = (u32)((u64)kmem_used() * 100u / (u64)kheap_size()), disk = 0;
    disk_pct(&disk);
    u32 k = 2166136261u;
    u32 parts[6] = { t.minute, t.hour, t.day, cpu, mem, disk };
    for (u32 i = 0; i < 6; i++) k = (k ^ parts[i]) * 16777619u;
    return k;
}

bool widgets_dragging(void) { return s_dragging >= 0; }
