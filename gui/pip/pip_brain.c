/* gui/pip/pip_brain.c -- Pip's pet model (pure). See pip_brain.h. */
#include "pip_brain.h"

#define CATCHUP_MAX  (3u * 1440u)
#define HAPPY_FLOOR  30
#define PAT_GAIN     8
#define PAT_CAP      24

/* Days since 1970 for a civil date (Howard Hinnant's algorithm). */
static i32 days_civil(i32 y, u32 m, u32 d) {
    y -= m <= 2;
    i32 era = (y >= 0 ? y : y - 399) / 400;
    u32 yoe = (u32)(y - era * 400);
    u32 doy = (153 * (m + (m > 2 ? (u32)-3 : 9)) + 2) / 5 + d - 1;
    u32 doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (i32)doe - 719468;
}

u32 pip_day_number(u32 ymd) {
    i32 y = (i32)(ymd / 10000), m = (i32)(ymd / 100 % 100), d = (i32)(ymd % 100);
    if (m < 1 || m > 12 || d < 1 || d > 31) return 0;
    i32 n = days_civil(y, (u32)m, (u32)d) - days_civil(2000, 1, 1);
    return n < 0 ? 0 : (u32)n;
}
u32 pip_minutes(u32 ymd, u32 hour, u32 minute) { return pip_day_number(ymd) * 1440u + hour * 60u + minute; }

static void copy_name(char *dst, const char *src, u32 n) {
    u32 o = 0;
    for (u32 i = 0; i < n && o < 14; i++) {
        char c = src[i];
        if (c >= 32 && c < 127) dst[o++] = c;
    }
    dst[o] = '\0';
    if (!o) { dst[0] = 'P'; dst[1] = 'i'; dst[2] = 'p'; dst[3] = '\0'; }
}

void pip_brain_new(pip_brain_t *b, u32 today, u32 now_min) {
    for (u32 i = 0; i < sizeof *b; i++) ((u8 *)b)[i] = 0;
    copy_name(b->name, "Pip", 3);
    b->happy = 70; b->energy = 80;
    b->born_day = today; b->last_day = today; b->days = 1;
    b->seen_min = now_min; b->hour_start_min = now_min;
    pip_brain_defaults(b);
}

void pip_brain_defaults(pip_brain_t *b) {
    b->roam = b->follow = b->nap = true;
    b->enabled = b->show = b->anim = b->tips = true;
    b->mode = PIP_PERCH;
}

static u8 clamp100(i32 v) { return (u8)(v < 0 ? 0 : v > 100 ? 100 : v); }

void pip_brain_advance(pip_brain_t *b, u32 now_min) {
    if (now_min <= b->seen_min) return;                  /* clock went backwards: ignore */
    u32 dt = now_min - b->seen_min;
    if (dt > CATCHUP_MAX) dt = CATCHUP_MAX;
    b->seen_min = now_min;
    u32 h = b->happy_carry + dt;
    b->happy = clamp100((i32)b->happy - (i32)(h / 30));
    b->happy_carry = (u16)(h % 30);
    if (b->mode == PIP_NAP) {
        b->energy = clamp100((i32)b->energy + 2 * (i32)dt);
        b->energy_carry = 0;
    } else if (b->mode == PIP_ROAM) {
        u32 e = b->energy_carry + dt;
        b->energy = clamp100((i32)b->energy - (i32)(e / 2));
        b->energy_carry = (u16)(e % 2);
    }
}

u32 pip_brain_signin(pip_brain_t *b, u32 today, u32 now_min) {
    u32 away = now_min > b->seen_min ? now_min - b->seen_min : 0;
    if (away > CATCHUP_MAX) away = CATCHUP_MAX;
    if (away) {
        i32 h = (i32)b->happy - (i32)(away / 30);
        if (b->happy >= HAPPY_FLOOR && h < HAPPY_FLOOR) h = HAPPY_FLOOR;   /* missed you, not miserable */
        if (b->happy < HAPPY_FLOOR) h = b->happy;
        b->happy = clamp100(h);
        b->energy = away >= 360 ? 100 : clamp100((i32)b->energy + 2 * (i32)away);
    }
    if (now_min > b->seen_min) b->seen_min = now_min;
    if (pip_day_number(today) > pip_day_number(b->last_day)) {
        b->days++;
        b->friend_pts = (u16)(b->friend_pts + 5);
        b->last_day = today;
    }
    b->mode = PIP_PERCH;
    return away;
}

