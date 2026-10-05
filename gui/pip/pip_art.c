/* gui/pip/pip_art.c -- Pip as drawing data (pure). See pip_art.h. */
#include "pip_art.h"

#define U(x10) ((i32)(x10) * 6554)                 /* tenths of a unit -> Q16 (65536/10) */
#define FX(x)  ((i32)(x) * 65536)                  /* whole units -> Q16 */

static u32 g_errors;
u32 pip_art_errors(void) { return g_errors; }

/* ---- Labels and tables ---- */
static const char *const COLOR_L[PIP_COLORS]  = { "Lilac", "Coral", "Mint", "Sky", "Honey", "Bubblegum", "Midnight", "Snow" };
static const char *const HAT_L[PIP_HATS]      = { "None", "Beanie", "Crown", "Party", "Wizard", "Cap", "Bow", "Phones", "Sprout" };
static const char *const OUTFIT_L[PIP_OUTFITS] = { "None", "Scarf", "Bow tie", "Hoodie", "Cape", "Overalls" };
static const char *const ACC_L[PIP_ACCS]      = { "None", "Glasses", "Shades", "Star clip" };
static const char *const EFFECT_L[PIP_EFFECTS] = { "None", "Glow", "Sparkles", "Hearts", "Bubbles", "Rainbow" };
static const char *const MOOD_L[PM_COUNT]     = { "Chilling", "Happy", "Sad", "Thinking", "Confused", "Watching", "Playing", "Sleeping", "Excited" };

const char *pip_color_label(u32 i)  { return i < PIP_COLORS ? COLOR_L[i] : ""; }
const char *pip_hat_label(u32 i)    { return i < PIP_HATS ? HAT_L[i] : ""; }
const char *pip_outfit_label(u32 i) { return i < PIP_OUTFITS ? OUTFIT_L[i] : ""; }
const char *pip_acc_label(u32 i)    { return i < PIP_ACCS ? ACC_L[i] : ""; }
const char *pip_effect_label(u32 i) { return i < PIP_EFFECTS ? EFFECT_L[i] : ""; }
const char *pip_mood_label(u32 m)   { return m < PM_COUNT ? MOOD_L[m] : ""; }

/* base, light, dark, belly */
static const u32 COLORS[PIP_COLORS][4] = {
    { 0x9C8CFF, 0xCEC5FF, 0x6A57DE, 0xEEEAFF }, { 0xFF8A6B, 0xFFC0AD, 0xE0573A, 0xFFE7DF },
    { 0x5FD9A8, 0xA8F0D1, 0x25A574, 0xE4FAF0 }, { 0x5CC0F2, 0xA8E0FB, 0x2786C4, 0xE2F4FD },
    { 0xFFC24B, 0xFFE09A, 0xDD9112, 0xFFF4D6 }, { 0xFF8EC4, 0xFFC4E1, 0xDD4F94, 0xFFE8F3 },
    { 0x3E4A78, 0x6370A6, 0x222A4A, 0x8B96C4 }, { 0xECEFF8, 0xFFFFFF, 0xB8BFD6, 0xFFFFFF },
};
u32 pip_color_base(u32 i) { return COLORS[i < PIP_COLORS ? i : 0][0]; }

enum { E_OPEN, E_HAPPY, E_SAD, E_CLOSED, E_CONFUSED, E_STAR };
enum { MO_SMILE, MO_BIG, MO_FROWN, MO_O, MO_WAVY, MO_FLAT };
enum { A_BOB, A_BOUNCE, A_DROOP, A_TILT, A_WOBBLE, A_HOP, A_BREATHE, A_JUMP };
enum { SH_NONE, SH_BOB, SH_BOUNCE, SH_HOP, SH_JUMP };
enum { OV_NONE, OV_TEAR, OV_THINK, OV_Q, OV_PLAY, OV_Z, OV_BANG };

typedef struct {
    u8 eye, mouth, anim, shadow, ov;
    bool wave, pulse, feet, lookanim;
    u8 blush;            /* 0..255 */
    i8 look_x, look_y;
    u32 bulb;
    u16 cycle_ms;
} mood_t;

static const mood_t MOODS[PM_COUNT] = {
    /* idle     */ { E_OPEN,     MO_SMILE, A_BOB,     SH_BOB,    OV_NONE,  false, false, false, false, 255, 0, 0,  0x8C9BFF, 2600 },
    /* happy    */ { E_HAPPY,    MO_BIG,   A_BOUNCE,  SH_BOUNCE, OV_NONE,  true,  false, false, false, 255, 0, 0,  0xFF8EC4, 950 },
    /* sad      */ { E_SAD,      MO_FROWN, A_DROOP,   SH_NONE,   OV_TEAR,  false, false, false, false, 38,  0, 0,  0x6CB8FF, 3200 },
    /* thinking */ { E_OPEN,     MO_FLAT,  A_TILT,    SH_NONE,   OV_THINK, false, true,  false, false, 255, 4, -6, 0xFFC24B, 3000 },
    /* confused */ { E_CONFUSED, MO_WAVY,  A_WOBBLE,  SH_NONE,   OV_Q,     false, true,  false, false, 255, 0, 0,  0xC08BFF, 1800 },
    /* watching */ { E_OPEN,     MO_O,     A_BOB,     SH_BOB,    OV_NONE,  false, false, false, true,  255, 0, 0,  0x3DD68C, 3200 },
    /* playing  */ { E_HAPPY,    MO_BIG,   A_HOP,     SH_HOP,    OV_PLAY,  false, false, true,  false, 255, 0, 0,  0xFF7A59, 500 },
    /* sleeping */ { E_CLOSED,   MO_O,     A_BREATHE, SH_NONE,   OV_Z,     false, false, false, false, 255, 0, 0,  0x5A6385, 3400 },
    /* excited  */ { E_STAR,     MO_BIG,   A_JUMP,    SH_JUMP,   OV_BANG,  true,  false, false, false, 255, 0, 0,  0xFFC24B, 850 },
};
u32 pip_mood_bulb(u32 m) { return MOODS[m < PM_COUNT ? m : 0].bulb; }
u32 pip_mood_cycle_ms(u32 m) { return MOODS[m < PM_COUNT ? m : 0].cycle_ms; }

