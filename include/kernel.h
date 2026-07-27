#ifndef KERNEL_H
#define KERNEL_H

/* =============================================================================
 * CareOS Kernel - kernel.h
 * Core type definitions, macros, and function prototypes
 * ============================================================================= */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>

/* -- Fundamental Types ---------------------------------------------------- */
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t   i8;
typedef int16_t  i16;
typedef int32_t  i32;
typedef int64_t  i64;

/* -- Port I/O ------------------------------------------------------------- */
static inline void outb(u16 port, u8 val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline u8 inb(u16 port) {
    u8 ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline void outw(u16 port, u16 val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}
static inline u16 inw(u16 port) {
    u16 ret;
    __asm__ volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline void outl(u16 port, u32 val) {
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}
static inline u32 inl(u16 port) {
    u32 ret;
    __asm__ volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline void io_wait(void) { outb(0x80, 0); }

/* -- VGA Text Mode -------------------------------------------------------- */
#define VGA_WIDTH   80
#define VGA_HEIGHT  25
#define VGA_MEMORY  ((volatile u16*)0xB8000)

/* VGA color codes */
typedef enum {
    VGA_BLACK=0, VGA_BLUE=1, VGA_GREEN=2, VGA_CYAN=3,
    VGA_RED=4, VGA_MAGENTA=5, VGA_BROWN=6, VGA_LIGHT_GREY=7,
    VGA_DARK_GREY=8, VGA_LIGHT_BLUE=9, VGA_LIGHT_GREEN=10,
    VGA_LIGHT_CYAN=11, VGA_LIGHT_RED=12, VGA_LIGHT_MAGENTA=13,
    VGA_YELLOW=14, VGA_WHITE=15
} vga_color;

#define VGA_ENTRY_COLOR(fg,bg)  ((u8)((bg)<<4|(fg)))
#define VGA_ENTRY(c,color)      ((u16)(c)|((u16)(color)<<8))

/* -- IDT / GDT ------------------------------------------------------------ */
#define GDT_CODE_SEG 0x08
#define GDT_DATA_SEG 0x10
#define IDT_ENTRIES  256

typedef struct {
    u16 limit_low;
    u16 base_low;
    u8  base_mid;
    u8  access;
    u8  granularity;
    u8  base_high;
} __attribute__((packed)) gdt_entry_t;

typedef struct {
    u16 limit;
    u64 base;
} __attribute__((packed)) gdt_ptr_t;

typedef struct {
    u16 base_low;
    u16 selector;
    u8  ist;        /* Interrupt Stack Table */
    u8  flags;
    u16 base_mid;
    u32 base_high;
    u32 reserved;
} __attribute__((packed)) idt_entry_t;

typedef struct {
    u16 limit;
    u64 base;
} __attribute__((packed)) idt_ptr_t;

/* CPU register snapshot (pushed by ISR stubs) */
typedef struct {
    u64 r15, r14, r13, r12, r11, r10, r9, r8;
    u64 rbp, rdi, rsi, rdx, rcx, rbx, rax;
    u64 int_no, err_code;
    u64 rip, cs, rflags, rsp, ss;
} __attribute__((packed)) registers_t;

typedef void (*isr_handler_t)(registers_t*);

/* -- PIC ------------------------------------------------------------------ */
#define PIC1_CMD    0x20
#define PIC1_DATA   0x21
#define PIC2_CMD    0xA0
#define PIC2_DATA   0xA1
#define PIC_EOI     0x20
#define IRQ0        32

/* -- PIT Timer ------------------------------------------------------------ */
#define PIT_CHANNEL0    0x40
#define PIT_CMD         0x43
#define PIT_HZ          100   /* 100 Hz = 10ms tick */

/* -- Keyboard ------------------------------------------------------------- */
#define KB_DATA_PORT    0x60
#define KB_STATUS_PORT  0x64
#define KB_BUF_SIZE     256

/* -- Memory ---------------------------------------------------------------
 * The heap is a static array in .bss (see heap_mem in kernel/memory.c), NOT a
 * region at KERNEL_HEAP_START -- that constant is vestigial and unused.
 * Growing KERNEL_HEAP_SIZE therefore grows the kernel image's memory footprint
 * directly, and the loader must zero all of it at boot.
 *
 * 512 MB against the 4 GB the VM is given. Consumers are the framebuffer
 * backbuffer (8 MB at 1080p), the image cache (RES_BUDGET_BYTES, 32 MB),
 * per-window buffers, and VFS file contents. */
#define KERNEL_HEAP_START   0x400000    /* vestigial; heap_mem[] lives in .bss */
#define KERNEL_HEAP_SIZE    (512*1024*1024) /* 512 MB heap */

/* FLOOR for the physical frames paging_init() withholds from pmm_alloc_frame().
 * The real reservation is computed from the _kernel_end symbol emitted by
 * kernel.ld, so it always covers the actual image; this only guarantees a
 * minimum, keeping low memory the loader may have used out of circulation.
 * Do NOT treat this as "where the kernel ends" -- it no longer is. */
#define KERNEL_RESERVED_BYTES (256*1024*1024)
#define PAGE_SIZE           4096

/* -- Serial (debug) ------------------------------------------------------- */
#define COM1_PORT   0x3F8

/* -- VGA text terminal ---------------------------------------------------- */
void terminal_init(void);
void terminal_clear(void);
void terminal_setcolor(u8 color);
void terminal_putchar(char c);
void terminal_write(const char *str);
void terminal_writeln(const char *str);
void terminal_set_cursor(u32 col, u32 row);
void terminal_scroll(void);
void terminal_write_colored(const char *str, u8 color);
void kprintf(const char *fmt, ...);
int  ksprintf(char *buf, const char *fmt, ...);
int  kvsnprintf(char *out, size_t max, const char *fmt, va_list ap);

/* -- GDT / IDT ------------------------------------------------------------ */
void gdt_init(void);
void idt_init(void);
void idt_set_gate(u8 num, u32 base, u16 sel, u8 flags);
void register_interrupt_handler(u8 n, isr_handler_t h);

/* -- IRQ / ISR ------------------------------------------------------------ */
void pic_init(void);
void pic_send_eoi(u8 irq);
void irq_handler(registers_t *r);
void isr_handler(registers_t *r);

/* -- Timer ---------------------------------------------------------------- */
void timer_init(u32 hz);
u32  timer_get_ticks(void);
void timer_wait(u32 ms);
void timer_tick_advance(void);

/* -- Keyboard driver ------------------------------------------------------ */
void keyboard_init(void);
char keyboard_getchar(void);
bool keyboard_haschar(void);
void keyboard_flush(void);
bool keyboard_ctrl_held(void);
bool keyboard_alt_held(void);
bool keyboard_shift_held(void);

/* -- Memory --------------------------------------------------------------- */
void  pmm_init(void);
/* -- Memory / Heap -------------------------------------------------------- */
typedef struct block_hdr {
    u64              magic;
    u64              size;
    bool             free;
    u8               reserved[15];
    struct block_hdr *next;
    struct block_hdr *prev;
} block_hdr_t;

#define HEAP_MAGIC  0xC4E05AFE

void  *kmalloc(size_t size);
void   kfree(void *ptr);
void *kmemset(void *dst, int c, size_t n);
void *kmemcpy(void *dst, const void *src, size_t n);
int   kmemcmp(const void *a, const void *b, size_t n);

/* -- String utils --------------------------------------------------------- */
size_t kstrlen(const char *s);
int    kstrcmp(const char *a, const char *b);
int    kstrncmp(const char *a, const char *b, size_t n);
char  *kstrcpy(char *dst, const char *src);
char  *kstrncpy(char *dst, const char *src, size_t n);
char  *kstrcat(char *dst, const char *src);
char  *kstrchr(const char *s, int c);
char  *kstrrchr(const char *s, int c);
void   kitoa(i32 n, char *buf, int base);
void   kutoa(u32 n, char *buf, int base);
int    katoi(const char *s);

/* -- Serial debug --------------------------------------------------------- */
void serial_init(void);
void serial_putchar(char c);
void serial_write(const char *s);
void serial_log_tail(char *buf, u32 max);
void serial_log_clear(void);
__attribute__((noreturn)) void kernel_panic(u32 code, const char *msg);

/* -- VFS / Filesystem ----------------------------------------------------- */
#define FS_MAX_FILES    64
#define FS_MAX_DIRS     64
#define FS_NAME_MAX     64
#define FS_PATH_MAX     256

/* Upper bound on a single file's heap allocation. File contents live on the
 * kernel heap (KERNEL_HEAP_SIZE, 192 MiB); this ceiling stops one runaway
 * write from consuming it. Previously file data was a 5 MiB inline array,
 * which cost 640 MiB of BSS for the 128-node pool whether used or not. */
#define FS_FILE_MAX_BYTES (16u*1024u*1024u)

typedef enum { FS_FILE, FS_DIR } fs_node_type_t;

typedef struct fs_node {
    char            name[FS_NAME_MAX];
    fs_node_type_t  type;
    u32             size;          /* valid bytes in data */
    char           *data;          /* heap or borrowed; NULL until content exists */
    u32             capacity;      /* allocated bytes; 0 when borrowed */
    bool            data_owned;    /* false = borrowed memory, never kfree'd */
    struct fs_node *parent;
    struct fs_node *children[32];
    u32             child_count;
    u32             permissions;   /* rwxrwxrwx bitmask */
    u32             inode_num;     /* ext2 inode number; 0 = in-memory only */
    bool            children_loaded;
} fs_node_t;

void       vfs_init(void);
void       vfs_storage_online(void);
fs_node_t *vfs_root(void);
fs_node_t *vfs_find(fs_node_t *dir, const char *name);
fs_node_t *vfs_mkdir(fs_node_t *parent, const char *name);
fs_node_t *vfs_mkfile(fs_node_t *parent, const char *name);
int        vfs_write(fs_node_t *f, const char *data, u32 len);
int        vfs_read(fs_node_t *f, char *buf, u32 len);
int        vfs_delete(fs_node_t *node);
fs_node_t *vfs_resolve_path(const char *path);

/* File data storage. Contents are heap-allocated on first write. A node with
 * data_owned == false borrows memory it must never free (e.g. the embedded
 * DOOM WAD living in .data). */
int         vfs_file_reserve(fs_node_t *f, u32 bytes); /* 0 ok, -1 fail */
const char *vfs_file_str(fs_node_t *f);                /* never NULL; "" if empty */
void        vfs_file_release(fs_node_t *f);

/* -- Paging types & flags -------------------------------------------------- */
typedef u64 pml4e_t;
typedef u64 pdpte_t;
typedef u64 pde_t;
typedef u64 pte_t;

#define PDE_PRESENT 0x001
#define PDE_RW      0x002
#define PDE_USER    0x004
#define PDE_PWT     0x008
#define PDE_PCD     0x010
#define PDE_ACCESSED 0x020
#define PDE_DIRTY   0x040
#define PDE_4MB     0x080  /* Huge page (2MB in 64-bit) */
#define PDE_GLOBAL  0x100

#define PTE_PRESENT 0x001
#define PTE_RW      0x002
#define PTE_USER    0x004
#define PTE_PWT     0x008
#define PTE_PCD     0x010

/* -- Process / Task (cooperative) ----------------------------------------- */
#define MAX_TASKS 32
typedef void (*task_func_t)(void);

typedef enum { TASK_READY, TASK_RUNNING, TASK_BLOCKED, TASK_DEAD } task_state_t;

/* Public view of a task. task_get()/task_current() hand out pointers to the
 * scheduler's private tcb_t cast to this type, so the two layouts MUST agree
 * field-for-field over this prefix. scheduler.c enforces that with
 * _Static_assert on every member; do not reorder one without the other.
 * (They previously disagreed from `state` onward, which is why `ps` printed a
 * stack pointer in its tick column.) */
typedef struct {
    u32          id;
    char         name[32];
    task_state_t state;
    u32          timeslice;
    u32          ticks_remaining;
    u32          tick_count;      /* total ticks consumed */
    u64          rsp;
    u64          cr3;
} task_t;

void task_init(void);
int  task_create(const char *name, task_func_t fn);
int  task_create_user(const char *name, u64 entry, pde_t *page_dir, int session);
void task_yield(void);
__attribute__((noreturn)) void task_exit(void);
task_t *task_current(void);
void task_list(void);

/* -- Shell ---------------------------------------------------------------- */
void shell_init(void);
void shell_run(void);

/* -- Package manager ------------------------------------------------------ */
void carepkg_init(void);
void carepkg_run(const char *cmd, const char *pkg);

/* -- GUI resource cache (gui/resource_cache.c) ----------------------------
 * Declared here, not only in gui/resource_cache.h, so kernel-side code that
 * rewrites or deletes an asset file can drop the stale decode without pulling
 * gui.h into the kernel -- the same separation appdb.c keeps. Passing NULL
 * flushes the whole cache. */
void res_forget(const char *path);

/* `res` command: archives mounted, cache occupancy, and -- the line that
 * matters -- whether an icon name actually resolves to a decoded image. A theme
 * that mounts but resolves nothing draws vector glyphs and looks intentional,
 * so there is no other way to tell from the running system.
 *
 * Writes a multi-line report into `out` rather than printing, because the two
 * shells have different output sinks (terminal_write vs win_append). 512 bytes
 * is comfortably enough. */
void res_status_text(char *out, u32 max);

/* -- System info ---------------------------------------------------------- */
void sysinfo_print(void);

/* -- Care Language interpreter -------------------------------------------- */
int care_lang_exec(const char *src, u32 len);
int care_lang_exec_buf(const char *src, u32 len, char *out, u32 out_max);

/* -- Startup scripts (rc.care) --------------------------------------------- */
void rc_care_run_startup(void);

/* -- Kernel main ---------------------------------------------------------- */
void kernel_main(u64 magic, u64 mbi_addr);

/* =========================================================================
 * Subsystem declarations (paging, scheduler, syscall, users, carepkg,
 * elf, drivers/rtc, drivers/pci, drivers/storage/ata, drivers/net/e1000)
 * ========================================================================= */

/* -- Paging / Virtual Memory ---------------------------------------------- */
/* Page-directory / page-table flag bits */
void  paging_init(void);
u32   pmm_alloc_frame(void);
void  pmm_free_frame(u32 frame);
u32   pmm_free_count(void);

pde_t *paging_create_dir(void);
int    paging_map(pde_t *dir, u64 virt, u64 phys, u32 flags);
void   paging_map_mmio(u32 phys_start, u32 size);
void   paging_switch_dir(pde_t *dir);
void   paging_switch_kernel(void);
void   paging_free_dir(pde_t *dir);
u64    paging_translate(pde_t *dir, u64 virt);
/* True only if every page of [virt, virt+len) is mapped user-accessible (and
 * writable when need_write) in `dir`, checking Present/User/RW at all four
 * levels. This is the page-table-enforced replacement for guessing at address
 * ranges when validating syscall arguments. */
bool   paging_user_range_ok(pde_t *dir, u64 virt, u64 len, bool need_write);

/* User-space private address region: a dedicated PML4 slot so every
 * process gets its own physical PDPT/PD/PT frames, never aliased with the
 * kernel's shared identity map at PML4[0] or with other processes.
 * See docs/superpowers/specs/2026-07-20-per-process-address-spaces-design.md */
#define USER_PML4_INDEX   1
#define USER_CODE_BASE    ((u64)USER_PML4_INDEX << 39)     /* 0x0000008000000000 */
#define USER_CODE_MAX     (USER_CODE_BASE + 0x40000000ULL) /* +1GB: code/data */
#define TASK_STACK_PAGES  512
#define TASK_STACK_SIZE   (TASK_STACK_PAGES * PAGE_SIZE)
#define USER_STACK_TOP    (USER_CODE_MAX + TASK_STACK_SIZE)  /* stack sits right after */

/* -- Preemptive scheduler ------------------------------------------------- */
void scheduler_init(void);
u64  task_get_cr3(int tid);
bool task_has_exited(int tid);
void task_kill(int tid);
void scheduler_tick(registers_t *r);
void task_block(void);
void task_unblock(u32 id);
u64  task_current_cr3(void);

/* -- System calls --------------------------------------------------------- */
void syscall_init(void);
int  copy_from_user(void *dst, const void *user_src, u32 len);
int  copy_to_user(void *user_dst, const void *src, u32 len);

/* -- User management ------------------------------------------------------ */
/* Password hash algorithms. Records written before v4 of the on-disk userdb
 * hold a 32-bit FNV-style hash; they verify against the legacy algorithm and
 * are transparently upgraded to PBKDF2 on the next successful login. */
#define USER_HASH_LEGACY_FNV  0u
#define USER_HASH_PBKDF2_S256 1u

#define USER_PASS_HASH_LEN 32u
#define USER_SALT_LEN      16u

/* NOTE: this must stay field-for-field identical to user_rec_t in
 * kernel/users.c. apps/app_users.c and apps/app_settings.c cast the void*
 * from user_get_by_uid() to user_t*, so any layout drift is silent corruption. */
typedef struct {
    u32  uid, gid;
    char name[32];
    u8   pass_hash[USER_PASS_HASH_LEN];
    char home[64];
    char shell[32];
    bool active;
    bool is_root;
    u8   salt[USER_SALT_LEN];
    u8   failed_attempts;
    u32  lock_until_tick;
    u32  theme_pref;
    u32  font_pref;
    u16  last_login_year;
    u8   last_login_month;
    u8   last_login_day;
    u8   last_login_hour;
    u8   last_login_minute;
    u8   hash_algo;
    bool must_change_password;
} user_t;

void *user_get_by_uid(u32 uid);
void        users_init(void);
int         user_login(const char *name, const char *pass);
void        user_logout(void);
const char *user_current_name(void);
u32         user_current_uid(void);
bool        user_is_root(void);
bool        user_can_read(fs_node_t *node);
bool        user_can_write(fs_node_t *node);
int         user_create(const char *name, const char *pass);
int         user_register(const char *name, const char *pass);
int         user_delete(const char *name);
int         user_set_admin(const char *name, bool is_admin);
int         user_change_password(const char *name, const char *old_pass,
                                 const char *new_pass);
void        user_list(void);
void        user_set_current_theme_preference(u32 theme);
#define USER_FONT_SYSTEM_DEFAULT 0xFFFFFFFFu
void        user_set_current_font_preference(u32 index);
/* True when the logged-in account still holds a shipped bootstrap password and
 * must set a new one before reaching the desktop. */
bool        user_must_change_password(void);

/* VFS path helper (implemented in users.c) */
void vfs_get_path(fs_node_t *node, char *buf, u32 max);

/* -- ELF loader ----------------------------------------------------------- */
int  elf_load_vfs(fs_node_t *node, const char *task_name);
int  elf_load_user(fs_node_t *node, const char *name, int session);
int  elf_load_path(const char *path, const char *task_name);
int  elf_check(fs_node_t *node);
u32  elf_entry_point(fs_node_t *node);

/* -- RTC driver ----------------------------------------------------------- */
typedef struct {
    u8  second;
    u8  minute;
    u8  hour;
    u8  day;
    u8  month;
    u16 year;
} rtc_time_t;

void rtc_init(void);
void rtc_read(rtc_time_t *t);
void rtc_format_time(const rtc_time_t *t, char *buf);   /* "HH:MM:SS"  */
void rtc_format_date(const rtc_time_t *t, char *buf);   /* "YYYY-MM-DD" */

/* -- PCI driver ----------------------------------------------------------- */
typedef struct {
    u8  bus, device, function;
    u16 vendor_id, device_id;
    u8  class_code, subclass, prog_if, revision;
    u8  header_type;
    u8  irq;
    u32 bar[6];
} pci_device_t;

void         pci_init(void);
void         pci_scan(void);
void         pci_list(void);
u32          pci_read32(u8 bus, u8 dev, u8 func, u8 offset);
u16          pci_read16(u8 bus, u8 dev, u8 func, u8 offset);
u8           pci_read8 (u8 bus, u8 dev, u8 func, u8 offset);
void         pci_write32(u8 bus, u8 dev, u8 func, u8 offset, u32 val);
pci_device_t *pci_find_class(u8 cls, u8 sub);
pci_device_t *pci_find_device(u16 vendor, u16 device);
u32           pci_device_count(void);
pci_device_t *pci_device_get(u32 idx);
const char   *pci_vendor_name(u16 vid);
const char   *pci_device_name(u16 vid, u16 did);

/* -- ATA / Storage driver ------------------------------------------------- */
void        ata_init(void);
bool        ata_is_present(void);
u32         ata_get_sectors(void);
const char *ata_get_model(void);
int         ata_read_sectors(u32 lba, u8 count, void *buf);
int         ata_write_sectors(u32 lba, u8 count, const void *buf);
int         ata_cached_read(u32 lba, void *buf);
int         ata_cached_write(u32 lba, const void *buf);
void        ata_cache_flush(void);
#define CAREOS_DISK_HOMEFS_SECTORS    96u
#define CAREOS_DISK_SETTINGS_SECTORS   4u
/* 8 sectors = 4096 B, enough for MAX_USERS (16) v4 records of 198 B each
 * (3168 B) plus the header. The old value of 4 could not even hold 16 v3
 * records (2512 B > 2048 B), which users_persist_save did not bound-check. */
#define CAREOS_DISK_USERDB_SECTORS     8u
#define CAREOS_DISK_RESERVED_SECTORS  (CAREOS_DISK_HOMEFS_SECTORS + CAREOS_DISK_SETTINGS_SECTORS + CAREOS_DISK_USERDB_SECTORS)

/* -- e1000 Ethernet driver ------------------------------------------------ */
void        e1000_init(void);
bool        e1000_is_up(void);
const u8   *e1000_get_mac(void);
void        e1000_poll(void);

/* net_send_frame is called by net.c; e1000.c implements it */
void net_send_frame(const u8 *frame, u32 len);

/* -- Networking types (used by net.c and kernel.h consumers) --------------- */
#define MAX_SOCKETS   16
#define NET_BUF_SIZE  4096

typedef enum { SOCK_UNUSED=0, SOCK_TCP, SOCK_UDP } sock_type_t;
typedef enum { SOCK_CLOSED=0, SOCK_SYN_SENT, SOCK_ESTABLISHED,
               SOCK_FIN_WAIT, SOCK_TIME_WAIT } sock_state_t;

typedef struct {
    sock_type_t type;
    sock_state_t state;
    u32 local_ip,  remote_ip;
    u16 local_port, remote_port;
    u32 seq, ack;
    u8  rx_buf[NET_BUF_SIZE];
    u32 rx_len;
} socket_t;

/* -- Net API (net.c) ------------------------------------------------------ */
void net_init(void);
void net_poll(void);
void net_configure(u32 ip, u32 netmask, u32 gateway, const u8 *mac);
void net_set_ip(u32 ip, u32 nm, u32 gw);
int  sock_create(sock_type_t type);
int  sock_connect(int fd, u32 ip, u16 port);
int  sock_send(int fd, const u8 *data, u32 len);
int  sock_recv(int fd, u8 *buf, u32 maxlen);
void sock_close(int fd);
int  dns_resolve(const char *host, u32 *out);
int  net_dhcp_renew(void);
void net_set_dns_server(u32 ip);
u32  net_get_dns_server(void);
const char *net_last_error(void);
int  http_get(const char *host, u16 port, const char *path,
              char *resp, u32 maxlen);
void icmp_ping(u32 dst, u32 seq);

/* -- HTTPS/TLS (net/tls.c) ------------------------------------------------- */
int  https_get(const char *hostname, const char *path,
               char *resp_buf, u32 maxlen);

/* -- Crypto (net/sha256.c, net/aes_gcm.c, net/x25519.c) -------------------- */
void sha256(const u8 *data, u32 len, u8 *out);
void hmac_sha256(const u8 *key, u32 klen, const u8 *msg, u32 mlen, u8 *out);
void hkdf_extract(const u8 *salt, u32 slen, const u8 *ikm, u32 ikm_len, u8 *prk);
void hkdf_expand(const u8 *prk, const u8 *info, u32 info_len, u8 *out, u32 olen);
void x25519(u8 *out, const u8 *scalar, const u8 *point);
/* aes128_ctx_t is an opaque 176-byte struct; callers allocate u8[256] and cast */
void aes128_init(void *ctx, const u8 *key);
void aes128_gcm_encrypt(const void *ctx, const u8 *iv, const u8 *aad, u32 aad_len,
                        const u8 *plain, u32 plen, u8 *cipher, u8 *tag);
int  aes128_gcm_decrypt(const void *ctx, const u8 *iv, const u8 *aad, u32 aad_len,
                        const u8 *cipher, u32 clen, u8 *plain, const u8 *tag);

/* -- ext2 filesystem driver ----------------------------------------------- */
int  ext2_mount(void);
u32  ext2_lookup(u32 dir_ino, const char *name);
u32  ext2_path_to_inode(const char *path);
int  ext2_write_data(u32 ino_num, u32 off, const void *buf, u32 len);
u32  ext2_create_file(u32 parent_ino, const char *name);
u32  ext2_mkdir(u32 parent_ino, const char *name);
int  ext2_unlink(u32 parent_ino, const char *name);

/* -- Extra kernel helpers ------------------------------------------------- */
u32  kmem_used(void);
u32  kmem_free_bytes(void);
int  vfs_copy(fs_node_t *src, fs_node_t *dst_dir, const char *new_name);
int  vfs_rename(fs_node_t *node, const char *new_name);
u32  vfs_node_count(void);
task_t *task_get(u32 id);
u32     task_count_active(void);
bool user_is_admin(u32 uid);
const char *user_current_name(void);
int  user_login(const char *name, const char *pass);
int  user_register(const char *name, const char *pass);
void user_passwd(const char *name, const char *new_pass);
bool net_is_up(void);
u32  net_get_ip(void);
void pci_list_devices(void);
bool carepkg_is_installed(const char *name);
int  carepkg_install(const char *pkg_path);
int  carepkg_remove(const char *name);
bool carepkg_get_info(u32 idx, char *name, char *version, char *desc, char *category, bool *installed);
u32  carepkg_count(void);

/* -- Persistent system settings ------------------------------------------- */
typedef struct {
    u32 theme;
    u32 mouse_sensitivity;
    u32 boot_fast;
    u32 clock_24h;
    u32 wallpaper;
    u32 taskbar_centered;
    bool wifi_connected;
    char wifi_ssid[32];
    char wifi_pass[64];
    u32  vesa_w;
    u32  vesa_h;
    u32  font_family;
    u32  accent;
} careos_settings_t;

void settings_init(void);
const careos_settings_t *settings_get(void);
void settings_set_theme(u32 theme);
void settings_set_mouse_sensitivity(u32 pct);
void settings_set_boot_fast(bool enabled);
void settings_set_clock_24h(bool enabled);
void settings_set_wallpaper(u32 wallpaper);
void settings_set_accent(u32 accent);
void settings_set_taskbar_centered(bool centered);
void settings_set_wifi_profile(const char *ssid, const char *pass, bool connected);
void settings_set_vesa_mode(u32 w, u32 h);
void settings_set_font_family(u32 index);

/* -- Pipes ---------------------------------------------------------------- */
#define PIPE_BUF_SIZE 4096

typedef struct {
    char buf[PIPE_BUF_SIZE];
    u32  len;
    bool closed;
} pipe_t;

pipe_t *pipe_create(void);
int     pipe_write(pipe_t *p, const char *data, u32 len);
int     pipe_read(pipe_t *p, char *out, u32 maxlen);
void    pipe_close(pipe_t *p);
void    pipe_reset(pipe_t *p);

/* -- Signals -------------------------------------------------------------- */
#define SIGINT  2
#define SIGKILL 9

void signal_send(u32 task_id, u32 sig);
void kb_push_char(char c);

/* -- BGA / VESA mode switching -------------------------------------------- */
typedef struct {
    u16         w, h;
    const char *label;
} vesa_mode_t;

void        vesa_init(void);
int         vesa_set_mode(u16 w, u16 h);
u32         vesa_mode_count(void);
vesa_mode_t vesa_mode_get(u32 idx);
u16         vesa_current_w(void);
u16         vesa_current_h(void);

/* -- Chunked HTTP --------------------------------------------------------- */
void http_decode_chunked(char *buf, u32 len, u32 *out_len);

/* GDT / GDT pointer -- exposed so scheduler.c can install the TSS */
extern u64         gdt[8];
extern gdt_ptr_t   gdt_ptr;

/* GDT segment selectors */
#define GDT_USER_CODE_SEG  0x18   /* ring-3 code (index 3, RPL=0 for gate) */
#define GDT_USER_DATA_SEG  0x20   /* ring-3 data */
#define GDT_TSS_SEG        0x28   /* TSS (index 5) */
#define GDT_USER_CODE_RPL3 0x1B   /* ring-3 code selector with RPL=3 — for iret frames */
#define GDT_USER_DATA_RPL3 0x23   /* ring-3 data selector with RPL=3 — for iret frames and segment regs */

#endif /* KERNEL_H */
