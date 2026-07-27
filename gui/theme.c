#include "kernel.h"
#include "gui.h"

static theme_t theme_dark = {
    .bg          = rgb(0x08, 0x0d, 0x17),
    .surface     = rgb(0x0f, 0x17, 0x26),
    .surface2    = rgb(0x16, 0x20, 0x33),
    .surface3    = rgb(0x1e, 0x2a, 0x42),
    .border      = rgb(0x2c, 0x3c, 0x58),
    .primary     = rgb(0x55, 0x9a, 0xff),
    .accent      = rgb(0x82, 0xbc, 0xff),
    .hover       = rgb(0x1c, 0x29, 0x42),
    .selection   = rgb(0x2a, 0x56, 0xb8),
    .text        = rgb(0xea, 0xf0, 0xff),
    .dim         = rgb(0x8a, 0x99, 0xba),
    .muted       = rgb(0x50, 0x5e, 0x78),
    .success     = rgb(0x2e, 0xcc, 0x8e),
    .warning     = rgb(0xf0, 0xb4, 0x30),
    .error       = rgb(0xf5, 0x60, 0x60),
    .info        = rgb(0x38, 0xbf, 0xf8),
    .taskbar     = rgb(0x08, 0x10, 0x1e),
    .winbar      = rgb(0x0d, 0x16, 0x28),
    .shadow      = rgb(0x00, 0x01, 0x04),
    .input_bg    = rgb(0x0a, 0x12, 0x20),
    .cursor      = rgb(0xff, 0xff, 0xff),
    .glass_tint  = rgb(0x44, 0x72, 0xc8),
    .glass_alpha = 30,
    .shadow_alpha = 160,
    .is_dark     = true
};

static theme_t theme_light = {
    .bg          = rgb(0xf1, 0xf5, 0xfb),
    .surface     = rgb(0xff, 0xff, 0xff),
    .surface2    = rgb(0xe8, 0xee, 0xf7),
    .surface3    = rgb(0xd7, 0xe2, 0xf0),
    .border      = rgb(0xc7, 0xd2, 0xe2),
    .primary     = rgb(0x2f, 0x6f, 0xc8),
    .accent      = rgb(0x4f, 0x8e, 0xe6),
    .hover       = rgb(0xe5, 0xec, 0xf6),
    .selection   = rgb(0x4f, 0x8e, 0xe6),
    .text        = rgb(0x1c, 0x26, 0x37),
    .dim         = rgb(0x5e, 0x6d, 0x83),
    .muted       = rgb(0x88, 0x97, 0xaa),
    .success     = rgb(0x16, 0xa3, 0x7a),
    .warning     = rgb(0xd9, 0x77, 0x06),
    .error       = rgb(0xdc, 0x4c, 0x64),
    .info        = rgb(0x25, 0x63, 0xeb),
    .taskbar     = rgb(0xf8, 0xfb, 0xff),
    .winbar      = rgb(0xf3, 0xf7, 0xfc),
    .shadow      = rgb(0x00, 0x00, 0x00),
    .input_bg    = rgb(0xf5, 0xf8, 0xfc),
    .cursor      = rgb(0x00, 0x00, 0x00),
    .glass_tint  = rgb(0xff, 0xff, 0xff),
    .glass_alpha = 132,
    .shadow_alpha = 72,
    .is_dark     = false
};

theme_t *g_theme = &theme_dark;

void theme_switch(bool dark) {
    g_theme = dark ? &theme_dark : &theme_light;
    /* The procedural wallpaper fallback is drawn from theme colours, so a theme
     * change must drop the cached backdrop. */
    gfx_wallpaper_cache_invalidate();
}

/* ── Accent colour engine ──────────────────────────────────────────────────
 * A single chosen accent drives primary + accent + selection across the whole
 * UI, because every surface already reads COL_PRIMARY / COL_ACCENT. We mutate
 * both theme structs so the choice survives a dark/light toggle. Each accent
 * carries its own dark- and light-theme triple so contrast stays right. */
typedef struct {
    const char *name;
    u32 d_primary, d_accent, d_selection;   /* dark theme */
    u32 l_primary, l_accent, l_selection;   /* light theme */
} accent_t;

static const accent_t ACCENTS[] = {
    { "Blue",   rgb(0x55,0x9a,0xff), rgb(0x82,0xbc,0xff), rgb(0x2a,0x56,0xb8),
                rgb(0x2f,0x6f,0xc8), rgb(0x4f,0x8e,0xe6), rgb(0x4f,0x8e,0xe6) },
    { "Green",  rgb(0x34,0xc7,0x59), rgb(0x6e,0xe7,0xa0), rgb(0x1f,0x8f,0x46),
                rgb(0x1a,0xa3,0x4a), rgb(0x34,0xc7,0x59), rgb(0x1a,0xa3,0x4a) },
    { "Orange", rgb(0xff,0x9f,0x0a), rgb(0xff,0xbf,0x60), rgb(0xc0,0x6f,0x00),
                rgb(0xe0,0x7a,0x00), rgb(0xff,0x9f,0x0a), rgb(0xe0,0x7a,0x00) },
    { "Purple", rgb(0xa8,0x55,0xf7), rgb(0xc9,0x9b,0xff), rgb(0x7c,0x3a,0xed),
                rgb(0x8b,0x3d,0xe0), rgb(0xa8,0x55,0xf7), rgb(0x8b,0x3d,0xe0) },
    { "Red",    rgb(0xff,0x5f,0x57), rgb(0xff,0x90,0x89), rgb(0xc0,0x39,0x2b),
                rgb(0xdc,0x4c,0x4c), rgb(0xff,0x5f,0x57), rgb(0xc0,0x39,0x2b) },
};
#define ACCENT_COUNT ((u32)(sizeof(ACCENTS) / sizeof(ACCENTS[0])))

static u32 g_accent_idx = 0;

void theme_set_accent(u32 idx) {
    if (idx >= ACCENT_COUNT) idx = 0;
    g_accent_idx = idx;
    const accent_t *a = &ACCENTS[idx];
    theme_dark.primary   = a->d_primary;
    theme_dark.accent    = a->d_accent;
    theme_dark.selection = a->d_selection;
    theme_light.primary   = a->l_primary;
    theme_light.accent    = a->l_accent;
    theme_light.selection = a->l_selection;
    gfx_wallpaper_cache_invalidate();
}

u32         theme_get_accent(void)        { return g_accent_idx; }
u32         theme_accent_count(void)       { return ACCENT_COUNT; }
const char *theme_accent_name(u32 idx)     { return ACCENTS[idx < ACCENT_COUNT ? idx : 0].name; }
u32         theme_accent_swatch(u32 idx)   { return ACCENTS[idx < ACCENT_COUNT ? idx : 0].d_primary; }
