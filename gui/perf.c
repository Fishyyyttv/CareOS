/* Frame timing HUD and deterministic compositor benchmark. */
#include "gui.h"
#include "perf.h"
#include "perf_stats.h"

static u64 s_tsc_per_us = 1;
static inline u64 perf_rdtsc(void) {
    u32 lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((u64)hi << 32) | lo;
}

void perf_init(void) {
    u32 t0 = timer_ms();
    while (timer_ms() == t0) __asm__ volatile("sti; hlt");
    u64 c0 = perf_rdtsc();
    u32 start = timer_ms();
    while ((u32)(timer_ms() - start) < 50u) __asm__ volatile("sti; hlt");
    u64 c1 = perf_rdtsc();
    s_tsc_per_us = (c1 - c0) / 50000u;
    if (!s_tsc_per_us) s_tsc_per_us = 1;
    char line[64];
    ksprintf(line, "[perf] tsc %d MHz\n", (int)s_tsc_per_us);
    serial_write(line);
}

u64 perf_now_us(void) { return perf_rdtsc() / s_tsc_per_us; }

static perf_ring_t s_frames;
static u64 s_frame_t0;
static u32 s_frames_this_sec, s_fps;
static u64 s_idle_us, s_idle_t0, s_window_t0;
static u32 s_cpu_pct;
static bool s_bench;
static bool s_bench_closing;
static void bench_record(u32 us);

void perf_frame_begin(void) { s_frame_t0 = perf_now_us(); }
void perf_frame_end(void) {
    u32 us = (u32)(perf_now_us() - s_frame_t0);
    perf_ring_push(&s_frames, us);
    s_frames_this_sec++;
    bench_record(us);
}

void perf_idle_begin(void) { s_idle_t0 = perf_now_us(); }
void perf_idle_end(void) { s_idle_us += perf_now_us() - s_idle_t0; }
u32 perf_cpu_busy_pct(void) { return s_cpu_pct; }

static bool s_hud;
bool perf_hud_enabled(void) { return s_hud; }
void perf_hud_set(bool on) { s_hud = on; }

static u32 s_bench_t0, s_bench_i, s_bench_frames, s_bench_over, s_bench_heap0;
static u32 s_bench_elapsed_ms;
static bool s_bench_failed;
static window_t *s_bench_prev_focus;
static u32 s_bench_prev_focus_id;
static u32 s_bench_samples[1024];
static u32 s_bench_sample_n;
static u32 s_bench_worst;
typedef struct {
    window_t *window;
    u32 instance_id;
    bool owned, was_minimized;
    rect_t rect;
} bench_window_t;
static bench_window_t s_bench_windows[4];
static i32 s_bench_x, s_bench_y;

static window_t *bench_valid_window(int i) {
    bench_window_t *entry = &s_bench_windows[i];
    window_t *w = entry->window;
    return w && w->active && w->instance_id == entry->instance_id ? w : NULL;
}

static void bench_restore_borrowed(void) {
    for (int i = 0; i < 4; i++) {
        bench_window_t *entry = &s_bench_windows[i];
        window_t *w = bench_valid_window(i);
        if (!w || entry->owned) continue;
        w->rect = entry->rect;
        w->target_rect = entry->rect;
        w->restore_rect = entry->rect;
        w->minimized = entry->was_minimized;
        w->animating = false;
        w->anim_kind = WM_ANIM_NONE;
        w->opacity = 255;
    }
    for (int i = 0; i < MAX_WINDOWS; i++) {
        window_t *w = wm_get_window(i);
        if (w && w->active) w->focused = false;
    }
    if (s_bench_prev_focus && s_bench_prev_focus->active &&
        s_bench_prev_focus->instance_id == s_bench_prev_focus_id)
        s_bench_prev_focus->focused = true;
}

static void bench_record(u32 us) {
    if (!s_bench) return;
    if (s_bench_sample_n < (u32)(sizeof(s_bench_samples) / sizeof(s_bench_samples[0])))
        s_bench_samples[s_bench_sample_n++] = us;
    if (us > s_bench_worst) s_bench_worst = us;
    if (us > 33000u) s_bench_over++;
    s_bench_frames++;
}

