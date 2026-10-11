/* gui/pip/pip_sprite.h -- cached Pip frames (kernel). */
#ifndef CAREOS_PIP_SPRITE_H
#define CAREOS_PIP_SPRITE_H
#include "kernel.h"
#include "pip_art.h"

#define PIP_FRAMES 12                       /* frames per mood cycle */

typedef struct { const u32 *px; i32 w, h; i32 ox, oy; } pip_frame_t;  /* ox,oy: design (0,0) in the sprite */

/* Straight-ARGB frame for this look/pose/size (size = width of the 160-unit box). */
bool pip_sprite_get(const pip_look_t *look, const pip_pose_t *pose, u32 size, pip_frame_t *out);
void pip_sprite_flush(void);

#endif
