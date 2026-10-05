/* gui/pip/pip.h -- Pip at runtime: the signed-in user's pet, events in,
 * frames out. One Pip per session; guests get an unsaved one. */
#ifndef CAREOS_PIP_H
#define CAREOS_PIP_H
#include "kernel.h"
#include "gui.h"
#include "pip_brain.h"

void        pip_session_start(void);          /* after sign-in / unlock */
void        pip_session_end(void);            /* sign-out: save */
void        pip_tick(i32 mx, i32 my, u32 idle_ms);   /* every frame */
void        pip_event(pip_event_t ev);
bool        pip_script_mood(const char *name);       /* Care: pip_mood("happy") */
void        pip_say(const char *text, u32 ms);       /* speech bubble */
const char *pip_bubble_text(void);                   /* NULL when none */
pip_brain_t *pip_brain(void);
pip_mood_t  pip_mood(void);
void        pip_set_look(pip_look_t look);
void        pip_set_mode(pip_mode_t mode);
void        pip_mark_dirty(void);
bool        pip_saved(void);                         /* false for guests */
u32         pip_today(void);                         /* yyyymmdd (local) */
/* Draw Pip with the 160-unit box's top-left at (x, y), size px wide.
 * mood PM_COUNT = his current mood. still = no animation. */
void        pip_draw(i32 x, i32 y, u32 size, u32 mood, bool still);
/* Lock (kind 0) / power (kind 1) cameo inside r. */
void        pip_cameo(rect_t r, u32 kind);
/* Cursor position used for "watching", set by pip_desktop. */
void        pip_set_anchor(i32 cx, i32 cy);

#endif
