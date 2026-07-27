/* =============================================================================
 * CareOS - kernel/appdb.c — Application Registry
 *
 * See include/appdb.h for the record layout and the on-disk format.
 *
 * The table is a flat, compacted array: appdb_remove() shifts the tail down so
 * appdb_get(i) is a plain index and appdb_count() is exact. That costs a memmove
 * on uninstall — a few kilobytes, once, on a user action — and buys a launcher
 * loop with no holes to skip and no separate "installed" flag to forget. It is
 * the opposite trade from carepkg's registry, which keeps tombstones because it
 * must remember that a package *was* installed.
 * ============================================================================= */

#include "appdb.h"

/* Worst-case bytes for one serialized record: every field at its maximum plus
 * the key names, the APP_START/APP_END lines and the blank separator. */
#define APPDB_RECORD_MAX  640u
#define APPDB_SERIAL_MAX  (APPDB_MAX_ENTRIES * APPDB_RECORD_MAX + 256u)

static app_entry_t appdb_table[APPDB_MAX_ENTRIES];
static u32         appdb_used = 0;

/* ── Builtin app table ──────────────────────────────────────────────────────
 * Kernel-resident apps. Seeded on every boot so a deleted or truncated
 * apps.db degrades to "packages forgotten", never to "empty launcher".
 * The icon column must stay inside the token set gfx_draw_icon() understands
 * (see launcher.c's appdb_icon_app map) or the tile renders blank. */
typedef struct {
    const char *id;
    const char *name;
    const char *category;
    const char *icon;
    const char *permissions;
} appdb_builtin_t;

static const appdb_builtin_t appdb_builtins[] = {
    { "terminal",   "Terminal",   "Productivity", "terminal", "gui,fs.read,fs.write" },
    { "files",      "Files",      "Productivity", "files",    "gui,fs.read,fs.write" },
    { "editor",     "Editor",     "Productivity", "editor",   "gui,fs.read,fs.write" },
    { "browser",    "Browser",    "Internet",     "browser",  "gui,net"              },
    { "netmon",     "NetMon",     "Internet",     "netmon",   "gui,net"              },
    { "settings",   "Settings",   "System",       "settings", "gui,fs.write"         },
    { "sysmon",     "Monitor",    "System",       "sysmon",   "gui"                  },
    { "users",      "Users",      "System",       "users",    "gui,fs.write"         },
    { "packages",   "Packages",   "System",       "packages", "gui,fs.write"         },
    { "calculator", "Calculator", "Utilities",    "calc",     "gui"                  },
    { "clock",      "Clock",      "Utilities",    "clock",    "gui"                  },
    { "notes",      "Notes",      "Utilities",    "notes",    "gui,fs.write"         },
    { "paint",      "Paint",      "Creative",     "paint",    "gui"                  },
    { "about",      "About",      "CareOS",       "about",    "gui"                  },
};

#define APPDB_BUILTIN_COUNT (sizeof(appdb_builtins) / sizeof(appdb_builtins[0]))

/* ── Helpers ────────────────────────────────────────────────────────────────
 * kstrncpy() only writes a terminator when the source ran out first, so a
 * src at or over the limit leaves dst unterminated. Every field copy here goes
 * through this instead. */
static void appdb_set(char *dst, u32 max, const char *src) {
    u32 i = 0;
    if (!dst || max == 0) return;
    if (src) for (; src[i] && i < max - 1; i++) dst[i] = src[i];
    dst[i] = '\0';
}

static int appdb_index_of(const char *id) {
    if (!id || !id[0]) return -1;
    for (u32 i = 0; i < appdb_used; i++)
        if (kstrcmp(appdb_table[i].id, id) == 0) return (int)i;
    return -1;
}

/* Parse "key=value"; returns true and fills dst when key matches. Same shape
 * as carepkg.c's kv(), kept local so the two parsers stay independent. */
static bool appdb_kv(const char *line, const char *key, char *dst, u32 max) {
    u32 klen = (u32)kstrlen(key);
    if (kstrncmp(line, key, klen) != 0 || line[klen] != '=') return false;
    appdb_set(dst, max, line + klen + 1);
    return true;
}

/* Resolve /system, creating it if this is a fresh boot. */
static fs_node_t *appdb_system_dir(void) {
    fs_node_t *sys = vfs_find(vfs_root(), "system");
    if (!sys) sys = vfs_mkdir(vfs_root(), "system");
    return sys;
}

/* ── Query API ──────────────────────────────────────────────────────────── */
u32 appdb_count(void) { return appdb_used; }

const app_entry_t *appdb_get(u32 index) {
    if (index >= appdb_used) return NULL;
    return &appdb_table[index];
}

const app_entry_t *appdb_find(const char *id) {
    int idx = appdb_index_of(id);
    return (idx < 0) ? NULL : &appdb_table[idx];
}

const char *appdb_builtin_token(const app_entry_t *entry) {
    u32 plen = (u32)kstrlen(APPDB_BUILTIN_PREFIX);
    if (!entry) return NULL;
    if (kstrncmp(entry->exec, APPDB_BUILTIN_PREFIX, plen) != 0) return NULL;
    return entry->exec[plen] ? entry->exec + plen : NULL;
}

