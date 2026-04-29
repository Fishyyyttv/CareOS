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

/* -- Multi-desktop -------------------------------------------------------- */
#define DESKTOP_COUNT 4
extern u32 g_current_desktop;

/* -- Idle / screensaver --------------------------------------------------- */
extern u32 g_last_activity_tick;

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


/* -- Application IDs ------------------------------------------------------ */
typedef enum {
    APP_NONE=0, APP_TERMINAL, APP_NOTES, APP_FILES,
    APP_SYSMON, APP_CALC, APP_ABOUT, APP_HELP,
    APP_BROWSER, APP_SETTINGS, APP_PKGMGR,
    APP_EDITOR, APP_PAINT, APP_CLOCK, APP_NETMON, APP_USERS
} app_id_t;

/* -- Typography & Icons --------------------------------------------------- */
typedef enum {
    FONT_CAPTION, /* 11px */
    FONT_BODY,    /* 13px */
    FONT_H3,      /* 16px */
    FONT_H2,      /* 20px */
    FONT_H1       /* 28px */
} font_size_t;

#define FONT_W       8
#define FONT_H      13
#define FONT_SPACING 3

void gfx_str_ex(i32 x, i32 y, const char *s, u32 fg, u32 bg, font_size_t size);
void gfx_str_centered_ex(i32 x, i32 y, i32 w, const char *s, u32 fg, u32 bg, font_size_t size);
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
    char  browser_content[16384];
    char  browser_title[128];
    bool  browser_loading;
    bool  browser_url_active;
    i32   browser_scroll;
    u32   browser_history_pos;
    char  browser_history[10][256];
    u32   browser_history_count;

    /* Editor app */
    char  editor_path[FS_PATH_MAX];
    bool  editor_modified;

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

    /* WM: maximize/restore */
    bool   maximized;
    rect_t restore_rect;

    /* Animation state */
    bool   animating;
    rect_t target_rect;
    u32    anim_start_tick;
    u8     opacity;       /* 0-255 */

    /* Snap layout flyout */
    u32    hover_start_tick;
    bool   showing_snap_layouts;

    /* Multi-desktop */
    u32    desktop;       /* 0-3; shown only when g_current_desktop matches */

    widget_t *root;
    gfx_buffer_t win_buffer;
} window_t;

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

void gfx_setpixel(i32 x, i32 y, u32 color);
void gfx_hline(i32 x, i32 y, i32 len, u32 color);
void gfx_vline(i32 x, i32 y, i32 len, u32 color);
void gfx_rect(i32 x, i32 y, i32 w, i32 h, u32 color);
void gfx_rect_outline(i32 x, i32 y, i32 w, i32 h, u32 color);
void gfx_rect_rounded(i32 x, i32 y, i32 w, i32 h, i32 r, u32 color);
void gfx_shadow(i32 x, i32 y, i32 w, i32 h);
void gfx_circle(i32 cx, i32 cy, i32 r, u32 color);
void gfx_circle_fill(i32 cx, i32 cy, i32 r, u32 color);

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

void app_editor_init(window_t *w);
void app_editor_draw(window_t *w);
void app_editor_key(window_t *w, char c);

void app_paint_init(window_t *w);
void app_paint_draw(window_t *w);
void app_paint_key(window_t *w, char c);

void app_clock_draw(window_t *w);
void app_netmon_draw(window_t *w);

void app_users_init(window_t *w);
void app_users_draw(window_t *w);
void app_users_key(window_t *w, char c);

/* -- Notification system -------------------------------------------------- */
#define NOTIFY_MAX 4
void notify_push(const char *title, const char *msg, u32 color);
void notify_draw(void);
void notify_tick(void);
bool notify_handle_mouse(mouse_t *m);

/* -- Additional GFX helpers ----------------------------------------------- */
void gfx_rect_rounded_outline(i32 x, i32 y, i32 w, i32 h, i32 r, u32 color);
void gfx_gradient_rect(i32 x, i32 y, i32 w, i32 h, u32 c1, u32 c2);
void gfx_str_clipped(i32 x, i32 y, i32 max_w, const char *s, u32 fg, u32 bg);
void gfx_set_clip(i32 x, i32 y, i32 w, i32 h);
void gfx_clear_clip(void);
i32  gfx_str_width(const char *s);
void gfx_str_right(i32 x, i32 y, i32 w, const char *s, u32 fg, u32 bg);
void gfx_bar(i32 x, i32 y, i32 w, i32 h, u32 bg, u32 fg, u32 pct);
void gfx_line(i32 x0, i32 y0, i32 x1, i32 y1, u32 color);
void gfx_triangle_fill(i32 x0, i32 y0, i32 x1, i32 y1, i32 x2, i32 y2, u32 c);
void gfx_shadow_ext(i32 x, i32 y, i32 w, i32 h, i32 depth);
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
void app_pkgmgr_click(window_t *w, i32 x, i32 y);
void app_paint_click(window_t *w, i32 x, i32 y, mouse_t *m);
void app_users_click(window_t *w, i32 x, i32 y);

#endif /* GUI_H */
