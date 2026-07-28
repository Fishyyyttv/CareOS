#ifndef GUI_H
#define GUI_H

/* =============================================================================
 * CareOS GUI -- gui.h
 * Linear 32-bpp framebuffer, resolution-independent layout
 * ============================================================================= */

#include "kernel.h"

/* -- Screen dimensions (set at runtime from multiboot) -------------------- */
extern u32 SCREEN_W;
extern u32 SCREEN_H;
extern u32 SCREEN_PITCH;    /* bytes per row */
extern u32 *FRAMEBUFFER;    /* linear framebuffer pointer */
extern u32 GFX_FONT_SCALE;   /* Bitmap text scale; kept 1 at 1080p for crisp UI text */
extern u32 GFX_FONT_W;   /* active family's BODY advance  */
extern u32 GFX_FONT_H;   /* active family's BODY line height */

/* -- Geometry ------------------------------------------------------------- */
typedef struct { i32 x, y; }         point_t;
typedef struct { i32 x, y, w, h; }   rect_t;

static inline bool rect_contains(rect_t r, i32 x, i32 y) {
    return x >= r.x && x < r.x+r.w && y >= r.y && y < r.y+r.h;
}
static inline rect_t rect_make(i32 x, i32 y, i32 w, i32 h) {
    rect_t r={x,y,w,h}; return r;
}

/* -- Mouse ---------------------------------------------------------------- */
typedef struct {
    i32  x, y;
    bool left, right;
    bool left_clicked, left_released;
    bool right_clicked, right_released;
    i32  scroll_delta;
} mouse_t;

/* -- 32-bpp color packing (Macro for constant initializers) --------------- */
#define rgb(r, g, b) (((u32)(r) << 16) | ((u32)(g) << 8) | (u32)(b))

/* -- CareOS Design Tokens & Theming --------------------------------------- */
typedef struct {
    u32 *pixels;
    u32 w, h;
    u32 pitch;
} gfx_buffer_t;

typedef struct widget_s widget_t;

typedef enum {
    WIDGET_PANEL,
    WIDGET_BUTTON,
    WIDGET_LABEL,
    WIDGET_INPUT,
    WIDGET_SCROLLBAR,
} widget_type_t;

struct widget_s {
    widget_type_t type;
    rect_t   rect;       /* relative to parent */
    rect_t   abs_rect;   /* absolute screen coords (cached) */
    u32      id;
    u32      color;
    u32      bg_color;
    char     text[64];
    
    void     (*draw)(widget_t *self, gfx_buffer_t *target);
    void     (*on_click)(widget_t *self, mouse_t *m);
    
    widget_t *parent;
    widget_t *first_child;
    widget_t *next_sibling;
    
    void     *data;      /* type-specific data */
};

typedef struct {
    u32 bg, surface, surface2, surface3, border;
    u32 primary, accent, hover, selection;
    u32 text, dim, muted;
    u32 success, warning, error, info;
    u32 taskbar, winbar, shadow;
    u32 input_bg, cursor, glass_tint;
    u8  glass_alpha, shadow_alpha;
    bool is_dark;
} theme_t;

extern theme_t *g_theme;

/* -- Color Helpers (kept for compatibility during migration) -------------- */
#define COL_BG          (g_theme->bg)
#define COL_SURFACE     (g_theme->surface)
#define COL_SURFACE2    (g_theme->surface2)
#define COL_SURFACE3    (g_theme->surface3)
#define COL_PRIMARY     (g_theme->primary)
#define COL_ACCENT      (g_theme->accent)
#define COL_TEXT        (g_theme->text)
#define COL_DIM         (g_theme->dim)
#define COL_MUTED       (g_theme->muted)
#define COL_BORDER      (g_theme->border)
#define COL_GREEN       (g_theme->success)
#define COL_RED         (g_theme->error)
#define COL_YELLOW      (g_theme->warning)
#define COL_TASKBAR     (g_theme->taskbar)
#define COL_WINBAR      (g_theme->winbar)
#define COL_SHADOW      (g_theme->shadow)
#define COL_HOVER       (g_theme->hover)
#define COL_INPUT_BG    (g_theme->input_bg)
#define COL_CURSOR      (g_theme->cursor)
#define COL_GLASS_TINT  (g_theme->glass_tint)
#define COL_SELECTION   (g_theme->selection)
#define THEME_GLASS_ALPHA  (g_theme->glass_alpha)
#define THEME_SHADOW_ALPHA (g_theme->shadow_alpha)
#define COL_WHITE       rgb(0xff,0xff,0xff)
#define COL_BLACK       rgb(0x00,0x00,0x00)

