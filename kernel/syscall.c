/* =============================================================================
 * CareOS - kernel/syscall.c
 * System-call interface via INT 0x80 for x86_64
 * ============================================================================= */
#include "kernel.h"

/* ── File descriptor table ─────────────────────────────────────────────────── */
#define FD_MAX      32
#define FD_STDIN     0
#define FD_STDOUT    1
#define FD_STDERR    2

typedef struct {
    bool       open;
    fs_node_t *node;
    u32        offset;
    u32        flags;
} fd_entry_t;

static fd_entry_t fd_table[FD_MAX];

static void fd_init(void) {
    kmemset(fd_table, 0, sizeof(fd_table));
    fd_table[FD_STDIN].open  = true;
    fd_table[FD_STDOUT].open = true;
    fd_table[FD_STDERR].open = true;
}

static int fd_alloc(void) {
    for (int i = 3; i < FD_MAX; i++)
        if (!fd_table[i].open) return i;
    return -1;
}

/* ── Error codes ────────────────────────────────────────────────────────────── */
#define EBADF    9
#define ENOENT   2
#define ENOMEM  12
#define EINVAL  22
#define EMFILE  24
#define EFAULT  14

/* ── User-pointer validation helpers ──────────────────────────────────────── */
/*
 * These used to be a range guess: `addr >= 0x400000 && len <= 0x3FFFFFFF - addr`.
 * That accepted every kernel address from 4MB to 1GB, so a ring-3 task could
 * hand a kernel pointer to read()/write() and get an arbitrary kernel
 * read/write primitive -- the identity map is present in every process's PML4,
 * so the kernel dereferencing it would succeed rather than fault.
 *
 * Validation is now done against the calling task's actual page tables:
 * a page is only accepted if the User bit (and the RW bit, when the kernel is
 * going to write) is set at all four levels. Because the kernel's identity map
 * omits PDE_USER, kernel addresses are rejected by construction rather than by
 * a hardcoded range that has to be kept in sync with the memory layout.
 *
 * INT 0x80 does not switch CR3, so the current task's page tables are the ones
 * live on the CPU: what we validate is exactly what the subsequent kmemcpy
 * dereferences.
 */
static bool user_range_ok(const void *ptr, u64 len, bool for_write) {
    u64 cr3 = task_current_cr3();
    /* cr3 == 0 marks a kernel task, which has no user address space and has no
     * business passing user pointers into these helpers. */
    if (cr3 == 0) return false;
    return paging_user_range_ok((pde_t *)cr3, (u64)ptr, len, for_write);
}

int copy_from_user(void *dst, const void *user_src, u32 len) {
    if (!user_range_ok(user_src, (u64)len, false)) return -(int)EFAULT;
    kmemcpy(dst, user_src, len);
    return 0;
}

int copy_to_user(void *user_dst, const void *src, u32 len) {
    if (!user_range_ok(user_dst, (u64)len, true)) return -(int)EFAULT;
    kmemcpy(user_dst, src, len);
    return 0;
}

/*
 * Copy a NUL-terminated string in from user space. The length is not known up
 * front, so validate one page at a time as the scan crosses into it -- never
 * read a byte that has not been proven user-accessible first. Returns the
 * length copied, or -EFAULT for an unmapped page and -EINVAL if the string is
 * longer than the caller's buffer.
 */
static int copy_str_from_user(char *dst, const char *user_src, u32 max) {
    if (!dst || !user_src || max == 0) return -(int)EINVAL;

    u64 addr = (u64)user_src;
    u64 checked_upto = 0;   /* exclusive end of the validated region */

    for (u32 i = 0; i < max; i++) {
        u64 at = addr + i;
        if (at >= checked_upto) {
            u64 page = at & ~0xFFFULL;
            if (!user_range_ok((const void *)page, PAGE_SIZE, false))
                return -(int)EFAULT;
            checked_upto = page + PAGE_SIZE;
        }
        dst[i] = user_src[i];
        if (dst[i] == '\0') return (int)i;
    }
    return -(int)EINVAL;    /* no terminator within max */
}

/* ── Syscall implementations ───────────────────────────────────────────────── */

static i32 sys_exit(i32 status) {
    task_exit();
    return 0;
}

static i32 sys_read(u32 fd, char *buf, u32 count) {
    if (fd >= FD_MAX || !fd_table[fd].open) return -(i32)EBADF;
    if (!buf || count == 0) return -(i32)EINVAL;
    /* The kernel writes into buf, so demand a writable user mapping. STDIN was
     * previously exempt from validation even though it writes to buf too. */
    if (!user_range_ok(buf, count, true)) return -(i32)EFAULT;

    if (fd == FD_STDIN) {
        u32 n = 0;
        while (n < count) {
            while (!keyboard_haschar()) task_yield();
            char c = keyboard_getchar();
            buf[n++] = c;
            if (c == '\n') break;
        }
        return (i32)n;
    }

    if (!fd_table[fd].node) return -(i32)EBADF;
    fs_node_t *node = fd_table[fd].node;
    u32 avail = node->size - fd_table[fd].offset;
    if (avail == 0) return 0;
    u32 n = count < avail ? count : avail;
    kmemcpy(buf, node->data + fd_table[fd].offset, n);
    fd_table[fd].offset += n;
    return (i32)n;
}

