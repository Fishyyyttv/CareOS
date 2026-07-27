/* =============================================================================
 * CareOS gui/icon.c  --  name -> image resolution over the resource cache
 *
 * Everything here is string juggling on top of res_image_sized(). The only
 * subtlety is the ORDER of candidate paths, which decides what a user sees when
 * two sources could both answer:
 *
 *   1. exact baked size          /system/icons/48/browser.cri
 *   2. next size up, downscaled  /system/icons/64/browser.cri
 *   3. next size down, upscaled  /system/icons/32/browser.cri
 *   4. loose file, any size      /system/icons/browser.{cri,bmp,tga}
 *
 * Baked art wins over a loose drop-in because the theme is what ships; a loose
 * file is the escape hatch for names the theme has never heard of. Both lose to
 * an absolute path in the appdb entry, which is an explicit choice by whoever
 * installed the package.
 *
 * Misses are cheap. res_image_sized() remembers failures, so an icon name that
 * resolves to nothing costs four string compares per frame, not four VFS walks.
 * ============================================================================= */

#include "kernel.h"
#include "gui.h"
#include "image.h"
#include "resource_cache.h"
#include "icon.h"

/* Ascending, and it must stay that way -- size_candidates() walks it in both
 * directions from the snap point. */
static const u32 icon_sizes[ICON_SIZE_COUNT] = { 16u, 24u, 32u, 48u, 64u };

/* Extensions tried when a name or path carries none, cheapest first. */
static const char *icon_exts[] = { ".cri", ".bmp", ".tga" };
#define ICON_EXT_COUNT 3

/* -- Small path builder ----------------------------------------------------
 * Appends to `out`, tracking a running length so a long name truncates into a
 * failed lookup rather than a buffer overrun. Every builder below is written as
 * append calls for that reason. */
typedef struct { char *buf; u32 len, max; bool ok; } pathbuf_t;

static void pb_init(pathbuf_t *p, char *buf, u32 max) {
    p->buf = buf; p->len = 0; p->max = max; p->ok = true; buf[0] = '\0';
}
static void pb_str(pathbuf_t *p, const char *s) {
    if (!p->ok) return;
    while (*s) {
        if (p->len >= p->max - 1u) { p->ok = false; return; }
        p->buf[p->len++] = *s++;
    }
    p->buf[p->len] = '\0';
}
static void pb_u32(pathbuf_t *p, u32 v) {
    char num[12];
    kutoa(v, num, 10);
    pb_str(p, num);
}

/* True when the path already ends in one of the extensions we understand. */
static bool has_known_ext(const char *path) {
    const char *dot = kstrrchr(path, '.');
    if (!dot) return false;
    for (u32 i = 0; i < ICON_EXT_COUNT; i++)
        if (kstrcmp(dot, icon_exts[i]) == 0) return true;
    return false;
}

/* Fill `out` with the baked sizes to try, best first: the exact snap, then
 * everything larger ascending, then everything smaller descending. Downscaling
 * a 64px glyph to 40px beats upscaling a 32px one, which is why up comes first.
 * Returns how many entries were written. */
static u32 size_candidates(u32 want, u32 *out) {
    /* Snap to the smallest baked size that is >= want; if want is bigger than
     * everything we have, snap to the largest. */
    u32 snap = ICON_SIZE_COUNT - 1u;
    for (u32 i = 0; i < ICON_SIZE_COUNT; i++) {
        if (icon_sizes[i] >= want) { snap = i; break; }
    }

    u32 n = 0;
    out[n++] = icon_sizes[snap];
    for (u32 i = snap + 1u; i < ICON_SIZE_COUNT; i++) out[n++] = icon_sizes[i];
    for (i32 i = (i32)snap - 1; i >= 0; i--)          out[n++] = icon_sizes[i];
    return n;
}

/* -- Resolution ------------------------------------------------------------ */

