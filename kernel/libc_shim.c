#include "kernel.h"
#include "libc/stdio.h"
#include "libc/stdlib.h"
#include "libc/string.h"
#include "libc/ctype.h"
#include "libc/unistd.h"
#include "libc/time.h"
#include "libc/signal.h"
#include "libc/math.h"
#include "libc/errno.h"
#include "libc/sys/types.h"
#include "libc/sys/stat.h"
#include "libc/dirent.h"
#include "libc/setjmp.h"

int errno = 0;

/* Doom Network Stubs */
int drone = 0;
int net_client_connected = 0;

/* -- FILE handle table (fopen/fread/fseek/ftell/fclose) ------------------- */
#define SHIM_MAX_FILES 16
/* Writable handles keep the whole file in wbuf and flush it with one
 * vfs_write() on fflush/fclose: vfs_write() always rewrites the file from
 * offset 0, and Doom's savegame code calls fwrite() once per byte. */
typedef struct {
    fs_node_t *node;
    u32        pos;
    bool       used;
    bool       writable;
    bool       dirty;
    char      *wbuf;
    u32        wlen;   /* logical file size while writable */
    u32        wcap;
} shim_file_t;
static shim_file_t shim_files[SHIM_MAX_FILES];

/* Absolute form of path. Ported code (Doom) uses relative paths such as
 * "./.savegame/doomsav0.dsg", but vfs_resolve_path() only accepts absolute
 * ones, so relative paths are anchored at the current user's home. */
static void shim_abs_path(const char *path, char *out, u32 max) {
    if (path[0] == '/') { kstrncpy(out, path, max - 1); out[max - 1] = '\0'; return; }
    while (path[0] == '.' && path[1] == '/') path += 2;
    if (user_current_uid() == 0) kstrcpy(out, "/root");
    else { kstrcpy(out, "/home/"); kstrncpy(out + 6, user_current_name(), FS_NAME_MAX); }
    if (path[0] == '\0' || (path[0] == '.' && path[1] == '\0')) return;
    u32 l = kstrlen(out);
    if (l + 1 + kstrlen(path) >= max) { out[0] = '\0'; return; }
    out[l] = '/'; kstrcpy(out + l + 1, path);
}

/* Split an absolute path into its (existing) parent directory and leaf name.
 * Trailing slashes are ignored. Returns NULL if the parent does not exist. */
static fs_node_t *shim_parent(const char *abs, char *leaf) {
    char buf[FS_PATH_MAX];
    kstrncpy(buf, abs, FS_PATH_MAX - 1); buf[FS_PATH_MAX - 1] = '\0';
    u32 l = kstrlen(buf);
    while (l > 1 && buf[l - 1] == '/') buf[--l] = '\0';
    char *slash = NULL;
    for (char *p = buf; *p; p++) if (*p == '/') slash = p;
    if (!slash || !slash[1]) return NULL;
    kstrncpy(leaf, slash + 1, FS_NAME_MAX - 1); leaf[FS_NAME_MAX - 1] = '\0';
    if (slash == buf) return vfs_root();
    *slash = '\0';
    fs_node_t *dir = vfs_resolve_path(buf);
    return (dir && dir->type == FS_DIR) ? dir : NULL;
}

/* Grow a writable handle's buffer to hold at least need bytes. */
static bool shim_reserve(shim_file_t *fh, u32 need) {
    if (need <= fh->wcap) return true;
    if (need > FS_FILE_MAX_BYTES) return false;
    u32 cap = fh->wcap ? fh->wcap : 4096;
    while (cap < need) cap *= 2;
    if (cap > FS_FILE_MAX_BYTES) cap = FS_FILE_MAX_BYTES;
    char *nb = (char *)kmalloc(cap);
    if (!nb) return false;
    if (fh->wbuf) { kmemcpy(nb, fh->wbuf, fh->wlen); kfree(fh->wbuf); }
    fh->wbuf = nb;
    fh->wcap = cap;
    return true;
}

static u32 shim_size(shim_file_t *fh) {
    return fh->writable ? fh->wlen : fh->node->size;
}