void pip_brain_pat(pip_brain_t *b, u32 now_min) {
    if (now_min >= b->hour_start_min + 60 || now_min < b->hour_start_min) {
        b->hour_start_min = now_min;
        b->pat_gain_hour = 0;
    }
    u32 room = PAT_CAP > b->pat_gain_hour ? PAT_CAP - b->pat_gain_hour : 0;
    u32 gain = room < PAT_GAIN ? room : PAT_GAIN;
    b->happy = clamp100((i32)b->happy + (i32)gain);
    b->pat_gain_hour = (u8)(b->pat_gain_hour + gain);
    if (b->friend_pts < 60000) b->friend_pts++;
}

void pip_brain_event(pip_brain_t *b, pip_event_t ev, u32 now_ms, u8 script_mood) {
    u8 mood; u32 ms;
    switch (ev) {
    case PE_PAT:          mood = PM_HAPPY;    ms = 6000; break;
    case PE_RUN_OK:       mood = PM_EXCITED;  ms = 5000; break;
    case PE_RUN_FAIL:     mood = PM_SAD;      ms = 8000; break;
    case PE_CONFUSED:     mood = PM_CONFUSED; ms = 4000; break;
    case PE_LOADING:      mood = PM_THINKING; ms = 4000; break;
    case PE_WELCOME_BACK: mood = PM_EXCITED;  ms = 6000; break;
    case PE_GREET:        mood = PM_HAPPY;    ms = 4500; break;
    case PE_OPEN_HOME:    mood = PM_EXCITED;  ms = 2500; break;
    case PE_CLOSE_HOME:   mood = PM_HAPPY;    ms = 2000; break;
    case PE_ACTION:       mood = PM_PLAYING;  ms = 1800; break;
    case PE_THINK:        mood = PM_THINKING; ms = 1500; break;
    case PE_DONE:         mood = PM_EXCITED;  ms = 3500; break;
    case PE_OOPS:         mood = PM_CONFUSED; ms = 3500; break;
    default:              mood = script_mood < PM_COUNT ? script_mood : PM_IDLE; ms = 6000; break;
    }
    b->event_mood = mood;
    b->event_until_ms = now_ms + ms;
}

pip_mood_t pip_brain_mood(const pip_brain_t *b, const pip_ctx_t *c, u32 now_ms) {
    if (b->event_until_ms && now_ms < b->event_until_ms) return (pip_mood_t)b->event_mood;
    bool night = c->hour >= 23 || c->hour < 7;
    if (b->mode == PIP_NAP || b->energy < 15 || (b->nap && c->idle_ms >= 10u * 60000u) ||
        (night && c->idle_ms >= 2u * 60000u))
        return PM_SLEEPING;
    if (b->happy < 25) return PM_SAD;
    if (b->mode == PIP_ROAM) return PM_PLAYING;
    if (b->follow && c->cursor_near) return PM_WATCHING;
    return PM_IDLE;
}

u32 pip_brain_level(const pip_brain_t *b) { u32 l = 1 + b->friend_pts / 40u; return l > 10 ? 10 : l; }

u32 pip_brain_age_days(const pip_brain_t *b, u32 today) {
    u32 t = pip_day_number(today), s = pip_day_number(b->born_day);
    return t > s ? t - s : 0;
}

/* ---- ~/.pip ---- */
static u32 put(char *o, u32 cap, u32 n, const char *s) {
    while (*s && n + 1 < cap) o[n++] = *s++;
    if (n < cap) o[n] = '\0';
    return n;
}
static u32 put_kv(char *o, u32 cap, u32 n, const char *k, u32 v) {
    char d[12]; u32 i = 0;
    do { d[i++] = (char)('0' + v % 10); v /= 10; } while (v && i < 11);
    n = put(o, cap, n, k); n = put(o, cap, n, "=");
    while (i && n + 1 < cap) o[n++] = d[--i];
    return put(o, cap, n, "\n");
}

u32 pip_brain_save(const pip_brain_t *b, char *o, u32 cap) {
    if (!o || cap < 64) return 0;
    u32 n = put(o, cap, 0, "PIP 1\nname=");
    n = put(o, cap, n, b->name); n = put(o, cap, n, "\n");
    n = put_kv(o, cap, n, "color", b->look.color);
    n = put_kv(o, cap, n, "hat", b->look.hat);
    n = put_kv(o, cap, n, "outfit", b->look.outfit);
    n = put_kv(o, cap, n, "acc", b->look.acc);
    n = put_kv(o, cap, n, "effect", b->look.effect);
    n = put_kv(o, cap, n, "happy", b->happy);
    n = put_kv(o, cap, n, "energy", b->energy);
    n = put_kv(o, cap, n, "friend", b->friend_pts);
    n = put_kv(o, cap, n, "born", b->born_day);
    n = put_kv(o, cap, n, "lastday", b->last_day);
    n = put_kv(o, cap, n, "days", b->days);
    n = put_kv(o, cap, n, "seen", b->seen_min);
    n = put_kv(o, cap, n, "roam", b->roam);
    n = put_kv(o, cap, n, "follow", b->follow);
    n = put_kv(o, cap, n, "nap", b->nap);
    n = put_kv(o, cap, n, "mode", b->mode);
    n = put_kv(o, cap, n, "on", b->enabled);
    n = put_kv(o, cap, n, "show", b->show);
    n = put_kv(o, cap, n, "anim", b->anim);
    n = put_kv(o, cap, n, "tips", b->tips);
    n = put_kv(o, cap, n, "intro", b->intro_seen);
    return n;
}