/* -- Clipboard ------------------------------------------------------------ */
#define CLIPBOARD_SIZE 4096
extern char g_clipboard[CLIPBOARD_SIZE];
extern u32  g_clipboard_len;
extern bool g_clipboard_is_cut;

/* -- Multi-desktop -------------------------------------------------------- */
#define DESKTOP_COUNT 4
extern u32 g_current_desktop;

/* -- Idle / screensaver --------------------------------------------------- */
extern u32 g_last_activity_tick;

/* Set by anything that wants to lock the session (Spotlight, a power menu);
 * the desktop loop honours it by running the login/lock flow. */
extern volatile bool g_lock_request;

#define TASKBAR_H       72
#define TOPBAR_H        34
#define SIDEBAR_W       182
#define TITLEBAR_H      48

#define kabs(x) ((x) < 0 ? -(x) : (x))
#define max(a,b) ((a) > (b) ? (a) : (b))

#define COL_ORANGE      rgb(0xfb,0x92,0x3c)
#define COL_CYAN        rgb(0x22,0xd3,0xee)
#define COL_PURPLE      rgb(0x7c,0x3a,0xed)
#define COL_TRANSPARENT 0xFF000001U

/* ── Care Design Language (CDL) tokens ─────────────────────────────────────
 * One source of truth for radii, shadows, glass, and animation timing so every
 * built-in surface reads as one intentional system. Change it here, not in the
 * individual widgets. Radii are the values from the CDL spec:
 *   buttons/inputs 10, menus 12, windows/cards 14, dock 24.                   */
#define CDL_SP            8       /* base spacing grid unit */
#define CDL_R_BUTTON      10
#define CDL_R_INPUT       10
#define CDL_R_MENU        12
#define CDL_R_WINDOW      14
#define CDL_R_CARD        14
#define CDL_R_DOCK        24

/* Soft drop shadow: large, very soft, low opacity → surfaces feel like they
 * float. Approximated by stacked, growing rounded blends (no real Gaussian). */
#define CDL_SHADOW_BLUR   28      /* halo reach in px */
#define CDL_SHADOW_ALPHA  40      /* ~15% ink at the core */
#define CDL_SHADOW_DY     10      /* light comes from above → offset down */

/* Selective glass: blurred wallpaper + ~25% tint + hairline. Used on the dock,
 * menus, notifications, launcher — NOT on window bodies (they stay opaque). */
#define CDL_GLASS_ALPHA   64      /* ~25% tint over the blurred backdrop */
#define CDL_GLASS_BLUR    6

/* Animation: one duration + one easing family for opens/closes/minimise.
 * Kept short so windows feel snappy rather than floaty. */
#define CDL_ANIM_MS       95

/* Pointer shapes. wm_handle_mouse sets g_cursor_shape from what is under the
 * cursor (resize edge / corner / move) and mouse_draw_cursor renders it. */
typedef enum {
    CURSOR_ARROW = 0,
    CURSOR_RESIZE_H,     /* ↔  left / right edge         */
    CURSOR_RESIZE_V,     /* ↕  top / bottom edge         */
    CURSOR_RESIZE_NWSE,  /* ⤢  TL / BR corner            */
    CURSOR_RESIZE_NESW,  /* ⤡  TR / BL corner            */
    CURSOR_MOVE          /* ✛  dragging a window         */
} cursor_shape_t;
extern cursor_shape_t g_cursor_shape;


/* -- Application IDs ------------------------------------------------------ */
typedef enum {
    APP_NONE=0, APP_TERMINAL, APP_NOTES, APP_FILES,
    APP_SYSMON, APP_CALC, APP_ABOUT, APP_HELP,
    APP_BROWSER, APP_SETTINGS, APP_PKGMGR,
    APP_EDITOR, APP_PAINT, APP_CLOCK, APP_NETMON, APP_USERS, APP_MAZE, APP_3D, APP_DOOM
} app_id_t;


