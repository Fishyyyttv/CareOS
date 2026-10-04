#include "pure.h"
#include "spring.h"
#include <assert.h>

/* Run a spring at a fixed frame time; return the time it settled (ms) and the
 * extreme position it reached. */
static u32 run(spring_t *s, u32 frame_ms, i32 *peak, i32 *trough) {
    *peak = s->pos; *trough = s->pos;
    for (u32 t = 0; t < 5000; t += frame_ms) {
        spring_step(s, frame_ms);
        if (s->pos > *peak) *peak = s->pos;
        if (s->pos < *trough) *trough = s->pos;
        if (spring_settled(s)) return t + frame_ms;
    }
    return 0;
}

int main(void) {
    spring_t s;
    i32 hi, lo;

    /* Opening pop: settles on target in well under a second with a small,
     * visible overshoot. */
    spring_init(&s, 0, SPRING_POP_K, SPRING_POP_C);
    spring_set_target(&s, SPRING_ONE);
    u32 t = run(&s, 16, &hi, &lo);
    assert(t > 150 && t < 700);
    assert(s.pos == SPRING_ONE && s.vel == 0);
    assert(hi > SPRING_ONE && hi < SPRING_ONE + SPRING_ONE / 20);   /* 0 .. 5 % */

    /* Same curve at 60 and at 25 fps: the integration is per millisecond. */
    spring_t a, b;
    spring_init(&a, 0, SPRING_POP_K, SPRING_POP_C); spring_set_target(&a, SPRING_ONE);
    spring_init(&b, 0, SPRING_POP_K, SPRING_POP_C); spring_set_target(&b, SPRING_ONE);
    for (int i = 0; i < 10; i++) spring_step(&a, 20);                /* 200 ms */
    for (int i = 0; i < 5; i++) spring_step(&b, 40);
    assert(a.pos == b.pos);

    /* Early in the move it is fast, late it is slow (ease-out). */
    spring_init(&a, 0, SPRING_POP_K, SPRING_POP_C); spring_set_target(&a, SPRING_ONE);
    spring_step(&a, 25); spring_step(&a, 25);                       /* 0..50 ms */
    i32 first = a.pos;
    for (int i = 0; i < 6; i++) spring_step(&a, 25);                /* to 200 ms */
    i32 before = a.pos;
    spring_step(&a, 25); spring_step(&a, 25);                       /* 200..250 ms */
    assert(first > a.pos - before);

    /* Closing: critically damped, never below zero. */
    spring_init(&s, SPRING_ONE, SPRING_EXIT_K, SPRING_EXIT_C);
    spring_set_target(&s, 0);
    t = run(&s, 16, &hi, &lo);
    assert(t > 100 && t < 700);
    assert(lo >= 0 && s.pos == 0);

    /* Reversing mid-flight keeps the velocity (no jump). */
    spring_init(&s, 0, SPRING_POP_K, SPRING_POP_C);
    spring_set_target(&s, SPRING_ONE);
    spring_step(&s, 80);
    i32 mid = s.pos, vel = s.vel;
    spring_set_target(&s, 0);
    spring_step(&s, 1);
    assert(s.pos >= mid && vel > 0);

    /* A stalled frame is clamped, not integrated as one huge step. */
    spring_init(&s, 0, SPRING_POP_K, SPRING_POP_C);
    spring_set_target(&s, SPRING_ONE);
    spring_step(&s, 100000);
    assert(s.pos > 0 && s.pos < SPRING_ONE + SPRING_ONE / 10);

    puts("spring OK");
    return 0;
}
