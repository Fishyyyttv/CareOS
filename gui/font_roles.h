/* gui/font_roles.h -- v10 type roles (visual-system spec §2). Pure, host-tested.
 *
 * Three roles, five size slots each:
 *   UI       Figtree              11 13 15 17 20
 *   DISPLAY  Bricolage Grotesque  17 22 28 52 64
 *   MONO     JetBrains Mono       11 13 14 16 20 */
#ifndef CAREOS_FONT_ROLES_H
#define CAREOS_FONT_ROLES_H
#include "pure.h"

u32 font_role_px(u32 role, u32 slot);              /* design px for a role/slot; slot clamped to 4 */
u32 font_role_fallback_size(u32 role, u32 slot);   /* nearest legacy font_size_t (11,13,16,20,28) */

#endif