/* -- Typography & Icons --------------------------------------------------- */
typedef enum {
    FONT_CAPTION, /* 11px */
    FONT_BODY,    /* 13px */
    FONT_H3,      /* 16px */
    FONT_H2,      /* 20px */
    FONT_H1       /* 28px */
} font_size_t;

/* Active font metrics. Variables, not constants -- font_set_family() updates
 * them. No call site uses these in a constant expression, so every existing
 * user keeps compiling unchanged.
 *
 * The (i32) cast is load-bearing, not decoration. These were the plain-int
 * literals 8 and 13; GFX_FONT_W/H are u32. Without the cast, any surrounding
 * `int - FONT_H` promotes to unsigned, so a negative intermediate wraps to
 * ~4.29e9 and a following /2 yields ~2.1e9 -- e.g. centring a text row inside
 * a bar shorter than the line height. Casting back to i32 preserves the exact
 * signed arithmetic every call site was written against. */
#define FONT_W  ((i32)GFX_FONT_W)
#define FONT_H  ((i32)GFX_FONT_H)

/* Line height of any text size, in device pixels, GFX_FONT_SCALE applied.
 * FONT_H only covers FONT_BODY; headings have their own line heights and they
 * are not a fixed multiple of the body one (JetBrains Mono H3 is 21 where BODY
 * is 17; Classic is 13 for both). Layouts that stack a heading above body text
 * must ask for the heading's own height or the two rows collide. */
i32 gfx_line_h_ex(font_size_t size);

void gfx_str_ex(i32 x, i32 y, const char *s, u32 fg, u32 bg, font_size_t size);
void gfx_str_centered_ex(i32 x, i32 y, i32 w, const char *s, u32 fg, u32 bg, font_size_t size);
i32  gfx_str_width_ex(const char *s, font_size_t size);
i32  gfx_str_width_n(const char *s, u32 n, font_size_t size);
void gfx_rect_blend(i32 x, i32 y, i32 w, i32 h, u32 color, u8 alpha);
void gfx_draw_icon(app_id_t app, i32 x, i32 y, i32 size, u32 color);

/* -- Window resize edges -------------------------------------------------- */
#define RESIZE_NONE    0
#define RESIZE_LEFT    1
#define RESIZE_RIGHT   2
#define RESIZE_TOP     3
#define RESIZE_BOTTOM  4
#define RESIZE_TL      5
#define RESIZE_TR      6
#define RESIZE_BL      7
#define RESIZE_BR      8

/* Minimum window dimensions */
#define MIN_WIN_W   280
#define MIN_WIN_H   220

/* -- Window --------------------------------------------------------------- */
#define MAX_WINDOWS  24


