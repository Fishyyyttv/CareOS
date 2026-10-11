/* tests/host/test_textbuf.c -- growable heap text buffers (window text_buf). */
#include "pure.h"
#include "textbuf.h"
#include <assert.h>
int main(void){
    char *b = NULL; u32 cap = 0;
    assert(tb_reserve(&b, &cap, 10, 1000) && cap >= 10);
    assert(b[0] == '\0');                                    /* fresh buffer is a valid "" */
    strcpy(b, "hello");
    assert(tb_reserve(&b, &cap, 600, 1000) && cap >= 600);
    assert(strcmp(b, "hello") == 0);                         /* contents preserved */
    u32 before = cap;
    assert(tb_reserve(&b, &cap, 5, 1000) && cap == before);  /* no shrink */
    assert(!tb_reserve(&b, &cap, 1001, 1000));               /* over max */
    assert(strcmp(b, "hello") == 0 && cap == before);        /* unchanged on failure */
    assert(tb_reserve(&b, &cap, 1000, 1000) && cap == 1000); /* exactly max */
    assert(!tb_reserve(&b, &cap, 1001, 1000));               /* over max even if cap is big */
    free(b);

    /* A small max caps the very first allocation too. */
    char *s = NULL; u32 scap = 0;
    assert(tb_reserve(&s, &scap, 10, 64) && scap <= 64 && scap >= 10);
    assert(!tb_reserve(&s, &scap, 65, 64));
    free(s);

    /* Zero need on an empty buffer still yields a usable (empty) string. */
    char *z = NULL; u32 zcap = 0;
    assert(tb_reserve(&z, &zcap, 1, 100) && z && z[0] == '\0');
    free(z);
    puts("textbuf OK"); return 0;
}
