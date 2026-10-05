/* web/engine/web.h -- CareOS native web engine ("Weave"), public API.
 *
 * Portable C: builds on the host (test harness) and freestanding inside CareOS.
 * The engine parses HTML (lexbor), runs the CSS cascade, lays the page out and
 * paints it into a caller-provided 0xFFRRGGBB canvas. Everything platform
 * specific (network, font bytes, logging) comes in through web_platform_t. */
#ifndef WEB_H
#define WEB_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

enum {
    WEB_FONT_SANS = 0, WEB_FONT_SANS_BOLD,
    WEB_FONT_MONO,     WEB_FONT_MONO_BOLD,
    WEB_FONT_COUNT
};

typedef struct {
    void *ctx;
    /* Fetch a URL synchronously. Returns a malloc()ed body (the engine free()s
     * it) and its length, or NULL. May write the post-redirect URL into
     * final_url. content_type may be NULL. */
    unsigned char *(*fetch)(void *ctx, const char *url, size_t *len,
                            char *final_url, size_t final_cap);
    /* Raw TTF bytes for a WEB_FONT_* face (must stay valid forever). */
    const unsigned char *(*font_data)(void *ctx, int face, size_t *len);
    /* Monotonic milliseconds (timers, load budgets). May be NULL. */
    uint64_t (*now_ms)(void *ctx);
    void (*log)(void *ctx, const char *msg);   /* may be NULL */
} web_platform_t;

typedef struct web_page web_page_t;

/* Install the platform once (fonts are loaded lazily from it). */
void web_init(const web_platform_t *p);

/* Parse `html` (UTF-8) fetched from `url`, fetch and apply its stylesheets,
 * build the box tree and lay it out for a viewport_w x viewport_h viewport.
 * Images are NOT fetched here; see web_page_load_images(). Never returns NULL
 * unless out of memory. */
web_page_t *web_page_load(const char *url, const char *html, size_t len,
                          int viewport_w, int viewport_h);

/* Fetch up to `max_images` pending images (0 = all). Returns how many are still
 * pending. Safe to call from a background task while the UI thread paints, as
 * long as the UI thread calls web_page_relayout_if_needed() before painting. */
int  web_page_load_images(web_page_t *pg, int max_images);

/* Re-run layout if the viewport changed or images arrived. Returns true if the
 * layout changed. */
bool web_page_relayout_if_needed(web_page_t *pg, int viewport_w, int viewport_h);

int         web_page_height(web_page_t *pg);       /* document height, px */
int         web_page_width(web_page_t *pg);        /* document width, px  */
const char *web_page_title(web_page_t *pg);
const char *web_page_url(web_page_t *pg);

/* Paint the document region starting at (scroll_x, scroll_y) into canvas
 * (cw x ch pixels, `stride` pixels per row). */
void web_page_paint(web_page_t *pg, uint32_t *canvas, int cw, int ch, int stride,
                    int scroll_x, int scroll_y);

/* Link under a document-space point; writes the absolute URL. */
bool web_page_link_at(web_page_t *pg, int x, int y, char *href, size_t cap);

/* Document y of an element id (for #fragment navigation), or -1. */
int  web_page_anchor_y(web_page_t *pg, const char *id);

void web_page_free(web_page_t *pg);

/* Debug: one line per box (tag, display, document geometry). */
void web_page_dump(web_page_t *pg, void (*out)(const char *line));

#endif
