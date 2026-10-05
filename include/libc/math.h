#ifndef _MATH_H
#define _MATH_H

/* Small freestanding math ABI used by the native web renderer. */
#include <stdlib.h>
#ifndef INFINITY
#define INFINITY (__builtin_inff())
#endif
#ifndef signbit
#define signbit(x) ((x) < 0)
#endif

double floor(double x); double ceil(double x); double fabs(double x); double trunc(double x);
double fmod(double x, double y); double sqrt(double x);
double pow(double x, double y); double cos(double x); double acos(double x);
float floorf(float x); float ceilf(float x); float fabsf(float x);
float fmodf(float x, float y); float sqrtf(float x);

#endif
