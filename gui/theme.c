#include "kernel.h"
#include "gui.h"
#include "window_chrome.h"

/* CareOS v10 Terminal / Settings artboards. Kept separate from legacy app
 * colours until the shared v10 token migration lands. Hairlines are already
 * composited over the opaque fallback, avoiding per-frame backdrop blur. */
static const wm_chrome_palette_t chrome_dark = {
    .body = 0x0D1120, .bar = 0x151B2E, .border = 0x2D3243,
    .divider = 0x23293B, .title = 0xEEF1FA, .muted = 0x9AA3BD,
    .inactive = 0x222A48, .close = 0xFF5F57, .minimize = 0xF5B83D,
    .maximize = 0x3DD68C, .glyph = 0x2A120A
};
static const wm_chrome_palette_t chrome_light = {
    .body = 0xFFFFFF, .bar = 0xFAFAFD, .border = 0xE3E3E8,
    .divider = 0xECECEF, .title = 0x161A2B, .muted = 0x5A6178,
    .inactive = 0xD5DAE8, .close = 0xFF5F57, .minimize = 0xF5B83D,
    .maximize = 0x3DD68C, .glyph = 0x2A120A
};

const wm_chrome_palette_t *theme_window_chrome(void) {
    return g_theme->is_dark ? &chrome_dark : &chrome_light;
}

/* Legacy fields are mapped onto the v10 tokens (spec §1) so every app that
 * still reads COL_* takes on the canvas palette before its own restyle.
 * border = the hairline composited over ground; primary/accent/selection are
 * owned by the accent engine below. */
static theme_t theme_dark = {
    .bg          = rgb(0x0B, 0x10, 0x20),
    .surface     = rgb(0x15, 0x1B, 0x2E),
    .surface2    = rgb(0x1C, 0x24, 0x40),
    .surface3    = rgb(0x22, 0x2A, 0x48),
    .border      = rgb(0x23, 0x28, 0x36),
    .primary     = rgb(0xFF, 0x7A, 0x59),
    .accent      = rgb(0xFF, 0x9A, 0x76),
    .hover       = rgb(0x1C, 0x24, 0x40),
    .selection   = rgb(0x9C, 0x3F, 0x27),
    .text        = rgb(0xEE, 0xF1, 0xFA),
    .dim         = rgb(0xB4, 0xBB, 0xD2),
    .muted       = rgb(0x9A, 0xA3, 0xBD),
    .success     = rgb(0x3D, 0xD6, 0x8C),
    .warning     = rgb(0xF5, 0xB8, 0x3D),
    .error       = rgb(0xFF, 0x5F, 0x57),
    .info        = rgb(0x8C, 0x9B, 0xFF),
    .taskbar     = rgb(0x12, 0x17, 0x2C),
    .winbar      = rgb(0x15, 0x1B, 0x2E),
    .shadow      = rgb(0x00, 0x00, 0x00),
    .input_bg    = rgb(0x16, 0x1C, 0x33),
    .cursor      = rgb(0xff, 0xff, 0xff),
    .glass_tint  = rgb(0x15, 0x1B, 0x2E),
    .glass_alpha = 184,
    .shadow_alpha = 140,
    .ground = rgb(0x0B,0x10,0x20), .surface_alt = rgb(0x16,0x1C,0x33),
    .raised = rgb(0x1C,0x24,0x40), .raised2 = rgb(0x22,0x2A,0x48), .track = rgb(0x2E,0x37,0x57),
    .ink = rgb(0xEE,0xF1,0xFA), .ink2 = rgb(0xB4,0xBB,0xD2),
    .care = rgb(0xFF,0x7A,0x59), .on_care = rgb(0x2A,0x12,0x0A), .care_soft = rgb(0xFF,0x9A,0x76),
    .calm = rgb(0x8C,0x9B,0xFF),
    .hairline = rgb(0xFF,0xFF,0xFF), .hairline_alpha = 26,
    .menubar_tint = rgb(0x0A,0x0E,0x1E), .menubar_alpha = 158,
    .dock_tint = rgb(0x12,0x17,0x2C), .dock_alpha = 168,
    .panel_tint = rgb(0x15,0x1B,0x2E), .panel_alpha = 184,
    .window_body = rgb(0x0D,0x11,0x20), .window_alpha = 235,
    .term_green = rgb(0x7C,0xF0,0xB5),
    .is_dark     = true
};

