/* Guard is CAREOS_-prefixed, like gui/font.h, and for the same reason: this
 * codebase uses short uppercase names for real objects. A plain ICON_H guard
 * silently deleted `static i32 ICON_H` in gui/wm.c the moment this header was
 * included there. */
#ifndef CAREOS_ICON_H
#define CAREOS_ICON_H

/* =============================================================================
 * CareOS gui/icon.h  --  named icons, theme lookup, and the vector fallback
 *
 * appdb's `icon` field has always held a short token ("terminal", "browser",
 * "generic") that the launcher mapped to a hand-drawn vector glyph. Packages
 * now want to ship a real picture instead. Rather than split the field into two
 * or version the app database, one rule covers both:
 *
 *     icon starting with '/'  ->  a VFS path to an image file
 *     anything else           ->  a name looked up in the icon theme
 *
 * A theme name is resolved to /system/icons/<size>/<name>.cri, preferring the
 * exact baked size, then the next size UP (downscaling keeps edges), then the
 * next size down. Flat /system/icons/<name>.{cri,bmp,tga} is checked last, so
 * dropping a single browser.bmp into that directory works with no theme
 * involved -- which is what someone experimenting will try first.
 *
 * If nothing resolves, icon_draw() falls back to gfx_draw_icon()'s vector glyph
 * for the supplied app_id_t. That fallback is the reason this system can be
 * merged before a single asset exists: the desktop looks exactly as it does
 * today until the icon theme is baked in, and improves the moment it is.
 * ============================================================================= */

#include "kernel.h"
#include "gui.h"
#include "image.h"

/* Where res_mount_archive() publishes the baked theme, and where a user can
 * drop loose files. Both are ordinary VFS paths -- nothing here is special. */
#define ICON_THEME_DIR   "/system/icons"
#define WALLPAPER_DIR    "/system/wallpapers"
#define ICON_NAME_MAX    64u

/* Sizes tools/gen-icons.py bakes. Lookup snaps to one of these before falling
 * back to a resample, so a request for 40px uses the 48px art, not the 16px. */
#define ICON_SIZE_COUNT  5

/* -- Startup ---------------------------------------------------------------
 * Brings up the resource cache and publishes the baked asset archive into the
 * VFS. Called from gui_init(); safe to call when no archive was linked in.
 * Implemented in gui/resource_boot.c. */
void     resources_init(void);

/* -- Lookup ----------------------------------------------------------------
 * Returns a cache-owned image at exactly `size` square, or NULL if the name
 * resolves to nothing. Valid for the current frame without retaining; see
 * resource_cache.h for the ownership rules. */
image_t *icon_lookup(const char *icon, u32 size);

/* True when `icon` resolves to a real image. Used by the package manager UI to
 * show whether an installed app shipped artwork. */
bool     icon_exists(const char *icon);

/* -- Drawing ---------------------------------------------------------------
 * Draws the resolved image at size*size, or gfx_draw_icon(fallback, ...) in
 * `fallback_color` when there is none. This is the call the launcher, taskbar
 * and desktop all make; none of them need to know which path was taken. */
void     icon_draw(const char *icon, i32 x, i32 y, i32 size,
                   app_id_t fallback, u32 fallback_color);

/* As above but draws the alpha channel in a flat colour. For symbolic glyphs
 * that should follow the theme accent rather than carry their own palette. */
void     icon_draw_tinted(const char *icon, i32 x, i32 y, i32 size,
                          app_id_t fallback, u32 color);

/* -- Drawing a built-in app ------------------------------------------------
 * The desktop sidebar, the taskbar and window titlebars all identify an app by
 * app_id_t, not by an icon name -- they have no appdb entry in hand. This is
 * the bridge: it maps the id to its theme name, draws the themed artwork if it
 * resolves, and otherwise calls gfx_draw_icon(app, ..., fallback_color) exactly
 * as those call sites used to.
 *
 * fallback_color is ONLY used for the vector fallback. Themed icons carry their
 * own palette and are drawn unmodified; tinting a full-colour Papirus glyph to
 * a single theme colour would throw away the reason for having it. */
void     icon_draw_app(app_id_t app, i32 x, i32 y, i32 size, u32 fallback_color);

/* "terminal" for APP_TERMINAL, and so on. NULL for APP_NONE or an id with no
 * theme name. The returned pointer is a string literal. */
const char *icon_name_for_app(app_id_t app);

/* -- Wallpaper -------------------------------------------------------------
 * Six slots, matching the six swatches the Settings picker has always drawn.
 * The active one comes from settings_get()->wallpaper, so choosing a different
 * background in Settings changes the desktop with no further plumbing.
 *
 * Only the ACTIVE background is ever decoded at full size, and it is pinned --
 * a 1280x720 decode is 3.5 MB and far too expensive to redo on eviction.
 * Switching slots releases the previous one. Six full-size backgrounds resident
 * at once would be ~21 MB against an 8 MB cache budget, which is why the picker
 * uses separate baked thumbnails instead of scaling the real thing. */
#define WALLPAPER_SLOTS 6

/* Cover-fits the active slot's image over the rectangle. Returns false when
 * that slot has no image and no default is installed, which is the caller's cue
 * to draw a procedural gradient instead. */
bool     wallpaper_draw(i32 x, i32 y, i32 w, i32 h);

/* The picker's preview: a pre-baked 132x64 image for `slot`, or NULL when that
 * slot ships no artwork. Cache-owned; safe to draw in the same frame. */
image_t *wallpaper_thumb(u32 slot);

#endif /* CAREOS_ICON_H */
