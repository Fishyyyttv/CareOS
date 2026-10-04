/* gui/font_roles.c -- type roles over the generated Figtree / Bricolage /
 * JetBrains Mono tables. The pure part maps role + slot to design pixels; the
 * kernel part picks the face and draws through gfx_str_face(). */
#include "font_roles.h"

static const u8 PX[3][5] = { {11,13,15,17,20}, {17,22,28,52,64}, {11,13,14,16,20} };
static const u8 LEGACY[5] = { 11, 13, 16, 20, 28 };   /* FONT_CAPTION .. FONT_H1 */

static u32 nearest_legacy(u32 px) {
    u32 best = 0, bd = 0xFFFFFFFFu;
    for (u32 i = 0; i < 5; i++) {
        u32 d = px > LEGACY[i] ? px - LEGACY[i] : LEGACY[i] - px;
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

u32 font_role_px(u32 role, u32 slot) {
    if (role > 2) role = 0;
    if (slot > 4) slot = 4;
    return PX[role][slot];
}

u32 font_role_fallback_size(u32 role, u32 slot) { return nearest_legacy(font_role_px(role, slot)); }

#ifndef HOST_TEST
#include "gui.h"
#include "font.h"

extern const font_family_t font_figtree, font_figtree_bold;
extern const font_family_t font_bricolage_bold, font_bricolage_xbold;
extern const font_family_t font_jetbrains_mono, font_jetbrains_mono_bold;
extern const font_family_t font_bricolage_clock;     /* digits, ':' and AM/PM only */

static const font_face_t *font_role_face(font_role_t role, u32 slot, font_weight_t w) {
    if (slot > 4) slot = 4;
    switch (role) {
    case FONT_ROLE_DISPLAY:
        return &(w == FONT_W_BOLD ? &font_bricolage_xbold : &font_bricolage_bold)->faces[slot];
    case FONT_ROLE_MONO:
        return &(w == FONT_W_BOLD ? &font_jetbrains_mono_bold : &font_jetbrains_mono)
                    ->faces[font_role_fallback_size(FONT_ROLE_MONO, slot)];
    default:
        return &(w == FONT_W_BOLD ? &font_figtree_bold : &font_figtree)->faces[slot];
    }
}

void gfx_str_role(i32 x, i32 y, const char *s, u32 fg, font_role_t role, u32 slot, font_weight_t w) {
    gfx_str_face(x, y, s, fg, font_role_face(role, slot, w));
}
i32 gfx_str_role_width(const char *s, font_role_t role, u32 slot, font_weight_t w) {
    return gfx_str_face_width_n(s, 0xFFFFFFFFu, font_role_face(role, slot, w));
}
i32 gfx_str_role_width_n(const char *s, u32 n, font_role_t role, u32 slot, font_weight_t w) {
    return gfx_str_face_width_n(s, n, font_role_face(role, slot, w));
}
i32 gfx_role_line_h(font_role_t role, u32 slot) {
    return (i32)font_role_face(role, slot, FONT_W_REGULAR)->line_h * (i32)GFX_FONT_SCALE;
}

/* Draw at most maxw px of s, ending in "..." when it does not fit. */
void gfx_str_role_fit(i32 x, i32 y, i32 maxw, const char *s, u32 fg,
                      font_role_t role, u32 slot, font_weight_t w) {
    const font_face_t *f = font_role_face(role, slot, w);
    if (gfx_str_face_width_n(s, 0xFFFFFFFFu, f) <= maxw) { gfx_str_face(x, y, s, fg, f); return; }
    i32 dots = gfx_str_face_width_n("...", 3, f);
    char buf[128];
    u32 n = 0;
    while (s[n] && n < sizeof(buf) - 4 && gfx_str_face_width_n(s, n + 1, f) + dots <= maxw) n++;
    kmemcpy(buf, s, n);
    buf[n] = '.'; buf[n + 1] = '.'; buf[n + 2] = '.'; buf[n + 3] = '\0';
    gfx_str_face(x, y, buf, fg, f);
}

/* Clock digits (Bricolage Bold, -0.04 em): slots 0..4 = 72 96 120 144 168 px. */
static const font_face_t *clock_face(u32 slot) { return &font_bricolage_clock.faces[slot > 4 ? 4 : slot]; }
void gfx_str_clock(i32 x, i32 y, const char *s, u32 fg, u32 slot) { gfx_str_face(x, y, s, fg, clock_face(slot)); }
i32  gfx_str_clock_width(const char *s, u32 slot) { return gfx_str_face_width_n(s, 0xFFFFFFFFu, clock_face(slot)); }
/* Top of the digits below the line top, and their height, for tight layout. */
i32  gfx_clock_cap_top(u32 slot) {
    const font_face_t *f = clock_face(slot);
    const glyph_t *g = &f->glyphs['0' - FONT_FIRST_CH];
    return ((i32)f->baseline - (i32)g->bearing_y) * (i32)GFX_FONT_SCALE;
}
i32  gfx_clock_cap_h(u32 slot) { return (i32)clock_face(slot)->glyphs['0' - FONT_FIRST_CH].h * (i32)GFX_FONT_SCALE; }
void gfx_str_huge(i32 x, i32 y, const char *s, u32 fg) { gfx_str_clock(x, y, s, fg, 2); }
i32  gfx_str_huge_width(const char *s) { return gfx_str_clock_width(s, 2); }

/* Word-wrap s into lines no wider than maxw (explicit '\n' breaks too) and
 * draw them; returns the height used. fg == COL_TRANSPARENT only measures. */
i32 gfx_str_role_wrap(i32 x, i32 y, i32 maxw, const char *s, u32 fg,
                      font_role_t role, u32 slot, font_weight_t w, i32 gap) {
    const font_face_t *f = font_role_face(role, slot, w);
    i32 lh = (i32)f->line_h * (i32)GFX_FONT_SCALE + gap, yy = y;
    char line[160];
    while (s && *s) {
        u32 n = 0, brk = 0;
        while (s[n] && s[n] != '\n' && n < sizeof(line) - 1) {
            if (gfx_str_face_width_n(s, n + 1, f) > maxw && n > 0) break;
            if (s[n] == ' ') brk = n;
            n++;
        }
        u32 take = n;
        if (s[n] && s[n] != '\n' && brk > 0) take = brk;   /* break at the last space */
        kmemcpy(line, s, take);
        line[take] = '\0';
        if (fg != COL_TRANSPARENT) gfx_str_face(x, yy, line, fg, f);
        yy += lh;
        s += take;
        while (*s == ' ') s++;
        if (*s == '\n') s++;
    }
    return yy - y;
}
#endif
