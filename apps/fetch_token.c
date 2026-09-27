/* apps/fetch_token.c -- identifies which request a finished fetch belongs to. */
#include "fetch_token.h"

bool fetch_token_matches(const fetch_token_t *r, const fetch_token_t *c) {
    return c->owner != 0 && r->owner == c->owner && r->gen == c->gen && r->tab == c->tab;
}
