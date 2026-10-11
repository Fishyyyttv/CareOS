/* apps/netrate.h -- throughput from free-running byte counters (pure). */
#ifndef CAREOS_NETRATE_H
#define CAREOS_NETRATE_H
#include "pure.h"

/* KB/s between two samples of a u32 byte counter taken dt_ms apart
 * (bytes per millisecond ~= KB/s). Counter wrap is handled by unsigned
 * subtraction; dt 0 counts as 1 ms. */
static inline u32 net_rate_kbs(u32 prev, u32 cur, u32 dt_ms) {
    return (cur - prev) / (dt_ms ? dt_ms : 1u);
}

/* History graphs store one byte per sample. */
static inline u32 net_rate_cap8(u32 kbs) { return kbs > 255u ? 255u : kbs; }

#endif