/* ---- Keyframes (CSS @keyframes as integers) ----
 * pct 0..100, dy in tenths of a unit, sx/sy in thousandths, rot in degrees. */
typedef struct { u8 pct; i16 dy; i16 sx, sy; i8 rot; } kf_t;
typedef struct { const kf_t *k; u8 n; } anim_t;
static const kf_t K_BOB[]     = { {0,0,1000,1000,0}, {50,-50,1010,990,0}, {100,0,1000,1000,0} };
static const kf_t K_BOUNCE[]  = { {0,0,1000,1000,0}, {15,0,1080,920,0}, {40,-200,940,1060,0}, {65,0,1060,940,0}, {82,-20,990,1010,0}, {100,0,1000,1000,0} };
static const kf_t K_DROOP[]   = { {0,0,1000,1000,0}, {50,50,1040,960,-2}, {100,0,1000,1000,0} };
static const kf_t K_TILT[]    = { {0,0,1000,1000,0}, {30,0,1000,1000,-6}, {70,-20,1000,1000,6}, {100,0,1000,1000,0} };
static const kf_t K_WOBBLE[]  = { {0,0,1000,1000,0}, {20,0,1000,1000,-8}, {40,0,1000,1000,6}, {60,0,1000,1000,-4}, {80,0,1000,1000,2}, {100,0,1000,1000,0} };
static const kf_t K_HOP[]     = { {0,0,1000,1000,0}, {25,-100,1000,1000,-4}, {50,-140,1000,1000,0}, {75,-60,1000,1000,4}, {100,0,1000,1000,0} };
static const kf_t K_BREATHE[] = { {0,0,1000,1000,0}, {50,0,1040,960,0}, {100,0,1000,1000,0} };
static const kf_t K_JUMP[]    = { {0,0,1000,1000,0}, {12,0,1120,860,0}, {42,-320,920,1080,0}, {70,0,1080,920,0}, {85,0,980,1020,0}, {100,0,1000,1000,0} };
static const anim_t ANIMS[] = {
    { K_BOB, 3 }, { K_BOUNCE, 6 }, { K_DROOP, 3 }, { K_TILT, 4 }, { K_WOBBLE, 6 }, { K_HOP, 5 }, { K_BREATHE, 3 }, { K_JUMP, 6 }
};
/* Shadow: dy = opacity in thousandths, sx = scale */
static const kf_t S_BOB[]    = { {0,1000,1000,0,0}, {50,1000,940,0,0}, {100,1000,1000,0,0} };
static const kf_t S_BOUNCE[] = { {0,1000,1000,0,0}, {15,1000,1080,0,0}, {40,500,700,0,0}, {65,1000,1060,0,0}, {100,1000,1000,0,0} };
static const kf_t S_HOP[]    = { {0,1000,1000,0,0}, {50,600,780,0,0}, {100,1000,1000,0,0} };
static const kf_t S_JUMP[]   = { {0,1000,1000,0,0}, {12,1000,1100,0,0}, {42,400,550,0,0}, {70,1000,1080,0,0}, {100,1000,1000,0,0} };
static const anim_t SHADOWS[] = { { 0, 0 }, { S_BOB, 3 }, { S_BOUNCE, 5 }, { S_HOP, 3 }, { S_JUMP, 5 } };

/* Smooth (ease-in-out) interpolation at phase 0..65535. */
static kf_t kf_at(const anim_t *a, u32 phase, bool still) {
    kf_t z = { 0, 0, 1000, 1000, 0 };
    if (!a->k || still) return a->k ? a->k[0] : z;
    u32 pct16 = phase * 100;                               /* pct in Q16 */
    for (u8 i = 1; i < a->n; i++) {
        u32 p0 = (u32)a->k[i - 1].pct << 16, p1 = (u32)a->k[i].pct << 16;
        if (pct16 > p1 && i + 1 < a->n) continue;
        i64 t = p1 > p0 ? (i64)(pct16 - p0) * 65536 / (p1 - p0) : 65536;
        if (t < 0) t = 0;
        if (t > 65536) t = 65536;
        t = (t * t >> 16) * (3 * 65536 - 2 * t) >> 16;     /* smoothstep */
        const kf_t *A = &a->k[i - 1], *B = &a->k[i];
        kf_t r;
        r.pct = 0;
        r.dy  = (i16)(A->dy + (((B->dy - A->dy) * t) >> 16));
        r.sx  = (i16)(A->sx + (((B->sx - A->sx) * t) >> 16));
        r.sy  = (i16)(A->sy + (((B->sy - A->sy) * t) >> 16));
        r.rot = (i8)(A->rot + (((B->rot - A->rot) * t) >> 16));
        return r;
    }
    return a->k[a->n - 1];
}

/* A triangle wave 0..65536..0 over the cycle, offset by off (0..65535). */
static u32 tri(u32 phase, u32 off) {
    u32 p = (phase + off) & 0xFFFF;
    return p < 32768 ? p * 2 : (65535 - p) * 2;
}

