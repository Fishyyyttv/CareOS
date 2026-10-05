/* CareOS gui/notify.c -- notifications (visual-system spec §12).
 *
 * One store feeds both views: toasts (top-right, 5 s, stacked) and the
 * Notification Center column (history, "Clear all"). A notification carries
 * the sending app (for its icon and name), a title, a body and up to two
 * action buttons. Do Not Disturb keeps notifications in the history but
 * suppresses their toasts. Glide-in motion arrives with phase 5. */
#include "pip/pip.h"
#include "kernel.h"
#include "gui.h"
#include "ui.h"

#define NOTE_MAX    24
#define TOAST_MAX      3
#define CARD_W       360
#define CARD_PAD      16
#define CARD_R        22
#define ACTION_H      40

typedef struct {
    bool     active;
    bool     toast;            /* still showing as a toast */
    app_id_t app;
    char     title[48];
    char     body[160];
    char     act[2][20];
    void   (*on_action)(u32 which);
    u32      born_ms;
    u32      seq;              /* newest first */
} note_t;

static note_t notes[NOTE_MAX];
static u32    note_seq = 0;
static bool   s_dnd = false;
static bool   s_center_visible = false;   /* toasts hide while the center shows */

static const char *app_label(app_id_t app) {
    switch (app) {
    case APP_TERMINAL: return "Terminal";  case APP_NOTES:    return "Notes";
    case APP_FILES:    return "Files";     case APP_SYSMON:   return "Monitor";
    case APP_CALC:     return "Calculator";case APP_ABOUT:    return "About";
    case APP_HELP:     return "Help";      case APP_BROWSER:  return "Browser";
    case APP_SETTINGS: return "Settings";  case APP_PKGMGR:   return "Packages";
    case APP_EDITOR:   return "Editor";    case APP_PAINT:    return "Paint";
    case APP_CLOCK:    return "Clock";     case APP_NETMON:   return "NetMon";
    case APP_USERS:    return "Users";     case APP_MAZE:     return "Maze";
    case APP_3D:       return "Wolf3D";    case APP_DOOM:     return "Doom";
    case APP_PIP:      return "Pip";
    default:           return "CareOS";
    }
}

static void copy(char *dst, const char *src, u32 max) {
    u32 i = 0;
    if (src) for (; src[i] && i + 1 < max; i++) dst[i] = src[i];
    dst[i] = '\0';
}

/* ---- Store ----------------------------------------------------------------- */
void notify_push_ex(app_id_t app, const char *title, const char *body,
                    const char *action0, const char *action1, void (*on_action)(u32 which)) {
    note_t *n = NULL;
    for (u32 i = 0; i < NOTE_MAX; i++) if (!notes[i].active) { n = &notes[i]; break; }
    if (!n) {                                  /* full: drop the oldest */
        n = &notes[0];
        for (u32 i = 1; i < NOTE_MAX; i++) if (notes[i].seq < n->seq) n = &notes[i];
    }
    kmemset(n, 0, sizeof(*n));
    n->active = true;
    n->toast = !s_dnd;
    n->app = app;
    copy(n->title, title, sizeof(n->title));
    copy(n->body, body, sizeof(n->body));
    copy(n->act[0], action0, sizeof(n->act[0]));
    copy(n->act[1], action1, sizeof(n->act[1]));
    n->on_action = on_action;
    n->born_ms = timer_ms();
    n->seq = ++note_seq;
}

/* Legacy entry point: CareOS-wide notifications without an app or actions. */
void notify_push(const char *title, const char *body, u32 icon_color) {
    if (icon_color == g_theme->error) pip_event(PE_CONFUSED);   /* an error: Pip is puzzled */
    notify_push_ex(APP_NONE, title, body, NULL, NULL, NULL);
}

void notify_set_dnd(bool on) { s_dnd = on; if (on) for (u32 i = 0; i < NOTE_MAX; i++) notes[i].toast = false; }
bool notify_dnd(void) { return s_dnd; }

