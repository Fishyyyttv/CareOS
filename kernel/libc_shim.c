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
typedef struct {
    fs_node_t *node;
    u32        pos;
    bool       used;
} shim_file_t;
static shim_file_t shim_files[SHIM_MAX_FILES];

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
    time_t res = (time_t)(timer_get_ticks() / 1000);
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
    return 0; /* Just pretend it worked */
}

/* -- Standard I/O stubs -- */
FILE *stdin = NULL;
FILE *stdout = NULL;
FILE *stderr = NULL;

FILE *fopen(const char *path, const char *mode) {
    fs_node_t *node = vfs_resolve_path(path);
    if (!node) return NULL;
    for (int i = 0; i < SHIM_MAX_FILES; i++) {
        if (!shim_files[i].used) {
            shim_files[i].node = node;
            shim_files[i].pos  = 0;
            shim_files[i].used = true;
            return (FILE *)&shim_files[i];
        }
    }
    return NULL;
}

int fclose(FILE *fp) {
    shim_file_t *fh = (shim_file_t *)fp;
    if (fh && fh >= shim_files && fh < shim_files + SHIM_MAX_FILES) {
        fh->used = false;
        fh->node = NULL;
        fh->pos  = 0;
    }
    return 0;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *fp) {
    shim_file_t *fh = (shim_file_t *)fp;
    if (!fh || !fh->used || !fh->node) return 0;
    size_t total = size * nmemb;
    u32    avail = fh->node->size > fh->pos ? fh->node->size - fh->pos : 0;
    if (total > (size_t)avail) total = (size_t)avail;
    if (total == 0) return 0;
    const char *src = fh->node->raw_data
                      ? (const char *)fh->node->raw_data
                      : fh->node->data;
    kmemcpy(ptr, src + fh->pos, total);
    fh->pos += (u32)total;
    return total / size;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *fp) {
    return 0;
}

int fseek(FILE *fp, long offset, int whence) {
    shim_file_t *fh = (shim_file_t *)fp;
    if (!fh || !fh->used || !fh->node) return -1;
    long new_pos;
    if      (whence == 0) new_pos = offset;                            /* SEEK_SET */
    else if (whence == 1) new_pos = (long)fh->pos + offset;           /* SEEK_CUR */
    else if (whence == 2) new_pos = (long)fh->node->size + offset;    /* SEEK_END */
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

int printf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    kvsnprintf(NULL, 0, format, args);
    va_end(args);
    return 0;
}

int fprintf(FILE *fp, const char *format, ...) {
    va_list args;
    va_start(args, format);
    /* For now, just print to console even if fp is a file */
    kvsnprintf(NULL, 0, format, args);
    va_end(args);
    return 0;
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
    return kvsnprintf(NULL, 0, format, ap);
}

int fflush(FILE *stream) { return 0; }

void rewind(FILE *fp) { fseek(fp, 0, 0); }
int  feof(FILE *fp) {
    shim_file_t *fh = (shim_file_t *)fp;
    if (!fh || !fh->used) return 1;
    return fh->pos >= fh->node->size ? 1 : 0;
}
int ferror(FILE *fp) { return 0; }


int remove(const char *pathname) {
    fs_node_t *n = vfs_resolve_path(pathname);
    if (n) return vfs_delete(n);
    return -1;
}

int rename(const char *oldpath, const char *newpath) {
    /* Simple stub: Doom only uses this for savegames */
    return 0;
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

double fabs(double x) { return (x < 0) ? -x : x; }

void *memset(void *s, int c, size_t n) { return kmemset(s, c, n); }

void *memcpy(void *dest, const void *src, size_t n) { return kmemcpy(dest, src, n); }
int memcmp(const void *s1, const void *s2, size_t n) { return kmemcmp(s1, s2, n); }
size_t strlen(const char *s) { return kstrlen(s); }
int strcmp(const char *s1, const char *s2) { return kstrcmp(s1, s2); }
int strncmp(const char *s1, const char *s2, size_t n) { return kstrncmp(s1, s2, n); }
char *strcpy(char *dest, const char *src) { return kstrcpy(dest, src); }
char *strncpy(char *dest, const char *src, size_t n) { return kstrncpy(dest, src, n); }

/* -- ctype.h -- */
int toupper(int c) { if (c >= 'a' && c <= 'z') return c - 'a' + 'A'; return c; }
int isspace(int c) { return (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'); }

int access(const char *pathname, int mode) {
    return vfs_resolve_path(pathname) ? 0 : -1;
}

int fstat(int fd, struct stat *statbuf) { return -1; }

/* -- dirent.h -- */
DIR *opendir(const char *name) { return NULL; }
struct dirent *readdir(DIR *dirp) { return NULL; }
int closedir(DIR *dirp) { return 0; }

/* -- setjmp.h -- */
int setjmp(jmp_buf env) { return 0; }
void longjmp(jmp_buf env, int val) { while(1); }

void *calloc(size_t nmemb, size_t size) {
    void *ptr = kmalloc(nmemb * size);
    if (ptr) kmemset(ptr, 0, nmemb * size);
    return ptr;
}

char *getenv(const char *name) { return NULL; }

