/* =============================================================================
 * CareOS - kernel/users.c
 * User accounts, sessions, password policy, lockout, and persistent user DB.
 * ============================================================================= */

#include "kernel.h"
#include "ext2.h"

#define MAX_USERS 16
#define USERDB_MAGIC   0x43555352u  /* CUSR */
#define USERDB_VERSION 3u
#define USERDB_SECTORS CAREOS_DISK_USERDB_SECTORS
#define USERDB_SECTOR_SIZE 512u

typedef struct {
    u32  uid, gid;
    char name[32];
    u32  pass_hash;
    char home[64];
    char shell[32];
    bool active;
    bool is_root;

    u32  salt;
    u8   failed_attempts;
    u32  lock_until_tick;
    u32  theme_pref;
    u16  last_login_year;
    u8   last_login_month;
    u8   last_login_day;
    u8   last_login_hour;
    u8   last_login_minute;
} user_rec_t;

typedef struct {
    bool logged_in;
    u32  uid;
    u32  login_ticks;
    char name[32];
} session_t;

typedef struct __attribute__((packed)) {
    u32 magic;
    u32 version;
    u32 count;
    u32 checksum;
} userdb_hdr_t;

typedef struct __attribute__((packed)) {
    u32  uid;
    u32  gid;
    u32  pass_hash;
    u8   active;
    u8   is_root;
    char name[32];
    char home[64];
    char shell[32];
    u32  salt;
    u32  theme_pref;
    u16  last_login_year;
    u8   last_login_month;
    u8   last_login_day;
    u8   last_login_hour;
    u8   last_login_minute;
} userdb_entry_v3_t;

typedef struct __attribute__((packed)) {
    u32  uid;
    u32  gid;
    u32  pass_hash;
    u8   active;
    u8   is_root;
    char name[32];
    char home[64];
    char shell[32];
    u32  salt;
} userdb_entry_v2_t;

typedef struct __attribute__((packed)) {
    u32  uid;
    u32  gid;
    u32  pass_hash;
    u8   active;
    u8   is_root;
    char name[32];
    char home[64];
    char shell[32];
} userdb_entry_v1_t;

#define USER_THEME_SYSTEM_DEFAULT 0xFFFFFFFFu

static user_rec_t users[MAX_USERS];
static u32        user_count = 0;
static u32        current_uid = 65534;
static session_t  session = { false, 65534, 0, "guest" };
static u8         userdb_io[USERDB_SECTORS * USERDB_SECTOR_SIZE];

static u32 simple_hash(const char *s) {
    u32 h = 2166136261u;
    while (*s) {
        h ^= (u8)*s++;
        h *= 16777619u;
    }
    return h;
}

static u32 user_salt_default(const char *name, u32 uid) {
    u32 t = timer_get_ticks();
    return simple_hash(name) ^ (uid * 2654435761u) ^ (t << 1) ^ 0x9e3779b9u;
}

/* Moderately stronger than plain djb2: salted and iterated. */
static u32 hash_password_salted(const char *pw, u32 salt) {
    u32 h = 2166136261u ^ salt;
    for (u32 round = 0; round < 512; round++) {
        const char *p = pw;
        h ^= (round + salt);
        while (*p) {
            h ^= (u8)*p++;
            h *= 16777619u;
            h ^= (h >> 13);
        }
        h = (h << 7) | (h >> 25);
        h ^= 0xA5A5A5A5u + round;
    }
    return h ^ (h >> 16);
}

static bool password_is_strong(const char *pw) {
    if (!pw) return false;
    u32 len = (u32)kstrlen(pw);
    if (len < 8) return false;

    bool has_u = false, has_l = false, has_d = false;
    for (u32 i = 0; pw[i]; i++) {
        char c = pw[i];
        if (c >= 'A' && c <= 'Z') has_u = true;
        else if (c >= 'a' && c <= 'z') has_l = true;
        else if (c >= '0' && c <= '9') has_d = true;
    }
    return has_u && has_l && has_d;
}

static void users_set_guest_session(void) {
    session.logged_in = false;
    session.uid = 65534;
    session.login_ticks = 0;
    kstrcpy(session.name, "guest");
    current_uid = 65534;
}

static user_rec_t *find_user_by_name(const char *name) {
    for (u32 i = 0; i < user_count; i++)
        if (users[i].active && kstrcmp(users[i].name, name) == 0)
            return &users[i];
    return NULL;
}