u32 notify_count(void) {
    u32 c = 0;
    for (u32 i = 0; i < NOTE_MAX; i++) if (notes[i].active) c++;
    return c;
}

void notify_clear_all(void) { kmemset(notes, 0, sizeof(notes)); }

void notify_tick(void) {
    u32 now = timer_ms();
    for (u32 i = 0; i < NOTE_MAX; i++)
        if (notes[i].toast && now - notes[i].born_ms > NOTIFY_LIFE_MS) notes[i].toast = false;
}

bool notify_active(void) {
    if (s_center_visible) return false;
    for (u32 i = 0; i < NOTE_MAX; i++) if (notes[i].toast) return true;
    return false;
}

/* Up to `max` active notes, newest first; toasts_only limits to live toasts. */
static u32 ordered(note_t **out, u32 max, bool toasts_only) {
    note_t *all[NOTE_MAX];
    u32 n = 0;
    for (u32 i = 0; i < NOTE_MAX; i++)
        if (notes[i].active && (!toasts_only || notes[i].toast)) all[n++] = &notes[i];
    for (u32 i = 0; i < n; i++)
        for (u32 j = i + 1; j < n; j++)
            if (all[j]->seq > all[i]->seq) { note_t *t = all[i]; all[i] = all[j]; all[j] = t; }
    if (n > max) n = max;
    for (u32 i = 0; i < n; i++) out[i] = all[i];
    return n;
}

/* ---- Card layout + drawing -------------------------------------------------- */
static void when(u32 born, char *buf) {
    u32 s = (timer_ms() - born) / 1000u;
    if (s < 60) kstrcpy(buf, "now");
    else if (s < 3600) ksprintf(buf, "%u min ago", s / 60u);
    else ksprintf(buf, "%u hr ago", s / 3600u);
}

/* Split body into at most two lines that fit w; the second ends in "..." when cut. */
static u32 wrap_body(const char *s, i32 w, char out[2][96]) {
    out[0][0] = out[1][0] = '\0';
    if (!s || !s[0]) return 0;
    u32 len = (u32)kstrlen(s), cut = 0, last_space = 0;
    while (cut < len && cut < 95) {
        if (gfx_str_role_width_n(s, cut + 1, FONT_ROLE_UI, 1, FONT_W_REGULAR) > w) break;
        if (s[cut] == ' ') last_space = cut;
        cut++;
    }
    if (cut >= len) { copy(out[0], s, 96); return 1; }
    if (last_space > 0) cut = last_space;
    kmemcpy(out[0], s, cut);
    out[0][cut] = '\0';
    const char *rest = s + cut;
    while (*rest == ' ') rest++;
    copy(out[1], rest, 96);
    return 2;
}

static i32 card_h(const note_t *n) {
    char lines[2][96];
    u32 nl = wrap_body(n->body, CARD_W - CARD_PAD * 2 - 52, lines);
    i32 text = gfx_role_line_h(FONT_ROLE_UI, 0) + 2 + gfx_role_line_h(FONT_ROLE_UI, 2)
             + (i32)nl * gfx_role_line_h(FONT_ROLE_UI, 1);
    if (text < 40) text = 40;
    return CARD_PAD + text + (n->act[0][0] ? 12 + ACTION_H : 0) + CARD_PAD;
}

static rect_t action_rect(rect_t card, u32 which, bool two) {
    i32 w = card.w - CARD_PAD * 2, y = card.y + card.h - CARD_PAD - ACTION_H;
    if (!two) return rect_make(card.x + CARD_PAD, y, w, ACTION_H);
    i32 half = (w - 8) / 2;
    return rect_make(card.x + CARD_PAD + (i32)which * (half + 8), y, half, ACTION_H);
}

