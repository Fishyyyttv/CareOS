/* CareOS v9 -- apps/app_settings.c -- Settings (v10, Phase 3 spec §7)
 *
 * Sidebar: search (filters sections, apps/settings_index.c), the account
 * card, and the sections with coloured glyph tiles. Each section shows only
 * settings and facts that really exist ("real data or hide").
 *
 * Draw registers every clickable rect in a hit list; click and drag look the
 * point up there, so the two can never drift apart (the old code kept two
 * hand-maintained copies of every offset). */
#include "apps_common.h"
#include "font.h"
#include "image.h"
#include "icon.h"
#include "care_wallpaper.h"
#include "ui.h"
#include "settings_index.h"
#include "session/session.h"
#include "../kernel/ext2.h"
#include "../kernel/account_rules.h"

extern void term_ip_to_str(u32 ip, char *out);

/* -- Hit list -------------------------------------------------------------- */
enum {
    A_NONE = 0, A_SECTION, A_SEARCH, A_THEME, A_WALL, A_ACCENT, A_GLASS, A_REDUCE,
    A_FONT, A_RES, A_BRIGHT, A_POINTER, A_SOUND, A_DND, A_CLEAR_NOTES, A_24H,
    A_TZ, A_FIELD, A_PW_APPLY, A_NAME_SAVE, A_AVATAR, A_DIAG, A_DHCP, A_FASTBOOT,
    A_ACCOUNT
};
typedef struct { rect_t r; u8 act; i16 arg; bool content; } shit_t;
#define SHIT_MAX 128
static shit_t s_hits[SHIT_MAX];
static u32    s_nhits;
static rect_t s_view;               /* main content viewport (clip) */
static i32    s_content_h;          /* last drawn content height */
static u8     s_drag_act;           /* slider being dragged */
static rect_t s_drag_r;
static u32    s_drag_val;
static char   s_name[32];           /* display-name field */
static bool   s_name_loaded;
static char   s_diag[160];          /* last diagnostics result */
static u32    s_diag_col;

static void hit(rect_t r, u8 act, i16 arg, bool content) {
    if (s_nhits < SHIT_MAX) { s_hits[s_nhits].r = r; s_hits[s_nhits].act = act; s_hits[s_nhits].arg = arg; s_hits[s_nhits].content = content; s_nhits++; }
}

/* Fields: 0 search, 1 display name, 2 current password, 3 new password. */
#define F_SEARCH 0u
#define F_NAME   1u
#define F_OLDPW  2u
#define F_NEWPW  3u

static void settings_set_status(window_t *w, const char *msg, u32 color) {
    kstrncpy(w->settings_status, msg, sizeof(w->settings_status) - 1);
    w->settings_status[sizeof(w->settings_status) - 1] = '\0';
    w->settings_status_color = color;
}

/* -- Palette ----------------------------------------------------------------- */
typedef struct { u32 body, side, card, line, ink, ink2, muted, sel, field; bool dark; } spal_t;
static spal_t spal(void) {
    spal_t p;
    p.dark  = g_theme->is_dark;
    p.body  = g_theme->window_body;
    p.side  = g_theme->surface_alt;
    p.ink   = g_theme->ink; p.ink2 = g_theme->ink2; p.muted = g_theme->muted;
    p.card  = p.dark ? rgb(0x1a, 0x20, 0x38) : rgb(0xff, 0xff, 0xff);
    p.line  = p.dark ? rgb(0x26, 0x2d, 0x48) : rgb(0xe1, 0xe4, 0xee);
    p.sel   = p.dark ? ui_mix(p.side, g_theme->care, 70) : rgb(0xff, 0xe3, 0xd9);
    p.field = p.dark ? rgb(0x10, 0x15, 0x29) : rgb(0xff, 0xff, 0xff);
    return p;
}

static const u32 SECTION_TILE[SET_COUNT] = {
    rgb(0x12,0x90,0xc2), rgb(0xf2,0x78,0x2e), rgb(0xe5,0x53,0x2e), rgb(0xe2,0x50,0x8f),
    rgb(0xeb,0x49,0x50), rgb(0x7f,0x6c,0xc2), rgb(0x2f,0x86,0xeb), rgb(0x1f,0xa5,0x62),
    rgb(0x8e,0x4d,0xf0)
};
static const ui_glyph_t SECTION_GLYPH[SET_COUNT] = {
    UI_GLYPH_WIRED, UI_GLYPH_SUN, UI_GLYPH_EYE, UI_GLYPH_SOUND, UI_GLYPH_BELL,
    UI_GLYPH_CLOCK, UI_GLYPH_USER, UI_GLYPH_FOLDER, UI_GLYPH_HEART
};

