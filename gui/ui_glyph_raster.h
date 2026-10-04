/* gui/ui_glyph_raster.h -- rasterise a v10 system glyph to an 8-bit coverage
 * mask. Pure (host-tested); gui/ui_glyph.c caches masks and blends them. */
#ifndef CAREOS_UI_GLYPH_RASTER_H
#define CAREOS_UI_GLYPH_RASTER_H
#include "pure.h"

#define UI_GLYPH_MAX_SIZE 72
#define UI_ICON_MAX_SIZE  128

u32  ui_glyph_raster_count(void);
/* Fill mask[size*size] with coverage 0..255. 2-unit round-cap strokes in the
 * 24-unit design viewbox, scaled to `size`. False for a bad glyph or size. */
bool ui_glyph_raster(u32 glyph, u32 size, u8 *mask);

u32  ui_icon_raster_count(void);
/* An app icon (ui_icon_t) as the AppIcon artboard draws it: 28 % radius tile,
 * 160deg gradient, inset top highlight and bottom shade, centred stroke glyph.
 * argb[size*size], alpha = edge coverage. The drop shadow is the caller's. */
bool ui_icon_raster(u32 icon, u32 size, u32 *argb);

#endif
