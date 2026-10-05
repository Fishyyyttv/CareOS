/* Minimal libm subset for the freestanding CareOS image. The browser only
 * needs basic geometry and font rasterisation math; keeping this here avoids
 * pulling a hosted libm into the kernel link. */
#include <math.h>

#ifdef signbit
#undef signbit
#endif
int signbit(double x) { return x < 0; }

float fabsf(float x) { return x < 0 ? -x : x; }
double trunc(double x) { return (double)(long)x; }
double floor(double x) { long n = (long)x; return (x < 0 && x != (double)n) ? (double)(n - 1) : (double)n; }
double ceil(double x) { long n = (long)x; return (x > 0 && x != (double)n) ? (double)(n + 1) : (double)n; }
double fmod(double x, double y) { if (y == 0) return 0; return x - (double)(long)(x / y) * y; }
double sqrt(double x) {
    if (x <= 0) return 0;
    double g = x > 1 ? x : 1;
    for (int i = 0; i < 12; i++) g = (g + x / g) * 0.5;
    return g;
}
double pow(double x, double y) {
    if (y == 0) return 1;
    if (y > 0.3332 && y < 0.3335) return sqrt(sqrt(x) * x);
    int n = (int)y;
    if ((double)n == y) {
        double r = 1; int k = n < 0 ? -n : n;
        while (k--) r *= x;
        return n < 0 ? (r ? 1 / r : 0) : r;
    }
    return 1;
}
double cos(double x) {
    const double pi = 3.141592653589793;
    x = fmod(x + pi, 2 * pi); if (x < 0) x += 2 * pi; x -= pi;
    double x2 = x * x;
    return 1 - x2 * 0.5 + x2 * x2 / 24 - x2 * x2 * x2 / 720 + x2 * x2 * x2 * x2 / 40320;
}
double acos(double x) {
    if (x >= 1) return 0;
    if (x <= -1) return 3.141592653589793;
    double lo = 0, hi = 3.141592653589793;
    for (int i = 0; i < 28; i++) { double m = (lo + hi) * 0.5; if (cos(m) > x) lo = m; else hi = m; }
    return (lo + hi) * 0.5;
}
float floorf(float x) { return (float)floor((double)x); }
float ceilf(float x) { return (float)ceil((double)x); }
float fmodf(float x, float y) { return (float)fmod((double)x, (double)y); }
float sqrtf(float x) { return (float)sqrt((double)x); }