static void draw_card(const note_t *n, rect_t r, const mouse_t *m) {
    ui_glass(40, r, CARD_R, g_theme->panel_tint, g_theme->is_dark ? 209 : 224, 30,
             UI_GLASS_HAIRLINE | UI_GLASS_SHADOW_M);
    ui_app_icon(ui_icon_for_app(n->app), r.x + CARD_PAD, r.y + CARD_PAD, 40);
    i32 tx = r.x + CARD_PAD + 52, tw = r.w - CARD_PAD * 2 - 52, y = r.y + CARD_PAD;

    char t[16];
    when(n->born_ms, t);
    gfx_str_role(tx, y, app_label(n->app), g_theme->muted, FONT_ROLE_UI, 0, FONT_W_BOLD);
    i32 ww = gfx_str_role_width(t, FONT_ROLE_UI, 0, FONT_W_REGULAR);
    gfx_str_role(tx + tw - ww, y, t, g_theme->muted, FONT_ROLE_UI, 0, FONT_W_REGULAR);
    y += gfx_role_line_h(FONT_ROLE_UI, 0) + 2;
    gfx_str_role_fit(tx, y, tw, n->title, g_theme->ink, FONT_ROLE_UI, 2, FONT_W_BOLD);
    y += gfx_role_line_h(FONT_ROLE_UI, 2);
    char lines[2][96];
    u32 nl = wrap_body(n->body, tw, lines);
    for (u32 k = 0; k < nl; k++) {
        gfx_str_role_fit(tx, y, tw, lines[k], g_theme->ink2, FONT_ROLE_UI, 1, FONT_W_REGULAR);
        y += gfx_role_line_h(FONT_ROLE_UI, 1);
    }

    bool two = n->act[1][0] != '\0';
    for (u32 a = 0; n->act[0][0] && a < (two ? 2u : 1u); a++) {
        rect_t b = action_rect(r, a, two);
        ui_button(b, n->act[a], a == 0 ? UI_BTN_PRIMARY : UI_BTN_SECONDARY,
                  ui_state_for(b, m, false, false));
    }
}

/* Click on a card: an action runs its callback; anywhere else dismisses it. */
static bool click_card(note_t *n, rect_t r, const mouse_t *m) {
    if (!rect_contains(r, m->x, m->y)) return false;
    bool two = n->act[1][0] != '\0';
    for (u32 a = 0; n->act[0][0] && a < (two ? 2u : 1u); a++) {
        if (!rect_contains(action_rect(r, a, two), m->x, m->y)) continue;
        void (*cb)(u32) = n->on_action;
        n->active = false;
        n->toast = false;
        if (cb) cb(a);
        return true;
    }
    n->active = false;
    n->toast = false;
    return true;
}

/* ---- Toasts ------------------------------------------------------------------ */
static i32 toast_x(void) { return (i32)SCREEN_W - 24 - CARD_W; }

void notify_draw(void) {
    if (s_center_visible) return;
    note_t *list[TOAST_MAX];
    u32 n = ordered(list, TOAST_MAX, true);
    i32 y = TOPBAR_H + 12;
    for (u32 i = 0; i < n; i++) {
        rect_t r = rect_make(toast_x(), y, CARD_W, card_h(list[i]));
        draw_card(list[i], r, NULL);
        y += r.h + 10;
    }
}

bool notify_handle_mouse(mouse_t *m) {
    if (s_center_visible || !m || !m->left_clicked) return false;
    note_t *list[TOAST_MAX];
    u32 n = ordered(list, TOAST_MAX, true);
    i32 y = TOPBAR_H + 12;
    for (u32 i = 0; i < n; i++) {
        rect_t r = rect_make(toast_x(), y, CARD_W, card_h(list[i]));
        if (click_card(list[i], r, m)) return true;
        y += r.h + 10;
    }
    return false;
}

/* ---- Notification Center column ----------------------------------------------- */
#define HEADER_H 30

static rect_t clear_rect(i32 x) {
    i32 w = gfx_str_role_width("Clear all", FONT_ROLE_UI, 1, FONT_W_BOLD) + 24;
    return rect_make(x + CARD_W - 6 - w, TOPBAR_H + 12, w, HEADER_H);
}

