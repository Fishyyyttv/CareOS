/* gui/pip/pip_desktop.c -- Pip on the desktop (spec §4): perched by the
 * dock, roaming along the dock edge and window tops, napping on the dock.
 * Click = open his Home to chat; right-click = menu (chat, pat, ...); while hovered a
 * status pill shows. Only Pip's own rectangles are damaged. */
#include "pip.h"
#include "ui.h"
#include "spring.h"

#define PERCH_SIZE 120
#define NAP_SIZE   110

typedef enum { DM_NONE = 0, DM_PAT, DM_THINK, DM_PLAY, DM_DRESS } dmenu_t;

static i32  g_x, g_y;                 /* design-box top-left */
static i32  g_tx, g_ty;               /* walk target */
static u32  g_roam_until, g_next_hop;
static bool g_menu;
static i32  g_menu_x, g_menu_y;
static rect_t g_last;                 /* last drawn area, for damage */
static bool g_hover;
static i32  g_presence;               /* 0 = off screen .. 256 = standing by the dock */

static bool wants_visible(void) { const pip_brain_t *b = pip_brain(); return b->enabled && b->show; }
void pip_desktop_enter(void) { g_presence = 0; g_x = g_y = 0; }
u32 pip_desktop_frame_ms(void) {
    const pip_brain_t *b = pip_brain();
    bool moving = g_presence != (wants_visible() ? 256 : 0);
    if (!wants_visible() && g_presence == 0) return 0;
    return b->anim ? 83u : (moving || pip_bubble_text() ? 250u : 500u);
}

extern bool taskbar_contains(i32 x, i32 y);

static rect_t dock_rect(void) {
    /* The dock is centred at the bottom: scan its extent from taskbar_contains. */
    i32 y = (i32)SCREEN_H - 40, x0 = (i32)SCREEN_W / 2, x1 = x0;
    while (x0 > 0 && taskbar_contains(x0 - 4, y)) x0 -= 4;
    while (x1 < (i32)SCREEN_W && taskbar_contains(x1 + 4, y)) x1 += 4;
    i32 top = y;
    while (top > 0 && taskbar_contains(x0 + 20, top - 2)) top -= 2;
    return rect_make(x0, top, x1 - x0, (i32)SCREEN_H - top);
}

static u32 cur_size(void) { return pip_brain()->mode == PIP_NAP ? NAP_SIZE : PERCH_SIZE; }

static void home_spot(i32 *x, i32 *y) {
    rect_t d = dock_rect();
    u32 s = cur_size();
    if (pip_brain()->mode == PIP_NAP) { *x = d.x + d.w - (i32)s - 10; *y = d.y - (i32)s * 120 / 160; }
    else { *x = d.x + d.w + 8; *y = d.y + d.h - 16 - (i32)s * 152 / 160; }  /* feet just above the screen edge */
}

bool pip_desktop_hidden(void) {
    window_t *f = wm_focused();
    return f && (f->app == APP_DOOM || f->app == APP_3D || f->app == APP_MAZE) && f->maximized;
}

void pip_desktop_go_play(void) {
    pip_set_mode(PIP_ROAM);
#ifdef CAREOS_PERF_BENCH_AT_BOOT
    g_roam_until = timer_ms() + 600000;   /* the benchmark measures a roaming Pip */
#else
    g_roam_until = timer_ms() + 40000;
#endif
    g_next_hop = 0;
}
void pip_desktop_come_back(void) { pip_set_mode(PIP_PERCH); }

/* Pick the next place to walk to: along the dock top, or onto a window top. */
static void pick_target(void) {
    rect_t d = dock_rect();
    u32 r = timer_ms() * 2654435761u;
    if ((r >> 28) & 1) {

        for (u32 tries = 0; tries < 8; tries++) {
            window_t *w = wm_get_window((int)((r >> 4) % MAX_WINDOWS));
            r = r * 1103515245u + 12345u;
            if (!w->active || w->minimized || w->rect.w < 200 || !wm_on_current_desktop(w)) continue;
            g_tx = w->rect.x + 20 + (i32)((r >> 8) % (u32)(w->rect.w - 160));
            g_ty = w->rect.y - (i32)PERCH_SIZE * 150 / 160 + 4;
            if (g_ty < 48) continue;
            return;
        }
    }
    g_tx = d.x + (i32)((r >> 8) % (u32)(d.w > 120 ? d.w - 120 : 1));
    g_ty = d.y - (i32)PERCH_SIZE * 150 / 160 + 6;
}

static void step_motion_once(void);
/* Pip moves on a fixed 12 Hz clock, so his speed doesn't follow the frame rate. */
static void step_motion(void) {
    static u32 s_last;
    u32 now = timer_ms();
    if (!s_last || now - s_last > 1000) s_last = now - 83;   /* first frame, or after a long stall */
    u32 n = (now - s_last) / 83;
    s_last += n * 83;
    while (n--) step_motion_once();
}

