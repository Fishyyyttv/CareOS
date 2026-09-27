#include "pure.h"
#include "span.h"
#include <assert.h>
int main(void){
    i32 ox[2], ol[2];
    assert(span_minus(0,10, 20,30, ox,ol)==1 && ox[0]==0 && ol[0]==10);   /* no overlap */
    assert(span_minus(0,100, 20,30, ox,ol)==2 && ox[0]==0 && ol[0]==20 && ox[1]==30 && ol[1]==70);
    assert(span_minus(25,3, 20,30, ox,ol)==0);                           /* fully hidden */
    assert(span_minus(10,15, 20,30, ox,ol)==1 && ox[0]==10 && ol[0]==10); /* left part */
    assert(span_minus(25,15, 20,30, ox,ol)==1 && ox[0]==30 && ol[0]==10); /* right part */
    assert(span_minus(0,10, 5,5, ox,ol)==1 && ol[0]==10);                /* empty hole */
    assert(span_minus(0,0, 5,9, ox,ol)==0);                              /* empty span */
    assert(span_minus(20,10, 20,30, ox,ol)==0);                          /* exact cover */
    puts("span OK"); return 0;
}
