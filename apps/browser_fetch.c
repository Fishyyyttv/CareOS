/* apps/browser_fetch.c -- background page loading for the browser.
 *
 * One kernel task does all network work so the GUI loop never blocks: it
 * fetches the document (following redirects), builds the page with the
 * native engine (stylesheets are fetched here too), hands the finished
 * web_page_t to the GUI, and then keeps loading that page's images.
 *
 * Ownership:
 *  - A built page belongs to the worker until browser_fetch_take() returns
 *    it; an untaken (superseded) page is freed by the worker.
 *  - After the take, the GUI owns the page, but the worker may still be
 *    decoding one of its images. The GUI therefore never frees a page itself:
 *    browser_fetch_release_page() frees it now, or -- if an image load is in
 *    flight -- lets the worker free it when that load finishes.
 *  - Shared state changes happen with interrupts off (single CPU) and those
 *    sections are short. The raw-response buffer (browser_fetch_buffer) is
 *    kept for the legacy text renderer and the source view. */
#include "apps_common.h"
#include "browser_fetch.h"
#include "fetch_token.h"
#include "http_parse.h"
#include "../web/engine/web.h"

#define BF_RESP_SZ 131072u
#define BF_URL_MAX 512u
#define BF_DOC_MAX (4u << 20)     /* main document cap */
#define BF_SUB_MAX (3u << 20)     /* stylesheets / images */
#define BF_TIMESLICE_MS 5u        /* short slices: frames keep flowing during a load */

static char s_resp[BF_RESP_SZ];
static char s_req_url[BF_URL_MAX], s_run_url[BF_URL_MAX];
static volatile bool s_pending, s_running, s_done;
static fetch_token_t s_req, s_run, s_cur;      /* requested / being serviced / wanted */
static volatile int s_n;
static volatile u32 s_t0;
static int  s_task = -1;
static volatile int s_vw = 1024, s_vh = 700;

static web_page_t *volatile s_page;            /* built, not yet taken */
static web_page_t *volatile s_img_page;        /* taken page whose images we load */
static volatile bool s_img_busy;               /* worker is inside an image load */
static web_page_t *volatile s_free_after;      /* released while busy: worker frees */

static inline u64 irq_off(void) {
    u64 f; __asm__ volatile ("pushfq; pop %0; cli" : "=r"(f) :: "memory"); return f;
}
static inline void irq_on(u64 f) { if (f & 0x200) __asm__ volatile ("sti" ::: "memory"); }

/* ── Engine platform ─────────────────────────────────────────────── */

static void split_host(const char *h, char *host, u32 host_max, const char **path) {
    const char *sl = kstrchr(h, '/');
    u32 hl = sl ? (u32)(sl - h) : (u32)kstrlen(h);
    if (hl > host_max - 1) hl = host_max - 1;
    kmemcpy(host, h, hl); host[hl] = '\0';
    *path = sl ? sl : "/";
}

/* GET url into a fresh kmalloc buffer (cap bytes), following redirects and
 * removing headers / chunked framing. Returns the body or NULL. */
static unsigned char *fetch_body(const char *url, u32 cap, size_t *len, char *final_url, size_t final_cap) {
    if (len) *len = 0;
    if (final_url && final_cap) final_url[0] = '\0';
    char cur[1024];
    kstrncpy(cur, url ? url : "", sizeof cur - 1); cur[sizeof cur - 1] = '\0';
    for (int hop = 0; hop < 6; hop++) {
        char host[160]; const char *path;
        const char *p = cur;
        bool tls = !kstrncmp(p, "https://", 8);
        if (tls) p += 8;
        else if (!kstrncmp(p, "http://", 7)) p += 7;
        else return NULL;
        split_host(p, host, sizeof host, &path);
        u16 port = 80;
        char *colon = kstrchr(host, ':');
        if (colon && !tls) {
            *colon = '\0'; port = 0;
            for (char *d = colon + 1; *d >= '0' && *d <= '9'; d++) port = (u16)(port * 10 + (*d - '0'));
            if (!port) port = 80;
        }
        char *raw = (char *)kmalloc(cap);
        if (!raw) return NULL;
        net_lock();
        int n = tls ? https_get(host, path, raw, cap - 1) : http_get(host, port, path, raw, cap - 1);
        net_unlock();
        if (n <= 0) { kfree(raw); return NULL; }
        raw[n] = '\0';
        int code = http_status_code(raw, (u32)n);
        if (code == 301 || code == 302 || code == 303 || code == 307 || code == 308) {
            char loc[768], next[1024];
            if (http_header(raw, (u32)n, "Location", loc, sizeof loc)) {
                http_url_resolve(cur, loc, next, sizeof next);
                kstrncpy(cur, next, sizeof cur - 1);
                kfree(raw);
                continue;
            }
        }
        u32 off = http_body_offset(raw, (u32)n);
        if (off > (u32)n) off = (u32)n;
        u32 blen = (u32)n - off;
        if (http_is_chunked(raw, (u32)n)) blen = http_dechunk(raw + off, blen);
        for (u32 i = 0; i < blen; i++) raw[i] = raw[off + i];   /* dst < src: forward copy */
        raw[blen] = '\0';
        if (len) *len = blen;
        if (final_url && final_cap) { kstrncpy(final_url, cur, final_cap - 1); final_url[final_cap - 1] = '\0'; }
        return (unsigned char *)raw;
    }
    return NULL;
}

static unsigned char *web_fetch_cb(void *ctx, const char *url, size_t *len, char *final_url, size_t final_cap) {
    (void)ctx;
    return fetch_body(url, BF_SUB_MAX, len, final_url, final_cap);
}

