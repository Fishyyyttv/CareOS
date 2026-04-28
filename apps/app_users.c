/* CareOS v9 -- apps/app_users.c -- live user management */
#include "apps_common.h"

static void users_set_status(window_t *w, const char *msg, u32 color) {
    kstrncpy(w->users_status, msg, sizeof(w->users_status) - 1);
    w->users_status[sizeof(w->users_status) - 1] = '\0';
    w->users_status_color = color;
}

static void users_mask(const char *src, char *out, u32 max) {
    u32 len = (u32)kstrlen(src);
    if (len >= max) len = max - 1;
    for (u32 i = 0; i < len; i++) out[i] = '*';
    out[len] = '\0';
}

static u32 users_collect(user_t **list, u32 max) {
    u32 count = 0;
    user_t *u = (user_t*)user_get_by_uid(0);
    if (u && count < max) list[count++] = u;
    for (u32 uid = 1000; uid < 1128 && count < max; uid++) {
        u = (user_t*)user_get_by_uid(uid);
        if (u) list[count++] = u;
    }
    return count;
}

static const user_t *users_selected(window_t *w) {
    return (const user_t*)user_get_by_uid(w->um_sel ? w->um_sel : user_current_uid());
}

void app_users_init(window_t *w){
    w->um_sel = user_current_uid();
    w->um_input_name[0] = '\0';
    w->um_input_pass[0] = '\0';
    w->um_field = 0;
    users_set_status(w, user_is_root()
        ? "Root can create, delete, and promote users"
        : "Sign in as root to manage other accounts", COL_DIM);
}

/* Shared layout — must match between draw and click */
#define UM_LIST_HDR   52
#define UM_LIST_START (UM_LIST_HDR + 8)
#define UM_ROW_H      40
#define UM_ROW_GAP     4
#define UM_INFO_Y      12
#define UM_INFO_H     116
#define UM_INFO_ROW    26
#define UM_FORM_Y     (UM_INFO_Y + UM_INFO_H + 20)
#define UM_INPUT_Y    (UM_FORM_Y + 38)
#define UM_BTN_Y      (UM_INPUT_Y + 46)
#define UM_BTN_H       34
#define UM_INPUT_H     36
#define UM_SB_H        26

