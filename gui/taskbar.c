/* =============================================================================
 * CareOS gui/taskbar.c -- Dedicated Taskbar Subsystem
 * ============================================================================= */
 #include "kernel.h"
 #include "gui.h"
 
 /* Taskbar layout constants */
 #define TB_LAUNCHER_W  54
 #define TB_TRAY_W     190
 #define TB_SLOT_W     140
 #define TB_SLOT_H     (TASKBAR_H - 14)
 #define TB_PAD          7
 #define TB_SHOWDESK_W  22
 
 /* >>> FIX: add missing globals <<< */
 static u32 tb_last_tick = 0;
 static rtc_time_t tb_time;
 
 static app_id_t pinned_apps[] = { APP_TERMINAL, APP_FILES, APP_BROWSER, APP_EDITOR, APP_SETTINGS };
 #define PINNED_COUNT 5
 #define TASKBAR_SLOT_MAX (PINNED_COUNT + MAX_WINDOWS)
 
 typedef struct {
     rect_t launcher_rect;
     rect_t app_rects[TASKBAR_SLOT_MAX];
     app_id_t app_ids[TASKBAR_SLOT_MAX];
     window_t *app_windows[TASKBAR_SLOT_MAX];
     u32 app_count;
 } taskbar_layout_t;
 
 
 static const char *taskbar_app_title(app_id_t app) {
     switch (app) {
     case APP_TERMINAL: return "Terminal";
     case APP_FILES:    return "Files";
     case APP_BROWSER:  return "Browser";
     case APP_EDITOR:   return "Editor";
     case APP_SETTINGS: return "Settings";
     case APP_SYSMON:   return "Monitor";
     case APP_CALC:     return "Calculator";
     case APP_PAINT:    return "Paint";
     case APP_CLOCK:    return "Clock";
     case APP_NETMON:   return "NetMon";
     case APP_USERS:    return "Users";
     case APP_ABOUT:    return "About";
     default: return "App";
     }
 }
 
 static void taskbar_build_layout(taskbar_layout_t *layout) {
     i32 sw = (i32)SCREEN_W;
     i32 ty = (i32)SCREEN_H - TASKBAR_H - 12;
     kmemset(layout, 0, sizeof(*layout));
 
     /* Only show running apps */
     for (int i = 0; i < MAX_WINDOWS && layout->app_count < TASKBAR_SLOT_MAX; i++) {
         window_t *w = wm_get_window(i);
         if (!w || !w->active) continue;
         layout->app_ids[layout->app_count] = w->app;
         layout->app_windows[layout->app_count] = w;
         layout->app_count++;
     }
 
     i32 icon_w = 48;
     i32 gap = 8;
     i32 total_w = TB_LAUNCHER_W + gap + (i32)layout->app_count * (icon_w + gap) + 40;
     i32 dock_x = (sw - total_w) / 2;
 
     layout->launcher_rect = rect_make(dock_x + 20, ty + (TASKBAR_H - 44)/2, TB_LAUNCHER_W, 44);
 
     for (u32 i = 0; i < layout->app_count; i++) {
         i32 x = dock_x + 20 + TB_LAUNCHER_W + gap + (i32)i * (icon_w + gap);
         layout->app_rects[i] = rect_make(x, ty + (TASKBAR_H - 44)/2, icon_w, 44);
     }
 }
 
 void taskbar_draw(void) {
     taskbar_layout_t layout;
     i32 sw = (i32)SCREEN_W;
     i32 ty = (i32)SCREEN_H - TASKBAR_H - 12;
     i32 sc = (i32)GFX_FONT_SCALE;
 
     /* Refresh RTC every ~50 ticks (0.5 s at 100 Hz) */
     u32 now = timer_get_ticks();
     if (now - tb_last_tick >= 50 || tb_last_tick == 0) {
         tb_last_tick = now;
         rtc_read(&tb_time);
     }
 
     taskbar_build_layout(&layout);
     
     i32 icon_w = 48;
     i32 gap = 8;
     i32 total_w = TB_LAUNCHER_W + gap + (i32)layout.app_count * (icon_w + gap) + 40;
     i32 dock_x = (sw - total_w) / 2;
 
     /* Dock Background (Glass Pill) */
     gfx_rect_rounded(dock_x, ty, total_w, TASKBAR_H, TASKBAR_H/2, g_theme->taskbar);
     gfx_rect_blend(dock_x, ty, total_w, TASKBAR_H, COL_WHITE, 12);
     gfx_rect_rounded_outline(dock_x, ty, total_w, TASKBAR_H, TASKBAR_H/2, COL_BORDER);
 
     /* Launcher */
     i32 lx = layout.launcher_rect.x, ly = layout.launcher_rect.y;
     gfx_rect_rounded(lx, ly, TB_LAUNCHER_W, 44, 12, launcher_open ? COL_PRIMARY : COL_SURFACE2);
     for (int i = 0; i < 6; i++) {
         i32 ox = (i % 3) * 10 + 15;
         i32 oy = (i / 3) * 10 + 16;
         gfx_rect_rounded(lx + ox, ly + oy, 6, 6, 2, launcher_open ? COL_WHITE : COL_PRIMARY);
     }
 
     /* App Icons */
     for (u32 i = 0; i < layout.app_count; i++) {
         rect_t r = layout.app_rects[i];
         window_t *w = layout.app_windows[i];
         bool active = w->focused && !w->minimized;
         
         if (active) {
             gfx_rect_rounded(r.x, r.y, r.w, r.h, 12, COL_SURFACE3);
             gfx_rect_rounded_outline(r.x, r.y, r.w, r.h, 12, COL_PRIMARY);
         }
         gfx_draw_icon(w->app, r.x + 8, r.y + 8, r.w - 16, active ? COL_TEXT : COL_DIM);
     }
     
     /* Time & Tray */
     char ts[64];
     rtc_time_t pt = tb_time;
 
     int hour = (int)pt.hour - 7;
     if (hour < 0) { hour += 24; if (pt.day > 0) pt.day--; }
     bool pm = (hour >= 12);
     int h12 = hour % 12;
     if (h12 == 0) h12 = 12;
 
     const char *months[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
     ksprintf(ts, "%s %d, %02d:%02d %s", 
              (pt.month > 0 && pt.month <= 12) ? months[pt.month-1] : "???",
              pt.day, h12, pt.minute, pm ? "PM" : "AM");
 
     i32 clock_w = gfx_str_width(ts);
     i32 tray_w = clock_w + 60;
     i32 tray_x = sw - tray_w - TB_SHOWDESK_W - TB_PAD;
     i32 text_y = ty + (TASKBAR_H - (i32)(FONT_H * sc)) / 2;
     
     gfx_rect_rounded(tray_x, ty + TB_PAD, tray_w, TB_SLOT_H, 12, COL_SURFACE2);
     gfx_rect_rounded_outline(tray_x, ty + TB_PAD, tray_w, TB_SLOT_H, 12, COL_BORDER);
     
     gfx_circle_fill(tray_x + 15, ty + TASKBAR_H / 2, 4, net_is_up() ? COL_GREEN : COL_RED);
     gfx_str(tray_x + 30, text_y, ts, COL_TEXT, COL_TRANSPARENT);
 
     gfx_rect(sw - TB_SHOWDESK_W, ty + 12, 1, TASKBAR_H - 24, COL_BORDER);
     if (rect_contains(rect_make(sw-TB_SHOWDESK_W, ty, TB_SHOWDESK_W, TASKBAR_H), lx, ly)) {
         gfx_rect_blend(sw - TB_SHOWDESK_W + 4, ty + TB_PAD, TB_SHOWDESK_W - 8, TB_SLOT_H, COL_WHITE, 10);
     }
 }
 
 void taskbar_handle_mouse(mouse_t *m) {
     taskbar_layout_t layout;
     if (!m->left_clicked) return;
     if (m->y < (i32)SCREEN_H - TASKBAR_H - 16) return;
 
     if (m->x >= (i32)SCREEN_W - TB_SHOWDESK_W) { wm_minimize_all(); return; }
 
     taskbar_build_layout(&layout);
 
     if (rect_contains(layout.launcher_rect, m->x, m->y)) { launcher_open = !launcher_open; return; }
 
     for (u32 i = 0; i < layout.app_count; i++) {
         if (!rect_contains(layout.app_rects[i], m->x, m->y)) continue;
         window_t *w = layout.app_windows[i];
         if (w) {
             if (w->focused && !w->minimized) wm_minimize(w);
             else { w->minimized = false; wm_focus(w); }
         }
         return;
     }
 }