/* ---- Drawing helpers ---- */
static pr_canvas_t *C;
static u32 P_BASE, P_LIGHT, P_DARK, P_BELLY, P_INK, P_SHINE, P_BULB;

static u32 argb(u32 rgb, u32 a) { return (a << 24) | (rgb & 0xFFFFFF); }
static void ell(pr_mat_t m, i32 cx, i32 cy, i32 rx, i32 ry, i32 rot, u32 rgb, u32 op) {
    pr_paint_t p = pr_solid(argb(rgb, 255));
    pr_fill_ellipse(C, m, U(cx), U(cy), U(rx), U(ry), rot, &p, (u8)op);
}
static void ring(pr_mat_t m, i32 cx, i32 cy, i32 r, i32 w, u32 rgb, u32 op) {
    pr_paint_t p = pr_solid(argb(rgb, 255));
    pr_stroke_ellipse(C, m, U(cx), U(cy), U(r), U(r), 0, U(w), &p, (u8)op);
}
static void fill(pr_mat_t m, const char *d, u32 rgb, u32 op) {
    pr_paint_t p = pr_solid(argb(rgb, 255));
    if (!pr_fill_path(C, m, d, &p, (u8)op)) g_errors++;
}
static void stroke(pr_mat_t m, const char *d, i32 w10, u32 rgb, u32 op) {
    pr_paint_t p = pr_solid(argb(rgb, 255));
    if (!pr_stroke_path(C, m, d, U(w10), &p, (u8)op)) g_errors++;
}
static void rrect(pr_mat_t m, i32 x, i32 y, i32 w, i32 h, i32 r, u32 rgb, u32 op) {
    pr_paint_t p = pr_solid(argb(rgb, 255));
    pr_fill_rrect(C, m, U(x), U(y), U(w), U(h), U(r), &p, (u8)op);
}
static pr_paint_t radial(i32 cx, i32 cy, i32 r, u32 c0, u32 c1, u32 c2, u8 n, u16 mid) {
    pr_paint_t p = pr_solid(0);
    p.kind = PR_RADIAL;
    p.cx = cx; p.cy = cy; p.r = r; p.nstops = n;
    p.stops[0].pos = 0; p.stops[0].argb = c0;
    if (n == 2) { p.stops[1].pos = 65535; p.stops[1].argb = c1; }
    else { p.stops[1].pos = mid; p.stops[1].argb = c1; p.stops[2].pos = 65535; p.stops[2].argb = c2; }
    return p;
}
static u32 a255(u32 permille) { return permille * 255 / 1000; }

/* Bold glyph ('?', '!', 'z') as round strokes in a 10-unit box, h10 tall. */
static void glyph(pr_mat_t m, char g, i32 x10, i32 y10, i32 h10, u32 rgb, u32 op) {
    pr_mat_t gm = pr_mul(m, pr_mul(pr_translate(U(x10), U(y10)), pr_scale(h10 * 6554, h10 * 6554)));
    pr_mat_t unit = pr_scale(65536 / 10, 65536 / 10);
    pr_mat_t gmu = pr_mul(gm, unit);                       /* glyph coordinates are 0..10 */
    pr_paint_t p = pr_solid(argb(rgb, 255));
    if (g == '?') {
        pr_stroke_path(C, gmu, "M2 3 Q2 0 5 0 Q8 0 8 3 Q8 5 5 6 L5 7.5", FX(17) / 10, &p, (u8)op);
        pr_fill_ellipse(C, gmu, FX(5), FX(96) / 10, FX(1), FX(1), 0, &p, (u8)op);
    } else if (g == '!') {
        pr_stroke_path(C, gmu, "M5 0.5 L5 6.6", FX(2), &p, (u8)op);
        pr_fill_ellipse(C, gmu, FX(5), FX(96) / 10, FX(11) / 10, FX(11) / 10, 0, &p, (u8)op);
    } else {
        pr_stroke_path(C, gmu, "M1.5 2 L8.5 2 L1.5 9 L8.5 9", FX(16) / 10, &p, (u8)op);
    }
}

/* ---- Layers ---- */
static void layer_effects_back(pr_mat_t m, const pip_look_t *l, u32 ph) {
    if (l->effect == 1) {                                   /* glow */
        u32 s = 920 + tri(ph, 0) * 160 / 65536;             /* aura 0.92..1.08 */
        pr_mat_t gm = pr_mul(m, pr_about(pr_scale(s * 65536 / 1000, s * 65536 / 1000), FX(80), FX(92)));
        pr_paint_t g = radial(32768, 32768, 32768, argb(P_LIGHT, 230), argb(P_LIGHT, 140), argb(P_LIGHT, 0), 3, 40000);
        pr_fill_ellipse(C, gm, FX(80), FX(92), FX(74), FX(74), 0, &g, (u8)(140 + tri(ph, 0) * 100 / 65536));
    }
    if (l->effect == 5) {                                   /* rainbow */
        stroke(m, "M-34 118 Q16 58 58 96", 60, 0xFF5A5F, 217);
        stroke(m, "M-34 126 Q16 66 58 104", 60, 0xFFC24B, 217);
        stroke(m, "M-34 134 Q16 74 58 112", 60, 0x3DD68C, 217);
        stroke(m, "M-34 142 Q16 82 58 120", 60, 0x5CC0F2, 217);
        stroke(m, "M-34 150 Q16 90 58 128", 60, 0x9C8CFF, 217);
    }
}