static i32 sys_write(u32 fd, const char *buf, u32 count) {
    if (fd >= FD_MAX || !fd_table[fd].open) return -(i32)EBADF;
    if (!buf || count == 0) return -(i32)EINVAL;
    /* Read-only access to buf is enough here, but STDOUT/STDERR must not be
     * exempt: echoing an unvalidated pointer to the terminal leaked kernel
     * memory to whoever could read the screen. */
    if (!user_range_ok(buf, count, false)) return -(i32)EFAULT;

    if (fd == FD_STDOUT || fd == FD_STDERR) {
        for (u32 i = 0; i < count; i++) terminal_putchar(buf[i]);
        return (i32)count;
    }

    if (!fd_table[fd].node) return -(i32)EBADF;
    fs_node_t *node = fd_table[fd].node;
    u32 space = FS_FILE_DATA_MAX - 1 - node->size;
    u32 n = count < space ? count : space;
    kmemcpy(node->data + node->size, buf, n);
    node->size += n;
    node->data[node->size] = '\0';
    fd_table[fd].offset = node->size;
    return (i32)n;
}

static i32 sys_open(const char *path, u32 flags, u32 mode) {
    (void)mode;
    if (!path) return -(i32)EINVAL;

    /* `path` was previously handed straight to vfs_resolve_path, letting a
     * ring-3 task point the kernel's string walk at any address it liked.
     * Copy it in through validated pages first and resolve the kernel copy. */
    char kpath[256];
    int n = copy_str_from_user(kpath, path, sizeof kpath);
    if (n < 0) return (i32)n;

    fs_node_t *node = vfs_resolve_path(kpath);
    if (!node) return -(i32)ENOENT;

    int fd = fd_alloc();
    if (fd < 0) return -(i32)EMFILE;

    fd_table[fd].open   = true;
    fd_table[fd].node   = node;
    fd_table[fd].offset = 0;
    fd_table[fd].flags  = flags;
    return fd;
}

static i32 sys_close(u32 fd) {
    if (fd >= FD_MAX || !fd_table[fd].open) return -(i32)EBADF;
    if (fd <= 2) return 0;
    fd_table[fd].open = false;
    fd_table[fd].node = NULL;
    return 0;
}

static i32 sys_sleep(u32 ms) {
    timer_wait(ms);
    return 0;
}

static i32 sys_getpid(void) {
    task_t *t = task_current();
    return t ? (i32)t->id : 1;
}

static u64 user_brk = 0x20000000;
static i64 sys_brk(u64 addr) {
    if (addr == 0) return (i64)user_brk;
    if (addr <= user_brk) return (i64)user_brk;
    user_brk = addr;
    return (i64)user_brk;
}

static i32 sys_gettime(void) {
    return (i32)timer_get_ticks();
}

static i32 sys_yield(void) {
    task_yield();
    return 0;
}

/* ── Dispatch table ─────────────────────────────────────────────────────────── */
#define SYSCALL_MAX 11
typedef i64 (*syscall_fn_t)(u64, u64, u64, u64, u64);

static i64 _exit_wrap(u64 a, u64 b, u64 c, u64 d, u64 e) { return sys_exit((i32)a); }
static i64 _read_wrap(u64 a, u64 b, u64 c, u64 d, u64 e) { return sys_read((u32)a, (char*)b, (u32)c); }
static i64 _write_wrap(u64 a, u64 b, u64 c, u64 d, u64 e) { return sys_write((u32)a, (const char*)b, (u32)c); }
static i64 _open_wrap(u64 a, u64 b, u64 c, u64 d, u64 e) { return sys_open((const char*)a, (u32)b, (u32)c); }
static i64 _close_wrap(u64 a, u64 b, u64 c, u64 d, u64 e) { return sys_close((u32)a); }
static i64 _sleep_wrap(u64 a, u64 b, u64 c, u64 d, u64 e) { return sys_sleep((u32)a); }
static i64 _getpid_wrap(u64 a, u64 b, u64 c, u64 d, u64 e) { return sys_getpid(); }
static i64 _brk_wrap(u64 a, u64 b, u64 c, u64 d, u64 e) { return sys_brk(a); }
static i64 _gettime_wrap(u64 a, u64 b, u64 c, u64 d, u64 e) { return sys_gettime(); }
static i64 _yield_wrap(u64 a, u64 b, u64 c, u64 d, u64 e) { return sys_yield(); }

static const syscall_fn_t syscall_table[SYSCALL_MAX] = {
    NULL, _exit_wrap, _read_wrap, _write_wrap, _open_wrap, _close_wrap,
    _sleep_wrap, _getpid_wrap, _brk_wrap, _gettime_wrap, _yield_wrap
};

void syscall_handler(registers_t *r) {
    u64 nr = r->rax;
    if (nr == 0 || nr >= SYSCALL_MAX || !syscall_table[nr]) {
        r->rax = (u64)(-(i64)EINVAL);
        return;
    }
    r->rax = (u64)syscall_table[nr](r->rbx, r->rcx, r->rdx, r->rsi, r->rdi);
}

/* Proper INT 0x80 stub: pushes dummy error + int_no, then enters the shared ISR path */
extern void isr_common_stub(void);
void int80_stub(void);
__asm__(
    ".global int80_stub\n"
    "int80_stub:\n"
    "  pushq $0\n"
    "  pushq $0x80\n"
    "  jmp isr_common_stub\n"
);

void syscall_init(void) {
    fd_init();
    idt_set_gate(0x80, (u32)(uintptr_t)int80_stub, GDT_CODE_SEG, 0xEF);
    register_interrupt_handler(0x80, syscall_handler);
}
