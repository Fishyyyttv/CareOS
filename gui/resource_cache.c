/* =============================================================================
 * CareOS gui/resource_cache.c  --  decode-once image cache and .cra mounting
 *
 * A flat table, linear scan. 96 entries is small enough that a scan costs less
 * than the hash it would replace, and it keeps the whole thing free of the
 * bucket bookkeeping that a kernel data structure has to get exactly right.
 *
 * Eviction is LRU with two hard rules:
 *   - nothing touched during the CURRENT tick is evictable, which is what makes
 *     "fetch it and draw it" safe without any refcounting at the call site;
 *   - nothing with refs > 0 is evictable, for the callers that do hold on.
 * If every entry is protected the insert simply fails and the caller gets an
 * uncached image_t back -- slower, never wrong.
 * ============================================================================= */

#include "kernel.h"
#include "gui.h"
#include "image.h"
#include "resource_cache.h"

typedef struct {
    char     key[RES_KEY_MAX];
    image_t *img;         /* NULL for a remembered failure */
    u32      bytes;       /* pixel storage charged to the budget */
    u32      last_use;    /* timer tick of the last hit */
    bool     used;
} res_entry_t;

static res_entry_t cache[RES_MAX_ENTRIES];
static u32         cache_bytes  = 0;
static u32         cache_used   = 0;
static u32         stat_hits    = 0;
static u32         stat_misses  = 0;
static bool        cache_ready  = false;

void res_cache_init(void) {
    kmemset(cache, 0, sizeof(cache));
    cache_bytes = 0;
    cache_used  = 0;
    stat_hits   = 0;
    stat_misses = 0;
    cache_ready = true;
    serial_write("[res] image cache ready\n");
}

/* -- Key construction ------------------------------------------------------
 * Native size is keyed by the bare path; a resample appends "@WxH". Built by
 * hand rather than through ksprintf so a key can never be truncated into a
 * collision with a different path. Returns false if it would not fit. */
static bool key_make(char *out, const char *path, u32 w, u32 h, bool sized) {
    u32 n = 0;
    while (path[n] && n < RES_KEY_MAX - 1u) { out[n] = path[n]; n++; }
    if (path[n]) return false;                   /* path itself too long */

    if (sized) {
        char num[12];
        out[n++] = '@';
        kutoa(w, num, 10);
        for (u32 i = 0; num[i]; i++) { if (n >= RES_KEY_MAX - 1u) return false; out[n++] = num[i]; }
        if (n >= RES_KEY_MAX - 1u) return false;
        out[n++] = 'x';
        kutoa(h, num, 10);
        for (u32 i = 0; num[i]; i++) { if (n >= RES_KEY_MAX - 1u) return false; out[n++] = num[i]; }
    }
    out[n] = '\0';
    return true;
}

static res_entry_t *cache_find(const char *key) {
    for (u32 i = 0; i < RES_MAX_ENTRIES; i++)
        if (cache[i].used && kstrcmp(cache[i].key, key) == 0) return &cache[i];
    return NULL;
}

static u32 image_bytes(const image_t *img) {
    if (!img || !img->pixels) return 0;
    /* Borrowed pixels live in the kernel image or in a VFS node we do not own;
     * they are not ours to account for and freeing the entry will not return
     * them. Only owned allocations count against the budget. */
    if (!(img->flags & IMG_OWNS_PIXELS)) return 0;
    return img->width * img->height * 4u;
}

static void entry_drop(res_entry_t *e) {
    if (e->img) {
        e->img->flags &= ~IMG_CACHED;   /* hand ownership back so free works */
        image_free(e->img);
    }
    cache_bytes -= e->bytes;
    cache_used--;
    kmemset(e, 0, sizeof(*e));
}

/* Evict the least recently used unprotected entry. Returns false when every
 * entry is pinned or was touched this tick. */
static bool cache_evict_one(void) {
    u32          now  = timer_get_ticks();
    res_entry_t *best = NULL;

    for (u32 i = 0; i < RES_MAX_ENTRIES; i++) {
        res_entry_t *e = &cache[i];
        if (!e->used) continue;
        if (e->last_use == now) continue;            /* in use this frame */
        if (e->img && e->img->refs > 0) continue;    /* pinned by res_retain */
        if (!best || e->last_use < best->last_use) best = e;
    }
    if (!best) return false;
    entry_drop(best);
    return true;
}

/* Take ownership of `img` under `key`. On failure the caller keeps `img` and
 * remains responsible for freeing it. */
