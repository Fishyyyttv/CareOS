#include "kernel.h"
#include "../gui/gui.h"
#include "../doomgeneric/doomgeneric/doomgeneric.h"

static window_t *doom_win = NULL;
static char *doom_argv[] = {"doom", "-iwad", "/home/user/DOOM1.WAD"};
static bool doom_started = false;

/* -- doomgeneric Implementation ------------------------------------------- */

void DG_Init() {
    kprintf("[doom] DG_Init called from thread.\n");
}

void DG_DrawFrame() {
    /* Runs on the Doom task thread. We only *publish* the latest frame into the
       window's offscreen buffer here; the GUI thread does the actual screen
       compositing in app_doom_draw() and owns gfx_flip(). Guard against the
       window slot being closed (and possibly reused by another app) while the
       Doom task keeps running. */
    if (!doom_win || !doom_win->active || doom_win->app != APP_DOOM) return;
    if (!DG_ScreenBuffer || !doom_win->win_buffer.pixels) return;

    /* Copy Doom's internal 32-bpp frame into the CareOS window buffer. Both use
       0xAARRGGBB (R@16, G@8, B@0), so no channel swizzle is required. */
    kmemcpy(doom_win->win_buffer.pixels, DG_ScreenBuffer,
            DOOMGENERIC_RESX * DOOMGENERIC_RESY * 4);
    /* NOTE: no gfx_dirty()/gfx_flip() from this thread -- the dirty list and
       backbuffer are owned by the GUI loop, which repaints periodically. */
}

void DG_SleepMs(uint32_t ms) {
    /* Simple yield to keep system responsive */
    task_yield();
}

uint32_t DG_GetTicksMs() {
    return timer_get_ticks() * (1000 / PIT_HZ);
}

int DG_GetKey(int* pressed, unsigned char* key) {
    /* TODO: Map CareOS keyboard driver to Doom keys */
    if (keyboard_haschar()) {
        *pressed = 1;
        *key = (unsigned char)keyboard_getchar();
        return 1;
    }
    return 0;
}

void DG_SetWindowTitle(const char * title) {
    if (doom_win) kstrncpy(doom_win->title, title, 31);
}

/* -- CareOS App Interface ------------------------------------------------- */

void doom_task_entry(void) {
    kprintf("[doom] Task started. Initializing engine...\n");
    doomgeneric_Create(3, doom_argv);
    kprintf("[doom] Task exiting (Doom quit).\n");
    doom_started = false;
}

void app_doom_init(window_t *w) {
    /* Keep w->app == APP_DOOM (set by wm_open) so the WM's switch(w->app)
       dispatch actually routes draw/key events to app_doom_draw/app_doom_key.
       Previously this was forced to APP_NONE, which left the window undispatched
       and blank. */
    doom_win = w;
    
    kprintf("[doom] Checking for /home/user/DOOM1.WAD...\n");
    fs_node_t *wad = vfs_resolve_path("/home/user/DOOM1.WAD");
    if (!wad) {
        kprintf("[doom] ERROR: DOOM1.WAD not found.\n");
        kstrncpy(w->title, "Doom (WAD Missing)", 31);
        return;
    }
    
    if (!doom_started) {
        kprintf("[doom] Spawning Doom thread...\n");
        task_create("doom", doom_task_entry);
        doom_started = true;
    }
}

void app_doom_draw(window_t *w) {
    /* Composite the frame the Doom thread published into win_buffer onto the
       window's client area. draw_window() has already set a gfx clip rect for
       us, but we write straight into the backbuffer for speed, so we bound the
       copy to the client rect (and the screen) ourselves.

       The Doom image is a fixed DOOMGENERIC_RESX x DOOMGENERIC_RESY block at the
       top-left of win_buffer. win_buffer keeps its original geometry even if the
       window is resized, so its stride is constant. */
    if (!w->win_buffer.pixels || !g_target || !g_target->pixels) return;

    rect_t cr = wm_client_rect(w);

    const i32 src_w = DOOMGENERIC_RESX;
    const i32 src_h = DOOMGENERIC_RESY;
    u32 sp = w->win_buffer.pitch / 4;   /* src stride in pixels */
    u32 dp = g_target->pitch / 4;       /* dst stride in pixels */

    /* Destination bounds = Doom image, clipped to client rect and screen. */
    i32 x0 = cr.x, y0 = cr.y;
    i32 x1 = cr.x + src_w, y1 = cr.y + src_h;
    if (x1 > cr.x + cr.w)      x1 = cr.x + cr.w;
    if (y1 > cr.y + cr.h)      y1 = cr.y + cr.h;
    if (x0 < 0)                x0 = 0;
    if (y0 < 0)                y0 = 0;
    if (x1 > (i32)g_target->w) x1 = (i32)g_target->w;
    if (y1 > (i32)g_target->h) y1 = (i32)g_target->h;

    for (i32 dy = y0; dy < y1; dy++) {
        const u32 *src = &w->win_buffer.pixels[(u32)(dy - cr.y) * sp];
        u32       *dst = &g_target->pixels[(u32)dy * dp];
        for (i32 dx = x0; dx < x1; dx++)
            dst[dx] = src[dx - cr.x] & 0x00FFFFFF;   /* drop Doom's alpha byte */
    }
}

void app_doom_key(window_t *w, char c) {
    /* Input is handled by DG_GetKey polling global state */
}