int strcasecmp(const char *s1, const char *s2) {
    while (*s1 && *s2) {
        char c1 = (*s1 >= 'A' && *s1 <= 'Z') ? (*s1 + 32) : *s1;
        char c2 = (*s2 >= 'A' && *s2 <= 'Z') ? (*s2 + 32) : *s2;
        if (c1 != c2) return (unsigned char)c1 - (unsigned char)c2;
        s1++; s2++;
    }
    return (unsigned char)*s1 - (unsigned char)*s2;
}

int strncasecmp(const char *s1, const char *s2, size_t n) {
    if (n == 0) return 0;
    while (n-- > 0 && *s1 && *s2) {
        char c1 = (*s1 >= 'A' && *s1 <= 'Z') ? (*s1 + 32) : *s1;
        char c2 = (*s2 >= 'A' && *s2 <= 'Z') ? (*s2 + 32) : *s2;
        if (c1 != c2) return (unsigned char)c1 - (unsigned char)c2;
        if (n == 0) return 0;
        s1++; s2++;
    }
    if (n == (size_t)-1) return 0;
    return (unsigned char)*s1 - (unsigned char)*s2;
}

time_t time(time_t *t) {
    time_t res = (time_t)(timer_ms() / 1000);
    if (t) *t = res;
    return res;
}

void (*signal(int sig, void (*func)(int)))(int) { return (void*)0; }
int raise(int sig) { return 0; }

int open(const char *pathname, int flags, ...) { return -1; }
int close(int fd) { return -1; }
int read(int fd, void *buf, size_t count) { return -1; }
int write(int fd, const void *buf, size_t count) { return -1; }
long lseek(int fd, long offset, int whence) { return -1; }

int stat(const char *pathname, struct stat *statbuf) {
    /* struct stat *st = (struct stat *)statbuf; */
    /* We don't have enough info in fs_node_t yet to fill this perfectly */
    return -1;
}

int mkdir(const char *pathname, mode_t mode) {
    (void)mode;
    char abs[FS_PATH_MAX], leaf[FS_NAME_MAX];
    shim_abs_path(pathname, abs, sizeof(abs));
    fs_node_t *n = vfs_resolve_path(abs);
    if (n) return n->type == FS_DIR ? 0 : -1;   /* already there: fine for Doom */
    fs_node_t *parent = shim_parent(abs, leaf);
    return (parent && vfs_mkdir(parent, leaf)) ? 0 : -1;
}

/* -- Standard I/O stubs -- */
FILE *stdin = NULL;
FILE *stdout = NULL;
FILE *stderr = NULL;

FILE *fopen(const char *path, const char *mode) {
    if (!path || !mode) return NULL;
    char abs[FS_PATH_MAX];
    shim_abs_path(path, abs, sizeof(abs));
    if (!abs[0]) return NULL;

    bool trunc  = kstrchr(mode, 'w') != NULL;
    bool append = kstrchr(mode, 'a') != NULL;
    bool plus   = kstrchr(mode, '+') != NULL;
    bool writable = trunc || append || plus;

    fs_node_t *node = vfs_resolve_path(abs);
    if (!node && (trunc || append)) {       /* "w"/"a" create missing files */
        char leaf[FS_NAME_MAX];
        fs_node_t *parent = shim_parent(abs, leaf);
        if (parent) node = vfs_mkfile(parent, leaf);
    }
    if (!node || node->type != FS_FILE) return NULL;

    for (int i = 0; i < SHIM_MAX_FILES; i++) {
        shim_file_t *fh = &shim_files[i];
        if (fh->used) continue;
        kmemset(fh, 0, sizeof(*fh));
        fh->node     = node;
        fh->used     = true;
        fh->writable = writable;
        if (writable) {
            /* Seed the buffer with current contents unless truncating. */
            u32 keep = (trunc || !node->data) ? 0 : node->size;
            if (!shim_reserve(fh, keep ? keep : 1)) { fh->used = false; return NULL; }
            if (keep) kmemcpy(fh->wbuf, node->data, keep);
            fh->wlen  = keep;
            fh->dirty = trunc;              /* "w" empties the file even if unwritten */
            if (append) fh->pos = keep;
        }
        return (FILE *)fh;
    }
    return NULL;
}

