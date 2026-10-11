/* CareOS v9 -- apps/app_pip.c -- Pip's Home (Phase 4 spec §5).
 * The room with a big live Pip, his name, real stats, a mood row, Pat /
 * Tuck in / Surprise me, "Let <name> out", and a dress-up aside whose tiles
 * preview each choice. Every change applies everywhere at once. */
#include "apps_common.h"
#include "ui.h"
#include "../gui/pip/pip.h"
#include "../gui/pip/pip_chat.h"
#include "../gui/pip/pip_assist.h"

enum { HA_NONE = 0, HA_NAME, HA_MOOD, HA_PAT, HA_TUCK, HA_SURPRISE, HA_OUT, HA_TAB, HA_OPT, HA_TOGGLE, HA_CHIP, HA_INPUT, HA_SEND, HA_ASIDE, HA_ENABLE };
typedef struct { rect_t r; u8 act; i32 arg; } hhit_t;
typedef struct {
    hhit_t hits[80]; u32 nhits;
    u32 tab;                       /* 0 color, 1 hat, 2 outfit, 3 acc, 4 effect */
    bool name_focus;
    i32 preview_mood; u32 preview_until;
    u32 aside;                     /* 0 chat, 1 dress up */
    char input[128]; u32 in_len;
    bool stick; u32 last_scroll, seen_serial;
} hstate_t;

static hstate_t *hst(window_t *w) {
    if (!w->app_state) {
        hstate_t *s = (hstate_t *)kmalloc(sizeof(hstate_t));
        if (!s) return NULL;
        kmemset(s, 0, sizeof *s);
        s->tab = 1; s->preview_mood = -1; s->stick = true;
        w->app_state = s;
    }
    return (hstate_t *)w->app_state;
}
static void hit(hstate_t *s, rect_t r, u8 a, i32 g) { if (s->nhits < 80) { s->hits[s->nhits].r = r; s->hits[s->nhits].act = a; s->hits[s->nhits].arg = g; s->nhits++; } }
static i32 lh(u32 slot) { return gfx_role_line_h(FONT_ROLE_UI, slot); }

static const char *const TAB_L[5] = { "Color", "Hats", "Outfits", "Extras", "Effects" };
static u32 tab_count(u32 t) { return t == 0 ? PIP_COLORS : t == 1 ? PIP_HATS : t == 2 ? PIP_OUTFITS : t == 3 ? PIP_ACCS : PIP_EFFECTS; }
static const char *opt_label(u32 t, u32 i) {
    return t == 0 ? pip_color_label(i) : t == 1 ? pip_hat_label(i) : t == 2 ? pip_outfit_label(i) : t == 3 ? pip_acc_label(i) : pip_effect_label(i);
}
static u8 *look_field(pip_look_t *l, u32 t) {
    return t == 0 ? &l->color : t == 1 ? &l->hat : t == 2 ? &l->outfit : t == 3 ? &l->acc : &l->effect;
}

static i32 s_req_aside = -1;
void app_pip_request_aside(u32 aside) { s_req_aside = (i32)aside; }

void app_pip_init(window_t *w) {
    (void)hst(w);
    if (pip_brain()->enabled) {
        pip_event(PE_OPEN_HOME);                  /* he perks up when his door opens */
        pip_chat_intro();
    }
}

static void meter(i32 x, i32 y, i32 w, const char *label, const char *val, u32 pct, u32 col) {
    gfx_str_role(x, y, label, rgb(0xEE, 0xF1, 0xFA), FONT_ROLE_UI, 1, FONT_W_BOLD);
    i32 vw = gfx_str_role_width(val, FONT_ROLE_UI, 1, FONT_W_BOLD);
    gfx_str_role(x + w - vw, y, val, rgb(0xD5, 0xDA, 0xEA), FONT_ROLE_UI, 1, FONT_W_BOLD);
    gfx_rect_rounded(x, y + lh(1) + 6, w, 8, 4, rgb(0x2E, 0x37, 0x57));
    i32 fw = w * (i32)(pct > 100 ? 100 : pct) / 100;
    if (fw > 8) gfx_rect_rounded(x, y + lh(1) + 6, fw, 8, 4, col);
}