typedef struct window {
    bool      active, focused, minimized, dragging;
    bool      resizing;
    u32       resize_edge;
    app_id_t  app;
    char      title[32];
    rect_t    rect;
    i32       drag_ox, drag_oy;

    char      text_buf[4096];
    u32       text_len;
    char      input_buf[256];
    u32       input_len;
    u32       scroll;
    u32       cursor_pos;

    struct fs_node *fm_dir;
    u32             fm_sel;

    char  calc_display[32];
    char  calc_expr[128];
    i32   calc_val, calc_prev;
    char  calc_op;
    bool  calc_new_num;
    bool  calc_error;

    u32   sysmon_tick;
    u32   z_order;
    u32   tab;            /* generic tab index */
    u32   sysmon_hist_pos;
    u32   sysmon_cpu_hist[64];
    u32   sysmon_mem_hist[64];
    u32   pkgmgr_tab;

    /* Settings app */
    u32   settings_tab;
    u32   settings_field;
    char  settings_old_pass[32];
    char  settings_new_pass[32];
    char  settings_status[96];
    u32   settings_status_color;

    /* Browser app */
    char  browser_url[256];
    char  browser_content[65536];
    char  browser_title[128];
    bool  browser_loading;
    bool  browser_url_active;
    i32   browser_scroll;
    u32   browser_history_pos;
    char  browser_history[10][256];
    u32   browser_history_count;
    /* Browser tabs */
    u32   browser_tab_count;
    u32   browser_tab_sel;
    char  browser_tab_url[4][256];
    char  browser_tab_title[4][48];
    i32   browser_tab_scroll[4];
    /* Browser bookmarks */
    u32   browser_bm_count;
    char  browser_bm_url[8][128];
    char  browser_bm_title[8][32];
    bool  browser_bm_open;       /* bookmarks dropdown visible */
    /* Browser find / source */
    char  browser_find_buf[64];
    u32   browser_find_len;
    bool  browser_find_active;
    bool  browser_source_view;
    u32   browser_find_count;
    u32   browser_find_cur;

    /* Editor app */
    char  editor_path[FS_PATH_MAX];
    bool  editor_modified;
    bool  editor_show_sidebar;
    u32   editor_sidebar_tab;

    /* Paint app */
    u32   paint_color;
    bool  paint_drawing;
    i32   paint_last_x, paint_last_y;

    /* Clock / NetMon */
    u32   clock_tick;

    /* Users app */
    u32   users_sel;
    u32   um_sel;
    char  um_input_name[32];
    char  um_input_pass[32];
    u32   um_field;
    char  users_status[96];
    u32   users_status_color;

    /* Package manager */
    u32   pkgmgr_sel;
    char  pkgmgr_status[128];
    bool  pkgmgr_installing;

    /* WM: maximize/restore */
    bool   maximized;
    rect_t restore_rect;

    /* Animation state */
    bool   animating;
    rect_t target_rect;
    u32    anim_start_tick;
    u8     opacity;       /* 0-255 */
    u8     anim_kind;     /* WM_ANIM_* : open / close / minimise transition */
    rect_t anim_from;     /* geometry captured when the transition began */
    u8     hover_edge;    /* RESIZE_* edge the cursor is over, for the highlight */

    /* Snap layout flyout */
    u32    hover_start_tick;
    bool   showing_snap_layouts;

    /* Multi-desktop */
    u32    desktop;       /* 0-3; shown only when g_current_desktop matches */

    widget_t *root;
    gfx_buffer_t win_buffer;
} window_t;

/* Window transition kinds (window_t.anim_kind) */
#define WM_ANIM_NONE      0
#define WM_ANIM_OPEN      1
#define WM_ANIM_CLOSE     2
#define WM_ANIM_MINIMIZE  3
#define WM_ANIM_RESTORE   4

/* Snapping modes */
#define SNAP_NONE       0
#define SNAP_LEFT       1
#define SNAP_RIGHT      2
#define SNAP_TOP        3
#define SNAP_BOTTOM     4
#define SNAP_TL         5
#define SNAP_TR         6
#define SNAP_BL         7
#define SNAP_BR         8
#define SNAP_FULL       9

/* Window text buffer size */
#define WIN_TEXT_BUF  4096

/* -- Desktop icon --------------------------------------------------------- */
typedef struct {
    char     label[16];
    app_id_t app;
    i32      x, y;
    bool     hover;
    bool     selected;
    u32      icon_color;
} desktop_icon_t;

/* -- Keyboard routing: ONLY to focused window ----------------------------- */
void wm_handle_key(char c, window_t *w);

/* -- Mouse ---------------------------------------------------------------- */
/* (Defined at top of header) */


/* -- Widget types (used by gfx.c) ----------------------------------------- */
typedef struct {
    rect_t rect;
    char   label[32];
    bool   hover;
    bool   pressed;
    bool   active;
    u32    bg;
    u32    fg;
} button_t;

#define TEXTINPUT_MAX 256

typedef struct {
    rect_t rect;
    char   buf[256];
    u32    len;
    u32    cursor;
    bool   focused;
    bool   hover;
    char   placeholder[64];
    u32    fg;
    u32    bg;
} textinput_t;

#define MAX_NOTIFICATIONS 4
#define NOTIF_LIFETIME    400   /* ticks */

typedef struct {
    bool   active;
    char   title[32];
    char   msg[64];
    char   body[128];
    u32    color;
    u32    icon_color;
    u32    born_tick;
    i32    y_off;
} notification_t;

/* Widget draw API */
void button_draw(const button_t *b);
bool button_hit(const button_t *b, i32 mx, i32 my);
void button_update(button_t *b, const mouse_t *m);
bool button_take_click(button_t *b, const mouse_t *m);
void textinput_draw(const textinput_t *t);
void textinput_key(textinput_t *t, char c);
void textinput_handle_mouse(textinput_t *t, const mouse_t *m);

