#ifndef _SETJMP_H
#define _SETJMP_H

/* Implemented in assembly in kernel/libc_shim.c. Slots 0..7 hold the SysV
 * x86-64 callee-saved registers plus rsp and the return address; the rest of
 * the buffer is reserved. */
typedef long jmp_buf[16];

/* returns_twice is not decoration: without it GCC assumes control passes
 * through setjmp exactly once and may keep locals live in registers or fold
 * branches across the call, so the resumed path after longjmp would observe
 * stale values. noreturn on longjmp likewise lets callers drop dead code
 * after it instead of falling through. */
__attribute__((returns_twice)) int  setjmp(jmp_buf env);
__attribute__((noreturn))      void longjmp(jmp_buf env, int val);

#endif
