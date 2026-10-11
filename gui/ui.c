/* gui/ui.c -- v10 UI kit components (visual-system spec §5).
 *
 * Everything reads the theme tokens; nothing here hard-codes a palette colour
 * except white/black overlays, which are theme-neutral. Glass blurs the live
 * backdrop each frame through gfx_blur_region; the cached-backdrop version
 * (spec §6) replaces ui_glass's body without changing callers. */
#include "kernel.h"
#include "gui.h"
#include "ui.h"
#include "ui_icons.h"
#include "ui_glyph_raster.h"
#include "glass.h"

#define WHITE 0xFFFFFFu


u32 ui_mix(u32 bg, u32 fg, u8 alpha) {
    u32 a = alpha, ia = 255u - a;
    u32 r = (((bg >> 16) & 0xFF) * ia + ((fg >> 16) & 0xFF) * a) / 255u;
    u32 g = (((bg >>  8) & 0xFF) * ia + ((fg >>  8) & 0xFF) * a) / 255u;
    u32 b = (( bg        & 0xFF) * ia + ( fg        & 0xFF) * a) / 255u;
    return (r << 16) | (g << 8) | b;
}

void ui_hairline(rect_t r, i32 radius, u32 under) {
    gfx_rect_rounded_outline(r.x, r.y, r.w, r.h, radius,
                             ui_mix(under, g_theme->hairline, g_theme->hairline_alpha));
}

static void shadow_for(rect_t r, i32 radius, u32 flags) {
    if (flags & UI_GLASS_SHADOW_L)      gfx_shadow_soft_ex(r.x, r.y, r.w, r.h, radius, 40, g_theme->is_dark ? 70 : 36, 18);
    else if (flags & UI_GLASS_SHADOW_M) gfx_shadow_soft_ex(r.x, r.y, r.w, r.h, radius, 28, g_theme->is_dark ? 56 : 30, 12);
    else if (flags & UI_GLASS_SHADOW_S) gfx_shadow_soft_ex(r.x, r.y, r.w, r.h, radius, 14, g_theme->is_dark ? 40 : 24, 4);
}

/* ---- Glass (spec §6) ---------------------------------------------------------
 * Each glass rect keeps its blurred backdrop. A frame re-blurs only when the
 * backdrop's signature changed (glass.c); otherwise it is one rounded blit.
 * Entries unused for 120 frames are freed. The guard drops to tint-only when
 * frames run slow, as does "Reduce transparency". */
#define GLASS_CACHE 24
#define GLASS_IDLE_FRAMES 120

typedef struct { bool used; rect_t r; i32 blur; u32 sig; u32 last; u32 *px; } glass_entry_t;
static glass_entry_t gcache[GLASS_CACHE];
static u32 g_frame = 0;
static glass_guard_t g_guard;
static bool g_reduce_transparency = false;
static bool g_alloc_warned = false;

void ui_frame_begin(void) { g_frame++; }

void ui_frame_end(u32 frame_ms) {
    bool was = g_guard.tint_only;
    glass_guard_frame(&g_guard, frame_ms, timer_ms());
    if (was != g_guard.tint_only)
        serial_write(g_guard.tint_only ? "[ui] glass: tint only (slow frames)\n"
                                       : "[ui] glass: blur restored\n");
    for (u32 i = 0; i < GLASS_CACHE; i++)
        if (gcache[i].used && g_frame - gcache[i].last > GLASS_IDLE_FRAMES) {
            kfree(gcache[i].px);
            kmemset(&gcache[i], 0, sizeof(gcache[i]));
        }
}

