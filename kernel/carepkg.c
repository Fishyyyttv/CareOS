/* =============================================================================
 * CareOS - kernel/carepkg.c  — CareOS Package Manager v4
 *
 * .care package format (replaces .cpk, though .cpk still accepted)
 * ─────────────────────────────────────────────────────────────────
 * A .care file is a plain-text bundle stored as a VFS file.
 *
 *   CARE 1.0
 *   name=<appname>
 *   version=<semver>
 *   description=<one-line>
 *   author=<author>
 *   exec=<filename inside package>
 *   icon=<icon-id>   (one of: terminal, notes, editor, files, calc, browser, generic)
 *   category=<Utilities|Development|Games|System|Network>
 *   permissions=<comma list: net,fs.read,fs.write,gui>
 *   ---FILES---
 *   FILE <relative-path>
 *   <content line 1>
 *   <content line 2>
 *   ...
 *   ---ENDFILE---
 *   ---END---
 *
 * Install flow:
 *   1. Parse manifest header
 *   2. Create /apps/<name>/ directory
 *   3. Write each FILE section into /apps/<name>/<path>
 *   4. Register in pkg_registry
 *   5. Launcher auto-picks up newly installed apps
 *
 * Uninstall flow:
 *   1. Remove /apps/<name>/ from VFS
 *   2. Mark registry entry as uninstalled
 * ============================================================================= */

#include "kernel.h"

/* ── Package registry ──────────────────────────────────────────────────────── */
#define MAX_PACKAGES 64

typedef struct {
    bool  installed;
    char  name[32];
    char  version[16];
    char  description[128];
    char  author[48];
    char  exec[FS_PATH_MAX];
    char  icon[32];
    char  category[32];
    char  permissions[128];
    char  deps[128];           /* comma-separated dependency names */
    u32   install_tick;
    char  install_path[FS_PATH_MAX];  /* /apps/<name> */
} care_pkg_t;

static care_pkg_t registry[MAX_PACKAGES];
static u32        pkg_count = 0;

/* Forward declarations */
static void carepkg_db_save(void);
static void carepkg_db_load(void);

/* ── Helpers ────────────────────────────────────────────────────────────────── */
static care_pkg_t *find_pkg(const char *name) {
    for (u32 i = 0; i < pkg_count; i++)
        if (registry[i].installed && kstrcmp(registry[i].name, name) == 0)
            return &registry[i];
    return NULL;
}

/* Parse "key=value"; returns true and fills dst if key matches */
static bool kv(const char *line, const char *key, char *dst, u32 max) {
    u32 klen = kstrlen(key);
    if (kstrncmp(line, key, klen) != 0 || line[klen] != '=') return false;
    kstrncpy(dst, line + klen + 1, max - 1);
    return true;
}

/* Ensure /apps/<name> directory exists; return it */
static fs_node_t *ensure_app_dir(const char *name) {
    fs_node_t *apps = vfs_find(vfs_root(), "apps");
    if (!apps) apps = vfs_mkdir(vfs_root(), "apps");
    fs_node_t *adir = vfs_find(apps, name);
    if (!adir) adir = vfs_mkdir(apps, name);
    return adir;
}

