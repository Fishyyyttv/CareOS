/* gui/spring.h -- damped spring motion (fixed point, no FPU). Pure, host-tested.
 *
 * pos/target are Q16 (65536 = 1.0); vel is Q16 per second. Stiffness k is in
 * 1/s^2 and damping c in 1/s for a unit mass, the same parameters a CSS or
 * SwiftUI spring takes. The step integrates in 1 ms slices (semi-implicit
 * Euler), so the curve does not depend on the frame rate. */
#ifndef CAREOS_SPRING_H
#define CAREOS_SPRING_H
#include "pure.h"

#define SPRING_ONE 65536

typedef struct {
    i32 pos, target;
    i32 vel;
    i32 k, c;
} spring_t;

void spring_init(spring_t *s, i32 pos, i32 k, i32 c);
void spring_set_target(spring_t *s, i32 target);
void spring_step(spring_t *s, u32 dt_ms);     /* dt is clamped to 64 ms */
bool spring_settled(const spring_t *s);       /* at rest on its target */

/* Presets. POP: a quick, slightly underdamped settle (damping ratio ~0.8,
 * ~1.5 % overshoot, settled ~650 ms). EXIT: critically damped, quicker, no
 * overshoot (so a closing value never dips below zero). */
#define SPRING_POP_K   210
#define SPRING_POP_C    23
#define SPRING_EXIT_K  600
#define SPRING_EXIT_C   49

#endif