void perf_hud_draw(void) {
    if (!s_hud) return;
    char line[128];
    u32 avg = perf_ring_avg(&s_frames);
    ksprintf(line, "%d fps  avg %d.%d ms  p95 %d ms  cpu %d%%  heap %d MB",
              (int)s_fps, (int)(avg / 1000u), (int)(avg / 100u % 10u),
              (int)(perf_ring_pct(&s_frames, 95) / 1000u), (int)s_cpu_pct,
              (int)(kmem_used() / (1024u * 1024u)));
    i32 w = gfx_str_width(line) + 16;
    i32 x = (i32)SCREEN_W - w - 12;
    gfx_rect_blend(x, TOPBAR_H + 8, w, 22, COL_BLACK, 170);
    gfx_str(x + 8, TOPBAR_H + 12, line, COL_WHITE, COL_TRANSPARENT);
}

void perf_tick(void) {
    u64 now = perf_now_us();
    if (!s_window_t0) { s_window_t0 = now; return; }
    u64 span = now - s_window_t0;
    if (span < 1000000u) return;
    u64 idle = s_idle_us > span ? span : s_idle_us;
    s_cpu_pct = (u32)(100u - idle * 100u / span);
    s_fps = s_frames_this_sec;
    s_frames_this_sec = 0;
    s_idle_us = 0;
    s_window_t0 = now;
    if (s_hud) {
        char line[160];
        ksprintf(line, "[perf] fps=%d avg_us=%d p95_us=%d worst_us=%d cpu=%d heap_kb=%d\n",
                  (int)s_fps, (int)perf_ring_avg(&s_frames),
                  (int)perf_ring_pct(&s_frames, 95), (int)perf_ring_max(&s_frames),
                  (int)s_cpu_pct, (int)(kmem_used() / 1024u));
        serial_write(line);
    }
}

static u32 bench_pct(u32 pct) {
    if (!s_bench_sample_n) return 0;
    u32 sorted[1024];
    for (u32 i = 0; i < s_bench_sample_n; i++) {
        u32 value = s_bench_samples[i], j = i;
        while (j && sorted[j - 1] > value) { sorted[j] = sorted[j - 1]; j--; }
        sorted[j] = value;
    }
    if (pct > 100) pct = 100;
    u32 rank = (pct * s_bench_sample_n + 99u) / 100u;
    if (!rank) rank = 1;
    return sorted[rank - 1];
}

static window_t *bench_open(app_id_t app, const char *title) {
    i32 w, h;
    app_default_size(app, (i32)SCREEN_W, (i32)SCREEN_H, &w, &h);
    return wm_open(app, title, ((i32)SCREEN_W - w) / 2, ((i32)SCREEN_H - h) / 2, w, h);
}

void perf_bench_start(const char *url) {
    (void)url;
    if (perf_bench_active()) {
        serial_write("[perf-bench] REJECTED: benchmark already active\n");
        notify_push("Benchmark", "Already running", COL_YELLOW);
        return;
    }
    s_bench_failed = false;
    s_bench_elapsed_ms = 0;
    kmemset(s_bench_windows, 0, sizeof(s_bench_windows));
    s_bench_prev_focus = wm_focused();
    s_bench_prev_focus_id = s_bench_prev_focus ? s_bench_prev_focus->instance_id : 0;
    s_bench_heap0 = kmem_used();
    static const app_id_t apps[4] = { APP_TERMINAL, APP_FILES, APP_SETTINGS, APP_BROWSER };
    static const char *titles[4] = { "Terminal", "Files", "Settings", "Browser" };
    /* Snapshot all borrowed windows before opening apps can change focus. */
    for (int i = 0; i < 4; i++) {
        bench_window_t *entry = &s_bench_windows[i];
        window_t *prior = wm_find_app(apps[i]);
        if (prior) {
            entry->window = prior;
            entry->instance_id = prior->instance_id;
            entry->owned = false;
            entry->rect = prior->rect;
            entry->was_minimized = prior->minimized;
        }
    }
    for (int i = 0; i < 4; i++) {
        bench_window_t *entry = &s_bench_windows[i];
        if (entry->window) {
            entry->window = wm_open(apps[i], titles[i], 0, 0, 0, 0);
        } else {
            entry->window = bench_open(apps[i], titles[i]);
            entry->owned = entry->window != NULL;
            if (entry->window) entry->instance_id = entry->window->instance_id;
        }
        if (!entry->window) {
            s_bench_failed = true;
            break;
        }
    }
    if (s_bench_failed) {
        for (int i = 0; i < 4; i++) {
            window_t *w = bench_valid_window(i);
            if (w && s_bench_windows[i].owned) wm_close(w);
        }
        bench_restore_borrowed();
        s_bench_closing = true;
        serial_write("[perf-bench] UNAVAILABLE: could not open all four windows\n");
        notify_push("Benchmark", "Could not open 4 windows", COL_RED);
        return;
    }
    window_t *browser = bench_valid_window(3);
    if (browser) {
        s_bench_x = browser->rect.x;
        s_bench_y = browser->rect.y;
    }
    s_bench_sample_n = 0;
    s_bench_frames = s_bench_over = s_bench_i = 0;
    s_bench_worst = 0;
    s_bench_closing = false;
    s_bench_t0 = timer_ms();
    s_bench = true;
    serial_write("[perf-bench] START\n");
}

