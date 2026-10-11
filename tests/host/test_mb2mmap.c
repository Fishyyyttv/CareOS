/* tests/host/test_mb2mmap.c -- multiboot2 memory-map parsing and the frame
 * usability rule the PMM applies. Malformed maps must never be trusted. */
#include "pure.h"
#include "mb2mmap.h"
#include <assert.h>

static u8 mbi[512] __attribute__((aligned(8)));
static u32 len;

static void put32(u32 off, u32 v) { memcpy(mbi + off, &v, 4); }
static void put64(u32 off, u64 v) { memcpy(mbi + off, &v, 8); }

/* Build: header, one mmap tag (type 6) with n entries, end tag. */
static void build(u32 entry_size, u32 n, const u64 *base, const u64 *elen, const u32 *type) {
    memset(mbi, 0, sizeof mbi);
    u32 off = 8;
    u32 tag_size = 16 + n * entry_size;
    put32(off + 0, 6); put32(off + 4, tag_size); put32(off + 8, entry_size); put32(off + 12, 0);
    for (u32 i = 0; i < n; i++) {
        u32 e = off + 16 + i * entry_size;
        put64(e, base[i]); put64(e + 8, elen[i]); put32(e + 16, type[i]); put32(e + 20, 0);
    }
    off += (tag_size + 7) & ~7u;
    put32(off, 0); put32(off + 4, 8); off += 8;          /* end tag */
    len = off;
    put32(0, len); put32(4, 0);
}

int main(void) {
    mb2_range_t r[8];
    const u64 B[3] = { 0x0, 0x100000, 0xFEC00000 };
    const u64 L[3] = { 0x9FC00, 0x3FEE0000, 0x1000 };
    const u32 T[3] = { 1, 1, 2 };                        /* two RAM, one reserved */

    build(24, 3, B, L, T);
    u32 n = mb2_usable_ranges(mbi, len, r, 8);
    assert(n == 2);
    assert(r[0].base == 0 && r[0].end == 0x9FC00);
    assert(r[1].base == 0x100000 && r[1].end == 0x100000 + 0x3FEE0000);

    /* Larger (future) entry stride is honoured. */
    build(32, 3, B, L, T);
    assert(mb2_usable_ranges(mbi, len, r, 8) == 2);

    /* Entry size smaller than an entry: reject the map. */
    build(8, 3, B, L, T);
    assert(mb2_usable_ranges(mbi, len, r, 8) == 0);

    /* Tag claiming to extend past the MBI: reject. */
    build(24, 3, B, L, T);
    put32(8 + 4, 4000);
    assert(mb2_usable_ranges(mbi, len, r, 8) == 0);

    /* Total size field larger than the buffer we were given: reject. */
    build(24, 3, B, L, T);
    assert(mb2_usable_ranges(mbi, len - 8, r, 8) == 0);

    /* Wrap-around entry (base + len overflows) is skipped, others kept. */
    const u64 B2[2] = { 0x100000, 0xFFFFFFFFFFFFF000ULL };
    const u64 L2[2] = { 0x1000000, 0x2000 };
    const u32 T2[2] = { 1, 1 };
    build(24, 2, B2, L2, T2);
    assert(mb2_usable_ranges(mbi, len, r, 8) == 1);

    /* Frame rule: inside a usable range and outside every reserved range. */
    build(24, 3, B, L, T);
    n = mb2_usable_ranges(mbi, len, r, 8);
    mb2_range_t resv[1] = { { 0x200000, 0x201000 } };
    assert( pmm_frame_ok(r, n, resv, 1, 0x100000, 0x1000));
    assert(!pmm_frame_ok(r, n, resv, 1, 0x200000, 0x1000));   /* reserved */
    assert(!pmm_frame_ok(r, n, resv, 1, 0x9F000, 0x1000));    /* straddles end of RAM */
    assert(!pmm_frame_ok(r, n, resv, 1, 0xE0000000, 0x1000)); /* PCI hole: not RAM */
    assert(!pmm_frame_ok(r, 0, resv, 0, 0x100000, 0x1000));   /* no map: nothing usable */
    puts("mb2mmap OK");
    return 0;
}
