#include "ui_icons.h"
/* index = app_id_t; value = ui_icon_t (gui/ui.h order) */
static const u8 MAP[] = {
    0 /*NONE->CARE*/, 1 /*TERMINAL*/, 4 /*NOTES*/, 2 /*FILES*/, 7 /*SYSMON->MONITOR*/,
    9 /*CALC*/, 14 /*ABOUT*/, 14 /*HELP->ABOUT*/, 3 /*BROWSER*/, 12 /*SETTINGS*/,
    11 /*PKGMGR->STORE*/, 5 /*EDITOR*/, 6 /*PAINT*/, 8 /*CLOCK*/, 10 /*NETMON*/,
    13 /*USERS*/, 15 /*MAZE->GAMES*/, 15 /*3D->GAMES*/, 15 /*DOOM->GAMES*/
};
u32 ui_icon_key_for_app(u32 app_id) { return app_id < sizeof(MAP) ? MAP[app_id] : 0u; }
