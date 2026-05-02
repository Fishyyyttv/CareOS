/* =============================================================================
 * CareOS - kernel/memory.c
 * Simple bump+free-list heap allocator and string/memory utilities
 * ============================================================================= */
#include "kernel.h"

/* -- Heap ------------------------------------------------------------------- */
#define HDR_SIZE    sizeof(block_hdr_t)
#define MIN_SPLIT   (HDR_SIZE + 16)

static u8          heap_mem[KERNEL_HEAP_SIZE] __attribute__((aligned(16)));
static block_hdr_t *heap_head = NULL;

void pmm_init(void) {
    heap_head         = (block_hdr_t *)heap_mem;
    heap_head->magic  = HEAP_MAGIC;
    heap_head->size   = KERNEL_HEAP_SIZE - HDR_SIZE;
    heap_head->free   = true;
    heap_head->next   = NULL;
    heap_head->prev   = NULL;
}

void *kmalloc(size_t sz) {
    if (!sz) return NULL;
    sz = (sz + 15) & ~15UL;

    block_hdr_t *b = heap_head;
    while (b) {
        if (b->free && b->size >= sz) {
            if (b->size >= sz + MIN_SPLIT) {
                block_hdr_t *nb = (block_hdr_t *)((u8*)b + HDR_SIZE + sz);
                nb->magic = HEAP_MAGIC;
                nb->size  = b->size - sz - HDR_SIZE;
                nb->free  = true;
                nb->next  = b->next;
                nb->prev  = b;
                if (b->next) b->next->prev = nb;
                b->next   = nb;
                b->size   = sz;
            }
            b->free = false;
            return (void *)((u8*)b + HDR_SIZE);
        }
        b = b->next;
    }
    return NULL;
}

void kfree(void *ptr) {
    if (!ptr) return;
    block_hdr_t *b = (block_hdr_t *)((u8*)ptr - HDR_SIZE);
    if (b->magic != HEAP_MAGIC) return;
    b->free = true;

    if (b->next && b->next->free) {
        b->size += HDR_SIZE + b->next->size;
        b->next = b->next->next;
        if (b->next) b->next->prev = b;
    }
    if (b->prev && b->prev->free) {
        b->prev->size += HDR_SIZE + b->size;
        b->prev->next  = b->next;
        if (b->next) b->next->prev = b->prev;
    }
}

/* -- Memory utilities -------------------------------------------------------- */
void *kmemset(void *dst, int c, size_t n) {
    u64 val = (u8)c;
    val |= (val << 8);
    val |= (val << 16);
    val |= (val << 32);
    
    size_t qwords = n / 8;
    size_t bytes  = n % 8;
    u64 *d64 = (u64*)dst;
    u8  *d8;

    __asm__ volatile("rep stosq" : "+D"(d64), "+c"(qwords) : "a"(val) : "memory");
    d8 = (u8*)d64;
    __asm__ volatile("rep stosb" : "+D"(d8), "+c"(bytes) : "a"((u8)c) : "memory");
    return dst;
}

void *kmemcpy(void *dst, const void *src, size_t n) {
    size_t qwords = n / 8;
    size_t bytes  = n % 8;
    u64 *d64 = (u64*)dst;
    const u64 *s64 = (const u64*)src;
    u8 *d8;
    const u8 *s8;

    __asm__ volatile("rep movsq" : "+D"(d64), "+S"(s64), "+c"(qwords) : : "memory");
    d8 = (u8*)d64;
    s8 = (const u8*)s64;
    __asm__ volatile("rep movsb" : "+D"(d8), "+S"(s8), "+c"(bytes) : : "memory");
    return dst;
}

int kmemcmp(const void *a, const void *b, size_t n) {
    const u8 *pa = (const u8*)a, *pb = (const u8*)b;
    while (n--) {
        if (*pa != *pb) return (int)*pa - (int)*pb;
        pa++; pb++;
    }
    return 0;
}

/* -- String utilities -------------------------------------------------------- */
size_t kstrlen(const char *s) {
    size_t n = 0;
    while (*s++) n++;
    return n;
}

int kstrcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int kstrncmp(const char *a, const char *b, size_t n) {
    while (n && *a && *a == *b) { a++; b++; n--; }
    if (!n) return 0;
    return (unsigned char)*a - (unsigned char)*b;
}

char *kstrcpy(char *dst, const char *src) {
    char *d = dst;
    while ((*d++ = *src++));
    return dst;
}

char *kstrncpy(char *dst, const char *src, size_t n) {
    char *d = dst;
    while (n && (*d++ = *src++)) n--;
    if (n) *d = '\0';
    return dst;
}

char *kstrcat(char *dst, const char *src) {
    char *d = dst;
    while (*d) d++;
    while ((*d++ = *src++));
    return dst;
}

char *kstrchr(const char *s, int c) {
    while (*s) { if (*s == (char)c) return (char*)s; s++; }
    return NULL;
}

char *kstrrchr(const char *s, int c) {
    const char *last = NULL;
    while (*s) { if (*s == (char)c) last = s; s++; }
    return (char*)last;
}