void ui_damage(rect_t r) {
    for (u32 i = 0; i < GLASS_CACHE; i++) {
        rect_t c = gcache[i].r;
        if (gcache[i].used && r.x < c.x + c.w && c.x < r.x + r.w && r.y < c.y + c.h && c.y < r.y + r.h)
            gcache[i].sig ^= 0xA5A5A5A5u;          /* force a re-blur */
    }
}
void ui_damage_all(void) { for (u32 i = 0; i < GLASS_CACHE; i++) gcache[i].sig ^= 0xA5A5A5A5u; }
void ui_glass_forget(u32 id) { (void)id; }
void ui_set_reduce_transparency(bool on) { g_reduce_transparency = on; }
bool ui_glass_tint_only(void) { return g_reduce_transparency || g_guard.tint_only; }

static glass_entry_t *glass_entry(rect_t r, i32 blur) {
    glass_entry_t *free_e = NULL, *lru = &gcache[0];
    for (u32 i = 0; i < GLASS_CACHE; i++) {
        glass_entry_t *e = &gcache[i];
        if (e->used && e->blur == blur && e->r.x == r.x && e->r.y == r.y && e->r.w == r.w && e->r.h == r.h)
            return e;
        if (!e->used && !free_e) free_e = e;
        if (e->used && e->last < lru->last) lru = e;
    }
    glass_entry_t *e = free_e ? free_e : lru;
    if (e->used) kfree(e->px);
    kmemset(e, 0, sizeof(*e));
    e->px = (u32 *)kmalloc((size_t)r.w * (size_t)r.h * 4u);
    if (!e->px) return NULL;
    e->used = true;
    e->r = r;
    e->blur = blur;
    e->sig = ~0u;              /* first use always blurs */
    return e;
}

/* Blur what is behind r into the panel; false = draw tint only. */
static bool glass_backdrop(rect_t r, i32 radius, i32 blur) {
    if (!g_target || !g_target->pixels) return false;
    i32 tw = (i32)g_target->w, th = (i32)g_target->h;
    rect_t c = r;
    if (c.x < 0) { c.w += c.x; c.x = 0; }
    if (c.y < 0) { c.h += c.y; c.y = 0; }
    if (c.x + c.w > tw) c.w = tw - c.x;
    if (c.y + c.h > th) c.h = th - c.y;
    if (c.w < 2 || c.h < 2) return false;

    u32 st = g_target->pitch / 4u;
    const u32 *src = g_target->pixels + (u32)c.y * st + (u32)c.x;
    glass_entry_t *e = glass_entry(c, blur);
    if (!e) {
        if (!g_alloc_warned) { serial_write("[ui] glass cache allocation failed; tint only\n"); g_alloc_warned = true; }
        return false;
    }
    e->last = g_frame;
    u32 sig = glass_signature(src, st, (u32)c.w, (u32)c.h);
    if (sig != e->sig) {
        u32 *scratch = (u32 *)kmalloc((size_t)glass_scratch_words((u32)c.w, (u32)c.h) * 4u);
        if (!scratch) return false;
        glass_blur(src, st, (u32)c.w, (u32)c.h, (u32)blur, e->px, scratch);
        kfree(scratch);
        e->sig = sig;
    }
    bool whole = c.x == r.x && c.y == r.y && c.w == r.w && c.h == r.h;
    if (whole && radius > 0) gfx_blit_argb_rounded(c.x, c.y, e->px, c.w, c.h, radius);
    else gfx_blit_argb(c.x, c.y, e->px, c.w, c.h, c.x, c.y, c.w, c.h);
    return true;
}

void ui_glass(u32 id, rect_t r, i32 radius, u32 tint, u8 alpha, i32 blur, u32 flags) {
    (void)id;
    if (r.w <= 0 || r.h <= 0) return;
    shadow_for(r, radius, flags);
    bool blurred = false;
    if (!(flags & UI_GLASS_MOVING) && blur > 0 && !ui_glass_tint_only())
        blurred = glass_backdrop(r, radius, blur);
    /* Tint-only glass leans on a stronger tint so text stays legible. */
    if (!blurred && alpha < 230) alpha = (u8)(alpha + (255 - alpha) / 2);
    gfx_rect_rounded_blend(r.x, r.y, r.w, r.h, radius, tint, alpha);
    if (flags & UI_GLASS_HAIRLINE) ui_hairline(r, radius, tint);
}