static user_rec_t *find_user_by_uid(u32 uid) {
    for (u32 i = 0; i < user_count; i++)
        if (users[i].active && users[i].uid == uid)
            return &users[i];
    return NULL;
}

static u32 users_db_lba(void) {
    u32 sectors = ata_get_sectors();
    if (sectors <= CAREOS_DISK_RESERVED_SECTORS + 64u) return 0;
    return sectors - CAREOS_DISK_USERDB_SECTORS;
}

static bool users_persist_available(void) {
    return ata_is_present() && users_db_lba() != 0;
}

static u32 userdb_checksum(const u8 *data, u32 len) {
    u32 h = 2166136261u;
    for (u32 i = 0; i < len; i++) {
        h ^= data[i];
        h *= 16777619u;
    }
    return h;
}

static void users_compact(void) {
    u32 w = 0;
    for (u32 r = 0; r < user_count; r++) {
        if (!users[r].active) continue;
        if (w != r) users[w] = users[r];
        w++;
    }
    while (w < MAX_USERS) {
        kmemset(&users[w], 0, sizeof(user_rec_t));
        w++;
    }

    user_count = 0;
    for (u32 k = 0; k < MAX_USERS; k++)
        if (users[k].active) user_count++;
}

static void users_sanitize_profile(user_rec_t *u) {
    if (!u) return;
    if (!u->salt) u->salt = user_salt_default(u->name, u->uid);
    u->failed_attempts = 0;
    u->lock_until_tick = 0;
    if (u->theme_pref != USER_THEME_SYSTEM_DEFAULT && u->theme_pref > 1)
        u->theme_pref = USER_THEME_SYSTEM_DEFAULT;
}

static void rebuild_passwd_file(void) {
    fs_node_t *etc = vfs_find(vfs_root(), "etc");
    if (!etc) return;
    fs_node_t *pf = vfs_find(etc, "passwd");
    if (!pf) pf = vfs_mkfile(etc, "passwd");
    if (!pf) return;

    char out[FS_FILE_DATA_MAX];
    out[0] = '\0';

    for (u32 i = 0; i < user_count; i++) {
        if (!users[i].active) continue;

        char uid_s[12], gid_s[12];
        kutoa(users[i].uid, uid_s, 10);
        kutoa(users[i].gid, gid_s, 10);

        kstrcat(out, users[i].name);
        kstrcat(out, ":x:");
        kstrcat(out, uid_s);
        kstrcat(out, ":");
        kstrcat(out, gid_s);
        kstrcat(out, ":");
        kstrcat(out, users[i].name);
        kstrcat(out, ":");
        kstrcat(out, users[i].home);
        kstrcat(out, ":");
        kstrcat(out, users[i].shell);
        kstrcat(out, "\n");
    }

    vfs_write(pf, out, (u32)kstrlen(out));
}

