/* apps/browser_fetch.c -- background HTTP(S) fetches for the browser.
 *
 * Buffer ownership: the worker writes s_resp only while servicing a request;
 * the GUI reads it only between a successful browser_fetch_take() and its next
 * browser_fetch_submit(). A submit always supersedes the unconsumed result,
 * so the worker may overwrite it. Shared state changes happen with interrupts
 * off (single CPU), and those sections are short (a URL copy at most). */
#include "apps_common.h"
#include "browser_fetch.h"
#include "fetch_token.h"

#define BF_RESP_SZ 131072u
#define BF_URL_MAX 512u
#define BF_TIMESLICE_MS 5u    /* short slices: frames keep flowing during a load */

static char s_resp[BF_RESP_SZ];
static char s_req_url[BF_URL_MAX], s_run_url[BF_URL_MAX];
static volatile bool s_pending, s_running, s_done;
static fetch_token_t s_req, s_run, s_cur;      /* requested / being serviced / wanted */
static volatile int s_n;
static volatile u32 s_t0;
static int  s_task = -1;

static inline u64 irq_off(void) {
    u64 f; __asm__ volatile ("pushfq; pop %0; cli" : "=r"(f) :: "memory"); return f;
}
static inline void irq_on(u64 f) { if (f & 0x200) __asm__ volatile ("sti" ::: "memory"); }

static void split_host(const char *h, char *host, u32 host_max, const char **path) {
    const char *sl = kstrchr(h, '/');
    u32 hl = sl ? (u32)(sl - h) : (u32)kstrlen(h);
    if (hl > host_max - 1) hl = host_max - 1;
    kmemcpy(host, h, hl); host[hl] = '\0';
    *path = sl ? sl : "/";
}

static int fetch_once(const char *url) {
    char host[128]; const char *path;
    if (kstrncmp(url, "https://", 8) == 0) {
        split_host(url + 8, host, sizeof host, &path);
        return https_get(host, path, s_resp, BF_RESP_SZ - 1);
    }
    const char *h = url;
    if (kstrncmp(url, "http://", 7) == 0) h = url + 7;
    split_host(h, host, sizeof host, &path);
    u16 port = 80;
    char *colon = kstrchr(host, ':');
    if (colon) {
        *colon = '\0'; port = 0;
        for (char *d = colon + 1; *d >= '0' && *d <= '9'; d++) port = (u16)(port * 10 + (*d - '0'));
        if (!port) port = 80;
    }
    return http_get(host, port, path, s_resp, BF_RESP_SZ - 1);
}

static void worker(void) {
    for (;;) {
        task_block_unless(&s_pending);          /* sleep until a request arrives */
        if (!s_pending) continue;
        u64 f = irq_off();
        kmemcpy(s_run_url, s_req_url, BF_URL_MAX);
        s_run = s_req;
        s_pending = false; s_done = false; s_running = true;
        irq_on(f);
        s_t0 = timer_ms();

        net_lock();
        int n = fetch_once(s_run_url);
        net_unlock();

        f = irq_off();
        s_n = n; s_running = false; s_done = true;
        irq_on(f);
    }
}

void browser_fetch_submit(u32 owner, u32 tab, const char *url) {
    if (s_task < 0) {
        s_task = task_create("browser-fetch", worker);
        if (s_task >= 0) task_set_timeslice(s_task, BF_TIMESLICE_MS * PIT_HZ / 1000);
    }
    u64 f = irq_off();
    s_cur.gen++; s_cur.owner = owner; s_cur.tab = tab;
    s_req = s_cur;
    kstrncpy(s_req_url, url, BF_URL_MAX - 1); s_req_url[BF_URL_MAX - 1] = '\0';
    s_pending = true;
    irq_on(f);
    task_wake(s_task);
}

void browser_fetch_cancel(u32 owner) {
    u64 f = irq_off();
    if (s_cur.owner == owner) { s_cur.owner = 0; s_cur.gen++; }
    irq_on(f);
}

bool browser_fetch_take(u32 owner, u32 tab, char *url_out, u32 url_max, int *n_out) {
    u64 f = irq_off();
    bool mine = s_done && s_cur.owner == owner && s_cur.tab == tab &&
                fetch_token_matches(&s_run, &s_cur);
    if (mine) {
        kstrncpy(url_out, s_run_url, url_max - 1); url_out[url_max - 1] = '\0';
        *n_out = s_n;
        s_done = false;
        s_cur.owner = 0;                        /* consumed */
    }
    irq_on(f);
    return mine;
}

bool  browser_fetch_busy(u32 owner, u32 tab) { return s_cur.owner == owner && s_cur.tab == tab; }
char *browser_fetch_buffer(void)      { return s_resp; }
u32   browser_fetch_buffer_size(void) { return BF_RESP_SZ; }
u32   browser_fetch_elapsed_ms(void)  { return s_running ? timer_ms() - s_t0 : 0; }