int fflush(FILE *stream) {
    shim_file_t *fh = (shim_file_t *)stream;
    if (!fh || fh < shim_files || fh >= shim_files + SHIM_MAX_FILES) return 0;
    if (!fh->used || !fh->writable || !fh->dirty) return 0;
    if (vfs_write(fh->node, fh->wbuf ? fh->wbuf : "", fh->wlen) < 0) return -1;
    fh->dirty = false;
    return 0;
}

int fclose(FILE *fp) {
    shim_file_t *fh = (shim_file_t *)fp;
    int rc = 0;
    if (fh && fh >= shim_files && fh < shim_files + SHIM_MAX_FILES) {
        rc = fflush(fp);
        if (fh->wbuf) kfree(fh->wbuf);
        kmemset(fh, 0, sizeof(*fh));
    }
    return rc;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *fp) {
    shim_file_t *fh = (shim_file_t *)fp;
    if (!fh || !fh->used || !fh->node || size == 0) return 0;
    size_t total = size * nmemb;
    u32    fsize = shim_size(fh);
    u32    avail = fsize > fh->pos ? fsize - fh->pos : 0;
    if (total > (size_t)avail) total = (size_t)avail;
    if (total == 0) return 0;
    /* Owned and borrowed content now share one pointer. */
    const char *src = fh->writable ? fh->wbuf : fh->node->data;
    if (!src) return 0;
    kmemcpy(ptr, src + fh->pos, total);
    fh->pos += (u32)total;
    return total / size;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *fp) {
    shim_file_t *fh = (shim_file_t *)fp;
    if (!fh || !fh->used || !fh->writable || !ptr || size == 0) return 0;
    u32 total = (u32)(size * nmemb);
    if (!shim_reserve(fh, fh->pos + total)) return 0;
    if (fh->pos > fh->wlen)                 /* seek past EOF: zero-fill the gap */
        kmemset(fh->wbuf + fh->wlen, 0, fh->pos - fh->wlen);
    kmemcpy(fh->wbuf + fh->pos, ptr, total);
    fh->pos += total;
    if (fh->pos > fh->wlen) fh->wlen = fh->pos;
    fh->dirty = true;
    return nmemb;
}

int fseek(FILE *fp, long offset, int whence) {
    shim_file_t *fh = (shim_file_t *)fp;
    if (!fh || !fh->used || !fh->node) return -1;
    long new_pos;
    if      (whence == 0) new_pos = offset;                            /* SEEK_SET */
    else if (whence == 1) new_pos = (long)fh->pos + offset;           /* SEEK_CUR */
    else if (whence == 2) new_pos = (long)shim_size(fh) + offset;     /* SEEK_END */
    else return -1;
    if (new_pos < 0) return -1;
    fh->pos = (u32)new_pos;
    return 0;
}

long ftell(FILE *fp) {
    shim_file_t *fh = (shim_file_t *)fp;
    if (!fh || !fh->used) return -1;
    return (long)fh->pos;
}

/* Console output from ported code (Doom) goes to the serial log, so a fatal
 * I_Error message is visible instead of vanishing. */
static int shim_vlog(const char *format, va_list ap) {
    static char buf[512];
    int n = kvsnprintf(buf, sizeof(buf), format, ap);
    serial_write(buf);
    return n;
}

int printf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    int n = shim_vlog(format, args);
    va_end(args);
    return n;
}

int fprintf(FILE *fp, const char *format, ...) {
    va_list args;
    va_start(args, format);
    (void)fp;   /* files and std streams alike go to the serial log */
    int n = shim_vlog(format, args);
    va_end(args);
    return n;
}

int vsnprintf(char *str, size_t size, const char *format, va_list ap) {
    return kvsnprintf(str, size, format, ap);
}

int snprintf(char *str, size_t size, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int n = kvsnprintf(str, size, format, args);
    va_end(args);
    return n;
}

int sprintf(char *str, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int n = kvsnprintf(str, 1024, format, args);
    va_end(args);
    return n;
}

int vfprintf(FILE *stream, const char *format, va_list ap) {
    (void)stream;
    return shim_vlog(format, ap);
}

void rewind(FILE *fp) { fseek(fp, 0, 0); }
int  feof(FILE *fp) {
    shim_file_t *fh = (shim_file_t *)fp;
    if (!fh || !fh->used) return 1;
    return fh->pos >= shim_size(fh) ? 1 : 0;
}
int ferror(FILE *fp) { return 0; }