static theme_t theme_light = {
    .bg          = rgb(0xE9, 0xEC, 0xF7),
    .surface     = rgb(0xFF, 0xFF, 0xFF),
    .surface2    = rgb(0xF5, 0xF6, 0xFA),
    .surface3    = rgb(0xE1, 0xE4, 0xEE),
    .border      = rgb(0xD4, 0xD7, 0xE3),
    .primary     = rgb(0xFF, 0x7A, 0x59),
    .accent      = rgb(0xE5, 0x53, 0x2E),
    .hover       = rgb(0xE1, 0xE4, 0xEE),
    .selection   = rgb(0xFF, 0xB4, 0x9E),
    .text        = rgb(0x16, 0x1A, 0x2B),
    .dim         = rgb(0x4A, 0x51, 0x68),
    .muted       = rgb(0x5A, 0x61, 0x78),
    .success     = rgb(0x1F, 0xA5, 0x62),
    .warning     = rgb(0xD9, 0x8A, 0x06),
    .error       = rgb(0xE5, 0x48, 0x4D),
    .info        = rgb(0x4F, 0x5E, 0xF2),
    .taskbar     = rgb(0xFF, 0xFF, 0xFF),
    .winbar      = rgb(0xFA, 0xFA, 0xFD),
    .shadow      = rgb(0x28, 0x32, 0x6E),
    .input_bg    = rgb(0xF5, 0xF6, 0xFA),
    .cursor      = rgb(0x00, 0x00, 0x00),
    .glass_tint  = rgb(0xFA, 0xFA, 0xFD),
    .glass_alpha = 199,
    .shadow_alpha = 52,
    .ground = rgb(0xE9,0xEC,0xF7), .surface_alt = rgb(0xF5,0xF6,0xFA),
    .raised = rgb(0xE1,0xE4,0xEE), .raised2 = rgb(0xE1,0xE4,0xEE), .track = rgb(0xD5,0xDA,0xE8),
    .ink = rgb(0x16,0x1A,0x2B), .ink2 = rgb(0x4A,0x51,0x68),
    .care = rgb(0xFF,0x7A,0x59), .on_care = rgb(0x2A,0x12,0x0A), .care_soft = rgb(0xE5,0x53,0x2E),
    .calm = rgb(0x4F,0x5E,0xF2),
    .hairline = rgb(0x16,0x1A,0x2B), .hairline_alpha = 22,
    .menubar_tint = rgb(0xFA,0xFA,0xFD), .menubar_alpha = 199,
    .dock_tint = rgb(0xFF,0xFF,0xFF), .dock_alpha = 184,
    .panel_tint = rgb(0xFA,0xFA,0xFD), .panel_alpha = 199,
    .window_body = rgb(0xFF,0xFF,0xFF), .window_alpha = 255,
    .term_green = rgb(0x1F,0xA5,0x62),
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
    { "Care",   rgb(0xff,0x7a,0x59), rgb(0xff,0x9a,0x76), rgb(0x9c,0x3f,0x27),
                rgb(0xff,0x7a,0x59), rgb(0xe5,0x53,0x2e), rgb(0xff,0xb4,0x9e) },
    { "Calm",   rgb(0x8c,0x9b,0xff), rgb(0xb0,0xbb,0xff), rgb(0x3d,0x4a,0xa8),
                rgb(0x4f,0x5e,0xf2), rgb(0x6f,0x7c,0xf5), rgb(0xb8,0xc0,0xfb) },
    { "Blue",   rgb(0x55,0x9a,0xff), rgb(0x82,0xbc,0xff), rgb(0x2a,0x56,0xb8),
                rgb(0x2f,0x6f,0xc8), rgb(0x4f,0x8e,0xe6), rgb(0x4f,0x8e,0xe6) },
    { "Green",  rgb(0x34,0xc7,0x59), rgb(0x6e,0xe7,0xa0), rgb(0x1f,0x8f,0x46),
                rgb(0x1a,0xa3,0x4a), rgb(0x34,0xc7,0x59), rgb(0x1a,0xa3,0x4a) },
    { "Purple", rgb(0xa8,0x55,0xf7), rgb(0xc9,0x9b,0xff), rgb(0x7c,0x3a,0xed),
                rgb(0x8b,0x3d,0xe0), rgb(0xa8,0x55,0xf7), rgb(0x8b,0x3d,0xe0) },
    { "Red",    rgb(0xff,0x5f,0x57), rgb(0xff,0x90,0x89), rgb(0xc0,0x39,0x2b),
                rgb(0xdc,0x4c,0x4c), rgb(0xff,0x5f,0x57), rgb(0xc0,0x39,0x2b) },
};
/* Fixed accents, plus one trailing "Auto" slot that pulls its colour from the
 * wallpaper (Material-You style). ACCENT_DYNAMIC is that last index. */