static bool users_persist_load(void) {
    if (!users_persist_available()) return false;

    u32 lba = users_db_lba();
    for (u32 i = 0; i < USERDB_SECTORS; i++) {
        if (ata_read_sectors(lba + i, 1, userdb_io + i * USERDB_SECTOR_SIZE) != 0)
            return false;
    }

    userdb_hdr_t *hdr = (userdb_hdr_t*)userdb_io;
    if (hdr->magic != USERDB_MAGIC) return false;
    if (hdr->count > MAX_USERS) return false;

    kmemset(users, 0, sizeof(users));
    user_count = 0;

    if (hdr->version == USERDB_VERSION) {
        u32 payload_len = hdr->count * (u32)sizeof(userdb_entry_v3_t);
        u32 max_payload = USERDB_SECTORS * USERDB_SECTOR_SIZE - (u32)sizeof(userdb_hdr_t);
        if (payload_len > max_payload) return false;

        u8 *payload = userdb_io + sizeof(userdb_hdr_t);
        if (userdb_checksum(payload, payload_len) != hdr->checksum) return false;

        userdb_entry_v3_t *entries = (userdb_entry_v3_t*)payload;
        for (u32 i = 0; i < hdr->count && i < MAX_USERS; i++) {
            user_rec_t *u = &users[user_count++];
            u->uid = entries[i].uid;
            u->gid = entries[i].gid;
            u->pass_hash = entries[i].pass_hash;
            u->active = entries[i].active ? true : false;
            u->is_root = entries[i].is_root ? true : false;
            u->salt = entries[i].salt;
            u->theme_pref = entries[i].theme_pref;
            u->last_login_year = entries[i].last_login_year;
            u->last_login_month = entries[i].last_login_month;
            u->last_login_day = entries[i].last_login_day;
            u->last_login_hour = entries[i].last_login_hour;
            u->last_login_minute = entries[i].last_login_minute;

            kstrncpy(u->name, entries[i].name, sizeof(u->name) - 1);
            u->name[sizeof(u->name) - 1] = '\0';
            kstrncpy(u->home, entries[i].home, sizeof(u->home) - 1);
            u->home[sizeof(u->home) - 1] = '\0';
            kstrncpy(u->shell, entries[i].shell, sizeof(u->shell) - 1);
            u->shell[sizeof(u->shell) - 1] = '\0';
            users_sanitize_profile(u);
        }
        return user_count > 0;
    }

    if (hdr->version == 2u) {
        u32 payload_len = hdr->count * (u32)sizeof(userdb_entry_v2_t);
        u32 max_payload = USERDB_SECTORS * USERDB_SECTOR_SIZE - (u32)sizeof(userdb_hdr_t);
        if (payload_len > max_payload) return false;

        u8 *payload = userdb_io + sizeof(userdb_hdr_t);
        if (userdb_checksum(payload, payload_len) != hdr->checksum) return false;

        userdb_entry_v2_t *entries = (userdb_entry_v2_t*)payload;
        for (u32 i = 0; i < hdr->count && i < MAX_USERS; i++) {
            user_rec_t *u = &users[user_count++];
            u->uid = entries[i].uid;
            u->gid = entries[i].gid;
            u->pass_hash = entries[i].pass_hash;
            u->active = entries[i].active ? true : false;
            u->is_root = entries[i].is_root ? true : false;
            u->salt = entries[i].salt;
            u->theme_pref = USER_THEME_SYSTEM_DEFAULT;
            u->last_login_year = 0;
            u->last_login_month = 0;
            u->last_login_day = 0;
            u->last_login_hour = 0;
            u->last_login_minute = 0;

            kstrncpy(u->name, entries[i].name, sizeof(u->name) - 1);
            u->name[sizeof(u->name) - 1] = '\0';
            kstrncpy(u->home, entries[i].home, sizeof(u->home) - 1);
            u->home[sizeof(u->home) - 1] = '\0';
            kstrncpy(u->shell, entries[i].shell, sizeof(u->shell) - 1);
            u->shell[sizeof(u->shell) - 1] = '\0';
            users_sanitize_profile(u);
        }
        return user_count > 0;
    }

    if (hdr->version == 1u) {
        u32 payload_len = hdr->count * (u32)sizeof(userdb_entry_v1_t);
        u32 max_payload = USERDB_SECTORS * USERDB_SECTOR_SIZE - (u32)sizeof(userdb_hdr_t);
        if (payload_len > max_payload) return false;

        u8 *payload = userdb_io + sizeof(userdb_hdr_t);
        if (userdb_checksum(payload, payload_len) != hdr->checksum) return false;

        userdb_entry_v1_t *entries = (userdb_entry_v1_t*)payload;
        for (u32 i = 0; i < hdr->count && i < MAX_USERS; i++) {
            user_rec_t *u = &users[user_count++];
            u->uid = entries[i].uid;
            u->gid = entries[i].gid;
            u->pass_hash = entries[i].pass_hash;
            u->active = entries[i].active ? true : false;
            u->is_root = entries[i].is_root ? true : false;
            u->salt = 0;
            u->theme_pref = USER_THEME_SYSTEM_DEFAULT;
            u->last_login_year = 0;
            u->last_login_month = 0;
            u->last_login_day = 0;
            u->last_login_hour = 0;
            u->last_login_minute = 0;

            kstrncpy(u->name, entries[i].name, sizeof(u->name) - 1);
            u->name[sizeof(u->name) - 1] = '\0';
            kstrncpy(u->home, entries[i].home, sizeof(u->home) - 1);
            u->home[sizeof(u->home) - 1] = '\0';
            kstrncpy(u->shell, entries[i].shell, sizeof(u->shell) - 1);
            u->shell[sizeof(u->shell) - 1] = '\0';
            users_sanitize_profile(u);
        }
        return user_count > 0;
    }

    return false;
}

