#include "perf_stats.h"

void perf_ring_push(perf_ring_t *r, u32 v) {
    r->v[r->head] = v;
    r->head = (r->head + 1u) % PERF_WIN;
    if (r->n < PERF_WIN) r->n++;
}

u32 perf_ring_avg(const perf_ring_t *r) {
    if (!r->n) return 0;
    u64 sum = 0;
    for (u32 i = 0; i < r->n; i++) sum += r->v[i];
    return (u32)(sum / r->n);
}

u32 perf_ring_max(const perf_ring_t *r) {
    u32 max = 0;
    for (u32 i = 0; i < r->n; i++) if (r->v[i] > max) max = r->v[i];
    return max;
}

u32 perf_ring_pct(const perf_ring_t *r, u32 pct) {
    if (!r->n) return 0;
    u32 sorted[PERF_WIN];
    for (u32 i = 0; i < r->n; i++) {
        u32 value = r->v[i], j = i;
        while (j && sorted[j - 1] > value) { sorted[j] = sorted[j - 1]; j--; }
        sorted[j] = value;
    }
    if (pct > 100) pct = 100;
    u32 rank = (pct * r->n + 99u) / 100u;
    if (!rank) rank = 1;
    return sorted[rank - 1];
}