/* `native` says the file at `path` is already size*size, so the resample layer
 * can be skipped entirely.
 *
 * This matters more than it looks. res_image_sized() deliberately does not file
 * a 1:1 result under its "path@WxH" key (two keys on one image_t would make
 * res_forget() free it twice), so asking it for a size the art already is
 * produces a MISS every single call -- a full cache scan per icon per frame,
 * for ever. The baked sizes exist precisely so the common request is 1:1, which
 * made that the normal path rather than the rare one: `res` reported a 49% miss
 * rate on a desktop that was drawing nothing but exact-size icons. */
static image_t *try_path(const char *path, u32 size, bool native) {
    return native ? res_image(path) : res_image_sized(path, size, size);
}

/* An absolute path from an appdb entry or a .care manifest. Its dimensions are
 * unknown, so it always goes through the resample layer. */
static image_t *lookup_path(const char *path, u32 size) {
    if (has_known_ext(path)) return try_path(path, size, false);

    /* No extension: the manifest said "icon=/apps/browser/icon" and meant
     * whichever of our formats it actually shipped. */
    char buf[FS_PATH_MAX];
    for (u32 i = 0; i < ICON_EXT_COUNT; i++) {
        pathbuf_t p; pb_init(&p, buf, sizeof(buf));
        pb_str(&p, path);
        pb_str(&p, icon_exts[i]);
        if (!p.ok) continue;
        image_t *img = try_path(buf, size, false);
        if (img) return img;
    }
    return NULL;
}

/* A bare theme name such as "terminal". */
static image_t *lookup_name(const char *name, u32 size) {
    char buf[FS_PATH_MAX];
    u32  sizes[ICON_SIZE_COUNT];
    u32  n = size_candidates(size, sizes);

    for (u32 i = 0; i < n; i++) {
        pathbuf_t p; pb_init(&p, buf, sizeof(buf));
        pb_str(&p, ICON_THEME_DIR "/");
        pb_u32(&p, sizes[i]);
        pb_str(&p, "/");
        pb_str(&p, name);
        pb_str(&p, ".cri");
        if (!p.ok) return NULL;                 /* name too long: no candidate fits */
        /* The size directory names the art's own dimensions, so when it equals
         * the request the file is already exactly right -- take the direct
         * path. This is the hot case: every baked size is a size some part of
         * the UI actually asks for. */
        image_t *img = try_path(buf, size, sizes[i] == size);
        if (img) return img;
    }

    /* Loose drop-in, any format, sitting directly in the theme directory. Its
     * dimensions are whatever the user saved, so it must be resampled. */
    for (u32 i = 0; i < ICON_EXT_COUNT; i++) {
        pathbuf_t p; pb_init(&p, buf, sizeof(buf));
        pb_str(&p, ICON_THEME_DIR "/");
        pb_str(&p, name);
        pb_str(&p, icon_exts[i]);
        if (!p.ok) return NULL;
        image_t *img = try_path(buf, size, false);
        if (img) return img;
    }
    return NULL;
}

image_t *icon_lookup(const char *icon, u32 size) {
    if (!icon || !icon[0] || size == 0 || size > IMAGE_MAX_DIM) return NULL;
    return (icon[0] == '/') ? lookup_path(icon, size) : lookup_name(icon, size);
}

bool icon_exists(const char *icon) {
    /* 48 is the launcher's tile size, so this warms the cache entry the UI will
     * ask for next rather than creating a throwaway one. */
    return icon_lookup(icon, 48u) != NULL;
}

/* -- Drawing --------------------------------------------------------------- */

void icon_draw(const char *icon, i32 x, i32 y, i32 size,
               app_id_t fallback, u32 fallback_color) {
    if (size <= 0) return;

    image_t *img = icon_lookup(icon, (u32)size);
    if (img) { gfx_draw_image(img, x, y); return; }

    gfx_draw_icon(fallback, x, y, size, fallback_color);
}

void icon_draw_tinted(const char *icon, i32 x, i32 y, i32 size,
                      app_id_t fallback, u32 color) {
    if (size <= 0) return;

    image_t *img = icon_lookup(icon, (u32)size);
    if (img) { gfx_draw_image_tinted(img, x, y, color); return; }

    gfx_draw_icon(fallback, x, y, size, color);
}

