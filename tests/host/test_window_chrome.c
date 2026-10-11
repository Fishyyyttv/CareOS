#include "window_chrome.h"
#include <assert.h>

int main(void) {
    /* Every pixel of every target belongs to exactly the intended control,
     * including shared boundaries. Repeat with a partly off-screen window. */
    for (i32 wx = -120; wx <= 120; wx += 120) {
        for (i32 i = 0; i < 3; i++) {
            i32 cx = wm_control_x(wx, i);
            for (i32 dy = -14; dy < 14; dy++)
                for (i32 dx = -11; dx < 11; dx++)
                    assert(wm_control_hit(wx, 80, 48, cx + dx, 104 + dy) == i);
            assert(wm_control_hit(wx, 80, 48, cx, 89) == WM_CONTROL_NONE);
            assert(wm_control_hit(wx, 80, 48, cx, 118) == WM_CONTROL_NONE);
        }
        assert(wm_control_hit(wx, 80, 48, wx + 9, 104) == WM_CONTROL_NONE);
        assert(wm_control_hit(wx, 80, 48, wx + 76, 104) == WM_CONTROL_NONE);
        assert(wm_control_hit(wx, 80, 48, wx + WM_TITLE_LEFT, 104) == WM_CONTROL_NONE);
    }
    assert(wm_snap_x(160, 1440) == 174);
    assert(wm_snap_x(-200, 1440) == 4);
    assert(wm_snap_x(1400, 1440) + WM_SNAP_WIDTH == 1436);
    puts("window chrome: control boundaries and flyout anchoring passed");
    return 0;
}
