/* apps/store_model.c -- Packages model (pure). */
#include "store_model.h"

static void copy_val(char *dst, u32 cap, const char *s, u32 n) {
    while (n && (*s == ' ' || *s == '\t')) { s++; n--; }
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\r' || s[n - 1] == '\t')) n--;
    if (n >= cap) n = cap - 1;
    for (u32 i = 0; i < n; i++) dst[i] = s[i];
    dst[n] = '\0';
}

static bool key_is(const char *s, u32 n, const char *key) {
    u32 i = 0;
    for (; key[i]; i++) if (i >= n || s[i] != key[i]) return false;
    return i == n;
}

bool care_manifest_parse(const char *t, u32 len, care_manifest_t *m) {
    if (!t || !m) return false;
    for (u32 i = 0; i < sizeof *m; i++) ((char *)m)[i] = 0;
    if (len < 5 || t[0] != 'C' || t[1] != 'A' || t[2] != 'R' || t[3] != 'E' || t[4] != ' ') return false;
    u32 i = 0;
    while (i < len && t[i] != '\n') i++;                       /* skip "CARE x.y" */
    while (i < len) {
        i++;
        u32 ls = i;
        while (i < len && t[i] != '\n') i++;
        u32 n = i - ls;
        const char *line = t + ls;
        if (n >= 3 && line[0] == '-' && line[1] == '-' && line[2] == '-') break;
        u32 eq = 0;
        while (eq < n && line[eq] != '=') eq++;
        if (eq == n) continue;
        const char *v = line + eq + 1; u32 vn = n - eq - 1;
        if (key_is(line, eq, "name"))             copy_val(m->name, sizeof m->name, v, vn);
        else if (key_is(line, eq, "version"))     copy_val(m->version, sizeof m->version, v, vn);
        else if (key_is(line, eq, "description")) copy_val(m->description, sizeof m->description, v, vn);
        else if (key_is(line, eq, "category"))    copy_val(m->category, sizeof m->category, v, vn);
        else if (key_is(line, eq, "author"))      copy_val(m->author, sizeof m->author, v, vn);
    }
    if (!m->version[0]) { m->version[0] = '0'; m->version[1] = '\0'; }
    return m->name[0] != '\0';
}

int care_version_cmp(const char *a, const char *b) {
    if (!a) a = "";
    if (!b) b = "";
    for (int part = 0; part < 6; part++) {
        u32 x = 0, y = 0;
        while (*a >= '0' && *a <= '9') { x = x * 10u + (u32)(*a - '0'); a++; }
        while (*b >= '0' && *b <= '9') { y = y * 10u + (u32)(*b - '0'); b++; }
        if (x != y) return x < y ? -1 : 1;
        while (*a && *a != '.') a++;
        while (*b && *b != '.') b++;
        if (!*a && !*b) return 0;
        if (*a) a++;
        if (*b) b++;
    }
    return 0;
}

pkg_rel_t store_relation(const char *installed, const char *file) {
    if (!installed) return PKG_NEW;
    int c = care_version_cmp(file, installed);
    return c > 0 ? PKG_UPDATE : c == 0 ? PKG_SAME : PKG_OLDER;
}

store_state_t store_state(store_counts_t c) {
    if (c.updates) return STORE_UPDATES;
    if (c.fresh) return STORE_NEW;
    if (c.installed) return STORE_NOTHING_NEW;
    return STORE_EMPTY;
}