static void layer_shadow(pr_mat_t m, const mood_t *md, u32 ph, bool still) {
    kf_t k = kf_at(&SHADOWS[md->shadow], ph, still);
    i32 s = (md->shadow ? k.sx : 1000) * 65536 / 1000;
    u32 op = md->shadow ? (u32)k.dy * 255 / 1000 : 255;
    pr_mat_t sm = pr_mul(m, pr_about(pr_scale(s, s), FX(80), FX(144)));
    pr_paint_t g = radial(32768, 32768, 32768, argb(0x0A0C1E, 82), argb(0x0A0C1E, 0), 0, 2, 0);
    pr_fill_ellipse(C, sm, FX(80), FX(144), FX(46), FX(8), 0, &g, (u8)op);
}

static void layer_outfit_back(pr_mat_t b, const pip_look_t *l) {
    if (l->outfit == 4) {                                   /* cape */
        fill(b, "M40 60 Q8 126 20 152 L140 152 Q152 126 120 60 Z", 0xE5303A, 255);
        stroke(b, "M40 60 Q16 120 26 148M120 60 Q144 120 134 148", 30, 0xB81F28, 255);
    }
}

static void layer_ears_feet_arms(pr_mat_t b, const mood_t *md, u32 ph, bool still) {
    ell(b, 460, 480, 110, 160, -26, P_BASE, 255);
    ell(b, 460, 500, 55, 95, -26, 0xFF9FB8, 153);
    ell(b, 1140, 480, 110, 160, 26, P_BASE, 255);
    ell(b, 1140, 500, 55, 95, 26, 0xFF9FB8, 153);
    i32 fa = 0, fb = 0;
    if (md->feet && !still) { fa = -(i32)tri(ph, 0) * 6 / 65536; fb = -(i32)tri(ph, 43690) * 6 / 65536; }
    pr_mat_t ma = pr_mul(b, pr_translate(0, FX(fa))), mb = pr_mul(b, pr_translate(0, FX(fb)));
    ell(ma, 580, 1430, 150, 85, 0, P_DARK, 255);
    ell(ma, 540, 1400, 60, 26, 0, 0xFFFFFF, 64);
    ell(mb, 1020, 1430, 150, 85, 0, P_DARK, 255);
    ell(mb, 980, 1400, 60, 26, 0, 0xFFFFFF, 64);
    ell(b, 240, 1080, 90, 130, 22, P_DARK, 255);
    i32 wave = (md->wave && !still) ? -(i32)tri(ph * 2 & 0xFFFF, 0) * 38 / 65536 : 0;
    pr_mat_t arm = pr_mul(b, pr_about(pr_rotate(wave), FX(130), FX(100)));
    ell(arm, 1360, 1080, 90, 130, -22, P_DARK, 255);
}

static void layer_body(pr_mat_t b, const mood_t *md) {
    static const char *BODY = "M80 36 C114 36 138 64 138 98 C138 128 114 144 80 144 C46 144 22 128 22 98 C22 64 46 36 80 36 Z";
    pr_paint_t g = radial(23593, 18350, 52429, argb(P_LIGHT, 255), argb(P_BASE, 255), argb(P_DARK, 255), 3, 32768);
    if (!pr_fill_path(C, b, BODY, &g, 255)) g_errors++;
    stroke(b, BODY, 20, P_DARK, 71);
    pr_paint_t bl = radial(32768, 26214, 39322, argb(P_BELLY, 242), argb(P_BELLY, 0), 0, 2, 0);
    pr_fill_ellipse(C, b, FX(80), FX(120), FX(40), FX(24), 0, &bl, 255);
    ell(b, 560, 580, 170, 80, -28, 0xFFFFFF, 128);
    ell(b, 740, 480, 30, 30, 0, 0xFFFFFF, 140);
    pr_paint_t ck = radial(32768, 32768, 32768, argb(0xFF6B8E, 191), argb(0xFF6B8E, 0), 0, 2, 0);
    pr_fill_ellipse(C, b, FX(49), FX(103), FX(11), FX(7), 0, &ck, md->blush);
    pr_fill_ellipse(C, b, FX(111), FX(103), FX(11), FX(7), 0, &ck, md->blush);
}

static void layer_outfit_front(pr_mat_t b, const pip_look_t *l) {
    switch (l->outfit) {
    case 1:
        fill(b, "M28 112 Q80 132 132 112 L133 125 Q80 145 27 125 Z", 0xFF5A5F, 255);
        stroke(b, "M28 112 Q80 132 132 112", 25, 0xFF8A8E, 255);
        fill(b, "M98 128 L110 154 L98 157 L90 132 Z", 0xE23D44, 255);
        stroke(b, "M96 139 L106 136M99 148 L108 145", 20, 0xFFFFFF, 255);
        break;
    case 2:
        fill(b, "M80 124 Q70 112 62 114 Q58 124 62 134 Q70 136 80 124 Z", 0x4F5EF2, 255);
        fill(b, "M80 124 Q90 112 98 114 Q102 124 98 134 Q90 136 80 124 Z", 0x4F5EF2, 255);
        ell(b, 800, 1240, 55, 55, 0, 0x3542C9, 255);
        stroke(b, "M66 120 L70 122M94 120 L90 122", 20, 0x8C9BFF, 255);
        break;
    case 3:
        fill(b, "M25 108 Q80 130 135 108 Q140 134 114 144 L46 144 Q20 134 25 108 Z", 0x3A4570, 255);
        stroke(b, "M25 108 Q80 130 135 108", 30, 0x5A6799, 255);
        fill(b, "M62 129 L98 129 L94 141 L66 141 Z", 0x2B3458, 255);
        stroke(b, "M72 120 L70 131M88 120 L90 131", 20, 0xFFFFFF, 255);
        ell(b, 700, 1320, 20, 20, 0, 0xFFFFFF, 255);
        ell(b, 900, 1320, 20, 20, 0, 0xFFFFFF, 255);
        break;
    case 5:
        stroke(b, "M62 104 L48 74M98 104 L112 74", 50, 0x4C7DFF, 255);
        fill(b, "M60 110 L100 110 L100 118 L124 118 L128 136 Q80 152 32 136 L36 118 L60 118 Z", 0x4C7DFF, 255);
        rrect(b, 710, 1160, 180, 110, 30, 0x3A66E0, 255);
        ell(b, 640, 1140, 28, 28, 0, 0xFFC24B, 255);
        ell(b, 960, 1140, 28, 28, 0, 0xFFC24B, 255);
        break;
    default: break;
    }
}

