/* =============================================================================
 * CareOS - kernel/paging.c
 * x86_64 four-level paging: PML4, PDPT, PD, PT
 * ============================================================================= */
#include "kernel.h"

/* ── Physical frame allocator ──────────────────────────────────────────────── */
#define PHYS_MEM_BYTES  (128UL * 1024 * 1024)
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

u32 pmm_alloc_frame(void) {
    for (u32 i = 0; i < BITMAP_WORDS; i++) {
        if (phys_bitmap[i] == 0xFFFFFFFF) continue;
        for (u32 b = 0; b < 32; b++) {
            if (!(phys_bitmap[i] & (1u << b))) {
                u32 frame = i * 32 + b;
                frame_set(frame);
                if (phys_free_frames) phys_free_frames--;
                return frame;
            }
        }
    }
    return (u32)~0u;
}

void pmm_free_frame(u32 frame) {
    if (frame >= FRAME_COUNT) return;
    frame_clear(frame);
    phys_free_frames++;
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

    /* Identity map first 1GB using 2MB pages (Huge pages) */
    kernel_pml4[0] = (u64)kernel_pdpt | PDE_PRESENT | PDE_RW;
    kernel_pdpt[0] = (u64)kernel_pd   | PDE_PRESENT | PDE_RW;

    for (int i = 0; i < 512; i++) {
        kernel_pd[i] = ((u64)i * 0x200000) | PDE_PRESENT | PDE_RW | PDE_4MB;
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

void paging_free_dir(pde_t *dir) {
    /* For now just free the PML4 frame */
    pmm_free_frame((u64)dir / PAGE_SIZE);
}
