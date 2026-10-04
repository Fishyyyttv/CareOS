/* gui/care_wallpaper.h -- the v10 "Care Night" / "Care Day" backgrounds.
 *
 * Rendered procedurally from the Desktop artboard (soft colour blobs plus a
 * faint ring) at the exact screen size, so it needs no archive space and the
 * theme switch flips Night <-> Day for free. Integer only; pure, host-tested. */
#ifndef CAREOS_CARE_WALLPAPER_H
#define CAREOS_CARE_WALLPAPER_H
#include "pure.h"

/* Settings wallpaper index of the Care background. 0..5 are the baked photo
 * slots (WALLPAPER_SLOTS in icon.h); the Care one sits after them so saved
 * photo choices keep their meaning. */
#define WALLPAPER_CARE 6u

/* Fill dst (w x h, row stride `stride` pixels, 0xRRGGBB) with the Care
 * background for the given theme. Layout scales from the 1440x900 artboard. */
void care_wallpaper_render(u32 *dst, u32 w, u32 h, u32 stride, bool dark);

#endif