static void layer_antenna(pr_mat_t b, const pip_look_t *l, const mood_t *md, u32 ph, bool still) {
    if (!(l->hat == 0 || l->hat == 6 || l->hat == 7)) return;  /* none, bow, headphones */
    stroke(b, "M80 38 Q75 27 83 17", 35, P_DARK, 255);
    u32 hs = still ? 1000 : 1000 + (ph & 0xFFFF) * 700 / 65536;  /* halo 1..1.7, fading */
    u32 hop = still ? 89 : 89 - (ph & 0xFFFF) * 89 / 65536;
    pr_mat_t hm = pr_mul(b, pr_about(pr_scale(hs * 65536 / 1000, hs * 65536 / 1000), FX(84), FX(14)));
    ell(hm, 840, 140, 110, 110, 0, P_BULB, hop);
    u32 bop = (md->pulse && !still) ? 255 - tri(ph * 4 & 0xFFFF, 0) * 178 / 65536 : 255;
    ell(b, 840, 140, 80, 80, 0, P_BULB, bop);
    ring(b, 840, 140, 80, 15, 0xFFFFFF, 89 * bop / 255);
    ell(b, 815, 115, 28, 28, 0, 0xFFFFFF, 230 * bop / 255);
}

static void layer_eyes(pr_mat_t b, const mood_t *md, const pip_pose_t *pose) {
    i32 lx = pose->look_x, ly = pose->look_y;
    if (!lx && !ly) { lx = md->look_x; ly = md->look_y; }
    if (md->lookanim && !pose->still && !pose->look_x && !pose->look_y) {
        u32 p = pose->phase;                                    /* pip-look: -6 at 25 %, +6 at 75 % */
        lx = p < 32768 ? -(i32)tri(p * 2 & 0xFFFF, 0) * 6 / 65536 : (i32)tri(p * 2 & 0xFFFF, 0) * 6 / 65536;
    }
    pr_mat_t e = pr_mul(b, pr_translate(FX(lx), FX(ly)));
    if (pose->blink && md->eye == E_OPEN) e = pr_mul(e, pr_about(pr_scale(65536, 6554), FX(80), FX(88)));
    switch (md->eye) {
    case E_OPEN:
        ell(e, 620, 880, 100, 125, 0, P_INK, 255); ell(e, 980, 880, 100, 125, 0, P_INK, 255);
        ell(e, 620, 940, 60, 40, 0, P_LIGHT, 140); ell(e, 980, 940, 60, 40, 0, P_LIGHT, 140);
        ell(e, 655, 825, 40, 40, 0, P_SHINE, 255); ell(e, 1015, 825, 40, 40, 0, P_SHINE, 255);
        ell(e, 585, 925, 18, 18, 0, P_SHINE, 217); ell(e, 945, 925, 18, 18, 0, P_SHINE, 217);
        break;
    case E_HAPPY:
        stroke(e, "M52 92 Q62 78 72 92M88 92 Q98 78 108 92", 50, P_INK, 255);
        break;
    case E_SAD:
        ell(e, 620, 920, 85, 95, 0, P_INK, 255); ell(e, 980, 920, 85, 95, 0, P_INK, 255);
        ell(e, 650, 880, 30, 30, 0, P_SHINE, 255); ell(e, 1010, 880, 30, 30, 0, P_SHINE, 255);
        ell(e, 620, 970, 50, 25, 0, 0x9ADBFA, 179); ell(e, 980, 970, 50, 25, 0, 0x9ADBFA, 179);
        stroke(e, "M50 77 Q59 74 70 81M110 77 Q101 74 90 81", 35, P_INK, 255);
        break;
    case E_CLOSED:
        stroke(e, "M52 90 Q62 98 72 90M88 90 Q98 98 108 90", 45, P_INK, 255);
        stroke(e, "M52 90 L48 87M108 90 L112 87", 30, P_INK, 255);
        break;
    case E_CONFUSED:
        ell(e, 620, 880, 110, 135, 0, P_INK, 255);
        ell(e, 660, 820, 42, 42, 0, P_SHINE, 255);
        ell(e, 580, 930, 18, 18, 0, P_SHINE, 217);
        stroke(e, "M89 91 L108 87", 50, P_INK, 255);
        stroke(e, "M49 70 Q62 61 75 69", 35, P_INK, 255);
        break;
    default: /* star */
        fill(e, "M62 74 Q64.8 85.2 76 88 Q64.8 90.8 62 102 Q59.2 90.8 48 88 Q59.2 85.2 62 74 Z", 0xFFC24B, 255);
        stroke(e, "M62 74 Q64.8 85.2 76 88 Q64.8 90.8 62 102 Q59.2 90.8 48 88 Q59.2 85.2 62 74 Z", 22, P_INK, 255);
        fill(e, "M98 74 Q100.8 85.2 112 88 Q100.8 90.8 98 102 Q95.2 90.8 84 88 Q95.2 85.2 98 74 Z", 0xFFC24B, 255);
        stroke(e, "M98 74 Q100.8 85.2 112 88 Q100.8 90.8 98 102 Q95.2 90.8 84 88 Q95.2 85.2 98 74 Z", 22, P_INK, 255);
        ell(e, 590, 840, 20, 20, 0, 0xFFFFFF, 255); ell(e, 950, 840, 20, 20, 0, 0xFFFFFF, 255);
        break;
    }
}