/* ── Parse & install a .care/.cpk file ──────────────────────────────────────── */
static int pkg_install_node(fs_node_t *node) {
    if (!node || node->type != FS_FILE) return -1;
    if (node->size == 0) return -1;

    if (pkg_count >= MAX_PACKAGES) {
        serial_write("[carepkg] registry full\n");
        return -1;
    }

    care_pkg_t tmp;
    kmemset(&tmp, 0, sizeof(tmp));

    /* Simple state machine: 0=pre-header, 1=header, 2=file-body */
    int state = 0;
    char cur_fname[64] = "";
    char *file_content = (char*)kmalloc(FS_FILE_DATA_MAX);
    if (!file_content) return -1;
    u32  fc_len = 0;
    fs_node_t *app_dir = NULL;

    /* Parse line by line */
    char line[256]; u32 li = 0;
    const char *buf = node->data;
    u32 len = node->size;

    int rc = 0;
    for (u32 i = 0; i <= len; i++) {
        char c = (i < len) ? buf[i] : '\n';
        if (c == '\r') continue;
        if (c == '\n') {
            line[li] = '\0';
            li = 0;

            if (state == 0) {
                if (kstrcmp(line,"CARE 1.0")==0 || kstrcmp(line,"CAREPKG 1.0")==0)
                    state = 1;
            } else if (state == 1) {
                if (kstrcmp(line,"---FILES---")==0) {
                    /* Validate header before proceeding */
                    if (tmp.name[0]=='\0') { rc = -1; goto _out; }
                    if (find_pkg(tmp.name)) {
                        terminal_write("carepkg: already installed: ");
                        terminal_writeln(tmp.name);
                        rc = -1; goto _out;
                    }
                    /* Check dependencies */
                    if (tmp.deps[0]) {
                        char dep_copy[128];
                        kstrncpy(dep_copy, tmp.deps, sizeof(dep_copy) - 1);
                        char *d = dep_copy;
                        while (*d) {
                            char dep_name[32]; u32 dn = 0;
                            while (*d && *d != ',' && dn < 31) dep_name[dn++] = *d++;
                            dep_name[dn] = '\0';
                            if (*d == ',') d++;
                            if (dep_name[0] && !find_pkg(dep_name)) {
                                terminal_write("carepkg: missing dependency: ");
                                terminal_writeln(dep_name);
                                rc = -1; goto _out;
                            }
                        }
                    }
                    app_dir = ensure_app_dir(tmp.name);
                    kstrcpy(tmp.install_path, "/apps/");
                    kstrcat(tmp.install_path, tmp.name);
                    state = 2;
                } else if (kstrcmp(line,"---END---")==0) {
                    break;
                } else {
                    kv(line,"name",       tmp.name,       sizeof(tmp.name));
                    kv(line,"version",    tmp.version,    sizeof(tmp.version));
                    kv(line,"description",tmp.description,sizeof(tmp.description));
                    kv(line,"author",     tmp.author,     sizeof(tmp.author));
                    kv(line,"exec",       tmp.exec,       sizeof(tmp.exec));
                    kv(line,"icon",       tmp.icon,       sizeof(tmp.icon));
                    kv(line,"category",   tmp.category,   sizeof(tmp.category));
                    kv(line,"permissions",tmp.permissions,sizeof(tmp.permissions));
                    kv(line,"deps",       tmp.deps,       sizeof(tmp.deps));
                }
            } else if (state == 2) {
                if (kstrncmp(line,"FILE ",5)==0) {
                    /* Save previous file if any */
                    if (cur_fname[0] && app_dir) {
                        fs_node_t *f = vfs_find(app_dir, cur_fname);
                        if (!f) f = vfs_mkfile(app_dir, cur_fname);
                        if (f) vfs_write(f, file_content, fc_len);
                    }
                    kstrncpy(cur_fname, line+5, 63);
                    file_content[0] = '\0';
                    fc_len = 0;
                } else if (kstrcmp(line,"---ENDFILE---")==0) {
                    /* Save current file */
                    if (cur_fname[0] && app_dir) {
                        fs_node_t *f = vfs_find(app_dir, cur_fname);
                        if (!f) f = vfs_mkfile(app_dir, cur_fname);
                        if (f) vfs_write(f, file_content, fc_len);
                    }
                    cur_fname[0] = '\0';
                    file_content[0] = '\0';
                    fc_len = 0;
                } else if (kstrcmp(line,"---END---")==0) {
                    /* Save last file */
                    if (cur_fname[0] && app_dir) {
                        fs_node_t *f = vfs_find(app_dir, cur_fname);
                        if (!f) f = vfs_mkfile(app_dir, cur_fname);
                        if (f) vfs_write(f, file_content, fc_len);
                    }
                    break;
                } else {
                    /* Accumulate file content */
                    if (fc_len + li + 2 < FS_FILE_DATA_MAX) {
                        if (fc_len > 0) file_content[fc_len++] = '\n';
                        kmemcpy(file_content + fc_len, line, kstrlen(line));
                        fc_len += kstrlen(line);
                        file_content[fc_len] = '\0';
                    }
                }
            }
        } else if (li < 255) {
            line[li++] = c;
        }
    }

    if (tmp.name[0] == '\0') { rc = -1; goto _out; }

    /* Write manifest file */
    if (app_dir) {
        fs_node_t *mf = vfs_find(app_dir, "manifest.care");
        if (!mf) mf = vfs_mkfile(app_dir, "manifest.care");
        if (mf) vfs_write(mf, node->data, node->size);
    }

    /* Register */
    tmp.installed    = true;
    tmp.install_tick = timer_get_ticks();
    registry[pkg_count++] = tmp;

    terminal_write("[care] Installed: ");
    terminal_write(tmp.name);
    terminal_write(" v");
    terminal_writeln(tmp.version);
    carepkg_db_save();

_out:
    kfree(file_content);
    return rc;
}

