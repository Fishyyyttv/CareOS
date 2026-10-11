/* kernel/heap.c -- first-fit free-list heap with O(1) accounting.
 *
 * Every operation runs with interrupts masked: the scheduler preempts on the
 * timer IRQ, and background tasks (the Doom thread, the browser fetch worker)
 * allocate too, so an unguarded list update could be torn mid-way. */
#include "pure.h"
#include "heap_types.h"
#ifdef HOST_TEST
#undef kmalloc   /* pure.h aliases these to libc; this file DEFINES them */
#undef kfree
#endif

#define HDR_SIZE  sizeof(block_hdr_t)
#define MIN_SPLIT (HDR_SIZE + 16)

static block_hdr_t *heap_head;
static u8 *s_heap_lo, *s_heap_hi;       /* [lo, hi) */
static size_t s_heap_size;
static u32 s_used, s_free, s_peak;

#ifdef HOST_TEST
static inline u64 irq_save(void) { return 0; }
static inline void irq_restore(u64 f) { (void)f; }
#else
static inline u64 irq_save(void) {
    u64 f;
    __asm__ volatile("pushfq; pop %0; cli" : "=r"(f) :: "memory");
    return f;
}
static inline void irq_restore(u64 f) {
    if (f & 0x200) __asm__ volatile("sti" ::: "memory");
}
#endif

void kheap_init(void *base, size_t size) {
    heap_head = (block_hdr_t *)base;
    heap_head->magic = HEAP_MAGIC;
    heap_head->size  = size - HDR_SIZE;
    heap_head->free  = true;
    heap_head->next  = heap_head->prev = NULL;
    s_heap_lo = (u8 *)base; s_heap_hi = (u8 *)base + size;
    s_heap_size = size;
    s_used = 0; s_peak = 0; s_free = (u32)heap_head->size;
}

size_t kheap_size(void) { return s_heap_size; }

void *kmalloc(size_t sz) {
    if (!sz || sz > s_heap_size) return NULL;          /* also stops rounding wrap */
    sz = (sz + 15) & ~(size_t)15;
    u64 fl = irq_save();
    for (block_hdr_t *b = heap_head; b; b = b->next) {
        if (!b->free || b->size < sz) continue;
        if (b->size >= sz + MIN_SPLIT) {
            block_hdr_t *nb = (block_hdr_t *)((u8 *)b + HDR_SIZE + sz);
            nb->magic = HEAP_MAGIC; nb->size = b->size - sz - HDR_SIZE; nb->free = true;
            nb->next = b->next; nb->prev = b;
            if (b->next) b->next->prev = nb;
            b->next = nb; b->size = sz;
            s_free -= (u32)HDR_SIZE;                    /* new header came out of free space */
        }
        b->free = false;
        s_used += (u32)b->size; s_free -= (u32)b->size;
        if (s_used > s_peak) s_peak = s_used;
        irq_restore(fl);
        return (u8 *)b + HDR_SIZE;
    }
    irq_restore(fl);
    return NULL;
}

void kfree(void *ptr) {
    if (!ptr) return;
    u8 *p = (u8 *)ptr;
    if (p < s_heap_lo + HDR_SIZE || p >= s_heap_hi) {
        serial_write("[heap] WARN: kfree of pointer outside the heap ignored\n");
        return;
    }
    u64 fl = irq_save();
    block_hdr_t *b = (block_hdr_t *)(p - HDR_SIZE);
    if (b->magic != HEAP_MAGIC || b->free) {
        irq_restore(fl);
        serial_write("[heap] WARN: bad or double kfree ignored\n");
        return;
    }
    b->free = true;
    s_used -= (u32)b->size; s_free += (u32)b->size;
    if (b->next && b->next->free) {                    /* absorb the next block */
        block_hdr_t *n = b->next;
        b->size += HDR_SIZE + n->size;
        b->next = n->next;
        if (b->next) b->next->prev = b;
        n->magic = 0;                                   /* stale pointers to n now fail */
        s_free += (u32)HDR_SIZE;
    }
    if (b->prev && b->prev->free) {                    /* merge into the previous block */
        block_hdr_t *pv = b->prev;
        pv->size += HDR_SIZE + b->size;
        pv->next = b->next;
        if (b->next) b->next->prev = pv;
        b->magic = 0;
        s_free += (u32)HDR_SIZE;
    }
    irq_restore(fl);
}

u32 kmem_used(void)       { return s_used; }
u32 kmem_free_bytes(void) { return s_free; }
u32 kmem_peak(void)       { return s_peak; }

u32 kmem_largest_free(void) {
    u32 m = 0;
    u64 fl = irq_save();
    for (block_hdr_t *b = heap_head; b; b = b->next)
        if (b->free && b->size > m) m = (u32)b->size;
    irq_restore(fl);
    return m;
}
