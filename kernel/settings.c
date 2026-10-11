#include "kernel.h"
#include "font.h"
#include "account_rules.h"

/* Accent colour engine lives in gui/theme.c (declared in gui.h, which the
 * kernel-side settings.c does not include). Forward-declare the two entry
 * points we call; the signatures match gui.h exactly. */
void theme_set_accent(u32 idx);
u32  theme_accent_count(void);
/* Dropping the cached wallpaper here catches every caller that changes the
 * backdrop -- settings app, terminal, shell, and CareLang scripts alike. */
void gfx_wallpaper_cache_invalidate(void);

#define SETTINGS_MAGIC    0x43535447u /* CSTG */
/* v6 kept the v5 layout and changed only the meaning of `accent` (the v10
 * list put Care first, see accent_from_v5). v7 appends the shell/start-screen
 * fields (theme mode, glass, dock, brightness, DND, time zone). */
#define SETTINGS_VERSION  7u

typedef struct __attribute__((packed)) {
    u32 magic;
    u32 version;
    u32 checksum;
    u32 theme;
    u32 mouse_sensitivity;
    u32 boot_fast;
    u32 clock_24h;
    u32 wallpaper;
    u8  wifi_connected;
    char wifi_ssid[32];
    char wifi_pass[64];
} settings_blob_v1_t;

typedef struct __attribute__((packed)) {
    u32 magic;
    u32 version;
    u32 checksum;
    u32 theme;
    u32 mouse_sensitivity;
    u32 boot_fast;
    u32 clock_24h;
    u32 wallpaper;
    u32 taskbar_centered;
    u8  wifi_connected;
    char wifi_ssid[32];
    char wifi_pass[64];
} settings_blob_v2_t;

typedef struct __attribute__((packed)) {
    u32 magic;
    u32 version;
    u32 checksum;
    u32 theme;
    u32 mouse_sensitivity;
    u32 boot_fast;
    u32 clock_24h;
    u32 wallpaper;
    u32 taskbar_centered;
    u32 vesa_w;
    u32 vesa_h;
    u8  wifi_connected;
    char wifi_ssid[32];
    char wifi_pass[64];
} settings_blob_v3_t;

typedef struct __attribute__((packed)) {
    u32 magic;
    u32 version;
    u32 checksum;
    u32 theme;
    u32 mouse_sensitivity;
    u32 boot_fast;
    u32 clock_24h;
    u32 wallpaper;
    u32 taskbar_centered;
    u32 vesa_w;
    u32 vesa_h;
    u8  wifi_connected;
    char wifi_ssid[32];
    char wifi_pass[64];
    u32 font_family;
} settings_blob_v4_t;

typedef struct __attribute__((packed)) {
    u32 magic;
    u32 version;
    u32 checksum;
    u32 theme;
    u32 mouse_sensitivity;
    u32 boot_fast;
    u32 clock_24h;
    u32 wallpaper;
    u32 taskbar_centered;
    u32 vesa_w;
    u32 vesa_h;
    u8  wifi_connected;
    char wifi_ssid[32];
    char wifi_pass[64];
    u32 font_family;
    u32 accent;
} settings_blob_v5_t;

/* The procedural v10 background, after the six photo slots. Mirrors
 * WALLPAPER_CARE in gui/care_wallpaper.h (settings.c doesn't include gui/). */
#define WALLPAPER_CARE_INDEX 6u

/* v5 accents: Blue, Green, Orange, Purple, Red, Auto. v6: Care, Calm, Blue,
 * Green, Purple, Red, Auto. Blue was the v5 default and moves to the v10
 * default, Care, along with Orange (its closest match). */
static u32 accent_from_v5(u32 a) {
    static const u8 MAP[6] = { 0, 3, 0, 4, 5, 6 };
    return a < 6u ? MAP[a] : 0u;
}