static res_entry_t *cache_insert(const char *key, image_t *img) {
    u32 bytes = image_bytes(img);

    while (cache_bytes + bytes > RES_BUDGET_BYTES) {
        if (!cache_evict_one()) break;   /* over budget but nothing to give */
    }
    if (cache_used >= RES_MAX_ENTRIES && !cache_evict_one()) return NULL;

    for (u32 i = 0; i < RES_MAX_ENTRIES; i++) {
        res_entry_t *e = &cache[i];
        if (e->used) continue;

        kstrncpy(e->key, key, RES_KEY_MAX - 1);
        e->img      = img;
        e->bytes    = bytes;
        e->last_use = timer_get_ticks();
        e->used     = true;
        if (img) img->flags |= IMG_CACHED;
        cache_bytes += bytes;
        cache_used++;
        return e;
    }
    return NULL;
}

/* -- Lookup ---------------------------------------------------------------- */

/* Resolve a path to an image from either backing store.
 *
 * A real VFS file is tried FIRST so dropping a file at the same path overrides
 * a baked asset -- the archive is the default artwork, not the authority. When
 * no file exists the mounted archives answer, and image_load_mem() is told it
 * may borrow: archive bytes live in .rodata and outlive everything. */
static image_t *load_resource(const char *path) {
    fs_node_t *node = vfs_resolve_path(path);
    if (node && node->type == FS_FILE && node->size > 0) return image_load(path);

    const u8 *data;
    u32       len;
    if (res_archive_find(path, &data, &len)) return image_load_mem(data, len, false);
    return NULL;
}

image_t *res_image(const char *path) {
    if (!cache_ready || !path || !path[0]) return NULL;

    char key[RES_KEY_MAX];
    if (!key_make(key, path, 0, 0, false)) return NULL;

    res_entry_t *e = cache_find(key);
    if (e) {
        e->last_use = timer_get_ticks();
        stat_hits++;
        return e->img;                 /* may be NULL: a remembered failure */
    }
    stat_misses++;

    image_t *img = load_resource(path);
    if (!cache_insert(key, img)) {
        /* Table full of pinned entries. Returning the uncached image would
         * leak it -- nobody downstream frees these -- so drop it and report
         * the miss. The next frame retries, which is the right behaviour for
         * a condition that is by definition temporary. */
        image_free(img);
        return NULL;
    }
    return img;
}

image_t *res_image_sized(const char *path, u32 w, u32 h) {
    if (!cache_ready || !path || !path[0] || w == 0 || h == 0) return NULL;

    char key[RES_KEY_MAX];
    if (!key_make(key, path, w, h, true)) return NULL;

    res_entry_t *e = cache_find(key);
    if (e) {
        e->last_use = timer_get_ticks();
        stat_hits++;
        return e->img;
    }
    stat_misses++;

    /* Goes through res_image() so the native decode is shared across sizes. */
    image_t *native = res_image(path);
    if (!native) {
        cache_insert(key, NULL);        /* remember the failure at this size */
        return NULL;
    }
    if (native->width == w && native->height == h) {
        /* Already the right size. Hand back the native entry WITHOUT filing it
         * under the sized key as well: two keys pointing at one image_t means
         * res_forget() would drop both and free it twice.
         *
         * The cost is NOT small, so do not reach here in a loop. Because
         * nothing is cached under the sized key, every repeat call misses and
         * pays a full table scan before landing on the res_image() hit above.
         * Callers that already know the asset's dimensions -- gui/icon.c does,
         * from the theme's size directories -- should call res_image() directly
         * instead. This path is for genuinely unknown-size images that happen
         * to match, not for the common case. */
        return native;
    }

    image_t *scaled = image_scaled(native, w, h);
    if (!scaled) return NULL;
    if (!cache_insert(key, scaled)) { image_free(scaled); return NULL; }
    return scaled;
}

/* -- Pinning --------------------------------------------------------------- */

void res_retain(image_t *img)  { if (img) img->refs++; }
void res_release(image_t *img) { if (img && img->refs > 0) img->refs--; }

/* -- Invalidation ---------------------------------------------------------- */

void res_forget(const char *path) {
    if (!cache_ready) return;
    if (!path) { res_cache_flush(); return; }

    u32 plen = (u32)kstrlen(path);
    for (u32 i = 0; i < RES_MAX_ENTRIES; i++) {
        res_entry_t *e = &cache[i];
        if (!e->used) continue;
        /* Drop the bare path and every "path@WxH" variant derived from it. */
        if (kstrncmp(e->key, path, plen) != 0) continue;
        if (e->key[plen] != '\0' && e->key[plen] != '@') continue;
        if (e->img) e->img->refs = 0;   /* forced: the bytes are stale anyway */
        entry_drop(e);
    }
}