static void layer_mouth(pr_mat_t b, const mood_t *md) {
    switch (md->mouth) {
    case MO_SMILE: stroke(b, "M71 106 Q75.5 112 80 107 Q84.5 112 89 106", 35, P_INK, 255); break;
    case MO_BIG:
        fill(b, "M68 104 Q80 124 92 104 Z", P_INK, 255);
        stroke(b, "M68 104 Q80 124 92 104 Z", 25, P_INK, 255);
        fill(b, "M73.5 113.5 Q80 108.5 86.5 113.5 Q80 119 73.5 113.5 Z", 0xFF6B8E, 255);
        break;
    case MO_FROWN: stroke(b, "M72 113 Q80 105 88 113", 35, P_INK, 255); break;
    case MO_O:     ell(b, 800, 1100, 45, 55, 0, P_INK, 255); break;
    case MO_WAVY:  stroke(b, "M70 110 Q74 105 78 110 Q82 115 86 110 Q88 108 90 108", 35, P_INK, 255); break;
    default:       stroke(b, "M74 109 Q80 107 87 108", 35, P_INK, 255); break;
    }
}

static void layer_acc(pr_mat_t b, const pip_look_t *l) {
    if (l->acc == 1) {
        pr_paint_t g = pr_solid(argb(0xFFFFFF, 46));
        pr_fill_ellipse(C, b, FX(62), FX(88), FX(14), FX(14), 0, &g, 255);
        pr_fill_ellipse(C, b, FX(98), FX(88), FX(14), FX(14), 0, &g, 255);
        ring(b, 620, 880, 140, 30, 0x2A2F45, 255);
        ring(b, 980, 880, 140, 30, 0x2A2F45, 255);
        stroke(b, "M76 87 Q80 83 84 87M48 86 L34 82M112 86 L126 82", 30, 0x2A2F45, 255);
        stroke(b, "M54 80 L58 77M90 80 L94 77", 20, 0xFFFFFF, 179);
    } else if (l->acc == 2) {
        fill(b, "M45 81 L77 81 L75 95 Q62 103 48 95 Z", 0x14121F, 255);
        fill(b, "M83 81 L115 81 L112 95 Q98 103 85 95 Z", 0x14121F, 255);
        stroke(b, "M77 83 L83 83M45 82 L34 79M115 82 L126 79", 30, 0x14121F, 255);
        stroke(b, "M51 85 L59 85M89 85 L97 85", 24, 0xFFFFFF, 179);
    } else if (l->acc == 3) {
        static const char *STAR = "M116 48 L119 55 L127 56 L121 61 L123 69 L116 65 L109 69 L111 61 L105 56 L113 55 Z";
        fill(b, STAR, 0xFFC24B, 255);
        stroke(b, STAR, 15, 0xD98E12, 255);
    }
}