void notify_center_set_visible(bool on) { s_center_visible = on; }

void notify_center_draw(i32 x, const mouse_t *m) {
    i32 y = TOPBAR_H + 12;
    i32 dh = gfx_role_line_h(FONT_ROLE_DISPLAY, 0);
    if (g_theme->is_dark)
        gfx_str_role(x + 6, y + (HEADER_H - dh) / 2 + 1, "Notifications", 0x000000, FONT_ROLE_DISPLAY, 0, FONT_W_REGULAR);
    gfx_str_role(x + 6, y + (HEADER_H - dh) / 2, "Notifications", g_theme->ink, FONT_ROLE_DISPLAY, 0, FONT_W_REGULAR);

    note_t *list[NOTE_MAX];
    u32 n = ordered(list, NOTE_MAX, false);
    if (n) {
        rect_t c = clear_rect(x);
        bool hover = m && rect_contains(c, m->x, m->y);
        gfx_rect_rounded_blend(c.x, c.y, c.w, c.h, c.h / 2, g_theme->panel_tint, hover ? 230 : 179);
        gfx_str_role(c.x + 12, c.y + (c.h - gfx_role_line_h(FONT_ROLE_UI, 1)) / 2, "Clear all",
                     g_theme->is_dark ? 0xD5DAEAu : g_theme->ink2, FONT_ROLE_UI, 1, FONT_W_BOLD);
    }
    y += HEADER_H + 10;

    if (!n) {
        rect_t r = rect_make(x, y, CARD_W, 64);
        ui_glass(41, r, CARD_R, g_theme->panel_tint, g_theme->is_dark ? 209 : 224, 30, UI_GLASS_HAIRLINE);
        const char *msg = "No notifications";
        i32 w = gfx_str_role_width(msg, FONT_ROLE_UI, 1, FONT_W_REGULAR);
        gfx_str_role(r.x + (r.w - w) / 2, r.y + (r.h - gfx_role_line_h(FONT_ROLE_UI, 1)) / 2, msg,
                     g_theme->muted, FONT_ROLE_UI, 1, FONT_W_REGULAR);
        return;
    }
    i32 bottom = (i32)SCREEN_H - TASKBAR_H - 12;
    for (u32 i = 0; i < n; i++) {
        rect_t r = rect_make(x, y, CARD_W, card_h(list[i]));
        if (r.y + r.h > bottom) break;
        draw_card(list[i], r, m);
        y += r.h + 10;
    }
}

/* True if the click landed on the column (consumed). */
bool notify_center_click(i32 x, mouse_t *m) {
    if (!m || !m->left_clicked) return false;
    note_t *list[NOTE_MAX];
    u32 n = ordered(list, NOTE_MAX, false);
    if (n && rect_contains(clear_rect(x), m->x, m->y)) { notify_clear_all(); return true; }
    i32 y = TOPBAR_H + 12 + HEADER_H + 10;
    i32 bottom = (i32)SCREEN_H - TASKBAR_H - 12;
    for (u32 i = 0; i < n; i++) {
        rect_t r = rect_make(x, y, CARD_W, card_h(list[i]));
        if (r.y + r.h > bottom) break;
        if (click_card(list[i], r, m)) return true;
        y += r.h + 10;
    }
    return rect_contains(rect_make(x, TOPBAR_H + 12, CARD_W, HEADER_H), m->x, m->y);
}

/* Newest notifications for the lock screen: app, title and age only (the body
 * stays private while locked). */
u32 notify_previews(notify_preview_t *out, u32 max) {
    note_t *list[NOTE_MAX];
    u32 n = ordered(list, max < NOTE_MAX ? max : NOTE_MAX, false);
    for (u32 i = 0; i < n; i++) {
        out[i].app = list[i]->app;
        copy(out[i].app_name, app_label(list[i]->app), sizeof(out[i].app_name));
        copy(out[i].title, list[i]->title, sizeof(out[i].title));
        when(list[i]->born_ms, out[i].when);
    }
    return n;
}
