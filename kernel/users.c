/* =============================================================================
 * CareOS - kernel/users.c
 * User accounts, sessions, password policy, lockout, and persistent user DB.
 * ============================================================================= */

#include "kernel.h"
#include "ext2.h"
#include "font.h"

#define MAX_USERS 16
#define USERDB_MAGIC   0x43555352u  /* CUSR */
#define USERDB_VERSION 6u
#define USERDB_SECTORS CAREOS_DISK_USERDB_SECTORS
#define USERDB_SECTOR_SIZE 512u

/* Must stay field-for-field identical to user_t in include/kernel.h -- the
 * GUI apps cast user_get_by_uid()'s result to user_t*. */
typedef struct {
    u32  uid, gid;
    char name[32];
    u8   pass_hash[USER_PASS_HASH_LEN];
    char home[64];
    char shell[32];
    bool active;
    bool is_root;

    u8   salt[USER_SALT_LEN];
    u8   failed_attempts;
    u32  lock_until_tick;
    u32  theme_pref;
    u32  font_pref;
    u32  wallpaper_pref;
    u32  mouse_pref;
    u32  clock24_pref;
    u32  taskbar_pref;
    u16  last_login_year;
    u8   last_login_month;
    u8   last_login_day;
    u8   last_login_hour;
    u8   last_login_minute;
    u8   hash_algo;
    bool must_change_password;
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
    u8   hash_algo;
    u8   must_change;
    u8   pass_hash[USER_PASS_HASH_LEN];
    u8   salt[USER_SALT_LEN];
    u8   active;
    u8   is_root;
    char name[32];
    char home[64];
    char shell[32];
    u32  theme_pref;
    u32  font_pref;
    u32  wallpaper_pref;
    u32  mouse_pref;
    u32  clock24_pref;
    u32  taskbar_pref;
    u16  last_login_year;
    u8   last_login_month;
    u8   last_login_day;
    u8   last_login_hour;
    u8   last_login_minute;
} userdb_entry_v6_t;

typedef struct __attribute__((packed)) {
    u32  uid;
    u32  gid;
    u8   hash_algo;
    u8   must_change;
    u8   pass_hash[USER_PASS_HASH_LEN];
    u8   salt[USER_SALT_LEN];
    u8   active;
    u8   is_root;
    char name[32];
    char home[64];
    char shell[32];
    u32  theme_pref;
    u32  font_pref;
    u16  last_login_year;
    u8   last_login_month;
    u8   last_login_day;
    u8   last_login_hour;
    u8   last_login_minute;
} userdb_entry_v5_t;

typedef struct __attribute__((packed)) {
    u32  uid;
    u32  gid;
    u8   hash_algo;
    u8   must_change;
    u8   pass_hash[USER_PASS_HASH_LEN];
    u8   salt[USER_SALT_LEN];
    u8   active;
    u8   is_root;
    char name[32];
    char home[64];
    char shell[32];
    u32  theme_pref;
    u16  last_login_year;
    u8   last_login_month;
    u8   last_login_day;
    u8   last_login_hour;
    u8   last_login_minute;
} userdb_entry_v4_t;

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

/* Legacy pre-v4 password hash. Retained ONLY to verify records written before
 * the move to PBKDF2, so existing installs are not locked out; those records
 * are rehashed on the next successful login. Never use this for new hashes.
 * (simple_hash and user_salt_default went away with it -- salts now come from
 * user_make_salt, which mixes rdtsc and the RTC rather than tick count alone.) */
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

/* -- Password hashing -------------------------------------------------------
 * Stored hashes are PBKDF2-HMAC-SHA256 (32 bytes, 16-byte salt). The previous
 * scheme returned a u32: 512 mixing rounds bought far less than a 32-bit
 * output cost, since that space is trivially searched offline and any
 * colliding string authenticates. hash_password_salted is kept below solely
 * to verify pre-v4 records so existing installs are not locked out. */