bool perf_bench_active(void) { return s_bench || s_bench_closing; }

void perf_bench_step(void) {
    if (s_bench_closing) {
        for (int i = 0; i < 4; i++)
            if (s_bench_windows[i].owned && bench_valid_window(i)) return;
        for (int i = 0; i < 4; i++)
            if (!s_bench_windows[i].owned && !bench_valid_window(i)) s_bench_failed = true;
        bench_restore_borrowed();
        s_bench_closing = false;
        if (s_bench_failed) {
            serial_write("[perf-bench] RESULT unavailable (window set changed)\n");
            return;
        }
        u32 elapsed = s_bench_elapsed_ms;
        u32 fps = s_bench_frames * 1000u / (elapsed ? elapsed : 1u);
        u32 p95 = bench_pct(95);
        bool pass = fps >= 30u && p95 <= 33000u;
        char line[220];
        u32 used = kmem_used();
        u32 delta = used >= s_bench_heap0 ? used - s_bench_heap0 : s_bench_heap0 - used;
        char delta_text[20];
        delta_text[0] = used >= s_bench_heap0 ? '+' : '-';
        kutoa(delta / 1024u, delta_text + 1, 10);
        u32 borrowed_alive = 0;
        for (int i = 0; i < 4; i++)
            if (!s_bench_windows[i].owned && bench_valid_window(i)) borrowed_alive++;
        ksprintf(line, "[perf-bench] RESULT elapsed_ms=%d fps=%d p95_ms=%d worst_ms=%d frames_over_33ms=%d borrowed_alive=%d heap_delta_kb=%s %s\n",
                  (int)elapsed,
                  (int)fps, (int)(p95 / 1000u), (int)(s_bench_worst / 1000u),
                  (int)s_bench_over, (int)borrowed_alive, delta_text, pass ? "PASS" : "FAIL");
        serial_write(line);
        notify_push("Benchmark", pass ? "PASS (>=30 fps)" : "FAIL (<30 fps)", pass ? COL_GREEN : COL_RED);
        return;
    }
    if (!s_bench) return;
    for (int i = 0; i < 4; i++) {
        window_t *w = bench_valid_window(i);
        if (!w || w->minimized || w->anim_kind == WM_ANIM_CLOSE) {
            s_bench_failed = true;
            s_bench = false;
            for (int j = 0; j < 4; j++) {
                window_t *owned = bench_valid_window(j);
                if (owned && s_bench_windows[j].owned) wm_close(owned);
            }
            bench_restore_borrowed();
            s_bench_closing = true;
            serial_write("[perf-bench] ABORTED: benchmark window changed during sampling\n");
            return;
        }
    }
    static const i8 orbit[16][2] = {
        {40,0},{37,15},{28,28},{15,37},{0,40},{-15,37},{-28,28},{-37,15},
        {-40,0},{-37,-15},{-28,-28},{-15,-37},{0,-40},{15,-37},{28,-28},{37,-15}
    };
    window_t *browser = bench_valid_window(3);
    if (browser) {
        browser->rect.x = s_bench_x + orbit[s_bench_i % 16][0];
        browser->rect.y = s_bench_y + orbit[s_bench_i % 16][1];
    }
    s_bench_i++;
    u32 elapsed = timer_ms() - s_bench_t0;
    if (elapsed < 10000u) return;
    s_bench_elapsed_ms = elapsed;
    for (int i = 0; i < 4; i++) {
        window_t *w = bench_valid_window(i);
        if (w && s_bench_windows[i].owned) wm_close(w);
    }
    s_bench = false;
    s_bench_closing = true;
}
