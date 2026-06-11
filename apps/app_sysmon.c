/* CareOS v9 -- apps/app_sysmon.c -- System Monitor */
#include "apps_common.h"

void app_sysmon_tick(window_t *w){
    u32 pos = w->sysmon_hist_pos % 64;
    w->sysmon_cpu_hist[pos] = (timer_get_ticks() % 70) + 5;
    w->sysmon_mem_hist[pos] = kmem_used() * 100 / KERNEL_HEAP_SIZE;
    w->sysmon_hist_pos++;
}

/* Draw a labelled progress bar with rounded ends and percentage label */
static i32 draw_stat_bar(i32 x, i32 y, i32 w, const char *label, u32 pct, u32 bar_col) {
    i32 sc = (i32)GFX_FONT_SCALE;
    i32 lh = FONT_H * sc;
    char pct_s[8]; kutoa(pct, pct_s, 10); kstrcat(pct_s, "%");

    gfx_str(x, y, label, COL_DIM, COL_TRANSPARENT);
    gfx_str_right(x, y, w, pct_s, COL_TEXT, COL_TRANSPARENT);
    y += lh + 4;

    i32 bh = 10 + 4 * sc;
    gfx_rect_rounded(x, y, w, bh, bh/2, COL_SURFACE3);
    gfx_rect_rounded_outline(x, y, w, bh, bh/2, COL_BORDER);
    if (pct > 0) {
        i32 fill = (w - 2) * (i32)pct / 100;
        if (fill > w - 2) fill = w - 2;
        if (fill > 0)
            gfx_rect_rounded(x + 1, y + 1, fill, bh - 2, (bh-2)/2, bar_col);
    }
    return y + bh + 10 + 4 * sc;
}

