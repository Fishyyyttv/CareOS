/* CareOS v9 -- apps/app_pip.c -- Pip's Home (Phase 4 spec §5).
 * The room with a big live Pip, his name, real stats, a mood row, Pat /
 * Tuck in / Surprise me, "Let <name> out", and a dress-up aside whose tiles
 * preview each choice. Every change applies everywhere at once. */
#include "apps_common.h"
#include "ui.h"
#include "../gui/pip/pip.h"

enum { HA_NONE = 0, HA_NAME, HA_MOOD, HA_PAT, HA_TUCK, HA_SURPRISE, HA_OUT, HA_TAB, HA_OPT, HA_TOGGLE };
typedef struct { rect_t r; u8 act; i32 arg; } hhit_t;
typedef struct {
    hhit_t hits[80]; u32 nhits;
    u32 tab;                       /* 0 color, 1 hat, 2 outfit, 3 acc, 4 effect */
    bool name_focus;
    i32 preview_mood; u32 preview_until;
} hstate_t;

static hstate_t *hst(window_t *w) {
    if (!w->app_state) {
        hstate_t *s = (hstate_t *)kmalloc(sizeof(hstate_t));
        if (!s) return NULL;
        kmemset(s, 0, sizeof *s);
        s->tab = 1; s->preview_mood = -1;
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

void app_pip_init(window_t *w) { (void)hst(w); }

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

void app_pip_draw(window_t *w) {
    rect_t cr = wm_client_rect(w);
    hstate_t *s = hst(w);
    if (!s) return;
    s->nhits = 0;
    pip_brain_t *b = pip_brain();
    i32 aw = cr.w >= 1100 ? 360 : 300;
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

    /* aside: dress up */
    i32 ax0 = cr.x + cr.w - aw;
    gfx_rect(ax0, cr.y, aw, cr.h, g_theme->surface_alt);
    gfx_vline(ax0, cr.y, cr.h, g_theme->is_dark ? rgb(0x26, 0x2D, 0x48) : rgb(0xE1, 0xE4, 0xEE));
    char dt[40]; kstrcpy(dt, "Dress up "); kstrcat(dt, b->name);
    gfx_str_role(ax0 + 20, cr.y + 20, dt, g_theme->ink, FONT_ROLE_DISPLAY, 1, FONT_W_BOLD);
    gfx_str_role(ax0 + 20, cr.y + 20 + gfx_role_line_h(FONT_ROLE_DISPLAY, 1), "Changes show up right away, everywhere.", g_theme->muted, FONT_ROLE_UI, 0, FONT_W_REGULAR);
    i32 tx = ax0 + 16, ty = cr.y + 84;
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
        case HA_OUT: pip_desktop_go_play(); wm_minimize(w); break;
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
    if (!s || !s->name_focus) return;
    pip_brain_t *b = pip_brain();
    u32 n = (u32)kstrlen(b->name);
    if (c == '\n' || c == 0x1B) { s->name_focus = false; if (!b->name[0]) kstrcpy(b->name, "Pip"); pip_mark_dirty(); return; }
    if (c == '\b') { if (n) b->name[n - 1] = '\0'; pip_mark_dirty(); return; }
    if (c >= 32 && c < 127 && n < 14) { b->name[n] = c; b->name[n + 1] = '\0'; pip_mark_dirty(); }
}