#define PBKDF2_ITERS 4096u

/* PBKDF2-HMAC-SHA256 with dkLen == 32, so exactly one output block. */
static void pbkdf2_sha256(const char *pw, const u8 *salt, u32 slen,
                          u32 iters, u8 *out32) {
    u8  block[USER_SALT_LEN + 4];
    u32 pwlen = (u32)kstrlen(pw);

    if (slen > USER_SALT_LEN) slen = USER_SALT_LEN;
    kmemcpy(block, salt, slen);
    /* INT32BE(1) -- the single block index */
    block[slen + 0] = 0; block[slen + 1] = 0;
    block[slen + 2] = 0; block[slen + 3] = 1;

    u8 u[32], t[32];
    hmac_sha256((const u8*)pw, pwlen, block, slen + 4u, u);
    kmemcpy(t, u, 32);

    for (u32 i = 1; i < iters; i++) {
        hmac_sha256((const u8*)pw, pwlen, u, 32, u);
        for (u32 j = 0; j < 32; j++) t[j] ^= u[j];
    }
    kmemcpy(out32, t, 32);
}

static u64 rdtsc_now(void) {
    u32 lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((u64)hi << 32) | lo;
}

/* Mixes the timestamp counter, the RTC, and the tick count. This is NOT a
 * CSPRNG -- it is adequate for salting a hobby kernel's password store, but
 * must not be reused for key material. The old salt leaned on
 * timer_get_ticks() alone, which is near-constant at first boot. */
static void user_make_salt(u8 *salt16) {
    rtc_time_t t; rtc_read(&t);
    u64 a = rdtsc_now();
    u64 b = ((u64)t.year << 40) ^ ((u64)t.month << 32) ^ ((u64)t.day << 24)
          ^ ((u64)t.hour << 16) ^ ((u64)t.minute << 8) ^ (u64)t.second
          ^ ((u64)timer_get_ticks() << 3) ^ (a >> 17);
    for (u32 i = 0; i < 8; i++) salt16[i]     = (u8)(a >> (i * 8));
    for (u32 i = 0; i < 8; i++) salt16[8 + i] = (u8)(b >> (i * 8));
}

static void user_set_password(user_rec_t *u, const char *pw) {
    if (!u || !pw) return;
    user_make_salt(u->salt);
    pbkdf2_sha256(pw, u->salt, USER_SALT_LEN, PBKDF2_ITERS, u->pass_hash);
    u->hash_algo = USER_HASH_PBKDF2_S256;
}

static void users_persist_save(void);

static bool user_verify_password(user_rec_t *u, const char *pw) {
    if (!u || !pw) return false;

    if (u->hash_algo == USER_HASH_PBKDF2_S256) {
        u8 got[32];
        pbkdf2_sha256(pw, u->salt, USER_SALT_LEN, PBKDF2_ITERS, got);
        u8 diff = 0;
        for (u32 i = 0; i < 32; i++) diff |= (u8)(got[i] ^ u->pass_hash[i]);
        return diff == 0;   /* constant time across the digest */
    }

    /* Pre-v4 record: verify with the old algorithm, then upgrade in place. */
    u32 legacy_salt = 0, want = 0;
    kmemcpy(&legacy_salt, u->salt, 4);
    kmemcpy(&want, u->pass_hash, 4);
    if (hash_password_salted(pw, legacy_salt) != want) return false;

    user_set_password(u, pw);
    users_persist_save();
    serial_write("[users] upgraded legacy password hash to PBKDF2 for ");
    serial_write(u->name);
    serial_write("\n");
    return true;
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
    /* A pre-v4 record may carry an all-zero salt (v1 had no salt field). Leave
     * it alone: the legacy verifier needs the stored value, whatever it was,
     * and the record is reseeded with a real salt on upgrade. */
    u->failed_attempts = 0;
    u->lock_until_tick = 0;
    if (u->theme_pref != USER_THEME_SYSTEM_DEFAULT && u->theme_pref > 1)
        u->theme_pref = USER_THEME_SYSTEM_DEFAULT;
    if (u->font_pref != USER_FONT_SYSTEM_DEFAULT && u->font_pref >= font_registry_count())
        u->font_pref = USER_FONT_SYSTEM_DEFAULT;
}

