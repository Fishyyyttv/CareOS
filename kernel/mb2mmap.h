/* kernel/mb2mmap.h -- multiboot2 memory map parsing (pure; host-tested). */
#ifndef CAREOS_MB2MMAP_H
#define CAREOS_MB2MMAP_H
#include "pure.h"

typedef struct { u64 base, end; } mb2_range_t;   /* [base, end) */

/* Collect the usable-RAM (type 1) ranges of the MB2 info block at mbi, whose
 * readable size is len. Any structural inconsistency (total size, tag size,
 * entry size) rejects the whole map and returns 0; entries whose range wraps
 * are skipped. Returns the number of ranges written (at most max). */
u32 mb2_usable_ranges(const u8 *mbi, u32 len, mb2_range_t *out, u32 max);

/* True if [addr, addr+size) lies inside one usable range and overlaps no
 * reserved range. With no usable ranges nothing is usable (fail closed). */
bool pmm_frame_ok(const mb2_range_t *usable, u32 nu, const mb2_range_t *resv, u32 nr,
                  u64 addr, u64 size);

#endif