/* ── Mutation API ───────────────────────────────────────────────────────── */
int appdb_register(const app_entry_t *entry) {
    if (!entry || entry->id[0] == '\0') return -1;

    int idx = appdb_index_of(entry->id);
    if (idx >= 0 && appdb_builtin_token(&appdb_table[idx])
                 && !appdb_builtin_token(entry)) {
        /* A package must never take over a builtin's id. Without this, a .care
         * bundle named "terminal" would rewrite the Terminal tile's exec to its
         * own script and the real Terminal would become unreachable. */
        serial_write("[appdb] refused: id shadows a builtin: ");
        serial_write(entry->id);
        serial_write("\n");
        return -1;
    }

    if (idx < 0) {
        if (appdb_used >= APPDB_MAX_ENTRIES) {
            serial_write("[appdb] registry full\n");
            return -1;
        }
        idx = (int)appdb_used++;
    }

    app_entry_t *slot = &appdb_table[idx];
    if (slot == entry) return 0;   /* self-register: the kmemset would wipe src */
    kmemset(slot, 0, sizeof(*slot));
    appdb_set(slot->id,          APPDB_ID_MAX,       entry->id);
    appdb_set(slot->name,        APPDB_NAME_MAX,     entry->name[0] ? entry->name : entry->id);
    appdb_set(slot->version,     APPDB_VERSION_MAX,  entry->version[0] ? entry->version : "1.0");
    appdb_set(slot->exec,        APPDB_EXEC_MAX,     entry->exec);
    appdb_set(slot->icon,        APPDB_ICON_MAX,     entry->icon[0] ? entry->icon : "generic");
    appdb_set(slot->category,    APPDB_CATEGORY_MAX, entry->category[0] ? entry->category : "General");
    appdb_set(slot->permissions, APPDB_PERM_MAX,     entry->permissions);
    return 0;
}

/* Drop slot idx and close the gap. Caller has already validated idx. */
static void appdb_remove_at(u32 idx) {
    for (u32 i = idx; i + 1 < appdb_used; i++)
        appdb_table[i] = appdb_table[i + 1];
    appdb_used--;
    kmemset(&appdb_table[appdb_used], 0, sizeof(appdb_table[0]));
}

int appdb_remove(const char *id) {
    int idx = appdb_index_of(id);
    if (idx < 0) return -1;

    /* Builtins are not removable. They are re-seeded every boot, so dropping
     * one only makes it vanish until the next reboot — and `carepkg remove
     * terminal` for a package that never registered must not take the real
     * Terminal down with it. */
    if (appdb_builtin_token(&appdb_table[idx])) return -1;

    appdb_remove_at((u32)idx);
    return 0;
}

/* ── Dispatch ───────────────────────────────────────────────────────────── */
int appdb_launch(const app_entry_t *entry, char *out, u32 out_max) {
    if (!entry) return -1;
    if (appdb_builtin_token(entry)) return -1;   /* caller owns builtins */
    if (entry->exec[0] == '\0') return -1;

    fs_node_t *f = vfs_resolve_path(entry->exec);
    if (!f || f->type != FS_FILE) return -1;

    if (out && out_max) {
        out[0] = '\0';
        return care_lang_exec_buf(vfs_file_str(f), f->size, out, out_max);
    }
    return care_lang_exec(vfs_file_str(f), f->size);
}

/* ── Serialization ──────────────────────────────────────────────────────── */
static u32 appdb_append(char *buf, u32 pos, u32 max, const char *s) {
    while (*s && pos < max - 1) buf[pos++] = *s++;
    buf[pos] = '\0';
    return pos;
}

static u32 appdb_append_kv(char *buf, u32 pos, u32 max,
                           const char *key, const char *value) {
    pos = appdb_append(buf, pos, max, key);
    pos = appdb_append(buf, pos, max, "=");
    pos = appdb_append(buf, pos, max, value);
    return appdb_append(buf, pos, max, "\n");
}

