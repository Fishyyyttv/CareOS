/* apps/calc_engine.c -- Calculator arithmetic in double precision (pure).
 * Built with FP_CFLAGS in the kernel (SSE enabled; the scheduler fxsaves every
 * task, so FP state survives preemption) and with the host compiler in
 * tests/host/test_calc_engine.c. */
#include "calc_engine.h"

static bool is_finite(double v) { return v == v && v - v == 0.0; }

static void set_str(char *dst, const char *src, u32 cap) {
    u32 i = 0;
    for (; src[i] && i + 1 < cap; i++) dst[i] = src[i];
    dst[i] = '\0';
}

static u32 slen(const char *s) { u32 n = 0; while (s[n]) n++; return n; }

/* Append the decimal digits of n (n >= 0), zero-padded to at least `width`. */
static u32 put_u64(char *out, u32 pos, u32 cap, i64 n, u32 width) {
    char tmp[24]; u32 k = 0;
    do { tmp[k++] = (char)('0' + n % 10); n /= 10; } while (n > 0 && k < sizeof tmp);
    while (k < width && k < sizeof tmp) tmp[k++] = '0';
    while (k > 0 && pos + 1 < cap) out[pos++] = tmp[--k];
    out[pos] = '\0';
    return pos;
}

static i64 pow10i(u32 e) { i64 p = 1; while (e--) p *= 10; return p; }

/* Print n / 10^dec with trailing fractional zeros removed. */
static u32 put_fixed(char *out, u32 pos, u32 cap, i64 n, u32 dec) {
    i64 scale = pow10i(dec);
    pos = put_u64(out, pos, cap, n / scale, 0);
    i64 frac = n % scale;
    if (dec && frac) {
        u32 fdigits = dec;
        while (fdigits && frac % 10 == 0) { frac /= 10; fdigits--; }
        if (pos + 1 < cap) out[pos++] = '.';
        pos = put_u64(out, pos, cap, frac, fdigits);
    }
    out[pos] = '\0';
    return pos;
}

void calc_format(double v, char *out, u32 cap) {
    if (!is_finite(v)) { set_str(out, "Error", cap); return; }
    u32 pos = 0;
    out[0] = '\0';
    if (v == 0.0) { set_str(out, "0", cap); return; }
    if (v < 0) { out[pos++] = '-'; out[pos] = '\0'; v = -v; }

    if (v < 1e12 && v == (double)(i64)v) {            /* exact integer */
        put_u64(out, pos, cap, (i64)v, 0);
        return;
    }

    /* Decimal exponent e with 1 <= v/10^e < 10. */
    double m = v; i32 e = 0;
    while (m >= 10.0) { m /= 10.0; e++; }
    while (m < 1.0)   { m *= 10.0; e--; }

    if (v >= 1e-6 && v < 1e12) {                      /* fixed, 10 significant digits */
        i32 dec = 9 - e;
        if (dec < 0) dec = 0;
        if (dec > 17) dec = 17;
        i64 n = (i64)(v * (double)pow10i((u32)dec) + 0.5);
        put_fixed(out, pos, cap, n, (u32)dec);
        return;
    }

    /* Scientific: d.ddddddddd e[+-]N, trailing zeros trimmed. */
    i64 n = (i64)(m * 1e9 + 0.5);
    if (n >= 10000000000LL) { n /= 10; e++; }          /* 9.9999999999 rounded up */
    pos = put_fixed(out, pos, cap, n, 9);
    if (pos + 1 < cap) out[pos++] = 'e';
    if (pos + 1 < cap) out[pos++] = e < 0 ? '-' : '+';
    put_u64(out, pos, cap, e < 0 ? -e : e, 0);
}

double calc_parse(const char *s) {
    bool neg = false;
    if (*s == '-') { neg = true; s++; }
    double v = 0.0, scale = 1.0;
    bool frac = false;
    for (; *s; s++) {
        if (*s == '.') { frac = true; continue; }
        if (*s < '0' || *s > '9') break;
        if (frac) { scale /= 10.0; v += (*s - '0') * scale; }
        else v = v * 10.0 + (*s - '0');
    }
    return neg ? -v : v;
}