static void layer_hat(pr_mat_t b, const pip_look_t *l) {
    switch (l->hat) {
    case 1:
        fill(b, "M42 56 Q44 18 80 16 Q116 18 118 56 Z", 0xFF7A59, 255);
        stroke(b, "M60 22 L58 52M80 17 L80 52M100 22 L102 52", 25, 0xE5603F, 255);
        rrect(b, 380, 490, 840, 140, 70, 0xE5603F, 255);
        ell(b, 800, 130, 95, 95, 0, 0xFFF1E8, 255);
        ell(b, 770, 100, 30, 30, 0, 0xFFFFFF, 255);
        break;
    case 2: {
        static const char *CROWN = "M52 48 L55 18 L68 33 L80 12 L92 33 L105 18 L108 48 Z";
        fill(b, CROWN, 0xFFC24B, 255);
        stroke(b, CROWN, 20, 0xD98E12, 255);
        rrect(b, 510, 420, 580, 80, 30, 0xF5AE2E, 255);
        ell(b, 800, 380, 45, 45, 0, 0xFF5A7A, 255);
        ell(b, 640, 400, 30, 30, 0, 0x5CC0F2, 255);
        ell(b, 960, 400, 30, 30, 0, 0x3DD68C, 255);
        ell(b, 550, 180, 30, 30, 0, 0xFFE08A, 255);
        ell(b, 800, 120, 30, 30, 0, 0xFFE08A, 255);
        ell(b, 1050, 180, 30, 30, 0, 0xFFE08A, 255);
        break;
    }
    case 3: {
        pr_mat_t g = pr_mul(b, pr_about(pr_rotate(14), FX(80), FX(44)));
        fill(g, "M63 48 L80 0 L97 48 Z", 0x8C9BFF, 255);
        stroke(g, "M70 30 L90 30M66 42 L94 42", 30, 0xFFFFFF, 179);
        ell(g, 760, 220, 26, 26, 0, 0xFFC24B, 255);
        ell(g, 860, 360, 26, 26, 0, 0xFF8EC4, 255);
        ell(g, 800, 0, 70, 70, 0, 0xFF8EC4, 255);
        break;
    }
    case 4:
        fill(b, "M56 48 Q70 28 72 2 Q86 -6 100 10 Q90 18 104 48 Z", 0x6A55D9, 255);
        ell(b, 800, 480, 460, 85, 0, 0x4B3AA8, 255);
        fill(b, "M86 22 L88 27 L93 27.5 L89 31 L90.5 36 L86 33.5 L81.5 36 L83 31 L79 27.5 L84 27 Z", 0xFFC24B, 255);
        ell(b, 720, 380, 20, 20, 0, 0xFFE08A, 255);
        ell(b, 950, 400, 16, 16, 0, 0xFFE08A, 255);
        break;
    case 5:
        fill(b, "M46 54 Q46 20 80 20 Q114 20 114 54 Z", 0x3CA0E8, 255);
        fill(b, "M102 50 Q132 46 142 56 Q126 61 104 56 Z", 0x2A7CC0, 255);
        ell(b, 800, 210, 35, 35, 0, 0x2A7CC0, 255);
        stroke(b, "M60 36 Q68 27 80 26", 30, 0xFFFFFF, 128);
        break;
    case 6:
        fill(b, "M104 44 Q88 24 82 40 Q84 58 104 44 Z", 0xFF5A8E, 255);
        fill(b, "M104 44 Q120 24 126 40 Q124 58 104 44 Z", 0xFF5A8E, 255);
        stroke(b, "M90 36 Q92 42 98 43M118 36 Q116 42 110 43", 20, 0xFF9FBF, 255);
        ell(b, 1040, 440, 60, 60, 0, 0xE23D74, 255);
        break;
    case 7:
        stroke(b, "M28 96 Q26 30 80 28 Q134 30 132 96", 70, 0x2A2F45, 255);
        rrect(b, 150, 840, 220, 320, 100, 0xFF7A59, 255);
        rrect(b, 1230, 840, 220, 320, 100, 0xFF7A59, 255);
        rrect(b, 190, 900, 60, 180, 30, 0xFFB49E, 255);
        rrect(b, 1270, 900, 60, 180, 30, 0xFFB49E, 255);
        break;
    case 8:
        stroke(b, "M80 38 Q79 28 81 18", 35, 0x2FAE7D, 255);
        fill(b, "M80 25 Q66 10 56 20 Q68 32 80 25 Z", 0x5FD9A8, 255);
        fill(b, "M81 20 Q92 2 106 10 Q96 26 81 20 Z", 0x3DD68C, 255);
        stroke(b, "M68 21 L76 24M94 12 L86 18", 15, 0x2FAE7D, 255);
        break;
    default: break;
    }
}

static void layer_overlay(pr_mat_t m, pr_mat_t b, const mood_t *md, u32 ph, bool still) {
    switch (md->ov) {
    case OV_TEAR: {
        i32 dy = still ? 0 : (i32)(ph * 14 / 65536);
        u32 op = still ? 255 : (ph < 13107 ? ph * 255 / 13107 : 255 - (ph - 13107) * 255 / 52429);
        fill(pr_mul(b, pr_translate(0, FX(dy))), "M55 100 Q50 109 55 112 Q60 109 55 100 Z", 0x6CB8FF, op);
        fill(m, "M112 26 Q112 16 122 16 Q126 8 134 12 Q144 10 144 20 Q150 26 142 30 L116 30 Q108 30 112 26 Z", 0x9AA3BD, 255);
        stroke(m, "M118 36 L116 42M128 36 L126 44M138 36 L136 42", 25, 0x6CB8FF, 255);
        break;
    }
    case OV_THINK:
        ell(m, 1215, 535, 35, 35, 0, 0xFFFFFF, 255);
        ell(m, 1315, 415, 55, 55, 0, 0xFFFFFF, 255);
        rrect(m, 1260, -60, 560, 360, 180, 0xFFFFFF, 255);
        for (u32 i = 0; i < 3; i++) {
            u32 op = still ? 255 : 255 - tri(ph * 3 & 0xFFFF, i * 13107) * 178 / 65536;
            ell(m, 1430 + (i32)i * 110, 120, 30, 30, 0, 0x6E5CE0, op);
        }
        break;
    case OV_Q: {
        kf_t k = kf_at(&ANIMS[A_WOBBLE], ph, still);
        pr_mat_t q = pr_mul(m, pr_about(pr_rotate(k.rot), FX(130), FX(30)));
        glyph(q, '?', 1220, 70, 245, 0xA97CFF, 255);
        glyph(m, '?', 1420, 300, 145, 0xC08BFF, 204);
        break;
    }
    case OV_PLAY:
        stroke(m, "M0 90 L14 90M-8 104 L12 104M0 118 L16 118", 40, P_DARK, 115);
        ell(m, 180, 1480, 60, 60, 0, P_LIGHT, 153);
        ell(m, 60, 1440, 40, 40, 0, P_LIGHT, 102);
        break;
    case OV_Z: {
        static const i32 ZX[3] = { 1120, 1260, 1420 }, ZY[3] = { 400, 260, 70 }, ZH[3] = { 130, 190, 230 };
        static const u32 ZC[3] = { 0x8C9BFF, 0x8C9BFF, 0xB7C1FF };
        for (u32 i = 0; i < 3; i++) {
            u32 p = (ph + i * 21845) & 0xFFFF;               /* staggered by a third */
            i32 dy = still ? 0 : 80 - (i32)(p * 360 / 65536);
            u32 op = still ? 255 : (p < 13107 ? p * 255 / 13107 : 255 - (p - 13107) * 255 / 52429);
            glyph(m, 'z', ZX[i], ZY[i] + dy, ZH[i], ZC[i], op);
        }
        break;
    }
    case OV_BANG: {
        kf_t k = kf_at(&ANIMS[A_HOP], ph, still);
        glyph(pr_mul(m, pr_translate(0, U(k.dy))), '!', 1240, 40, 275, 0xFF7A59, 255);
        break;
    }
    default: break;
    }
}