/* -- Small drawing helpers ------------------------------------------------- */
static i32 lh(u32 slot) { return gfx_role_line_h(FONT_ROLE_UI, slot); }
static void txt(i32 x, i32 y, const char *s, u32 c, u32 slot, bool bold) {
    gfx_str_role(x, y, s, c, FONT_ROLE_UI, slot, bold ? FONT_W_BOLD : FONT_W_REGULAR);
}
static i32 txt_w(const char *s, u32 slot, bool bold) {
    return gfx_str_role_width(s, FONT_ROLE_UI, slot, bold ? FONT_W_BOLD : FONT_W_REGULAR);
}
static void card(const spal_t *p, i32 x, i32 y, i32 w, i32 h) {
    gfx_rect_rounded(x, y, w, h, 18, p->card);
    if (!p->dark) gfx_rect_rounded_outline(x, y, w, h, 18, p->line);
}
/* Row title + subtitle at the left of a 60 px row. */
static void row_label(const spal_t *p, i32 x, i32 y, i32 maxw, const char *title, const char *sub) {
    i32 h = lh(2) + (sub ? lh(1) : 0);
    i32 ty = y + (60 - h) / 2;
    gfx_str_role_fit(x, ty, maxw, title, p->ink, FONT_ROLE_UI, 2, FONT_W_BOLD);
    if (sub) gfx_str_role_fit(x, ty + lh(2), maxw, sub, p->muted, FONT_ROLE_UI, 1, FONT_W_REGULAR);
}
static void row_sep(const spal_t *p, i32 x, i32 y, i32 w) { gfx_hline(x, y, w, p->line); }
static void heading(const spal_t *p, i32 x, i32 y, const char *s) {
    char up[48]; u32 i = 0;
    for (; s[i] && i < sizeof up - 1; i++) up[i] = (s[i] >= 'a' && s[i] <= 'z') ? (char)(s[i] - 32) : s[i];
    up[i] = '\0';
    txt(x, y, up, p->muted, 1, true);
}
/* A small button; returns its width. */
static i32 sbutton(const spal_t *p, i32 x, i32 y, const char *label, bool primary, u8 act, i16 arg, bool content) {
    i32 w = txt_w(label, 2, true) + 28, h = 40;
    u32 bg = primary ? g_theme->care : (p->dark ? rgb(0x1c, 0x24, 0x40) : rgb(0xee, 0xf0, 0xf6));
    u32 fg = primary ? g_theme->on_care : p->ink;
    gfx_rect_rounded(x, y, w, h, 12, bg);
    txt(x + 14, y + (h - lh(2)) / 2, label, fg, 2, true);
    hit(rect_make(x, y, w, h), act, arg, content);
    return w;
}
static void field(const spal_t *p, rect_t r, const char *text, const char *ph, bool focused, bool secret) {
    gfx_rect_rounded(r.x, r.y, r.w, r.h, 12, p->field);
    gfx_rect_rounded_outline(r.x, r.y, r.w, r.h, 12, focused ? g_theme->care : p->line);
    char shown[64]; u32 n = 0;
    for (; text[n] && n < sizeof shown - 1; n++) shown[n] = secret ? '*' : text[n];
    shown[n] = '\0';
    i32 ty = r.y + (r.h - lh(2)) / 2;
    if (n) gfx_str_role_fit(r.x + 12, ty, r.w - 24, shown, p->ink, FONT_ROLE_UI, 2, FONT_W_REGULAR);
    else   gfx_str_role_fit(r.x + 12, ty, r.w - 24, ph, p->muted, FONT_ROLE_UI, 2, FONT_W_REGULAR);
    if (focused && ((timer_ms() / 500) & 1) == 0) {
        i32 cx = r.x + 12 + (n ? gfx_str_role_width(shown, FONT_ROLE_UI, 2, FONT_W_REGULAR) : 0) + 1;
        if (cx < r.x + r.w - 10) gfx_rect(cx, ty, 2, lh(2), g_theme->care);
    }
}
/* Segmented control; returns its width. */
static i32 segmented(const spal_t *p, i32 x, i32 y, const char *const *labels, u32 n, u32 sel, u8 act, bool content) {
    i32 pad = 4, h = 44, total = pad;
    i32 ws[8];
    for (u32 i = 0; i < n && i < 8; i++) { ws[i] = txt_w(labels[i], 2, true) + 28; total += ws[i] + (i ? 4 : 0); }
    total += pad;
    gfx_rect_rounded(x, y, total, h, 12, p->dark ? rgb(0x10, 0x15, 0x29) : rgb(0xee, 0xf0, 0xf6));
    i32 bx = x + pad;
    for (u32 i = 0; i < n && i < 8; i++) {
        if (i == sel) {
            gfx_rect_rounded(bx, y + pad, ws[i], h - 2 * pad, 9, p->dark ? rgb(0x2a, 0x33, 0x58) : rgb(0xff, 0xff, 0xff));
        }
        txt(bx + 14, y + (h - lh(2)) / 2, labels[i], i == sel ? p->ink : p->ink2, 2, i == sel);
        hit(rect_make(bx, y + pad, ws[i], h - 2 * pad), act, (i16)i, content);
        bx += ws[i] + 4;
    }
    return total;
}
static void toggle(i32 x, i32 y, bool on, u8 act, bool content) {
    ui_toggle(x, y, on, UI_NORMAL);
    hit(rect_make(x - 4, y - 8, UI_TOGGLE_W + 8, UI_TOGGLE_H + 16), act, 0, content);
}
static void slider(rect_t r, u32 pct, u8 act, bool content) {
    ui_slider(r, pct, UI_NORMAL);
    /* The knob is white: ring it so it shows on a light track too. */
    gfx_circle(r.x + r.w * (i32)(pct > 100 ? 100 : pct) / 100, r.y + r.h / 2, 9, g_theme->is_dark ? g_theme->track : rgb(0xc4, 0xc9, 0xd8));
    hit(rect_make(r.x - 10, r.y, r.w + 20, r.h), act, 0, content);
}
static void fmt_mib(u64 bytes, char *out) {
    u32 mib = (u32)(bytes >> 20);
    if (mib >= 10240) { kutoa(mib / 1024, out, 10); kstrcat(out, " GB"); }
    else if (mib >= 1024) {
        char d[4]; kutoa(mib / 1024, out, 10); kstrcat(out, ".");
        kutoa((mib % 1024) * 10 / 1024, d, 10); kstrcat(out, d); kstrcat(out, " GB");
    } else { kutoa(mib, out, 10); kstrcat(out, " MB"); }
}
static void fmt_tz(i32 m, char *out) {
    kstrcpy(out, "UTC");
    if (m == 0) return;
    kstrcat(out, m < 0 ? "-" : "+");
    u32 a = (u32)(m < 0 ? -m : m);
    char b[8]; kutoa(a / 60, b, 10); kstrcat(out, b);
    if (a % 60) { kstrcat(out, ":"); kutoa(a % 60, b, 10); kstrcat(out, b); }
}
static const i16 TZ_LIST[] = { -600,-540,-480,-420,-360,-300,-240,-180,-120,-60,0,60,120,180,
                               240,300,330,360,420,480,540,570,600,660,720,780 };
#define TZ_N ((i32)(sizeof TZ_LIST / sizeof TZ_LIST[0]))