static void users_persist_save(void) {
    users_compact();
    if (!users_persist_available()) return;

    kmemset(userdb_io, 0, sizeof(userdb_io));

    userdb_hdr_t *hdr = (userdb_hdr_t*)userdb_io;
    hdr->magic = USERDB_MAGIC;
    hdr->version = USERDB_VERSION;
    hdr->count = user_count;

    userdb_entry_v3_t *entries = (userdb_entry_v3_t*)(userdb_io + sizeof(userdb_hdr_t));
    for (u32 i = 0; i < user_count && i < MAX_USERS; i++) {
        entries[i].uid = users[i].uid;
        entries[i].gid = users[i].gid;
        entries[i].pass_hash = users[i].pass_hash;
        entries[i].active = users[i].active ? 1 : 0;
        entries[i].is_root = users[i].is_root ? 1 : 0;
        entries[i].salt = users[i].salt;
        entries[i].theme_pref = users[i].theme_pref;
        entries[i].last_login_year = users[i].last_login_year;
        entries[i].last_login_month = users[i].last_login_month;
        entries[i].last_login_day = users[i].last_login_day;
        entries[i].last_login_hour = users[i].last_login_hour;
        entries[i].last_login_minute = users[i].last_login_minute;

        kstrncpy(entries[i].name, users[i].name, sizeof(entries[i].name) - 1);
        entries[i].name[sizeof(entries[i].name) - 1] = '\0';
        kstrncpy(entries[i].home, users[i].home, sizeof(entries[i].home) - 1);
        entries[i].home[sizeof(entries[i].home) - 1] = '\0';
        kstrncpy(entries[i].shell, users[i].shell, sizeof(entries[i].shell) - 1);
        entries[i].shell[sizeof(entries[i].shell) - 1] = '\0';
    }

    u32 payload_len = hdr->count * (u32)sizeof(userdb_entry_v3_t);
    hdr->checksum = userdb_checksum((u8*)entries, payload_len);

    u32 lba = users_db_lba();
    for (u32 i = 0; i < USERDB_SECTORS; i++) {
        ata_write_sectors(lba + i, 1, userdb_io + i * USERDB_SECTOR_SIZE);
    }

    rebuild_passwd_file();
}

static void ensure_home_dirs(void) {
    fs_node_t *home = vfs_find(vfs_root(), "home");
    if (!home) home = vfs_mkdir(vfs_root(), "home");
    if (!home) return;

    for (u32 i = 0; i < user_count; i++) {
        if (!users[i].active) continue;
        const char *base = users[i].name;
        if (kstrcmp(base, "root") == 0) continue;
        vfs_mkdir(home, base);
    }
}

static u32 next_user_uid(void) {
    u32 uid = 1000;
    for (u32 i = 0; i < user_count; i++)
        if (users[i].active && users[i].uid >= uid)
            uid = users[i].uid + 1;
    return uid;
}

static void users_stamp_login(user_rec_t *u) {
    rtc_time_t now;
    rtc_read(&now);
    u->last_login_year = now.year;
    u->last_login_month = now.month;
    u->last_login_day = now.day;
    u->last_login_hour = now.hour;
    u->last_login_minute = now.minute;
}

static int user_add(u32 uid, u32 gid, const char *name, const char *pass,
                    const char *home, bool is_root) {
    if (user_count >= MAX_USERS) return -1;

    user_rec_t *u = &users[user_count++];
    kmemset(u, 0, sizeof(*u));

    u->uid = uid;
    u->gid = gid;
    kstrncpy(u->name, name, sizeof(u->name) - 1);
    kstrncpy(u->home, home, sizeof(u->home) - 1);
    kstrcpy(u->shell, "/bin/sh");
    u->active = true;
    u->is_root = is_root;
    u->theme_pref = USER_THEME_SYSTEM_DEFAULT;
    u->salt = user_salt_default(name, uid);
    u->pass_hash = hash_password_salted(pass, u->salt);
    return 0;
}