void ui_panel(rect_t r, i32 radius, u32 fill, u32 flags) {
    shadow_for(r, radius, flags);
    gfx_rect_rounded(r.x, r.y, r.w, r.h, radius, fill);
    if (flags & UI_GLASS_HAIRLINE) ui_hairline(r, radius, fill);
}

ui_state_t ui_state_for(rect_t r, const mouse_t *m, bool pressed, bool disabled) {
    if (disabled) return UI_DISABLED;
    if (!m || !rect_contains(r, m->x, m->y)) return UI_NORMAL;
    return pressed ? UI_PRESSED : UI_HOVER;
}

static void label_centered(rect_t r, const char *s, u32 fg, font_weight_t w) {
    i32 tw = gfx_str_role_width(s, FONT_ROLE_UI, 1, w);
    i32 th = gfx_role_line_h(FONT_ROLE_UI, 1);
    gfx_str_role(r.x + (r.w - tw) / 2, r.y + (r.h - th) / 2, s, fg, FONT_ROLE_UI, 1, w);
}

void ui_button(rect_t r, const char *label, ui_button_kind_t kind, ui_state_t st) {
    u32 bg = g_theme->raised2, fg = g_theme->ink;
    bool fill = true;
    switch (kind) {
    case UI_BTN_PRIMARY:    bg = g_theme->care;  fg = g_theme->on_care; break;
    case UI_BTN_DANGER:     bg = g_theme->error; fg = WHITE; break;
    case UI_BTN_PILL_LIGHT: bg = g_theme->ink;   fg = g_theme->ground; break;
    case UI_BTN_GHOST:      fill = (st == UI_HOVER || st == UI_PRESSED); bg = g_theme->raised; fg = g_theme->ink2; break;
    default: break;
    }
    i32 rad = (kind == UI_BTN_PILL_LIGHT) ? r.h / 2 : UI_R_BUTTON;
    if (rad * 2 > r.h) rad = r.h / 2;
    if (fill) gfx_rect_rounded(r.x, r.y, r.w, r.h, rad, bg);
    if (kind == UI_BTN_SECONDARY) ui_hairline(r, rad, bg);
    if (st == UI_HOVER && kind != UI_BTN_GHOST) gfx_rect_rounded_blend(r.x, r.y, r.w, r.h, rad, WHITE, 22);
    if (st == UI_PRESSED) gfx_rect_rounded_blend(r.x, r.y, r.w, r.h, rad, 0, 30);
    label_centered(r, label, st == UI_DISABLED ? g_theme->muted : fg, FONT_W_BOLD);
    if (st == UI_FOCUSED)
        gfx_rect_rounded_outline(r.x - 2, r.y - 2, r.w + 4, r.h + 4, rad + 2,
                                 ui_mix(g_theme->ground, g_theme->care, 153));
}

void ui_icon_button(rect_t r, ui_glyph_t g, ui_state_t st, bool active) {
    if (active || st == UI_HOVER || st == UI_PRESSED) {
        u32 over = g_theme->is_dark ? WHITE : g_theme->ink;
        u8 a = active ? (g_theme->is_dark ? 36 : 26) : (g_theme->is_dark ? 22 : 16);
        gfx_rect_rounded_blend(r.x, r.y, r.w, r.h, 8, over, a);
    }
    i32 s = r.h < 24 ? r.h - 4 : 17;
    ui_glyph(g, r.x + (r.w - s) / 2, r.y + (r.h - s) / 2, s,
             st == UI_DISABLED ? g_theme->muted : g_theme->ink);
}

