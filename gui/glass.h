/* gui/glass.h -- the pure parts of the UI kit's glass (visual-system spec §6):
 * the half-resolution blur, the backdrop signature that decides when a cached
 * blur is stale, and the slow-frame guard. Host-tested; gui/ui.c owns the
 * cache itself. */
#ifndef CAREOS_GLASS_H
#define CAREOS_GLASS_H
#include "pure.h"

/* Blur src (w x h, row stride `stride` pixels, 0xRRGGBB) into dst (w x h,
 * packed). Downsample x2 (box), three separable box passes of radius
 * blur/6 at half resolution (~Gaussian), bilinear upsample. `scratch` needs
 * glass_scratch_words(w, h) u32s. */
u32  glass_scratch_words(u32 w, u32 h);
void glass_blur(const u32 *src, u32 stride, u32 w, u32 h, u32 blur, u32 *dst, u32 *scratch);

/* Signature of a backdrop region: every 4th pixel of every 3rd row plus the
 * region's last row and column, so a moved window edge or a changed wallpaper
 * shows up while a sub-sample change under heavy blur may be missed. */
u32  glass_signature(const u32 *src, u32 stride, u32 w, u32 h);

/* Frame-time guard: three consecutive frames over 33 ms switch glass to
 * tint-only; it comes back after 1 s with no frame over 20 ms. */
typedef struct { u32 slow_run; bool tint_only; u32 calm_since; } glass_guard_t;
void glass_guard_frame(glass_guard_t *g, u32 frame_ms, u32 now_ms);

#endif
