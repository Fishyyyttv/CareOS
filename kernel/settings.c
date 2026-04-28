#include "kernel.h"

#define SETTINGS_MAGIC    0x43535447u /* CSTG */
#define SETTINGS_VERSION  2u

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
    g_settings.wallpaper = 0;
    g_settings.taskbar_centered = 1;
    g_settings.wifi_connected = false;
    g_settings.wifi_ssid[0] = '\0';
    g_settings.wifi_pass[0] = '\0';
}

static void settings_clamp(void) {
    if (g_settings.mouse_sensitivity < 40) g_settings.mouse_sensitivity = 40;
    if (g_settings.mouse_sensitivity > 200) g_settings.mouse_sensitivity = 200;
    if (g_settings.theme > 1) g_settings.theme = 0;
    if (g_settings.wallpaper > 5) g_settings.wallpaper = 0;
    g_settings.taskbar_centered = g_settings.taskbar_centered ? 1u : 0u;
}

static void settings_save(void) {
    if (!settings_available()) return;

    kmemset(settings_io, 0, sizeof(settings_io));

    settings_blob_v2_t *b = (settings_blob_v2_t*)settings_io;
    b->magic = SETTINGS_MAGIC;
    b->version = SETTINGS_VERSION;
    b->theme = g_settings.theme;
    b->mouse_sensitivity = g_settings.mouse_sensitivity;
    b->boot_fast = g_settings.boot_fast;
    b->clock_24h = g_settings.clock_24h;
    b->wallpaper = g_settings.wallpaper;
    b->taskbar_centered = g_settings.taskbar_centered;
    b->wifi_connected = g_settings.wifi_connected ? 1 : 0;
    kstrncpy(b->wifi_ssid, g_settings.wifi_ssid, sizeof(b->wifi_ssid) - 1);
    b->wifi_ssid[sizeof(b->wifi_ssid) - 1] = '\0';
    kstrncpy(b->wifi_pass, g_settings.wifi_pass, sizeof(b->wifi_pass) - 1);
    b->wifi_pass[sizeof(b->wifi_pass) - 1] = '\0';

    b->checksum = 0;
    b->checksum = fnv1a32((const u8*)b, sizeof(*b));

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
        return;
    }

    u32 lba = settings_lba();
    for (u32 i = 0; i < CAREOS_DISK_SETTINGS_SECTORS; i++) {
        if (ata_read_sectors(lba + i, 1, settings_io + i * 512u) != 0) {
            serial_write("[settings] read failed, using defaults\n");
            settings_save();
            return;
        }
    }

    u32 magic = *(u32*)settings_io;
    u32 version = *((u32*)settings_io + 1);

    if (magic != SETTINGS_MAGIC) {
        serial_write("[settings] no prior settings, writing defaults\n");
        settings_save();
        return;
    }

    if (version == SETTINGS_VERSION && settings_load_v2((const settings_blob_v2_t*)settings_io)) {
        serial_write("[settings] loaded v2 settings from disk\n");
        return;
    }

    if (version == 1u && settings_load_v1((const settings_blob_v1_t*)settings_io)) {
        serial_write("[settings] migrated v1 settings to v2\n");
        settings_save();
        return;
    }

    serial_write("[settings] checksum mismatch, using defaults\n");
    settings_defaults();
    settings_save();
}

const careos_settings_t *settings_get(void) {
    return &g_settings;
}

void settings_set_theme(u32 theme) {
    g_settings.theme = theme;
    settings_clamp();
    settings_save();
}

void settings_set_mouse_sensitivity(u32 pct) {
    g_settings.mouse_sensitivity = pct;
    settings_clamp();
    settings_save();
}

void settings_set_boot_fast(bool enabled) {
    g_settings.boot_fast = enabled ? 1u : 0u;
    settings_save();
}

void settings_set_clock_24h(bool enabled) {
    g_settings.clock_24h = enabled ? 1u : 0u;
    settings_save();
}

void settings_set_wallpaper(u32 wallpaper) {
    g_settings.wallpaper = wallpaper;
    settings_clamp();
    settings_save();
}

void settings_set_taskbar_centered(bool centered) {
    g_settings.taskbar_centered = centered ? 1u : 0u;
    settings_save();
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