static i32 chip(hstate_t *s, i32 x, i32 y, const char *label, u32 dot, bool on, u8 act, i32 arg) {
    i32 w = gfx_str_role_width(label, FONT_ROLE_UI, 1, FONT_W_BOLD) + (dot ? 34 : 24);
    gfx_rect_rounded(x, y, w, 34, 17, on ? rgb(0xFF, 0xFF, 0xFF) : rgb(0x26, 0x2D, 0x4F));
    if (dot) gfx_circle_fill(x + 15, y + 17, 5, dot);
    gfx_str_role(x + (dot ? 26 : 12), y + (34 - lh(1)) / 2, label, on ? rgb(0x1A, 0x16, 0x30) : rgb(0xEE, 0xF1, 0xFA), FONT_ROLE_UI, 1, FONT_W_BOLD);
    hit(s, rect_make(x, y, w, 34), act, arg);
    return w;
}

/* ---- chat panel -------------------------------------------------------------- */
#define CH_LINES 48
static i32 spw_cache;

/* Word-wrap t into lines no wider than maxw. Returns the line count; *widest is the longest line. */
static u32 wrap_text(const char *t, i32 maxw, u16 *st, u16 *ln, i32 *widest) {
    u32 len = (u32)kstrlen(t), n = 0, i = 0;
    if (!spw_cache) spw_cache = gfx_str_role_width(" ", FONT_ROLE_UI, 1, FONT_W_REGULAR);
    *widest = 0;
    while (i < len && n < CH_LINES) {
        if (t[i] == '\n') { st[n] = (u16)i; ln[n] = 0; n++; i++; continue; }
        u32 ls = i, le = i; i32 lw = 0;
        while (i < len && t[i] != '\n') {
            u32 we = i; while (we < len && t[we] != ' ' && t[we] != '\n') we++;
            i32 ww = gfx_str_role_width_n(t + i, we - i, FONT_ROLE_UI, 1, FONT_W_REGULAR);
            if (le == ls && ww > maxw) {                      /* one huge word (a long path): cut it */
                u32 k = we - i; while (k > 1 && gfx_str_role_width_n(t + i, k, FONT_ROLE_UI, 1, FONT_W_REGULAR) > maxw) k--;
                le = i + k; lw = gfx_str_role_width_n(t + i, k, FONT_ROLE_UI, 1, FONT_W_REGULAR); i = le; break;
            }
            if (le != ls && lw + spw_cache + ww > maxw) break;
            lw = le == ls ? ww : lw + spw_cache + ww;
            le = we; i = we;
            while (i < len && t[i] == ' ') i++;
        }
        st[n] = (u16)ls; ln[n] = (u16)(le - ls); n++;
        if (lw > *widest) *widest = lw;
        if (i < len && t[i] == '\n') i++;
    }
    return n;
}

static void typing_dots(i32 x, i32 y) {
    u32 ph = (timer_ms() / 280) % 4;
    for (u32 d = 0; d < 3; d++) {
        i32 up = (ph == d + 1) ? 3 : 0;
        gfx_circle_fill(x + 12 + (i32)d * 12, y + 17 - up, 3, g_theme->muted);
    }
}

