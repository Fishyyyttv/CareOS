/* =============================================================================
 * CareOS - kernel/vfs.c
 * In-memory VFS with persisted /home subtree on ATA disk.
 * ============================================================================= */
#include "kernel.h"
#include "ext2.h"

static fs_node_t node_pool[FS_MAX_FILES + FS_MAX_DIRS];
static u32       node_count = 0;
static fs_node_t *fs_root_node = NULL;

#define HOMEFS_MAGIC   0x43484F4Du /* CHOM */
#define HOMEFS_VERSION 1u

typedef struct __attribute__((packed)) {
    u32 magic;
    u32 version;
    u32 used;
    u32 checksum;
    u32 entries;
} homefs_hdr_t;

typedef struct __attribute__((packed)) {
    u8  type;
    u16 path_len;
    u16 data_len;
} homefs_entry_hdr_t;

static u8   home_io[CAREOS_DISK_HOMEFS_SECTORS * 512u];
static bool home_storage_ready = false;
static bool home_loading = false;
static bool ext2_home_ready = false;

/* -- Node allocation -------------------------------------------------------- */
static fs_node_t *alloc_node(void) {
    if (node_count >= FS_MAX_FILES + FS_MAX_DIRS) return NULL;
    fs_node_t *n = &node_pool[node_count++];
    kmemset(n, 0, sizeof(fs_node_t));
    n->permissions = 0755;
    return n;
}

static void add_child(fs_node_t *parent, fs_node_t *child);

/* -- Public API ------------------------------------------------------------- */
fs_node_t *vfs_root(void) { return fs_root_node; }

static fs_node_t *find_cached_child(fs_node_t *dir, const char *name) {
    if (!dir || dir->type != FS_DIR) return NULL;
    for (u32 i = 0; i < dir->child_count; i++)
        if (kstrcmp(dir->children[i]->name, name) == 0)
            return dir->children[i];
    return NULL;
}

static void vfs_ext2_cache_file(fs_node_t *node) {
    ext2_inode_t ino;
    if (!node || node->type != FS_FILE || node->inode_num == 0) return;
    if (ext2_read_inode(node->inode_num, &ino) != 0) return;

    node->size = ino.i_size;
    u32 to_read = ino.i_size;
    if (to_read >= FS_FILE_DATA_MAX) to_read = FS_FILE_DATA_MAX - 1;
    if (to_read > 0) {
        if (ext2_read_data(&ino, 0, node->data, to_read) < 0) return;
    }
    node->data[to_read] = '\0';
}

static fs_node_t *vfs_ext2_cache_child(fs_node_t *parent, u32 inode_num,
                                       u8 file_type, const char *name) {
    fs_node_t *child = find_cached_child(parent, name);
    if (child) return child;

    ext2_inode_t ino;
    if (ext2_read_inode(inode_num, &ino) != 0) return NULL;

    child = alloc_node();
    if (!child) return NULL;

    kstrncpy(child->name, name, FS_NAME_MAX - 1);
    child->name[FS_NAME_MAX - 1] = '\0';
    child->inode_num = inode_num;
    child->size = ino.i_size;

    if (file_type == EXT2_FT_DIR || (ino.i_mode & 0xF000u) == EXT2_S_IFDIR) {
        child->type = FS_DIR;
        child->permissions = 0755;
        child->children_loaded = false;
    } else {
        child->type = FS_FILE;
        child->permissions = 0644;
        vfs_ext2_cache_file(child);
    }

    add_child(parent, child);
    return child;
}

static void vfs_ext2_sync_dir(fs_node_t *dir) {
    if (!dir || dir->type != FS_DIR || dir->inode_num == 0 || dir->children_loaded) return;

    ext2_dirent_info_t entries[32];
    u32 count = 0;
    if (ext2_list_dir(dir->inode_num, entries, 32, &count) != 0) return;

    for (u32 i = 0; i < count; i++)
        vfs_ext2_cache_child(dir, entries[i].inode, entries[i].file_type, entries[i].name);

    dir->children_loaded = true;
}

