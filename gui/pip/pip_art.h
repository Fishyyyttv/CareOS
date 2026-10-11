/* gui/pip/pip_art.h -- Pip as drawing data (pure, host-tested).
 * Transcribed from the Pet artboard (docs/design/careos-v10/Pet.dc.html):
 * the 160-unit design grid, the layer order, the mood table and the CSS
 * keyframes as integer tables. Overlays reach x -40..200, y -24..176. */
#ifndef CAREOS_PIP_ART_H
#define CAREOS_PIP_ART_H
#include "pip_raster.h"

#define PIP_COLORS  8
#define PIP_HATS    9
#define PIP_OUTFITS 6
#define PIP_ACCS    4
#define PIP_EFFECTS 6

typedef enum {
    PM_IDLE = 0, PM_HAPPY, PM_SAD, PM_THINKING, PM_CONFUSED,
    PM_WATCHING, PM_PLAYING, PM_SLEEPING, PM_EXCITED, PM_COUNT
} pip_mood_t;

typedef struct { u8 color, hat, outfit, acc, effect; } pip_look_t;
typedef struct {
    u8   mood;
    u16  phase;          /* position in the mood's cycle, 0..65535 */
    bool still;          /* no animation */
    bool blink;          /* eyes shut for a blink (open-eyed moods) */
    i8   look_x, look_y; /* eye offset in design units (watching follows the cursor) */
} pip_pose_t;

const char *pip_color_label(u32 i);
const char *pip_hat_label(u32 i);
const char *pip_outfit_label(u32 i);
const char *pip_acc_label(u32 i);
const char *pip_effect_label(u32 i);
const char *pip_mood_label(u32 m);
u32  pip_mood_bulb(u32 m);        /* 0xRRGGBB */
u32  pip_mood_cycle_ms(u32 m);    /* one animation cycle */
u32  pip_color_base(u32 i);       /* 0xRRGGBB, for UI swatches */

void pip_art_draw(pr_canvas_t *c, pr_mat_t m, const pip_look_t *look, const pip_pose_t *pose);
u32  pip_art_errors(void);

#endif
