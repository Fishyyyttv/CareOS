/* =============================================================================
 * CareOS - kernel/elf.c
 * ELF64 executable loader (x86_64).
 *
 * Loads a statically-linked ELF64 binary from a VFS node, maps its PT_LOAD
 * segments into a fresh user page directory, and creates a ring-3 task.
 *
 * Supported: ET_EXEC, EM_X86_64, little-endian, static linkage.
 * ============================================================================= */

#include "kernel.h"

/* ── ELF64 constants ────────────────────────────────────────────────────────── */
#define ELF_MAGIC       0x464C457Fu  /* "\x7fELF" little-endian */
#define ELFCLASS64      2
#define ELFDATA2LSB     1
#define ET_EXEC         2
#define EM_X86_64       62
#define PT_LOAD         1
#define PF_X            0x1u
#define PF_W            0x2u
#define PF_R            0x4u

#define USER_VADDR_MIN  0x400000ULL
#define USER_VADDR_MAX  0xBFF00000ULL

/* ── ELF64 data structures ──────────────────────────────────────────────────── */
typedef struct {
    u32 e_ident_magic;
    u8  e_ident_class;
    u8  e_ident_data;
    u8  e_ident_ver;
    u8  e_ident_osabi;
    u8  e_ident_pad[8];
    u16 e_type;
    u16 e_machine;
    u32 e_version;
    u64 e_entry;
    u64 e_phoff;
    u64 e_shoff;
    u32 e_flags;
    u16 e_ehsize;
    u16 e_phentsize;
    u16 e_phnum;
    u16 e_shentsize;
    u16 e_shnum;
    u16 e_shstrndx;
} __attribute__((packed)) elf64_ehdr_t;

typedef struct {
    u32 p_type;
    u32 p_flags;
    u64 p_offset;
    u64 p_vaddr;
    u64 p_paddr;
    u64 p_filesz;
    u64 p_memsz;
    u64 p_align;
} __attribute__((packed)) elf64_phdr_t;

/* ── Validation ─────────────────────────────────────────────────────────────── */
static int elf_validate(const u8 *data, u32 size) {
    if (size < sizeof(elf64_ehdr_t)) return -1;
    const elf64_ehdr_t *eh = (const elf64_ehdr_t *)data;
    if (eh->e_ident_magic != ELF_MAGIC)     return -2;
    if (eh->e_ident_class != ELFCLASS64)    return -3;
    if (eh->e_ident_data  != ELFDATA2LSB)   return -4;
    if (eh->e_type        != ET_EXEC)       return -5;
    if (eh->e_machine     != EM_X86_64)     return -6;
    if (eh->e_phnum       == 0)             return -7;
    return 0;
}

/* ── elf_load_vfs: kernel-space flat load ────────────────────────────────────── */
int elf_load_vfs(fs_node_t *node, const char *task_name) {
    if (!node || node->type != FS_FILE) return -1;

    const u8 *data = (const u8 *)node->data;
    u32       size = node->size;

    int v = elf_validate(data, size);
    if (v != 0) {
        serial_write("[elf] validation failed: ");
        char buf[8]; kitoa(v, buf, 10); serial_write(buf); serial_write("\n");
        return v;
    }

    const elf64_ehdr_t *eh = (const elf64_ehdr_t *)data;

    /* Copy each PT_LOAD into kmalloc'd memory and patch entry to physical addr */
    for (u32 i = 0; i < eh->e_phnum; i++) {
        u64 ph_off = eh->e_phoff + (u64)i * eh->e_phentsize;
        if (ph_off + sizeof(elf64_phdr_t) > size) continue;
        const elf64_phdr_t *ph = (const elf64_phdr_t *)(data + ph_off);
        if (ph->p_type != PT_LOAD || ph->p_memsz == 0) continue;

        u8 *seg = (u8 *)kmalloc((size_t)ph->p_memsz + PAGE_SIZE);
        if (!seg) return -1;
        u8 *aligned = (u8 *)(((uintptr_t)seg + PAGE_SIZE - 1) & ~(uintptr_t)(PAGE_SIZE - 1));
        kmemset(aligned, 0, (size_t)ph->p_memsz);

        if (ph->p_filesz > 0) {
            u64 copy = ph->p_filesz;
            if (ph->p_offset + copy > size) copy = size - ph->p_offset;
            kmemcpy(aligned, data + ph->p_offset, (size_t)copy);
        }

        serial_write("[elf] PT_LOAD vaddr=0x");
        char buf[16]; kutoa((u32)ph->p_vaddr, buf, 16); serial_write(buf);
        serial_write(" -> phys=0x"); kutoa((u32)(uintptr_t)aligned, buf, 16);
        serial_write(buf); serial_write("\n");
    }

    serial_write("[elf] entry=0x");
    char buf[16]; kutoa((u32)eh->e_entry, buf, 16); serial_write(buf); serial_write("\n");

    task_func_t fn = (task_func_t)(uintptr_t)eh->e_entry;
    int tid = task_create(task_name, fn);
    if (tid < 0) { serial_write("[elf] task_create failed\n"); return -1; }
    serial_write("[elf] launched '"); serial_write(task_name); serial_write("'\n");
    return tid;
}