void res_cache_flush(void) {
    if (!cache_ready) return;
    for (u32 i = 0; i < RES_MAX_ENTRIES; i++) {
        if (!cache[i].used) continue;
        if (cache[i].img) cache[i].img->refs = 0;
        entry_drop(&cache[i]);
    }
    cache_bytes = 0;
    cache_used  = 0;
}

u32 res_cache_count(void)  { return cache_used;  }
u32 res_cache_bytes(void)  { return cache_bytes; }
u32 res_cache_hits(void)   { return stat_hits;   }
u32 res_cache_misses(void) { return stat_misses; }

/* -- .cra archive mounting ------------------------------------------------- */

static inline u32 rd32le(const u8 *p) {
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

typedef struct {
    const u8 *blob;
    u32       total;
    u32       count;
    char      prefix[64];     /* absolute, no trailing '/' */
    u32       prefix_len;
} res_archive_t;

static res_archive_t archives[RES_MAX_ARCHIVES];
static u32           archive_count = 0;

int res_mount_archive(const u8 *blob, u32 len, const char *mount_dir) {
    if (!blob || len < CRA_HEADER_BYTES || !mount_dir || !mount_dir[0]) return -1;
    if (mount_dir[0] != '/') return -1;
    if (archive_count >= RES_MAX_ARCHIVES) return -1;
    if (blob[0] != CRA_MAGIC0 || blob[1] != CRA_MAGIC1 ||
        blob[2] != CRA_MAGIC2 || blob[3] != CRA_MAGIC3) {
        serial_write("[res] archive: bad magic\n");
        return -1;
    }

    u32 count = rd32le(blob + 4);
    u32 total = rd32le(blob + 8);
    if (total > len) {
        serial_write("[res] archive: truncated\n");
        return -1;
    }
    if ((u64)CRA_HEADER_BYTES + (u64)count * CRA_ENTRY_BYTES > (u64)total) {
        serial_write("[res] archive: bad entry table\n");
        return -1;
    }

    res_archive_t *a = &archives[archive_count];
    a->blob  = blob;
    a->total = total;
    a->count = count;
    kstrncpy(a->prefix, mount_dir, sizeof(a->prefix) - 1);
    a->prefix_len = (u32)kstrlen(a->prefix);
    /* Normalise "/system/" to "/system" so the join below is unambiguous. */
    while (a->prefix_len > 1u && a->prefix[a->prefix_len - 1u] == '/')
        a->prefix[--a->prefix_len] = '\0';
    archive_count++;

    char nb[12];
    kutoa(count, nb, 10);
    serial_write("[res] mounted archive: ");
    serial_write(nb);
    serial_write(" entries under ");
    serial_write(a->prefix);
    serial_write("\n");
    return (int)count;
}

u32 res_archive_count(void) { return archive_count; }

u32 res_archive_entries(void) {
    u32 n = 0;
    for (u32 i = 0; i < archive_count; i++) n += archives[i].count;
    return n;
}

/* Linear scan over the entry table.
 *
 * A binary search would need the baker to guarantee a sorted table, and a
 * hand-made archive that broke that promise would fail silently and invisibly.
 * The scan runs only on a resource_cache MISS, and misses are themselves cached
 * (including failures), so the whole boot performs a few hundred of these once
 * and never again. Correctness that cannot be accidentally broken is worth more
 * here than a lookup that is already off the hot path. */
bool res_archive_find(const char *path, const u8 **data, u32 *len) {
    if (!path || path[0] != '/') return false;

    for (u32 ai = 0; ai < archive_count; ai++) {
        res_archive_t *a = &archives[ai];

        if (kstrncmp(path, a->prefix, a->prefix_len) != 0) continue;
        const char *rel = path + a->prefix_len;
        if (*rel != '/') continue;          /* "/systemx" must not match "/system" */
        rel++;
        if (!*rel) continue;

        for (u32 i = 0; i < a->count; i++) {
            const u8 *rec = a->blob + CRA_HEADER_BYTES + (size_t)i * CRA_ENTRY_BYTES;
            const char *name = (const char *)rec;

            /* Names are NUL-padded to CRA_NAME_MAX and may fill it exactly, so
             * compare bounded and require the relative path to end where the
             * name does. */
            u32 n = 0;
            while (n < CRA_NAME_MAX && name[n] && rel[n] && name[n] == rel[n]) n++;
            bool name_end = (n == CRA_NAME_MAX) || (name[n] == '\0');
            if (!name_end || rel[n] != '\0') continue;

            u32 offset = rd32le(rec + CRA_NAME_MAX);
            u32 length = rd32le(rec + CRA_NAME_MAX + 4u);
            if (length == 0 || (u64)offset + length > (u64)a->total) return false;

            *data = a->blob + offset;
            *len  = length;
            return true;
        }
    }
    return false;
}
