/* gui/session/common.c -- pieces shared by the start screens. */
#include "kernel.h"
#include "session/session.h"

/* The avatar gradients (Signup artboard: Sunset, Ocean, Mint, Honey, plus two
 * more so the six profile colours all have one). 150deg, light to dark. */
static const u32 AVATAR[AVATAR_COLORS][2] = {
    { 0xFF9A76, 0x7B61FF },   /* Sunset */
    { 0x3CC8E8, 0x3F6DF2 },   /* Ocean  */
    { 0x7CF0B5, 0x0E9E8F },   /* Mint   */
    { 0xFFD36B, 0xF2782E },   /* Honey  */
    { 0xC08BFF, 0x6A4FF0 },   /* Grape  */
    { 0xFF8FA8, 0xE2508F },   /* Rose   */
};

void session_avatar_colors(u32 idx, u32 *c0, u32 *c1) {
    if (idx >= AVATAR_COLORS) idx = 0;
    *c0 = AVATAR[idx][0];
    *c1 = AVATAR[idx][1];
}

/* A gradient disc with an anti-aliased edge and the name's initial. */
void session_avatar(i32 cx, i32 cy, i32 r, u32 color_idx, const char *name, bool guest) {
    if (r < 4) return;
    u32 c0, c1;
    session_avatar_colors(color_idx, &c0, &c1);
    if (guest) c0 = c1 = 0x36D1C0;
    i32 d = r * 2;
    u32 *px = (u32 *)kmalloc((size_t)d * (size_t)d * 4u);
    if (!px) { gfx_circle_fill(cx, cy, r, c0); return; }
    /* linear-gradient(150deg): direction (sin 150, -cos 150) = (.5, .866) */
    i64 len = (i64)d * 1366;
    for (i32 y = 0; y < d; y++) {
        for (i32 x = 0; x < d; x++) {
            i64 dx = (i64)(2 * x + 1 - d) * 128, dy = (i64)(2 * y + 1 - d) * 128;   /* Q8 */
            i64 dist = 0;
            {   /* isqrt of dx^2+dy^2 */
                u64 v = (u64)(dx * dx + dy * dy), res = 0, bit = 1ull << 62;
                while (bit > v) bit >>= 2;
                while (bit) { if (v >= res + bit) { v -= res + bit; res = (res >> 1) + bit; } else res >>= 1; bit >>= 2; }
                dist = (i64)res;
            }
            i64 cov = 128 + ((i64)r * 256 - dist);
            if (cov <= 0) { px[y * d + x] = 0; continue; }
            if (cov > 255) cov = 255;
            i64 proj = (i64)(2 * x + 1 - d) * 500 + (i64)(2 * y + 1 - d) * 866;
            i64 t = 128 + proj * 128 / len;
            if (t < 0) t = 0;
            if (t > 256) t = 256;
            u32 col = ui_mix(c0, c1, (u8)(t > 255 ? 255 : t));
            px[y * d + x] = ((u32)cov << 24) | col;
        }
    }
    gfx_blend_argb(cx - r, cy - r, px, d, d);
    kfree(px);

    char ini[2] = { name && name[0] ? name[0] : '?', 0 };
    if (ini[0] >= 'a' && ini[0] <= 'z') ini[0] = (char)(ini[0] - 32);
    if (guest) ini[0] = 'G';
    u32 slot = r >= 44 ? 3u : (r >= 26 ? 2u : (r >= 16 ? 0u : 0u));
    font_role_t role = r >= 16 ? FONT_ROLE_DISPLAY : FONT_ROLE_UI;
    if (r < 16) slot = 1;
    i32 tw = gfx_str_role_width(ini, role, slot, FONT_W_BOLD);
    i32 th = gfx_role_line_h(role, slot);
    gfx_str_role(cx - tw / 2, cy - th / 2, ini, guest ? 0x062A26u : 0xFFFFFFu, role, slot, FONT_W_BOLD);
}

void session_pet_slot(rect_t r, u32 mood) { (void)r; (void)mood; }