static void rebuild_passwd_file(void) {
    fs_node_t *etc = vfs_find(vfs_root(), "etc");
    if (!etc) return;
    fs_node_t *pf = vfs_find(etc, "passwd");
    if (!pf) pf = vfs_mkfile(etc, "passwd");
    if (!pf) return;

    char out[4096];
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
        u32 payload_len = hdr->count * (u32)sizeof(userdb_entry_v6_t);
        u32 max_payload = USERDB_SECTORS * USERDB_SECTOR_SIZE - (u32)sizeof(userdb_hdr_t);
        if (payload_len > max_payload) return false;

        u8 *payload = userdb_io + sizeof(userdb_hdr_t);
        if (userdb_checksum(payload, payload_len) != hdr->checksum) return false;

        userdb_entry_v6_t *entries = (userdb_entry_v6_t*)payload;
        for (u32 i = 0; i < hdr->count && i < MAX_USERS; i++) {
            user_rec_t *u = &users[user_count++];
            u->uid = entries[i].uid;
            u->gid = entries[i].gid;
            u->hash_algo = entries[i].hash_algo;
            u->must_change_password = entries[i].must_change ? true : false;
            kmemcpy(u->pass_hash, entries[i].pass_hash, USER_PASS_HASH_LEN);
            kmemcpy(u->salt, entries[i].salt, USER_SALT_LEN);
            u->active = entries[i].active ? true : false;
            u->is_root = entries[i].is_root ? true : false;
            u->theme_pref = entries[i].theme_pref;
            u->font_pref = entries[i].font_pref;
            u->wallpaper_pref = entries[i].wallpaper_pref;
            u->mouse_pref = entries[i].mouse_pref;
            u->clock24_pref = entries[i].clock24_pref;
            u->taskbar_pref = entries[i].taskbar_pref;
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

    /* v5 predates per-user wallpaper/mouse/clock/taskbar prefs. Load it and
     * default the new prefs to unset (global fallback); the record upgrades
     * to v6 on the next save. */
    if (hdr->version == 5u) {
        u32 payload_len = hdr->count * (u32)sizeof(userdb_entry_v5_t);
        u32 max_payload = USERDB_SECTORS * USERDB_SECTOR_SIZE - (u32)sizeof(userdb_hdr_t);
        if (payload_len > max_payload) return false;

        u8 *payload = userdb_io + sizeof(userdb_hdr_t);
        if (userdb_checksum(payload, payload_len) != hdr->checksum) return false;

        userdb_entry_v5_t *entries = (userdb_entry_v5_t*)payload;
        for (u32 i = 0; i < hdr->count && i < MAX_USERS; i++) {
            user_rec_t *u = &users[user_count++];
            u->uid = entries[i].uid;
            u->gid = entries[i].gid;
            u->hash_algo = entries[i].hash_algo;
            u->must_change_password = entries[i].must_change ? true : false;
            kmemcpy(u->pass_hash, entries[i].pass_hash, USER_PASS_HASH_LEN);
            kmemcpy(u->salt, entries[i].salt, USER_SALT_LEN);
            u->active = entries[i].active ? true : false;
            u->is_root = entries[i].is_root ? true : false;
            u->theme_pref = entries[i].theme_pref;
            u->font_pref = entries[i].font_pref;
            u->wallpaper_pref = USER_PREF_UNSET;
            u->mouse_pref     = USER_PREF_UNSET;
            u->clock24_pref   = USER_PREF_UNSET;
            u->taskbar_pref   = USER_PREF_UNSET;
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

    /* v4 predates per-user font_pref. Load it and default the font to the
     * system choice; the record upgrades to v6 on the next save. */
    if (hdr->version == 4u) {
        u32 payload_len = hdr->count * (u32)sizeof(userdb_entry_v4_t);
        u32 max_payload = USERDB_SECTORS * USERDB_SECTOR_SIZE - (u32)sizeof(userdb_hdr_t);
        if (payload_len > max_payload) return false;

        u8 *payload = userdb_io + sizeof(userdb_hdr_t);
        if (userdb_checksum(payload, payload_len) != hdr->checksum) return false;

        userdb_entry_v4_t *entries = (userdb_entry_v4_t*)payload;
        for (u32 i = 0; i < hdr->count && i < MAX_USERS; i++) {
            user_rec_t *u = &users[user_count++];
            u->uid = entries[i].uid;
            u->gid = entries[i].gid;
            u->hash_algo = entries[i].hash_algo;
            u->must_change_password = entries[i].must_change ? true : false;
            kmemcpy(u->pass_hash, entries[i].pass_hash, USER_PASS_HASH_LEN);
            kmemcpy(u->salt, entries[i].salt, USER_SALT_LEN);
            u->active = entries[i].active ? true : false;
            u->is_root = entries[i].is_root ? true : false;
            u->theme_pref = entries[i].theme_pref;
            u->font_pref = USER_FONT_SYSTEM_DEFAULT;
            u->wallpaper_pref = USER_PREF_UNSET;
            u->mouse_pref     = USER_PREF_UNSET;
            u->clock24_pref   = USER_PREF_UNSET;
            u->taskbar_pref   = USER_PREF_UNSET;
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

    /* v1-v3 hold a 32-bit legacy hash. Keep it, tag the record, and let
     * user_verify_password upgrade it on the next successful login. */
    if (hdr->version == 3u) {
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
            kmemcpy(u->pass_hash, &entries[i].pass_hash, 4);
            kmemcpy(u->salt, &entries[i].salt, 4);
            u->hash_algo = USER_HASH_LEGACY_FNV;
            u->must_change_password = false;
            u->active = entries[i].active ? true : false;
            u->is_root = entries[i].is_root ? true : false;
            u->theme_pref = entries[i].theme_pref;
            u->font_pref = USER_FONT_SYSTEM_DEFAULT;
            u->wallpaper_pref = USER_PREF_UNSET;
            u->mouse_pref     = USER_PREF_UNSET;
            u->clock24_pref   = USER_PREF_UNSET;
            u->taskbar_pref   = USER_PREF_UNSET;
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
            kmemcpy(u->pass_hash, &entries[i].pass_hash, 4);
            kmemcpy(u->salt, &entries[i].salt, 4);
            u->hash_algo = USER_HASH_LEGACY_FNV;
            u->must_change_password = false;
            u->active = entries[i].active ? true : false;
            u->is_root = entries[i].is_root ? true : false;
            u->theme_pref = USER_THEME_SYSTEM_DEFAULT;
            u->font_pref = USER_FONT_SYSTEM_DEFAULT;
            u->wallpaper_pref = USER_PREF_UNSET;
            u->mouse_pref     = USER_PREF_UNSET;
            u->clock24_pref   = USER_PREF_UNSET;
            u->taskbar_pref   = USER_PREF_UNSET;
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
            kmemcpy(u->pass_hash, &entries[i].pass_hash, 4);
            /* v1 had no salt field at all; the legacy verifier hashed with 0. */
            kmemset(u->salt, 0, USER_SALT_LEN);
            u->hash_algo = USER_HASH_LEGACY_FNV;
            u->must_change_password = false;
            u->active = entries[i].active ? true : false;
            u->is_root = entries[i].is_root ? true : false;
            u->theme_pref = USER_THEME_SYSTEM_DEFAULT;
            u->font_pref = USER_FONT_SYSTEM_DEFAULT;
            u->wallpaper_pref = USER_PREF_UNSET;
            u->mouse_pref     = USER_PREF_UNSET;
            u->clock24_pref   = USER_PREF_UNSET;
            u->taskbar_pref   = USER_PREF_UNSET;
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

    /* users_persist_load bound-checks its payload, but this function did not:
     * it wrote user_count records into a fixed userdb_io[] with no cap, so a
     * populated account list overflowed the static buffer and corrupted
     * whatever followed it in BSS. Cap the write to what the region holds. */
    u32 max_payload = USERDB_SECTORS * USERDB_SECTOR_SIZE - (u32)sizeof(userdb_hdr_t);
    u32 max_entries = max_payload / (u32)sizeof(userdb_entry_v6_t);
    u32 to_write = user_count < max_entries ? user_count : max_entries;
    if (to_write < user_count)
        serial_write("[users] WARNING: userdb region too small, truncating save\n");
    hdr->count = to_write;

    userdb_entry_v6_t *entries = (userdb_entry_v6_t*)(userdb_io + sizeof(userdb_hdr_t));
    for (u32 i = 0; i < to_write; i++) {
        entries[i].uid = users[i].uid;
        entries[i].gid = users[i].gid;
        entries[i].hash_algo = users[i].hash_algo;
        entries[i].must_change = users[i].must_change_password ? 1 : 0;
        kmemcpy(entries[i].pass_hash, users[i].pass_hash, USER_PASS_HASH_LEN);
        kmemcpy(entries[i].salt, users[i].salt, USER_SALT_LEN);
        entries[i].active = users[i].active ? 1 : 0;
        entries[i].is_root = users[i].is_root ? 1 : 0;
        entries[i].theme_pref = users[i].theme_pref;
        entries[i].font_pref = users[i].font_pref;
        entries[i].wallpaper_pref = users[i].wallpaper_pref;
        entries[i].mouse_pref     = users[i].mouse_pref;
        entries[i].clock24_pref   = users[i].clock24_pref;
        entries[i].taskbar_pref   = users[i].taskbar_pref;
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

    u32 payload_len = hdr->count * (u32)sizeof(userdb_entry_v6_t);
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
    u->font_pref = USER_FONT_SYSTEM_DEFAULT;
    u->wallpaper_pref = USER_PREF_UNSET;
    u->mouse_pref     = USER_PREF_UNSET;
    u->clock24_pref   = USER_PREF_UNSET;
    u->taskbar_pref   = USER_PREF_UNSET;
    user_set_password(u, pass);
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

    if (!user_verify_password(u, pass ? pass : "")) {
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

void session_begin(u32 uid) {
    user_prefs_t p;
    user_prefs_get(uid, &p);
    settings_apply_prefs(&p);
    serial_write("[session] begin for uid, prefs applied\n");
}

void session_end(void) {
    user_logout();
    serial_write("[session] end\n");
}

const char *user_current_name(void) {
    return session.logged_in ? session.name : "guest";
}

u32 user_current_uid(void) {
    return current_uid;
}

bool user_session_active(void) {
    return session.logged_in && current_uid != 65534;
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

    if (!root && !user_verify_password(u, old_pass ? old_pass : ""))
        return -4;

    user_set_password(u, new_pass);
    /* The shipped bootstrap password is now gone for this account. */
    u->must_change_password = false;
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

    /* These are deliberately weak, well-known bootstrap credentials, kept so a
     * fresh QEMU boot is not a chicken-and-egg problem. They are marked
     * must_change_password, so the login gate forces a strong replacement
     * before the desktop is reachable and they cannot survive first boot.
     * Do NOT "fix" this by inventing a stronger literal here -- a shipped
     * password is weak because it is shipped, not because it is short. */
    if (!users_persist_load()) {
        user_add(0, 0, "root", "root", "/root", true);
        user_add(1000, 1000, "user", "CareOS123", "/home/user", false);
        for (u32 i = 0; i < user_count; i++) users[i].must_change_password = true;
        users_persist_save();
        serial_write("[users] initialized default account database\n");
    } else {
        users_compact();
        u32 before = user_count;
        if (!find_user_by_name("root"))
            user_add(0, 0, "root", "root", "/root", true);
        if (!find_user_by_name("user"))
            user_add(1000, 1000, "user", "CareOS123", "/home/user", false);
        /* Only newly re-seeded accounts get the flag; existing ones keep
         * whatever the operator already set. */
        for (u32 i = before; i < user_count; i++) users[i].must_change_password = true;
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

    user_set_password(u, new_pass);
    u->must_change_password = false;
    users_persist_save();
}

bool user_must_change_password(void) {
    user_rec_t *u = find_user_by_uid(current_uid);
    return u ? u->must_change_password : false;
}

/* Enumerator for the greeter (account-picker) UI. Only active accounts in
 * the users[] table are enumerated; guest (uid 65534) is never a stored
 * account so the loop already excludes it. */
u32 user_enum_count(void) {
    u32 n = 0;
    for (u32 i = 0; i < user_count; i++)
        if (users[i].active) n++;
    return n;
}

bool user_enum_at(u32 idx, u32 *uid_out, char *name_out, u32 name_cap, bool *is_root_out) {
    u32 seen = 0;
    for (u32 i = 0; i < user_count; i++) {
        if (!users[i].active) continue;
        if (seen == idx) {
            if (uid_out) *uid_out = users[i].uid;
            if (is_root_out) *is_root_out = users[i].is_root;
            if (name_out && name_cap) {
                kstrncpy(name_out, users[i].name, name_cap - 1);
                name_out[name_cap - 1] = '\0';
            }
            return true;
        }
        seen++;
    }
    return false;
}

void user_set_current_theme_preference(u32 theme) {
    user_rec_t *u = find_user_by_uid(current_uid);
    if (!u) return;
    u->theme_pref = (theme <= 1) ? theme : USER_THEME_SYSTEM_DEFAULT;
    users_persist_save();
}

void user_set_current_font_preference(u32 index) {
    user_rec_t *u = find_user_by_uid(current_uid);
    if (!u) return;
    u->font_pref = (index < font_registry_count()) ? index : USER_FONT_SYSTEM_DEFAULT;
    users_persist_save();
}

void user_prefs_get(u32 uid, user_prefs_t *out) {
    if (!out) return;
    user_rec_t *u = find_user_by_uid(uid);
    if (!u) {
        out->theme = out->font = out->wallpaper = USER_PREF_UNSET;
        out->mouse_sensitivity = out->clock_24h = out->taskbar_centered = USER_PREF_UNSET;
        return;
    }
    out->theme            = u->theme_pref;
    out->font             = u->font_pref;
    out->wallpaper        = u->wallpaper_pref;
    out->mouse_sensitivity= u->mouse_pref;
    out->clock_24h        = u->clock24_pref;
    out->taskbar_centered = u->taskbar_pref;
}

void user_prefs_set_current(const user_prefs_t *p) {
    if (!p) return;
    user_rec_t *u = find_user_by_uid(current_uid);
    if (!u) return;
    /* theme/font are clamped the same way the dedicated setters above
     * (user_set_current_theme_preference / user_set_current_font_preference)
     * clamp them, so an out-of-range value can never be persisted and left
     * live until the next reboot re-sanitizes it. */
    if (p->theme != USER_PREF_UNSET)
        u->theme_pref = (p->theme <= 1) ? p->theme : USER_THEME_SYSTEM_DEFAULT;
    if (p->font != USER_PREF_UNSET)
        u->font_pref = (p->font < font_registry_count()) ? p->font : USER_FONT_SYSTEM_DEFAULT;
    if (p->wallpaper != USER_PREF_UNSET)         u->wallpaper_pref = p->wallpaper;
    if (p->mouse_sensitivity != USER_PREF_UNSET) u->mouse_pref     = p->mouse_sensitivity;
    if (p->clock_24h != USER_PREF_UNSET)         u->clock24_pref   = p->clock_24h;
    if (p->taskbar_centered != USER_PREF_UNSET)  u->taskbar_pref   = p->taskbar_centered;
    users_persist_save();
}

void users_selftest(void) {
    /* Round-trips per-user prefs through the live record set. Uses the seeded
     * 'user' account (uid 1000). Restores original values so boot state is
     * unchanged. */
    user_rec_t *u = find_user_by_name("user");
    if (!u) { serial_write("[selftest] users: FAIL (no 'user' account)\n"); return; }

    u32 save_uid = current_uid;
    current_uid = u->uid;
    user_prefs_t orig; user_prefs_get(u->uid, &orig);

    /* Part 1: direct record round-trip through user_prefs_get. */
    u->theme_pref = 0; u->wallpaper_pref = 7; u->mouse_pref = 42;
    user_prefs_t got; user_prefs_get(u->uid, &got);
    bool ok = (got.theme == 0 && got.wallpaper == 7 && got.mouse_sensitivity == 42);

    /* Part 2: user_prefs_set_current -- a USER_PREF_UNSET field (wallpaper)
     * must be skipped and left untouched, in-range fields must be written,
     * and an out-of-range theme/font must be clamped exactly like
     * user_set_current_theme_preference / user_set_current_font_preference
     * clamp them (regression check for the missing-clamp finding). */
    user_prefs_t before2; user_prefs_get(u->uid, &before2);
    user_prefs_t p2 = {
        .theme = 99,                    /* out of range -> must clamp */
        .font = 99999,                  /* out of range -> must clamp */
        .wallpaper = USER_PREF_UNSET,   /* left unset -> must not change */
        .mouse_sensitivity = 77,
        .clock_24h = 1,
        .taskbar_centered = 1,
    };
    user_prefs_set_current(&p2);
    user_prefs_t after2; user_prefs_get(u->uid, &after2);
    bool ok2 = (after2.theme == USER_THEME_SYSTEM_DEFAULT &&
                after2.font == USER_FONT_SYSTEM_DEFAULT &&
                after2.wallpaper == before2.wallpaper &&
                after2.mouse_sensitivity == 77 &&
                after2.clock_24h == 1 &&
                after2.taskbar_centered == 1);
    ok = ok && ok2;

    /* restore */
    u->theme_pref = orig.theme; u->font_pref = orig.font;
    u->wallpaper_pref = orig.wallpaper; u->mouse_pref = orig.mouse_sensitivity;
    u->clock24_pref = orig.clock_24h; u->taskbar_pref = orig.taskbar_centered;
    current_uid = save_uid;

    serial_write(ok ? "[selftest] users_prefs: PASS\n"
                     : "[selftest] users_prefs: FAIL\n");

    /* apply/capture round-trip via the live settings blob */
    user_rec_t *su = find_user_by_name("user");
    if (su) {
        u32 sv = current_uid; current_uid = su->uid;
        user_prefs_t ap = { .theme = 1, .font = USER_PREF_UNSET, .wallpaper = 2,
                            .mouse_sensitivity = 120, .clock_24h = 0, .taskbar_centered = 0 };
        settings_apply_prefs(&ap);
        const careos_settings_t *cs = settings_get();
        bool ok2 = (cs->theme == 1 && cs->wallpaper == 2 &&
                    cs->mouse_sensitivity == 120 && cs->clock_24h == 0 &&
                    cs->taskbar_centered == 0);
        serial_write(ok2 ? "[selftest] settings_apply: PASS\n"
                         : "[selftest] settings_apply: FAIL\n");
        current_uid = sv;
    }
}