static int pkg_install(const char *path) {
    fs_node_t *node = vfs_resolve_path(path);
    if (!node || node->type != FS_FILE) {
        terminal_write("carepkg: not found: "); terminal_writeln(path);
        return -1;
    }
    return pkg_install_node(node);
}

/* ── Uninstall ────────────────────────────────────────────────────────────── */
static int pkg_remove(const char *name) {
    care_pkg_t *e = find_pkg(name);
    if (!e) {
        terminal_write("carepkg: not installed: "); terminal_writeln(name);
        return -1;
    }
    /* Remove /apps/<name> */
    fs_node_t *node = vfs_resolve_path(e->install_path);
    if (node) vfs_delete(node);
    e->installed = false;
    terminal_write("[care] Removed: "); terminal_writeln(name);
    carepkg_db_save();
    return 0;
}

/* ── List installed ───────────────────────────────────────────────────────── */
static void pkg_list(void) {
    u32 cnt = 0;
    terminal_writeln("Installed .care packages:");
    terminal_writeln("NAME             VER      CATEGORY    DESCRIPTION");
    terminal_writeln("──────────────── ──────── ─────────── ────────────────────────");
    for (u32 i = 0; i < pkg_count; i++) {
        if (!registry[i].installed) continue;
        char buf[128]; kstrcpy(buf, registry[i].name);
        u32 l=kstrlen(buf); while(l<17){buf[l++]=' ';} buf[l]='\0';
        kstrcat(buf, registry[i].version);
        l=kstrlen(buf); while(l<26){buf[l++]=' ';} buf[l]='\0';
        kstrcat(buf, registry[i].category[0]?registry[i].category:"General");
        l=kstrlen(buf); while(l<38){buf[l++]=' ';} buf[l]='\0';
        kstrcat(buf, registry[i].description);
        terminal_writeln(buf);
        cnt++;
    }
    if (!cnt) terminal_writeln("  (none)");
}

/* ── Info ─────────────────────────────────────────────────────────────────── */
static void pkg_info(const char *name) {
    care_pkg_t *e = find_pkg(name);
    if (!e) { terminal_write("carepkg: not found: "); terminal_writeln(name); return; }
    terminal_write("Name:        "); terminal_writeln(e->name);
    terminal_write("Version:     "); terminal_writeln(e->version);
    terminal_write("Description: "); terminal_writeln(e->description);
    terminal_write("Author:      "); terminal_writeln(e->author);
    terminal_write("Category:    "); terminal_writeln(e->category[0]?e->category:"General");
    terminal_write("Installed:   "); terminal_writeln(e->install_path);
    terminal_write("Exec:        "); terminal_writeln(e->exec);
    terminal_write("Permissions: "); terminal_writeln(e->permissions);
    terminal_write("Deps:        "); terminal_writeln(e->deps[0] ? e->deps : "(none)");
}

/* ── Create a .care file template ────────────────────────────────────────── */
static void pkg_create(const char *name, const char *version) {
    fs_node_t *tmp = vfs_find(vfs_root(), "tmp");
    if (!tmp) tmp = vfs_mkdir(vfs_root(), "tmp");

    char fname[48]; kstrcpy(fname, name); kstrcat(fname, ".care");
    fs_node_t *f = vfs_find(tmp, fname);
    if (!f) f = vfs_mkfile(tmp, fname);
    if (!f) { terminal_writeln("carepkg: failed to create file"); return; }

    char content[1024];
    kstrcpy(content, "CARE 1.0\n");
    kstrcat(content, "name="); kstrcat(content, name); kstrcat(content, "\n");
    kstrcat(content, "version="); kstrcat(content, version[0]?version:"1.0.0"); kstrcat(content, "\n");
    kstrcat(content, "description=My CareOS application\n");
    kstrcat(content, "author=You\n");
    kstrcat(content, "exec=main\n");
    kstrcat(content, "icon=generic\n");
    kstrcat(content, "category=Utilities\n");
    kstrcat(content, "permissions=fs.read\n");
    kstrcat(content, "---FILES---\n");
    kstrcat(content, "FILE main\n");
    kstrcat(content, "echo Hello from "); kstrcat(content, name); kstrcat(content, "\n");
    kstrcat(content, "---ENDFILE---\n");
    kstrcat(content, "---END---\n");
    vfs_write(f, content, kstrlen(content));

    terminal_write("[care] Template created: /tmp/");
    terminal_writeln(fname);
}

/* ── Persistent DB: /var/pkg/installed.db ──────────────────────────────────
 * Format (one line per package, pipe-delimited):
 *   name|version|description|author|exec|icon|category|permissions|deps|install_path
 */