static int user_create_common(const char *name, const char *pass, bool require_root) {
    if (!name || !pass || name[0] == '\0') return -1;
    if (!password_is_strong(pass)) return -2;
    if (require_root && !user_is_root()) return -3;
    if (kstrcmp(name, "guest") == 0) return -1;
    if (find_user_by_name(name)) return -1;
    if (user_count >= MAX_USERS) return -1;

    u32 uid = next_user_uid();

    char home[64];
    kstrcpy(home, "/home/");
    kstrcat(home, name);

    fs_node_t *home_dir = vfs_find(vfs_root(), "home");
    if (home_dir) vfs_mkdir(home_dir, name);

    return user_add(uid, uid, name, pass, home, false);
}

int user_login(const char *name, const char *pass) {
    user_rec_t *u = find_user_by_name(name);
    if (!u) return -1;

    u32 now = timer_get_ticks();
    if (u->lock_until_tick > now) return -3;

    if (u->pass_hash != hash_password_salted(pass, u->salt)) {
        if (u->failed_attempts < 255) u->failed_attempts++;
        if (u->failed_attempts >= 5) {
            u->lock_until_tick = now + (30u * PIT_HZ);
            u->failed_attempts = 0;
        }
        return -2;
    }

    u->failed_attempts = 0;
    u->lock_until_tick = 0;

    session.logged_in = true;
    session.uid = u->uid;
    session.login_ticks = now;
    kstrcpy(session.name, u->name);

    /* Ensure /home/<username> exists on ext2 */
    {
        char home_path[64];
        kstrcpy(home_path, "/home/");
        kstrcat(home_path, u->name);

        u32 home_ino = ext2_path_to_inode(home_path);
        if (home_ino == 0) {
            u32 home_dir = ext2_path_to_inode("/home");
            if (home_dir == 0)
                home_dir = ext2_mkdir(EXT2_ROOT_INODE, "home");
            if (home_dir != 0) {
                ext2_mkdir(home_dir, u->name);
                serial_write("[users] created home dir for ");
                serial_write(u->name); serial_write("\n");
            }
        }
    }

    current_uid = u->uid;
    users_stamp_login(u);

    if (u->theme_pref != USER_THEME_SYSTEM_DEFAULT)
        settings_set_theme(u->theme_pref);
    users_persist_save();

    serial_write("[users] login: ");
    serial_write(name);
    serial_write("\n");
    return 0;
}

void user_logout(void) {
    serial_write("[users] logout: ");
    serial_write(session.name);
    serial_write("\n");
    users_set_guest_session();
}

const char *user_current_name(void) {
    return session.logged_in ? session.name : "guest";
}

u32 user_current_uid(void) {
    return current_uid;
}

bool user_is_root(void) {
    user_rec_t *u = find_user_by_uid(current_uid);
    return u ? u->is_root : false;
}

bool user_can_read(fs_node_t *node) {
    if (!node) return false;
    if (user_is_root()) return true;
    return (node->permissions & 0004) != 0;
}

bool user_can_write(fs_node_t *node) {
    if (!node) return false;
    if (user_is_root()) return true;
    return (node->permissions & 0002) != 0;
}

int user_create(const char *name, const char *pass) {
    int rc = user_create_common(name, pass, true);
    if (rc == 0) users_persist_save();
    return rc;
}

int user_register(const char *name, const char *pass) {
    int rc = user_create_common(name, pass, false);
    if (rc == 0) users_persist_save();
    return rc;
}

int user_delete(const char *name) {
    if (!user_is_root()) return -3;

    user_rec_t *u = find_user_by_name(name);
    if (!u || u->is_root) return -1;

    if (u->uid == current_uid)
        users_set_guest_session();

    u->active = false;
    users_compact();
    users_persist_save();
    return 0;
}

int user_set_admin(const char *name, bool is_admin) {
    if (!user_is_root()) return -3;

    user_rec_t *u = find_user_by_name(name);
    if (!u || !u->active) return -1;
    if (u->is_root && !is_admin) return -2;
    if (kstrcmp(u->name, "root") == 0) return -2;

    u->is_root = is_admin ? true : false;
    users_persist_save();
    return 0;
}

int user_change_password(const char *name, const char *old_pass,
                         const char *new_pass) {
    user_rec_t *u = find_user_by_name(name);
    if (!u || !new_pass) return -1;
    if (!password_is_strong(new_pass)) return -2;

    bool self_change = (u->uid == current_uid);
    bool root = user_is_root();
    if (!root && !self_change) return -3;

    if (!root && u->pass_hash != hash_password_salted(old_pass ? old_pass : "", u->salt))
        return -4;

    u->salt = user_salt_default(u->name, u->uid) ^ timer_get_ticks();
    u->pass_hash = hash_password_salted(new_pass, u->salt);
    users_persist_save();
    return 0;
}

