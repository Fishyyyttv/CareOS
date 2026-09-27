/* kernel/mb2mmap.c -- multiboot2 memory map parsing (pure; host-tested). */
#include "mb2mmap.h"

static u32 rd32(const u8 *p) { u32 v; kmemcpy(&v, p, 4); return v; }
static u64 rd64(const u8 *p) { u64 v; kmemcpy(&v, p, 8); return v; }

u32 mb2_usable_ranges(const u8 *mbi, u32 len, mb2_range_t *out, u32 max) {
    if (!mbi || len < 16) return 0;
    u32 total = rd32(mbi);
    if (total < 16 || total > len) return 0;
    u32 n = 0, off = 8;
    while (off + 8 <= total) {
        u32 type = rd32(mbi + off), size = rd32(mbi + off + 4);
        if (size < 8 || size > total - off) return 0;           /* tag overruns the MBI */
        if (type == 0) break;                                   /* end tag */
        if (type == 6) {
            if (size < 16) return 0;
            u32 esz = rd32(mbi + off + 8);
            if (esz < 24) return 0;                             /* smaller than an entry */
            for (u32 e = 16; e + esz <= size; e += esz) {
                const u8 *ent = mbi + off + e;
                u64 base = rd64(ent), elen = rd64(ent + 8);
                u32 etype = rd32(ent + 16);
                if (etype != 1 || elen == 0) continue;
                if (base + elen < base) continue;               /* wraps */
                if (n < max) { out[n].base = base; out[n].end = base + elen; n++; }
            }
        }
        off += (size + 7u) & ~7u;
    }
    return n;
}

bool pmm_frame_ok(const mb2_range_t *usable, u32 nu, const mb2_range_t *resv, u32 nr,
                  u64 addr, u64 size) {
    u64 end = addr + size;
    for (u32 i = 0; i < nr; i++)
        if (addr < resv[i].end && end > resv[i].base) return false;
    for (u32 i = 0; i < nu; i++)
        if (addr >= usable[i].base && end <= usable[i].end) return true;
    return false;
}
