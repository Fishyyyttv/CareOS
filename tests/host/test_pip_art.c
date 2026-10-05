/* tests/host/test_pip_art.c -- every look and mood draws, inside its box. */
#include "pure.h"
#include "pip_art.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define S 96                       /* 96 px Pip: canvas covers -40..200 x -24..176 units */
#define CW (S * 240 / 160)
#define CH (S * 200 / 160)
static u32 px[CW * CH];

static void draw(const pip_look_t *l, const pip_pose_t *p) {
    memset(px, 0, sizeof px);
    pr_canvas_t c = { px, CW, CH };
    pr_mat_t m = pr_mul(pr_scale(S * 65536 / 160, S * 65536 / 160), pr_translate(40 * 65536, 24 * 65536));
    pip_art_draw(&c, m, l, p);
}
static u64 coverage(void) { u64 s = 0; for (u32 i = 0; i < CW * CH; i++) s += px[i] >> 24; return s; }

int main(void) {
    pip_look_t look = { 0, 0, 0, 0, 0 };
    pip_pose_t pose = { PM_IDLE, 0, true, false, 0, 0 };
    draw(&look, &pose);
    u64 base = coverage();
    assert(base > 0);
    /* the body sits where the artboard puts it: the centre of the 160 box is opaque */
    i32 cx = (40 + 80) * S / 160, cy = (24 + 98) * S / 160;
    assert((px[cy * CW + cx] >> 24) == 255);
    /* every mood, every phase, every look draws without a parse error */
    for (u32 m = 0; m < PM_COUNT; m++)
        for (u32 ph = 0; ph < 65536; ph += 16384) {
            pip_pose_t p = { (u8)m, (u16)ph, false, (ph & 16384) != 0, 3, -2 };
            draw(&look, &p);
            assert(coverage() > 0);
        }
    for (u8 c = 0; c < PIP_COLORS; c++)
        for (u8 h = 0; h < PIP_HATS; h++)
            for (u8 o = 0; o < PIP_OUTFITS; o++) {
                pip_look_t l = { c, h, o, (u8)((c + h) % PIP_ACCS), (u8)((h + o) % PIP_EFFECTS) };
                draw(&l, &pose);
            }
    assert(pip_art_errors() == 0);
    /* a hat adds pixels above the head */
    pip_look_t crown = { 0, 2, 0, 0, 0 };
    draw(&crown, &pose);
    u64 top = 0;
    for (i32 y = 0; y < (24 + 30) * S / 160; y++) for (i32 x = 0; x < CW; x++) top += px[y * CW + x] >> 24;
    assert(top > 0);
    /* labels and mood data */
    assert(!strcmp(pip_mood_label(PM_SLEEPING), "Sleeping") && !strcmp(pip_hat_label(2), "Crown"));
    assert(pip_mood_bulb(PM_HAPPY) == 0xFF8EC4 && pip_mood_bulb(PM_WATCHING) == 0x3DD68C);
    assert(pip_mood_cycle_ms(PM_IDLE) == 2600 && pip_mood_cycle_ms(PM_PLAYING) == 500);
    assert(!strcmp(pip_color_label(7), "Snow") && !strcmp(pip_effect_label(5), "Rainbow"));
    puts("pip_art OK");
    return 0;
}
