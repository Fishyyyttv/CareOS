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
    if (!doom_win || !DG_ScreenBuffer) return;
    
    /* Blit Doom's internal 32-bpp buffer to the CareOS window buffer */
    kmemcpy(doom_win->win_buffer.pixels, DG_ScreenBuffer, 
            DOOMGENERIC_RESX * DOOMGENERIC_RESY * 4);
    
    /* Trigger a redraw of the window client area */
    rect_t cr = wm_client_rect(doom_win);
    gfx_dirty(cr.x, cr.y, DOOMGENERIC_RESX, DOOMGENERIC_RESY);
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
    w->app = APP_NONE; /* Doom handles its own state */
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
    /* Doom thread calls DG_DrawFrame which updates win_buffer directly. */
}

void app_doom_key(window_t *w, char c) {
    /* Input is handled by DG_GetKey polling global state */
}