/* -- GFX API -------------------------------------------------------------- */
void theme_switch(bool dark);
void gfx_init(u32 *fb, u32 w, u32 h, u32 pitch);
void gfx_set_pixel_format(u8 r_shift, u8 g_shift, u8 b_shift); /* B-01 */
void gfx_dirty(i32 x, i32 y, i32 w, i32 h);                   /* P-02 */
void gfx_flip(void);
void gfx_clear(u32 color);

/* Wallpaper cache: compose the static desktop backdrop once and blit it back
 * each frame instead of re-rendering it. blit() returns false when no valid
 * cache exists (draw the wallpaper normally, then capture()). Invalidate on any
 * theme/accent/wallpaper change. */
void gfx_wallpaper_cache_invalidate(void);
bool gfx_wallpaper_cache_blit(void);
void gfx_wallpaper_cache_capture(void);
/* Parallax variant of the cache blit: shifts the cached backdrop a few px
 * opposite the cursor (edge-clamped) for depth, still one pass, no re-render. */
bool gfx_wallpaper_parallax_blit(i32 cursor_x, i32 cursor_y);

/* Desktop backdrop cache: the fully composited static chrome (wallpaper +
 * frosted sidebar + widget panels). Those panels run a per-frame blur + soft
 * shadow that dominated the frame; capturing the result once and blitting it
 * lets interaction frames skip that work entirely. Invalidate whenever the
 * chrome's content changes (theme, clock tick, hover). */
void gfx_desktop_cache_invalidate(void);
bool gfx_desktop_cache_blit(void);
void gfx_desktop_cache_capture(void);

void gfx_setpixel(i32 x, i32 y, u32 color);
void gfx_hline(i32 x, i32 y, i32 len, u32 color);
void gfx_vline(i32 x, i32 y, i32 len, u32 color);
/* -- Graphics Primitives -------------------------------------------------- */
void gfx_rect(i32 x, i32 y, i32 w, i32 h, u32 color);
void gfx_rect_outline(i32 x, i32 y, i32 w, i32 h, u32 color);
void gfx_rect_alpha(i32 x, i32 y, i32 w, i32 h, u32 color, u8 alpha);
void gfx_rect_rounded(i32 x, i32 y, i32 w, i32 h, i32 r, u32 color);
void gfx_rect_rounded_outline(i32 x, i32 y, i32 w, i32 h, i32 r, u32 color);
void gfx_rect_blend(i32 x, i32 y, i32 w, i32 h, u32 color, u8 alpha);
void gfx_shadow(i32 x, i32 y, i32 w, i32 h);
void gfx_shadow_ext(i32 x, i32 y, i32 w, i32 h, u32 alpha);
void gfx_circle(i32 cx, i32 cy, i32 r, u32 color);
void gfx_circle_fill(i32 cx, i32 cy, i32 r, u32 color);

/* ── CDL primitives (foundation for glass, soft shadows, animation) ──────── */
u32  gfx_isqrt(u32 v);
/* Alpha-blended rounded-rect fill — composites only inside the rounded shape,
 * so no square corners leak past the radius (unlike gfx_rect_blend). */
void gfx_rect_rounded_blend(i32 x, i32 y, i32 w, i32 h, i32 r, u32 color, u8 alpha);
/* Separable box blur of the current target in place, over [x,y,w,h]. */
void gfx_blur_region(i32 x, i32 y, i32 w, i32 h, i32 radius);
/* Modern soft drop shadow (CDL defaults / explicit). Draw BEFORE the surface. */
void gfx_shadow_soft(i32 x, i32 y, i32 w, i32 h, i32 r);
void gfx_shadow_soft_ex(i32 x, i32 y, i32 w, i32 h, i32 r, i32 blur, u8 alpha, i32 dy);
/* Glass panel = blur the backdrop + tint + top highlight + hairline border. */
void gfx_glass_panel(i32 x, i32 y, i32 w, i32 h, i32 r);
void gfx_glass_panel_ex(i32 x, i32 y, i32 w, i32 h, i32 r, u32 tint, u8 alpha, i32 blur);
/* Time-eased 0..256 progress (ease-out cubic), and an integer lerp helper. */
u32  cdl_ease_out(u32 elapsed_ms, u32 duration_ms);
i32  cdl_lerp(i32 a, i32 b, u32 t256);

