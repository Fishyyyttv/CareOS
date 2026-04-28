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


/* -- 32-bpp color packing ------------------------------------------------- */
static inline u32 rgb(u8 r, u8 g, u8 b) {
    return ((u32)r << 16) | ((u32)g << 8) | (u32)b;
}

/* -- CareOS palette ------------------------------------------------------- */
#define COL_BG          rgb(0x11,0x12,0x17)
#define COL_SURFACE     rgb(0x1a,0x1d,0x27)
#define COL_SURFACE2    rgb(0x22,0x26,0x3a)
#define COL_PRIMARY     rgb(0x4A,0x6F,0xFF)
#define COL_ACCENT      rgb(0x8B,0x9F,0xFF)
#define COL_TEXT        rgb(0xe8,0xea,0xf6)
#define COL_DIM         rgb(0x88,0x91,0xb2)
#define COL_MUTED       rgb(0x55,0x5d,0x7a)
#define COL_BORDER      rgb(0x2a,0x35,0x66)
#define COL_GREEN       rgb(0x4a,0xde,0x80)
#define COL_RED         rgb(0xf8,0x71,0x71)
#define COL_YELLOW      rgb(0xfb,0xbf,0x24)
#define COL_TASKBAR     rgb(0x0d,0x0f,0x14)
#define COL_WINBAR      rgb(0x14,0x17,0x22)
#define COL_SHADOW      rgb(0x05,0x05,0x08)
#define COL_HOVER       rgb(0x2a,0x3a,0x66)
#define COL_DESK_GRAD   rgb(0x13,0x15,0x1f)
#define COL_BTN_CLOSE   rgb(0xf8,0x71,0x71)
#define COL_BTN_MIN     rgb(0xfb,0xbf,0x24)
#define COL_BTN_MAX     rgb(0x4a,0xde,0x80)
#define COL_CURSOR      rgb(0xff,0xff,0xff)
#define COL_SELECTION   rgb(0x2a,0x45,0x88)
#define COL_INPUT_BG    rgb(0x16,0x19,0x24)
#define COL_WHITE       rgb(0xff,0xff,0xff)
#define COL_BLACK       rgb(0x00,0x00,0x00)
#define COL_ORANGE      rgb(0xfb,0x92,0x3c)
#define COL_CYAN        rgb(0x22,0xd3,0xee)
#define COL_PURPLE      rgb(0x7c,0x3a,0xed)
#define COL_SURFACE3    rgb(0x2c,0x31,0x4a)
#define COL_TRANSPARENT 0xFF000001U  /* sentinel: gfx_char skips bg pixels */

/* -- Geometry ------------------------------------------------------------- */
typedef struct { i32 x, y; }         point_t;
typedef struct { i32 x, y, w, h; }   rect_t;

static inline bool rect_contains(rect_t r, i32 x, i32 y) {
    return x >= r.x && x < r.x+r.w && y >= r.y && y < r.y+r.h;
}
static inline rect_t rect_make(i32 x, i32 y, i32 w, i32 h) {
    rect_t r={x,y,w,h}; return r;
}

/* -- Layout constants (scaled at runtime) --------------------------------- */
#define TITLEBAR_H  24
#define TASKBAR_H   36
#define FONT_W       8
#define FONT_H      10

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
#define MIN_WIN_W   200
#define MIN_WIN_H   150

/* -- Window --------------------------------------------------------------- */
#define MAX_WINDOWS  8

typedef enum {
    APP_NONE=0, APP_TERMINAL, APP_NOTES, APP_FILES,
    APP_SYSMON, APP_CALC, APP_ABOUT,
    APP_BROWSER, APP_SETTINGS, APP_PKGMGR,
    APP_EDITOR, APP_PAINT, APP_CLOCK, APP_NETMON, APP_USERS
} app_id_t;

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

    /* Browser app */
    char  browser_url[256];
    char  browser_content[2048];
    bool  browser_loading;
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

    /* Package manager */
    u32   pkgmgr_sel;
    char  pkgmgr_status[128];

    /* WM: maximize/restore */
    bool   maximized;
    rect_t restore_rect;
} window_t;

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

/* -- Mouse ---------------------------------------------------------------- */
typedef struct {
    i32  x, y;
    bool left, right;
    bool left_clicked, left_released;
    bool right_clicked, right_released;
    i32  scroll_delta;
} mouse_t;


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
void textinput_draw(const textinput_t *t);
void textinput_key(textinput_t *t, char c);

/* -- GFX API -------------------------------------------------------------- */
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
window_t *wm_open(app_id_t app, const char *title, i32 x, i32 y, i32 w, i32 h);
void      wm_close(window_t *w);
void      wm_focus(window_t *w);
void      wm_draw_all(void);
void      wm_handle_mouse(mouse_t *m);
void      wm_handle_key(char c, window_t *w);
window_t *wm_focused(void);
window_t *wm_find_app(app_id_t app);
void      wm_cycle_focus(int dir);
void      wm_snap_focused(int mode);

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

/* -- GUI entry ------------------------------------------------------------ */
void gui_init(u32 *fb, u32 w, u32 h, u32 pitch);
void gui_run(void);

/* -- Launcher ------------------------------------------------------------- */
void launcher_draw(bool open);
bool launcher_handle_mouse(mouse_t *m, bool *open);

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
