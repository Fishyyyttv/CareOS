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
static bool user_ptr_ok(const void *ptr, u64 len) {
    u64 addr = (u64)ptr;
    /* Identity map is first 1GB */
    return addr >= 0x400000ULL && len <= 0x3FFFFFFFULL - addr;
}

int copy_from_user(void *dst, const void *user_src, u32 len) {
    if (!user_ptr_ok(user_src, (u64)len)) return -(int)EFAULT;
    kmemcpy(dst, user_src, len);
    return 0;
}

int copy_to_user(void *user_dst, const void *src, u32 len) {
    if (!user_ptr_ok(user_dst, (u64)len)) return -(int)EFAULT;
    kmemcpy(user_dst, src, len);
    return 0;
}

/* ── Syscall implementations ───────────────────────────────────────────────── */

static i32 sys_exit(i32 status) {
    task_exit();
    return 0;
}

static i32 sys_read(u32 fd, char *buf, u32 count) {
    if (fd >= FD_MAX || !fd_table[fd].open) return -(i32)EBADF;
    if (!buf || count == 0) return -(i32)EINVAL;
    if (fd != FD_STDIN && !user_ptr_ok(buf, count)) return -(i32)EFAULT;

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
    if (fd != FD_STDOUT && fd != FD_STDERR && !user_ptr_ok(buf, count)) return -(i32)EFAULT;

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
    fs_node_t *node = vfs_resolve_path(path);
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