static void cpu_brand(char *out) {
    static char cached[52];          /* CPUID is a VM exit under virtualisation: once */
    if (cached[0]) { kstrcpy(out, cached); return; }
    u32 r[12];
    u32 a, b, c, d;
    __asm__ volatile ("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x80000000u), "c"(0));
    if (a < 0x80000004u) { kstrcpy(out, "x86-64 processor"); return; }
    for (u32 i = 0; i < 3; i++)
        __asm__ volatile ("cpuid" : "=a"(r[i*4]), "=b"(r[i*4+1]), "=c"(r[i*4+2]), "=d"(r[i*4+3]) : "a"(0x80000002u + i), "c"(0));
    char tmp[49]; kmemcpy(tmp, r, 48); tmp[48] = '\0';
    const char *s = tmp; while (*s == ' ') s++;
    kstrncpy(out, s, 48); out[48] = '\0';
    kstrcpy(cached, out);
}

/* -- Section bodies (y = content top, already scrolled); return the height -- */
static i32 sec_network(window_t *w, const spal_t *p, i32 x, i32 y, i32 cw) {
    (void)w;
    i32 y0 = y;
    bool up = net_is_up();
    u32 mbps = e1000_link_mbps();
    /* Connection card */
    i32 ch = 96;
    gfx_rect_rounded(x, y, cw, ch, 20, p->dark ? ui_mix(p->body, g_theme->care, 40) : rgb(0xff, 0xf0, 0xeb));
    if (!p->dark) gfx_rect_rounded_outline(x, y, cw, ch, 20, rgb(0xff, 0xd6, 0xc8));
    gfx_rect_rounded(x + 20, y + 20, 56, 56, 18, g_theme->care);
    ui_glyph(UI_GLYPH_WIRED, x + 20 + 14, y + 20 + 14, 28, g_theme->on_care);
    gfx_str_role(x + 94, y + 22, "Ethernet", p->ink, FONT_ROLE_DISPLAY, 1, FONT_W_BOLD);
    i32 sy = y + 22 + gfx_role_line_h(FONT_ROLE_DISPLAY, 1) + 2;
    gfx_circle_fill(x + 98, sy + lh(2) / 2, 4, up ? g_theme->success : g_theme->error);
    txt(x + 110, sy, up ? (mbps ? "Connected" : "Connected (link speed unknown)") : "Not connected", p->ink2, 2, false);
    y += ch + 16;

    /* Stat tiles: IP, Router, DNS, Link speed */
    char ip[24], gw[24], dns[24], sp[24];
    if (up) term_ip_to_str(net_get_ip(), ip); else kstrcpy(ip, "None");
    if (up && net_get_gateway()) term_ip_to_str(net_get_gateway(), gw); else kstrcpy(gw, "None");
    if (net_get_dns_server()) term_ip_to_str(net_get_dns_server(), dns); else kstrcpy(dns, "Not set");
    if (mbps) { kutoa(mbps, sp, 10); kstrcat(sp, " Mbps"); } else kstrcpy(sp, "No link");
    const char *lab[4] = { "IP address", "Router", "DNS", "Link speed" };
    const char *val[4] = { ip, gw, dns, sp };
    i32 cols = cw >= 640 ? 4 : 2, tw = (cw - (cols - 1) * 12) / cols, th = 72;
    for (i32 i = 0; i < 4; i++) {
        i32 tx = x + (i % cols) * (tw + 12), ty = y + (i / cols) * (th + 12);
        gfx_rect_rounded(tx, ty, tw, th, 16, p->card);
        if (!p->dark) gfx_rect_rounded_outline(tx, ty, tw, th, 16, p->line);
        txt(tx + 16, ty + 14, lab[i], p->muted, 0, true);
        gfx_str_role_fit(tx + 16, ty + 14 + lh(0) + 4, tw - 32, val[i], p->ink, FONT_ROLE_MONO, 3, FONT_W_BOLD);
    }
    y += ((4 + cols - 1) / cols) * (th + 12) + 4;

    /* Diagnostics result */
    if (s_diag[0]) {
        i32 dh = 52;
        gfx_rect_rounded(x, y, cw, dh, 16, ui_mix(p->body, s_diag_col, p->dark ? 36 : 28));
        gfx_rect_rounded_outline(x, y, cw, dh, 16, ui_mix(p->body, s_diag_col, 90));
        gfx_str_role_fit(x + 16, y + (dh - lh(2)) / 2, cw - 32, s_diag, p->ink, FONT_ROLE_UI, 2, FONT_W_REGULAR);
        y += dh + 16;
    }

    /* Terminal hint */
    i32 hh = 52;
    u32 calm = g_theme->calm;
    gfx_rect_rounded(x, y, cw, hh, 16, ui_mix(p->body, calm, p->dark ? 26 : 20));
    gfx_rect_rounded_outline(x, y, cw, hh, 16, ui_mix(p->body, calm, 64));
    i32 hx = x + 16, hy = y + (hh - lh(2)) / 2;
    txt(hx, hy, "Prefer the terminal? ", p->ink2, 2, false);
    hx += txt_w("Prefer the terminal? ", 2, false);
    i32 cwid = gfx_str_role_width("network --status", FONT_ROLE_MONO, 1, FONT_W_REGULAR) + 12;
    gfx_rect_rounded(hx, y + (hh - 26) / 2, cwid, 26, 6, p->dark ? rgb(0x0f, 0x14, 0x26) : rgb(0xee, 0xf0, 0xf6));
    gfx_str_role(hx + 6, y + (hh - gfx_role_line_h(FONT_ROLE_MONO, 1)) / 2, "network --status", p->ink, FONT_ROLE_MONO, 1, FONT_W_REGULAR);
    hx += cwid + 6;
    gfx_str_role_fit(hx, hy, x + cw - 16 - hx, "shows the same info.", p->ink2, FONT_ROLE_UI, 2, FONT_W_REGULAR);
    y += hh;
    return y - y0;
}

static i32 sec_display(window_t *w, const spal_t *p, i32 x, i32 y, i32 cw) {
    (void)w;
    i32 y0 = y;
    const careos_settings_t *cfg = settings_get();
    heading(p, x, y, "Resolution"); y += lh(1) + 10;
    u32 mc = vesa_mode_count();
    u16 cur_w = vesa_current_w(), cur_h = vesa_current_h();
    if (mc == 0) {
        char b[48], a[12]; kutoa(SCREEN_W, b, 10); kstrcat(b, " x "); kutoa(SCREEN_H, a, 10); kstrcat(b, a);
        card(p, x, y, cw, 60);
        row_label(p, x + 18, y, cw - 36, b, "Set by the boot loader; this display can't switch modes");
        y += 60 + 20;
    } else {
        i32 cols = cw >= 600 ? 3 : 2, bw = (cw - (cols - 1) * 10) / cols, bh = 56;
        for (u32 i = 0; i < mc; i++) {
            vesa_mode_t vm = vesa_mode_get(i);
            bool cur = vm.w == cur_w && vm.h == cur_h;
            i32 bx = x + (i32)(i % (u32)cols) * (bw + 10), by = y + (i32)(i / (u32)cols) * (bh + 10);
            gfx_rect_rounded(bx, by, bw, bh, 14, cur ? (p->dark ? ui_mix(p->body, g_theme->care, 50) : rgb(0xff, 0xf4, 0xf0)) : p->card);
            gfx_rect_rounded_outline(bx, by, bw, bh, 14, cur ? g_theme->care : p->line);
            txt(bx + 16, by + (bh - lh(2)) / 2, vm.label, p->ink, 2, cur);
            if (cur) ui_glyph(UI_GLYPH_CHECK, bx + bw - 34, by + (bh - 18) / 2, 18, g_theme->care);
            hit(rect_make(bx, by, bw, bh), A_RES, (i16)i, true);
        }
        y += (i32)((mc + (u32)cols - 1) / (u32)cols) * (bh + 10) + 10;
    }
    card(p, x, y, cw, 120);
    i32 sw = cw >= 600 ? 240 : cw / 3;
    u32 bpct = s_drag_act == A_BRIGHT ? s_drag_val : cfg->brightness_pct;
    row_label(p, x + 18, y, cw - sw - 60, "Brightness", "Software dimming of the whole screen");
    slider(rect_make(x + cw - 18 - sw, y + 18, sw, 24), (bpct - 10) * 100 / 90, A_BRIGHT, true);
    row_sep(p, x + 1, y + 60, cw - 2);
    u32 mpct = s_drag_act == A_POINTER ? s_drag_val : cfg->mouse_sensitivity;
    char pl[32]; kutoa(mpct, pl, 10); kstrcat(pl, " %");
    row_label(p, x + 18, y + 60, cw - sw - 120, "Pointer speed", pl);
    slider(rect_make(x + cw - 18 - sw, y + 60 + 18, sw, 24), (mpct - 40) * 100 / 160, A_POINTER, true);
    y += 120;
    return y - y0;
}

static void mini_desktop(i32 x, i32 y, i32 w, i32 h, bool dark) {
    /* Everything stays inside the rounded preview, so no clip is needed. */
    gfx_rect_rounded(x, y, w, h, 12, dark ? rgb(0x0b, 0x10, 0x20) : rgb(0xe9, 0xec, 0xf7));
    i32 br = h * 2 / 5;
    gfx_circle_fill(dark ? x + w - br - 10 : x + br + 10, y + br + 12, br, dark ? rgb(0x4a, 0x2a, 0x2e) : rgb(0xc9, 0xd2, 0xff));
    gfx_rect_rounded(x + 6, y + 5, w - 12, 8, 4, dark ? rgb(0x1a, 0x20, 0x38) : rgb(0xfb, 0xfb, 0xfd));
    gfx_rect_rounded(x + 30, y + 24, w - 60 < 120 ? w - 60 : 120, h - 44, 8, dark ? rgb(0x1a, 0x20, 0x38) : rgb(0xff, 0xff, 0xff));
}

static i32 sec_appearance(window_t *w, const spal_t *p, i32 x, i32 y, i32 cw) {
    (void)w;
    i32 y0 = y;
    const careos_settings_t *cfg = settings_get();
    /* Theme cards */
    static const char *const names[3] = { "Light", "Dark", "Auto" };
    static const u32 modes[3] = { 1, 0, 2 };
    i32 tw = (cw - 28) / 3, th = 104 + 16 + lh(2) + 10;
    for (i32 i = 0; i < 3; i++) {
        bool sel = cfg->theme_mode == modes[i];
        i32 tx = x + i * (tw + 14);
        gfx_rect_rounded(tx, y, tw, th, 18, sel ? (p->dark ? ui_mix(p->body, g_theme->care, 40) : rgb(0xff, 0xf4, 0xf0)) : p->card);
        gfx_rect_rounded_outline(tx, y, tw, th, 18, sel ? g_theme->care : p->line);
        if (sel) gfx_rect_rounded_outline(tx + 1, y + 1, tw - 2, th - 2, 17, g_theme->care);
        i32 px = tx + 8, py = y + 8, pw = tw - 16;
        if (modes[i] == 2) {
            /* Auto: day on the left, night on the right (the canvas split). */
            i32 hw = pw / 2;
            gfx_rect_rounded(px, py, pw, 104, 12, rgb(0xe9, 0xec, 0xf7));
            gfx_rect_rounded(px + hw - 12, py, pw - hw + 12, 104, 12, rgb(0x0b, 0x10, 0x20));
            gfx_rect(px + hw, py, 12, 104, rgb(0x0b, 0x10, 0x20));
            gfx_rect(px + hw - 12, py, 12, 104, rgb(0xe9, 0xec, 0xf7));
            ui_glyph(UI_GLYPH_SUN, px + hw / 2 - 14, py + 38, 28, rgb(0xf2, 0x78, 0x2e));
            ui_glyph(UI_GLYPH_MOON, px + hw + (pw - hw) / 2 - 14, py + 38, 28, rgb(0xb7, 0xc1, 0xff));
        } else mini_desktop(px, py, pw, 104, modes[i] == 0);
        txt(tx + 12, y + 8 + 104 + 8, names[i], p->ink, 2, sel);
        if (sel) ui_glyph(UI_GLYPH_CHECK, tx + tw - 32, y + 8 + 104 + 8, 18, g_theme->care);
        hit(rect_make(tx, y, tw, th), A_THEME, (i16)modes[i], true);
    }
    y += th + 24;

    /* Wallpaper strip: the Care background, then installed wallpapers. */
    heading(p, x, y, "Wallpaper"); y += lh(1) + 10;
    {
        i32 ww = 132, wh = 64, gap = 10, cols = (cw + gap) / (ww + gap);
        if (cols < 1) cols = 1;
        i32 n = 0;
        for (u32 t = 0; t <= WALLPAPER_SLOTS; t++) {
            u32 value = t == 0 ? WALLPAPER_CARE : t - 1u;
            image_t *thumb = value == WALLPAPER_CARE ? NULL : wallpaper_thumb(value);
            if (value != WALLPAPER_CARE && !thumb) continue;          /* empty slot: hide */
            i32 wx = x + (n % cols) * (ww + gap), wy = y + (n / cols) * (wh + gap);
            if (value == WALLPAPER_CARE) wallpaper_care_preview(wx, wy, ww, wh);
            else gfx_blit_argb_rounded(wx, wy, thumb->pixels, ww, wh, 12);
            if (cfg->wallpaper == value) {
                gfx_rect_rounded_outline(wx - 3, wy - 3, ww + 6, wh + 6, 15, g_theme->care);
                gfx_rect_rounded_outline(wx - 2, wy - 2, ww + 4, wh + 4, 14, g_theme->care);
            }
            hit(rect_make(wx, wy, ww, wh), A_WALL, (i16)value, true);
            n++;
        }
        y += ((n + cols - 1) / cols) * (wh + gap) + 14;
    }

    /* Accent, glass, reduce transparency, font */
    i32 rows = 4, ch = rows * 60;
    card(p, x, y, cw, ch);
    {
        u32 n = theme_accent_count();
        i32 dx = x + cw - 18 - (i32)n * 38 + 8;
        row_label(p, x + 18, y, dx - x - 30, "Accent color", "Buttons, toggles and highlights");
        for (u32 i = 0; i < n; i++) {
            i32 cx = dx + (i32)i * 38 + 15, cy = y + 30;
            if (theme_get_accent() == i) gfx_circle_fill(cx, cy, 19, p->ink);
            if (theme_get_accent() == i) gfx_circle_fill(cx, cy, 17, p->card);
            gfx_circle_fill(cx, cy, 15, theme_accent_swatch(i));
            hit(rect_make(cx - 17, cy - 17, 34, 34), A_ACCENT, (i16)i, true);
        }
    }
    row_sep(p, x + 1, y + 60, cw - 2);
    {
        i32 sw = cw >= 600 ? 240 : cw / 3;
        u32 g = s_drag_act == A_GLASS ? s_drag_val : cfg->glass_alpha_pct;
        row_label(p, x + 18, y + 60, cw - sw - 60, "Window glass", "How see-through windows and panels are");
        slider(rect_make(x + cw - 18 - sw, y + 60 + 18, sw, 24), (100 - g) * 100 / 60, A_GLASS, true);
    }
    row_sep(p, x + 1, y + 120, cw - 2);
    row_label(p, x + 18, y + 120, cw - 120, "Reduce transparency", "Solid panels instead of blurred glass");
    toggle(x + cw - 18 - UI_TOGGLE_W, y + 120 + (60 - UI_TOGGLE_H) / 2, cfg->reduce_transparency != 0, A_REDUCE, true);
    row_sep(p, x + 1, y + 180, cw - 2);
    {
        const char *fn[8]; u32 nf = font_registry_count(); if (nf > 4) nf = 4;
        for (u32 i = 0; i < nf; i++) fn[i] = font_registry_name(i);
        i32 segw = 8;
        for (u32 i = 0; i < nf; i++) segw += txt_w(fn[i], 2, true) + 32;
        bool fits = segw < cw - 200;
        row_label(p, x + 18, y + 180, fits ? cw - segw - 50 : cw - 36, "Font", fits ? "Text in apps and the shell" : fn[font_active_index() < nf ? font_active_index() : 0]);
        if (fits) segmented(p, x + cw - 18 - segw, y + 180 + 8, fn, nf, font_active_index(), A_FONT, true);
    }
    y += ch;
    return y - y0;
}

static i32 sec_sound(window_t *w, const spal_t *p, i32 x, i32 y, i32 cw) {
    (void)w;
    card(p, x, y, cw, 60);
    row_label(p, x + 18, y, cw - 120, "System sounds", "Beeps from the PC speaker (no sound card driver yet)");
    toggle(x + cw - 18 - UI_TOGGLE_W, y + (60 - UI_TOGGLE_H) / 2, !speaker_muted(), A_SOUND, true);
    return 60;
}

static i32 sec_notify(window_t *w, const spal_t *p, i32 x, i32 y, i32 cw) {
    (void)w;
    card(p, x, y, cw, 120);
    row_label(p, x + 18, y, cw - 120, "Do Not Disturb", "Keep notifications quiet; they still collect in the list");
    toggle(x + cw - 18 - UI_TOGGLE_W, y + (60 - UI_TOGGLE_H) / 2, notify_dnd(), A_DND, true);
    row_sep(p, x + 1, y + 60, cw - 2);
    u32 n = notify_count();
    char sub[48]; kutoa(n, sub, 10); kstrcat(sub, n == 1 ? " notification in the list" : " notifications in the list");
    row_label(p, x + 18, y + 60, cw - 160, "Notification list", sub);
    if (n) {
        i32 bw = txt_w("Clear all", 2, true) + 28;
        sbutton(p, x + cw - 18 - bw, y + 60 + 10, "Clear all", false, A_CLEAR_NOTES, 0, true);
    }
    return 120;
}

static i32 sec_datetime(window_t *w, const spal_t *p, i32 x, i32 y, i32 cw) {
    (void)w;
    const careos_settings_t *cfg = settings_get();
    rtc_time_t t; clock_read_local(&t);
    char now[32], b[8];
    u32 hr = t.hour;
    if (!cfg->clock_24h) { hr = hr % 12; if (hr == 0) hr = 12; }
    kutoa(hr, now, 10); kstrcat(now, ":");
    if (t.minute < 10) kstrcat(now, "0");
    kutoa(t.minute, b, 10); kstrcat(now, b);
    if (!cfg->clock_24h) kstrcat(now, t.hour < 12 ? " AM" : " PM");
    card(p, x, y, cw, 180);
    row_label(p, x + 18, y, cw - 200, "Current time", "From the hardware clock and your time zone");
    i32 nw = txt_w(now, 3, true);
    txt(x + cw - 18 - nw, y + (60 - lh(3)) / 2, now, p->ink, 3, true);
    row_sep(p, x + 1, y + 60, cw - 2);
    row_label(p, x + 18, y + 60, cw - 120, "24-hour clock", "13:00 instead of 1:00 PM");
    toggle(x + cw - 18 - UI_TOGGLE_W, y + 60 + (60 - UI_TOGGLE_H) / 2, cfg->clock_24h != 0, A_24H, true);
    row_sep(p, x + 1, y + 120, cw - 2);
    char tz[16]; fmt_tz(cfg->tz_offset_min, tz);
    row_label(p, x + 18, y + 120, cw - 240, "Time zone", "A fixed offset from UTC (no daylight saving)");
    i32 bx = x + cw - 18 - 40;
    gfx_rect_rounded(bx, y + 130, 40, 40, 12, p->dark ? rgb(0x1c, 0x24, 0x40) : rgb(0xee, 0xf0, 0xf6));
    ui_glyph(UI_GLYPH_CHEVRON_R, bx + 11, y + 141, 18, p->ink);
    hit(rect_make(bx, y + 130, 40, 40), A_TZ, 1, true);
    i32 tw = txt_w(tz, 2, true);
    txt(bx - 12 - tw, y + 120 + (60 - lh(2)) / 2, tz, p->ink, 2, true);
    i32 lx = bx - 24 - tw - 40;
    gfx_rect_rounded(lx, y + 130, 40, 40, 12, p->dark ? rgb(0x1c, 0x24, 0x40) : rgb(0xee, 0xf0, 0xf6));
    ui_glyph(UI_GLYPH_CHEVRON_L, lx + 11, y + 141, 18, p->ink);
    hit(rect_make(lx, y + 130, 40, 40), A_TZ, -1, true);
    return 180;
}

static i32 sec_accounts(window_t *w, const spal_t *p, i32 x, i32 y, i32 cw) {
    i32 y0 = y;
    const user_t *cu = (const user_t *)user_get_by_uid(user_current_uid());
    user_profile_t prof; kmemset(&prof, 0, sizeof prof);
    bool have = user_profile_get(user_current_name(), &prof);
    if (!s_name_loaded) {
        kstrncpy(s_name, have && prof.display_name[0] ? prof.display_name : user_current_name(), 31);
        s_name[31] = '\0'; s_name_loaded = true;
    }
    /* Profile card */
    i32 ph = 112;
    card(p, x, y, cw, ph);
    session_avatar(x + 18 + 38, y + ph / 2, 38, prof.avatar_color, s_name, false);
    gfx_str_role_fit(x + 112, y + 26, cw - 130, s_name[0] ? s_name : user_current_name(), p->ink, FONT_ROLE_DISPLAY, 1, FONT_W_BOLD);
    char role[96];
    kstrcpy(role, cu && cu->is_root ? "Administrator" : "Standard account");
    kstrcat(role, "  -  "); kstrcat(role, cu ? cu->home : "/");
    gfx_str_role_fit(x + 112, y + 26 + gfx_role_line_h(FONT_ROLE_DISPLAY, 1) + 4, cw - 130, role, p->muted, FONT_ROLE_UI, 1, FONT_W_REGULAR);
    y += ph + 20;

    /* A guest session has no stored account: say so instead of offering
     * controls that can't save. */
    if (!have) {
        card(p, x, y, cw, 84);
        txt(x + 18, y + 18, "Guest session", p->ink, 2, true);
        gfx_str_role_fit(x + 18, y + 18 + lh(2) + 2, cw - 36,
                         "Nothing here is saved. Create an account on the login screen to keep a name, avatar and password.",
                         p->muted, FONT_ROLE_UI, 1, FONT_W_REGULAR);
        return y + 84 - y0;
    }

    /* Name + avatar colour */
    heading(p, x, y, "Profile"); y += lh(1) + 10;
    card(p, x, y, cw, 140);
    row_label(p, x + 18, y, 200, "Display name", "Shown on the login screen");
    i32 fx = x + cw / 2 - 40, fw = cw - (fx - x) - 18;
    i32 sbw = txt_w("Save", 2, true) + 28;
    field(p, rect_make(fx, y + 10, fw - sbw - 10, 40), s_name, "Your name", w->settings_field == F_NAME, false);
    hit(rect_make(fx, y + 10, fw - sbw - 10, 40), A_FIELD, F_NAME, true);
    sbutton(p, x + cw - 18 - sbw, y + 10, "Save", true, A_NAME_SAVE, 0, true);
    row_sep(p, x + 1, y + 60, cw - 2);
    row_label(p, x + 18, y + 60, 200, "Avatar colour", NULL);
    for (u32 i = 0; i < 6; i++) {
        i32 cx = fx + 20 + (i32)i * 52, cy = y + 60 + 40;
        if (prof.avatar_color == i) gfx_circle_fill(cx, cy, 22, g_theme->care);
        if (prof.avatar_color == i) gfx_circle_fill(cx, cy, 20, p->card);
        session_avatar(cx, cy, 18, i, s_name, false);
        hit(rect_make(cx - 20, cy - 20, 40, 40), A_AVATAR, (i16)i, true);
    }
    y += 140 + 20;

    /* Password */
    heading(p, x, y, "Password"); y += lh(1) + 10;
    card(p, x, y, cw, 150);
    i32 hw = (cw - 18 * 2 - 12) / 2;
    field(p, rect_make(x + 18, y + 18, hw, 44), w->settings_old_pass, cu && cu->is_root ? "Current password (optional)" : "Current password", w->settings_field == F_OLDPW, true);
    hit(rect_make(x + 18, y + 18, hw, 44), A_FIELD, F_OLDPW, true);
    field(p, rect_make(x + 18 + hw + 12, y + 18, hw, 44), w->settings_new_pass, "New password", w->settings_field == F_NEWPW, true);
    hit(rect_make(x + 18 + hw + 12, y + 18, hw, 44), A_FIELD, F_NEWPW, true);
    /* strength meter for the new password */
    u32 bars = password_meter_bars(w->settings_new_pass);
    for (u32 i = 0; i < 4; i++) {
        u32 col = i < bars ? (bars <= 1 ? g_theme->error : bars <= 2 ? g_theme->warning : g_theme->success) : p->line;
        gfx_rect_rounded(x + 18 + hw + 12 + (i32)i * (hw / 4), y + 70, hw / 4 - 6, 4, 2, col);
    }
    sbutton(p, x + 18, y + 92, "Update password", true, A_PW_APPLY, 0, true);
    if (w->settings_status[0]) {
        i32 bw = txt_w("Update password", 2, true) + 28;
        gfx_str_role_fit(x + 18 + bw + 16, y + 92 + (40 - lh(1)) / 2, cw - bw - 60, w->settings_status,
                         w->settings_status_color ? w->settings_status_color : p->muted, FONT_ROLE_UI, 1, FONT_W_REGULAR);
    }
    y += 150;
    return y - y0;
}

static i32 sec_storage(window_t *w, const spal_t *p, i32 x, i32 y, i32 cw) {
    (void)w;
    u64 total = 0, freeb = 0;
    if (!ext2_space(&total, &freeb)) {
        card(p, x, y, cw, 84);
        txt(x + 18, y + 18, "No disk is mounted", p->ink, 2, true);
        gfx_str_role_fit(x + 18, y + 18 + lh(2) + 2, cw - 36, "Files live in memory and are lost at restart. Attach a disk and restart to keep them.",
                         p->muted, FONT_ROLE_UI, 1, FONT_W_REGULAR);
        return 84;
    }
    u64 used = total - freeb;
    u32 pct = total ? (u32)(used * 100u / total) : 0;
    card(p, x, y, cw, 128);
    gfx_rect_rounded(x + 18, y + 18, 44, 44, 14, SECTION_TILE[SET_STORAGE]);
    ui_glyph(UI_GLYPH_FOLDER, x + 18 + 11, y + 18 + 11, 22, rgb(0xff, 0xff, 0xff));
    txt(x + 78, y + 18, "CareOS disk", p->ink, 2, true);
    char a[24], b[24], line[80];
    fmt_mib(used, a); fmt_mib(total, b);
    kstrcpy(line, a); kstrcat(line, " used of "); kstrcat(line, b); kstrcat(line, " (ext2)");
    gfx_str_role_fit(x + 78, y + 18 + lh(2) + 2, cw - 100, line, p->muted, FONT_ROLE_UI, 1, FONT_W_REGULAR);
    ui_progress(rect_make(x + 18, y + 82, cw - 36, 10), pct, pct >= 90 ? g_theme->error : g_theme->care);
    fmt_mib(freeb, a); kstrcpy(line, a); kstrcat(line, " free");
    txt(x + 18, y + 100, line, p->ink2, 1, false);
    return 128;
}

static i32 sec_about(window_t *w, const spal_t *p, i32 x, i32 y, i32 cw) {
    (void)w;
    i32 y0 = y;
    const careos_settings_t *cfg = settings_get();
    card(p, x, y, cw, 112);
    ui_care_logo(x + 22, y + 24, 64);
    gfx_str_role(x + 104, y + 28, "CareOS", p->ink, FONT_ROLE_DISPLAY, 2, FONT_W_BOLD);
    txt(x + 104, y + 28 + gfx_role_line_h(FONT_ROLE_DISPLAY, 2), "Version 9.0  -  64-bit", p->muted, 1, false);
    y += 112 + 20;

    char cpu[52], mem[24], disp[40], up[32], disk[24], a[12];
    cpu_brand(cpu);
    if (pmm_ram_mib()) fmt_mib((u64)pmm_ram_mib() << 20, mem); else kstrcpy(mem, "Unknown");
    kutoa(SCREEN_W, disp, 10); kstrcat(disp, " x "); kutoa(SCREEN_H, a, 10); kstrcat(disp, a); kstrcat(disp, ", 32-bit colour");
    u32 s = timer_ms() / 1000u;
    if (s < 60u) kstrcpy(up, "Less than a minute");
    else { kutoa(s / 3600u, up, 10); kstrcat(up, " h "); kutoa((s / 60u) % 60u, a, 10); kstrcat(up, a); kstrcat(up, " min"); }
    u64 total = 0;
    if (ext2_space(&total, NULL)) { fmt_mib(total, disk); kstrcat(disk, " (ext2)"); } else kstrcpy(disk, "Not mounted");
    const char *lab[6] = { "Processor", "Memory", "Display", "Network adapter", "Disk", "Up for" };
    const char *val[6] = { cpu, mem, disp, e1000_is_up() ? "Intel PRO/1000 (e1000)" : "None found", disk, up };
    heading(p, x, y, "This computer"); y += lh(1) + 10;
    card(p, x, y, cw, 6 * 52);
    for (i32 i = 0; i < 6; i++) {
        i32 ry = y + i * 52;
        if (i) row_sep(p, x + 1, ry, cw - 2);
        txt(x + 18, ry + (52 - lh(2)) / 2, lab[i], p->ink2, 2, false);
        i32 vw = cw - 220;
        gfx_str_role_fit(x + 200, ry + (52 - lh(2)) / 2, vw, val[i], p->ink, FONT_ROLE_UI, 2, FONT_W_BOLD);
    }
    y += 6 * 52 + 20;
    heading(p, x, y, "Startup"); y += lh(1) + 10;
    card(p, x, y, cw, 60);
    row_label(p, x + 18, y, cw - 120, "Fast boot", "Skip the boot menu delay");
    toggle(x + cw - 18 - UI_TOGGLE_W, y + (60 - UI_TOGGLE_H) / 2, cfg->boot_fast != 0, A_FASTBOOT, true);
    y += 60;
    return y - y0;
}

typedef i32 (*sec_fn)(window_t *, const spal_t *, i32, i32, i32);
static const sec_fn SECTION_FN[SET_COUNT] = {
    sec_network, sec_display, sec_appearance, sec_sound, sec_notify,
    sec_datetime, sec_accounts, sec_storage, sec_about
};

/* -- App entry points ------------------------------------------------------ */
void app_settings_init(window_t *w){
    w->settings_tab = SET_APPEARANCE;
    w->settings_field = F_SEARCH;
    w->settings_old_pass[0] = '\0';
    w->settings_new_pass[0] = '\0';
    w->input_len = 0; w->input_buf[0] = '\0';
    w->settings_status[0] = '\0';
    s_name_loaded = false;
    s_diag[0] = '\0';
}

void app_settings_draw(window_t *w){
    rect_t cr = wm_client_rect(w);
    spal_t p = spal();
    s_nhits = 0;
    if (w->settings_tab >= SET_COUNT) w->settings_tab = 0;

    i32 sb = cr.w >= 900 ? 280 : cr.w >= 640 ? 220 : 180;
    gfx_rect(cr.x, cr.y, cr.w, cr.h, p.body);
    gfx_rect(cr.x, cr.y, sb, cr.h, p.side);
    gfx_vline(cr.x + sb, cr.y, cr.h, p.line);

    /* Sidebar: search */
    i32 sx = cr.x + 14, sw = sb - 28, y = cr.y + 16;
    {
        rect_t r = rect_make(sx, y, sw, 40);
        gfx_rect_rounded(r.x, r.y, r.w, r.h, 12, p.field);
        gfx_rect_rounded_outline(r.x, r.y, r.w, r.h, 12, w->settings_field == F_SEARCH && w->input_len ? g_theme->care : p.line);
        ui_glyph(UI_GLYPH_SEARCH, r.x + 12, r.y + 11, 18, p.muted);
        i32 ty = r.y + (40 - lh(2)) / 2;
        if (w->input_len) gfx_str_role_fit(r.x + 38, ty, r.w - 48, w->input_buf, p.ink, FONT_ROLE_UI, 2, FONT_W_REGULAR);
        else gfx_str_role_fit(r.x + 38, ty, r.w - 48, "Search settings", p.muted, FONT_ROLE_UI, 2, FONT_W_REGULAR);
        if (w->settings_field == F_SEARCH && ((timer_ms() / 500) & 1) == 0) {
            i32 cx = r.x + 38 + (w->input_len ? gfx_str_role_width(w->input_buf, FONT_ROLE_UI, 2, FONT_W_REGULAR) : 0);
            if (cx < r.x + r.w - 10) gfx_rect(cx, ty, 2, lh(2), g_theme->care);
        }
        hit(r, A_SEARCH, 0, false);
        y += 40 + 12;
    }
    /* Account card */
    {
        user_profile_t prof; kmemset(&prof, 0, sizeof prof);
        bool have = user_profile_get(user_current_name(), &prof);
        const char *nm = have && prof.display_name[0] ? prof.display_name : user_current_name();
        rect_t r = rect_make(sx, y, sw, 56);
        gfx_rect_rounded(r.x, r.y, r.w, r.h, 14, p.dark ? rgb(0x1c, 0x24, 0x40) : rgb(0xff, 0xff, 0xff));
        session_avatar(r.x + 10 + 19, r.y + 28, 19, prof.avatar_color, nm, false);
        gfx_str_role_fit(r.x + 58, r.y + 10, r.w - 66, nm, p.ink, FONT_ROLE_UI, 2, FONT_W_BOLD);
        gfx_str_role_fit(r.x + 58, r.y + 10 + lh(2), r.w - 66, user_is_root() ? "Administrator" : "Standard account",
                         p.muted, FONT_ROLE_UI, 0, FONT_W_REGULAR);
        hit(r, A_ACCOUNT, 0, false);
        y += 56 + 12;
    }
    /* Sections (filtered by the search) */
    u32 vis[SET_COUNT];
    u32 nv = settings_filter(w->input_len ? w->input_buf : "", vis, SET_COUNT);
    if (nv == 0) {
        gfx_str_role_wrap(sx + 8, y + 6, sw - 16, "Nothing matches. Try another word, like \"theme\" or \"disk\".",
                          p.muted, FONT_ROLE_UI, 1, FONT_W_REGULAR, 2);
    }
    for (u32 k = 0; k < nv; k++) {
        u32 i = vis[k];
        rect_t r = rect_make(sx, y, sw, 40);
        bool sel = i == w->settings_tab;
        if (sel) gfx_rect_rounded(r.x, r.y, r.w, r.h, 12, p.sel);
        gfx_rect_rounded(r.x + 10, r.y + 7, 26, 26, 8, SECTION_TILE[i]);
        ui_glyph(SECTION_GLYPH[i], r.x + 10 + 5, r.y + 7 + 5, 16, rgb(0xff, 0xff, 0xff));
        gfx_str_role_fit(r.x + 48, r.y + (40 - lh(2)) / 2, r.w - 56, SETTINGS_SECTIONS[i].title, p.ink,
                         FONT_ROLE_UI, 2, sel ? FONT_W_BOLD : FONT_W_REGULAR);
        hit(r, A_SECTION, (i16)i, false);
        y += 43;
    }

    /* Main: title + section body, scrolled and clipped. */
    i32 mx = cr.x + sb + (cr.w - sb >= 640 ? 36 : 20), mw = cr.x + cr.w - (cr.w - sb >= 640 ? 36 : 20) - mx;
    i32 top = cr.y + 28;
    u32 sec = w->settings_tab;
    gfx_str_role(mx, top, SETTINGS_SECTIONS[sec].title, p.ink, FONT_ROLE_DISPLAY, 2, FONT_W_BOLD);
    i32 title_h = gfx_role_line_h(FONT_ROLE_DISPLAY, 2);
    /* Network actions sit beside the title, as on the canvas. */
    if (sec == SET_NETWORK) {
        i32 b2 = txt_w("Renew DHCP", 2, true) + 28, b1 = txt_w("Run diagnostics", 2, true) + 28;
        i32 bx = mx + mw - b2;
        if (bx - b1 - 8 > mx + txt_w("Network", 3, true) + 40) {
            sbutton(&p, bx, top + (title_h - 40) / 2, "Renew DHCP", false, A_DHCP, 0, false);
            sbutton(&p, bx - 8 - b1, top + (title_h - 40) / 2, "Run diagnostics", false, A_DIAG, 0, false);
        }
    }
    i32 body_top = top + title_h + 18;
    s_view = rect_make(cr.x + sb + 1, body_top, cr.w - sb - 1, cr.y + cr.h - body_top);
    i32 max_scroll = s_content_h - s_view.h + 24;
    if (max_scroll < 0) max_scroll = 0;
    if ((i32)w->scroll * 12 > max_scroll) w->scroll = (u32)((max_scroll + 11) / 12);
    i32 off = (i32)w->scroll * 12;
    if (off > max_scroll) off = max_scroll;
    gfx_set_clip(s_view.x, s_view.y, s_view.w, s_view.h);
    s_content_h = SECTION_FN[sec](w, &p, mx, body_top - off, mw);
    gfx_clear_clip();
    if (max_scroll > 0) {
        i32 th = s_view.h * s_view.h / (s_content_h + 24); if (th < 30) th = 30;
        i32 ty = s_view.y + (s_view.h - th) * off / max_scroll;
        gfx_rect_rounded_blend(cr.x + cr.w - 8, ty, 4, th, 2, p.ink, 70);
    }
}

/* -- Actions --------------------------------------------------------------- */
static void apply_theme_mode(u32 mode) {
    settings_set_theme_mode(mode);
    bool dark = mode == 0;
    if (mode == 2) { rtc_time_t t; clock_read_local(&t); dark = settings_auto_theme_dark(t.hour); }
    if (mode <= 1) user_set_current_theme_preference(mode);
    theme_switch(dark);
    gfx_dirty(0, 0, (i32)SCREEN_W, (i32)SCREEN_H);
}

static u32 slider_pct(rect_t r, i32 x) {
    i32 v = (x - (r.x + 10)) * 100 / (r.w - 20);
    return v < 0 ? 0 : v > 100 ? 100 : (u32)v;
}

/* Live value while dragging; nothing is saved until release. */
static void slider_live(u8 act, u32 pct) {
    const careos_settings_t *cfg = settings_get();
    if (act == A_BRIGHT)  { s_drag_val = 10 + pct * 90 / 100; gui_set_brightness_live(s_drag_val); }
    if (act == A_POINTER) { s_drag_val = 40 + pct * 160 / 100; s_drag_val -= s_drag_val % 10; }
    if (act == A_GLASS)   { s_drag_val = 100 - pct * 60 / 100; theme_set_glass(s_drag_val, cfg->reduce_transparency != 0); }
}

void app_settings_release(window_t *w) {
    (void)w;
    if (!s_drag_act) return;
    const careos_settings_t *cfg = settings_get();
    if (s_drag_act == A_BRIGHT)  settings_set_brightness(s_drag_val);
    if (s_drag_act == A_POINTER) settings_set_mouse_sensitivity(s_drag_val);
    if (s_drag_act == A_GLASS)   settings_set_glass(s_drag_val, cfg->reduce_transparency != 0);
    s_drag_act = 0;
}

void app_settings_drag(window_t *w, i32 x, i32 y) {
    (void)w; (void)y;
    if (!s_drag_act) return;
    slider_live(s_drag_act, slider_pct(s_drag_r, x));
}

static void run_diagnostics(void) {
    char ip[24];
    if (!e1000_is_up()) { kstrcpy(s_diag, "No network adapter was found."); s_diag_col = g_theme->error; return; }
    if (!e1000_link_mbps()) { kstrcpy(s_diag, "The cable or virtual link is down. Check the connection."); s_diag_col = g_theme->error; return; }
    if (!net_is_up()) { kstrcpy(s_diag, "Link is up but there is no address. Try Renew DHCP."); s_diag_col = g_theme->warning; return; }
    if (!net_trylock()) { kstrcpy(s_diag, "The network is busy (a page is loading). Try again in a moment."); s_diag_col = g_theme->warning; return; }
    u32 addr = 0;
    int rc = dns_resolve("example.com", &addr);
    net_unlock();
    term_ip_to_str(net_get_ip(), ip);
    if (rc == 0 && addr) {
        char r[24]; term_ip_to_str(addr, r);
        kstrcpy(s_diag, "All good: link up, address "); kstrcat(s_diag, ip);
        kstrcat(s_diag, ", DNS resolved example.com to "); kstrcat(s_diag, r); kstrcat(s_diag, ".");
        s_diag_col = g_theme->success;
    } else {
        kstrcpy(s_diag, "Address "); kstrcat(s_diag, ip);
        kstrcat(s_diag, " works locally, but DNS lookups fail. Check the DNS server.");
        s_diag_col = g_theme->warning;
    }
}

static void apply_password(window_t *w) {
    int rc = user_change_password(user_current_name(),
        w->settings_old_pass[0] ? w->settings_old_pass : NULL, w->settings_new_pass);
    if (rc == 0) {
        settings_set_status(w, "Password updated", g_theme->success);
        w->settings_old_pass[0] = '\0'; w->settings_new_pass[0] = '\0';
    } else if (rc == -2) settings_set_status(w, "Use 8+ characters with upper, lower case and a digit", g_theme->warning);
    else if (rc == -4) settings_set_status(w, "The current password didn't match", g_theme->error);
    else settings_set_status(w, "Couldn't update the password", g_theme->error);
}

static void save_name(window_t *w) {
    user_profile_t prof; kmemset(&prof, 0, sizeof prof);
    user_profile_get(user_current_name(), &prof);
    kstrncpy(prof.display_name, s_name, sizeof prof.display_name - 1);
    prof.display_name[sizeof prof.display_name - 1] = '\0';
    if (user_profile_set(user_current_name(), &prof) == 0) settings_set_status(w, "Name saved", g_theme->success);
    else settings_set_status(w, "Couldn't save the name", g_theme->error);
}

static void do_action(window_t *w, const shit_t *h, i32 x) {
    const careos_settings_t *cfg = settings_get();
    switch (h->act) {
    case A_SECTION: w->settings_tab = (u32)h->arg; w->scroll = 0; w->settings_status[0] = '\0'; break;
    case A_SEARCH:  w->settings_field = F_SEARCH; break;
    case A_ACCOUNT: w->settings_tab = SET_ACCOUNTS; w->scroll = 0; break;
    case A_THEME:   apply_theme_mode((u32)h->arg); break;
    case A_WALL:    settings_set_wallpaper((u32)h->arg); break;
    case A_ACCENT:  settings_set_accent((u32)h->arg); gfx_dirty(0, 0, (i32)SCREEN_W, (i32)SCREEN_H); break;
    case A_REDUCE:
        settings_set_glass(cfg->glass_alpha_pct, !cfg->reduce_transparency);
        theme_set_glass(cfg->glass_alpha_pct, cfg->reduce_transparency != 0);
        break;
    case A_FONT:
        settings_set_font_family((u32)h->arg);
        user_set_current_font_preference((u32)h->arg);
        gfx_dirty(0, 0, (i32)SCREEN_W, (i32)SCREEN_H);
        break;
    case A_RES: {
        vesa_mode_t vm = vesa_mode_get((u32)h->arg);
        if (vesa_set_mode(vm.w, vm.h) == 0) settings_set_vesa_mode((u32)vm.w, (u32)vm.h);
        break;
    }
    case A_BRIGHT: case A_POINTER: case A_GLASS:
        s_drag_act = h->act; s_drag_r = h->r;
        slider_live(h->act, slider_pct(h->r, x));
        break;
    case A_SOUND:   speaker_set_muted(!speaker_muted()); break;
    case A_DND:     { bool on = !notify_dnd(); notify_set_dnd(on); settings_set_dnd(on); break; }
    case A_CLEAR_NOTES: notify_clear_all(); break;
    case A_24H:     settings_set_clock_24h(!cfg->clock_24h); break;
    case A_TZ: {
        i32 cur = 0;
        for (i32 i = 0; i < TZ_N; i++) if (TZ_LIST[i] == cfg->tz_offset_min) cur = i;
        cur += h->arg;
        if (cur < 0) cur = 0;
        if (cur >= TZ_N) cur = TZ_N - 1;
        settings_set_tz_offset(TZ_LIST[cur]);
        break;
    }
    case A_FIELD:   w->settings_field = (u32)h->arg; break;
    case A_PW_APPLY: apply_password(w); break;
    case A_NAME_SAVE: save_name(w); break;
    case A_AVATAR: {
        user_profile_t prof; kmemset(&prof, 0, sizeof prof);
        user_profile_get(user_current_name(), &prof);
        prof.avatar_color = (u8)h->arg;
        user_profile_set(user_current_name(), &prof);
        break;
    }
    case A_DIAG:    run_diagnostics(); break;
    case A_DHCP:
        if (!net_trylock()) { kstrcpy(s_diag, "The network is busy. Try again in a moment."); s_diag_col = g_theme->warning; }
        else {
            int rc = net_dhcp_renew(); net_unlock();
            if (rc == 0) { kstrcpy(s_diag, "Got a fresh address from DHCP."); s_diag_col = g_theme->success; }
            else { kstrcpy(s_diag, "No DHCP server answered. Check the cable or the virtual network."); s_diag_col = g_theme->error; }
        }
        break;
    case A_FASTBOOT: settings_set_boot_fast(!cfg->boot_fast); break;
    default: break;
    }
}

void app_settings_click(window_t *w, i32 x, i32 y, mouse_t *m) {
    (void)m;
    for (u32 i = s_nhits; i-- > 0;) {
        const shit_t *h = &s_hits[i];
        if (!rect_contains(h->r, x, y)) continue;
        if (h->content && !rect_contains(s_view, x, y)) continue;
        do_action(w, h, x);
        return;
    }
}

static void edit_buf(char *buf, u32 cap, u32 *len_io, char c) {
    u32 len = len_io ? *len_io : (u32)kstrlen(buf);
    if (c == '\b') { if (len) buf[--len] = '\0'; }
    else if (c >= 32 && c < 127 && len + 1 < cap) { buf[len++] = c; buf[len] = '\0'; }
    if (len_io) *len_io = len;
}

void app_settings_key(window_t *w, char c) {
    u8 k = (u8)c;
    if (k == KEY_NAV_UP || k == KEY_NAV_DOWN) {
        if (w->settings_field != F_SEARCH) return;
        u32 vis[SET_COUNT];
        u32 nv = settings_filter(w->input_len ? w->input_buf : "", vis, SET_COUNT);
        if (!nv) return;
        u32 cur = 0;
        for (u32 i = 0; i < nv; i++) if (vis[i] == w->settings_tab) cur = i;
        cur = k == KEY_NAV_UP ? (cur + nv - 1) % nv : (cur + 1) % nv;
        w->settings_tab = vis[cur]; w->scroll = 0;
        return;
    }
    if (k == KEY_NAV_PGDN) { w->scroll += 20; return; }
    if (k == KEY_NAV_PGUP) { w->scroll = w->scroll > 20 ? w->scroll - 20 : 0; return; }
    if (c == 0x1B) {
        if (w->settings_field != F_SEARCH) w->settings_field = F_SEARCH;
        else { w->input_len = 0; w->input_buf[0] = '\0'; }
        return;
    }
    if (c == '\t' && w->settings_tab == SET_ACCOUNTS) {
        w->settings_field = w->settings_field == F_SEARCH ? F_NAME : w->settings_field == F_NAME ? F_OLDPW :
                            w->settings_field == F_OLDPW ? F_NEWPW : F_SEARCH;
        return;
    }
    switch (w->settings_field) {
    case F_NAME:
        if (c == '\n') { save_name(w); return; }
        edit_buf(s_name, sizeof s_name, NULL, c);
        return;
    case F_OLDPW:
        if (c == '\n') { w->settings_field = F_NEWPW; return; }
        edit_buf(w->settings_old_pass, sizeof w->settings_old_pass, NULL, c);
        return;
    case F_NEWPW:
        if (c == '\n') { apply_password(w); return; }
        edit_buf(w->settings_new_pass, sizeof w->settings_new_pass, NULL, c);
        return;
    default:
        if (c == '\n') return;
        edit_buf(w->input_buf, 48, &w->input_len, c);
        /* Keep the selection on a section that still matches. */
        if (!settings_section_matches(w->settings_tab, w->input_buf)) {
            u32 first;
            if (settings_filter(w->input_buf, &first, 1)) { w->settings_tab = first; w->scroll = 0; }
        }
        return;
    }
}
