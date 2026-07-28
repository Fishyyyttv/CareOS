/* CareOS gui/widgets.c -- movable desktop widgets.
 *
 * Three frosted-glass cards live on the desktop, behind every window: a live
 * Clock, a Calendar for the current month, and a System meter. They are drawn
 * each frame right after the wallpaper and before the windows, and they can be
 * dragged anywhere between the top bar and the dock.
 *
 * Everything here is integer-only (freestanding kernel: no libc, no FPU). Time
 * and date come from the RTC; the system meter reads the same real kernel stats
 * the System Monitor app uses (kmem_used / KERNEL_HEAP_SIZE, timer_get_ticks,
 * task_count_active) -- no fabricated "live" numbers.
 */
#include "kernel.h"
#include "gui.h"
#include "widgets.h"

/* -- Layout constants (all padding is a multiple of CDL_SP = 8) ------------ */
#define WGT_W        240      /* shared card width                          */
#define WGT_PAD      16       /* inner padding (2 * CDL_SP)                  */
#define WGT_GAP      8        /* gap between cards, and between content rows */
#define WGT_MARGIN   20       /* clearance from the screen edges            */

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
static const char *k_month_abbr[] = {
    "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"
};
static const char *k_weekday_full[] = {
    "Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"
};
static const char *k_weekday_abbr[] = { "Su","Mo","Tu","We","Th","Fr","Sa" };

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

/* ============================================================================
 * Init -- default positions, stacked down the right edge
 * ==========================================================================*/

