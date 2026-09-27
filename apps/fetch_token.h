/* apps/fetch_token.h -- identifies which request a finished fetch belongs to. */
#ifndef CAREOS_FETCH_TOKEN_H
#define CAREOS_FETCH_TOKEN_H
#include "pure.h"

/* gen: bumped on every submit/cancel. owner: the window's instance_id (never
 * 0; stable across slot reuse, unlike a pointer). tab: the browser tab. */
typedef struct { u32 gen; u32 owner; u32 tab; } fetch_token_t;

bool fetch_token_matches(const fetch_token_t *result, const fetch_token_t *current);

#endif