#define DB_PATH "/var/pkg/installed.db"

static void carepkg_db_save(void) {
    fs_node_t *pkg_dir = vfs_resolve_path("/var/pkg");
    if (!pkg_dir) {
        fs_node_t *var = vfs_find(vfs_root(), "var");
        if (!var) var = vfs_mkdir(vfs_root(), "var");
        if (var) pkg_dir = vfs_find(var, "pkg");
        if (!pkg_dir && var) pkg_dir = vfs_mkdir(var, "pkg");
    }
    if (!pkg_dir) return;

    fs_node_t *db = vfs_find(pkg_dir, "installed.db");
    if (!db) db = vfs_mkfile(pkg_dir, "installed.db");
    if (!db) return;

    char *buf = (char*)kmalloc(FS_FILE_DATA_MAX);
    if (!buf) return;
    u32  pos = 0;
    kstrncpy(buf, "# CareOS package database v1\n", FS_FILE_DATA_MAX - 1);
    pos = (u32)kstrlen(buf);

    for (u32 i = 0; i < pkg_count && pos < FS_FILE_DATA_MAX - 256; i++) {
        if (!registry[i].installed) continue;
        care_pkg_t *e = &registry[i];
        /* Append: name|version|desc|author|exec|icon|category|perms|deps|path\n */
        const char *fields[] = { e->name, e->version, e->description, e->author,
                                  e->exec, e->icon, e->category, e->permissions,
                                  e->deps, e->install_path };
        for (int f = 0; f < 10 && pos < FS_FILE_DATA_MAX - 64; f++) {
            kstrncpy(buf + pos, fields[f], 127);
            pos += kstrlen(buf + pos);
            buf[pos++] = (f < 9) ? '|' : '\n';
        }
    }
    buf[pos] = '\0';
    vfs_write(db, buf, pos);
    kfree(buf);
}

static void carepkg_db_load(void) {
    fs_node_t *db = vfs_resolve_path(DB_PATH);
    if (!db || db->type != FS_FILE || db->size == 0) return;

    const char *p = db->data;
    u32 len = db->size;
    char line[512];
    u32 li = 0;

    for (u32 i = 0; i <= len; i++) {
        char c = (i < len) ? p[i] : '\n';
        if (c == '\r') continue;
        if (c == '\n') {
            line[li] = '\0'; li = 0;
            if (line[0] == '#' || line[0] == '\0') continue;
            if (pkg_count >= MAX_PACKAGES) break;

            care_pkg_t e; kmemset(&e, 0, sizeof(e));
            e.installed = true;
            /* Split by '|' into 10 fields */
            char *fields[10] = { e.name, e.version, e.description, e.author,
                                  e.exec, e.icon, e.category, e.permissions,
                                  e.deps, e.install_path };
            u32 sizes[10]    = { 32, 16, 128, 48, FS_PATH_MAX, 32, 32, 128, 128, FS_PATH_MAX };
            char *cur = line;
            for (int f = 0; f < 10; f++) {
                char *pipe = cur;
                while (*pipe && *pipe != '|') pipe++;
                u32 flen = (u32)(pipe - cur);
                if (flen >= sizes[f]) flen = sizes[f] - 1;
                kmemcpy(fields[f], cur, flen);
                fields[f][flen] = '\0';
                cur = *pipe ? pipe + 1 : pipe;
            }
            if (e.name[0] && !find_pkg(e.name))
                registry[pkg_count++] = e;
        } else if (li < 511) {
            line[li++] = c;
        }
    }
    serial_write("[carepkg] DB loaded\n");
}

