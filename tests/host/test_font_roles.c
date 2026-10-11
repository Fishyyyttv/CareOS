#include "pure.h"
#include "font_roles.h"
#include <assert.h>
int main(void) {
    assert(font_role_px(0, 0) == 11 && font_role_px(0, 4) == 20);
    assert(font_role_px(1, 3) == 52 && font_role_px(1, 4) == 64);
    assert(font_role_px(2, 2) == 14);
    assert(font_role_px(1, 99) == 64);                 /* slot clamps */
    assert(font_role_px(9, 0) == font_role_px(0, 0));  /* unknown role -> UI */
    /* nearest of today's 5 sizes: CAPTION=0 .. H1=4 */
    assert(font_role_fallback_size(0, 1) == 1);        /* UI 13 -> FONT_BODY */
    assert(font_role_fallback_size(1, 4) == 4);        /* Display 64 -> FONT_H1 */
    assert(font_role_fallback_size(1, 0) == 2);        /* Display 17 -> FONT_H3 */
    assert(font_role_fallback_size(2, 2) == 1);        /* Mono 14 -> 13 px face */
    puts("font_roles ok");
    return 0;
}
