/* gui/pip/pip.c -- Pip at runtime. See pip.h and the spec
 * docs/superpowers/specs/2026-10-04-careos-pip-design.md. */
#include "pip.h"
#include "pip_sprite.h"
#include "../../apps/apps_common.h"

static pip_brain_t g_b;
static bool g_ready, g_saved, g_dirty;
static u32  g_last_save_ms, g_last_tick_min;
static pip_mood_t g_mood = PM_IDLE;
static u32  g_mood_start_ms;
static char g_bubble[64];
static u32  g_bubble_until;
static i32  g_anchor_x = -100000, g_anchor_y = -100000;
static i32  g_mx, g_my;
static u32  g_blink_at;

static void pip_path(char *out) { app_home_path(out); kstrcat(out, "/.pip"); }

u32 pip_today(void) {
    rtc_time_t t; clock_read_local(&t);
    return (u32)t.year * 10000u + (u32)t.month * 100u + t.day;
}
static u32 now_min(void) {
    rtc_time_t t; clock_read_local(&t);
    return pip_minutes((u32)t.year * 10000u + (u32)t.month * 100u + t.day, t.hour, t.minute);
}

static void save_now(void) {
    if (!g_saved) return;
    char path[80]; pip_path(path);
    char buf[512];
    u32 n = pip_brain_save(&g_b, buf, sizeof buf);
    fs_node_t *f = vfs_resolve_path(path);
    if (!f) {
        char home[64]; app_home_path(home);
        fs_node_t *h = vfs_resolve_path(home);
        if (h) f = vfs_mkfile(h, ".pip");
    }
    if (f) vfs_write(f, buf, n);
    g_dirty = false;
    g_last_save_ms = timer_ms();
}

void pip_session_start(void) {
    u32 today = pip_today(), nm = now_min();
    char home[64]; app_home_path(home);
    g_saved = user_current_uid() == 0 || vfs_resolve_path(home) != NULL;
    char path[80]; pip_path(path);
    fs_node_t *f = g_saved ? vfs_resolve_path(path) : NULL;
    if (!f || !pip_brain_load(&g_b, vfs_file_str(f), f->size)) pip_brain_new(&g_b, today, nm);
    u32 away = pip_brain_signin(&g_b, today, nm);
    g_ready = true;
    g_last_tick_min = nm;
    g_mood = PM_IDLE; g_mood_start_ms = timer_ms();
    pip_sprite_flush();
#ifdef CAREOS_PERF_BENCH_AT_BOOT
    pip_desktop_go_play();                   /* bench with Pip roaming the whole run */
#endif
    if (away >= 60) {
        pip_brain_event(&g_b, PE_WELCOME_BACK, timer_ms(), 0);
        pip_say("You're back!", 3000);
    }
    g_dirty = true;
    save_now();
}

void pip_session_end(void) { if (g_ready) save_now(); g_ready = false; pip_sprite_flush(); }

pip_brain_t *pip_brain(void) { return &g_b; }
pip_mood_t pip_mood(void) { return g_mood; }
bool pip_saved(void) { return g_saved; }
void pip_mark_dirty(void) { g_dirty = true; }
void pip_set_anchor(i32 cx, i32 cy) { g_anchor_x = cx; g_anchor_y = cy; }

void pip_set_look(pip_look_t look) { g_b.look = look; g_dirty = true; }
void pip_set_mode(pip_mode_t mode) { g_b.mode = (u8)mode; g_dirty = true; }

void pip_say(const char *text, u32 ms) {
    if (!text) return;
    kstrncpy(g_bubble, text, sizeof g_bubble - 1);
    g_bubble[sizeof g_bubble - 1] = '\0';
    g_bubble_until = timer_ms() + ms;
}
const char *pip_bubble_text(void) { return (g_bubble[0] && timer_ms() < g_bubble_until) ? g_bubble : NULL; }