static void draw_chat(window_t *w, hstate_t *s, rect_t conv, rect_t chips_r, rect_t in_r) {
    pip_brain_t *b = pip_brain();
    bool dark = g_theme->is_dark;
    /* conversation: measure everything, then draw what's visible */
    u32 n = pip_chat_count();
    i32 maxbw = (conv.w - 16) * 84 / 100, pad = 12, gap = 10, lhh = lh(1) + 3;
    i32 total = 8;
    for (u32 i = 0; i < n; i++) {
        u16 st[CH_LINES], ln[CH_LINES]; i32 wd;
        u32 nl = wrap_text(pip_chat_get(i)->text, maxbw - 2 * pad, st, ln, &wd);
        total += (i32)nl * lhh + 2 * pad - 3 + gap;
    }
    if (pip_chat_busy()) total += 36 + gap;
    i32 maxoff = total > conv.h ? total - conv.h : 0;
    if (pip_chat_serial() != s->seen_serial) { s->seen_serial = pip_chat_serial(); s->stick = true; }
    if (w->scroll != s->last_scroll) s->stick = (i32)w->scroll * 12 >= maxoff;
    if (s->stick) w->scroll = (u32)((maxoff + 11) / 12);
    s->last_scroll = w->scroll;
    i32 off = (i32)w->scroll * 12; if (off > maxoff) off = maxoff;
    i32 y = conv.y + (total < conv.h ? conv.h - total : 0) + 8 - off;
    gfx_set_clip(conv.x, conv.y, conv.w, conv.h);
    for (u32 i = 0; i < n; i++) {
        const pip_msg_t *m = pip_chat_get(i);
        u16 st[CH_LINES], ln[CH_LINES]; i32 wd;
        u32 nl = wrap_text(m->text, maxbw - 2 * pad, st, ln, &wd);
        i32 bw = wd + 2 * pad, bh = (i32)nl * lhh + 2 * pad - 3;
        if (y + bh >= conv.y && y <= conv.y + conv.h) {
            i32 bx = m->pip ? conv.x + 8 : conv.x + conv.w - 8 - bw;
            u32 fill = m->pip ? (dark ? rgb(0x26, 0x2F, 0x52) : rgb(0xFF, 0xFF, 0xFF)) : g_theme->care;
            u32 ink = m->pip ? g_theme->ink : g_theme->on_care;
            gfx_rect_rounded(bx, y, bw, bh, 16, fill);
            for (u32 k = 0; k < nl; k++) {
                char line[PIP_MSG_LEN]; u32 L = ln[k] < sizeof line - 1 ? ln[k] : sizeof line - 1;
                kmemcpy(line, m->text + st[k], L); line[L] = '\0';
                gfx_str_role(bx + pad, y + pad - 1 + (i32)k * lhh, line, ink, FONT_ROLE_UI, 1, FONT_W_REGULAR);
            }
        }
        y += bh + gap;
    }
    if (pip_chat_busy()) {
        i32 bw = 56;
        gfx_rect_rounded(conv.x + 8, y, bw, 34, 17, dark ? rgb(0x26, 0x2F, 0x52) : rgb(0xFF, 0xFF, 0xFF));
        typing_dots(conv.x + 8 + 8, y);
    }
    gfx_clear_clip();
    if (maxoff > 0) {
        i32 th = conv.h * conv.h / total; if (th < 28) th = 28;
        i32 ty = conv.y + (conv.h - th) * off / maxoff;
        gfx_rect_rounded_blend(conv.x + conv.w - 5, ty, 4, th, 2, g_theme->ink, 60);
    }
    /* quick actions */
    i32 cx = chips_r.x, cy = chips_r.y;
    for (u32 i = 0; i < pip_quick_count(); i++) {
        const char *lab = pip_quick_label(i);
        i32 cw = gfx_str_role_width(lab, FONT_ROLE_UI, 0, FONT_W_BOLD) + 22;
        if (cx + cw > chips_r.x + chips_r.w) { cx = chips_r.x; cy += 36; }
        if (cy + 30 > chips_r.y + chips_r.h) break;
        gfx_rect_rounded(cx, cy, cw, 30, 15, dark ? rgb(0x26, 0x2D, 0x4F) : rgb(0xFF, 0xFF, 0xFF));
        gfx_rect_rounded_outline(cx, cy, cw, 30, 15, dark ? rgb(0x3A, 0x44, 0x6E) : rgb(0xE1, 0xE4, 0xEE));
        gfx_str_role(cx + 11, cy + (30 - lh(0)) / 2, lab, g_theme->ink, FONT_ROLE_UI, 0, FONT_W_BOLD);
        hit(s, rect_make(cx, cy, cw, 30), HA_CHIP, (i32)i);
        cx += cw + 6;
    }
    /* input */
    gfx_rect_rounded(in_r.x, in_r.y, in_r.w, in_r.h, in_r.h / 2, dark ? rgb(0x10, 0x15, 0x29) : rgb(0xFF, 0xFF, 0xFF));
    bool focus = !s->name_focus;
    gfx_rect_rounded_outline(in_r.x, in_r.y, in_r.w, in_r.h, in_r.h / 2, focus ? g_theme->care : (dark ? rgb(0x26, 0x2D, 0x48) : rgb(0xE1, 0xE4, 0xEE)));
    i32 sendd = in_r.h - 10, tx = in_r.x + 18, tw = in_r.w - 18 - sendd - 14;
    i32 ty = in_r.y + (in_r.h - lh(2)) / 2;
    if (s->in_len) {
        i32 iw = gfx_str_role_width(s->input, FONT_ROLE_UI, 2, FONT_W_REGULAR);
        i32 sx = iw > tw ? iw - tw : 0;                        /* keep the end of the line in view */
        gfx_set_clip(tx, in_r.y, tw, in_r.h);
        gfx_str_role(tx - sx, ty, s->input, g_theme->ink, FONT_ROLE_UI, 2, FONT_W_REGULAR);
        gfx_clear_clip();
        if (focus && ((timer_ms() / 500) & 1) == 0) gfx_rect(tx + (iw > tw ? tw : iw) + 1, ty, 2, lh(2), g_theme->care);
    } else {
        gfx_str_role_fit(tx, ty, tw, "Ask me anything...", g_theme->muted, FONT_ROLE_UI, 2, FONT_W_REGULAR);
        if (focus && ((timer_ms() / 500) & 1) == 0) gfx_rect(tx, ty, 2, lh(2), g_theme->care);
    }
    rect_t sb = rect_make(in_r.x + in_r.w - 5 - sendd, in_r.y + 5, sendd, sendd);
    gfx_circle_fill(sb.x + sendd / 2, sb.y + sendd / 2, sendd / 2, s->in_len ? g_theme->care : (dark ? rgb(0x2E, 0x37, 0x57) : rgb(0xDD, 0xE0, 0xEC)));
    ui_glyph(UI_GLYPH_ARROW_R, sb.x + (sendd - 18) / 2, sb.y + (sendd - 18) / 2, 18, s->in_len ? g_theme->on_care : g_theme->muted);
    hit(s, rect_make(in_r.x, in_r.y, in_r.w - sendd - 8, in_r.h), HA_INPUT, 0);
    hit(s, sb, HA_SEND, 0);
    (void)b;
}