static void step_motion_once(void) {
    pip_brain_t *b = pip_brain();
    i32 tgt = wants_visible() ? 256 : 0;                       /* enter / leave the desktop */
    if (!b->anim) g_presence = tgt;
    else if (g_presence < tgt) g_presence = g_presence + 32 > tgt ? tgt : g_presence + 32;
    else if (g_presence > tgt) g_presence = g_presence - 32 < tgt ? tgt : g_presence - 32;
    if (!b->anim && b->mode == PIP_ROAM) pip_desktop_come_back();   /* reduced animation: no wandering */
    if (b->mode == PIP_ROAM) {
#ifdef CAREOS_PERF_BENCH_AT_BOOT
        if (timer_ms() > g_roam_until) { pip_desktop_come_back(); return; }
#else
        if (timer_ms() > g_roam_until || b->energy < 30 || !b->roam) { pip_desktop_come_back(); return; }
#endif
        if (timer_ms() > g_next_hop) { pick_target(); g_next_hop = timer_ms() + 3500; }
        i32 dx = g_tx - g_x, dy = g_ty - g_y;
        g_x += dx > 3 ? 3 : dx < -3 ? -3 : dx;
        g_y += dy > 6 ? 6 : dy < -6 ? -6 : dy;
    } else {
        i32 hx, hy; home_spot(&hx, &hy);
        g_x += (hx - g_x) / 4 + ((hx - g_x) > 0) - ((hx - g_x) < 0);
        g_y += (hy - g_y) / 4 + ((hy - g_y) > 0) - ((hy - g_y) < 0);
    }
}

static rect_t pip_rect(void) {
    u32 s = cur_size();
    return rect_make(g_x + (i32)s * 20 / 160, g_y + (i32)s * 30 / 160, (i32)s * 120 / 160, (i32)s * 120 / 160);
}

static void draw_bubble(const char *t, i32 x, i32 y) {
    i32 w = gfx_str_role_width(t, FONT_ROLE_UI, 1, FONT_W_BOLD) + 28, h = 36;
    i32 bx = x - w / 2, by = y - h;
    if (bx < 8) bx = 8;
    if (bx + w > (i32)SCREEN_W - 8) bx = (i32)SCREEN_W - 8 - w;
    if (by < 48) by = 48;
    gfx_shadow_soft(bx, by, w, h, 18);
    gfx_rect_rounded(bx, by, w, h, 18, rgb(0xFF, 0xFF, 0xFF));
    gfx_str_role(bx + 14, by + (h - gfx_role_line_h(FONT_ROLE_UI, 1)) / 2, t, rgb(0x1A, 0x16, 0x30), FONT_ROLE_UI, 1, FONT_W_BOLD);
}

static const char *status_text(void) {
    switch (pip_mood()) {
    case PM_PLAYING:  return "Playing on your desktop";
    case PM_WATCHING: return "Watching you work";
    case PM_HAPPY:    return "Feeling loved";
    case PM_THINKING: return "Thinking";
    case PM_SLEEPING: return pip_brain()->mode == PIP_NAP ? "Napping on the dock" : "Sleeping";
    case PM_EXCITED:  return "Super excited";
    case PM_SAD:      return "A bit down";
    case PM_CONFUSED: return "Confused";
    default:          return "Chilling";
    }
}

#define MENU_N 5
static const char *const MENU_L[MENU_N] = { "Chat with me", "Give a pat", "Ask what I am thinking", "Go play", "Dress up..." };
static rect_t menu_item(u32 i) { return rect_make(g_menu_x, g_menu_y + 8 + (i32)i * 38, 240, 38); }

