/* include/pure.h -- lets small pure-logic modules compile both in the
 * freestanding kernel and in host unit tests (make host-test, -DHOST_TEST). */
#ifndef CAREOS_PURE_H
#define CAREOS_PURE_H

#ifdef HOST_TEST
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
typedef uint8_t  u8;  typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
typedef int8_t   i8;  typedef int16_t  i16; typedef int32_t  i32; typedef int64_t  i64;
#define kmemcpy  memcpy
#define kmemset  memset
#define kstrlen  strlen
#define kstrncpy strncpy
#define kstrncmp strncmp
#define ksprintf sprintf
#define kmalloc  malloc
#define kfree    free
static inline void serial_write(const char *s) { (void)s; }
#else
#include "kernel.h"
#endif
#endif