int remove(const char *pathname) {
    char abs[FS_PATH_MAX];
    shim_abs_path(pathname, abs, sizeof(abs));
    fs_node_t *n = vfs_resolve_path(abs);
    if (n) return vfs_delete(n);
    return -1;
}

/* Doom saves to temp.dsg and then renames it over doomsavN.dsg. Same-dir
 * renames are a VFS name change; across directories the data is copied. */
int rename(const char *oldpath, const char *newpath) {
    char from[FS_PATH_MAX], to[FS_PATH_MAX], leaf[FS_NAME_MAX];
    shim_abs_path(oldpath, from, sizeof(from));
    shim_abs_path(newpath, to, sizeof(to));
    fs_node_t *src = vfs_resolve_path(from);
    fs_node_t *dst_dir = shim_parent(to, leaf);
    if (!src || !dst_dir) return -1;
    if (src == vfs_resolve_path(to)) return 0;

    fs_node_t *old = vfs_find(dst_dir, leaf);   /* POSIX: replace the target */
    if (old && vfs_delete(old) != 0) return -1;

    if (src->parent == dst_dir) return vfs_rename(src, leaf);
    if (src->type != FS_FILE) return -1;
    fs_node_t *dst = vfs_mkfile(dst_dir, leaf);
    if (!dst || vfs_write(dst, src->data ? src->data : "", src->size) < 0) return -1;
    return vfs_delete(src);
}

int putchar(int c) {
    terminal_putchar(c);
    return c;
}

/* -- stdlib.h -- */
int sscanf(const char *str, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int count = 0;
    while (*format) {
        if (*format == '%') {
            format++;
            if (*format == 'x') {
                u32 *p = va_arg(args, u32 *);
                u32 val = 0;
                while (*str == ' ') str++;
                if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) str += 2;
                while (1) {
                    u32 digit;
                    if (*str >= '0' && *str <= '9') digit = *str - '0';
                    else if (*str >= 'a' && *str <= 'f') digit = *str - 'a' + 10;
                    else if (*str >= 'A' && *str <= 'F') digit = *str - 'A' + 10;
                    else break;
                    val = (val << 4) | digit;
                    str++;
                }
                *p = val;
                count++;
            } else if (*format == 'd') {
                int *p = va_arg(args, int *);
                *p = katoi(str);
                while (*str == ' ' || *str == '-' || (*str >= '0' && *str <= '9')) str++;
                count++;
            }
        }
        format++;
    }
    va_end(args);
    return count;
}
double atof(const char *nptr) {
    double res = 0.0;
    double sign = 1.0;
    if (*nptr == '-') { sign = -1.0; nptr++; }
    while (*nptr >= '0' && *nptr <= '9') {
        res = res * 10.0 + (*nptr - '0');
        nptr++;
    }
    if (*nptr == '.') {
        nptr++;
        double factor = 0.1;
        while (*nptr >= '0' && *nptr <= '9') {
            res += (*nptr - '0') * factor;
            factor /= 10.0;
            nptr++;
        }
    }
    return res * sign;
}

void *malloc(size_t size) { return kmalloc(size); }
void free(void *ptr) { kfree(ptr); }
void abort(void) { kernel_panic(0xAB07, "C runtime abort"); }
int abs(int j) { return (j < 0) ? -j : j; }
int system(const char *command) { return -1; }



void *realloc(void *ptr, size_t size) {
    if (!ptr) return kmalloc(size);
    if (size == 0) { kfree(ptr); return NULL; }
    
    /* We need to know the old size to copy safely. 
       In CareOS memory.c, the size is stored in the block header. */
    block_hdr_t *b = (block_hdr_t *)((u8*)ptr - sizeof(block_hdr_t));
    if (b->magic != HEAP_MAGIC) return NULL; /* HEAP_MAGIC */
    
    size_t old_size = (size_t)b->size;
    if (size <= old_size) return ptr; /* Already big enough */
    
    void *new_ptr = kmalloc(size);
    if (!new_ptr) return NULL;
    
    kmemcpy(new_ptr, ptr, old_size);
    kfree(ptr);
    return new_ptr;
}

void exit(int status) {
    kernel_panic(status, "Application exited");
}

int atoi(const char *nptr) { return katoi(nptr); }

