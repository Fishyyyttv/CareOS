#ifndef _SETJMP_H
#define _SETJMP_H

/* frestanding setjmp is hard without assembly, but we can stub it if not used for real logic */
typedef long jmp_buf[16];

int setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val);

#endif
