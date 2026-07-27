#ifndef CAREOS_RESOURCE_CACHE_H
#define CAREOS_RESOURCE_CACHE_H

/* =============================================================================
 * CareOS gui/resource_cache.h  --  decode-once image cache
 *
 * The launcher redraws every tile every frame. Without a cache that is one
 * vfs_resolve_path() + one decode + one resample per icon per frame -- at 16
 * tiles and 60 fps, roughly a thousand decodes a second to draw a picture that
 * never changes. The cache turns all of that into a string compare.
 *
 * Ownership model, stated plainly because it is the part that bites:
 *
 *   - The cache OWNS every image_t it hands out. They carry IMG_CACHED, so
 *     image_free() on one is a no-op and cannot corrupt the table.
 *   - A returned pointer is valid until the next eviction. Drawing it in the
 *     same frame you fetched it is always safe: nothing touched during the
 *     current tick is ever evicted.
 *   - Keeping a pointer ACROSS frames requires res_retain(), paired with
 *     res_release(). Retained images are pinned and never evicted. The
 *     wallpaper does this; the launcher does not need to.
 *
 * Failures are cached too. A missing icon is looked up as often as a present
 * one, and re-walking the VFS 16 times a frame to rediscover that a file is
 * still absent is the same cost as the problem we set out to solve.
 * ============================================================================= */

#include "kernel.h"
#include "image.h"

/* Key is a VFS path, optionally suffixed "@WxH" for a resampled variant. */
#define RES_KEY_MAX        96u
#define RES_MAX_ENTRIES    96u

/* Soft ceiling on decoded pixel data. Icons baked by tools/gen-icons.py are
 * borrowed straight out of the kernel image and count as zero, so this budget
 * is really about wallpapers and anything a package installs.
 *
 * Sized against the LARGEST SINGLE IMAGE, not the total. A 1920x1080 wallpaper
 * is 8.1 MB decoded, and the active one is pinned by res_retain(). At the
 * original 8 MiB budget one wallpaper exceeded the whole allowance on its own
 * and could never be evicted, so every icon insert afterwards ran the eviction
 * loop, threw out other icons to make room that pinning made unreclaimable, and
 * re-decoded them next frame. The cache would have quietly become a
 * thrash-per-frame decoder while still drawing correctly.
 *
 * 32 MiB against a 192 MiB kernel heap leaves room for a 4K wallpaper plus
 * every icon size the desktop asks for. Raise it, do not lower it, if bigger
 * artwork ever ships. */
#define RES_BUDGET_BYTES   (32u * 1024u * 1024u)

/* -- Lifecycle -------------------------------------------------------------
 * Call once from gui_init(), after gfx_init() and after the VFS is up. */
void res_cache_init(void);

/* -- Lookup ----------------------------------------------------------------
 * res_image()       load and cache `path` at its native size.
 * res_image_sized() as above, then cache a nearest-neighbour resample to
 *                   exactly w*h. The native image stays cached too, so several
 *                   sizes of one asset share a single decode.
 * Both return NULL when the file is missing or undecodable, and remember that
 * so the next call is a string compare rather than another VFS walk. */
image_t *res_image(const char *path);
image_t *res_image_sized(const char *path, u32 w, u32 h);

/* -- Pinning ---------------------------------------------------------------
 * Only needed to hold a pointer across frames. Unbalanced retains leak cache
 * slots rather than memory: the image stays pinned until the next flush. */
void res_retain(image_t *img);
void res_release(image_t *img);

/* -- Invalidation ----------------------------------------------------------
 * res_forget() after overwriting a file so the next draw sees the new bytes;
 * carepkg calls it when a package replaces its icon. Passing NULL forgets
 * everything, which is what a theme switch wants. */
void res_forget(const char *path);
void res_cache_flush(void);

/* -- Introspection (the `res` shell command) ------------------------------- */
u32 res_cache_count(void);
u32 res_cache_bytes(void);
u32 res_cache_hits(void);
u32 res_cache_misses(void);

/* Mounted archives, and the total entries they advertise. */
u32 res_archive_count(void);
u32 res_archive_entries(void);

/* -- .cra archive mounting -------------------------------------------------
 * A .cra bundles many .cri images into one blob so the icon theme can be linked
 * into the kernel with a single objcopy.
 *
 * A mounted archive is a LOOKUP SOURCE, not a set of files. res_image() checks
 * the VFS first and falls back to the mounted archives, so an entry named
 * "icons/48/browser.cri" under mount point "/system" answers a request for
 * "/system/icons/48/browser.cri" without any fs_node_t existing.
 *
 * That indirection is not an optimisation, it is a hard requirement. The whole
 * VFS is a fixed pool of FS_MAX_FILES + FS_MAX_DIRS (128) nodes shared by the
 * entire OS. Publishing a real node per icon consumed every remaining slot at
 * boot -- 145 icons against roughly 40 free nodes -- which silently truncated
 * the theme AND left nothing for package installs or user files. Reading them
 * out of the archive costs zero nodes and zero pixel storage.
 *
 * The VFS is checked first so a file dropped at the same path still overrides
 * the baked asset; the archive is the default, not the authority.
 *
 * `mount_dir` is the absolute path the entry names hang off. Returns the number
 * of entries the archive advertises, or -1 if it is malformed.
 *
 * Header, all little-endian:
 *    0  4  magic "CRA1"
 *    4  4  entry_count
 *    8  4  total archive size in bytes
 *   12  4  reserved, 0
 *   16  .. entry_count records of CRA_ENTRY_BYTES:
 *            0  48  name, NUL-padded relative path
 *           48   4  offset from start of archive
 *           52   4  length in bytes
 *           56   2  width   (informational; the .cri header is authoritative)
 *           58   2  height
 * Payloads follow the table, each 4-byte aligned so image_decode_cri() can
 * borrow them without a misaligned u32 load.
 *
 * 48 bytes of name is enough for "icons/48/accessories-text-editor.cri" with
 * room to spare, which is what lets ONE archive carry the whole /system tree --
 * icons, wallpapers and anything added later -- mounted with a single call. */
#define CRA_MAGIC0        'C'
#define CRA_MAGIC1        'R'
#define CRA_MAGIC2        'A'
#define CRA_MAGIC3        '1'
#define CRA_HEADER_BYTES  16u
#define CRA_ENTRY_BYTES   60u
#define CRA_NAME_MAX      48u

/* How many archives may be mounted at once. One is the icon theme; the second
 * exists so a future user theme can be layered over it without a redesign. */
#define RES_MAX_ARCHIVES  4u

int res_mount_archive(const u8 *blob, u32 len, const char *mount_dir);

/* Resolve an absolute path against the mounted archives. Returns false when no
 * archive carries it. On success *data points into the archive itself and stays
 * valid forever, which is what lets image_decode_cri() borrow it. */
bool res_archive_find(const char *path, const u8 **data, u32 *len);

#endif /* CAREOS_RESOURCE_CACHE_H */