int puts(const char *s) {
    while(*s) terminal_putchar(*s++);
    terminal_putchar('\n');
    return 0;
}

/* -- string.h -- */
char *strchr(const char *s, int c) {
    while (*s) { if (*s == (char)c) return (char *)s; s++; }
    if (c == 0) return (char *)s;
    return NULL;
}

char *strrchr(const char *s, int c) {
    const char *last = NULL;
    do { if (*s == (char)c) last = s; } while (*s++);
    return (char *)last;
}

char *strstr(const char *haystack, const char *needle) {
    if (!*needle) return (char *)haystack;
    for (; *haystack; haystack++) {
        if (*haystack == *needle) {
            const char *h = haystack, *n = needle;
            while (*h && *n && *h == *n) { h++; n++; }
            if (!*n) return (char *)haystack;
        }
    }
    return NULL;
}

char *strdup(const char *s) {
    size_t len = kstrlen(s) + 1;
    char *new = kmalloc(len);
    if (new) kmemcpy(new, s, len);
    return new;
}

/* Floating-point functions are provided by the musl math subset. */

void *memset(void *s, int c, size_t n) { return kmemset(s, c, n); }

void *memcpy(void *dest, const void *src, size_t n) { return kmemcpy(dest, src, n); }
void *memmove(void *dest, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dest; const unsigned char *s = (const unsigned char *)src;
    if (d == s) return dest;
    if (d < s) for (size_t i = 0; i < n; i++) d[i] = s[i];
    else for (size_t i = n; i-- > 0;) d[i] = s[i];
    return dest;
}
int memcmp(const void *s1, const void *s2, size_t n) { return kmemcmp(s1, s2, n); }
void *memchr(const void *s, int c, size_t n) {
    const unsigned char *p = (const unsigned char *)s;
    for (size_t i = 0; i < n; i++) if (p[i] == (unsigned char)c) return (void *)(p + i);
    return NULL;
}

char *strpbrk(const char *s, const char *accept) {
    for (; *s; s++) for (const char *a = accept; *a; a++) if (*s == *a) return (char *)s;
    return NULL;
}
size_t strlen(const char *s) { return kstrlen(s); }
int strcmp(const char *s1, const char *s2) { return kstrcmp(s1, s2); }
int strncmp(const char *s1, const char *s2, size_t n) { return kstrncmp(s1, s2, n); }
char *strcpy(char *dest, const char *src) { return kstrcpy(dest, src); }
/* Standard strncpy: zero-fills the rest of dest. kstrncpy writes one NUL and
 * leaves the tail as-is, which broke DOOM's 8-byte lump-name compares. */
char *strncpy(char *dest, const char *src, size_t n) {
    size_t i = 0;
    for (; i < n && src[i]; i++) dest[i] = src[i];
    for (; i < n; i++) dest[i] = '\0';
    return dest;
}

static unsigned long shim_strtoul(const char *s, char **end, int base) {
    const char *p = s; while (isspace((unsigned char)*p)) p++;
    bool neg = false; if (*p == '+' || *p == '-') { neg = *p++ == '-'; }
    if (base == 0) { base = 10; if (p[0] == '0') { base = 8; if (p[1] == 'x' || p[1] == 'X') { base = 16; p += 2; } } }
    unsigned long v = 0; const char *first = p;
    for (;;) {
        int d = (*p >= '0' && *p <= '9') ? *p - '0' : (*p >= 'a' && *p <= 'z') ? *p - 'a' + 10 : (*p >= 'A' && *p <= 'Z') ? *p - 'A' + 10 : -1;
        if (d < 0 || d >= base) break;
        v = v * (unsigned)base + (unsigned)d; p++;
    }
    if (end) *end = (char *)(p == first ? s : p);
    return neg ? (unsigned long)(-(long)v) : v;
}
long strtol(const char *s, char **end, int base) { return (long)shim_strtoul(s, end, base); }
unsigned long strtoul(const char *s, char **end, int base) { return shim_strtoul(s, end, base); }
float strtof(const char *s, char **end) {
    const char *p = s; while (isspace((unsigned char)*p)) p++;
    bool neg = false; if (*p == '+' || *p == '-') { neg = *p++ == '-'; }
    float v = 0; bool any = false;
    while (*p >= '0' && *p <= '9') { v = v * 10.0f + (float)(*p++ - '0'); any = true; }
    if (*p == '.') { p++; float q = 0.1f; while (*p >= '0' && *p <= '9') { v += (*p++ - '0') * q; q *= 0.1f; any = true; } }
    if (end) *end = (char *)(any ? p : s);
    return neg ? -v : v;
}