void app_pip_draw(window_t *w) {
    rect_t cr = wm_client_rect(w);
    hstate_t *s = hst(w);
    if (!s) return;
    s->nhits = 0;
    pip_brain_t *b = pip_brain();
    i32 aw = cr.w >= 1100 ? 420 : 340;
    rect_t room = rect_make(cr.x, cr.y, cr.w - aw, cr.h);
    /* the room: sky gradient, window, floor */
    gfx_gradient_rect(room.x, room.y, room.w, room.h * 3 / 4, rgb(0x24, 0x2C, 0x5A), rgb(0x3B, 0x2F, 0x6E));
    gfx_rect(room.x, room.y + room.h * 3 / 4, room.w, room.h - room.h * 3 / 4, rgb(0x2A, 0x22, 0x4A));
    gfx_rect_rounded(room.x + room.w - 220, room.y + 40, 150, 120, 14, rgb(0x16, 0x1C, 0x40));
    gfx_rect_rounded_outline(room.x + room.w - 220, room.y + 40, 150, 120, 14, rgb(0x5A, 0x63, 0x95));
    gfx_circle_fill(room.x + room.w - 120, room.y + 80, 14, rgb(0xFF, 0xE0, 0x8A));
    /* name card */
    pip_draw(room.x + 24, room.y + 20, 70, PM_HAPPY, true);
    char age[48]; kutoa(pip_brain_age_days(b, pip_today()), age, 10);
    char line[64]; kstrcpy(line, b->name); kstrcat(line, ", age "); kstrcat(line, age); kstrcat(line, pip_brain_age_days(b, pip_today()) == 1 ? " day" : " days");
    gfx_str_role(room.x + 104, room.y + 44, line, rgb(0xEE, 0xF1, 0xFA), FONT_ROLE_DISPLAY, 1, FONT_W_BOLD);
    if (!pip_saved()) {
        char gs[72]; kstrcpy(gs, "Guest session -- "); kstrcat(gs, b->name); kstrcat(gs, " won't remember this visit");
        gfx_str_role(room.x + 104, room.y + 44 + gfx_role_line_h(FONT_ROLE_DISPLAY, 1), gs, rgb(0xFF, 0xC2, 0x4B), FONT_ROLE_UI, 0, FONT_W_REGULAR);
    }
    /* stats panel geometry; big Pip stands just above it */
    const i32 ph = 252;
    i32 px = room.x + 24, py = room.y + room.h - ph - 20, pw = room.w - 48 < 520 ? room.w - 48 : 520;
    u32 mood = (s->preview_mood >= 0 && timer_ms() < s->preview_until) ? (u32)s->preview_mood : PM_COUNT;
    {
        i32 top = room.y + 70, bottom = py - 8;          /* clear of the name card and the panel */
        i32 sz = bottom - top; if (sz > 280) sz = 280; if (sz < 120) sz = 120;
        pip_draw(room.x + room.w / 2 - sz / 2, bottom - sz * 152 / 160, sz, mood, false);
    }
    ui_glass(0x9200, rect_make(px, py, pw, ph), 22, rgb(0x15, 0x1B, 0x2E), 200, 24, UI_GLASS_HAIRLINE);
    gfx_str_role(px + 18, py + 16, "Name", rgb(0x9A, 0xA3, 0xBD), FONT_ROLE_UI, 0, FONT_W_BOLD);
    rect_t nf = rect_make(px + 70, py + 8, 200, 34);
    gfx_rect_rounded(nf.x, nf.y, nf.w, nf.h, 10, rgb(0x10, 0x15, 0x29));
    if (s->name_focus) gfx_rect_rounded_outline(nf.x, nf.y, nf.w, nf.h, 10, g_theme->care);
    gfx_str_role(nf.x + 10, nf.y + (34 - lh(2)) / 2, b->name, rgb(0xEE, 0xF1, 0xFA), FONT_ROLE_UI, 2, FONT_W_BOLD);
    hit(s, nf, HA_NAME, 0);
    char hv[12], ev[12], lv[12];
    kutoa(b->happy, hv, 10); kstrcat(hv, "%");
    kutoa(b->energy, ev, 10); kstrcat(ev, "%");
    kstrcpy(lv, "Lv "); { char d[4]; kutoa(pip_brain_level(b), d, 10); kstrcat(lv, d); }
    i32 mw = (pw - 36 - 24) / 3;
    meter(px + 18, py + 56, mw, "Happiness", hv, b->happy, rgb(0xFF, 0x8E, 0xC4));
    meter(px + 18 + mw + 12, py + 56, mw, "Energy", ev, b->energy, rgb(0xFF, 0xC2, 0x4B));
    meter(px + 18 + 2 * (mw + 12), py + 56, mw, "Friendship", lv, (b->friend_pts % 40) * 100 / 40, rgb(0x3D, 0xD6, 0x8C));
    /* mood row */
    i32 cx = px + 18, cy = py + 104;
    for (u32 m = 0; m < PM_COUNT && cx < px + pw - 60; m++) {
        cx += chip(s, cx, cy, pip_mood_label(m), pip_mood_bulb(m), (u32)s->preview_mood == m && timer_ms() < s->preview_until, HA_MOOD, (i32)m) + 6;
        if (cx > px + pw - 120 && m + 1 < PM_COUNT) { cx = px + 18; cy += 40; }
    }
    /* actions */
    i32 ay = py + ph - 48, ax = px + 18;
    gfx_hline(px + 18, ay - 10, pw - 36, rgb(0x2E, 0x37, 0x57));
    ax += chip(s, ax, ay, "Pat", 0xFF8EC4, false, HA_PAT, 0) + 8;
    ax += chip(s, ax, ay, "Tuck in", 0xB7C1FF, false, HA_TUCK, 0) + 8;
    ax += chip(s, ax, ay, "Surprise me", 0xFFC24B, false, HA_SURPRISE, 0) + 8;
    char out[32]; kstrcpy(out, "Let "); kstrcat(out, b->name); kstrcat(out, " out");
    i32 ow = gfx_str_role_width(out, FONT_ROLE_UI, 2, FONT_W_BOLD) + 32;
    gfx_rect_rounded(room.x + room.w - 24 - ow, room.y + room.h - 64, ow, 42, 21, g_theme->care);
    gfx_str_role(room.x + room.w - 24 - ow + 16, room.y + room.h - 64 + (42 - lh(2)) / 2, out, g_theme->on_care, FONT_ROLE_UI, 2, FONT_W_BOLD);
    hit(s, rect_make(room.x + room.w - 24 - ow, room.y + room.h - 64, ow, 42), HA_OUT, 0);

    /* aside: chat / dress up */
    i32 ax0 = cr.x + cr.w - aw;
    gfx_rect(ax0, cr.y, aw, cr.h, g_theme->surface_alt);
    gfx_vline(ax0, cr.y, cr.h, g_theme->is_dark ? rgb(0x26, 0x2D, 0x48) : rgb(0xE1, 0xE4, 0xEE));
    if (s_req_aside >= 0) { s->aside = (u32)s_req_aside; s_req_aside = -1; }
    {
        static const char *const AL[2] = { "Chat", "Dress up" };
        i32 sx = ax0 + 16;
        for (u32 t = 0; t < 2; t++) {
            i32 tw = gfx_str_role_width(AL[t], FONT_ROLE_UI, 2, FONT_W_BOLD) + 28;
            if (s->aside == t) gfx_rect_rounded(sx, cr.y + 12, tw, 36, 18, g_theme->is_dark ? rgb(0x2A, 0x33, 0x58) : rgb(0xFF, 0xFF, 0xFF));
            gfx_str_role(sx + 14, cr.y + 12 + (36 - lh(2)) / 2, AL[t], s->aside == t ? g_theme->ink : g_theme->ink2, FONT_ROLE_UI, 2, FONT_W_BOLD);
            hit(s, rect_make(sx, cr.y + 12, tw, 36), HA_ASIDE, (i32)t);
            sx += tw + 4;
        }
    }
    if (s->aside == 0) {
        if (!b->enabled) {
            rect_t cd = rect_make(ax0 + 16, cr.y + 68, aw - 32, 170);
            gfx_rect_rounded(cd.x, cd.y, cd.w, cd.h, 18, g_theme->is_dark ? rgb(0x1A, 0x20, 0x38) : rgb(0xFF, 0xFF, 0xFF));
            char t1[40]; kstrcpy(t1, b->name); kstrcat(t1, " is turned off");
            gfx_str_role(cd.x + 18, cd.y + 16, t1, g_theme->ink, FONT_ROLE_DISPLAY, 1, FONT_W_BOLD);
            gfx_str_role_wrap(cd.x + 18, cd.y + 16 + gfx_role_line_h(FONT_ROLE_DISPLAY, 1) + 6, cd.w - 36,
                              "No chat, tips or desktop presence while he's off. Turn him back on here or in Settings > Pip.",
                              g_theme->ink2, FONT_ROLE_UI, 1, FONT_W_REGULAR, 2);
            i32 bw = gfx_str_role_width("Turn Pip on", FONT_ROLE_UI, 2, FONT_W_BOLD) + 36;
            gfx_rect_rounded(cd.x + 18, cd.y + cd.h - 56, bw, 40, 20, g_theme->care);
            gfx_str_role(cd.x + 18 + 18, cd.y + cd.h - 56 + (40 - lh(2)) / 2, "Turn Pip on", g_theme->on_care, FONT_ROLE_UI, 2, FONT_W_BOLD);
            hit(s, rect_make(cd.x + 18, cd.y + cd.h - 56, bw, 40), HA_ENABLE, 0);
        } else {
            i32 in_h = 48, pad = 16;
            rect_t in_r = rect_make(ax0 + pad, cr.y + cr.h - pad - in_h, aw - 2 * pad, in_h);
            i32 chip_rows = cr.h < 620 ? 2 : 3;
            rect_t chips_r = rect_make(ax0 + pad, in_r.y - 10 - chip_rows * 36 + 6, aw - 2 * pad, chip_rows * 36 - 6);
            rect_t conv = rect_make(ax0 + 4, cr.y + 64, aw - 8, chips_r.y - 10 - (cr.y + 64));
            draw_chat(w, s, conv, chips_r, in_r);
        }
        return;
    }
    char dt[40]; kstrcpy(dt, "Dress up "); kstrcat(dt, b->name);
    i32 top = cr.y + 52;
    gfx_str_role(ax0 + 20, top + 12, dt, g_theme->ink, FONT_ROLE_DISPLAY, 1, FONT_W_BOLD);
    gfx_str_role(ax0 + 20, top + 12 + gfx_role_line_h(FONT_ROLE_DISPLAY, 1), "Changes show up right away, everywhere.", g_theme->muted, FONT_ROLE_UI, 0, FONT_W_REGULAR);
    i32 tx = ax0 + 16, ty = top + 76;
    for (u32 t = 0; t < 5; t++) {
        i32 tw = gfx_str_role_width(TAB_L[t], FONT_ROLE_UI, 1, FONT_W_BOLD) + 20;
        if (s->tab == t) gfx_rect_rounded(tx, ty, tw, 32, 10, g_theme->is_dark ? rgb(0x2A, 0x33, 0x58) : rgb(0xFF, 0xFF, 0xFF));
        gfx_str_role(tx + 10, ty + (32 - lh(1)) / 2, TAB_L[t], s->tab == t ? g_theme->ink : g_theme->ink2, FONT_ROLE_UI, 1, FONT_W_BOLD);
        hit(s, rect_make(tx, ty, tw, 32), HA_TAB, (i32)t);
        tx += tw + 2;
    }
    i32 gy = ty + 44, tile = (aw - 32 - 16) / 3;
    {   /* shrink the tiles until every row fits above the toggles' heading */
        i32 rows = (i32)(tab_count(s->tab) + 2) / 3;
        i32 room_h = cr.y + cr.h - 3 * 50 - 50 - 14 - gy;
        i32 fit = rows ? room_h / rows - 26 : tile;
        if (fit < tile) tile = fit < 72 ? 72 : fit;
    }
    for (u32 i = 0; i < tab_count(s->tab); i++) {
        i32 ox = ax0 + 16 + (i32)(i % 3) * (tile + 8), oy = gy + (i32)(i / 3) * (tile + 26);
        pip_look_t l = b->look;
        *look_field(&l, s->tab) = (u8)i;
        bool on = *look_field(&b->look, s->tab) == i;
        gfx_rect_rounded(ox, oy, tile, tile + 18, 14, g_theme->is_dark ? rgb(0x1A, 0x20, 0x38) : rgb(0xFF, 0xFF, 0xFF));
        if (on) gfx_rect_rounded_outline(ox, oy, tile, tile + 18, 14, g_theme->care);
        pip_brain_t saved = *b;
        b->look = l;                                   /* preview this choice */
        pip_draw(ox + (tile - 64) / 2, oy + 4, 64, PM_IDLE, true);
        b->look = saved.look;
        gfx_str_role_fit(ox + 8, oy + tile - 6, tile - 16, opt_label(s->tab, i), g_theme->ink, FONT_ROLE_UI, 0, FONT_W_BOLD);
        hit(s, rect_make(ox, oy, tile, tile + 18), HA_OPT, (i32)i);
    }
    /* toggles */
    i32 tgy = cr.y + cr.h - 3 * 50 - 50;
    char wo[40]; kstrcpy(wo, "When "); kstrcat(wo, b->name); kstrcat(wo, " is out");
    gfx_str_role(ax0 + 20, tgy, wo, g_theme->muted, FONT_ROLE_UI, 0, FONT_W_BOLD);
    static const char *const TL[3] = { "Roam around", "Follow my cursor", "Nap when I'm away" };
    bool vals[3] = { b->roam, b->follow, b->nap };
    for (u32 i = 0; i < 3; i++) {
        i32 y = tgy + 24 + (i32)i * 50;
        gfx_str_role(ax0 + 20, y + (UI_TOGGLE_H - lh(2)) / 2, TL[i], g_theme->ink, FONT_ROLE_UI, 2, FONT_W_REGULAR);
        ui_toggle(ax0 + aw - 20 - UI_TOGGLE_W, y, vals[i], UI_NORMAL);
        hit(s, rect_make(ax0 + 12, y - 8, aw - 24, UI_TOGGLE_H + 16), HA_TOGGLE, (i32)i);
    }
}

