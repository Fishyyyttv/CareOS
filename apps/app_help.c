/* CareOS v9 -- apps/app_help.c -- Help viewer */
#include "apps_common.h"

#define HELP_HDR_H  46

void app_help_init(window_t *w){
    win_clear(w);
    win_append(w, "KEYBOARD SHORTCUTS\n");
    win_append(w, "  Terminal    Enter=run  Backspace=delete\n");
    win_append(w, "  Notes       Type to edit, Tab to indent\n");
    win_append(w, "  Editor      Type to edit, Tab=4 spaces\n");
    win_append(w, "  Files       Click=select  Dbl-click=open\n");
    win_append(w, "  Help        j/Space=down  k=up\n");
    win_append(w, "\n");
    win_append(w, "SHELL COMMANDS\n");
    win_append(w, "  ls [-l]        List directory contents\n");
    win_append(w, "  cd <dir>       Change working directory\n");
    win_append(w, "  cat <file>     Print file contents\n");
    win_append(w, "  mkdir <dir>    Create directory\n");
    win_append(w, "  rm <file>      Remove file\n");
    win_append(w, "  ps             List running processes\n");
    win_append(w, "  whoami         Show current user\n");
    win_append(w, "  free           Show memory usage\n");
    win_append(w, "  date           Show current date/time\n");
    win_append(w, "  uname          Show system information\n");
    win_append(w, "  ping <host>    Ping a network host\n");
    win_append(w, "  curl <url>     Make HTTP request\n");
    win_append(w, "  carepkg        Package manager CLI\n");
    win_append(w, "\n");
    win_append(w, "PACKAGE MANAGER\n");
    win_append(w, "  carepkg list           List all packages\n");
    win_append(w, "  carepkg install <pkg>  Install package\n");
    win_append(w, "  carepkg remove <pkg>   Remove package\n");
    win_append(w, "  carepkg search <q>     Search packages\n");
    win_append(w, "  carepkg create <name>  Make .care template\n");
    win_append(w, "  carepkg update         Update all packages\n");
    win_append(w, "\n");
    win_append(w, "APPLICATIONS\n");
    win_append(w, "  Terminal    Full shell with command set\n");
    win_append(w, "  Browser     HTTP/HTTPS web browser\n");
    win_append(w, "  Files       File system explorer\n");
    win_append(w, "  Editor      Code editor, syntax highlighting\n");
    win_append(w, "  Paint       64x64 pixel art canvas\n");
    win_append(w, "  Monitor     CPU / RAM / process graphs\n");
    win_append(w, "  Settings    System configuration panel\n");
    win_append(w, "  Packages    GUI package manager\n");
    win_append(w, "  Users       User account management\n");
    win_append(w, "  NetMon      Network traffic monitor\n");
    win_append(w, "  Clock       Analog clock and calendar\n");
    win_append(w, "\n");
    win_append(w, "ABOUT\n");
    win_append(w, "  CareOS v9.0  --  IA-32 hobby operating system\n");
    win_append(w, "  Kernel: CareOS 9.0 x86 protected mode\n");
    win_append(w, "  Graphics: Linear 32-bpp framebuffer\n");
    win_append(w, "  Network: TCP/IP stack with DHCP\n");
    win_append(w, "  Storage: In-memory virtual filesystem\n");
}