/* -- Built-in app -> theme name --------------------------------------------
 * These names are the keys of ICON_MAP in tools/gen-icons.py. Changing one here
 * without changing it there silently drops that app back to its vector glyph,
 * which is why both lists spell the names out rather than deriving them.
 *
 * This duplicates part of launcher_icons[] in gui/launcher.c, but in the other
 * direction: the launcher maps a manifest token to an app_id_t to pick a
 * fallback glyph, while this maps an app_id_t to a token to pick artwork. The
 * two tables answer different questions and neither can be derived from the
 * other -- several tokens ("generic", "calc") map to an app whose own name
 * differs. */
const char *icon_name_for_app(app_id_t app) {
    switch (app) {
    case APP_TERMINAL: return "terminal";
    case APP_NOTES:    return "notes";
    case APP_FILES:    return "files";
    case APP_SYSMON:   return "sysmon";
    case APP_CALC:     return "calc";
    case APP_ABOUT:    return "about";
    case APP_HELP:     return "help";
    case APP_BROWSER:  return "browser";
    case APP_SETTINGS: return "settings";
    case APP_PKGMGR:   return "packages";
    case APP_EDITOR:   return "editor";
    case APP_PAINT:    return "paint";
    case APP_CLOCK:    return "clock";
    case APP_NETMON:   return "netmon";
    case APP_USERS:    return "users";
    case APP_MAZE:     return "maze";
    case APP_3D:       return "3d";
    case APP_DOOM:     return "doom";
    case APP_NONE:
    default:           return NULL;
    }
}

void icon_draw_app(app_id_t app, i32 x, i32 y, i32 size, u32 fallback_color) {
    if (size <= 0) return;

    const char *name = icon_name_for_app(app);
    if (name) {
        image_t *img = icon_lookup(name, (u32)size);
        if (img) { gfx_draw_image(img, x, y); return; }
    }
    gfx_draw_icon(app, x, y, size, fallback_color);
}

/* -- Wallpaper -------------------------------------------------------------
 * Held at native size and cover-fitted at draw time, so a resolution change
 * (vesa_set_mode) needs no reload. Pinned while active: a 1280x720 decode is
 * 3.5 MB and re-doing it because a 48px icon wanted the slot would be a bad
 * trade in both directions. */
static image_t *wallpaper_img  = NULL;
static i32      wallpaper_slot = -1;   /* which slot wallpaper_img holds; -1 = none loaded */

/* "/system/wallpapers/<slot>.cri", or the "-thumb" variant. */
static bool wallpaper_path(char *buf, u32 max, u32 slot, bool thumb) {
    pathbuf_t p; pb_init(&p, buf, max);
    pb_str(&p, WALLPAPER_DIR "/");
    pb_u32(&p, slot);
    if (thumb) pb_str(&p, "-thumb");
    pb_str(&p, ".cri");
    return p.ok;
}

static image_t *wallpaper_load(u32 slot) {
    char buf[FS_PATH_MAX];

    if (wallpaper_path(buf, sizeof(buf), slot, false)) {
        image_t *img = res_image(buf);
        if (img) return img;
    }

    /* Slot has no artwork of its own. Try a loose "default" file first -- that
     * is the drop-in override a user can write into /system/wallpapers without
     * rebuilding anything. */
    for (u32 i = 0; i < ICON_EXT_COUNT; i++) {
        pathbuf_t p; pb_init(&p, buf, sizeof(buf));
        pb_str(&p, WALLPAPER_DIR "/default");
        pb_str(&p, icon_exts[i]);
        if (!p.ok) continue;
        image_t *img = res_image(buf);
        if (img) return img;
    }

    /* Then slot 0, the shipped default, so a build that bakes only one
     * background shows it in every slot rather than dropping five of the six to
     * the procedural gradient. Slot 0 used to be duplicated into the archive
     * under the name "default.cri" purely to serve this fallback; at 2.1 MB a
     * copy that was a real cost in the kernel image, so it points here instead. */
    if (slot != 0 && wallpaper_path(buf, sizeof(buf), 0, false))
        return res_image(buf);

    return NULL;
}

