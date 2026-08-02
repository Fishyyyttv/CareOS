/* CareOS gui/font.c -- active font family and registry. */
#include "kernel.h"
#include "font.h"
#include "gui.h"

extern const font_family_t font_jetbrains_mono;
extern const font_family_t font_jetbrains_mono_bold;
extern const font_family_t font_classic;

static const font_family_t *registry[] = {
    &font_jetbrains_mono,
    &font_jetbrains_mono_bold,
    &font_classic,
};
#define REGISTRY_COUNT (sizeof(registry) / sizeof(registry[0]))

static u32 active_index = 0;

u32 font_registry_count(void) { return (u32)REGISTRY_COUNT; }
u32 font_active_index(void)   { return active_index; }

const char *font_registry_name(u32 index) {
    if (index >= REGISTRY_COUNT) return "";
    return registry[index]->name;
}

const font_face_t *font_face_at(u32 size_index) {
    if (size_index >= FONT_FACES) size_index = FONT_BODY;
    return &registry[active_index]->faces[size_index];
}

/* Bold always resolves to the Bold family regardless of the active choice, so
 * gfx_str_bold() renders real weight rather than a synthetic one. */
const font_face_t *font_bold_face_at(u32 size_index) {
    if (size_index >= FONT_FACES) size_index = FONT_BODY;
    return &font_jetbrains_mono_bold.faces[size_index];
}

void font_set_family(u32 index) {
    if (index >= REGISTRY_COUNT) return;
    active_index = index;
    const font_face_t *body = &registry[active_index]->faces[FONT_BODY];
    GFX_FONT_W = body->advance;
    GFX_FONT_H = body->line_h;
}

void font_init(void) {
    const careos_settings_t *s = settings_get();
    font_set_family(s->font_family < REGISTRY_COUNT ? s->font_family : 0u);
}