typedef struct __attribute__((packed)) {
    settings_blob_v5_t base;           /* checksum covers the whole v7 blob */
    u32 theme_mode;
    u32 glass_alpha_pct;
    u32 reduce_transparency;
    u32 dock_position;
    u32 dock_autohide;
    u32 brightness_pct;
    u32 dnd;
    i32 tz_offset_min;
} settings_blob_v7_t;

#define TZ_DEFAULT_MIN (-420)          /* the UTC-7 the desktop clock always used */

static careos_settings_t g_settings;
static u8 settings_io[CAREOS_DISK_SETTINGS_SECTORS * 512u];

static u32 settings_lba(void) {
    u32 sectors = ata_get_sectors();
    u32 need = CAREOS_DISK_SETTINGS_SECTORS + CAREOS_DISK_USERDB_SECTORS;
    if (sectors <= need + 64u) return 0;
    return sectors - CAREOS_DISK_USERDB_SECTORS - CAREOS_DISK_SETTINGS_SECTORS;
}

static bool settings_available(void) {
    return ata_is_present() && settings_lba() != 0;
}

static u32 fnv1a32(const u8 *p, u32 len) {
    u32 h = 2166136261u;
    for (u32 i = 0; i < len; i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

static void settings_defaults(void) {
    kmemset(&g_settings, 0, sizeof(g_settings));
    g_settings.theme = 0;
    g_settings.mouse_sensitivity = 100;
    g_settings.boot_fast = 0;
    g_settings.clock_24h = 1;
    g_settings.wallpaper = WALLPAPER_CARE_INDEX;
    g_settings.taskbar_centered = 1;
    g_settings.vesa_w = 1920;
    g_settings.vesa_h = 1080;
    g_settings.wifi_connected = false;
    g_settings.wifi_ssid[0] = '\0';
    g_settings.wifi_pass[0] = '\0';
    g_settings.font_family = 0;
    g_settings.accent = 0;
    g_settings.theme_mode = 0;
    g_settings.glass_alpha_pct = 100;
    g_settings.reduce_transparency = 0;
    g_settings.dock_position = 0;
    g_settings.dock_autohide = 0;
    g_settings.brightness_pct = 100;
    g_settings.dnd = 0;
    g_settings.tz_offset_min = TZ_DEFAULT_MIN;
}

/* Fields added in v7, for blobs written before it: keep today's behaviour. */
static void settings_v7_defaults(void) {
    g_settings.theme_mode = g_settings.theme == 1 ? 1u : 0u;
    g_settings.glass_alpha_pct = 100;
    g_settings.reduce_transparency = 0;
    g_settings.dock_position = 0;
    g_settings.dock_autohide = 0;
    g_settings.brightness_pct = 100;
    g_settings.dnd = 0;
    g_settings.tz_offset_min = TZ_DEFAULT_MIN;
}

static void settings_clamp(void) {
    if (g_settings.mouse_sensitivity < 40) g_settings.mouse_sensitivity = 40;
    if (g_settings.mouse_sensitivity > 200) g_settings.mouse_sensitivity = 200;
    if (g_settings.theme > 1) g_settings.theme = 0;
    if (g_settings.wallpaper > WALLPAPER_CARE_INDEX) g_settings.wallpaper = WALLPAPER_CARE_INDEX;
    g_settings.taskbar_centered = g_settings.taskbar_centered ? 1u : 0u;
    if (g_settings.font_family >= font_registry_count()) g_settings.font_family = 0;
    if (g_settings.accent >= theme_accent_count()) g_settings.accent = 0;
    if (g_settings.theme_mode > 2) g_settings.theme_mode = 0;
    if (g_settings.glass_alpha_pct < 40) g_settings.glass_alpha_pct = 40;
    if (g_settings.glass_alpha_pct > 100) g_settings.glass_alpha_pct = 100;
    g_settings.reduce_transparency = g_settings.reduce_transparency ? 1u : 0u;
    if (g_settings.dock_position > 2) g_settings.dock_position = 0;
    g_settings.dock_autohide = g_settings.dock_autohide ? 1u : 0u;
    if (g_settings.brightness_pct < 10) g_settings.brightness_pct = 10;
    if (g_settings.brightness_pct > 100) g_settings.brightness_pct = 100;
    g_settings.dnd = g_settings.dnd ? 1u : 0u;
    if (g_settings.tz_offset_min < -12 * 60 || g_settings.tz_offset_min > 14 * 60)
        g_settings.tz_offset_min = TZ_DEFAULT_MIN;
}

static void settings_save(void) {
    if (!settings_available()) return;

    kmemset(settings_io, 0, sizeof(settings_io));

    settings_blob_v7_t *b7 = (settings_blob_v7_t*)settings_io;
    settings_blob_v5_t *b = &b7->base;
    b->magic = SETTINGS_MAGIC;
    b->version = SETTINGS_VERSION;
    b->theme = g_settings.theme;
    b->mouse_sensitivity = g_settings.mouse_sensitivity;
    b->boot_fast = g_settings.boot_fast;
    b->clock_24h = g_settings.clock_24h;
    b->wallpaper = g_settings.wallpaper;
    b->taskbar_centered = g_settings.taskbar_centered;
    b->vesa_w = g_settings.vesa_w;
    b->vesa_h = g_settings.vesa_h;
    b->wifi_connected = g_settings.wifi_connected ? 1 : 0;
    kstrncpy(b->wifi_ssid, g_settings.wifi_ssid, sizeof(b->wifi_ssid) - 1);
    b->wifi_ssid[sizeof(b->wifi_ssid) - 1] = '\0';
    kstrncpy(b->wifi_pass, g_settings.wifi_pass, sizeof(b->wifi_pass) - 1);
    b->wifi_pass[sizeof(b->wifi_pass) - 1] = '\0';
    b->font_family = g_settings.font_family;
    b->accent = g_settings.accent;
    b7->theme_mode = g_settings.theme_mode;
    b7->glass_alpha_pct = g_settings.glass_alpha_pct;
    b7->reduce_transparency = g_settings.reduce_transparency;
    b7->dock_position = g_settings.dock_position;
    b7->dock_autohide = g_settings.dock_autohide;
    b7->brightness_pct = g_settings.brightness_pct;
    b7->dnd = g_settings.dnd;
    b7->tz_offset_min = g_settings.tz_offset_min;

    b->checksum = 0;
    b->checksum = fnv1a32((const u8*)b7, sizeof(*b7));

    u32 lba = settings_lba();
    for (u32 i = 0; i < CAREOS_DISK_SETTINGS_SECTORS; i++)
        ata_write_sectors(lba + i, 1, settings_io + i * 512u);
}

static bool settings_load_v2(const settings_blob_v2_t *b) {
    u32 expect = b->checksum;
    settings_blob_v2_t temp;
    kmemcpy(&temp, b, sizeof(temp));
    temp.checksum = 0;
    if (fnv1a32((const u8*)&temp, sizeof(temp)) != expect) return false;

    g_settings.theme = b->theme;
    g_settings.mouse_sensitivity = b->mouse_sensitivity;
    g_settings.boot_fast = b->boot_fast ? 1u : 0u;
    g_settings.clock_24h = b->clock_24h ? 1u : 0u;
    g_settings.wallpaper = b->wallpaper;
    g_settings.taskbar_centered = b->taskbar_centered ? 1u : 0u;
    g_settings.wifi_connected = b->wifi_connected ? true : false;
    kstrncpy(g_settings.wifi_ssid, b->wifi_ssid, sizeof(g_settings.wifi_ssid) - 1);
    g_settings.wifi_ssid[sizeof(g_settings.wifi_ssid) - 1] = '\0';
    kstrncpy(g_settings.wifi_pass, b->wifi_pass, sizeof(g_settings.wifi_pass) - 1);
    g_settings.wifi_pass[sizeof(g_settings.wifi_pass) - 1] = '\0';
    settings_clamp();
    return true;
}

static bool settings_load_v3(const settings_blob_v3_t *b) {
    u32 expect = b->checksum;
    settings_blob_v3_t temp;
    kmemcpy(&temp, b, sizeof(temp));
    temp.checksum = 0;
    if (fnv1a32((const u8*)&temp, sizeof(temp)) != expect) return false;

    g_settings.theme = b->theme;
    g_settings.mouse_sensitivity = b->mouse_sensitivity;
    g_settings.boot_fast = b->boot_fast ? 1u : 0u;
    g_settings.clock_24h = b->clock_24h ? 1u : 0u;
    g_settings.wallpaper = b->wallpaper;
    g_settings.taskbar_centered = b->taskbar_centered ? 1u : 0u;
    g_settings.vesa_w = b->vesa_w;
    g_settings.vesa_h = b->vesa_h;
    g_settings.wifi_connected = b->wifi_connected ? true : false;
    kstrncpy(g_settings.wifi_ssid, b->wifi_ssid, sizeof(g_settings.wifi_ssid) - 1);
    g_settings.wifi_ssid[sizeof(g_settings.wifi_ssid) - 1] = '\0';
    kstrncpy(g_settings.wifi_pass, b->wifi_pass, sizeof(g_settings.wifi_pass) - 1);
    g_settings.wifi_pass[sizeof(g_settings.wifi_pass) - 1] = '\0';
    settings_clamp();
    return true;
}

static bool settings_load_v4(const settings_blob_v4_t *b) {
    u32 expect = b->checksum;
    settings_blob_v4_t temp;
    kmemcpy(&temp, b, sizeof(temp));
    temp.checksum = 0;
    if (fnv1a32((const u8*)&temp, sizeof(temp)) != expect) return false;

    g_settings.theme = b->theme;
    g_settings.mouse_sensitivity = b->mouse_sensitivity;
    g_settings.boot_fast = b->boot_fast ? 1u : 0u;
    g_settings.clock_24h = b->clock_24h ? 1u : 0u;
    g_settings.wallpaper = b->wallpaper;
    g_settings.taskbar_centered = b->taskbar_centered ? 1u : 0u;
    g_settings.vesa_w = b->vesa_w;
    g_settings.vesa_h = b->vesa_h;
    g_settings.wifi_connected = b->wifi_connected ? true : false;
    kstrncpy(g_settings.wifi_ssid, b->wifi_ssid, sizeof(g_settings.wifi_ssid) - 1);
    g_settings.wifi_ssid[sizeof(g_settings.wifi_ssid) - 1] = '\0';
    kstrncpy(g_settings.wifi_pass, b->wifi_pass, sizeof(g_settings.wifi_pass) - 1);
    g_settings.wifi_pass[sizeof(g_settings.wifi_pass) - 1] = '\0';
    g_settings.font_family = b->font_family;
    settings_clamp();
    return true;
}

static bool settings_load_v5(const settings_blob_v5_t *b) {
    u32 expect = b->checksum;
    settings_blob_v5_t temp;
    kmemcpy(&temp, b, sizeof(temp));
    temp.checksum = 0;
    if (fnv1a32((const u8*)&temp, sizeof(temp)) != expect) return false;

    g_settings.theme = b->theme;
    g_settings.mouse_sensitivity = b->mouse_sensitivity;
    g_settings.boot_fast = b->boot_fast ? 1u : 0u;
    g_settings.clock_24h = b->clock_24h ? 1u : 0u;
    g_settings.wallpaper = b->wallpaper;
    g_settings.taskbar_centered = b->taskbar_centered ? 1u : 0u;
    g_settings.vesa_w = b->vesa_w;
    g_settings.vesa_h = b->vesa_h;
    g_settings.wifi_connected = b->wifi_connected ? true : false;
    kstrncpy(g_settings.wifi_ssid, b->wifi_ssid, sizeof(g_settings.wifi_ssid) - 1);
    g_settings.wifi_ssid[sizeof(g_settings.wifi_ssid) - 1] = '\0';
    kstrncpy(g_settings.wifi_pass, b->wifi_pass, sizeof(g_settings.wifi_pass) - 1);
    g_settings.wifi_pass[sizeof(g_settings.wifi_pass) - 1] = '\0';
    g_settings.font_family = b->font_family;
    g_settings.accent = b->accent;
    settings_clamp();
    return true;
}

/* v7: verify the checksum over the whole blob, then reuse the v5 field copy
 * on a re-checksummed copy of the base. */
static bool settings_load_v7(const settings_blob_v7_t *b) {
    settings_blob_v7_t t;
    kmemcpy(&t, b, sizeof(t));
    u32 expect = t.base.checksum;
    t.base.checksum = 0;
    if (fnv1a32((const u8*)&t, sizeof(t)) != expect) return false;
    t.base.checksum = 0;
    t.base.checksum = fnv1a32((const u8*)&t.base, sizeof(t.base));
    if (!settings_load_v5(&t.base)) return false;
    g_settings.theme_mode = t.theme_mode;
    g_settings.glass_alpha_pct = t.glass_alpha_pct;
    g_settings.reduce_transparency = t.reduce_transparency;
    g_settings.dock_position = t.dock_position;
    g_settings.dock_autohide = t.dock_autohide;
    g_settings.brightness_pct = t.brightness_pct;
    g_settings.dnd = t.dnd;
    g_settings.tz_offset_min = t.tz_offset_min;
    settings_clamp();
    return true;
}

static bool settings_load_v1(const settings_blob_v1_t *b) {
    u32 expect = b->checksum;
    settings_blob_v1_t temp;
    kmemcpy(&temp, b, sizeof(temp));
    temp.checksum = 0;
    if (fnv1a32((const u8*)&temp, sizeof(temp)) != expect) return false;

    g_settings.theme = b->theme;
    g_settings.mouse_sensitivity = b->mouse_sensitivity;
    g_settings.boot_fast = b->boot_fast ? 1u : 0u;
    g_settings.clock_24h = b->clock_24h ? 1u : 0u;
    g_settings.wallpaper = b->wallpaper;
    g_settings.taskbar_centered = 1;
    g_settings.wifi_connected = b->wifi_connected ? true : false;
    kstrncpy(g_settings.wifi_ssid, b->wifi_ssid, sizeof(g_settings.wifi_ssid) - 1);
    g_settings.wifi_ssid[sizeof(g_settings.wifi_ssid) - 1] = '\0';
    kstrncpy(g_settings.wifi_pass, b->wifi_pass, sizeof(g_settings.wifi_pass) - 1);
    g_settings.wifi_pass[sizeof(g_settings.wifi_pass) - 1] = '\0';
    settings_clamp();
    return true;
}

void settings_init(void) {
    settings_defaults();

    if (!settings_available()) {
        serial_write("[settings] storage unavailable, using defaults\n");
        theme_set_accent(g_settings.accent);
        return;
    }

    u32 lba = settings_lba();
    for (u32 i = 0; i < CAREOS_DISK_SETTINGS_SECTORS; i++) {
        if (ata_read_sectors(lba + i, 1, settings_io + i * 512u) != 0) {
            serial_write("[settings] read failed, using defaults\n");
            settings_save();
            theme_set_accent(g_settings.accent);
            return;
        }
    }

    u32 magic = *(u32*)settings_io;
    u32 version = *((u32*)settings_io + 1);

    if (magic != SETTINGS_MAGIC) {
        serial_write("[settings] no prior settings, writing defaults\n");
        settings_save();
        theme_set_accent(g_settings.accent);
        return;
    }

    if (version == SETTINGS_VERSION && settings_load_v7((const settings_blob_v7_t*)settings_io)) {
        serial_write("[settings] loaded v7 settings from disk\n");
        theme_set_accent(g_settings.accent);
        return;
    }

    if (version == 6u && settings_load_v5((const settings_blob_v5_t*)settings_io)) {
        settings_v7_defaults();
        serial_write("[settings] migrated v6 settings to v7\n");
        settings_save();
        theme_set_accent(g_settings.accent);
        return;
    }

    if (version == 5u && settings_load_v5((const settings_blob_v5_t*)settings_io)) {
        g_settings.accent = accent_from_v5(((const settings_blob_v5_t*)settings_io)->accent);
        /* Still on the shipped default photo: move to the v10 default. */
        if (g_settings.wallpaper == 0u) g_settings.wallpaper = WALLPAPER_CARE_INDEX;
        settings_v7_defaults();
        serial_write("[settings] migrated v5 settings to v7\n");
        settings_save();
        theme_set_accent(g_settings.accent);
        return;
    }

    if (version == 4u && settings_load_v4((const settings_blob_v4_t*)settings_io)) {
        g_settings.accent = 0u;   /* default to Care */
        settings_v7_defaults();
        serial_write("[settings] migrated v4 settings to v7\n");
        settings_save();
        theme_set_accent(g_settings.accent);
        return;
    }

    if (version == 3u && settings_load_v3((const settings_blob_v3_t*)settings_io)) {
        g_settings.font_family = 0u;   /* default to JetBrains Mono */
        g_settings.accent = 0u;
        settings_v7_defaults();
        serial_write("[settings] migrated v3 settings to v7\n");
        settings_save();
        theme_set_accent(g_settings.accent);
        return;
    }

    if (version == 2u && settings_load_v2((const settings_blob_v2_t*)settings_io)) {
        g_settings.font_family = 0u;
        g_settings.accent = 0u;
        settings_v7_defaults();
        serial_write("[settings] migrated v2 settings to v7\n");
        settings_save();
        theme_set_accent(g_settings.accent);
        return;
    }

    if (version == 1u && settings_load_v1((const settings_blob_v1_t*)settings_io)) {
        g_settings.font_family = 0u;
        g_settings.accent = 0u;
        settings_v7_defaults();
        serial_write("[settings] migrated v1 settings to v7\n");
        settings_save();
        theme_set_accent(g_settings.accent);
        return;
    }

    serial_write("[settings] checksum mismatch, using defaults\n");
    settings_defaults();
    settings_save();
    theme_set_accent(g_settings.accent);
}

const careos_settings_t *settings_get(void) {
    return &g_settings;
}

void settings_set_theme(u32 theme) {
    g_settings.theme = theme;
    settings_clamp();
    if (user_session_active()) settings_capture_to_current_user();
    else settings_save();
}

void settings_set_mouse_sensitivity(u32 pct) {
    g_settings.mouse_sensitivity = pct;
    settings_clamp();
    if (user_session_active()) settings_capture_to_current_user();
    else settings_save();
}

void settings_set_boot_fast(bool enabled) {
    g_settings.boot_fast = enabled ? 1u : 0u;
    settings_save();
}

void settings_set_clock_24h(bool enabled) {
    g_settings.clock_24h = enabled ? 1u : 0u;
    if (user_session_active()) settings_capture_to_current_user();
    else settings_save();
}

void settings_set_wallpaper(u32 wallpaper) {
    g_settings.wallpaper = wallpaper;
    settings_clamp();
    if (user_session_active()) settings_capture_to_current_user();
    else settings_save();
    gfx_wallpaper_cache_invalidate();
    /* When the accent is "Auto" (the trailing dynamic slot), re-derive it from
     * the newly selected wallpaper so the whole UI recolours to match. */
    if (g_settings.accent == theme_accent_count() - 1u)
        theme_set_accent(g_settings.accent);
}

void settings_set_accent(u32 accent) {
    g_settings.accent = accent;
    settings_clamp();
    settings_save();
    theme_set_accent(g_settings.accent);
}

void settings_set_taskbar_centered(bool centered) {
    g_settings.taskbar_centered = centered ? 1u : 0u;
    if (user_session_active()) settings_capture_to_current_user();
    else settings_save();
}

void settings_set_wifi_profile(const char *ssid, const char *pass, bool connected) {
    if (!ssid) ssid = "";
    if (!pass) pass = "";
    kstrncpy(g_settings.wifi_ssid, ssid, sizeof(g_settings.wifi_ssid) - 1);
    g_settings.wifi_ssid[sizeof(g_settings.wifi_ssid) - 1] = '\0';
    kstrncpy(g_settings.wifi_pass, pass, sizeof(g_settings.wifi_pass) - 1);
    g_settings.wifi_pass[sizeof(g_settings.wifi_pass) - 1] = '\0';
    g_settings.wifi_connected = connected;
    settings_save();
}

void settings_set_vesa_mode(u32 w, u32 h) {
    g_settings.vesa_w = w;
    g_settings.vesa_h = h;
    settings_save();
}

void settings_set_font_family(u32 index) {
    if (index >= font_registry_count()) return;
    g_settings.font_family = index;
    font_set_family(index);
    if (user_session_active()) settings_capture_to_current_user();
    else settings_save();
}

void settings_apply_prefs(const user_prefs_t *p) {
    if (!p) return;
    if (p->theme != USER_PREF_UNSET)             g_settings.theme = p->theme;
    if (p->wallpaper != USER_PREF_UNSET)         g_settings.wallpaper = p->wallpaper;
    if (p->mouse_sensitivity != USER_PREF_UNSET) g_settings.mouse_sensitivity = p->mouse_sensitivity;
    if (p->clock_24h != USER_PREF_UNSET)         g_settings.clock_24h = p->clock_24h ? 1u : 0u;
    if (p->taskbar_centered != USER_PREF_UNSET)  g_settings.taskbar_centered = p->taskbar_centered ? 1u : 0u;
    settings_clamp();
    if (p->font != USER_PREF_UNSET && p->font < font_registry_count()) {
        g_settings.font_family = p->font;
        font_set_family(p->font);
    }
    /* NOTE: no settings_save() -- per-user prefs must not touch the global blob.
     * The live theme switch (g_theme) is done by the GUI session path via
     * theme_switch(), mirroring gui.c's boot-time theme selection. */
}

void settings_capture_to_current_user(void) {
    user_prefs_t p;
    p.theme            = g_settings.theme;
    p.font             = g_settings.font_family;
    p.wallpaper        = g_settings.wallpaper;
    p.mouse_sensitivity= g_settings.mouse_sensitivity;
    p.clock_24h        = g_settings.clock_24h;
    p.taskbar_centered = g_settings.taskbar_centered;
    user_prefs_set_current(&p);
}

/* ── v7 setters ─────────────────────────────────────────────────────────── */
bool settings_auto_theme_dark(u32 local_hour) { return auto_theme_dark(local_hour); }

void settings_set_theme_mode(u32 mode) {
    g_settings.theme_mode = mode;
    if (mode <= 1) g_settings.theme = mode;   /* fixed modes also set the theme */
    settings_clamp();
    settings_save();
    if (user_session_active() && mode <= 1) settings_capture_to_current_user();
}

void settings_set_tz_offset(i32 minutes) {
    g_settings.tz_offset_min = minutes;
    settings_clamp();
    settings_save();
}

void settings_set_glass(u32 alpha_pct, bool reduce) {
    g_settings.glass_alpha_pct = alpha_pct;
    g_settings.reduce_transparency = reduce ? 1u : 0u;
    settings_clamp();
    settings_save();
}

void settings_set_dock(u32 position, bool autohide) {
    g_settings.dock_position = position;
    g_settings.dock_autohide = autohide ? 1u : 0u;
    settings_clamp();
    settings_save();
}

void settings_set_brightness(u32 pct) {
    g_settings.brightness_pct = pct;
    settings_clamp();
    settings_save();
}

void settings_set_dnd(bool on) {
    g_settings.dnd = on ? 1u : 0u;
    settings_save();
}