static bool key_eq(const char *s, u32 n, const char *k) {
    u32 i = 0;
    for (; k[i]; i++) if (i >= n || s[i] != k[i]) return false;
    return i == n;
}
static i64 parse_int(const char *s, u32 n) {
    i64 v = 0; bool neg = false; u32 i = 0;
    if (n && s[0] == '-') { neg = true; i = 1; }
    for (; i < n && s[i] >= '0' && s[i] <= '9'; i++) if (v < 100000000) v = v * 10 + (s[i] - '0');
    return neg ? -v : v;
}
static u32 clampu(i64 v, u32 lo, u32 hi) { return v < (i64)lo ? lo : v > (i64)hi ? hi : (u32)v; }

bool pip_brain_load(pip_brain_t *b, const char *t, u32 len) {
    if (!t || len < 6 || t[0] != 'P' || t[1] != 'I' || t[2] != 'P' || t[3] != ' ' || t[4] != '1') return false;
    pip_brain_new(b, 20260101, 0);
    u32 i = 0;
    while (i < len && t[i] != '\n') i++;
    while (i < len) {
        i++;
        u32 ls = i;
        while (i < len && t[i] != '\n') i++;
        const char *l = t + ls; u32 n = i - ls, eq = 0;
        while (eq < n && l[eq] != '=') eq++;
        if (eq == n) continue;
        const char *v = l + eq + 1; u32 vn = n - eq - 1;
        i64 x = parse_int(v, vn);
        if (key_eq(l, eq, "name"))         copy_name(b->name, v, vn);
        else if (key_eq(l, eq, "color"))   b->look.color = (u8)(x >= 0 && x < PIP_COLORS ? x : 0);
        else if (key_eq(l, eq, "hat"))     b->look.hat = (u8)(x >= 0 && x < PIP_HATS ? x : 0);
        else if (key_eq(l, eq, "outfit"))  b->look.outfit = (u8)(x >= 0 && x < PIP_OUTFITS ? x : 0);
        else if (key_eq(l, eq, "acc"))     b->look.acc = (u8)(x >= 0 && x < PIP_ACCS ? x : 0);
        else if (key_eq(l, eq, "effect"))  b->look.effect = (u8)(x >= 0 && x < PIP_EFFECTS ? x : 0);
        else if (key_eq(l, eq, "happy"))   b->happy = (u8)clampu(x, 0, 100);
        else if (key_eq(l, eq, "energy"))  b->energy = (u8)clampu(x, 0, 100);
        else if (key_eq(l, eq, "friend"))  b->friend_pts = (u16)clampu(x, 0, 60000);
        else if (key_eq(l, eq, "born"))    b->born_day = clampu(x, 20000101, 99991231);
        else if (key_eq(l, eq, "lastday")) b->last_day = clampu(x, 20000101, 99991231);
        else if (key_eq(l, eq, "days"))    b->days = (u16)clampu(x, 1, 60000);
        else if (key_eq(l, eq, "seen"))    b->seen_min = clampu(x, 0, 0x7fffffff);
        else if (key_eq(l, eq, "roam"))    b->roam = x != 0;
        else if (key_eq(l, eq, "follow"))  b->follow = x != 0;
        else if (key_eq(l, eq, "nap"))     b->nap = x != 0;
        else if (key_eq(l, eq, "mode"))    b->mode = (u8)clampu(x, 0, 2);
        else if (key_eq(l, eq, "on"))      b->enabled = x != 0;
        else if (key_eq(l, eq, "show"))    b->show = x != 0;
        else if (key_eq(l, eq, "anim"))    b->anim = x != 0;
        else if (key_eq(l, eq, "tips"))    b->tips = x != 0;
        else if (key_eq(l, eq, "intro"))   b->intro_seen = x != 0;
    }
    b->hour_start_min = b->seen_min;
    return true;
}
