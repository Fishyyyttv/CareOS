/* apps/browser_fetch.h -- background HTTP(S) fetches for the browser.
 *
 * One kernel task performs fetches so the GUI loop never blocks on the
 * network (a slow or unreachable host used to freeze the whole desktop for
 * the full connect timeout). One request is in flight at a time; a newer
 * submit supersedes an older one, whose result is then dropped. */
#ifndef CAREOS_BROWSER_FETCH_H
#define CAREOS_BROWSER_FETCH_H
#include "pure.h"

/* owner = the window's instance_id (non-zero); tab = the browser tab. */
void  browser_fetch_submit(u32 owner, u32 tab, const char *url);
void  browser_fetch_cancel(u32 owner);
/* True once when this owner/tab's latest request has finished. The response
 * (n bytes, n <= 0 on failure) is then in browser_fetch_buffer() and stays
 * valid until the next browser_fetch_submit(). */
bool  browser_fetch_take(u32 owner, u32 tab, char *url_out, u32 url_max, int *n_out);
/* Is a request for this owner/tab pending, running or waiting to be taken? */
bool  browser_fetch_busy(u32 owner, u32 tab);
char *browser_fetch_buffer(void);
u32   browser_fetch_buffer_size(void);
u32   browser_fetch_elapsed_ms(void);

#endif