void app_pip_click(window_t *w, i32 x, i32 y) {
    hstate_t *s = hst(w);
    if (!s) return;
    pip_brain_t *b = pip_brain();
    for (u32 k = s->nhits; k-- > 0;) {
        const hhit_t *h = &s->hits[k];
        if (!rect_contains(h->r, x, y)) continue;
        if (h->act != HA_NAME) s->name_focus = false;
        switch (h->act) {
        case HA_NAME: s->name_focus = true; break;
        case HA_MOOD: s->preview_mood = h->arg; s->preview_until = timer_ms() + 5000; break;
        case HA_PAT: pip_event(PE_PAT); break;
        case HA_TUCK: pip_set_mode(PIP_NAP); pip_say("Night night...", 2500); break;
        case HA_SURPRISE: {
            u32 r = timer_ms() * 2654435761u;
            pip_look_t l = { (u8)(r % PIP_COLORS), (u8)((r >> 5) % PIP_HATS), (u8)((r >> 10) % PIP_OUTFITS), (u8)((r >> 15) % PIP_ACCS), (u8)((r >> 20) % PIP_EFFECTS) };
            pip_set_look(l);
            pip_event(PE_WELCOME_BACK);
            break;
        }
        case HA_OUT: b->show = true; pip_mark_dirty(); pip_desktop_go_play(); wm_minimize(w); break;
        case HA_CHIP: pip_chat_submit(pip_quick_text((u32)h->arg)); break;
        case HA_INPUT: break;
        case HA_SEND: if (s->in_len) { pip_chat_submit(s->input); s->in_len = 0; s->input[0] = 0; } break;
        case HA_ASIDE: s->aside = (u32)h->arg; break;
        case HA_ENABLE:
            b->enabled = true; b->show = true; pip_mark_dirty(); pip_save_now();
            pip_event(PE_GREET); pip_chat_intro();
            break;
        case HA_TAB: s->tab = (u32)h->arg; break;
        case HA_OPT: { pip_look_t l = b->look; *look_field(&l, s->tab) = (u8)h->arg; pip_set_look(l); break; }
        case HA_TOGGLE:
            if (h->arg == 0) b->roam = !b->roam; else if (h->arg == 1) b->follow = !b->follow; else b->nap = !b->nap;
            pip_mark_dirty();
            break;
        default: break;
        }
        return;
    }
    s->name_focus = false;
}