/* Click ripples: spawn one at a point, draw+decay all, query if any animating. */
void gfx_ripple(i32 x, i32 y, u32 color);
void gfx_ripples_draw(void);
bool gfx_ripples_active(void);

/* ── Accent colour engine (drives primary/accent/selection theme-wide) ───── */
void        theme_set_accent(u32 idx);
u32         theme_get_accent(void);
u32         theme_accent_count(void);
const char *theme_accent_name(u32 idx);
u32         theme_accent_swatch(u32 idx);
/* Sample a vivid dominant colour from the active wallpaper (0xRRGGBB). Returns
 * false if no wallpaper is loaded. Backs the "Auto" (Material-You) accent. */
bool        wallpaper_dominant_color(u32 *out_rgb);

/* ── Desktop widgets (gui/widgets.c) ─────────────────────────────────────── */
void widgets_init(void);
void widgets_draw(mouse_t *m);          /* desktop layer, behind windows */
bool widgets_handle_mouse(mouse_t *m);  /* true if a widget consumed the click/drag */

void gfx_char(i32 x, i32 y, char c, u32 fg, u32 bg);
void gfx_str(i32 x, i32 y, const char *s, u32 fg, u32 bg);
void gfx_str_centered(i32 x, i32 y, i32 w, const char *s, u32 fg, u32 bg);
void gfx_str_bg_none(i32 x, i32 y, const char *s, u32 fg); /* transparent bg */

/* -- WM API --------------------------------------------------------------- */
void      wm_init(void);
void      app_default_size(app_id_t app, i32 sw, i32 sh, i32 *w, i32 *h);
window_t *wm_open(app_id_t app, const char *title, i32 x, i32 y, i32 w, i32 h);
void      wm_close(window_t *w);
void      wm_focus(window_t *w);
void      wm_draw_all(void);
bool      wm_animate_all(void);
void      wm_draw_snap_layouts(window_t *w);
void      wm_handle_mouse(mouse_t *m);
void      wm_handle_key(char c, window_t *w);
window_t *wm_focused(void);
window_t *wm_find_app(app_id_t app);
void      wm_cycle_focus(int dir);
void      wm_snap_focused(int mode);
void      wm_minimize_all(void);
void      wm_minimize(window_t *w);
window_t *wm_get_window(int i);

void desktop_draw(void);
void desktop_handle_mouse(mouse_t *m);
void taskbar_draw(void);
void taskbar_handle_mouse(mouse_t *m);

/* -- App API -------------------------------------------------------------- */
void app_terminal_init(window_t *w);
void app_terminal_draw(window_t *w);
void app_terminal_key(window_t *w, char c);

void app_notes_init(window_t *w);
void app_notes_draw(window_t *w);
void app_notes_key(window_t *w, char c);

void app_files_init(window_t *w);
void app_files_draw(window_t *w);
void app_files_key(window_t *w, char c);

void app_sysmon_draw(window_t *w);
void app_sysmon_tick(window_t *w);

void app_calc_init(window_t *w);
void app_calc_draw(window_t *w);
void app_calc_key(window_t *w, char c);

void app_about_init(window_t *w);
void app_about_draw(window_t *w);

/* -- New app APIs --------------------------------------------------------- */
void app_settings_init(window_t *w);
void app_settings_draw(window_t *w);
void app_settings_key(window_t *w, char c);
void app_settings_click(window_t *w, i32 x, i32 y, mouse_t *m);

void app_browser_init(window_t *w);
void app_browser_draw(window_t *w);
void app_browser_key(window_t *w, char c);

void app_pkgmgr_init(window_t *w);
void app_pkgmgr_draw(window_t *w);
void app_pkgmgr_key(window_t *w, char c);

void app_doom_init(window_t *w);
void app_doom_draw(window_t *w);
void app_doom_key(window_t *w, char c);

void app_editor_init(window_t *w);
void app_editor_draw(window_t *w);
void app_editor_key(window_t *w, char c);
void app_editor_click(window_t *w, i32 x, i32 y, mouse_t *m);

void app_paint_init(window_t *w);
void app_paint_draw(window_t *w);
void app_paint_key(window_t *w, char c);

void app_clock_draw(window_t *w);
void app_netmon_draw(window_t *w);

void app_users_init(window_t *w);
void app_users_draw(window_t *w);
void app_users_key(window_t *w, char c);