void session_frame_end(const mouse_t *m) {
    if (m) mouse_draw_cursor(m->x, m->y);
    gfx_flip();
    __asm__ volatile("sti; hlt");
}

static const char *const DAYS[7] = { "Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday" };
static const char *const MONS[12] = { "January","February","March","April","May","June","July",
                                      "August","September","October","November","December" };

void session_date_line(char *out, bool weekday_long) {
    rtc_time_t t;
    clock_read_local(&t);
    const char *mon = (t.month >= 1 && t.month <= 12) ? MONS[t.month - 1] : "";
    i32 wd = clock_day_of_week((i32)t.year, (i32)t.month, (i32)t.day);
    if (weekday_long) ksprintf(out, "%s, %s %d", DAYS[wd], mon, (int)t.day);
    else ksprintf(out, "%s %d", mon, (int)t.day);
}

void session_time_line(char *out) {
    rtc_time_t t;
    clock_read_local(&t);
    const careos_settings_t *cfg = settings_get();
    if (cfg && cfg->clock_24h) ksprintf(out, "%02d:%02d", (int)t.hour, (int)t.minute);
    else {
        int h = (int)t.hour % 12;
        if (h == 0) h = 12;
        ksprintf(out, "%d:%02d", h, (int)t.minute);
    }
}

bool sfield_key(sfield_t *f, char c) {
    if (c == '\b') {
        if (f->len == 0) return false;
        f->buf[--f->len] = '\0';
        return true;
    }
    if (c < 32 || c > 126 || f->len >= sizeof(f->buf) - 1) return false;
    f->buf[f->len++] = c;
    f->buf[f->len] = '\0';
    return true;
}

void sfield_clear(sfield_t *f) {
    kmemset(f->buf, 0, sizeof(f->buf));
    f->len = 0;
}

void sfield_text(const sfield_t *f, char *out, u32 cap) {
    u32 n = f->len < cap - 1 ? f->len : cap - 1;
    if (f->secret && !f->reveal) { for (u32 i = 0; i < n; i++) out[i] = '*'; }
    else kmemcpy(out, f->buf, n);
    out[n] = '\0';
}

void session_field(rect_t r, const sfield_t *f, const char *placeholder, bool focused,
                   u32 fill, u32 border, u32 ink, u32 muted) {
    i32 rad = r.h >= 50 ? 18 : 14;
    if (focused) gfx_rect_rounded_blend(r.x - 4, r.y - 4, r.w + 8, r.h + 8, rad + 4, g_theme->calm, 36);
    gfx_rect_rounded(r.x, r.y, r.w, r.h, rad, focused ? g_theme->calm : border);
    gfx_rect_rounded(r.x + 2, r.y + 2, r.w - 4, r.h - 4, rad - 2, fill);
    i32 ty = r.y + (r.h - gfx_role_line_h(FONT_ROLE_UI, 2)) / 2;
    if (f->len == 0) {
        if (placeholder) gfx_str_role(r.x + 14, ty, placeholder, muted, FONT_ROLE_UI, 2, FONT_W_REGULAR);
    } else if (f->secret && !f->reveal) {
        i32 cx = r.x + 14 + 4;
        for (u32 i = 0; i < f->len && cx < r.x + r.w - 20; i++, cx += 13)
            gfx_circle_fill(cx, r.y + r.h / 2, 4, ink);
    } else {
        gfx_str_role_fit(r.x + 14, ty, r.w - 28, f->buf, ink, FONT_ROLE_UI, 2, FONT_W_REGULAR);
    }
    if (focused && (timer_ms() / 500) % 2 == 0) {
        i32 cx;
        if (f->secret && !f->reveal) cx = r.x + 14 + (i32)f->len * 13;
        else cx = r.x + 14 + gfx_str_role_width(f->buf, FONT_ROLE_UI, 2, FONT_W_REGULAR) + 1;
        if (cx < r.x + r.w - 12) gfx_rect(cx, ty + 2, 2, gfx_role_line_h(FONT_ROLE_UI, 2) - 4, g_theme->care);
    }
}