void app_users_draw(window_t *w){
    rect_t cr = wm_client_rect(w);
    i32 sc = (i32)GFX_FONT_SCALE;
    user_t *list[24];
    const user_t *selected;
    u32 count = users_collect(list, 24);
    i32 list_w = cr.w * 44 / 100;
    i32 det_x  = cr.x + list_w + 1;
    i32 det_w  = cr.w - list_w - 1;
    i32 dpx    = det_x + 14;
    i32 dpw    = det_w - 28;
    char masked[32];
    char last_login[32];
    (void)sc;

    /* Background panels */
    gfx_rect(cr.x, cr.y, cr.w, cr.h, COL_SURFACE);
    gfx_rect(cr.x, cr.y, list_w, cr.h, COL_SURFACE2);
    gfx_vline(cr.x + list_w, cr.y, cr.h, COL_BORDER);

    /* Left header */
    gfx_gradient_rect(cr.x, cr.y, list_w, UM_LIST_HDR, g_theme->surface3, COL_SURFACE2);
    gfx_hline(cr.x, cr.y + UM_LIST_HDR, list_w, COL_BORDER);
    gfx_str_ex(cr.x + 14, cr.y + 10, "User Accounts", COL_TEXT, COL_TRANSPARENT, FONT_H2);
    gfx_str(cr.x + 14, cr.y + 32, "Local account database", g_theme->muted, COL_TRANSPARENT);

    /* User rows */
    for (u32 i = 0; i < count; i++) {
        user_t *u = list[i];
        i32 ry = cr.y + UM_LIST_START + (i32)i * (UM_ROW_H + UM_ROW_GAP);
        bool sel = (u->uid == w->um_sel) || (!w->um_sel && u->uid == user_current_uid());
        gfx_rect_rounded(cr.x + 8, ry, list_w - 16, UM_ROW_H, 8,
                         sel ? COL_SELECTION : g_theme->surface3);
        if (sel) {
            gfx_rect_rounded_outline(cr.x + 8, ry, list_w - 16, UM_ROW_H, 8, COL_PRIMARY);
            gfx_rect_rounded(cr.x + 8, ry + 8, 3, UM_ROW_H - 16, 1, COL_PRIMARY);
        }
        /* Avatar dot */
        gfx_circle_fill(cr.x + 24, ry + UM_ROW_H/2, 9,
                        u->is_root ? g_theme->primary : g_theme->surface2);
        gfx_circle(cr.x + 24, ry + UM_ROW_H/2, 9, COL_BORDER);
        char init[2] = { u->name[0], '\0' };
        gfx_str(cr.x + 20, ry + UM_ROW_H/2 - 5, init, COL_WHITE, COL_TRANSPARENT);

        gfx_str(cr.x + 40, ry + 9, u->name, COL_TEXT, COL_TRANSPARENT);
        gfx_str(cr.x + 40, ry + 23, u->is_root ? "Administrator" : "Standard",
                u->is_root ? COL_ACCENT : g_theme->muted, COL_TRANSPARENT);
    }

    /* Right header */
    selected = users_selected(w);
    if (!selected && count > 0) selected = list[0];

    gfx_gradient_rect(det_x, cr.y, det_w, UM_LIST_HDR, g_theme->surface3, COL_SURFACE);
    gfx_hline(det_x, cr.y + UM_LIST_HDR, det_w, COL_BORDER);
    gfx_str_ex(dpx, cr.y + 10, selected ? selected->name : "No user selected",
               COL_TEXT, COL_TRANSPARENT, FONT_H2);
    if (selected)
        gfx_str(dpx, cr.y + 32, selected->is_root ? "Administrator" : "Standard user",
                selected->is_root ? COL_ACCENT : g_theme->muted, COL_TRANSPARENT);

    if (selected) {
        /* Build last login string */
        last_login[0] = '\0';
        if (selected->last_login_year) {
            char tmp[12];
            char year[8], hour[8], minute[8];
            kutoa(selected->last_login_year, year, 10);
            kutoa(selected->last_login_hour, hour, 10);
            kutoa(selected->last_login_minute, minute, 10);
            kstrcpy(last_login, year); kstrcat(last_login, "-");
            kutoa(selected->last_login_month, tmp, 10);
            if (selected->last_login_month < 10) kstrcat(last_login, "0");
            kstrcat(last_login, tmp); kstrcat(last_login, "-");
            kutoa(selected->last_login_day, tmp, 10);
            if (selected->last_login_day < 10) kstrcat(last_login, "0");
            kstrcat(last_login, tmp); kstrcat(last_login, " ");
            if (selected->last_login_hour < 10) kstrcat(last_login, "0");
            kstrcat(last_login, hour); kstrcat(last_login, ":");
            if (selected->last_login_minute < 10) kstrcat(last_login, "0");
            kstrcat(last_login, minute);
        } else {
            kstrcpy(last_login, "Never recorded");
        }

        /* Info card */
        i32 card_y = cr.y + UM_LIST_HDR + UM_INFO_Y;
        gfx_rect_rounded(dpx, card_y, dpw, UM_INFO_H, 10, g_theme->surface2);
        gfx_rect_rounded_outline(dpx, card_y, dpw, UM_INFO_H, 10, COL_BORDER);

        const char *lbls[] = { "UID", "Role", "Home", "Last login" };
        char uid_buf[12]; kutoa(selected->uid, uid_buf, 10);
        const char *vals[] = { uid_buf,
            selected->is_root ? "Administrator" : "Standard user",
            selected->home, last_login };
        u32 val_cols[] = { COL_TEXT,
            selected->is_root ? COL_ACCENT : COL_TEXT,
            COL_TEXT, g_theme->muted };

        for (int r = 0; r < 4; r++) {
            i32 ry2 = card_y + 6 + r * UM_INFO_ROW;
            gfx_str(dpx + 12, ry2, lbls[r], g_theme->dim, COL_TRANSPARENT);
            gfx_str(dpx + 96,  ry2, vals[r], val_cols[r], COL_TRANSPARENT);
            if (r < 3) gfx_hline(dpx + 8, ry2 + UM_INFO_ROW - 2, dpw - 16, COL_BORDER);
        }

        /* Create user form */
        i32 form_base = cr.y + UM_LIST_HDR;
        gfx_str_ex(dpx, form_base + UM_FORM_Y, "Create User", COL_TEXT, COL_TRANSPARENT, FONT_H2);
        gfx_str(dpx, form_base + UM_FORM_Y + 20, "Password must be strong",
                g_theme->muted, COL_TRANSPARENT);

        /* Inputs */
        i32 inp_w = (dpw - 10) / 2;
        i32 inp_y  = form_base + UM_INPUT_Y;
        textinput_t name_box, pass_box;
        kmemset(&name_box, 0, sizeof(name_box));
        kmemset(&pass_box, 0, sizeof(pass_box));
        name_box.rect = rect_make(dpx, inp_y, inp_w, UM_INPUT_H);
        name_box.focused = (w->um_field == 0);
        kstrcpy(name_box.buf, w->um_input_name);
        name_box.len = (u32)kstrlen(w->um_input_name);
        name_box.cursor = name_box.len;
        kstrcpy(name_box.placeholder, "Username");
        textinput_draw(&name_box);

        users_mask(w->um_input_pass, masked, sizeof(masked));
        pass_box.rect = rect_make(dpx + inp_w + 10, inp_y, inp_w, UM_INPUT_H);
        pass_box.focused = (w->um_field == 1);
        kstrcpy(pass_box.buf, masked);
        pass_box.len = (u32)kstrlen(masked);
        pass_box.cursor = pass_box.len;
        kstrcpy(pass_box.placeholder, "Password");
        textinput_draw(&pass_box);

        /* Buttons — evenly split */
        i32 btn_base = form_base + UM_BTN_Y;
        i32 btn_w    = (dpw - 8) / 3;
        button_t create = { .rect = rect_make(dpx, btn_base, btn_w, UM_BTN_H),
                            .active = true, .bg = COL_PRIMARY, .fg = COL_WHITE };
        button_t admin  = { .rect = rect_make(dpx + btn_w + 4, btn_base, btn_w, UM_BTN_H),
                            .active = false, .bg = g_theme->surface3, .fg = COL_TEXT };
        button_t del    = { .rect = rect_make(dpx + (btn_w + 4)*2, btn_base, btn_w, UM_BTN_H),
                            .active = false, .bg = g_theme->surface3, .fg = COL_TEXT };
        kstrcpy(create.label, "Create");
        kstrcpy(admin.label, selected->is_root ? "Demote" : "Promote");
        kstrcpy(del.label, "Delete");
        button_draw(&create);
        button_draw(&admin);
        button_draw(&del);
    }

    /* Status bar */
    i32 sb_y = cr.y + cr.h - UM_SB_H;
    gfx_rect(det_x, sb_y, det_w, UM_SB_H, g_theme->surface3);
    gfx_hline(det_x, sb_y, det_w, COL_BORDER);
    gfx_str_clipped(dpx, sb_y + (UM_SB_H - FONT_H) / 2,
                    det_w - 20, w->users_status,
                    w->users_status_color ? w->users_status_color : g_theme->dim,
                    COL_TRANSPARENT);
}