static const char *const EVENT_LINES[] = {
    "Hehe, that tickles!", "It worked!!", "Hmm, that didn't run.", "Huh? What's that?",
    "Hmm...", "You're back!", NULL
};

void pip_event(pip_event_t ev) {
    if (!g_ready) return;
    u32 now = timer_ms();
    if (ev == PE_PAT) { pip_brain_pat(&g_b, now_min()); g_dirty = true; }
    pip_brain_event(&g_b, ev, now, 0);
    if (ev < PE_SCRIPT && EVENT_LINES[ev] && (!pip_bubble_text() || ev != PE_PAT)) pip_say(EVENT_LINES[ev], 2600);
}

bool pip_script_mood(const char *name) {
    for (u32 m = 0; m < PM_COUNT; m++) {
        const char *l = pip_mood_label(m);
        bool eq = true;
        for (u32 i = 0; name[i] || l[i]; i++) {
            char a = name[i], b = l[i];
            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b) { eq = false; break; }
        }
        if (eq || (m == PM_IDLE && !kstrcmp(name, "idle"))) {
            pip_brain_event(&g_b, PE_SCRIPT, timer_ms(), (u8)m);
            return true;
        }
    }
    return false;
}

void pip_tick(i32 mx, i32 my, u32 idle_ms) {
    if (!g_ready) return;
    g_mx = mx; g_my = my;
    u32 nm = now_min();
    if (nm != g_last_tick_min) { pip_brain_advance(&g_b, nm); g_last_tick_min = nm; g_dirty = true; }
    rtc_time_t t; clock_read_local(&t);
    i32 dx = mx - g_anchor_x, dy = my - g_anchor_y;
    pip_ctx_t c = { idle_ms, t.hour, dx * dx + dy * dy < 200 * 200 };
    pip_mood_t m = pip_brain_mood(&g_b, &c, timer_ms());
    if (m != g_mood) { g_mood = m; g_mood_start_ms = timer_ms(); }
    if (g_dirty && timer_ms() - g_last_save_ms > 10000) save_now();
}

void pip_draw(i32 x, i32 y, u32 size, u32 mood, bool still) {
    pip_pose_t p;
    p.mood = (u8)(mood < PM_COUNT ? mood : g_mood);
    u32 cyc = pip_mood_cycle_ms(p.mood);
    u32 t = timer_ms() - g_mood_start_ms;
    p.phase = (u16)((t % cyc) * 65536u / cyc);
    p.still = still;
    /* blink every 4.2 s for about 120 ms */
    u32 now = timer_ms();
    if (now - g_blink_at > 4200) g_blink_at = now;
    p.blink = !still && now - g_blink_at < 120;
    p.look_x = 0; p.look_y = 0;
    if (p.mood == PM_WATCHING && g_anchor_x > -100000) {
        i32 dx = g_mx - g_anchor_x, dy = g_my - g_anchor_y;
        p.look_x = (i8)(dx > 40 ? 6 : dx < -40 ? -6 : dx * 6 / 40);
        p.look_y = (i8)(dy > 40 ? 4 : dy < -40 ? -4 : dy * 4 / 40);
    }
    pip_frame_t f;
    if (!pip_sprite_get(&g_b.look, &p, size, &f)) return;
    gfx_blend_argb(x - f.ox, y - f.oy, f.px, f.w, f.h);
}

void pip_cameo(rect_t r, u32 kind) {
    if (!g_ready) {                                    /* the login screen: nobody's Pip */
        if (kind == 0) return;
        pip_brain_new(&g_b, pip_today(), now_min());
    }
    u32 size = (u32)(r.h < r.w ? r.h : r.w);
    if (size > 140) size = 140;
    i32 x = r.x + (r.w - (i32)size) / 2, y = r.y + (r.h - (i32)size) / 2;
    pip_draw(x, y, size, kind == 0 ? PM_SLEEPING : PM_CONFUSED, false);
}
