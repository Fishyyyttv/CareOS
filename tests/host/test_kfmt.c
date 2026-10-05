#include "pure.h"
#include "kfmt.h"
#include <assert.h>
#include <stdarg.h>

static char out[128];

static int fmt_to(char *buf, size_t size, const char *f, ...) {
    va_list ap;
    va_start(ap, f);
    int n = kfmt_vformat(buf, size, f, ap);
    va_end(ap);
    return n;
}

/* Format with kfmt and libc and require identical output and length. */
static void same(const char *f, ...) {
    char want[128];
    va_list ap, ap2;
    va_start(ap, f);
    va_copy(ap2, ap);
    int n = kfmt_vformat(out, sizeof(out), f, ap);
    int m = vsnprintf(want, sizeof(want), f, ap2);
    va_end(ap2);
    va_end(ap);
    if (n != m || strcmp(out, want) != 0) {
        fprintf(stderr, "fmt \"%s\": got \"%s\" (%d), want \"%s\" (%d)\n", f, out, n, want, m);
        assert(0);
    }
}

int main(void) {
    /* %i is what DOOM's joystick config variable names use. */
    same("joystick_physical_button%i", 3);
    same("%d %i", -42, -7);
    same("%u %x %X %o", 4000000000u, 0xbeefu, 0xbeefu, 8u);
    same("%5d|%-5d|%05d|%+d|% d", 42, 42, 42, 42, 42);
    same("%05d|%-05d|%5d", -42, -42, -42);
    same("%-8s|%8s|%.3s|%.*s", "ab", "ab", "abcdef", 2, "xyz");
    same("%*d|%-*d|%*d", 6, 7, 6, 7, -4, 9);
    same("%ld %lu %lld %llx", -5000000000L, 5000000000UL, -1LL, 0x1234567890abULL);
    same("%zu %hd %hhu", (size_t)99, (short)-3, (unsigned char)200);
    same("%c%c %%", 'o', 'k');
    same("%.3d|%8.3d|%-8.3x|%.0d|", 5, 5, 0xa, 0);
    same("%#x %#o %#X", 255u, 8u, 0u);
    same("%s|", "");
    same("%d", (int)0x80000000);

    /* NULL strings print as (null) rather than faulting. */
    fmt_to(out, sizeof(out), "%s", (char *)NULL);
    assert(strcmp(out, "(null)") == 0);

    /* %p is a full 64-bit pointer. */
    fmt_to(out, sizeof(out), "%p", (void *)0x123456789abcULL);
    assert(strcmp(out, "0x123456789abc") == 0);

    /* Truncation: always NUL-terminated, returns the untruncated length. */
    char small[6];
    assert(fmt_to(small, sizeof(small), "hello %s", "world") == 11);
    assert(strcmp(small, "hello") == 0);
    assert(fmt_to(small, sizeof(small), "%8d", 1) == 8);
    assert(strcmp(small, "     ") == 0);

    /* Counting mode: NULL buffer of size 0 writes nothing. */
    assert(fmt_to(NULL, 0, "abc%d", 12) == 5);

    printf("kfmt: ok\n");
    return 0;
}
