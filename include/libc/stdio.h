#ifndef _LIBC_STDIO_H
#define _LIBC_STDIO_H

#include <stddef.h>
#include <stdarg.h>
#include "../../include/kernel.h"

#define EOF (-1)

typedef fs_node_t FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

FILE *fopen(const char *path, const char *mode);
int fclose(FILE *fp);
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *fp);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *fp);
int fseek(FILE *fp, long offset, int whence);
long ftell(FILE *fp);
void rewind(FILE *fp);
int  feof(FILE *fp);
int  ferror(FILE *fp);

int printf(const char *format, ...);
int fprintf(FILE *fp, const char *format, ...);
int sprintf(char *str, const char *format, ...);
int snprintf(char *str, size_t size, const char *format, ...);
int sscanf(const char *str, const char *format, ...);
int vsnprintf(char *str, size_t size, const char *format, va_list ap);
int vfprintf(FILE *stream, const char *format, va_list ap);

int fflush(FILE *stream);
int remove(const char *pathname);
int rename(const char *oldpath, const char *newpath);

int puts(const char *s);
int putchar(int c);

void perror(const char *s);

#endif