void app_sysmon_draw(window_t *w){
    rect_t cr = wm_client_rect(w);
    i32 sc = (i32)GFX_FONT_SCALE;

    gfx_rect(cr.x, cr.y, cr.w, cr.h, COL_SURFACE2);

    /* Tabs */
    const char *tabs[] = {"Stats", "Apps", "Memory", "Hardware"};
    i32 tab_count = 4;
    i32 tab_h = 18 + 8 * sc;
    i32 tab_w = cr.w / tab_count;

    for (int i = 0; i < tab_count; i++) {
        i32 tx = cr.x + i * tab_w;
        bool active = (u32)i == w->tab;
        gfx_rect(tx, cr.y, tab_w, tab_h, active ? COL_SURFACE3 : COL_SURFACE2);
        if (active)
            gfx_rect_rounded(tx + 4, cr.y + tab_h - 4, tab_w - 8, 3, 1, COL_PRIMARY);
        gfx_set_clip(tx + 2, cr.y, tab_w - 4, tab_h);
        gfx_str_centered(tx, cr.y + (tab_h - FONT_H * sc) / 2, tab_w,
                         tabs[i], active ? COL_TEXT : COL_MUTED, COL_TRANSPARENT);
        gfx_clear_clip();
    }
    gfx_hline(cr.x, cr.y + tab_h, cr.w, COL_BORDER);

    i32 cy = cr.y + tab_h + 12;
    i32 bx = cr.x + 14;
    i32 bw = cr.w - 28;

    if (w->tab == 0) {
        /* Overview */
        u32 cpu = w->sysmon_cpu_hist[(w->sysmon_hist_pos + 63) % 64];
        u32 mem = kmem_used() * 100 / KERNEL_HEAP_SIZE;

        cy = draw_stat_bar(bx, cy, bw, "CPU Usage", cpu, COL_GREEN);
        cy = draw_stat_bar(bx, cy, bw, "Memory",    mem, COL_PRIMARY);

        /* Info rows */
        struct { const char *label; const char *val; } rows[4];
        char task_s[8], uptime_s[16], net_val[24];
        kutoa(task_count_active(), task_s, 10);
        kutoa(timer_get_ticks()/1000, uptime_s, 10); kstrcat(uptime_s, "s");
        kstrcpy(net_val, net_is_up() ? "UP (e1000)" : "DOWN");

        rows[0].label = "Tasks:";    rows[0].val = task_s;
        rows[1].label = "Uptime:";   rows[1].val = uptime_s;
        rows[2].label = "Network:";  rows[2].val = net_val;
        rows[3].label = "FS Nodes:"; char fn_s[8]; kutoa(vfs_node_count(), fn_s, 10);
        rows[3].val = fn_s;

        cy += 4;
        i32 lw = gfx_str_width("FS Nodes:");
        for (int i = 0; i < 4; i++) {
            gfx_str(bx, cy, rows[i].label, COL_MUTED, COL_TRANSPARENT);
            u32 col = (i == 2) ? (net_is_up() ? COL_GREEN : COL_RED) : COL_ACCENT;
            gfx_str(bx + lw + FONT_W * sc * 2, cy, rows[i].val, col, COL_TRANSPARENT);
            cy += FONT_H * sc + 8;
        }

    } else if (w->tab == 1) {
        /* Process list */
        i32 lh = FONT_H * sc + 6;
        /* Header */
        gfx_rect(bx, cy, bw, lh, COL_SURFACE3);
        gfx_str(bx + 4, cy + (lh - FONT_H * sc) / 2, "  PID  STATE    NAME", COL_ACCENT, COL_TRANSPARENT);
        cy += lh + 2;

        for (u32 i = 0; i < MAX_TASKS && cy < cr.y + cr.h - lh; i++) {
            task_t *t = task_get(i + 1);
            if (!t || t->state == TASK_DEAD) continue;
            const char *st[] = {"UNUSED","READY","RUN","BLOCK","SLEEP","ZOMBIE","DEAD"};
            char line[48]; char n[8];
            kstrcpy(line, "  ");
            kutoa(t->id, n, 10); kstrcat(line, n);
            while (kstrlen(line) < 7) kstrcat(line, " ");
            kstrcat(line, t->state <= TASK_DEAD ? st[t->state] : "?");
            while (kstrlen(line) < 16) kstrcat(line, " ");
            kstrcat(line, t->name);
            u32 col = (t->state == TASK_RUNNING) ? COL_GREEN : COL_TEXT;
            gfx_set_clip(bx, cy, bw, lh);
            gfx_str(bx + 4, cy + (lh - FONT_H * sc) / 2, line, col, COL_TRANSPARENT);
            gfx_clear_clip();
            cy += lh + 1;
        }

    } else if (w->tab == 2) {
        /* Memory graph */
        gfx_str(bx, cy, "Memory Usage (64 ticks)", COL_TEXT, COL_TRANSPARENT);
        cy += FONT_H * sc + 8;

        i32 gh = 60 + 30 * sc;
        i32 gw = bw;
        gfx_rect(bx, cy, gw, gh, COL_SURFACE3);
        gfx_rect_outline(bx, cy, gw, gh, COL_BORDER);

        for (int i = 1; i < 64; i++) {
            u32 v1 = w->sysmon_mem_hist[(w->sysmon_hist_pos + (u32)i - 1) % 64];
            u32 v2 = w->sysmon_mem_hist[(w->sysmon_hist_pos + (u32)i)     % 64];
            i32 y1 = cy + gh - (i32)(v1 * (u32)gh / 100);
            i32 y2 = cy + gh - (i32)(v2 * (u32)gh / 100);
            gfx_line(bx + (i-1) * gw / 64, y1, bx + i * gw / 64, y2, COL_PRIMARY);
        }
        cy += gh + 10;

        char used_s[16], free_s[16];
        kutoa(kmem_used()/1024, used_s, 10); kstrcat(used_s, " KB");
        kutoa(kmem_free_bytes()/1024, free_s, 10); kstrcat(free_s, " KB");
        gfx_str(bx, cy, "Used:",  COL_MUTED, COL_TRANSPARENT);
        gfx_str(bx + gfx_str_width("Used:  "), cy, used_s, COL_ACCENT, COL_TRANSPARENT);
        cy += FONT_H * sc + 6;
        gfx_str(bx, cy, "Free:", COL_MUTED, COL_TRANSPARENT);
        gfx_str(bx + gfx_str_width("Free:  "), cy, free_s, g_theme->success, COL_TRANSPARENT);
    } else if (w->tab == 3) {
        /* Hardware (PCI) */
        i32 lh = FONT_H * sc + 6;
        gfx_str(bx, cy, "PCI Devices Detected:", COL_ACCENT, COL_TRANSPARENT);
        cy += lh + 6;
        
        u32 dev_count = pci_device_count();
        if (dev_count == 0) {
            gfx_str(bx, cy, "Scanning bus... (none found)", COL_DIM, COL_TRANSPARENT);
        } else {
            for (u32 i = 0; i < dev_count && cy < cr.y + cr.h - lh; i++) {
                pci_device_t *d = pci_device_get(i);
                char line[64]; char tmp[8];
                kutoa(d->bus, tmp, 10); kstrcpy(line, tmp); kstrcat(line, ":");
                kutoa(d->device, tmp, 10); kstrcat(line, tmp); kstrcat(line, "  ");
                
                /* Show Vendor + Device Name */
                const char *vn = pci_vendor_name(d->vendor_id);
                const char *dn = pci_device_name(d->vendor_id, d->device_id);
                kstrcat(line, vn); kstrcat(line, " "); kstrcat(line, dn);
                
                gfx_str(bx, cy, line, COL_TEXT, COL_TRANSPARENT);
                cy += lh + 2;
            }
        }
    }
}

void app_sysmon_click(window_t *w, i32 x, i32 y){
    rect_t cr = wm_client_rect(w);
    i32 sc = (i32)GFX_FONT_SCALE;
    i32 tab_h = 18 + 8 * sc;
    i32 tab_w = cr.w / 4;
    if (y < cr.y + tab_h) {
        int idx = (x - cr.x) / tab_w;
        if (idx >= 0 && idx < 4) w->tab = (u32)idx;
    }
}
