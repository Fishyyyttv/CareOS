/* tests/host/test_heap.c -- kernel heap allocator: O(1) accounting, peak,
 * double/stale-free guards, coalescing, overflow-safe size rounding. */
#include "pure.h"
#include <assert.h>
#include <stdint.h>
#undef kmalloc   /* pure.h maps these to libc for OTHER tests; here we test ours */
#undef kfree
void  kheap_init(void *base, size_t size);
void *kmalloc(size_t); void kfree(void *);
u32 kmem_used(void); u32 kmem_free_bytes(void); u32 kmem_peak(void); u32 kmem_largest_free(void);

static unsigned char arena[1 << 20] __attribute__((aligned(16)));

int main(void){
    kheap_init(arena, sizeof arena);
    u32 free0 = kmem_free_bytes();
    assert(kmem_used() == 0);
    void *a = kmalloc(100), *b = kmalloc(1000), *c = kmalloc(10);
    assert(a && b && c && ((uintptr_t)a & 15) == 0 && ((uintptr_t)b & 15) == 0);
    assert(kmem_used() == 112 + 1008 + 16);          /* 16-byte rounding */
    u32 peak = kmem_peak();
    assert(peak == kmem_used());
    kfree(b);
    assert(kmem_used() == 112 + 16);
    kfree(b);                                         /* double free: ignored */
    assert(kmem_used() == 112 + 16);
    kfree(a); kfree(c);
    assert(kmem_used() == 0);
    assert(kmem_peak() == peak);
    assert(kmem_free_bytes() == free0);               /* fully coalesced */
    assert(kmem_largest_free() == free0);

    /* A pointer whose header was merged away must not be freed again. */
    void *p = kmalloc(64), *q = kmalloc(64), *r = kmalloc(64);
    kfree(q); kfree(p);                               /* p absorbs q */
    kfree(q);                                         /* stale: ignored */
    assert(kmem_used() == 64);                        /* only r remains */
    kfree(r);
    assert(kmem_used() == 0 && kmem_free_bytes() == free0);

    /* Garbage pointers are ignored, not trusted. */
    unsigned char junk[64] = {0};
    kfree(junk + 48);
    assert(kmem_used() == 0);

    assert(kmalloc(0) == NULL);
    assert(kmalloc(sizeof arena) == NULL);            /* too big */
    assert(kmalloc((size_t)-5) == NULL);              /* rounding must not wrap */
    assert(kmem_used() == 0);
    puts("heap OK"); return 0;
}