#define ACCENT_STATIC  ((u32)(sizeof(ACCENTS) / sizeof(ACCENTS[0])))
#define ACCENT_DYNAMIC ACCENT_STATIC

static u32  g_accent_idx = 0;
static u32  g_dyn_color  = rgb(0xff, 0x7a, 0x59);   /* last extracted / fallback */
static bool g_dyn_valid  = false;

/* Blend c toward t (white/black) by amt (0..255), and lift a dim colour so an
 * accent pulled from a dark wallpaper still reads on dark surfaces. */
static u32 mix(u32 c, u32 t, u32 amt) {
    u32 inv = 255 - amt;
    u32 r = (((c >> 16) & 0xFF) * inv + ((t >> 16) & 0xFF) * amt) / 255;
    u32 g = (((c >>  8) & 0xFF) * inv + ((t >>  8) & 0xFF) * amt) / 255;
    u32 b = (( c        & 0xFF) * inv + ( t        & 0xFF) * amt) / 255;
    return (r << 16) | (g << 8) | b;
}
static u32 ensure_bright(u32 c) {
    u32 r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
    u32 mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    if (mx >= 140 || mx == 0) return c;
    u32 sc = 140u * 255u / mx;
    r = r * sc / 255; if (r > 255) r = 255;
    g = g * sc / 255; if (g > 255) g = 255;
    b = b * sc / 255; if (b > 255) b = 255;
    return (r << 16) | (g << 8) | b;
}

static void apply_dynamic(void) {
    u32 c;
    if (wallpaper_dominant_color(&c)) { g_dyn_color = c; g_dyn_valid = true; }
    u32 base = ensure_bright(g_dyn_valid ? g_dyn_color : ACCENTS[0].d_primary);
    theme_dark.primary    = base;
    theme_dark.accent     = mix(base, 0xFFFFFF, 90);
    theme_dark.selection  = mix(base, 0x000000, 110);
    theme_light.primary   = mix(base, 0x000000, 60);
    theme_light.accent    = base;
    theme_light.selection = mix(base, 0x000000, 40);
}

void theme_set_accent(u32 idx) {
    if (idx > ACCENT_DYNAMIC) idx = 0;
    g_accent_idx = idx;
    if (idx == ACCENT_DYNAMIC) {
        apply_dynamic();
    } else {
        const accent_t *a = &ACCENTS[idx];
        theme_dark.primary   = a->d_primary;
        theme_dark.accent    = a->d_accent;
        theme_dark.selection = a->d_selection;
        theme_light.primary   = a->l_primary;
        theme_light.accent    = a->l_accent;
        theme_light.selection = a->l_selection;
    }
    gfx_wallpaper_cache_invalidate();
}

u32         theme_get_accent(void)        { return g_accent_idx; }
u32         theme_accent_count(void)       { return ACCENT_STATIC + 1u; }   /* + Auto */
const char *theme_accent_name(u32 idx)     { return idx >= ACCENT_STATIC ? "Auto" : ACCENTS[idx].name; }
u32         theme_accent_swatch(u32 idx)   {
    if (idx >= ACCENT_STATIC) return g_dyn_valid ? g_dyn_color : rgb(0x8a, 0x99, 0xba);
    return ACCENTS[idx].d_primary;
}
