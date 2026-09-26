#ifndef CAREOS_PERF_H
#define CAREOS_PERF_H
#include "gui.h"

void perf_init(void);
u64 perf_now_us(void);
void perf_frame_begin(void);
void perf_frame_end(void);
void perf_idle_begin(void);
void perf_idle_end(void);
void perf_tick(void);
u32 perf_cpu_busy_pct(void);
bool perf_hud_enabled(void);
void perf_hud_set(bool on);
void perf_hud_draw(void);
void perf_bench_start(const char *url);
bool perf_bench_active(void);
void perf_bench_step(void);

#endif