void kitoa(i32 n, char *buf, int base) {
    if (base < 2 || base > 16) { buf[0]='\0'; return; }
    char tmp[33]; int i = 0;
    bool neg = (base == 10 && n < 0);
    u32 un = neg ? (u32)(-(u64)n) : (u32)n;
    if (un == 0) tmp[i++] = '0';
    else while (un) { tmp[i++] = "0123456789abcdef"[un%base]; un/=base; }
    if (neg) tmp[i++] = '-';
    int j = 0;
    while (i--) buf[j++] = tmp[i];
    buf[j] = '\0';
}

void kutoa(u32 n, char *buf, int base) {
    if (base < 2 || base > 16) { buf[0]='\0'; return; }
    char tmp[33]; int i = 0;
    if (n == 0) tmp[i++] = '0';
    else while (n) { tmp[i++] = "0123456789abcdef"[n%base]; n/=base; }
    int j = 0;
    while (i--) buf[j++] = tmp[i];
    buf[j] = '\0';
}

int katoi(const char *s) {
    int r = 0, sign = 1;
    while (*s == ' ') s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') r = r*10 + (*s++ - '0');
    return r * sign;
}

/* -- Serial (COM1) + in-kernel log buffer ---------------------------------- */
#define SERIAL_LOG_MAX 16384
static char serial_log_buf[SERIAL_LOG_MAX];
static u32  serial_log_len = 0;

static void serial_log_append_char(char c) {
    if (serial_log_len < SERIAL_LOG_MAX - 1) {
        serial_log_buf[serial_log_len++] = c;
        serial_log_buf[serial_log_len] = '\0';
        return;
    }

    for (u32 i = 1; i < SERIAL_LOG_MAX - 1; i++)
        serial_log_buf[i - 1] = serial_log_buf[i];
    serial_log_buf[SERIAL_LOG_MAX - 2] = c;
    serial_log_buf[SERIAL_LOG_MAX - 1] = '\0';
}

void serial_log_tail(char *buf, u32 max) {
    if (!buf || max == 0) return;
    if (serial_log_len == 0) { buf[0] = '\0'; return; }

    u32 n = serial_log_len;
    if (n >= max) n = max - 1;
    u32 start = serial_log_len - n;

    for (u32 i = 0; i < n; i++) buf[i] = serial_log_buf[start + i];
    buf[n] = '\0';
}

void serial_log_clear(void) {
    serial_log_len = 0;
    serial_log_buf[0] = '\0';
}

void serial_init(void) {
    outb(COM1_PORT+1, 0x00);
    outb(COM1_PORT+3, 0x80);
    outb(COM1_PORT+0, 0x03);
    outb(COM1_PORT+1, 0x00);
    outb(COM1_PORT+3, 0x03);
    outb(COM1_PORT+2, 0xC7);
    outb(COM1_PORT+4, 0x0B);
    serial_log_clear();
}

void serial_putchar(char c) {
    while (!(inb(COM1_PORT+5) & 0x20));
    outb(COM1_PORT, (u8)c);
    serial_log_append_char(c);
}

void serial_write(const char *s) {
    while (*s) serial_putchar(*s++);
}

__attribute__((noreturn)) void kernel_panic(u32 code, const char *msg) {
    char b[20];
    u64 rbp = 0, ret = 0;

    __asm__ volatile ("mov %%rbp, %0" : "=r"(rbp));
    if (rbp) ret = *((u64*)rbp + 1);

    serial_write("\n[KERNEL PANIC] code=0x");
    kutoa(code, b, 16); serial_write(b);
    serial_write(" msg="); serial_write(msg ? msg : "(null)");
    serial_write("\n  rbp=0x");
    kutoa(rbp >> 32, b, 16); serial_write(b);
    kutoa(rbp & 0xFFFFFFFF, b, 16); serial_write(b);
    serial_write("  ret=0x");
    kutoa(ret >> 32, b, 16); serial_write(b);
    kutoa(ret & 0xFFFFFFFF, b, 16); serial_write(b);
    serial_write("\n");

    terminal_write_colored("\n[KERNEL PANIC] ", VGA_ENTRY_COLOR(VGA_WHITE, VGA_RED));
    terminal_write_colored(msg ? msg : "panic", VGA_ENTRY_COLOR(VGA_WHITE, VGA_RED));
    terminal_write("\ncode=0x");
    kutoa(code, b, 16); terminal_write(b);
    terminal_write("\nSystem halted.\n");

    __asm__ volatile ("cli");
    while (1) { __asm__ volatile ("hlt"); }
}

/* -- Heap stats expected by shell.c / apps.c -------------------------------- */
u32 kmem_used(void) {
    u32 used = 0;
    block_hdr_t *b = heap_head;
    while (b) {
        if (!b->free) used += b->size;
        b = b->next;
    }
    return used;
}

u32 kmem_free_bytes(void) {
    u32 free_bytes = 0;
    block_hdr_t *b = heap_head;
    while (b) {
        if (b->free) free_bytes += b->size;
        b = b->next;
    }
    return free_bytes;
}

void sysinfo_print(void) {
    terminal_writeln("CareOS v7 - System Information");
    char buf[32];
    terminal_write("  Heap used:  "); kutoa(kmem_used()/1024, buf, 10);
    terminal_write(buf); terminal_writeln(" KB");
    terminal_write("  Heap free:  "); kutoa(kmem_free_bytes()/1024, buf, 10);
    terminal_write(buf); terminal_writeln(" KB");
}
