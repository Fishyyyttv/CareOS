#ifndef _LIBC_UNISTD_H
#define _LIBC_UNISTD_H

#include <stddef.h>
#include <stdint.h>

int access(const char *pathname, int mode);
#define F_OK 0

int close(int fd);
int read(int fd, void *buf, size_t count);
int write(int fd, const void *buf, size_t count);
long lseek(int fd, long offset, int whence);

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#endif