bool wallpaper_draw(i32 x, i32 y, i32 w, i32 h) {
    const careos_settings_t *cfg = settings_get();
    i32 slot = cfg ? (i32)cfg->wallpaper : 0;
    if (slot < 0 || slot >= WALLPAPER_SLOTS) slot = 0;

    if (slot != wallpaper_slot) {
        /* Unpin the outgoing background before loading the new one, so only one
         * full-size image is ever held. The old one becomes an ordinary cache
         * entry and is evicted in turn. */
        if (wallpaper_img) res_release(wallpaper_img);
        wallpaper_img  = wallpaper_load((u32)slot);
        wallpaper_slot = slot;
        if (wallpaper_img) res_retain(wallpaper_img);
    }
    if (!wallpaper_img) return false;

    gfx_draw_image_cover(wallpaper_img, x, y, w, h);
    return true;
}

image_t *wallpaper_thumb(u32 slot) {
    if (slot >= WALLPAPER_SLOTS) return NULL;
    char buf[FS_PATH_MAX];
    if (!wallpaper_path(buf, sizeof(buf), slot, true)) return NULL;
    /* Baked at exactly the swatch size, so res_image() -- not res_image_sized()
     * -- is the right call; see the note above try_path(). */
    return res_image(buf);
}

/* -- `res` diagnostic ------------------------------------------------------
 * Lives here rather than in resource_cache.c because the useful line is the
 * last one, and only this file can produce it: an archive can mount perfectly
 * and still resolve nothing if the entry names and the lookup paths disagree.
 * Since that miss is invisible -- a vector glyph is drawn instead, and it looks
 * deliberate -- this is the fastest way to answer "is the theme actually being
 * used?" without rebuilding anything.
 *
 * Renders to a string, not to a device. CareOS has two command dispatchers with
 * two different output sinks: shell/shell.c writes through terminal_write() to
 * the VGA console, while the GUI terminal in apps/app_terminal.c appends to a
 * window buffer with win_append(). A function that picked one would silently do
 * nothing in the other -- which is exactly what happened when this first wrote
 * through terminal_write(): typing `res` in the desktop terminal answered
 * "command not found", and adding it there would have printed to a console
 * nobody was looking at. */
static void sb_str(char *out, u32 max, u32 *n, const char *s) {
    while (*s && *n < max - 1u) out[(*n)++] = *s++;
    out[*n] = '\0';
}

static void sb_u32(char *out, u32 max, u32 *n, u32 v) {
    char b[12];
    kutoa(v, b, 10);
    sb_str(out, max, n, b);
}

void res_status_text(char *out, u32 max) {
    if (!out || max == 0) return;
    u32 n = 0;
    out[0] = '\0';

    sb_str(out, max, &n, "CareOS graphics resources\n  archives   ");
    sb_u32(out, max, &n, res_archive_count());
    sb_str(out, max, &n, " mounted, ");
    sb_u32(out, max, &n, res_archive_entries());
    sb_str(out, max, &n, " entries\n  cache      ");
    sb_u32(out, max, &n, res_cache_count());
    sb_str(out, max, &n, " images, ");
    sb_u32(out, max, &n, res_cache_bytes() / 1024u);
    sb_str(out, max, &n, " KiB, ");
    sb_u32(out, max, &n, res_cache_hits());
    sb_str(out, max, &n, " hits, ");
    sb_u32(out, max, &n, res_cache_misses());
    sb_str(out, max, &n, " misses\n  wallpaper  ");
    sb_str(out, max, &n, wallpaper_img ? "installed\n" : "none (procedural)\n");

    image_t *probe = icon_lookup("terminal", 48);
    if (probe) {
        sb_str(out, max, &n, "  theme      live, terminal@48 -> ");
        sb_u32(out, max, &n, probe->width);
        sb_str(out, max, &n, "x");
        sb_u32(out, max, &n, probe->height);
        sb_str(out, max, &n, "\n");
    } else {
        sb_str(out, max, &n,
               "  theme      NOT RESOLVING -- drawing vector glyphs\n"
               "             check names against " ICON_THEME_DIR "\n");
    }
}