int appdb_save(void) {
    fs_node_t *sys = appdb_system_dir();
    if (!sys) return -1;

    fs_node_t *db = vfs_find(sys, "apps.db");
    if (!db) db = vfs_mkfile(sys, "apps.db");
    if (!db) return -1;

    char *buf = (char*)kmalloc(APPDB_SERIAL_MAX);
    if (!buf) return -1;

    u32 pos = 0;
    buf[0] = '\0';
    pos = appdb_append(buf, pos, APPDB_SERIAL_MAX,
                       "# CareOS application registry v1\n"
                       "# Generated by appdb — edit while the system is down, "
                       "or it will be overwritten on the next install.\n\n");

    for (u32 i = 0; i < appdb_used; i++) {
        if (pos + APPDB_RECORD_MAX >= APPDB_SERIAL_MAX) break;
        const app_entry_t *e = &appdb_table[i];
        pos = appdb_append(buf, pos, APPDB_SERIAL_MAX, "APP_START\n");
        pos = appdb_append_kv(buf, pos, APPDB_SERIAL_MAX, "id",          e->id);
        pos = appdb_append_kv(buf, pos, APPDB_SERIAL_MAX, "name",        e->name);
        pos = appdb_append_kv(buf, pos, APPDB_SERIAL_MAX, "version",     e->version);
        pos = appdb_append_kv(buf, pos, APPDB_SERIAL_MAX, "exec",        e->exec);
        pos = appdb_append_kv(buf, pos, APPDB_SERIAL_MAX, "icon",        e->icon);
        pos = appdb_append_kv(buf, pos, APPDB_SERIAL_MAX, "category",    e->category);
        pos = appdb_append_kv(buf, pos, APPDB_SERIAL_MAX, "permissions", e->permissions);
        pos = appdb_append(buf, pos, APPDB_SERIAL_MAX, "APP_END\n\n");
    }

    int rc = (vfs_write(db, buf, pos) < 0) ? -1 : 0;
    kfree(buf);
    return rc;
}

/* Line-oriented record parser. state 0 = between records, 1 = inside one.
 * A record only lands in the table when it reaches APP_END with a non-empty
 * id, so a file truncated mid-record loses that record and nothing else. */
static void appdb_load(void) {
    fs_node_t *db = vfs_resolve_path(APPDB_PATH);
    if (!db || db->type != FS_FILE || db->size == 0) return;

    const char *p   = vfs_file_str(db);
    u32         len = db->size;

    app_entry_t cur;
    kmemset(&cur, 0, sizeof(cur));

    int  state = 0;
    char line[512];
    u32  li = 0;
    u32  loaded = 0;

    for (u32 i = 0; i <= len; i++) {
        char c = (i < len) ? p[i] : '\n';
        if (c == '\r') continue;
        if (c != '\n') {
            if (li < sizeof(line) - 1) line[li++] = c;
            continue;
        }

        line[li] = '\0';
        li = 0;
        if (line[0] == '#' || line[0] == '\0') continue;

        if (state == 0) {
            if (kstrcmp(line, "APP_START") == 0) {
                kmemset(&cur, 0, sizeof(cur));
                state = 1;
            }
            continue;
        }

        if (kstrcmp(line, "APP_END") == 0) {
            state = 0;
            if (cur.id[0] && appdb_register(&cur) == 0) loaded++;
            continue;
        }

        /* Unknown keys match nothing and are dropped — forward compatibility. */
        appdb_kv(line, "id",          cur.id,          APPDB_ID_MAX);
        appdb_kv(line, "name",        cur.name,        APPDB_NAME_MAX);
        appdb_kv(line, "version",     cur.version,     APPDB_VERSION_MAX);
        appdb_kv(line, "exec",        cur.exec,        APPDB_EXEC_MAX);
        appdb_kv(line, "icon",        cur.icon,        APPDB_ICON_MAX);
        appdb_kv(line, "category",    cur.category,    APPDB_CATEGORY_MAX);
        appdb_kv(line, "permissions", cur.permissions, APPDB_PERM_MAX);
    }

    char n[12];
    kutoa(loaded, n, 10);
    serial_write("[appdb] loaded ");
    serial_write(n);
    serial_write(" entries from ");
    serial_write(APPDB_PATH);
    serial_write("\n");
}

/* ── Init ───────────────────────────────────────────────────────────────── */
static void appdb_seed_builtins(void) {
    for (u32 i = 0; i < APPDB_BUILTIN_COUNT; i++) {
        const appdb_builtin_t *b = &appdb_builtins[i];

        /* An existing builtin record wins: a user may have re-categorised or
         * renamed a builtin in apps.db and re-seeding must not stomp that.
         * A record under a builtin id that is *not* a builtin came from a stale
         * apps.db written before the shadowing guard existed — reclaim it. */
        int idx = appdb_index_of(b->id);
        if (idx >= 0) {
            if (appdb_builtin_token(&appdb_table[idx])) continue;
            appdb_remove_at((u32)idx);
        }

        app_entry_t e;
        kmemset(&e, 0, sizeof(e));
        appdb_set(e.id,          APPDB_ID_MAX,       b->id);
        appdb_set(e.name,        APPDB_NAME_MAX,     b->name);
        appdb_set(e.version,     APPDB_VERSION_MAX,  "1.0");
        appdb_set(e.exec,        APPDB_EXEC_MAX,     APPDB_BUILTIN_PREFIX);
        kstrcat(e.exec, b->id);
        appdb_set(e.icon,        APPDB_ICON_MAX,     b->icon);
        appdb_set(e.category,    APPDB_CATEGORY_MAX, b->category);
        appdb_set(e.permissions, APPDB_PERM_MAX,     b->permissions);
        appdb_register(&e);
    }
}

void appdb_init(void) {
    kmemset(appdb_table, 0, sizeof(appdb_table));
    appdb_used = 0;

    appdb_system_dir();     /* /system must exist before load or save */
    appdb_load();
    appdb_seed_builtins();
    appdb_save();

    serial_write("[appdb] application registry ready\n");
}