void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *)) {
    unsigned char *a = (unsigned char *)base;
    for (size_t i = 1; i < nmemb; i++) {
        size_t j = i;
        while (j > 0 && compar(a + j * size, a + (j - 1) * size) < 0) {
            for (size_t k = 0; k < size; k++) { unsigned char t = a[j * size + k]; a[j * size + k] = a[(j - 1) * size + k]; a[(j - 1) * size + k] = t; }
            j--;
        }
    }
}

/* -- ctype.h -- */
int toupper(int c) { if (c >= 'a' && c <= 'z') return c - 'a' + 'A'; return c; }
int isspace(int c) { return (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'); }

int access(const char *pathname, int mode) {
    char abs[FS_PATH_MAX];
    shim_abs_path(pathname, abs, sizeof(abs));
    return vfs_resolve_path(abs) ? 0 : -1;
}

int fstat(int fd, struct stat *statbuf) { return -1; }

/* -- dirent.h -- */
DIR *opendir(const char *name) { return NULL; }
struct dirent *readdir(DIR *dirp) { return NULL; }
int closedir(DIR *dirp) { return 0; }

/* -- setjmp.h --
 *
 * Real x86-64 System V implementation. These cannot be written in C: setjmp
 * has to capture the caller's stack pointer and return address, and longjmp
 * has to restore them and jump, which no portable C construct can express.
 *
 * jmp_buf is long[16] (128 bytes); we use the first 8 slots:
 *   0:rbx  8:rbp  16:r12  24:r13  32:r14  40:r15  48:rsp  56:rip
 *
 * Only the SysV callee-saved integer registers need saving -- every xmm
 * register is caller-saved in this ABI, so no FPU/SSE state belongs here.
 * The x87 control word and MXCSR are likewise not restored, matching the
 * behaviour glibc's _setjmp (as opposed to sigsetjmp) provides.
 *
 * rsp is stored as it will be *after* the ret pops the return address, so
 * longjmp lands on a stack that looks exactly like a normal return from
 * setjmp to its caller. */
__asm__(
    ".text\n"
    ".global setjmp\n"
    ".type setjmp, @function\n"
    "setjmp:\n"
    "  movq %rbx,   0(%rdi)\n"
    "  movq %rbp,   8(%rdi)\n"
    "  movq %r12,  16(%rdi)\n"
    "  movq %r13,  24(%rdi)\n"
    "  movq %r14,  32(%rdi)\n"
    "  movq %r15,  40(%rdi)\n"
    "  leaq 8(%rsp), %rax\n"      /* rsp as of after our ret */
    "  movq %rax,  48(%rdi)\n"
    "  movq (%rsp), %rax\n"       /* our return address */
    "  movq %rax,  56(%rdi)\n"
    "  xorl %eax, %eax\n"         /* direct call returns 0 */
    "  ret\n"
    ".size setjmp, .-setjmp\n"

    ".global longjmp\n"
    ".type longjmp, @function\n"
    "longjmp:\n"
    "  movl %esi, %eax\n"
    "  testl %eax, %eax\n"
    "  jnz 1f\n"
    "  movl $1, %eax\n"           /* longjmp(env,0) must surface as 1 */
    "1:\n"
    "  movq  0(%rdi), %rbx\n"
    "  movq  8(%rdi), %rbp\n"
    "  movq 16(%rdi), %r12\n"
    "  movq 24(%rdi), %r13\n"
    "  movq 32(%rdi), %r14\n"
    "  movq 40(%rdi), %r15\n"
    "  movq 48(%rdi), %rsp\n"
    "  jmp *56(%rdi)\n"           /* resume at the setjmp call site */
    ".size longjmp, .-longjmp\n"
);

void *calloc(size_t nmemb, size_t size) {
    void *ptr = kmalloc(nmemb * size);
    if (ptr) kmemset(ptr, 0, nmemb * size);
    return ptr;
}

char *getenv(const char *name) { return NULL; }