void app_help_draw(window_t *w){
    rect_t cr = wm_client_rect(w);
    i32 sc = (i32)GFX_FONT_SCALE;
    u32 lh = FONT_H * (u32)sc + 3;

    gfx_rect(cr.x, cr.y, cr.w, cr.h, COL_SURFACE);

    /* Header */
    gfx_gradient_rect(cr.x, cr.y, cr.w, HELP_HDR_H, g_theme->surface3, COL_SURFACE);
    gfx_hline(cr.x, cr.y + HELP_HDR_H, cr.w, COL_BORDER);
    gfx_str_ex(cr.x + 14, cr.y + 10, "Help & Reference", COL_TEXT, COL_TRANSPARENT, FONT_H2);
    gfx_str(cr.x + 14, cr.y + 28, "CareOS v9  --  j/Space scroll down, k scroll up",
            g_theme->muted, COL_TRANSPARENT);

    /* Scrollable content with per-line colour */
    i32 content_y = cr.y + HELP_HDR_H + 2;
    i32 content_h = cr.h - HELP_HDR_H - 2;
    u32 lines_vis = (u32)content_h / lh;

    /* Count total lines */
    u32 total = 1;
    for (u32 i = 0; i < w->text_len; i++) if (w->text_buf[i] == '\n') total++;
    if (w->scroll + lines_vis > total)
        w->scroll = (total > lines_vis) ? total - lines_vis : 0;

    /* Seek to start line */
    u32 line = 0; const char *p = w->text_buf;
    while (line < w->scroll && *p) { if (*p == '\n') line++; p++; }

    i32 y = content_y + 3;
    u32 drawn = 0;
    while (*p && drawn < lines_vis) {
        const char *end = p; while (*end && *end != '\n') end++;
        u32 len = (u32)(end - p); if (len > 159) len = 159;
        char tmp[160]; kmemcpy(tmp, p, len); tmp[len] = '\0';

        /* Choose color based on line content */
        u32 fc;
        bool is_section = (tmp[0] >= 'A' && tmp[0] <= 'Z' && tmp[1] >= 'A' && tmp[1] <= 'Z');
        bool is_empty   = (tmp[0] == '\0');
        if (is_section) {
            /* Section header: draw highlight bar */
            gfx_rect(cr.x, y - 1, cr.w, (i32)lh + 1, g_theme->surface2);
            gfx_rect_rounded(cr.x + 6, y, 3, (i32)lh - 1, 1, COL_PRIMARY);
            fc = COL_ACCENT;
        } else if (is_empty) {
            fc = COL_TEXT;
        } else if (tmp[0] == ' ' && tmp[1] == ' ') {
            /* Indented line — command name in accent, description in muted */
            fc = COL_TEXT;
            u32 ci = 2;
            while (ci < len && tmp[ci] != ' ' && tmp[ci] != '\0') ci++;
            u32 dstart = ci;
            while (dstart < len && tmp[dstart] == ' ') dstart++;
            if (dstart < len && dstart > 2) {
                /* Draw command in accent, description in muted */
                char cmd_part[64]; u32 cl = (ci - 2 < 63) ? ci - 2 : 63;
                kmemcpy(cmd_part, tmp + 2, cl); cmd_part[cl] = '\0';
                char desc_part[128]; u32 dl = (len - dstart < 127) ? len - dstart : 127;
                kmemcpy(desc_part, tmp + dstart, dl); desc_part[dl] = '\0';
                gfx_str(cr.x + 14 + 2 * FONT_W * sc, y, cmd_part, COL_ACCENT, COL_TRANSPARENT);
                i32 desc_x = cr.x + 14 + 2 * FONT_W * sc + (i32)kstrlen(cmd_part) * FONT_W * sc;
                /* pad to alignment */
                gfx_str(cr.x + 14 + 18 * FONT_W * sc, y, desc_part, g_theme->muted, COL_TRANSPARENT);
                y += (i32)lh; drawn++;
                if (*end == '\n') p = end + 1; else break;
                continue;
            }
        } else {
            fc = COL_TEXT;
        }
        gfx_str_clipped(cr.x + 14, y, cr.w - 20, tmp, fc, COL_TRANSPARENT);
        y += (i32)lh; drawn++;
        if (*end == '\n') p = end + 1; else break;
    }
}

void app_help_key(window_t *w, char c){
    if (c == '\x1B') return;
    rect_t cr = wm_client_rect(w);
    u32 lh = FONT_H * (u32)GFX_FONT_SCALE + 3;
    u32 vis = (u32)(cr.h - HELP_HDR_H - 2) / lh;
    if (c == 'j' || c == ' ') { if (w->scroll + vis < w->text_len) w->scroll += 3; }
    if (c == 'k') { if (w->scroll >= 3) w->scroll -= 3; else w->scroll = 0; }
}
