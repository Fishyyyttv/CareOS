#ifndef _ASSERT_H
#define _ASSERT_H

#include <stdio.h>
#include <stdlib.h>

#ifdef NDEBUG
#define assert(ignore) ((void)0)
#else
#define assert(expression) \
    if(!(expression)) { \
        printf("Assertion failed: %s, file %s, line %d\n", #expression, __FILE__, __LINE__); \
        exit(1); \
    }
#endif

#endif
