/* gui/session/power.c -- the power menu (Power artboard).
 *
 * Modal over whatever is on screen: that frame blurred and dimmed, with a
 * centred card. Only real choices: Restart, Shut down and (in a session) Log
 * out. Sleep, "reopen my windows" and the power-button countdown wait for ACPI
 * sleep, session restore and ACPI button events. Pip's spot above the title is
 * kept for phase 4. */
#include "kernel.h"
#include "session/session.h"
#include "glass.h"

typedef struct { rect_t card, tiles[3], cancel; u32 ntiles; i32 title_y, sub_y; } playout_t;

enum { T_RESTART, T_SHUTDOWN, T_LOGOUT };

static void playout(playout_t *P, bool from_login) {
    i32 sw = (i32)SCREEN_W, sh = (i32)SCREEN_H;
    P->ntiles = from_login ? 2 : 3;
    i32 w = 640, pad = 32;
    i32 title_h = gfx_role_line_h(FONT_ROLE_DISPLAY, 2), sub_h = gfx_role_line_h(FONT_ROLE_UI, 2);
    i32 h = pad + 44 + title_h + 6 + sub_h + 24 + 116 + 24 + 46 + pad;
    i32 x = (sw - w) / 2, y = (sh - h) / 2;
    P->card = rect_make(x, y, w, h);
    P->title_y = y + pad + 44;                        /* 44: room for Pip (phase 4) */
    P->sub_y = P->title_y + title_h + 6;
    i32 ty = P->sub_y + sub_h + 24, gap = 12;
    i32 tw = (w - 2 * pad - (i32)(P->ntiles - 1) * gap) / (i32)P->ntiles;
    for (u32 i = 0; i < P->ntiles; i++) P->tiles[i] = rect_make(x + pad + (i32)i * (tw + gap), ty, tw, 116);
    i32 cw = gfx_str_role_width("Cancel", FONT_ROLE_UI, 2, FONT_W_BOLD) + 44;
    P->cancel = rect_make(x + w - pad - cw, ty + 116 + 24, cw, 46);
}

static void tile(rect_t r, ui_glyph_t g, const char *label, bool danger, bool hover) {
    u32 bg = danger ? g_theme->care : (g_theme->is_dark ? 0x1C2440u : g_theme->raised);
    u32 fg = danger ? g_theme->on_care : g_theme->ink;
    gfx_rect_rounded(r.x, r.y, r.w, r.h, 22, bg);
    if (hover) gfx_rect_rounded_blend(r.x, r.y, r.w, r.h, 22, 0xFFFFFF, 26);
    i32 lh = gfx_role_line_h(FONT_ROLE_UI, 2), top = r.y + (r.h - 52 - 12 - lh) / 2;
    gfx_circle_fill(r.x + r.w / 2, top + 26, 26, danger ? ui_mix(bg, 0x2A120A, 36) : (g_theme->is_dark ? 0x2A3358u : g_theme->raised2));
    ui_glyph(g, r.x + r.w / 2 - 11, top + 15, 22, fg);
    i32 tw = gfx_str_role_width(label, FONT_ROLE_UI, 2, FONT_W_BOLD);
    gfx_str_role(r.x + (r.w - tw) / 2, top + 52 + 12, label, fg, FONT_ROLE_UI, 2, FONT_W_BOLD);
}

power_choice_t session_power_menu(mouse_t *m, bool from_login) {
    i32 sw = (i32)SCREEN_W, sh = (i32)SCREEN_H;
    u32 n = (u32)sw * (u32)sh;
    u32 *bg = (u32 *)kmalloc((size_t)n * 4u);
    u32 *scratch = (u32 *)kmalloc((size_t)glass_scratch_words((u32)sw, (u32)sh) * 4u);
    if (bg && scratch && g_target && g_target->pixels) {
        /* rgba(5,8,20,.62) over the screen blurred 14 px. */
        glass_blur(g_target->pixels, g_target->pitch / 4u, (u32)sw, (u32)sh, 14, bg, scratch);
        for (u32 i = 0; i < n; i++) bg[i] = ui_mix(bg[i], 0x050814, 158);
    } else if (bg) {
        for (u32 i = 0; i < n; i++) bg[i] = 0x050814;
    }
    if (scratch) kfree(scratch);

    playout_t P;
    playout(&P, from_login);
    static const ui_glyph_t G[3] = { UI_GLYPH_RESTART, UI_GLYPH_POWER, UI_GLYPH_USER };
    static const char *const LBL[3] = { "Restart", "Shut down", "Log out" };
    power_choice_t result = POWER_CANCEL;
    keyboard_flush();

    while (1) {
        bool done = false;
        while (keyboard_haschar()) if (gui_read_key() == 27) done = true;
        mouse_update(m);
        if (m->left_clicked) {
            if (!rect_contains(P.card, m->x, m->y) || rect_contains(P.cancel, m->x, m->y)) done = true;
            for (u32 i = 0; i < P.ntiles; i++) {
                if (!rect_contains(P.tiles[i], m->x, m->y)) continue;
                if (i == T_RESTART) { serial_write("[power] restart\n"); power_reboot(); }
                if (i == T_SHUTDOWN) { serial_write("[power] shut down\n"); power_shutdown(); }
                if (i == T_LOGOUT) { result = POWER_LOGOUT; done = true; }
            }
        }
        if (done) break;

        if (bg) gfx_blit_argb(0, 0, bg, sw, sh, 0, 0, sw, sh);
        gfx_shadow_soft_ex(P.card.x, P.card.y, P.card.w, P.card.h, 32, 60, 110, 30);
        ui_panel(P.card, 32, g_theme->is_dark ? 0x151B2Eu : g_theme->surface, UI_GLASS_HAIRLINE);
        session_pet_slot(rect_make(P.card.x + P.card.w / 2 - 70, P.card.y - 96 + 32, 140, 140), 1);
        const char *t = "Taking a break?";
        i32 tw = gfx_str_role_width(t, FONT_ROLE_DISPLAY, 2, FONT_W_BOLD);
        gfx_str_role(P.card.x + (P.card.w - tw) / 2, P.title_y, t, g_theme->ink, FONT_ROLE_DISPLAY, 2, FONT_W_BOLD);
        const char *s = "Pick what you'd like to do.";   /* "your windows are saved" needs session restore */
        i32 sw2 = gfx_str_role_width(s, FONT_ROLE_UI, 2, FONT_W_REGULAR);
        gfx_str_role(P.card.x + (P.card.w - sw2) / 2, P.sub_y, s, g_theme->ink2, FONT_ROLE_UI, 2, FONT_W_REGULAR);
        for (u32 i = 0; i < P.ntiles; i++)
            tile(P.tiles[i], G[i], LBL[i], i == T_SHUTDOWN, rect_contains(P.tiles[i], m->x, m->y));
        ui_button(P.cancel, "Cancel", UI_BTN_SECONDARY, ui_state_for(P.cancel, m, false, false));
        session_frame_end(m);
    }
    if (bg) kfree(bg);
    keyboard_flush();
    return result;
}
