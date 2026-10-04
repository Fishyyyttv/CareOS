#include "pure.h"
#include "ui_icons.h"
#include <assert.h>
/* app ids from gui.h app_id_t order */
enum { NONE, TERMINAL, NOTES, FILES, SYSMON, CALC, ABOUT, HELP, BROWSER, SETTINGS, PKGMGR,
       EDITOR, PAINT, CLOCK, NETMON, USERS, MAZE, D3, DOOM };
enum { I_CARE, I_TERMINAL, I_FILES, I_BROWSER, I_NOTES, I_EDITOR, I_PAINT, I_MONITOR, I_CLOCK,
       I_CALC, I_NETMON, I_STORE, I_SETTINGS, I_USERS, I_ABOUT, I_GAMES };
int main(void) {
    assert(ui_icon_key_for_app(TERMINAL) == I_TERMINAL);
    assert(ui_icon_key_for_app(SYSMON) == I_MONITOR);
    assert(ui_icon_key_for_app(PKGMGR) == I_STORE);
    assert(ui_icon_key_for_app(HELP) == I_ABOUT);
    assert(ui_icon_key_for_app(MAZE) == I_GAMES && ui_icon_key_for_app(D3) == I_GAMES && ui_icon_key_for_app(DOOM) == I_GAMES);
    assert(ui_icon_key_for_app(NONE) == I_CARE);
    assert(ui_icon_key_for_app(999) == I_CARE);
    puts("ui_icons ok"); return 0;
}
