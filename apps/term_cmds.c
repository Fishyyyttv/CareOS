/* apps/term_cmds.c -- Terminal command table and completion model (pure). */
#include "term_cmds.h"

const term_cmd_t TERM_CMDS[] = {
    { "ls",       TG_FILES,   "list a folder" },
    { "cd",       TG_FILES,   "change folder" },
    { "pwd",      TG_FILES,   "where am I" },
    { "cat",      TG_FILES,   "show a file" },
    { "mkdir",    TG_FILES,   "new folder" },
    { "rm",       TG_FILES,   "delete" },
    { "touch",    TG_FILES,   "new empty file" },
    { "echo",     TG_FILES,   "print text" },
    { "ifconfig", TG_NETWORK, "addresses" },
    { "ping",     TG_NETWORK, "reach a host" },
    { "curl",     TG_NETWORK, "fetch a page" },
    { "wifi",     TG_NETWORK, "wireless profile" },
    { "network",  TG_NETWORK, "link status" },
    { "uname",    TG_SYSTEM,  "system name" },
    { "date",     TG_SYSTEM,  "date and time" },
    { "free",     TG_SYSTEM,  "memory use" },
    { "uptime",   TG_SYSTEM,  "time since boot" },
    { "sysinfo",  TG_SYSTEM,  "system details" },
    { "ps",       TG_SYSTEM,  "running tasks" },
    { "whoami",   TG_SYSTEM,  "current user" },
    { "carepkg",  TG_CAREOS,  "packages" },
    { "dmesg",    TG_CAREOS,  "kernel log" },
    { "settings", TG_CAREOS,  "show or set" },
    { "res",      TG_CAREOS,  "resource caches" },
    { "perf",     TG_CAREOS,  "frame-time HUD" },
    { "theme",    TG_LOOK,    "terminal colours" },
    { "opacity",  TG_LOOK,    "transparency" },
    { "clear",    TG_LOOK,    "clear the screen" },
    { "care",     TG_CARE,    "run a script" },
    { "help",     TG_CARE,    "this list" },
};
const u32 TERM_CMD_COUNT = sizeof(TERM_CMDS) / sizeof(TERM_CMDS[0]);

const term_comp_t TERM_COMPS[] = {
    { "ls",               "list a folder" },
    { "cd",               "change folder" },
    { "pwd",              "where am I" },
    { "cat",              "show a file" },
    { "mkdir",            "new folder" },
    { "rm",               "delete" },
    { "touch",            "new empty file" },
    { "echo",             "print text" },
    { "ifconfig",         "addresses" },
    { "ping",             "reach a host" },
    { "curl",             "fetch a page" },
    { "wifi status",      "wireless status" },
    { "wifi scan",        "nearby networks" },
    { "network --status", "link up or down" },
    { "network --ip",     "show address" },
    { "uname",            "system name" },
    { "date",             "date and time" },
    { "free",             "memory use" },
    { "uptime",           "time since boot" },
    { "sysinfo --version","CareOS version" },
    { "sysinfo --mem",    "free memory" },
    { "ps",               "running tasks" },
    { "whoami",           "current user" },
    { "carepkg list",     "installed packages" },
    { "dmesg",            "kernel log" },
    { "settings show",    "saved settings" },
    { "res",              "resource caches" },
    { "perf on",          "frame-time HUD" },
    { "perf off",         "hide the HUD" },
    { "theme list",       "terminal colours" },
    { "opacity",          "transparency" },
    { "clear",            "clear the screen" },
    { "care",             "run a script" },
    { "help",             "all commands" },
};
const u32 TERM_COMP_COUNT = sizeof(TERM_COMPS) / sizeof(TERM_COMPS[0]);

const char *term_group_name(u32 group) {
    static const char *const names[TG_COUNT] = {
        "Files", "Network", "System", "CareOS", "Look", "Care language"
    };
    return group < TG_COUNT ? names[group] : "";
}

u32 term_help_line(u32 group, char *out, u32 cap) {
    u32 len = 0;
    if (!out || cap == 0) return 0;
    out[0] = '\0';
    for (u32 i = 0; i < TERM_CMD_COUNT; i++) {
        if (TERM_CMDS[i].group != group) continue;
        const char *s = TERM_CMDS[i].name;
        const char *extra = (group == TG_CARE && !kstrncmp(s, "care", 5)) ? " <file.cl>" : "";
        if (len && len + 1 < cap) out[len++] = ' ';
        for (const char *p = s; *p && len + 1 < cap; p++) out[len++] = *p;
        for (const char *p = extra; *p && len + 1 < cap; p++) out[len++] = *p;
        out[len] = '\0';
    }
    return len;
}

#define TD_MAX 32
u32 term_edit_distance(const char *a, const char *b, u32 limit) {
    u32 la = (u32)kstrlen(a), lb = (u32)kstrlen(b);
    if (la >= TD_MAX || lb >= TD_MAX) return limit + 1;
    if ((la > lb ? la - lb : lb - la) > limit) return limit + 1;
    static u32 d[TD_MAX][TD_MAX];      /* 4 KB: off the kernel stack */
    for (u32 i = 0; i <= la; i++) d[i][0] = i;
    for (u32 j = 0; j <= lb; j++) d[0][j] = j;
    for (u32 i = 1; i <= la; i++) {
        u32 row_min = 0xFFFFFFFFu;
        for (u32 j = 1; j <= lb; j++) {
            u32 cost = a[i - 1] == b[j - 1] ? 0 : 1;
            u32 v = d[i - 1][j] + 1;
            if (d[i][j - 1] + 1 < v) v = d[i][j - 1] + 1;
            if (d[i - 1][j - 1] + cost < v) v = d[i - 1][j - 1] + cost;
            if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1] &&
                d[i - 2][j - 2] + 1 < v)
                v = d[i - 2][j - 2] + 1;
            d[i][j] = v;
            if (v < row_min) row_min = v;
        }
        if (row_min > limit) return limit + 1;
    }
    return d[la][lb] > limit ? limit + 1 : d[la][lb];
}

const char *term_suggest_cmd(const char *typed) {
    if (!typed || !typed[0]) return NULL;
    u32 tl = (u32)kstrlen(typed);
    const char *best = NULL;
    u32 best_d = 3, best_same = 0;
    for (u32 i = 0; i < TERM_CMD_COUNT; i++) {
        const char *c = TERM_CMDS[i].name;
        u32 d = term_edit_distance(typed, c, 2);
        if (d == 0) return NULL;                 /* already a command */
        if (d > 2 || d >= tl) continue;
        u32 same = c[0] == typed[0];
        if (d < best_d || (d == best_d && same > best_same)) {
            best = c; best_d = d; best_same = same;
        }
    }
    return best;
}

u32 term_comp_match(const char *input, const term_comp_t *src, u32 n, u32 *out, u32 max) {
    u32 il = input ? (u32)kstrlen(input) : 0, count = 0;
    for (u32 i = 0; i < n; i++) {
        const char *t = src[i].text;
        if (!t || (u32)kstrlen(t) <= il) continue;
        if (il && kstrncmp(t, input, il)) continue;
        if (count < max && out) out[count] = i;
        count++;
    }
    return count;
}

u32 term_comp_common(const term_comp_t *src, const u32 *idx, u32 n) {
    if (n == 0) return 0;
    const char *first = src[idx[0]].text;
    u32 len = (u32)kstrlen(first);
    for (u32 k = 1; k < n; k++) {
        const char *t = src[idx[k]].text;
        u32 j = 0;
        while (j < len && t[j] && t[j] == first[j]) j++;
        len = j;
    }
    return len;
}
