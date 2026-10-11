/* gui/ui_glyph.c -- cached system glyphs. Rasterising a glyph costs a few
 * thousand distance tests, so each (glyph, size) mask is built once and then
 * only blended. The cache is small and fixed: the shell uses a handful of
 * glyphs at two or three sizes. */
#include "kernel.h"
#include "gui.h"
#include "ui.h"
#include "ui_glyph_raster.h"

_Static_assert(UI_GLYPH_COUNT == 30, "ui_glyph_t must match tools/gen-ui-glyphs.py");
_Static_assert(UI_ICON_COUNT == 23, "ui_icon_t must match ICONS in tools/gen-ui-glyphs.py");

#define GLYPH_CACHE 48

typedef struct { u8 glyph, size; u8 *mask; } glyph_slot_t;
static glyph_slot_t cache[GLYPH_CACHE];
static u32 next_victim;

static const u8 *glyph_mask(u32 g, u32 size) {
    for (u32 i = 0; i < GLYPH_CACHE; i++)
        if (cache[i].mask && cache[i].glyph == g && cache[i].size == size) return cache[i].mask;
    u32 slot = GLYPH_CACHE;
    for (u32 i = 0; i < GLYPH_CACHE; i++)
        if (!cache[i].mask) { slot = i; break; }
    if (slot == GLYPH_CACHE) {               /* full: round-robin eviction */
        slot = next_victim++ % GLYPH_CACHE;
        kfree(cache[slot].mask);
        cache[slot].mask = NULL;
    }
    u8 *m = (u8 *)kmalloc(size * size);
    if (!m) return NULL;
    if (!ui_glyph_raster(g, size, m)) { kfree(m); return NULL; }
    cache[slot].glyph = (u8)g;
    cache[slot].size = (u8)size;
    cache[slot].mask = m;
    return m;
}

void ui_glyph(ui_glyph_t g, i32 x, i32 y, i32 size, u32 color) {
    if ((u32)g >= UI_GLYPH_COUNT || size < 4 || size > UI_GLYPH_MAX_SIZE) return;
    const u8 *m = glyph_mask((u32)g, (u32)size);
    if (m) gfx_blend_mask(x, y, m, size, size, color);
}