fs_node_t *vfs_find(fs_node_t *dir, const char *name) {
    if (!dir || dir->type != FS_DIR) return NULL;
    if (dir->inode_num != 0) vfs_ext2_sync_dir(dir);

    fs_node_t *child = find_cached_child(dir, name);
    if (child) return child;

    if (dir->inode_num != 0) {
        u32 inode_num = ext2_lookup(dir->inode_num, name);
        if (inode_num != 0)
            return vfs_ext2_cache_child(dir, inode_num, 0, name);
    }

    return NULL;
}

static void add_child(fs_node_t *parent, fs_node_t *child) {
    if (parent->child_count < 32) {
        parent->children[parent->child_count++] = child;
        child->parent = parent;
    }
}

/* -- Home persistence helpers ---------------------------------------------- */
static u32 homefs_lba(void) {
    u32 sectors = ata_get_sectors();
    if (sectors <= CAREOS_DISK_RESERVED_SECTORS + 64u) return 0;
    return sectors - CAREOS_DISK_RESERVED_SECTORS;
}

static bool homefs_available(void) {
    return ata_is_present() && homefs_lba() != 0;
}

static u32 homefs_checksum(const u8 *p, u32 len) {
    u32 h = 2166136261u;
    for (u32 i = 0; i < len; i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

static bool node_is_home_related(fs_node_t *node) {
    fs_node_t *cur = node;
    while (cur && cur->parent) {
        if (cur->parent == fs_root_node && kstrcmp(cur->name, "home") == 0)
            return true;
        cur = cur->parent;
    }
    return false;
}

static void homefs_get_relpath(fs_node_t *node, char *out, u32 max) {
    if (!out || max == 0) return;
    out[0] = '\0';

    if (!node || !node->parent) return;

    char parts[16][FS_NAME_MAX];
    u32 depth = 0;
    fs_node_t *cur = node;

    while (cur && cur->parent && depth < 16) {
        if (cur->parent == fs_root_node && kstrcmp(cur->name, "home") == 0)
            break;
        kstrncpy(parts[depth], cur->name, FS_NAME_MAX - 1);
        parts[depth][FS_NAME_MAX - 1] = '\0';
        depth++;
        cur = cur->parent;
    }

    if (!cur || !cur->parent) return;

    u32 pos = 0;
    for (i32 i = (i32)depth - 1; i >= 0; i--) {
        if (parts[i][0] == '\0') continue;
        for (u32 j = 0; parts[i][j] != '\0'; j++) {
            if (pos + 1 >= max) {
                out[max - 1] = '\0';
                return;
            }
            out[pos++] = parts[i][j];
        }
        if (i > 0) {
            if (pos + 1 >= max) {
                out[max - 1] = '\0';
                return;
            }
            out[pos++] = '/';
        }
    }
    out[pos] = '\0';
}

static bool homefs_append(const void *src, u32 len, u32 *off, u32 max) {
    if (!src || !off) return false;
    if (*off + len > max) return false;
    kmemcpy(home_io + *off, src, len);
    *off += len;
    return true;
}

static void homefs_serialize_node(fs_node_t *node, u32 *off, u32 max, u32 *entries) {
    if (!node || !off || !entries) return;

    char rel[FS_PATH_MAX];
    homefs_get_relpath(node, rel, sizeof(rel));
    if (rel[0] != '\0') {
        u32 plen = (u32)kstrlen(rel);
        u32 dlen = (node->type == FS_FILE) ? node->size : 0;
        if (dlen > FS_FILE_DATA_MAX) dlen = FS_FILE_DATA_MAX;

        homefs_entry_hdr_t eh;
        eh.type = (u8)node->type;
        eh.path_len = (u16)plen;
        eh.data_len = (u16)dlen;

        if (!homefs_append(&eh, sizeof(eh), off, max)) return;
        if (plen && !homefs_append(rel, plen, off, max)) return;
        if (dlen && !homefs_append(node->data, dlen, off, max)) return;
        (*entries)++;
    }

    if (node->type == FS_DIR) {
        for (u32 i = 0; i < node->child_count; i++)
            homefs_serialize_node(node->children[i], off, max, entries);
    }
}

static fs_node_t *homefs_ensure_dir_path(fs_node_t *base, const char *path) {
    if (!base || !path) return NULL;
    if (path[0] == '\0') return base;

    char tmp[FS_PATH_MAX];
    kstrncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    fs_node_t *cur = base;
    char *tok = tmp;
    while (tok && *tok) {
        char *slash = kstrchr(tok, '/');
        if (slash) *slash = '\0';

        fs_node_t *next = vfs_find(cur, tok);
        if (!next) next = vfs_mkdir(cur, tok);
        if (!next || next->type != FS_DIR) return NULL;

        cur = next;
        if (!slash) break;
        tok = slash + 1;
    }

    return cur;
}

static void homefs_deserialize_entry(const char *path, u32 plen, u8 type,
                                     const u8 *data, u32 dlen) {
    fs_node_t *home = vfs_find(fs_root_node, "home");
    if (!home) home = vfs_mkdir(fs_root_node, "home");
    if (!home || plen == 0) return;

    char full[FS_PATH_MAX];
    if (plen >= sizeof(full)) plen = sizeof(full) - 1;
    kmemcpy(full, path, plen);
    full[plen] = '\0';

    char *leaf = kstrrchr(full, '/');
    fs_node_t *parent = home;
    char *name = full;

    if (leaf) {
        *leaf = '\0';
        name = leaf + 1;
        parent = homefs_ensure_dir_path(home, full);
    }

    if (!parent || !name[0]) return;

    if (type == FS_DIR) {
        vfs_mkdir(parent, name);
        return;
    }

    fs_node_t *f = vfs_mkfile(parent, name);
    if (!f) return;
    if (dlen > FS_FILE_DATA_MAX) dlen = FS_FILE_DATA_MAX;
    vfs_write(f, (const char*)data, dlen);
}

static void homefs_save(void) {
    if (!home_storage_ready || !homefs_available()) return;

    kmemset(home_io, 0, sizeof(home_io));
    u32 off = sizeof(homefs_hdr_t);
    u32 entries = 0;

    fs_node_t *home = vfs_find(fs_root_node, "home");
    if (home) {
        for (u32 i = 0; i < home->child_count; i++)
            homefs_serialize_node(home->children[i], &off, (u32)sizeof(home_io), &entries);
    }

    homefs_hdr_t *hdr = (homefs_hdr_t*)home_io;
    hdr->magic = HOMEFS_MAGIC;
    hdr->version = HOMEFS_VERSION;
    hdr->used = off - (u32)sizeof(homefs_hdr_t);
    hdr->entries = entries;
    hdr->checksum = homefs_checksum(home_io + sizeof(homefs_hdr_t), hdr->used);

    u32 lba = homefs_lba();
    for (u32 i = 0; i < CAREOS_DISK_HOMEFS_SECTORS; i++)
        ata_write_sectors(lba + i, 1, home_io + i * 512u);
}

static void homefs_clear_home_tree(void) {
    fs_node_t *home = vfs_find(fs_root_node, "home");
    if (!home) home = vfs_mkdir(fs_root_node, "home");
    if (!home) return;

    while (home->child_count > 0) {
        fs_node_t *child = home->children[home->child_count - 1];
        vfs_delete(child);
    }
}

static bool homefs_load(void) {
    if (!homefs_available()) return false;

    u32 lba = homefs_lba();
    for (u32 i = 0; i < CAREOS_DISK_HOMEFS_SECTORS; i++) {
        if (ata_read_sectors(lba + i, 1, home_io + i * 512u) != 0)
            return false;
    }

    homefs_hdr_t *hdr = (homefs_hdr_t*)home_io;
    u32 max_payload = (u32)sizeof(home_io) - (u32)sizeof(homefs_hdr_t);

    if (hdr->magic != HOMEFS_MAGIC || hdr->version != HOMEFS_VERSION) return false;
    if (hdr->used > max_payload) return false;

    u32 got = homefs_checksum(home_io + sizeof(homefs_hdr_t), hdr->used);
    if (got != hdr->checksum) return false;

    home_loading = true;
    homefs_clear_home_tree();

    u32 off = sizeof(homefs_hdr_t);
    u32 end = off + hdr->used;
    u32 loaded = 0;

    while (off + sizeof(homefs_entry_hdr_t) <= end) {
        homefs_entry_hdr_t eh;
        kmemcpy(&eh, home_io + off, sizeof(eh));
        off += sizeof(eh);

        if (off + eh.path_len + eh.data_len > end) break;

        const char *path = (const char*)(home_io + off);
        off += eh.path_len;

        const u8 *data = home_io + off;
        off += eh.data_len;

        if (eh.path_len == 0) continue;
        homefs_deserialize_entry(path, eh.path_len, eh.type, data, eh.data_len);
        loaded++;
    }

    home_loading = false;
    return loaded > 0 || hdr->entries == 0;
}

static void homefs_maybe_save(fs_node_t *node_hint) {
    if (!home_storage_ready || home_loading) return;
    if (!node_hint) return;
    if (ext2_home_ready) return;
    if (!node_is_home_related(node_hint)) return;
    homefs_save();
}

void vfs_storage_online(void) {
    home_storage_ready = false;
    ext2_home_ready = false;

    fs_node_t *home = vfs_find(fs_root_node, "home");
    if (!home) home = vfs_mkdir(fs_root_node, "home");
    if (home) {
        u32 home_ino = ext2_path_to_inode("/home");
        if (home_ino == 0)
            home_ino = ext2_mkdir(EXT2_ROOT_INODE, "home");
        if (home_ino != 0) {
            homefs_clear_home_tree();
            home->inode_num = home_ino;
            home->children_loaded = false;
            ext2_home_ready = true;
            vfs_ext2_sync_dir(home);
            serial_write("[vfs] /home bridged to ext2\n");
            return;
        }
    }

    if (!homefs_available()) {
        serial_write("[homefs] storage unavailable, using in-memory home\n");
        return;
    }

    if (homefs_load()) {
        home_storage_ready = true;
        serial_write("[homefs] loaded /home from disk\n");
        return;
    }

    home_storage_ready = true;
    homefs_save();
    serial_write("[homefs] initialized /home persistence\n");
}

fs_node_t *vfs_mkdir(fs_node_t *parent, const char *name) {
    if (!parent || !name) return NULL;
    fs_node_t *existing = vfs_find(parent, name);
    if (existing) return existing->type == FS_DIR ? existing : NULL;

    u32 new_inode = 0;
    if (parent->inode_num != 0) {
        new_inode = ext2_mkdir(parent->inode_num, name);
        if (new_inode == 0) return NULL;
    }

    fs_node_t *n = alloc_node();
    if (!n) return NULL;
    kstrncpy(n->name, name, FS_NAME_MAX - 1);
    n->name[FS_NAME_MAX - 1] = '\0';
    n->type = FS_DIR;
    n->inode_num = new_inode;
    n->children_loaded = (new_inode == 0);
    add_child(parent, n);
    homefs_maybe_save(parent);
    return n;
}

fs_node_t *vfs_mkfile(fs_node_t *parent, const char *name) {
    if (!parent || !name) return NULL;
    fs_node_t *existing = vfs_find(parent, name);
    if (existing) return existing->type == FS_FILE ? existing : NULL;

    u32 new_inode = 0;
    if (parent->inode_num != 0) {
        new_inode = ext2_create_file(parent->inode_num, name);
        if (new_inode == 0) return NULL;
    }

    fs_node_t *n = alloc_node();
    if (!n) return NULL;
    kstrncpy(n->name, name, FS_NAME_MAX - 1);
    n->name[FS_NAME_MAX - 1] = '\0';
    n->type = FS_FILE;
    n->permissions = 0644;
    n->inode_num = new_inode;
    add_child(parent, n);
    homefs_maybe_save(parent);
    return n;
}

static i32 vfs_ext2_read(fs_node_t *node, u32 off, u32 len, u8 *buf) {
    ext2_inode_t ino;
    if (ext2_read_inode(node->inode_num, &ino) != 0) return -1;
    return ext2_read_data(&ino, off, buf, len);
}

static i32 vfs_ext2_write(fs_node_t *node, u32 off, u32 len, const u8 *buf) {
    return ext2_write_data(node->inode_num, off, buf, len);
}

int vfs_write(fs_node_t *f, const char *data, u32 len) {
    if (!f || f->type != FS_FILE) return -1;
    if (f->inode_num != 0) {
        int written = (int)vfs_ext2_write(f, 0, len, (const u8*)data);
        if (written > 0) {
            u32 cached = (u32)written;
            if (cached >= FS_FILE_DATA_MAX) cached = FS_FILE_DATA_MAX - 1;
            kmemcpy(f->data, data, cached);
            f->data[cached] = '\0';
            f->size = (u32)written;
        }
        return written;
    }
    if (len >= FS_FILE_DATA_MAX) len = FS_FILE_DATA_MAX - 1;
    kmemcpy(f->data, data, len);
    f->data[len] = '\0';
    f->size = len;
    homefs_maybe_save(f);
    return (int)len;
}

int vfs_read(fs_node_t *f, char *buf, u32 len) {
    if (!f || f->type != FS_FILE) return -1;
    if (f->inode_num != 0) {
        int n = (int)vfs_ext2_read(f, 0, len, (u8*)buf);
        if (n >= 0) vfs_ext2_cache_file(f);
        return n;
    }
    u32 n = f->size < len ? f->size : len;
    kmemcpy(buf, f->data, n);
    return (int)n;
}

static void vfs_wipe_subtree(fs_node_t *node) {
    if (!node) return;
    while (node->child_count > 0) {
        fs_node_t *child = node->children[node->child_count - 1];
        node->child_count--;
        vfs_wipe_subtree(child);
    }
    kmemset(node, 0, sizeof(fs_node_t));
}

int vfs_delete(fs_node_t *node) {
    if (!node || !node->parent) return -1;
    bool in_home = node_is_home_related(node);
    if (node->inode_num != 0 && node->parent->inode_num != 0) {
        if (ext2_unlink(node->parent->inode_num, node->name) != 0)
            return -1;
    }

    fs_node_t *p = node->parent;
    for (u32 i = 0; i < p->child_count; i++) {
        if (p->children[i] == node) {
            for (u32 j = i; j < p->child_count - 1; j++)
                p->children[j] = p->children[j + 1];
            p->child_count--;
            vfs_wipe_subtree(node);
            if (in_home) homefs_save();
            return 0;
        }
    }
    return -1;
}

/* Resolve absolute path like /home/user/Documents */
fs_node_t *vfs_resolve_path(const char *path) {
    if (!path || path[0] != '/') return NULL;
    fs_node_t *cur = fs_root_node;
    if (cur->inode_num != 0) vfs_ext2_sync_dir(cur);
    if (path[1] == '\0') return cur;

    char buf[FS_PATH_MAX];
    kstrncpy(buf, path + 1, FS_PATH_MAX - 1);
    buf[FS_PATH_MAX - 1] = '\0';

    char *tok = buf;
    char *next;
    while (tok && *tok) {
        next = kstrchr(tok, '/');
        if (next) *next++ = '\0';
        cur = vfs_find(cur, tok);
        if (!cur) return NULL;
        if (cur->type == FS_DIR && cur->inode_num != 0)
            vfs_ext2_sync_dir(cur);
        tok = next;
    }
    return cur;
}

/* -- Build default filesystem ---------------------------------------------- */
void vfs_init(void) {
    home_storage_ready = false;
    home_loading = false;

    fs_root_node = alloc_node();
    kstrcpy(fs_root_node->name, "/");
    fs_root_node->type = FS_DIR;
    fs_root_node->children_loaded = true;

    vfs_mkdir(fs_root_node, "boot");
    vfs_mkdir(fs_root_node, "dev");
    vfs_mkdir(fs_root_node, "etc");
    vfs_mkdir(fs_root_node, "lib");
    vfs_mkdir(fs_root_node, "media");
    vfs_mkdir(fs_root_node, "opt");
    vfs_mkdir(fs_root_node, "proc");
    vfs_mkdir(fs_root_node, "root");
    vfs_mkdir(fs_root_node, "run");
    vfs_mkdir(fs_root_node, "srv");
    vfs_mkdir(fs_root_node, "sys");
    vfs_mkdir(fs_root_node, "tmp");
    vfs_mkdir(fs_root_node, "var");

    fs_node_t *usr = vfs_mkdir(fs_root_node, "usr");
    vfs_mkdir(usr, "bin");
    vfs_mkdir(usr, "lib");
    vfs_mkdir(usr, "share");

    fs_node_t *home = vfs_mkdir(fs_root_node, "home");
    fs_node_t *user = vfs_mkdir(home, "user");
    vfs_mkdir(user, "Documents");
    vfs_mkdir(user, "Downloads");
    vfs_mkdir(user, "Pictures");
    vfs_mkdir(user, "Desktop");

    fs_node_t *etc = vfs_find(fs_root_node, "etc");
    fs_node_t *hostname = vfs_mkfile(etc, "hostname");
    vfs_write(hostname, "careos", 6);

    fs_node_t *os_release = vfs_mkfile(etc, "os-release");
    vfs_write(os_release,
        "NAME=CareOS\nVERSION=1.0\nID=careos\nPRETTY_NAME=\"CareOS 1.0\"\n", 60);

    fs_node_t *passwd = vfs_mkfile(etc, "passwd");
    vfs_write(passwd,
        "root:x:0:0:root:/root:/bin/sh\nuser:x:1000:1000:user:/home/user:/bin/sh\n", 70);

    fs_node_t *proc = vfs_find(fs_root_node, "proc");
    fs_node_t *cpuinfo = vfs_mkfile(proc, "cpuinfo");
    vfs_write(cpuinfo,
        "processor : 0\nvendor_id : CareOS Virtual CPU\ncpu MHz   : 1000\n", 62);

    fs_node_t *meminfo = vfs_mkfile(proc, "meminfo");
    vfs_write(meminfo,
        "MemTotal:    4096 MB\nMemFree:     3400 MB\nMemAvailable: 3200 MB\n", 65);

    fs_node_t *version = vfs_mkfile(proc, "version");
    vfs_write(version, "CareOS version 1.0 (careos@localhost) #1 SMP", 43);

    fs_node_t *docs = vfs_find(user, "Documents");
    fs_node_t *readme = vfs_mkfile(docs, "README.txt");
    vfs_write(readme,
        "Welcome to CareOS!\n\nThis is your Documents folder.\n"
        "CareOS is a lightweight operating system.\n\n"
        "Type 'help' in the terminal for available commands.\n", 158);

    fs_node_t *desktop = vfs_find(user, "Desktop");
    fs_node_t *link = vfs_mkfile(desktop, "terminal.lnk");
    vfs_write(link, "app=terminal", 12);

    fs_node_t *bin = vfs_find(usr, "bin");
    const char *bins[] = {
        "sh", "ls", "cat", "echo", "mkdir", "rm", "cp", "mv",
        "touch", "grep", "find", "ps", "kill", "clear", "nano",
        "python3", "node", "gcc", "git", "carepkg", NULL
    };
    for (int i = 0; bins[i]; i++) {
        fs_node_t *f = vfs_mkfile(bin, bins[i]);
        vfs_write(f, "ELF binary", 10);
    }

    fs_node_t *var = vfs_find(fs_root_node, "var");
    fs_node_t *log = vfs_mkdir(var, "log");
    fs_node_t *syslog = vfs_mkfile(log, "syslog");
    vfs_write(syslog,
        "[boot] CareOS kernel loaded\n"
        "[init] Starting system services\n"
        "[net]  Network interface up\n"
        "[desk] CareDesktop started\n", 115);
}

/* -- Extra helpers expected by shell.c / apps.c ---------------------------- */
int vfs_copy(fs_node_t *src, fs_node_t *dst_dir, const char *new_name) {
    if (!src || src->type != FS_FILE || !dst_dir) return -1;
    fs_node_t *n = vfs_mkfile(dst_dir, new_name ? new_name : src->name);
    if (!n) return -1;
    kmemcpy(n->data, src->data, src->size);
    n->size = src->size;
    n->data[n->size < FS_FILE_DATA_MAX ? n->size : FS_FILE_DATA_MAX - 1] = '\0';
    homefs_maybe_save(dst_dir);
    return 0;
}

int vfs_rename(fs_node_t *node, const char *new_name) {
    if (!node || !new_name) return -1;
    kstrncpy(node->name, new_name, FS_NAME_MAX - 1);
    node->name[FS_NAME_MAX - 1] = '\0';
    homefs_maybe_save(node);
    return 0;
}

u32 vfs_node_count(void) {
    return node_count;
}
