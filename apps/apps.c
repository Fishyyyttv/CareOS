/* CareOS v9 -- apps/apps.c
 * This file is now a stub. All individual apps have been split into
 * separate files in this directory. See apps_common.h for shared helpers.
 *
 * Individual app files:
 *   app_terminal.c  - Terminal / shell
 *   app_notes.c     - Notes / text editor
 *   app_files.c     - File manager
 *   app_sysmon.c    - System monitor
 *   app_calc.c      - Calculator
 *   app_about.c     - About screen
 *   app_settings.c  - Settings panel
 *   app_browser.c   - Web browser
 *   app_pkgmgr.c    - Package manager
 *   app_editor.c    - Code editor
 *   app_paint.c     - Paint app
 *   app_clock.c     - Clock / calendar
 *   app_netmon.c    - Network monitor
 *   app_users.c     - User manager
 *   app_help.c      - Help viewer
 */
#include "apps_common.h"

/* Title-bar toolbar dispatch (see gui.h). Apps without a toolbar keep the
 * plain window title. */
bool app_toolbar_draw(window_t *w, rect_t bar) {
    if (bar.w < 60) return false;
    switch (w->app) {
    case APP_TERMINAL: return app_terminal_toolbar(w, bar);
    case APP_FILES:    return app_files_toolbar(w, bar);
    case APP_EDITOR:   return app_editor_toolbar(w, bar);
    default:           return false;
    }
}

bool app_toolbar_click(window_t *w, i32 x, i32 y) {
    switch (w->app) {
    case APP_TERMINAL: return app_terminal_toolbar_click(w, x, y);
    case APP_FILES:    return app_files_toolbar_click(w, x, y);
    case APP_EDITOR:   return app_editor_toolbar_click(w, x, y);
    default:           return false;
    }
}