void app_maze_init(window_t *w);
void app_maze_draw(window_t *w);
void app_maze_key(window_t *w, char c);

void app_3d_init(window_t *w);
void app_3d_draw(window_t *w);
void app_3d_key(window_t *w, char c);

/* -- Notification system -------------------------------------------------- */
#define NOTIFY_MAX 4
void notify_push(const char *title, const char *msg, u32 color);
void notify_draw(void);
void notify_tick(void);
bool notify_active(void);
bool notify_handle_mouse(mouse_t *m);

/* -- Additional GFX helpers ----------------------------------------------- */
void gfx_gradient_rect(i32 x, i32 y, i32 w, i32 h, u32 c1, u32 c2);
void gfx_str_clipped(i32 x, i32 y, i32 max_w, const char *s, u32 fg, u32 bg);
void gfx_set_clip(i32 x, i32 y, i32 w, i32 h);
void gfx_clear_clip(void);
bool gfx_get_clip(rect_t *out);   /* false when unclipped; see gfx.c */
i32  gfx_str_width(const char *s);
void gfx_str_right(i32 x, i32 y, i32 w, const char *s, u32 fg, u32 bg);
void gfx_bar(i32 x, i32 y, i32 w, i32 h, u32 bg, u32 fg, u32 pct);
void gfx_line(i32 x0, i32 y0, i32 x1, i32 y1, u32 color);
void gfx_triangle_fill(i32 x0, i32 y0, i32 x1, i32 y1, i32 x2, i32 y2, u32 c);
void gfx_str_bold(i32 x, i32 y, const char *s, u32 fg, u32 bg);

/* -- WM helpers ----------------------------------------------------------- */
rect_t wm_client_rect(window_t *w);

/* -- Mouse driver --------------------------------------------------------- */
void mouse_init(void);
void mouse_update(mouse_t *m);
void mouse_draw_cursor(i32 x, i32 y);

/* -- Widget System -------------------------------------------------------- */
widget_t *widget_create(widget_type_t type, i32 x, i32 y, i32 w, i32 h);
void widget_add_child(widget_t *parent, widget_t *child);
void widget_update_abs_rect(widget_t *wi, i32 px, i32 py);
void widget_draw_recursive(widget_t *wi, gfx_buffer_t *target);
void layout_vbox(widget_t *parent, i32 padding, i32 gap);
void layout_hbox(widget_t *parent, i32 padding, i32 gap);

/* -- GFX & Theme ---------------------------------------------------------- */
void gfx_init(u32 *fb, u32 w, u32 h, u32 pitch);
void gfx_set_target(gfx_buffer_t *target);
void gfx_blit(gfx_buffer_t *src, i32 dx, i32 dy);
void gui_init(u32 *fb, u32 w, u32 h, u32 pitch);
void gui_run(void);

/* -- Launcher ------------------------------------------------------------- */
extern bool launcher_open;
void launcher_draw(mouse_t *m);
void launcher_handle_key(char c);
void launcher_handle_mouse(mouse_t *m);

/* -- App help aliases ----------------------------------------------------- */
void app_help_init(window_t *w);
void app_help_draw(window_t *w);
void app_help_key(window_t *w, char c);

/* -- NetMon tick ---------------------------------------------------------- */
void app_netmon_tick(window_t *w);

/* -- Click handlers ------------------------------------------------------- */
void app_files_click(window_t *w, i32 x, i32 y, mouse_t *m);
void app_calc_click(window_t *w, i32 x, i32 y);
void app_sysmon_click(window_t *w, i32 x, i32 y);
void app_browser_click(window_t *w, i32 x, i32 y);
void app_browser_scroll(window_t *w, i32 delta);
void app_pkgmgr_click(window_t *w, i32 x, i32 y);
void app_paint_click(window_t *w, i32 x, i32 y, mouse_t *m);
void app_users_click(window_t *w, i32 x, i32 y);

/* -- System Sounds --------------------------------------------------------- */
void speaker_play(u32 freq);
void speaker_stop(void);
void speaker_beep(u32 freq, u32 ms);
void speaker_startup(void);
void speaker_error(void);

extern gfx_buffer_t g_screen_buf;
extern gfx_buffer_t *g_target;

#endif /* GUI_H */
