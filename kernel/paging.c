/* =============================================================================
 * CareOS - kernel/paging.c
 * x86_64 four-level paging: PML4, PDPT, PD, PT
 * ============================================================================= */
#include "kernel.h"

/* ── Physical frame allocator ──────────────────────────────────────────────── */
#define PHYS_MEM_BYTES  (4096UL * 1024 * 1024)
#define FRAME_COUNT     (PHYS_MEM_BYTES / PAGE_SIZE)
#define BITMAP_WORDS    (FRAME_COUNT / 32)

static u32 phys_bitmap[BITMAP_WORDS];
static u32 phys_free_frames = 0;

static void frame_set(u32 frame) {
    phys_bitmap[frame / 32] |= (1u << (frame % 32));
}

static void frame_clear(u32 frame) {
    phys_bitmap[frame / 32] &= ~(1u << (frame % 32));
}

/* phys_bitmap/phys_free_frames are shared, unlocked global state. This
 * kernel has no spinlocks, and interrupts are enabled throughout most of
 * kernel-mode execution (including while building a new process's page
 * tables), so a timer tick can preempt mid-allocation and switch to a task
 * whose own exit path calls pmm_free_frame concurrently -- corrupting the
 * bitmap with no synchronization at all. Disable interrupts around the
 * critical section (save/restore, not unconditional sti, so this nests
 * safely if ever called from an already-cli'd context) rather than trying
 * to protect every caller individually. */
u32 pmm_alloc_frame(void) {
    u64 flags;
    __asm__ volatile ("pushfq; cli; pop %0" : "=r"(flags) :: "memory");

    u32 result = (u32)~0u;
    for (u32 i = 0; i < BITMAP_WORDS; i++) {
        if (phys_bitmap[i] == 0xFFFFFFFF) continue;
        for (u32 b = 0; b < 32; b++) {
            if (!(phys_bitmap[i] & (1u << b))) {
                u32 frame = i * 32 + b;
                frame_set(frame);
                if (phys_free_frames) phys_free_frames--;
                result = frame;
                goto done;
            }
        }
    }
done:
    __asm__ volatile ("push %0; popfq" : : "r"(flags) : "memory", "cc");
    return result;
}

/* Declared in kernel.h but never defined until now; nothing referenced it, so
 * it never surfaced as a link error. Useful for spotting frame leaks. */
u32 pmm_free_count(void) {
    return phys_free_frames;
}

void pmm_free_frame(u32 frame) {
    if (frame >= FRAME_COUNT) return;

    u64 flags;
    __asm__ volatile ("pushfq; cli; pop %0" : "=r"(flags) :: "memory");

    frame_clear(frame);
    phys_free_frames++;

    __asm__ volatile ("push %0; popfq" : : "r"(flags) : "memory", "cc");
}

/* ── Paging structures ─────────────────────────────────────────────────────── */
#define PML4_INDEX(v) (((v) >> 39) & 0x1FF)
#define PDPT_INDEX(v) (((v) >> 30) & 0x1FF)
#define PD_INDEX(v)   (((v) >> 21) & 0x1FF)
#define PT_INDEX(v)   (((v) >> 12) & 0x1FF)

static pml4e_t kernel_pml4[512] __attribute__((aligned(4096)));
static pdpte_t kernel_pdpt[512] __attribute__((aligned(4096)));
static pde_t   kernel_pd[512]   __attribute__((aligned(4096)));

static void paging_load_cr3(u64 phys_pml4) {
    __asm__ volatile ("mov %0, %%cr3" : : "r"(phys_pml4) : "memory");
}

static void page_fault_handler(registers_t *r) {
    u64 fault_addr;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(fault_addr));
    char buf[20];

    serial_write("\n[PAGE FAULT] addr=0x");
    kutoa(fault_addr >> 32, buf, 16); serial_write(buf);
    kutoa(fault_addr & 0xFFFFFFFF, buf, 16); serial_write(buf);
    serial_write(" rip=0x");
    kutoa(r->rip >> 32, buf, 16); serial_write(buf);
    kutoa(r->rip & 0xFFFFFFFF, buf, 16); serial_write(buf);
    serial_write(" err=0x");
    kutoa(r->err_code, buf, 16); serial_write(buf);
    serial_write("\n");

    kernel_panic(0x0E, "Unhandled page fault");
}