void user_list(void) {
    terminal_writeln("UID   GID   NAME             HOME");
    terminal_writeln("----- ----- ---------------- ----------------");

    for (u32 i = 0; i < user_count; i++) {
        if (!users[i].active) continue;

        char buf[128];
        char uid_s[12], gid_s[12];
        kutoa(users[i].uid, uid_s, 10);
        kutoa(users[i].gid, gid_s, 10);

        kstrcpy(buf, uid_s);
        kstrcat(buf, "  ");
        kstrcat(buf, gid_s);
        kstrcat(buf, "  ");
        kstrcat(buf, users[i].name);
        kstrcat(buf, "     ");
        kstrcat(buf, users[i].home);
        terminal_writeln(buf);
    }
}

void vfs_get_path(fs_node_t *node, char *buf, u32 max) {
    if (!buf || max == 0) return;
    if (!node) { buf[0] = '\0'; return; }

    if (!node->parent) {
        if (max >= 2) { buf[0] = '/'; buf[1] = '\0'; }
        else buf[0] = '\0';
        return;
    }

    char parts[16][FS_NAME_MAX];
    int depth = 0;
    fs_node_t *cur = node;

    while (cur && cur->parent && depth < 16) {
        kstrncpy(parts[depth], cur->name, FS_NAME_MAX - 1);
        parts[depth][FS_NAME_MAX - 1] = '\0';
        depth++;
        cur = cur->parent;
    }

    buf[0] = '\0';
    u32 len = 0;

    for (int p = depth - 1; p >= 0; p--) {
        if (len + 1 >= max) break;
        buf[len++] = '/';
        buf[len] = '\0';

        for (u32 j = 0; parts[p][j] != '\0'; j++) {
            if (len + 1 >= max) break;
            buf[len++] = parts[p][j];
            buf[len] = '\0';
        }
    }

    if (len == 0) {
        if (max >= 2) { buf[0] = '/'; buf[1] = '\0'; }
        else buf[0] = '\0';
    } else {
        buf[max - 1] = '\0';
    }
}

void users_init(void) {
    kmemset(users, 0, sizeof(users));
    kmemset(&session, 0, sizeof(session));
    user_count = 0;
    users_set_guest_session();

    if (!users_persist_load()) {
        user_add(0, 0, "root", "root", "/root", true);
        user_add(1000, 1000, "user", "CareOS123", "/home/user", false);
        users_persist_save();
        serial_write("[users] initialized default account database\n");
    } else {
        users_compact();
        if (!find_user_by_name("root"))
            user_add(0, 0, "root", "root", "/root", true);
        if (!find_user_by_name("user"))
            user_add(1000, 1000, "user", "CareOS123", "/home/user", false);
        users_persist_save();
        serial_write("[users] loaded account database from disk\n");
    }

    ensure_home_dirs();
    rebuild_passwd_file();
    serial_write("[users] subsystem ready, awaiting login\n");
}

void *user_get_by_uid(u32 uid) {
    for (u32 i = 0; i < user_count; i++)
        if (users[i].active && users[i].uid == uid)
            return &users[i];
    return NULL;
}

bool user_is_admin(u32 uid) {
    user_rec_t *u = find_user_by_uid(uid);
    return u ? u->is_root : false;
}

void user_passwd(const char *name, const char *new_pass) {
    user_rec_t *u = find_user_by_name(name);
    if (!u || !new_pass) return;

    bool self_change = (u->uid == current_uid);
    if (!user_is_root() && !self_change) return;
    if (!password_is_strong(new_pass)) return;

    u->salt = user_salt_default(u->name, u->uid) ^ timer_get_ticks();
    u->pass_hash = hash_password_salted(new_pass, u->salt);
    users_persist_save();
}

void user_set_current_theme_preference(u32 theme) {
    user_rec_t *u = find_user_by_uid(current_uid);
    if (!u) return;
    u->theme_pref = (theme <= 1) ? theme : USER_THEME_SYSTEM_DEFAULT;
    users_persist_save();
}
