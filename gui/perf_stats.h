#ifndef CAREOS_PERF_STATS_H
#define CAREOS_PERF_STATS_H
#include "pure.h"

#define PERF_WIN 128
typedef struct { u32 v[PERF_WIN]; u32 n; u32 head; } perf_ring_t;
void perf_ring_push(perf_ring_t *r, u32 v);
u32 perf_ring_avg(const perf_ring_t *r);
u32 perf_ring_max(const perf_ring_t *r);
u32 perf_ring_pct(const perf_ring_t *r, u32 pct);

#endif