void app_users_key(window_t *w, char c){
    char *target = (w->um_field == 0) ? w->um_input_name : w->um_input_pass;
    u32 max = 31;
    if (c == '\t') { w->um_field = 1 - w->um_field; return; }
    if (c == '\n') {
        int rc;
        if (!user_is_root()) {
            users_set_status(w, "Root access is required to create accounts", COL_YELLOW);
            return;
        }
        rc = user_create(w->um_input_name, w->um_input_pass);
        if (rc == 0) {
            users_set_status(w, "User created successfully", COL_GREEN);
            w->um_input_name[0] = '\0';
            w->um_input_pass[0] = '\0';
        } else if (rc == -2) {
            users_set_status(w, "Password must include upper/lowercase letters and a number", COL_YELLOW);
        } else {
            users_set_status(w, "Unable to create user", COL_RED);
        }
        return;
    }
    if (c == '\b') {
        u32 len = (u32)kstrlen(target);
        if (len > 0) target[len - 1] = '\0';
        return;
    }
    if (c >= 32 && c < 127) {
        u32 len = (u32)kstrlen(target);
        if (len < max) { target[len] = c; target[len + 1] = '\0'; }
    }
}

void app_users_click(window_t *w, i32 x, i32 y){
    rect_t cr = wm_client_rect(w);
    user_t *list[24];
    const user_t *selected = users_selected(w);
    u32 count = users_collect(list, 24);
    i32 list_w = cr.w * 44 / 100;
    i32 det_x  = cr.x + list_w + 1;
    i32 det_w  = cr.w - list_w - 1;
    i32 dpx    = det_x + 14;
    i32 dpw    = det_w - 28;

    /* User list rows */
    for (u32 i = 0; i < count; i++) {
        i32 ry = cr.y + UM_LIST_START + (i32)i * (UM_ROW_H + UM_ROW_GAP);
        if (rect_contains(rect_make(cr.x + 8, ry, list_w - 16, UM_ROW_H), x, y)) {
            w->um_sel = list[i]->uid;
            return;
        }
    }

    /* Input fields */
    i32 inp_w  = (dpw - 10) / 2;
    i32 inp_y  = cr.y + UM_LIST_HDR + UM_INPUT_Y;
    if (rect_contains(rect_make(dpx, inp_y, inp_w, UM_INPUT_H), x, y))
        w->um_field = 0;
    if (rect_contains(rect_make(dpx + inp_w + 10, inp_y, inp_w, UM_INPUT_H), x, y))
        w->um_field = 1;

    /* Buttons */
    i32 btn_base = cr.y + UM_LIST_HDR + UM_BTN_Y;
    i32 btn_w    = (dpw - 8) / 3;

    if (rect_contains(rect_make(dpx, btn_base, btn_w, UM_BTN_H), x, y)) {
        app_users_key(w, '\n');
        return;
    }
    if (rect_contains(rect_make(dpx + btn_w + 4, btn_base, btn_w, UM_BTN_H), x, y)) {
        int rc;
        if (!selected || !user_is_root()) {
            users_set_status(w, "Root access is required to change roles", COL_YELLOW);
            return;
        }
        rc = user_set_admin(selected->name, selected->is_root ? false : true);
        if (rc == 0)
            users_set_status(w, selected->is_root ? "User demoted to standard" : "User promoted to administrator", COL_GREEN);
        else
            users_set_status(w, "Unable to change account role", COL_RED);
        return;
    }
    if (rect_contains(rect_make(dpx + (btn_w + 4)*2, btn_base, btn_w, UM_BTN_H), x, y)) {
        int rc;
        if (!selected || !user_is_root()) {
            users_set_status(w, "Root access is required to delete users", COL_YELLOW);
            return;
        }
        rc = user_delete(selected->name);
        if (rc == 0) {
            users_set_status(w, "User deleted", COL_GREEN);
            w->um_sel = user_current_uid();
        } else {
            users_set_status(w, "Unable to delete selected user", COL_RED);
        }
    }
}