void widgets_init(void) {
    i32 cap_lh  = gfx_line_h_ex(FONT_CAPTION);
    i32 body_lh = gfx_line_h_ex(FONT_BODY);
    i32 h1_lh   = gfx_line_h_ex(FONT_H1);
    i32 h3_lh   = gfx_line_h_ex(FONT_H3);

    /* Card heights are derived from the real line metrics so they fit whatever
     * font family/scale is active, not hard-coded pixel guesses. */
    i32 clock_h = WGT_PAD + cap_lh + h1_lh + body_lh + WGT_PAD;

    i32 grid_w  = WGT_W - 2 * WGT_PAD;
    i32 cell    = grid_w / 7;                 /* square-ish calendar cell     */
    i32 cal_h   = WGT_PAD + h3_lh + WGT_GAP   /* title                        */
                + cell                        /* weekday header row           */
                + 6 * cell                    /* up to 6 week rows            */
                + WGT_PAD;

    i32 bar_h   = 12;
    i32 sys_h   = WGT_PAD + h3_lh + WGT_GAP    /* title                       */
                + cap_lh + 4 + bar_h + WGT_GAP /* memory label + bar          */
                + body_lh                      /* uptime row                  */
                + body_lh                      /* tasks row                   */
                + WGT_PAD;

    i32 x = (i32)SCREEN_W - WGT_W - WGT_MARGIN;
    if (x < WGT_MARGIN) x = WGT_MARGIN;
    i32 y = (i32)TOPBAR_H + WGT_MARGIN;

    s_widget[WIDGET_CLOCK].rect    = rect_make(x, y, WGT_W, clock_h);
    y += clock_h + WGT_GAP + WGT_GAP;
    s_widget[WIDGET_CALENDAR].rect = rect_make(x, y, WGT_W, cal_h);
    y += cal_h + WGT_GAP + WGT_GAP;
    s_widget[WIDGET_SYSTEM].rect   = rect_make(x, y, WGT_W, sys_h);

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

/* Common card chrome: a soft shadow, then the frosted glass panel. The shadow
 * is tighter than the CDL default (smaller blur/offset) because the cards stack
 * close together -- a full-size halo would bleed into the neighbour below and
 * read as the cards overlapping. */
static void draw_card_base(rect_t r) {
    gfx_shadow_soft_ex(r.x, r.y, r.w, r.h, CDL_R_CARD, 15, 32, 5);
    gfx_glass_panel(r.x, r.y, r.w, r.h, CDL_R_CARD);
}

static void draw_clock(rect_t r, const rtc_time_t *t) {
    i32 cap_lh = gfx_line_h_ex(FONT_CAPTION);
    i32 h1_lh  = gfx_line_h_ex(FONT_H1);
    i32 wd     = day_of_week((i32)t->year, (i32)t->month, (i32)t->day);
    i32 inner  = r.w - 2 * WGT_PAD;
    i32 cx     = r.x + WGT_PAD;
    i32 cy     = r.y + WGT_PAD;

    /* Small accent title: the weekday, e.g. "SATURDAY". */
    gfx_str_ex(cx, cy, k_weekday_full[wd], COL_ACCENT, COL_TRANSPARENT, FONT_CAPTION);
    cy += cap_lh;

    /* Big live time HH:MM:SS, centred (24h to stay unambiguous & consistent). */
    char time_s[16];
    ksprintf(time_s, "%02d:%02d:%02d", (int)t->hour, (int)t->minute, (int)t->second);
    gfx_str_centered_ex(cx, cy, inner, time_s, COL_TEXT, COL_TRANSPARENT, FONT_H1);
    cy += h1_lh;

    /* Date line: "Jul 26, 2026". */
    const char *mon = (t->month >= 1 && t->month <= 12) ? k_month_abbr[t->month - 1] : "---";
    char date_s[24];
    ksprintf(date_s, "%s %d, %d", mon, (int)t->day, (int)t->year);
    gfx_str_centered_ex(cx, cy, inner, date_s, COL_DIM, COL_TRANSPARENT, FONT_BODY);
}

static void draw_calendar(rect_t r, const rtc_time_t *t) {
    i32 h3_lh   = gfx_line_h_ex(FONT_H3);
    i32 cap_lh  = gfx_line_h_ex(FONT_CAPTION);
    i32 grid_w  = r.w - 2 * WGT_PAD;
    i32 cell    = grid_w / 7;
    i32 gx      = r.x + WGT_PAD;
    i32 gy      = r.y + WGT_PAD;

    i32 month   = (t->month >= 1 && t->month <= 12) ? (i32)t->month : 1;
    i32 year    = (i32)t->year;
    i32 today   = (i32)t->day;

    /* Title: "July 2026". */
    char title[32];
    ksprintf(title, "%s %d", k_month_full[month - 1], year);
    gfx_str_ex(gx, gy, title, COL_TEXT, COL_TRANSPARENT, FONT_H3);
    gy += h3_lh + WGT_GAP;

    /* Weekday header row (Su Mo Tu We Th Fr Sa). */
    i32 hdr_ty = gy + (cell - cap_lh) / 2;
    for (i32 c = 0; c < 7; c++) {
        gfx_str_centered_ex(gx + c * cell, hdr_ty, cell, k_weekday_abbr[c],
                            COL_MUTED, COL_TRANSPARENT, FONT_CAPTION);
    }
    gy += cell;

    /* Day grid. The 1st sits under its real weekday column. */
    i32 first_dow = day_of_week(year, month, 1);
    i32 dim       = days_in_month(month, year);

    for (i32 d = 1; d <= dim; d++) {
        i32 slot = first_dow + (d - 1);
        i32 col  = slot % 7;
        i32 row  = slot / 7;
        i32 cxx  = gx + col * cell;
        i32 cyy  = gy + row * cell;

        char num[4];
        ksprintf(num, "%d", (int)d);

        if (d == today) {
            /* TODAY: a filled accent chip with white text. */
            i32 inset = 2;
            gfx_rect_rounded(cxx + inset, cyy + inset,
                             cell - 2 * inset, cell - 2 * inset,
                             CDL_R_BUTTON, COL_ACCENT);
            gfx_str_centered_ex(cxx, cyy + (cell - cap_lh) / 2, cell, num,
                                COL_WHITE, COL_TRANSPARENT, FONT_CAPTION);
        } else {
            /* Sundays dimmed a touch so weekends read at a glance. */
            u32 col_txt = (col == 0) ? COL_MUTED : COL_TEXT;
            gfx_str_centered_ex(cxx, cyy + (cell - cap_lh) / 2, cell, num,
                                col_txt, COL_TRANSPARENT, FONT_CAPTION);
        }
    }
}

/* Rounded meter bar in the sysmon style: track + coloured fill sized to pct. */
static void draw_meter(i32 x, i32 y, i32 w, i32 h, u32 pct, u32 fill_col) {
    if (pct > 100) pct = 100;
    gfx_rect_rounded(x, y, w, h, h / 2, COL_SURFACE3);
    gfx_rect_rounded_outline(x, y, w, h, h / 2, COL_BORDER);
    if (pct > 0) {
        i32 fill = (w - 2) * (i32)pct / 100;
        if (fill > w - 2) fill = w - 2;
        if (fill > 0)
            gfx_rect_rounded(x + 1, y + 1, fill, h - 2, (h - 2) / 2, fill_col);
    }
}

static void draw_system(rect_t r) {
    i32 h3_lh   = gfx_line_h_ex(FONT_H3);
    i32 cap_lh  = gfx_line_h_ex(FONT_CAPTION);
    i32 body_lh = gfx_line_h_ex(FONT_BODY);
    i32 inner   = r.w - 2 * WGT_PAD;
    i32 cx      = r.x + WGT_PAD;
    i32 cy      = r.y + WGT_PAD;

    /* Title. */
    gfx_str_ex(cx, cy, "System", COL_TEXT, COL_TRANSPARENT, FONT_H3);
    cy += h3_lh + WGT_GAP;

    /* Memory meter -- REAL: kmem_used() over the kernel heap size. 64-bit
     * intermediate so used*100 can't wrap the u32 range at large heaps. */
    u32 mem_pct = (u32)((u64)kmem_used() * 100u / (u64)KERNEL_HEAP_SIZE);
    char pct_s[8];
    ksprintf(pct_s, "%d%%", (int)mem_pct);
    gfx_str_ex(cx, cy, "Memory", COL_DIM, COL_TRANSPARENT, FONT_CAPTION);
    gfx_str_right(cx, cy, inner, pct_s, COL_TEXT, COL_TRANSPARENT);
    cy += cap_lh + 4;

    /* Colour-code the fill by load so the meter carries meaning, not just hue. */
    u32 mem_col = (mem_pct < 60) ? COL_GREEN : (mem_pct < 85 ? COL_YELLOW : COL_RED);
    draw_meter(cx, cy, inner, 12, mem_pct, mem_col);
    cy += 12 + WGT_GAP;

    /* Uptime -- REAL: milliseconds since boot from timer_get_ticks(). */
    u32 secs = timer_get_ticks() / 1000u;
    u32 days = secs / 86400u; secs %= 86400u;
    u32 hrs  = secs / 3600u;  secs %= 3600u;
    u32 mins = secs / 60u;    secs %= 60u;
    char up_s[24];
    if (days > 0) ksprintf(up_s, "%dd %dh %dm", (int)days, (int)hrs, (int)mins);
    else          ksprintf(up_s, "%02d:%02d:%02d", (int)hrs, (int)mins, (int)secs);
    gfx_str_ex(cx, cy, "Uptime", COL_DIM, COL_TRANSPARENT, FONT_BODY);
    gfx_str_right(cx, cy, inner, up_s, COL_ACCENT, COL_TRANSPARENT);
    cy += body_lh;

    /* Active tasks -- REAL: scheduler count. */
    char task_s[8];
    ksprintf(task_s, "%d", (int)task_count_active());
    gfx_str_ex(cx, cy, "Tasks", COL_DIM, COL_TRANSPARENT, FONT_BODY);
    gfx_str_right(cx, cy, inner, task_s, COL_TEXT, COL_TRANSPARENT);
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