/* ── Init: register /apps dir, install demo packages ─────────────────────── */
void carepkg_init(void) {
    kmemset(registry, 0, sizeof(registry));
    pkg_count = 0;

    /* Ensure /apps directory exists */
    if (!vfs_find(vfs_root(), "apps"))
        vfs_mkdir(vfs_root(), "apps");

    /* Load persisted package list */
    carepkg_db_load();

    /* Create and auto-install a demo "hello" .care package */
    fs_node_t *tmp = vfs_find(vfs_root(), "tmp");
    if (!tmp) tmp = vfs_mkdir(vfs_root(), "tmp");

    const char *hello_care =
        "CARE 1.0\n"
        "name=hello\n"
        "version=1.0.0\n"
        "description=Hello World demo\n"
        "author=CareOS Team\n"
        "exec=hello\n"
        "icon=terminal\n"
        "category=Demos\n"
        "permissions=fs.read\n"
        "---FILES---\n"
        "FILE hello\n"
        "#!/bin/sh\n"
        "echo Hello from CareOS!\n"
        "echo This is a .care package.\n"
        "---ENDFILE---\n"
        "FILE README.txt\n"
        "Hello World .care package\n"
        "Installed via carepkg.\n"
        "---ENDFILE---\n"
        "---END---\n";

    fs_node_t *hf = vfs_mkfile(tmp, "hello.care");
    vfs_write(hf, hello_care, kstrlen(hello_care));
    pkg_install_node(hf);

    /* Create a second demo — a simple shell script app */
    const char *calc_care =
        "CARE 1.0\n"
        "name=quickcalc\n"
        "version=0.1.0\n"
        "description=Quick arithmetic helper\n"
        "author=CareOS Community\n"
        "exec=calc.sh\n"
        "icon=calc\n"
        "category=Utilities\n"
        "permissions=fs.read,gui\n"
        "---FILES---\n"
        "FILE calc.sh\n"
        "#!/bin/sh\n"
        "echo QuickCalc 0.1 - enter expressions\n"
        "---ENDFILE---\n"
        "FILE README.txt\n"
        "QuickCalc - simple arithmetic tool\n"
        "---ENDFILE---\n"
        "---END---\n";

    fs_node_t *cf = vfs_mkfile(tmp, "quickcalc.care");
    vfs_write(cf, calc_care, kstrlen(calc_care));
    pkg_install_node(cf);

    serial_write("[carepkg] .care package manager ready\n");
}

/* ── Main CLI dispatcher ──────────────────────────────────────────────────── */
void carepkg_run(const char *cmd, const char *arg) {
    if (!cmd || cmd[0]=='\0') { carepkg_run("help", NULL); return; }

    if (kstrcmp(cmd,"install")==0) {
        if (!arg || !arg[0]) { terminal_writeln("usage: carepkg install <path.care>"); return; }
        pkg_install(arg);
    } else if (kstrcmp(cmd,"remove")==0 || kstrcmp(cmd,"rm")==0 || kstrcmp(cmd,"uninstall")==0) {
        if (!arg || !arg[0]) { terminal_writeln("usage: carepkg remove <name>"); return; }
        pkg_remove(arg);
    } else if (kstrcmp(cmd,"list")==0 || kstrcmp(cmd,"ls")==0) {
        pkg_list();
    } else if (kstrcmp(cmd,"info")==0) {
        if (!arg || !arg[0]) { terminal_writeln("usage: carepkg info <name>"); return; }
        pkg_info(arg);
    } else if (kstrcmp(cmd,"create")==0) {
        if (!arg || !arg[0]) { terminal_writeln("usage: carepkg create <name> [version]"); return; }
        pkg_create(arg, "1.0.0");
    } else if (kstrcmp(cmd,"help")==0) {
        terminal_writeln("carepkg — CareOS Package Manager (.care format)");
        terminal_writeln("");
        terminal_writeln("  install <path.care>   Install a package");
        terminal_writeln("  remove  <name>        Remove a package");
        terminal_writeln("  list                  List all installed packages");
        terminal_writeln("  info    <name>        Show package details");
        terminal_writeln("  create  <name>        Create a .care template in /tmp");
        terminal_writeln("");
        terminal_writeln(".care format: plain-text manifest with FILE sections");
        terminal_writeln("Packages install to /apps/<name>/");
    } else {
        terminal_write("carepkg: unknown command: "); terminal_writeln(cmd);
        terminal_writeln("Run 'carepkg help' for usage.");
    }
}

/* ── Direct API for apps/GUI ─────────────────────────────────────────────── */
bool carepkg_is_installed(const char *name) {
    return find_pkg(name) != NULL;
}

int carepkg_install(const char *pkg_path) {
    return pkg_install(pkg_path);
}

int carepkg_remove(const char *name) {
    return pkg_remove(name);
}

/* Get package info for GUI display */
bool carepkg_get_info(u32 idx, char *name, char *version, char *desc, char *category, bool *installed) {
    u32 real = 0;
    for (u32 i = 0; i < pkg_count; i++) {
        if (real == idx) {
            kstrncpy(name,     registry[i].name,        31);
            kstrncpy(version,  registry[i].version,     15);
            kstrncpy(desc,     registry[i].description, 127);
            kstrncpy(category, registry[i].category,    31);
            *installed = registry[i].installed;
            return true;
        }
        real++;
    }
    return false;
}

u32 carepkg_count(void) { return pkg_count; }