void ui_toggle(i32 x, i32 y, bool on, ui_state_t st) {
    gfx_rect_rounded(x, y, UI_TOGGLE_W, UI_TOGGLE_H, UI_TOGGLE_H / 2, on ? g_theme->care : g_theme->track);
    if (st == UI_HOVER) gfx_rect_rounded_blend(x, y, UI_TOGGLE_W, UI_TOGGLE_H, UI_TOGGLE_H / 2, WHITE, 18);
    i32 kx = on ? x + UI_TOGGLE_W - 3 - 22 : x + 3;
    gfx_circle_fill(kx + 11, y + 14, 11, on ? WHITE : (g_theme->is_dark ? g_theme->ink2 : WHITE));
}

void ui_slider(rect_t r, u32 value_pct, ui_state_t st) {
    (void)st;
    if (value_pct > 100) value_pct = 100;
    i32 ty = r.y + r.h / 2 - 3;
    gfx_rect_rounded(r.x, ty, r.w, 6, 3, g_theme->track);
    i32 fw = r.w * (i32)value_pct / 100;
    if (fw > 0) gfx_rect_rounded(r.x, ty, fw, 6, 3, g_theme->care);
    gfx_circle_fill(r.x + fw, r.y + r.h / 2, 9, WHITE);
}

void ui_chip(rect_t r, const char *label, ui_icon_t icon, bool selected, ui_state_t st) {
    u32 bg = selected ? g_theme->ink : g_theme->raised;
    u32 fg = selected ? g_theme->ground : g_theme->ink;
    gfx_rect_rounded(r.x, r.y, r.w, r.h, r.h / 2, bg);
    if (st == UI_HOVER && !selected) gfx_rect_rounded_blend(r.x, r.y, r.w, r.h, r.h / 2, WHITE, 20);
    i32 tx = r.x + 16;
    if (icon < UI_ICON_COUNT) { ui_app_icon(icon, r.x + 8, r.y + (r.h - 24) / 2, 24); tx = r.x + 40; }
    gfx_str_role(tx, r.y + (r.h - gfx_role_line_h(FONT_ROLE_UI, 1)) / 2, label, fg, FONT_ROLE_UI, 1, FONT_W_BOLD);
}

void ui_input(rect_t r, const textinput_t *t, const char *placeholder, ui_glyph_t icon, bool focused) {
    gfx_rect_rounded_blend(r.x, r.y, r.w, r.h, UI_R_INPUT, g_theme->panel_tint, g_theme->panel_alpha);
    if (focused) gfx_rect_rounded_outline(r.x, r.y, r.w, r.h, UI_R_INPUT, g_theme->care);
    else         ui_hairline(r, UI_R_INPUT, g_theme->panel_tint);
    i32 tx = r.x + 18;
    if (icon != UI_GLYPH_NONE) { ui_glyph(icon, r.x + 18, r.y + (r.h - 20) / 2, 20, g_theme->ink2); tx = r.x + 50; }
    i32 ty = r.y + (r.h - gfx_role_line_h(FONT_ROLE_UI, 3)) / 2;
    if (t && t->len) gfx_str_role(tx, ty, t->buf, g_theme->ink, FONT_ROLE_UI, 3, FONT_W_REGULAR);
    else if (placeholder) gfx_str_role(tx, ty, placeholder, g_theme->muted, FONT_ROLE_UI, 3, FONT_W_REGULAR);
}

void ui_progress(rect_t r, u32 pct, u32 color) {
    if (pct > 100) pct = 100;
    gfx_rect_rounded(r.x, r.y, r.w, r.h, r.h / 2, g_theme->track);
    i32 fw = r.w * (i32)pct / 100;
    if (fw > r.h) gfx_rect_rounded(r.x, r.y, fw, r.h, r.h / 2, color);
    else if (fw > 0) gfx_circle_fill(r.x + r.h / 2, r.y + r.h / 2, r.h / 2, color);
}

