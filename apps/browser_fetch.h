/* apps/browser_fetch.h -- background page loading for the browser.
 *
 * One kernel task performs fetches and builds pages with the native web
 * engine so the GUI loop never blocks on the network (a slow or unreachable
 * host used to freeze the whole desktop for the full connect timeout). One
 * request is in flight at a time; a newer submit supersedes an older one,
 * whose result is then dropped. After a page is taken, the same task keeps
 * loading its images in the background. */
#ifndef CAREOS_BROWSER_FETCH_H
#define CAREOS_BROWSER_FETCH_H
#include "pure.h"

typedef struct web_page web_page_t;

/* Install the engine's platform hooks (idempotent). */
void  browser_engine_init(void);
/* Viewport the next built page is laid out for (content area size). */
void  browser_fetch_set_viewport(int w, int h);
/* owner = the window's instance_id (non-zero); tab = the browser tab. */
void  browser_fetch_submit(u32 owner, u32 tab, const char *url);
void  browser_fetch_cancel(u32 owner);
/* True once when this owner/tab's latest request has finished. url_out gets
 * the final (post-redirect) URL, n_out the body size (<= 0 on failure) and
 * page_out the built page (NULL if building failed); the caller then owns the
 * page and must give it back with browser_fetch_release_page(). The first
 * BUFFER_SIZE bytes of the body are in browser_fetch_buffer(). */
bool  browser_fetch_take(u32 owner, u32 tab, char *url_out, u32 url_max, int *n_out,
                         web_page_t **page_out);
/* Free a page obtained from take() (safe while its images still load). */
void  browser_fetch_release_page(web_page_t *pg);
/* Is a request for this owner/tab pending, running or waiting to be taken? */
bool  browser_fetch_busy(u32 owner, u32 tab);
char *browser_fetch_buffer(void);
u32   browser_fetch_buffer_size(void);
u32   browser_fetch_elapsed_ms(void);

#endif
