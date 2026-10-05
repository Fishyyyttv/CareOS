/* QuickJS platform hooks: no hosted libc, threads, or process access. */
#include "kernel.h"
#include "heap_types.h"

size_t careos_js_malloc_size(const void *ptr) {
    if (!ptr) return 0;
    const block_hdr_t *b = (const block_hdr_t *)((const u8 *)ptr - sizeof(block_hdr_t));
    return b->magic == HEAP_MAGIC ? (size_t)b->size : 0;
}

uint64_t careos_js_monotonic_ns(void) {
    return (uint64_t)timer_ms() * 1000000;
}

int64_t careos_js_wall_us(void) {
    rtc_time_t t;
    rtc_read(&t);
    /* Gregorian civil date -> days since 1970-01-01. RTC is treated as UTC. */
    int y = t.year - (t.month <= 2);
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned mp = (unsigned)((int)t.month + (t.month > 2 ? -3 : 9));
    unsigned doy = (153 * mp + 2) / 5 + t.day - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    int64_t days = (int64_t)era * 146097 + doe - 719468;
    return ((days * 24 + t.hour) * 3600 + t.minute * 60 + t.second) * 1000000;
}
