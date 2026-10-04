/* gui/spring.c -- damped spring, integrated in 1 ms steps. */
#include "spring.h"

void spring_init(spring_t *s, i32 pos, i32 k, i32 c) {
    s->pos = pos;
    s->target = pos;
    s->vel = 0;
    s->k = k;
    s->c = c;
}

void spring_set_target(spring_t *s, i32 target) { s->target = target; }

void spring_step(spring_t *s, u32 dt_ms) {
    if (dt_ms > 64) dt_ms = 64;            /* a stalled frame must not explode */
    i64 pos = s->pos, vel = s->vel;
    for (u32 i = 0; i < dt_ms; i++) {
        /* a = k (target - x) - c v, all Q16; one millisecond per step */
        i64 acc = (i64)s->k * ((i64)s->target - pos) - (i64)s->c * vel;
        vel += acc / 1000;
        pos += vel / 1000;
    }
    s->pos = (i32)pos;
    s->vel = (i32)vel;
    if (spring_settled(s)) { s->pos = s->target; s->vel = 0; }
}

bool spring_settled(const spring_t *s) {
    i32 d = s->target - s->pos;
    if (d < 0) d = -d;
    i32 v = s->vel < 0 ? -s->vel : s->vel;
    return d < 66 && v < 1300;             /* within 0.1 % and nearly still */
}