static void layer_effects_front(pr_mat_t m, const pip_look_t *l, u32 mood, u32 ph, bool still) {
    if (l->effect == 2) {                                   /* sparkles, twinkling */
        static const char *SP[4] = {
            "M20 30 Q21 39 30 40 Q21 41 20 50 Q19 41 10 40 Q19 39 20 30 Z",
            "M148 62 Q149 69 156 70 Q149 71 148 78 Q147 71 140 70 Q147 69 148 62 Z",
            "M142 122 Q143 129 150 130 Q143 131 142 138 Q141 131 134 130 Q141 129 142 122 Z",
            "M10 106 Q11 111 16 112 Q11 113 10 118 Q9 113 4 112 Q9 111 10 106 Z" };
        static const i32 SX[4] = { 20, 148, 142, 10 }, SY[4] = { 40, 70, 130, 112 };
        static const u32 SC[4] = { 0xFFE08A, 0xFFFFFF, 0xFFE08A, 0xFFFFFF };
        for (u32 i = 0; i < 4; i++) {
            u32 t = still ? 65536 : tri(ph, i * 16384);
            i32 s = (400 + (i32)(t * 600 / 65536)) * 65536 / 1000;
            u32 op = 64 + t * 191 / 65536;
            fill(pr_mul(m, pr_about(pr_scale(s, s), FX(SX[i]), FX(SY[i]))), SP[i], SC[i], op);
        }
    }
    if (l->effect == 3 || mood == PM_HAPPY) {
        fill(m, "M28 50 C28 44 36 42 38 48 C40 42 48 44 48 50 C48 56 38 62 38 62 C38 62 28 56 28 50 Z", 0xFF6B8E, 255);
        fill(m, "M122 40 C122 35 129 33 130 38 C132 33 139 35 139 40 C139 45 130 50 130 50 C130 50 122 45 122 40 Z", 0xFF8EC4, 255);
    }
    if (l->effect == 4) {
        static const i32 BX[3] = { 220, 1400, 1300 }, BY[3] = { 700, 600, 1000 }, BR[3] = { 70, 90, 50 };
        for (u32 i = 0; i < 3; i++) {
            i32 dy = still ? 0 : -(i32)(tri(ph, i * 21845) * 6 / 65536);
            pr_mat_t bm = pr_mul(m, pr_translate(0, FX(dy)));
            pr_paint_t g = pr_solid(argb(0x9ADBFA, 89));
            pr_fill_ellipse(C, bm, U(BX[i]), U(BY[i]), U(BR[i]), U(BR[i]), 0, &g, 255);
            ring(bm, BX[i], BY[i], BR[i], 15, 0x9ADBFA, 255);
            if (i < 2) ell(bm, BX[i] - 25, BY[i] - 25, 22, 22, 0, 0xFFFFFF, 204);
        }
    }
    (void)a255;
}

void pip_art_draw(pr_canvas_t *c, pr_mat_t m, const pip_look_t *look, const pip_pose_t *pose) {
    if (!c || !look || !pose) return;
    C = c;
    pip_look_t l = *look;
    if (l.color >= PIP_COLORS) l.color = 0;
    if (l.hat >= PIP_HATS) l.hat = 0;
    if (l.outfit >= PIP_OUTFITS) l.outfit = 0;
    if (l.acc >= PIP_ACCS) l.acc = 0;
    if (l.effect >= PIP_EFFECTS) l.effect = 0;
    u32 mood = pose->mood < PM_COUNT ? pose->mood : PM_IDLE;
    const mood_t *md = &MOODS[mood];
    P_BASE = COLORS[l.color][0]; P_LIGHT = COLORS[l.color][1]; P_DARK = COLORS[l.color][2]; P_BELLY = COLORS[l.color][3];
    P_INK = l.color == 6 ? 0xF4F1FF : 0x1A1630;
    P_SHINE = l.color == 6 ? 0x1A1630 : 0xFFFFFF;
    P_BULB = md->bulb;
    bool still = pose->still;
    u32 ph = still ? 0 : pose->phase;

    kf_t k = kf_at(&ANIMS[md->anim], ph, still);
    pr_mat_t bx = pr_mul(pr_translate(0, U(k.dy)),
                  pr_mul(pr_scale(k.sx * 65536 / 1000, k.sy * 65536 / 1000), pr_rotate(k.rot)));
    pr_mat_t b = pr_mul(m, pr_about(bx, FX(80), FX(144)));

    layer_effects_back(m, &l, ph);
    if (md->ov == OV_PLAY) layer_overlay(m, b, md, ph, still);
    layer_shadow(m, md, ph, still);
    layer_outfit_back(b, &l);
    layer_ears_feet_arms(b, md, ph, still);
    layer_body(b, md);
    layer_outfit_front(b, &l);
    layer_antenna(b, &l, md, ph, still);
    layer_eyes(b, md, pose);
    layer_mouth(b, md);
    layer_acc(b, &l);
    layer_hat(b, &l);
    if (md->ov != OV_PLAY) layer_overlay(m, b, md, ph, still);
    layer_effects_front(m, &l, mood, ph, still);
}
