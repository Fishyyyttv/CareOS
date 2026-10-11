/* gui/pip/pip_brain.h -- Pip's pet model (pure, host-tested).
 * Stats, the mood stack, time-away catch-up and the ~/.pip text format.
 * Time is in minutes since 2000-01-01 (from the RTC); event moods use the
 * millisecond uptime. */
#ifndef CAREOS_PIP_BRAIN_H
#define CAREOS_PIP_BRAIN_H
#include "pip_art.h"

typedef enum { PE_PAT = 0, PE_RUN_OK, PE_RUN_FAIL, PE_CONFUSED, PE_LOADING, PE_WELCOME_BACK, PE_SCRIPT,
               PE_GREET, PE_OPEN_HOME, PE_CLOSE_HOME, PE_ACTION, PE_THINK, PE_DONE, PE_OOPS } pip_event_t;
typedef enum { PIP_PERCH = 0, PIP_ROAM, PIP_NAP } pip_mode_t;

typedef struct {
    char       name[15];
    pip_look_t look;
    u8         happy, energy;        /* 0..100 */
    u16        friend_pts;
    u32        born_day;             /* yyyymmdd */
    u32        last_day;             /* yyyymmdd of the last day counted */
    u16        days;                 /* days together */
    u32        seen_min;             /* minutes since 2000 at the last advance */
    u32        hour_start_min;       /* pat-cap window */
    u8         pat_gain_hour;        /* happiness from pats in that window */
    u16        happy_carry, energy_carry;   /* minutes toward the next point */
    bool       roam, follow, nap;
    /* assistant settings (Settings > Pip), saved per user in ~/.pip */
    bool       enabled, show, anim, tips, intro_seen;
    u8         mode;                 /* pip_mode_t */
    /* not saved */
    u8         event_mood;
    u32        event_until_ms;
} pip_brain_t;

typedef struct { u32 idle_ms; u8 hour; bool cursor_near; } pip_ctx_t;

u32  pip_day_number(u32 yyyymmdd);
u32  pip_minutes(u32 yyyymmdd, u32 hour, u32 minute);

void pip_brain_defaults(pip_brain_t *b);   /* the Settings > Pip toggles and roam/follow/nap */
void pip_brain_new(pip_brain_t *b, u32 today, u32 now_min);
u32  pip_brain_signin(pip_brain_t *b, u32 today, u32 now_min);
void pip_brain_advance(pip_brain_t *b, u32 now_min);
void pip_brain_pat(pip_brain_t *b, u32 now_min);
void pip_brain_event(pip_brain_t *b, pip_event_t ev, u32 now_ms, u8 script_mood);
pip_mood_t pip_brain_mood(const pip_brain_t *b, const pip_ctx_t *c, u32 now_ms);
u32  pip_brain_level(const pip_brain_t *b);
u32  pip_brain_age_days(const pip_brain_t *b, u32 today);
u32  pip_brain_save(const pip_brain_t *b, char *out, u32 cap);
bool pip_brain_load(pip_brain_t *b, const char *text, u32 len);

#endif
