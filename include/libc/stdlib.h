#ifndef _LIBC_STDLIB_H
#define _LIBC_STDLIB_H

#include <stddef.h>
#include "../../include/kernel.h"

void *malloc(size_t size);
void free(void *ptr);
void *realloc(void *ptr, size_t size);
void *calloc(size_t nmemb, size_t size);

int abs(int j);
int system(const char *command);
double atof(const char *nptr);
void exit(int status);
char *getenv(const char *name);
int atoi(const char *nptr);
long atol(const char *nptr);

void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *));

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

#endif
