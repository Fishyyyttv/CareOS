/* gui/pip/pip_sprite.c -- an LRU of rendered Pip frames, budgeted by bytes.
 * Key: look, mood, frame (phase / 12), blink, look offset, still, size. */
#include "pip_sprite.h"

#define PS_SLOTS  96
#define PS_BUDGET (6u * 1024u * 1024u)

typedef struct {
    u32 key_look, key_pose, size;
    u32 *px; i32 w, h;
    u32 used;
} ps_slot_t;

static ps_slot_t g_slots[PS_SLOTS];
static u32 g_bytes, g_clock;

static u32 look_key(const pip_look_t *l) {
    return (u32)l->color | ((u32)l->hat << 4) | ((u32)l->outfit << 8) | ((u32)l->acc << 12) | ((u32)l->effect << 16);
}
static u32 pose_key(const pip_pose_t *p) {
    u32 frame = p->still ? 0 : (u32)p->phase * PIP_FRAMES / 65536u;
    return (u32)p->mood | (frame << 4) | ((u32)p->blink << 9) | ((u32)p->still << 10) |
           ((u32)(u8)p->look_x << 12) | ((u32)(u8)p->look_y << 20);
}

static void evict_one(void) {
    u32 best = PS_SLOTS, oldest = 0xFFFFFFFFu;
    for (u32 i = 0; i < PS_SLOTS; i++)
        if (g_slots[i].px && g_slots[i].used < oldest) { oldest = g_slots[i].used; best = i; }
    if (best == PS_SLOTS) return;
    g_bytes -= (u32)(g_slots[best].w * g_slots[best].h * 4);
    kfree(g_slots[best].px);
    g_slots[best].px = NULL;
}

void pip_sprite_flush(void) {
    for (u32 i = 0; i < PS_SLOTS; i++) if (g_slots[i].px) { kfree(g_slots[i].px); g_slots[i].px = NULL; }
    g_bytes = 0;
}

bool pip_sprite_get(const pip_look_t *look, const pip_pose_t *pose, u32 size, pip_frame_t *out) {
    if (size < 16 || size > 400) return false;
    pip_pose_t q = *pose;
    if (!q.still) q.phase = (u16)(((u32)q.phase * PIP_FRAMES / 65536u) * 65536u / PIP_FRAMES);  /* snap */
    u32 lk = look_key(look), pk = pose_key(&q);
    i32 w = (i32)(size * 240 / 160), h = (i32)(size * 200 / 160);
    out->w = w; out->h = h; out->ox = (i32)(size * 40 / 160); out->oy = (i32)(size * 24 / 160);
    g_clock++;
    u32 free_slot = PS_SLOTS;
    for (u32 i = 0; i < PS_SLOTS; i++) {
        ps_slot_t *s = &g_slots[i];
        if (!s->px) { if (free_slot == PS_SLOTS) free_slot = i; continue; }
        if (s->key_look == lk && s->key_pose == pk && s->size == size) { s->used = g_clock; out->px = s->px; return true; }
    }
    u32 bytes = (u32)(w * h * 4);
    while (g_bytes + bytes > PS_BUDGET && g_bytes) evict_one();
    if (free_slot == PS_SLOTS) { evict_one(); for (u32 i = 0; i < PS_SLOTS; i++) if (!g_slots[i].px) { free_slot = i; break; } }
    if (free_slot == PS_SLOTS) return false;
    u32 *px = (u32 *)kmalloc(bytes);
    if (!px) return false;
    kmemset(px, 0, bytes);
    pr_canvas_t c = { px, w, h };
    i32 k = (i32)(size * 65536 / 160);
    pr_mat_t m = pr_mul(pr_scale(k, k), pr_translate(40 * 65536, 24 * 65536));
    pip_art_draw(&c, m, look, &q);
    pr_unpremultiply(&c);
    ps_slot_t *s = &g_slots[free_slot];
    s->key_look = lk; s->key_pose = pk; s->size = size; s->px = px; s->w = w; s->h = h; s->used = g_clock;
    g_bytes += bytes;
    out->px = px;
    return true;
}
