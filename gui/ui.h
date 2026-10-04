/* gui/ui.h -- the v10 UI kit (visual-system spec §5). Shell and app code draw
 * through these components and the theme tokens instead of raw colours.
 * Interface frozen per the W0 plan (docs/superpowers/plans/2026-10-03-careos-v10-w0-foundation.md). */
#ifndef CAREOS_UI_H
#define CAREOS_UI_H
#include "gui.h"

typedef enum { UI_NORMAL = 0, UI_HOVER, UI_PRESSED, UI_FOCUSED, UI_DISABLED } ui_state_t;
typedef enum { UI_BTN_PRIMARY = 0, UI_BTN_SECONDARY, UI_BTN_GHOST, UI_BTN_PILL_LIGHT, UI_BTN_DANGER } ui_button_kind_t;

/* Radii (px) */
#define UI_R_SMALL     10
#define UI_R_BUTTON    14
#define UI_R_INPUT     18
#define UI_R_CARD      20
#define UI_R_WINDOW    22
#define UI_R_PANEL     24
#define UI_R_CC        28
#define UI_TOGGLE_W    46
#define UI_TOGGLE_H    28
#define UI_HIT_MIN     44

/* ui_glass flags */
#define UI_GLASS_HAIRLINE  0x01u
#define UI_GLASS_SHADOW_S  0x02u
#define UI_GLASS_SHADOW_M  0x04u
#define UI_GLASS_SHADOW_L  0x08u
#define UI_GLASS_MOVING    0x10u   /* being dragged: tint only this frame */

/* Order matches tools/gen-ui-glyphs.py (GLYPHS) -- keep them in step. */
typedef enum {
    UI_GLYPH_WIFI = 0, UI_GLYPH_WIRED, UI_GLYPH_BLUETOOTH, UI_GLYPH_SOUND, UI_GLYPH_MUTE,
    UI_GLYPH_BATTERY, UI_GLYPH_SEARCH, UI_GLYPH_BELL, UI_GLYPH_MOON, UI_GLYPH_SUN,
    UI_GLYPH_LOCK, UI_GLYPH_POWER, UI_GLYPH_RESTART, UI_GLYPH_USER, UI_GLYPH_CLOCK,
    UI_GLYPH_PLUS, UI_GLYPH_SLIDERS, UI_GLYPH_GRID, UI_GLYPH_CHEVRON_L, UI_GLYPH_CHEVRON_R,
    UI_GLYPH_CHEVRON_D, UI_GLYPH_CLOSE, UI_GLYPH_CHECK, UI_GLYPH_ARROW_R, UI_GLYPH_EYE,
    UI_GLYPH_CAMERA, UI_GLYPH_HOME, UI_GLYPH_FOLDER, UI_GLYPH_TRASH, UI_GLYPH_HEART,
    UI_GLYPH_COUNT
} ui_glyph_t;
#define UI_GLYPH_NONE UI_GLYPH_COUNT

typedef enum {
    UI_ICON_CARE = 0, UI_ICON_TERMINAL, UI_ICON_FILES, UI_ICON_BROWSER, UI_ICON_NOTES,
    UI_ICON_EDITOR, UI_ICON_PAINT, UI_ICON_MONITOR, UI_ICON_CLOCK, UI_ICON_CALC,
    UI_ICON_NETMON, UI_ICON_STORE, UI_ICON_SETTINGS, UI_ICON_USERS, UI_ICON_ABOUT,
    UI_ICON_GAMES, UI_ICON_PHOTOS, UI_ICON_MUSIC, UI_ICON_MAIL, UI_ICON_CALENDAR,
    UI_ICON_PET, UI_ICON_LAUNCHER, UI_ICON_TRASH, UI_ICON_COUNT
} ui_icon_t;

/* Frame + damage (compositor). gui_run calls frame_begin/end; anything that
 * changes pixels under glass calls ui_damage. */
void ui_frame_begin(void);
void ui_frame_end(u32 frame_ms);
void ui_damage(rect_t r);
void ui_damage_all(void);

/* Surfaces */
void ui_glass(u32 id, rect_t r, i32 radius, u32 tint, u8 alpha, i32 blur, u32 flags);
void ui_glass_forget(u32 id);
void ui_panel(rect_t r, i32 radius, u32 fill, u32 flags);

/* Controls */
ui_state_t ui_state_for(rect_t r, const mouse_t *m, bool pressed, bool disabled);
void ui_button(rect_t r, const char *label, ui_button_kind_t kind, ui_state_t st);
void ui_icon_button(rect_t r, ui_glyph_t g, ui_state_t st, bool active);
void ui_toggle(i32 x, i32 y, bool on, ui_state_t st);
void ui_slider(rect_t r, u32 value_pct, ui_state_t st);
void ui_chip(rect_t r, const char *label, ui_icon_t icon /* UI_ICON_COUNT = none */, bool selected, ui_state_t st);
void ui_input(rect_t r, const textinput_t *t, const char *placeholder, ui_glyph_t icon, bool focused);
void ui_progress(rect_t r, u32 pct, u32 color);
i32  ui_kbd(i32 x, i32 y, const char *label);          /* returns drawn width */
void ui_traffic_lights(i32 x, i32 y, bool hover, bool focused);
#define UI_TRAFFIC_W  58                                /* 3 x 14 + 2 x 8 */
i32  ui_traffic_hit(i32 x, i32 y, i32 mx, i32 my);      /* -1 none, 0 close, 1 min, 2 max */

/* Glyphs and icons */
void ui_glyph(ui_glyph_t g, i32 x, i32 y, i32 size, u32 color);
void ui_app_icon(ui_icon_t icon, i32 x, i32 y, i32 size);
ui_icon_t ui_icon_for_app(app_id_t app);

/* Helpers beyond the frozen set (additive). */
u32  ui_mix(u32 bg, u32 fg, u8 alpha);                  /* fg over bg at alpha */
void ui_hairline(rect_t r, i32 radius, u32 under);      /* token hairline over `under` */
void ui_care_logo(i32 x, i32 y, i32 size);              /* coral disc + heart */
void ui_set_reduce_transparency(bool on);               /* glass draws tint only */
bool ui_glass_tint_only(void);                          /* reduced, or the slow-frame guard */
#endif