extern const unsigned char web_font_sans_start[], web_font_sans_end[];
extern const unsigned char web_font_sans_bold_start[], web_font_sans_bold_end[];
extern const unsigned char web_font_mono_start[], web_font_mono_end[];
extern const unsigned char web_font_mono_bold_start[], web_font_mono_bold_end[];

static const unsigned char *web_font_cb(void *ctx, int face, size_t *len) {
    (void)ctx;
    const unsigned char *a = NULL, *b = NULL;
    switch (face) {
    case WEB_FONT_SANS:      a = web_font_sans_start;      b = web_font_sans_end;      break;
    case WEB_FONT_SANS_BOLD: a = web_font_sans_bold_start; b = web_font_sans_bold_end; break;
    case WEB_FONT_MONO:      a = web_font_mono_start;      b = web_font_mono_end;      break;
    case WEB_FONT_MONO_BOLD: a = web_font_mono_bold_start; b = web_font_mono_bold_end; break;
    default: break;
    }
    if (len) *len = (a && b && b >= a) ? (size_t)(b - a) : 0;
    return a;
}

static uint64_t web_now_cb(void *ctx) { (void)ctx; return (uint64_t)timer_ms(); }
static void web_log_cb(void *ctx, const char *msg) { (void)ctx; serial_write("[web] "); serial_write(msg); serial_write("\n"); }

void browser_engine_init(void) {
    static bool done;
    if (done) return;
    web_platform_t p = {0};
    p.fetch = web_fetch_cb;
    p.font_data = web_font_cb;
    p.now_ms = web_now_cb;
    p.log = web_log_cb;
    web_init(&p);
    done = true;
}

/* ── Worker ──────────────────────────────────────────────────────── */

/* Load images of the page the GUI is showing, one at a time, until done or
 * a new request arrives. */
static void load_images(void) {
    for (;;) {
        u64 f = irq_off();
        web_page_t *pg = s_img_page;
        if (!pg || s_pending) { irq_on(f); return; }
        s_img_busy = true;
        irq_on(f);
        int left = web_page_load_images(pg, 1);
        f = irq_off();
        s_img_busy = false;
        web_page_t *dead = s_free_after;
        s_free_after = NULL;
        if (left == 0 && s_img_page == pg) s_img_page = NULL;
        irq_on(f);
        if (dead) web_page_free(dead);
        if (left == 0) return;
    }
}

static void worker(void) {
    for (;;) {
        if (!s_pending) load_images();
        task_block_unless(&s_pending);          /* sleep until a request arrives */
        if (!s_pending) continue;
        u64 f = irq_off();
        kmemcpy(s_run_url, s_req_url, BF_URL_MAX);
        s_run = s_req;
        s_pending = false; s_done = false; s_running = true;
        web_page_t *stale = s_page;             /* superseded, never taken */
        s_page = NULL;
        int vw = s_vw, vh = s_vh;
        irq_on(f);
        if (stale) web_page_free(stale);
        s_t0 = timer_ms();

        size_t len = 0;
        char final_url[BF_URL_MAX];
        unsigned char *body = fetch_body(s_run_url, BF_DOC_MAX, &len, final_url, sizeof final_url);
        web_page_t *pg = NULL;
        int n = -1;
        if (body) {
            n = (int)len;
            /* keep a copy for the source view / legacy renderer */
            u32 keep = len < BF_RESP_SZ - 1 ? (u32)len : BF_RESP_SZ - 1;
            kmemcpy(s_resp, body, keep); s_resp[keep] = '\0';
            if (!s_pending) pg = web_page_load(final_url, (const char *)body, len, vw, vh);
            kfree(body);
            if (final_url[0]) kstrncpy(s_run_url, final_url, BF_URL_MAX - 1);
        }

        f = irq_off();
        bool wanted = fetch_token_matches(&s_run, &s_cur) && !s_pending;
        if (wanted) s_page = pg;
        s_n = n; s_running = false; s_done = true;
        irq_on(f);
        if (!wanted && pg) web_page_free(pg);
    }
}

void browser_fetch_set_viewport(int w, int h) {
    if (w > 64) s_vw = w;
    if (h > 64) s_vh = h;
}

void browser_fetch_submit(u32 owner, u32 tab, const char *url) {
    browser_engine_init();
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

bool browser_fetch_take(u32 owner, u32 tab, char *url_out, u32 url_max, int *n_out, web_page_t **page_out) {
    u64 f = irq_off();
    bool mine = s_done && s_cur.owner == owner && s_cur.tab == tab &&
                fetch_token_matches(&s_run, &s_cur);
    if (mine) {
        kstrncpy(url_out, s_run_url, url_max - 1); url_out[url_max - 1] = '\0';
        *n_out = s_n;
        if (page_out) *page_out = s_page;
        if (s_page) s_img_page = s_page;        /* keep loading its images */
        s_page = NULL;
        s_done = false;
        s_cur.owner = 0;                        /* consumed */
    }
    irq_on(f);
    if (mine && page_out && *page_out && s_task >= 0) task_wake(s_task);
    return mine;
}

void browser_fetch_release_page(web_page_t *pg) {
    if (!pg) return;
    u64 f = irq_off();
    bool defer = false;
    if (s_img_page == pg) {
        s_img_page = NULL;
        if (s_img_busy) { s_free_after = pg; defer = true; }
    }
    irq_on(f);
    if (!defer) web_page_free(pg);
}

bool  browser_fetch_busy(u32 owner, u32 tab) { return s_cur.owner == owner && s_cur.tab == tab; }
char *browser_fetch_buffer(void)      { return s_resp; }
u32   browser_fetch_buffer_size(void) { return BF_RESP_SZ; }
u32   browser_fetch_elapsed_ms(void)  { return s_running ? timer_ms() - s_t0 : 0; }
