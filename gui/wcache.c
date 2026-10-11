/* gui/wcache.c -- per-window pixel cache policy (pure; host-tested). */
#include "wcache.h"

static bool fully_on_screen(const wcache_query_t *q) {
    return q->x >= 0 && q->y >= 0 &&
           (i64)q->x + q->w <= q->screen_w && (i64)q->y + q->h <= q->screen_h;
}

wcache_action_t wcache_decide(wcache_state_t *st, const wcache_query_t *q) {
    /* Anything that can change the window's pixels every frame: draw live and
     * drop the cache so it is recaptured once the window settles. */
    if (q->focused || q->animating || q->hover_near || q->busy) {
        st->valid = false;
        return WC_DRAW;
    }
    bool usable = st->valid && st->w == q->w && st->h == q->h &&
                  st->theme_id == q->theme_id && st->font_id == q->font_id &&
                  (u32)(q->now_ms - st->captured_ms) < WCACHE_REFRESH_MS + q->stagger_ms;
    if (usable) return WC_BLIT;
    /* Capturing a partly off-screen window would store missing pixels. */
    return fully_on_screen(q) ? WC_DRAW_AND_CAPTURE : WC_DRAW;
}

void wcache_captured(wcache_state_t *st, const wcache_query_t *q) {
    st->valid = true;
    st->w = q->w; st->h = q->h;
    st->captured_ms = q->now_ms;
    st->theme_id = q->theme_id; st->font_id = q->font_id;
}
