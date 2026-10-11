/* tests/host/test_fetch_token.c -- a finished fetch may only be applied to
 * the window instance, tab and request generation that asked for it. */
#include "pure.h"
#include "fetch_token.h"
#include <assert.h>
int main(void){
    fetch_token_t cur = { 5, 42, 0 };
    fetch_token_t ok  = { 5, 42, 0 };
    fetch_token_t old = { 4, 42, 0 };      /* superseded by a newer request */
    fetch_token_t oth = { 5, 43, 0 };      /* another window (or a reused slot) */
    fetch_token_t tab = { 5, 42, 1 };      /* the user switched tab */
    assert(fetch_token_matches(&ok, &cur));
    assert(!fetch_token_matches(&old, &cur));
    assert(!fetch_token_matches(&oth, &cur));
    assert(!fetch_token_matches(&tab, &cur));
    fetch_token_t cancelled = { 6, 0, 0 };  /* cancel clears the owner (id 0 is never a window) */
    assert(!fetch_token_matches(&ok, &cancelled));
    fetch_token_t zero = { 6, 0, 0 };
    assert(!fetch_token_matches(&zero, &cancelled));   /* ownerless never matches */
    puts("fetch_token OK"); return 0;
}