void calc_reset(calc_t *c) {
    c->acc = 0.0; c->op = 0; c->fresh = true; c->error = false;
    set_str(c->disp, "0", sizeof c->disp);
    c->expr[0] = '\0';
}

static void show(calc_t *c, double v) {
    if (!is_finite(v)) {
        set_str(c->disp, "Error", sizeof c->disp);
        c->error = true; c->op = 0; c->expr[0] = '\0';
        return;
    }
    calc_format(v, c->disp, sizeof c->disp);
}

/* Apply the pending operator to acc and the displayed entry. */
static void apply(calc_t *c) {
    double b = calc_parse(c->disp), r = c->acc;
    switch (c->op) {
    case '+': r = c->acc + b; break;
    case '-': r = c->acc - b; break;
    case '*': r = c->acc * b; break;
    case '/':
        if (b == 0.0) { show(c, 1.0 / b - 1.0 / b); return; }   /* NaN -> Error */
        r = c->acc / b; break;
    default:  r = b; break;
    }
    c->acc = r;
    show(c, r);
}

static u32 digit_count(const char *s) {
    u32 n = 0;
    for (; *s; s++) if (*s >= '0' && *s <= '9') n++;
    return n;
}

void calc_key(calc_t *c, char k) {
    if (k == 'c') { calc_reset(c); return; }
    if (c->error) {
        if ((k >= '0' && k <= '9') || k == '.' || k == 'b') calc_reset(c);
        else return;
        if (k == 'b') return;
    }

    if ((k >= '0' && k <= '9') || k == '.') {
        if (c->fresh) { set_str(c->disp, "0", sizeof c->disp); c->fresh = false; }
        u32 l = slen(c->disp);
        if (k == '.') {
            for (u32 i = 0; i < l; i++) if (c->disp[i] == '.') return;
        } else {
            if (digit_count(c->disp) >= CALC_MAX_DIGITS) return;
            if (!kstrncmp(c->disp, "0", 2))  { c->disp[0] = k; return; }
            if (!kstrncmp(c->disp, "-0", 3)) { c->disp[1] = k; return; }
        }
        if (l + 1 < sizeof c->disp) { c->disp[l] = k; c->disp[l + 1] = '\0'; }
        return;
    }

    switch (k) {
    case '+': case '-': case '*': case '/': {
        if (c->op && !c->fresh) apply(c);
        else if (!c->op) c->acc = calc_parse(c->disp);
        if (c->error) return;
        c->op = k; c->fresh = true;
        u32 l = slen(c->disp);
        if (l + 3 < sizeof c->expr) {
            kmemcpy(c->expr, c->disp, l);
            c->expr[l] = ' '; c->expr[l + 1] = k; c->expr[l + 2] = '\0';
        }
        return;
    }
    case '=':
        if (c->op && !c->fresh) apply(c);
        c->op = 0; c->fresh = true; c->expr[0] = '\0';
        return;
    case 'n': {
        if (!kstrncmp(c->disp, "0", 2)) return;
        u32 l = slen(c->disp);
        if (c->disp[0] == '-') { for (u32 i = 0; i < l; i++) c->disp[i] = c->disp[i + 1]; }
        else if (l + 1 < sizeof c->disp) {
            for (u32 i = l + 1; i > 0; i--) c->disp[i] = c->disp[i - 1];
            c->disp[0] = '-';
        }
        return;
    }
    case '%':
        show(c, calc_parse(c->disp) / 100.0);
        c->fresh = true;
        return;
    case 'b': {
        if (c->fresh) return;
        u32 l = slen(c->disp);
        if (l) c->disp[--l] = '\0';
        if (l == 0 || !kstrncmp(c->disp, "-", 2)) set_str(c->disp, "0", sizeof c->disp);
        return;
    }
    default:
        return;
    }
}