i32 ui_kbd(i32 x, i32 y, const char *label) {
    i32 w = gfx_str_role_width(label, FONT_ROLE_MONO, 0, FONT_W_REGULAR) + 16;
    gfx_rect_rounded(x, y, w, 24, 8, g_theme->raised2);
    gfx_str_role(x + 8, y + (24 - gfx_role_line_h(FONT_ROLE_MONO, 0)) / 2, label,
                 g_theme->ink2, FONT_ROLE_MONO, 0, FONT_W_REGULAR);
    return w;
}

void ui_traffic_lights(i32 x, i32 y, bool hover, bool focused) {
    (void)hover;
    u32 c0 = focused ? 0xFF5F57u : g_theme->raised2;
    u32 c1 = focused ? 0xF5B83Du : g_theme->raised2;
    u32 c2 = focused ? 0x3DD68Cu : g_theme->raised2;
    gfx_circle_fill(x + 7, y + 7, 7, c0);
    gfx_circle_fill(x + 29, y + 7, 7, c1);
    gfx_circle_fill(x + 51, y + 7, 7, c2);
}
i32 ui_traffic_hit(i32 x, i32 y, i32 mx, i32 my) {
    if (my < y - 4 || my > y + 18) return -1;
    for (i32 i = 0; i < 3; i++) { i32 cx = x + 7 + i * 22; if (mx >= cx - 10 && mx <= cx + 10) return i; }
    return -1;
}

void ui_care_logo(i32 x, i32 y, i32 size) {
    i32 r = size / 2;
    gfx_circle_fill(x + r, y + r, r, g_theme->care);
    i32 hs = size * 13 / 20;
    ui_glyph(UI_GLYPH_HEART, x + (size - hs) / 2, y + (size - hs) / 2 + 1, hs, g_theme->on_care);
}

/* App icons (spec §3), rendered from the AppIcon artboard's definitions by
 * ui_icon_raster() and cached per (icon, size). The artboard's drop shadow,
 * 0 4px 14px rgba(8,10,30,.22), is drawn live so it lands on whatever is
 * behind the icon. */
#define ICON_CACHE 48
typedef struct { u8 icon, size; u32 *px; } icon_slot_t;
static icon_slot_t icon_cache[ICON_CACHE];
static u32 icon_victim;

static const u32 *icon_pixels(u32 icon, u32 size) {
    for (u32 i = 0; i < ICON_CACHE; i++)
        if (icon_cache[i].px && icon_cache[i].icon == icon && icon_cache[i].size == size)
            return icon_cache[i].px;
    u32 slot = ICON_CACHE;
    for (u32 i = 0; i < ICON_CACHE; i++) if (!icon_cache[i].px) { slot = i; break; }
    if (slot == ICON_CACHE) {
        slot = icon_victim++ % ICON_CACHE;
        kfree(icon_cache[slot].px);
        icon_cache[slot].px = NULL;
    }
    u32 *px = (u32 *)kmalloc((size_t)size * size * 4u);
    if (!px) return NULL;
    if (!ui_icon_raster(icon, size, px)) { kfree(px); return NULL; }
    icon_cache[slot].icon = (u8)icon;
    icon_cache[slot].size = (u8)size;
    icon_cache[slot].px = px;
    return px;
}

void ui_app_icon(ui_icon_t icon, i32 x, i32 y, i32 size) {
    if ((u32)icon >= UI_ICON_COUNT) icon = UI_ICON_CARE;
    if (size > UI_ICON_MAX_SIZE) size = UI_ICON_MAX_SIZE;
    if (size < 12) return;
    i32 r = (size * 28 + 50) / 100;
    if (size >= 32) gfx_shadow_soft_ex(x, y, size, size, r, 14, 40, 4);
    const u32 *px = icon_pixels((u32)icon, (u32)size);
    if (px) gfx_blend_argb(x, y, px, size, size);
}
ui_icon_t ui_icon_for_app(app_id_t app) { return (ui_icon_t)ui_icon_key_for_app((u32)app); }
