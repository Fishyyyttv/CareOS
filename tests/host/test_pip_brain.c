/* tests/host/test_pip_brain.c -- Pip's pet model. */
#include "pure.h"
#include "pip_brain.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define DAY 20261004u

static pip_ctx_t ctx(u32 idle_ms, u8 hour, bool near) { pip_ctx_t c = { idle_ms, hour, near }; return c; }

int main(void) {
    assert(pip_day_number(20000101) == 0 && pip_day_number(20000102) == 1);
    assert(pip_day_number(20010101) == 366);                       /* 2000 was a leap year */
    assert(pip_minutes(20000102, 1, 30) == 1440 + 90);
    u32 now = pip_minutes(DAY, 12, 0);

    pip_brain_t b;
    pip_brain_new(&b, DAY, now);
    assert(b.happy == 70 && b.energy == 80 && pip_brain_level(&b) == 1 && !strcmp(b.name, "Pip"));
    assert(b.roam && b.follow && b.nap && b.mode == PIP_PERCH);
    assert(pip_brain_age_days(&b, DAY) == 0 && pip_brain_age_days(&b, 20261007) == 3);

    /* happiness: -1 per 30 minutes */
    pip_brain_advance(&b, now + 60);
    assert(b.happy == 68);
    /* roaming costs energy, napping restores it */
    b.mode = PIP_ROAM;
    pip_brain_advance(&b, now + 80);
    assert(b.energy == 70);
    b.mode = PIP_NAP;
    pip_brain_advance(&b, now + 85);
    assert(b.energy == 80);
    b.mode = PIP_PERCH;

    /* pats: +8, capped at +24 per hour; friendship always grows */
    b.happy = 40;
    u32 f0 = b.friend_pts;
    for (u32 i = 0; i < 100; i++) pip_brain_pat(&b, now + 90);
    assert(b.happy == 64 && b.friend_pts == f0 + 100);
    pip_brain_pat(&b, now + 90 + 61);                              /* a new hour */
    assert(b.happy == 72);
    pip_brain_t lv = b; lv.friend_pts = 1000;
    assert(pip_brain_level(&lv) == 10);

    /* moods: an event beats the resting mood, then expires */
    pip_brain_t m;
    pip_brain_new(&m, DAY, now);
    assert(pip_brain_mood(&m, &(pip_ctx_t){0, 12, false}, 0) == PM_IDLE);
    pip_brain_event(&m, PE_RUN_FAIL, 1000, 0);
    assert(pip_brain_mood(&m, &(pip_ctx_t){0, 12, false}, 2000) == PM_SAD);
    assert(pip_brain_mood(&m, &(pip_ctx_t){0, 12, false}, 20000) == PM_IDLE);
    pip_brain_event(&m, PE_PAT, 30000, 0);
    assert(pip_brain_mood(&m, &(pip_ctx_t){0, 12, false}, 31000) == PM_HAPPY);
    pip_brain_event(&m, PE_SCRIPT, 40000, PM_CONFUSED);
    assert(pip_brain_mood(&m, &(pip_ctx_t){0, 12, false}, 41000) == PM_CONFUSED);
    /* resting rules, in priority order */
    pip_ctx_t c = ctx(0, 12, true);
    assert(pip_brain_mood(&m, &c, 100000) == PM_WATCHING);
    m.follow = false;
    assert(pip_brain_mood(&m, &c, 100000) == PM_IDLE);
    m.mode = PIP_ROAM;
    assert(pip_brain_mood(&m, &c, 100000) == PM_PLAYING);
    m.happy = 20;
    assert(pip_brain_mood(&m, &c, 100000) == PM_SAD);
    c = ctx(11 * 60000, 12, false);
    assert(pip_brain_mood(&m, &c, 100000) == PM_SLEEPING);         /* ten idle minutes, nap on */
    m.nap = false;
    assert(pip_brain_mood(&m, &c, 100000) == PM_SAD);
    c = ctx(3 * 60000, 23, false);
    assert(pip_brain_mood(&m, &c, 100000) == PM_SLEEPING);         /* late, two idle minutes */
    m.happy = 70; m.energy = 10;
    c = ctx(0, 12, false);
    assert(pip_brain_mood(&m, &c, 100000) == PM_SLEEPING);         /* worn out */
    m.energy = 80; m.mode = PIP_NAP;
    assert(pip_brain_mood(&m, &c, 100000) == PM_SLEEPING);

    /* time away: catch-up capped, happiness floored at 30, energy restored */
    pip_brain_t a;
    pip_brain_new(&a, DAY, now);
    a.happy = 90; a.energy = 20;
    u32 away = pip_brain_signin(&a, 20261020, pip_minutes(20261020, 9, 0));
    assert(away == 3 * 1440);                                      /* capped at 3 days */
    assert(a.happy == 30 && a.energy == 100 && a.days == 2);
    /* a low-happiness Pip isn't pushed lower than where he was */
    a.happy = 20;
    pip_brain_signin(&a, 20261021, pip_minutes(20261021, 9, 0));
    assert(a.happy == 20);
    /* the clock stepping backwards: no underflow, nothing lost */
    pip_brain_t back;
    pip_brain_new(&back, DAY, now);
    assert(pip_brain_signin(&back, DAY, now - 500) == 0 && back.happy == 70);
    pip_brain_advance(&back, now - 1000);
    assert(back.happy == 70);

    /* save / load round-trip */
    pip_brain_t s;
    pip_brain_new(&s, DAY, now);
    strcpy(s.name, "Biscuit");
    s.look.color = 3; s.look.hat = 2; s.look.outfit = 1; s.look.acc = 2; s.look.effect = 4;
    s.happy = 55; s.energy = 33; s.friend_pts = 77; s.days = 9; s.roam = false; s.mode = PIP_NAP;
    char buf[512];
    u32 n = pip_brain_save(&s, buf, sizeof buf);
    assert(n > 0 && !strncmp(buf, "PIP 1\n", 6));
    pip_brain_t r;
    assert(pip_brain_load(&r, buf, n));
    assert(!strcmp(r.name, "Biscuit") && r.look.color == 3 && r.look.hat == 2 && r.look.outfit == 1);
    assert(r.look.acc == 2 && r.look.effect == 4 && r.happy == 55 && r.energy == 33);
    assert(r.friend_pts == 77 && r.days == 9 && !r.roam && r.mode == PIP_NAP);
    assert(r.born_day == s.born_day && r.seen_min == s.seen_min);
    /* damaged or hostile files: fresh or clamped, never trusted blindly */
    assert(!pip_brain_load(&r, "garbage", 7));
    assert(!pip_brain_load(&r, "", 0));
    const char *wild = "PIP 1\nhappy=999\nenergy=-5\ncolor=77\nhat=200\nname=Abcdefghijklmnopqrstuvwxyz0123456789\n";
    assert(pip_brain_load(&r, wild, (u32)strlen(wild)));
    assert(r.happy == 100 && r.energy == 0 && r.look.color == 0 && r.look.hat == 0);
    assert(strlen(r.name) <= 14);
    const char *empty_name = "PIP 1\nname=\n";
    assert(pip_brain_load(&r, empty_name, (u32)strlen(empty_name)) && !strcmp(r.name, "Pip"));
    puts("pip_brain OK");
    return 0;
}
