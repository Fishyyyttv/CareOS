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

/* Large clock digits. Display ExtraBold 64 until a dedicated 120 px face lands. */
void gfx_str_huge(i32 x, i32 y, const char *s, u32 fg) { gfx_str_role(x, y, s, fg, FONT_ROLE_DISPLAY, 4, FONT_W_BOLD); }
i32  gfx_str_huge_width(const char *s) { return gfx_str_role_width(s, FONT_ROLE_DISPLAY, 4, FONT_W_BOLD); }
#endif