void app_pip_key(window_t *w, char c) {
    hstate_t *s = hst(w);
    if (!s) return;
    pip_brain_t *b = pip_brain();
    if (!s->name_focus) {                                   /* typing goes to the chat box */
        if (s->aside != 0 || !b->enabled) return;
        if (c == '\n' || c == '\r') { if (s->in_len) { pip_chat_submit(s->input); s->in_len = 0; s->input[0] = '\0'; } return; }
        if (c == 0x1B) { s->in_len = 0; s->input[0] = '\0'; return; }
        if (c == '\b') { if (s->in_len) s->input[--s->in_len] = '\0'; return; }
        if (c >= 32 && c < 127 && s->in_len < sizeof s->input - 1) { s->input[s->in_len++] = c; s->input[s->in_len] = '\0'; }
        return;
    }
    u32 n = (u32)kstrlen(b->name);
    if (c == '\n' || c == 0x1B) { s->name_focus = false; if (!b->name[0]) kstrcpy(b->name, "Pip"); pip_mark_dirty(); return; }
    if (c == '\b') { if (n) b->name[n - 1] = '\0'; pip_mark_dirty(); return; }
    if (c >= 32 && c < 127 && n < 14) { b->name[n] = c; b->name[n + 1] = '\0'; pip_mark_dirty(); }
}