/* Internal map helper for 4K pages */
static int paging_map_internal(pml4e_t *pml4, u64 virt, u64 phys, u32 flags) {
    u32 pml4_i = PML4_INDEX(virt);
    u32 pdpt_i = PDPT_INDEX(virt);
    u32 pd_i   = PD_INDEX(virt);
    u32 pt_i   = PT_INDEX(virt);

    if (!(pml4[pml4_i] & PDE_PRESENT)) {
        u32 f = pmm_alloc_frame();
        if (f == (u32)~0u) return -1;
        pml4[pml4_i] = ((u64)f * PAGE_SIZE) | PDE_PRESENT | PDE_RW | PDE_USER;
        kmemset((void*)((u64)f * PAGE_SIZE), 0, PAGE_SIZE);
    }
    pdpte_t *pdpt = (pdpte_t*)(pml4[pml4_i] & ~0xFFFULL);

    if (!(pdpt[pdpt_i] & PDE_PRESENT)) {
        u32 f = pmm_alloc_frame();
        if (f == (u32)~0u) return -1;
        pdpt[pdpt_i] = ((u64)f * PAGE_SIZE) | PDE_PRESENT | PDE_RW | PDE_USER;
        kmemset((void*)((u64)f * PAGE_SIZE), 0, PAGE_SIZE);
    }
    pde_t *pd = (pde_t*)(pdpt[pdpt_i] & ~0xFFFULL);

    if (!(pd[pd_i] & PDE_PRESENT)) {
        u32 f = pmm_alloc_frame();
        if (f == (u32)~0u) return -1;
        pd[pd_i] = ((u64)f * PAGE_SIZE) | PDE_PRESENT | PDE_RW | PDE_USER;
        kmemset((void*)((u64)f * PAGE_SIZE), 0, PAGE_SIZE);
    }
    pte_t *pt = (pte_t*)(pd[pd_i] & ~0xFFFULL);

    pt[pt_i] = (phys & ~0xFFFULL) | flags | PDE_PRESENT;
    __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
    return 0;
}

void paging_init(void) {
    kmemset(phys_bitmap, 0xFF, sizeof(phys_bitmap));
    phys_free_frames = 0;

    u32 reserved_frames = KERNEL_RESERVED_BYTES / PAGE_SIZE;
    for (u32 f = reserved_frames; f < FRAME_COUNT; f++) {
        frame_clear(f);
        phys_free_frames++;
    }

    kmemset(kernel_pml4, 0, sizeof(kernel_pml4));
    kmemset(kernel_pdpt, 0, sizeof(kernel_pdpt));
    kmemset(kernel_pd,   0, sizeof(kernel_pd));

    /* Identity map first 2GB using 2MB pages (Huge pages) */
    kernel_pml4[0] = (u64)kernel_pdpt | PDE_PRESENT | PDE_RW;
    kernel_pdpt[0] = (u64)kernel_pd   | PDE_PRESENT | PDE_RW;
    
    /* Allocate a second PD for the second GB */
    static pde_t kernel_pd2[512] __attribute__((aligned(4096)));
    kernel_pdpt[1] = (u64)kernel_pd2 | PDE_PRESENT | PDE_RW;

    for (int i = 0; i < 512; i++) {
        kernel_pd[i]  = ((u64)i * 0x200000) | PDE_PRESENT | PDE_RW | PDE_4MB;
        kernel_pd2[i] = ((u64)(i + 512) * 0x200000) | PDE_PRESENT | PDE_RW | PDE_4MB;
    }

    register_interrupt_handler(14, page_fault_handler);
    paging_load_cr3((u64)kernel_pml4);
    serial_write("[paging] 4-level enabled\n");
}

void paging_map_mmio(u32 phys_start, u32 size) {
    u64 start = (u64)phys_start & ~0xFFFULL;
    u64 end   = ((u64)phys_start + size + 4095) & ~0xFFFULL;
    for (u64 v = start; v < end; v += PAGE_SIZE) {
        paging_map_internal(kernel_pml4, v, v, PDE_PRESENT | PDE_RW | PDE_PCD);
    }
    serial_write("[paging] mmio mapped\n");
}

pde_t *paging_create_dir(void) {
    u32 frame = pmm_alloc_frame();
    if (frame == (u32)~0u) return NULL;
    pml4e_t *pml4 = (pml4e_t*)((u64)frame * PAGE_SIZE);
    kmemset(pml4, 0, PAGE_SIZE);
    pml4[0] = kernel_pml4[0]; /* Share kernel identity map */
    return (pde_t*)pml4;
}

int paging_map(pde_t *dir_as_pml4, u64 virt, u64 phys, u32 flags) {
    return paging_map_internal((pml4e_t*)dir_as_pml4, virt, phys, flags);
}

void paging_switch_dir(pde_t *dir) {
    paging_load_cr3((u64)dir);
}

void paging_switch_kernel(void) {
    paging_load_cr3((u64)kernel_pml4);
}