int elf_load_path(const char *path, const char *task_name) {
    fs_node_t *node = vfs_resolve_path(path);
    if (!node) { terminal_write("elf: not found: "); terminal_writeln(path); return -1; }
    return elf_load_vfs(node, task_name ? task_name : node->name);
}

/* ── elf_load_user: user-space ring-3 load ───────────────────────────────────── */
int elf_load_user(fs_node_t *node, const char *name, int session) {
    if (!node || node->type != FS_FILE) return -1;

    const u8 *data = (const u8 *)node->data;
    u32       size = node->size;

    if (elf_validate(data, size) != 0) return -2;

    const elf64_ehdr_t *eh = (const elf64_ehdr_t *)data;

    pde_t *dir = paging_create_dir();
    if (!dir) return -3;

    for (u32 i = 0; i < eh->e_phnum; i++) {
        u64 ph_off = eh->e_phoff + (u64)i * eh->e_phentsize;
        if (ph_off + sizeof(elf64_phdr_t) > size) continue;
        const elf64_phdr_t *ph = (const elf64_phdr_t *)(data + ph_off);
        if (ph->p_type != PT_LOAD || ph->p_memsz == 0) continue;

        u64 virt_start = ph->p_vaddr & ~(u64)(PAGE_SIZE - 1);
        u64 virt_end   = (ph->p_vaddr + ph->p_memsz + PAGE_SIZE - 1) & ~(u64)(PAGE_SIZE - 1);

        if (virt_start < USER_VADDR_MIN || virt_end > USER_VADDR_MAX) {
            paging_free_dir(dir);
            return -4;
        }

        for (u64 virt = virt_start; virt < virt_end; virt += PAGE_SIZE) {
            u32 frame = pmm_alloc_frame();
            if (frame == (u32)~0u) { paging_free_dir(dir); return -5; }

            u8 *kva = (u8 *)((u64)frame * PAGE_SIZE);
            kmemset(kva, 0, PAGE_SIZE);

            u64 page_end      = virt + PAGE_SIZE;
            u64 seg_file_end  = ph->p_vaddr + ph->p_filesz;
            u64 file_avail    = size > ph->p_offset ? (u64)(size - ph->p_offset) : 0;
            if (file_avail < ph->p_filesz) seg_file_end = ph->p_vaddr + file_avail;

            if (ph->p_vaddr < page_end && seg_file_end > virt) {
                u64 copy_start = ph->p_vaddr > virt ? ph->p_vaddr : virt;
                u64 copy_end   = seg_file_end < page_end ? seg_file_end : page_end;
                u64 src_off    = ph->p_offset + (copy_start - ph->p_vaddr);
                u64 dst_off    = copy_start - virt;
                kmemcpy(kva + dst_off, data + src_off, (size_t)(copy_end - copy_start));
            }

            u32 flags = PTE_PRESENT | PTE_USER | ((ph->p_flags & PF_W) ? PTE_RW : 0u);
            if (paging_map(dir, virt, (u64)frame * PAGE_SIZE, flags) != 0) {
                pmm_free_frame(frame);
                paging_free_dir(dir);
                return -6;
            }
        }

        serial_write("[elf_user] mapped segment vaddr=0x");
        char tmp[16]; kutoa((u32)ph->p_vaddr, tmp, 16); serial_write(tmp); serial_write("\n");
    }

    int tid = task_create_user(name, eh->e_entry, dir, session);
    if (tid < 0) { paging_free_dir(dir); return -7; }

    serial_write("[elf_user] launched '"); serial_write(name);
    serial_write("' entry=0x");
    char tmp[16]; kutoa((u32)eh->e_entry, tmp, 16); serial_write(tmp); serial_write("\n");
    return tid;
}

int elf_check(fs_node_t *node) {
    if (!node || node->type != FS_FILE || node->size < sizeof(elf64_ehdr_t)) return -1;
    return elf_validate((const u8 *)node->data, node->size);
}

u32 elf_entry_point(fs_node_t *node) {
    if (elf_check(node) != 0) return 0;
    const elf64_ehdr_t *eh = (const elf64_ehdr_t *)node->data;
    return (u32)eh->e_entry;
}
