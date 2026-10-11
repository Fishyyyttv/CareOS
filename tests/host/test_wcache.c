/* tests/host/test_wcache.c -- when may an unfocused window be blitted from
 * its cached pixels instead of being fully re-rendered? */
#include "pure.h"
#include "wcache.h"
#include <assert.h>

static wcache_query_t base(void) {
    wcache_query_t q;
    memset(&q, 0, sizeof q);
    q.x = 100; q.y = 100; q.w = 400; q.h = 300;
    q.screen_w = 1920; q.screen_h = 1080;
    q.now_ms = 10000; q.theme_id = 1; q.font_id = 0;
    return q;
}

int main(void) {
    wcache_state_t st; memset(&st, 0, sizeof st);
    wcache_query_t q = base();

    /* Nothing cached yet: draw and capture. */
    assert(wcache_decide(&st, &q) == WC_DRAW_AND_CAPTURE);
    wcache_captured(&st, &q);
    assert(st.valid);

    /* Fresh, same size/theme/font: blit. Moving the window is fine. */
    q.now_ms += 100; q.x += 37; q.y -= 20;
    assert(wcache_decide(&st, &q) == WC_BLIT);

    /* Focused windows always draw live, and lose their cache. */
    q.focused = true;
    assert(wcache_decide(&st, &q) == WC_DRAW);
    assert(!st.valid);
    q.focused = false;
    assert(wcache_decide(&st, &q) == WC_DRAW_AND_CAPTURE);
    wcache_captured(&st, &q);

    /* Pointer near the window (hover effects), busy app, animation: live. */
    q.hover_near = true;  assert(wcache_decide(&st, &q) == WC_DRAW); q.hover_near = false;
    wcache_captured(&st, &q);
    q.busy = true;        assert(wcache_decide(&st, &q) == WC_DRAW); q.busy = false;
    wcache_captured(&st, &q);
    q.animating = true;   assert(wcache_decide(&st, &q) == WC_DRAW); q.animating = false;
    wcache_captured(&st, &q);

    /* Resize, theme or font change: re-render. */
    q.w += 10;            assert(wcache_decide(&st, &q) == WC_DRAW_AND_CAPTURE);
    wcache_captured(&st, &q);
    q.theme_id = 2;       assert(wcache_decide(&st, &q) == WC_DRAW_AND_CAPTURE);
    wcache_captured(&st, &q);
    q.font_id = 5;        assert(wcache_decide(&st, &q) == WC_DRAW_AND_CAPTURE);
    wcache_captured(&st, &q);

    /* Periodic refresh keeps background content current (stagger delays it). */
    q.now_ms += WCACHE_REFRESH_MS - 1;   assert(wcache_decide(&st, &q) == WC_BLIT);
    q.now_ms += 1;                       assert(wcache_decide(&st, &q) == WC_DRAW_AND_CAPTURE);
    wcache_captured(&st, &q);
    q.stagger_ms = 200;
    q.now_ms += WCACHE_REFRESH_MS + 100; assert(wcache_decide(&st, &q) == WC_BLIT);
    q.now_ms += 100;                     assert(wcache_decide(&st, &q) == WC_DRAW_AND_CAPTURE);
    q.stagger_ms = 0;

    /* Never capture a window that is partly off screen: those pixels would be
     * missing. An existing valid cache may still be blitted there. */
    memset(&st, 0, sizeof st);
    q = base(); q.x = -50;
    assert(wcache_decide(&st, &q) == WC_DRAW);
    q = base(); wcache_captured(&st, &q);
    q.x = 1800; q.now_ms += 50;          /* dragged partly off the right edge */
    assert(wcache_decide(&st, &q) == WC_BLIT);
    q.now_ms += WCACHE_REFRESH_MS;       /* stale and off screen: draw, no capture */
    assert(wcache_decide(&st, &q) == WC_DRAW);

    /* Timer wrap: captured just before u32 wrap, queried just after. */
    memset(&st, 0, sizeof st);
    q = base(); q.now_ms = 0xFFFFFF00u; wcache_captured(&st, &q);
    q.now_ms = 0x00000010u;              /* 0x110 ms later */
    assert(wcache_decide(&st, &q) == WC_BLIT);

    puts("wcache OK");
    return 0;
}
