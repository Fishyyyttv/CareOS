#include "pure.h"
#include "ui_glyph_raster.h"
#include <assert.h>

enum { WIFI, WIRED, BLUETOOTH, SOUND, MUTE, BATTERY, SEARCH, BELL, MOON, SUN, LOCK, POWER,
       RESTART, USER, CLOCK, PLUS, SLIDERS, GRID, CHEV_L, CHEV_R, CHEV_D, CLOSE, CHECK,
       ARROW_R, EYE, CAMERA, HOME, FOLDER, TRASH, HEART, COUNT };

static u8 m[UI_GLYPH_MAX_SIZE * UI_GLYPH_MAX_SIZE];

int main(void) {
    assert(ui_glyph_raster_count() == COUNT);
    assert(!ui_glyph_raster(COUNT, 24, m));
    assert(!ui_glyph_raster(PLUS, 2, m));
    assert(!ui_glyph_raster(PLUS, UI_GLYPH_MAX_SIZE + 1, m));

    /* Plus at 24: the bars are 2 px wide centred on x/y = 12 (pixels 11, 12). */
    assert(ui_glyph_raster(PLUS, 24, m));
    assert(m[12 * 24 + 11] == 255 && m[12 * 24 + 12] == 255);
    assert(m[8 * 24 + 11] == 255 && m[8 * 24 + 12] == 255);
    assert(m[8 * 24 + 9] == 0 && m[8 * 24 + 14] == 0);
    assert(m[2 * 24 + 2] == 0);                         /* corner empty */

    /* Search at 24: the ring (r=7 about 11,11) is inked, its middle is empty. */
    assert(ui_glyph_raster(SEARCH, 24, m));
    assert(m[11 * 24 + 11] == 0);
    assert(m[11 * 24 + 4] > 200 || m[11 * 24 + 3] > 200);

    /* Filled heart: solid in the middle, empty in the corners. */
    assert(ui_glyph_raster(HEART, 24, m));
    assert(m[12 * 24 + 12] == 255);
    assert(m[1 * 24 + 1] == 0 && m[22 * 24 + 22] == 0);

    /* Every glyph rasterises at the sizes the kit uses and inks something. */
    static const u32 sizes[] = { 14, 16, 17, 20, 22, 24, 34 };
    for (u32 g = 0; g < COUNT; g++)
        for (u32 i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
            u32 s = sizes[i], ink = 0;
            assert(ui_glyph_raster(g, s, m));
            for (u32 k = 0; k < s * s; k++) ink += m[k];
            assert(ink > 255u * s / 2);
        }
    /* App icons: 23 of them; transparent outside the rounded corner, opaque
     * gradient inside, glyph colour at the glyph's centre stroke. */
    static u32 px[UI_ICON_MAX_SIZE * UI_ICON_MAX_SIZE];
    assert(ui_icon_raster_count() == 23);
    assert(!ui_icon_raster(23, 52, px));
    assert(!ui_icon_raster(0, UI_ICON_MAX_SIZE + 1, px));
    for (u32 i = 0; i < 23; i++) {
        assert(ui_icon_raster(i, 52, px));
        assert((px[0] >> 24) == 0);                      /* corner cut */
        assert((px[26 * 52 + 2] >> 24) == 255);         /* left edge, mid-height */
    }
    /* Terminal: 7CF0B5 prompt stroke passes through (8,8)->(10,12) in 24 units. */
    assert(ui_icon_raster(1, 48, px));
    {
        /* 26 px glyph at offset 11: (9,11) units -> pixel (21,23) */
        u32 c = px[23 * 48 + 21] & 0xFFFFFF;
        assert(((c >> 8) & 0xFF) > 0xC0);                /* strong green */
    }
    /* Gradient runs top-left light to bottom-right dark (Files: 6E9BFF -> 3F6DF2). */
    assert(ui_icon_raster(2, 52, px));
    assert(((px[8 * 52 + 20] >> 8) & 0xFF) > ((px[44 * 52 + 32] >> 8) & 0xFF));
    puts("ui_glyph OK");
    return 0;
}
