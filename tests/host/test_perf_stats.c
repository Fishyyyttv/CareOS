#include "pure.h"
#include "perf_stats.h"
#include <assert.h>

static void test_empty(void) {
    perf_ring_t r = {0};
    assert(perf_ring_avg(&r) == 0 && perf_ring_max(&r) == 0 && perf_ring_pct(&r, 95) == 0);
}
static void test_avg_max(void) {
    perf_ring_t r = {0};
    for (u32 i = 1; i <= 10; i++) perf_ring_push(&r, i * 1000);
    assert(perf_ring_avg(&r) == 5500 && perf_ring_max(&r) == 10000);
}
static void test_wraps_keeps_latest(void) {
    perf_ring_t r = {0};
    for (u32 i = 0; i < PERF_WIN; i++) perf_ring_push(&r, 99999);
    for (u32 i = 0; i < PERF_WIN; i++) perf_ring_push(&r, 10);
    assert(r.n == PERF_WIN && perf_ring_max(&r) == 10);
}
static void test_p95(void) {
    perf_ring_t r = {0};
    for (u32 i = 1; i <= 100; i++) perf_ring_push(&r, i);
    assert(perf_ring_pct(&r, 95) == 95 && perf_ring_pct(&r, 100) == 100 && perf_ring_pct(&r, 0) == 1);
}
int main(void) {
    test_empty(); test_avg_max(); test_wraps_keeps_latest(); test_p95();
    puts("perf_stats OK");
    return 0;
}
