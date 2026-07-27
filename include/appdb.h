#ifndef APPDB_H
#define APPDB_H

/* =============================================================================
 * CareOS - include/appdb.h — Application Registry ("app database")
 *
 * The appdb is the single table of "what applications exist on this machine".
 * It holds two kinds of entry, in one uniform record layout:
 *
 *   builtin  — an app compiled into the kernel (Terminal, Files, Settings...).
 *              exec is "builtin:<token>"; the GUI maps <token> to an app_id_t
 *              and dispatches through wm_open(). Seeded at appdb_init() from a
 *              table in appdb.c, so a missing or truncated apps.db can never
 *              leave the desktop without its own apps.
 *
 *   package  — an app installed by carepkg from a .care bundle.
 *              exec is an absolute VFS path, e.g. "/apps/browser/main".
 *              Registered by carepkg at install time, dropped at remove time.
 *
 * On-disk form is /system/apps.db, a plain-text record file in the same style
 * as the rest of CareOS's config (no JSON, no binary blob, editable with the
 * built-in editor):
 *
 *   # comment
 *   APP_START
 *   id=terminal
 *   name=Terminal
 *   version=1.0
 *   exec=builtin:terminal
 *   icon=terminal
 *   category=Productivity
 *   permissions=gui,fs.read,fs.write
 *   APP_END
 *
 * Unknown keys are skipped rather than rejected, so a newer CareOS can add
 * fields without an older kernel choking on the file.
 *
 * NOTE ON PERSISTENCE: like /var/pkg/installed.db, /system lives in the
 * in-memory VFS. apps.db survives for the life of a boot and is rebuilt from
 * the builtin table + carepkg's own database on the next one. Point APPDB_PATH
 * at an ext2-backed or /home path to make it durable across reboots.
 *
 * NOTE ON permissions: the field is recorded and displayed, not enforced.
 * There is no permission check anywhere in the kernel yet — treat it as
 * metadata that a future sandbox will consume.
 * ============================================================================= */

#include "kernel.h"

#define APPDB_PATH          "/system/apps.db"
#define APPDB_MAX_ENTRIES   64

/* Field widths. Sized to hold everything carepkg's manifest can produce:
 * name[32] + '/apps/' + exec[FS_PATH_MAX] must fit in APPDB_EXEC_MAX. */
#define APPDB_ID_MAX        32
#define APPDB_NAME_MAX      64
#define APPDB_VERSION_MAX   16
#define APPDB_EXEC_MAX      128
#define APPDB_ICON_MAX      128
#define APPDB_CATEGORY_MAX  32
#define APPDB_PERM_MAX      128

/* exec prefix marking a kernel-resident app. Everything else is a VFS path. */
#define APPDB_BUILTIN_PREFIX "builtin:"

typedef struct {
    char id[APPDB_ID_MAX];              /* unique key, lowercase, no spaces */
    char name[APPDB_NAME_MAX];          /* human label shown in the launcher */
    char version[APPDB_VERSION_MAX];
    char exec[APPDB_EXEC_MAX];          /* "builtin:<token>" or "/apps/x/main" */
    char icon[APPDB_ICON_MAX];          /* icon token (see gfx_draw_icon set)  */
    char category[APPDB_CATEGORY_MAX];
    char permissions[APPDB_PERM_MAX];   /* comma list: gui,net,fs.read,fs.write */
} app_entry_t;

/* -- Lifecycle ------------------------------------------------------------- */

/* Load /system/apps.db, then seed any missing builtin entries and write the
 * file back. Must run after vfs_init() and before carepkg_init(). */
void appdb_init(void);

/* Serialize the whole table to APPDB_PATH. 0 on success, -1 on failure.
 * Creating /system if absent is part of the job. */
int  appdb_save(void);

/* -- Mutation -------------------------------------------------------------- */

/* Add entry, or overwrite the existing record with the same id (so reinstalling
 * a package upgrades its registration instead of duplicating it). Does NOT
 * write to disk — call appdb_save() when a batch of changes is complete.
 * Returns 0 on success, -1 on a bad/empty id, a full table, or an attempt to
 * register a package under a builtin's id — otherwise a .care bundle named
 * "terminal" would repoint the Terminal tile at its own script. */
int  appdb_register(const app_entry_t *entry);

/* Drop the entry with this id. Does not touch /apps/<id> on disk; deleting the
 * files is carepkg's job. Returns 0 on success, -1 if no such id or if the id
 * names a builtin (those are re-seeded every boot; removing one is meaningless
 * and would let `carepkg remove terminal` unregister the real Terminal). */
int  appdb_remove(const char *id);

/* -- Queries ---------------------------------------------------------------
 * Returned pointers alias the internal table and are invalidated by the next
 * appdb_register() or appdb_remove(). Copy out anything you intend to keep. */

const app_entry_t *appdb_find(const char *id);
u32                appdb_count(void);
const app_entry_t *appdb_get(u32 index);   /* NULL when index >= appdb_count() */

/* -- Dispatch --------------------------------------------------------------- */

/* For "builtin:terminal" returns "terminal"; returns NULL for package apps.
 * The GUI owns the token -> app_id_t mapping; appdb.c never sees gui.h. */
const char *appdb_builtin_token(const app_entry_t *entry);

/* Run a package app's exec through the Care language interpreter. Output is
 * copied into out (may be NULL). Returns 0 on success, -1 if the entry is a
 * builtin (the caller dispatches those itself) or the exec file is missing. */
int appdb_launch(const app_entry_t *entry, char *out, u32 out_max);

#endif /* APPDB_H */