void paging_free_dir(pde_t *dir) {
    pml4e_t *pml4 = (pml4e_t *)dir;

    if (pml4[USER_PML4_INDEX] & PDE_PRESENT) {
        pdpte_t *pdpt = (pdpte_t *)(pml4[USER_PML4_INDEX] & ~0xFFFULL);
        for (u32 pi = 0; pi < 512; pi++) {
            if (!(pdpt[pi] & PDE_PRESENT)) continue;
            pde_t *pd = (pde_t *)(pdpt[pi] & ~0xFFFULL);
            for (u32 di = 0; di < 512; di++) {
                if (!(pd[di] & PDE_PRESENT) || (pd[di] & PDE_4MB)) continue;
                pte_t *pt = (pte_t *)(pd[di] & ~0xFFFULL);
                for (u32 ti = 0; ti < 512; ti++) {
                    if (pt[ti] & PTE_PRESENT) {
                        pmm_free_frame((u32)((pt[ti] & ~0xFFFULL) / PAGE_SIZE));
                    }
                }
                pmm_free_frame((u32)((u64)pt / PAGE_SIZE));
            }
            pmm_free_frame((u32)((u64)pd / PAGE_SIZE));
        }
        pmm_free_frame((u32)((u64)pdpt / PAGE_SIZE));
    }

    pmm_free_frame((u32)((u64)dir / PAGE_SIZE));
}

/* A paging entry grants user access only if Present and User/Supervisor are
 * set, and write access only if R/W is also set -- at *every* level of the
 * walk, which is exactly the rule the CPU applies. The kernel's identity map
 * at PML4[0] deliberately omits PDE_USER, so this correctly refuses any
 * kernel address even though that map is present in every process's PML4. */
static bool entry_grants_user(u64 e, bool need_write) {
    if (!(e & PDE_PRESENT)) return false;
    if (!(e & PDE_USER))    return false;
    if (need_write && !(e & PDE_RW)) return false;
    return true;
}

bool paging_user_range_ok(pde_t *dir, u64 virt, u64 len, bool need_write) {
    if (!dir) return false;
    if (len == 0) return true;
    if (virt + len < virt) return false;          /* wraps past the top */

    pml4e_t *pml4 = (pml4e_t *)dir;
    u64 first = virt & ~0xFFFULL;
    u64 last  = (virt + len - 1) & ~0xFFFULL;

    for (u64 p = first; ; p += PAGE_SIZE) {
        u64 e = pml4[PML4_INDEX(p)];
        if (!entry_grants_user(e, need_write)) return false;

        pdpte_t *pdpt = (pdpte_t *)(e & ~0xFFFULL);
        e = pdpt[PDPT_INDEX(p)];
        if (!entry_grants_user(e, need_write)) return false;
        if (!(e & PDE_4MB)) {                     /* not a 1GB page: descend */
            pde_t *pd = (pde_t *)(e & ~0xFFFULL);
            e = pd[PD_INDEX(p)];
            if (!entry_grants_user(e, need_write)) return false;
            if (!(e & PDE_4MB)) {                 /* not a 2MB page: descend */
                pte_t *pt = (pte_t *)(e & ~0xFFFULL);
                if (!entry_grants_user(pt[PT_INDEX(p)], need_write)) return false;
            }
        }

        if (p == last) break;
    }
    return true;
}

u64 paging_translate(pde_t *dir, u64 virt) {
    pml4e_t *pml4 = (pml4e_t *)dir;
    u32 pml4_i = PML4_INDEX(virt);
    u32 pdpt_i = PDPT_INDEX(virt);
    u32 pd_i   = PD_INDEX(virt);
    u32 pt_i   = PT_INDEX(virt);

    if (!(pml4[pml4_i] & PDE_PRESENT)) return ~0ULL;
    pdpte_t *pdpt = (pdpte_t *)(pml4[pml4_i] & ~0xFFFULL);

    if (!(pdpt[pdpt_i] & PDE_PRESENT)) return ~0ULL;
    pde_t *pd = (pde_t *)(pdpt[pdpt_i] & ~0xFFFULL);

    if (!(pd[pd_i] & PDE_PRESENT)) return ~0ULL;
    if (pd[pd_i] & PDE_4MB) {
        return (pd[pd_i] & ~0x1FFFFFULL) | (virt & 0x1FFFFFULL);
    }
    pte_t *pt = (pte_t *)(pd[pd_i] & ~0xFFFULL);

    if (!(pt[pt_i] & PTE_PRESENT)) return ~0ULL;
    return (pt[pt_i] & ~0xFFFULL) | (virt & 0xFFFULL);
}
