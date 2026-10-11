/* include/heap_types.h -- kernel heap block header (shared by kernel/heap.c,
 * kernel/libc_shim.c's realloc, and host tests). Needs u64/bool in scope. */
#ifndef CAREOS_HEAP_TYPES_H
#define CAREOS_HEAP_TYPES_H

typedef struct block_hdr {
    u64              magic;
    u64              size;
    bool             free;
    u8               reserved[15];
    struct block_hdr *next;
    struct block_hdr *prev;
} block_hdr_t;

#define HEAP_MAGIC  0xC4E05AFE

#endif
