/* kernel/kfmt.c -- printf-style formatting (pure, host-tested). See kfmt.h.
 *
 * Replaces the old kvsnprintf body, which knew only %d %u %x %p %s %c with a
 * zero pad: %i came out as a literal 'i' (DOOM built the config variable
 * "joystick_physical_buttoni", hit I_Error and took the kernel down with it),
 * %d was unsigned, and width / '-' / l / ll / z / precision were ignored. */
#include "kfmt.h"

typedef struct {
    char  *out;
    size_t size;
    size_t len;        /* chars the full output needs */
} sink_t;

static void put(sink_t *s, char c) {
    if (s->out && s->len + 1 < s->size) s->out[s->len] = c;
    s->len++;
}

static void put_n(sink_t *s, char c, int n) { while (n-- > 0) put(s, c); }

enum { F_LEFT = 1, F_ZERO = 2, F_PLUS = 4, F_SPACE = 8, F_ALT = 16 };

static void put_number(sink_t *s, u64 v, bool neg, unsigned base, bool upper,
                       int flags, int width, int prec) {
    char digits[24];
    int nd = 0;
    const char *set = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    if (v == 0 && prec != 0) digits[nd++] = '0';
    while (v) { digits[nd++] = set[v % base]; v /= base; }

    char sign = neg ? '-' : (flags & F_PLUS) ? '+' : (flags & F_SPACE) ? ' ' : 0;
    const char *prefix = "";
    if ((flags & F_ALT) && base == 16 && nd && !(nd == 1 && digits[0] == '0'))
        prefix = upper ? "0X" : "0x";
    int zeros = prec > nd ? prec - nd : 0;
    if ((flags & F_ALT) && base == 8 && zeros == 0 && (nd == 0 || digits[nd - 1] != '0')) zeros = 1;
    int plen = (int)kstrlen(prefix);
    int body = (sign ? 1 : 0) + plen + zeros + nd;
    int pad = width > body ? width - body : 0;

    /* '0' pads after the sign/prefix, unless '-' or a precision is given. */
    bool zero_pad = (flags & F_ZERO) && !(flags & F_LEFT) && prec < 0;
    if (zero_pad) { zeros += pad; pad = 0; }

    if (!(flags & F_LEFT)) put_n(s, ' ', pad);
    if (sign) put(s, sign);
    for (const char *p = prefix; *p; p++) put(s, *p);
    put_n(s, '0', zeros);
    while (nd) put(s, digits[--nd]);
    if (flags & F_LEFT) put_n(s, ' ', pad);
}

int kfmt_vformat(char *out, size_t size, const char *fmt, va_list ap) {
    sink_t s = { out, size, 0 };

    for (const char *f = fmt; *f; f++) {
        if (*f != '%') { put(&s, *f); continue; }
        f++;

        int flags = 0;
        for (;; f++) {
            if      (*f == '-') flags |= F_LEFT;
            else if (*f == '0') flags |= F_ZERO;
            else if (*f == '+') flags |= F_PLUS;
            else if (*f == ' ') flags |= F_SPACE;
            else if (*f == '#') flags |= F_ALT;
            else break;
        }

        int width = 0;
        if (*f == '*') {
            width = va_arg(ap, int);
            if (width < 0) { flags |= F_LEFT; width = -width; }
            f++;
        } else {
            while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0');
        }

        int prec = -1;
        if (*f == '.') {
            f++;
            if (*f == '*') { prec = va_arg(ap, int); if (prec < 0) prec = -1; f++; }
            else { prec = 0; while (*f >= '0' && *f <= '9') prec = prec * 10 + (*f++ - '0'); }
        }

        int lng = 0;                    /* -2 hh, -1 h, 0 int, 1 long/size, 2 long long */
        for (;; f++) {
            if      (*f == 'h') lng--;
            else if (*f == 'l') lng++;
            else if (*f == 'z' || *f == 'j' || *f == 't') lng = 1;
            else break;
        }

        switch (*f) {
        case 'd': case 'i': {
            i64 v;
            if (lng >= 1) v = lng >= 2 ? va_arg(ap, long long) : va_arg(ap, long);
            else {
                v = va_arg(ap, int);
                if (lng == -1) v = (short)v;
                else if (lng <= -2) v = (signed char)v;
            }
            u64 mag = v < 0 ? (u64)0 - (u64)v : (u64)v;
            put_number(&s, mag, v < 0, 10, false, flags, width, prec);
            break;
        }
        case 'u': case 'x': case 'X': case 'o': {
            u64 v;
            if (lng >= 1) v = lng >= 2 ? va_arg(ap, unsigned long long) : va_arg(ap, unsigned long);
            else {
                v = va_arg(ap, unsigned int);
                if (lng == -1) v = (unsigned short)v;
                else if (lng <= -2) v = (unsigned char)v;
            }
            unsigned base = (*f == 'o') ? 8 : (*f == 'u') ? 10 : 16;
            put_number(&s, v, false, base, *f == 'X', flags & ~(F_PLUS | F_SPACE), width, prec);
            break;
        }
        case 'p': {
            u64 v = (u64)(size_t)va_arg(ap, void *);
            put_number(&s, v, false, 16, false, (flags | F_ALT) & ~(F_PLUS | F_SPACE), width, prec);
            break;
        }
        case 'c': {
            char c = (char)va_arg(ap, int);
            if (!(flags & F_LEFT)) put_n(&s, ' ', width - 1);
            put(&s, c);
            if (flags & F_LEFT) put_n(&s, ' ', width - 1);
            break;
        }
        case 's': {
            const char *str = va_arg(ap, const char *);
            if (!str) str = "(null)";
            int n = 0;
            while (str[n] && (prec < 0 || n < prec)) n++;
            if (!(flags & F_LEFT)) put_n(&s, ' ', width - n);
            for (int i = 0; i < n; i++) put(&s, str[i]);
            if (flags & F_LEFT) put_n(&s, ' ', width - n);
            break;
        }
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G':
#ifdef HOST_TEST
            (void)va_arg(ap, double);
#endif
            /* The kernel builds without SSE, so doubles cannot be fetched. */
            put(&s, '?');
            break;
        case '%': put(&s, '%'); break;
        case '\0': f--; break;          /* trailing lone '%' */
        default: put(&s, '%'); put(&s, *f); break;
        }
    }

    if (out && size) out[s.len < size ? s.len : size - 1] = '\0';
    return (int)s.len;
}
