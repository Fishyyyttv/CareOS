/* apps/settings_index.c -- Settings sections and search (pure). */
#include "settings_index.h"

const settings_section_t SETTINGS_SECTIONS[SET_COUNT] = {
    { "Network",       "wi-fi wifi ethernet internet ip address router gateway dns dhcp link speed diagnostics" },
    { "Display",       "resolution screen brightness dim pointer mouse speed sensitivity" },
    { "Appearance",    "theme light dark auto wallpaper background accent colour color glass transparency blur font" },
    { "Sound",         "audio speaker beep volume mute system sounds" },
    { "Notifications", "do not disturb dnd focus alerts toasts clear" },
    { "Date & Time",   "clock 24-hour hour time zone region utc" },
    { "Accounts",      "user password avatar display name administrator profile sign" },
    { "Storage",       "disk space ext2 free used drive" },
    { "About CareOS",  "version cpu processor memory ram hardware system fast boot startup uptime" },
};

static char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

/* Case-insensitive search for word[0..n) in hay. */
static bool contains_word(const char *hay, const char *word, u32 n) {
    for (; *hay; hay++) {
        u32 k = 0;
        while (k < n && hay[k] && lower(hay[k]) == lower(word[k])) k++;
        if (k == n) return true;
    }
    return false;
}

bool settings_section_matches(u32 section, const char *query) {
    if (section >= SET_COUNT) return false;
    if (!query) return true;
    const settings_section_t *s = &SETTINGS_SECTIONS[section];
    const char *p = query;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        u32 n = 0;
        while (p[n] && p[n] != ' ') n++;
        if (!contains_word(s->title, p, n) && !contains_word(s->keywords, p, n)) return false;
        p += n;
    }
    return true;
}

u32 settings_filter(const char *query, u32 *out, u32 max) {
    u32 n = 0;
    for (u32 i = 0; i < SET_COUNT; i++) {
        if (!settings_section_matches(i, query)) continue;
        if (out && n < max) out[n] = i;
        n++;
    }
    return n;
}