void pip_desktop_draw(void) {
    if (pip_desktop_hidden()) return;
    if (!wants_visible() && g_presence == 0) {                  /* turned off or hidden: draw nothing */
        if (g_last.w) { ui_damage(g_last); g_last = rect_make(0, 0, 0, 0); }
        g_menu = false; g_hover = false;
        return;
    }
    if (!g_x && !g_y) home_spot(&g_x, &g_y);
    step_motion();
    if (!wants_visible()) { g_menu = false; g_hover = false; }
    /* sliding in or out: ease from below the screen edge */
    i32 slide_save = g_y;
    if (g_presence < 256) {
        i32 p = g_presence, e = (p * p / 256) * (768 - 2 * p) / 256;
        g_y += (256 - e) * ((i32)SCREEN_H - g_y) / 256;
    }
    rect_t pr = pip_rect();
    pip_set_anchor(pr.x + pr.w / 2, pr.y + pr.h / 3);
    u32 s = cur_size();
    pip_draw(g_x, g_y, s, PM_COUNT, false);
    rect_t area = rect_make(g_x - (i32)s * 40 / 160, g_y - (i32)s * 24 / 160 - 40, (i32)s * 240 / 160, (i32)s * 200 / 160 + 40);
    const char *bt = pip_bubble_text();
    if (bt) draw_bubble(bt, pr.x + pr.w / 2, pr.y - 6);
    if (g_hover && !g_menu) {                                 /* status pill */
        i32 pw = 300, ph = 64, px = pr.x + pr.w / 2 - pw / 2, py = pr.y + pr.h + 6;
        if (py + ph > (i32)SCREEN_H) py = pr.y - ph - 50;
        if (px < 8) px = 8;
        if (px + pw > (i32)SCREEN_W - 8) px = (i32)SCREEN_W - 8 - pw;
        ui_glass(0x9100, rect_make(px, py, pw, ph), 18, g_theme->panel_tint, g_theme->panel_alpha, 24, UI_GLASS_HAIRLINE | UI_GLASS_SHADOW_S);
        gfx_str_role(px + 14, py + 10, pip_brain()->name, g_theme->ink, FONT_ROLE_UI, 2, FONT_W_BOLD);
        { const char *hint = "Click to chat"; gfx_str_role(px + pw - 14 - gfx_str_role_width(hint, FONT_ROLE_UI, 0, FONT_W_REGULAR), py + 14, hint, g_theme->muted, FONT_ROLE_UI, 0, FONT_W_REGULAR); }
        gfx_circle_fill(px + 18, py + 42, 4, pip_mood_bulb(pip_mood()));
        gfx_str_role_fit(px + 28, py + 34, pw - 40, status_text(), g_theme->ink2, FONT_ROLE_UI, 0, FONT_W_REGULAR);
        area = rect_make(px < area.x ? px : area.x, area.y, (px + pw > area.x + area.w ? px + pw : area.x + area.w) - (px < area.x ? px : area.x), (py + ph > area.y + area.h ? py + ph : area.y + area.h) - area.y);
    }
    if (g_menu) {
        i32 mh = 8 + MENU_N * 38 + 8;
        gfx_shadow_soft(g_menu_x, g_menu_y, 240, mh, 16);
        gfx_rect_rounded(g_menu_x, g_menu_y, 240, mh, 16, g_theme->is_dark ? rgb(0x1C, 0x24, 0x40) : rgb(0xFF, 0xFF, 0xFF));
        for (u32 i = 0; i < MENU_N; i++) {
            rect_t r = menu_item(i);
            const char *l = (i == 3 && pip_brain()->mode == PIP_ROAM) ? "Come back" : MENU_L[i];
            gfx_str_role(r.x + 16, r.y + (38 - gfx_role_line_h(FONT_ROLE_UI, 2)) / 2, l, g_theme->ink, FONT_ROLE_UI, 2, FONT_W_REGULAR);
        }
        ui_damage(rect_make(g_menu_x - 20, g_menu_y - 20, 280, mh + 40));
    }
    ui_damage(g_last);
    ui_damage(area);
    g_last = area;
    g_y = slide_save;
}

static void think_line(void) {
    char t[64]; u32 n = 0;

    for (u32 i = 0; i < MAX_WINDOWS; i++) { window_t *ww = wm_get_window((int)i); if (ww && ww->active && !ww->minimized && wm_on_current_desktop(ww)) n++; }
    pip_brain_t *b = pip_brain();
    u32 sel = timer_ms() / 1000 % 3;
    if (b->energy < 30) kstrcpy(t, "Energy's low... a nap soon?");
    else if (sel == 0) { char d[8]; kutoa(n, d, 10); kstrcpy(t, "You've got "); kstrcat(t, d); kstrcat(t, n == 1 ? " window open." : " windows open."); }
    else if (sel == 1) { char d[8]; kutoa(timer_ms() / 60000, d, 10); kstrcpy(t, "We've been up "); kstrcat(t, d); kstrcat(t, " minutes."); }
    else { char d[8]; kutoa(b->happy, d, 10); kstrcpy(t, "Happiness: "); kstrcat(t, d); kstrcat(t, "%. Pat me?"); }
    pip_say(t, 3500);
    pip_event(PE_LOADING);
}

bool pip_desktop_mouse(mouse_t *m) {
    if (!pip_brain()->enabled || !pip_brain()->show || g_presence < 256 || pip_desktop_hidden()) { g_hover = false; g_menu = false; return false; }
    rect_t pr = pip_rect();
    g_hover = rect_contains(pr, m->x, m->y);
    if (g_menu && (m->left_clicked || m->right_clicked)) {
        for (u32 i = 0; i < MENU_N; i++) {
            if (!rect_contains(menu_item(i), m->x, m->y)) continue;
            g_menu = false;
            if (i == 0) pip_open_home();
            else if (i == 1) pip_event(PE_PAT);
            else if (i == 2) think_line();
            else if (i == 3) { if (pip_brain()->mode == PIP_ROAM) pip_desktop_come_back(); else pip_desktop_go_play(); }
            else { pip_open_home(); app_pip_request_aside(1); }
            return true;
        }
        g_menu = false;
        return rect_contains(pr, m->x, m->y);
    }
    if (!g_hover) return false;
    if (m->right_clicked) { g_menu = true; g_menu_x = m->x > (i32)SCREEN_W - 248 ? (i32)SCREEN_W - 248 : m->x; g_menu_y = m->y - 8 - MENU_N * 38 - 8; if (g_menu_y < 48) g_menu_y = m->y; return true; }
    if (m->left_clicked) {                                            /* click: open his Home to chat */
        if (pip_brain()->mode == PIP_NAP) pip_set_mode(PIP_PERCH);   /* wake */
        pip_open_home();
        return true;
    }
    return m->left || m->left_released;                              /* own the press */
}
