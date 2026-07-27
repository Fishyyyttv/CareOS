/* CareOS v9 -- apps/app_files.c -- File Manager v9 */
#include "apps_common.h"
#include "image.h"
#include "icon.h"

/* fm_mode stored in w->tab: 0=browse, 1=rename_input, 2=new_file_input, 3=new_folder_input */
#define FM_MODE_BROWSE   0
#define FM_MODE_RENAME   1
#define FM_MODE_NEWFILE  2
#define FM_MODE_NEWFOLD  3

#define FM_SB  (88 + 52 * (i32)GFX_FONT_SCALE)
#define FM_TB  (16 + 14 * (i32)GFX_FONT_SCALE)
#define FM_ROW (14 + 10 * (i32)GFX_FONT_SCALE)
#define FM_SB_ITEMS 6

#define FM_HDR (FONT_H * (i32)GFX_FONT_SCALE + 6)
#define FM_STA (FONT_H * (i32)GFX_FONT_SCALE + 7)

static const char *FM_PLACES[FM_SB_ITEMS] = {"/","~","/home","/etc","/usr/bin","/var/log"};

/* Static state for view mode & mouse position & breadcrumbs */
static bool s_grid_view = false;
static i32  s_mouse_x   = -1;
static i32  s_mouse_y   = -1;

#define MAX_BREADCRUMBS 10
typedef struct {
    char name[32];
    char path[64];
    rect_t rect;
} breadcrumb_t;

static breadcrumb_t s_breadcrumbs[MAX_BREADCRUMBS];
static int          s_breadcrumb_count = 0;

static void fm_build_breadcrumbs(const char *cur_path) {
    s_breadcrumb_count = 0;
    
    kstrcpy(s_breadcrumbs[0].name, "/");
    kstrcpy(s_breadcrumbs[0].path, "/");
    s_breadcrumbs[0].rect = rect_make(0, 0, 0, 0);
    s_breadcrumb_count = 1;

    if (!cur_path || kstrcmp(cur_path, "/") == 0 || cur_path[0] == '\0') {
        return;
    }

    char cum_path[64] = "";
    const char *p = cur_path;
    while (*p == '/') p++;

    while (*p && s_breadcrumb_count < MAX_BREADCRUMBS) {
        i32 len = 0;
        while (p[len] != '\0' && p[len] != '/') {
            len++;
        }
        if (len > 0) {
            char name[32];
            i32 copy_len = len < 31 ? len : 31;
            kmemcpy(name, p, (u32)copy_len);
            name[copy_len] = '\0';

            kstrcat(cum_path, "/");
            kstrcat(cum_path, name);

            kstrcpy(s_breadcrumbs[s_breadcrumb_count].name, name);
            kstrcpy(s_breadcrumbs[s_breadcrumb_count].path, cum_path);
            s_breadcrumbs[s_breadcrumb_count].rect = rect_make(0, 0, 0, 0);
            s_breadcrumb_count++;
        }
        p += len;
        while (*p == '/') p++;
    }
}

void app_files_init(window_t *w){
    w->fm_dir = vfs_resolve_path("/home/user");
    if(!w->fm_dir) w->fm_dir = vfs_root();
    w->fm_sel  = 0;
    w->tab     = FM_MODE_BROWSE;
    w->input_buf[0] = '\0';
    w->input_len = 0;
}

/* Hand-drawn 10x10 fallback, used when the icon theme has nothing for a node. */
static void fm_draw_icon_vector(i32 x, i32 y, bool is_dir, u32 color){
    if(is_dir){
        gfx_rect(x,y+2,10,8,color);
        gfx_rect(x,y,5,4,color);
    } else {
        gfx_rect(x,y,10,10,color);
        gfx_rect(x+6,y,4,4,rgb(0x0a,0x0c,0x14)); /* folded corner */
        gfx_hline(x+2,y+4,6,rgb(0x33,0x3a,0x55));
        gfx_hline(x+2,y+6,5,rgb(0x33,0x3a,0x55));
        gfx_hline(x+2,y+8,6,rgb(0x33,0x3a,0x55));
    }
}

static const char *fm_icon_name(const fs_node_t *n){
    if(n->type == FS_DIR) return "folder";
    const char *dot = kstrrchr(n->name, '.');
    if(!dot) return NULL;
    if(kstrcmp(dot,".care")==0) return "packages";
    if(kstrcmp(dot,".cri")==0 || kstrcmp(dot,".bmp")==0 || kstrcmp(dot,".tga")==0)
        return "image-file";
    return NULL;
}

static void fm_draw_icon(i32 x, i32 y, i32 size, const fs_node_t *n, u32 color){
    const char *name = fm_icon_name(n);
    image_t *img = name ? icon_lookup(name, (u32)size) : NULL;
    if(!img && n->type == FS_FILE) img = icon_lookup("text-file", (u32)size);
    if(img){ gfx_draw_image(img, x, y); return; }
    fm_draw_icon_vector(x, y, n->type == FS_DIR, color);
}

void app_files_draw(window_t *w){
    rect_t cr = wm_client_rect(w);
    gfx_rect(cr.x,cr.y,cr.w,cr.h,COL_SURFACE);

    /* -- Sidebar -- */
    gfx_rect(cr.x,cr.y,FM_SB,cr.h,COL_SURFACE2);
    gfx_vline(cr.x+FM_SB,cr.y,cr.h,COL_BORDER);

    i32 sc = (i32)GFX_FONT_SCALE;
    i32 sb_h_h = 10 + 8 * sc;
    /* Sidebar header */
    gfx_rect(cr.x,cr.y,FM_SB,sb_h_h,COL_SURFACE3);
    gfx_str(cr.x+6,cr.y + (sb_h_h - (i32)(FONT_H * sc)) / 2,"Bookmarks",COL_DIM,COL_SURFACE3);
    gfx_hline(cr.x,cr.y+sb_h_h,FM_SB,COL_BORDER);

    char cur_path[64]; vfs_get_path(w->fm_dir,cur_path,sizeof(cur_path));
    if(!cur_path[0]){cur_path[0]='/';cur_path[1]='\0';}

    i32 sb_row_h = 12 + 8 * sc;
    for(int i=0;i<FM_SB_ITEMS;i++){
        i32 py = cr.y + sb_h_h + 4 + i * (sb_row_h + 4);
        const char *pl = FM_PLACES[i];
        /* Resolve ~ for display */
        char resolved[64]; kstrcpy(resolved, pl);
        if(kstrcmp(pl,"~")==0){
            kstrcpy(resolved, user_current_uid()==0?"/root":"/home/user");
        }
        bool active = (kstrcmp(cur_path,resolved)==0);
        bool sb_hover = (s_mouse_x >= cr.x + 2 && s_mouse_x < cr.x + FM_SB - 2 &&
                         s_mouse_y >= py && s_mouse_y < py + sb_row_h);
        u32 bg = active ? COL_SELECTION : (sb_hover ? COL_SURFACE3 : COL_SURFACE2);
        gfx_rect(cr.x+2,py,FM_SB-4,sb_row_h,bg);
        if(active) gfx_rect(cr.x,py,3,sb_row_h,COL_PRIMARY);
        else if(sb_hover) gfx_rect(cr.x,py,2,sb_row_h,COL_ACCENT);

        gfx_set_clip(cr.x+8, py, FM_SB-12, sb_row_h);
        gfx_str(cr.x+8,py + (sb_row_h - (i32)(FONT_H * sc)) / 2,pl,active?COL_TEXT:(sb_hover?COL_TEXT:COL_DIM),bg);
        gfx_clear_clip();
    }

    /* -- Toolbar -- */
    i32 tx = cr.x+FM_SB;
    i32 list_rw = (cr.w - FM_SB) * 62 / 100;   /* file list width; rest is preview pane */
    gfx_rect(tx,cr.y,cr.w-FM_SB,FM_TB,COL_SURFACE3);
    gfx_hline(tx,cr.y+FM_TB,cr.w-FM_SB,COL_BORDER);

    /* Up button */
    bool can_up = w->fm_dir && w->fm_dir->parent;
    u32 upc = can_up ? COL_TEXT : COL_MUTED;
    i32 btn_w_up = 30 + 10 * sc;
    bool up_hover = (s_mouse_x >= tx + 2 && s_mouse_x < tx + 2 + btn_w_up &&
                     s_mouse_y >= cr.y + 3 && s_mouse_y < cr.y + FM_TB - 3);
    u32 up_bg = up_hover ? COL_SURFACE : COL_SURFACE2;
    gfx_rect_rounded(tx+2,cr.y+3,btn_w_up,FM_TB-6,3,up_bg);
    if (up_hover) gfx_rect_rounded_outline(tx+2,cr.y+3,btn_w_up,FM_TB-6,3,COL_PRIMARY);
    gfx_str(tx+7,cr.y + (FM_TB - (i32)(FONT_H * sc)) / 2,"^ Up",upc,up_bg);

    /* Action buttons */
    struct { const char *lbl; u32 color; } btns[]={
        {"+File",COL_GREEN},{"+Dir",COL_PRIMARY},{"Del",COL_RED},{"Ren",COL_YELLOW},{"Open",COL_ACCENT}
    };
    i32 btn_w_act = 40 + 12 * sc;
    for(int i=0;i<5;i++){
        i32 bx = tx + btn_w_up + 6 + i * (btn_w_act + 4);
        bool act_hover = (s_mouse_x >= bx && s_mouse_x < bx + btn_w_act &&
                          s_mouse_y >= cr.y + 3 && s_mouse_y < cr.y + FM_TB - 3);
        u32 bg_act = act_hover ? COL_SURFACE : COL_SURFACE2;
        gfx_rect_rounded(bx,cr.y+3,btn_w_act,FM_TB-6,3,bg_act);
        if (act_hover) gfx_rect_rounded_outline(bx,cr.y+3,btn_w_act,FM_TB-6,3,btns[i].color);
        gfx_set_clip(bx + 2, cr.y + 3, btn_w_act - 4, FM_TB - 6);
        gfx_str_centered(bx,cr.y + (FM_TB - (i32)(FONT_H * sc)) / 2,btn_w_act,btns[i].lbl,btns[i].color,bg_act);
        gfx_clear_clip();
    }

    /* View Toggle button ([List] / [Grid]) */
    i32 bx_view = tx + btn_w_up + 6 + 5 * (btn_w_act + 4);
    i32 btn_w_view = 42 + 10 * sc;
    bool view_hover = (s_mouse_x >= bx_view && s_mouse_x < bx_view + btn_w_view &&
                       s_mouse_y >= cr.y + 3 && s_mouse_y < cr.y + FM_TB - 3);
    const char *view_lbl = s_grid_view ? "Grid" : "List";
    u32 view_bg = s_grid_view ? COL_PRIMARY : (view_hover ? COL_SURFACE : COL_SURFACE2);
    u32 view_fg = s_grid_view ? COL_WHITE : (view_hover ? COL_ACCENT : COL_TEXT);
    gfx_rect_rounded(bx_view, cr.y + 3, btn_w_view, FM_TB - 6, 3, view_bg);
    if (view_hover || s_grid_view) gfx_rect_rounded_outline(bx_view, cr.y + 3, btn_w_view, FM_TB - 6, 3, COL_ACCENT);
    gfx_str_centered(bx_view, cr.y + (FM_TB - (i32)(FONT_H * sc)) / 2, btn_w_view, view_lbl, view_fg, view_bg);

    /* Path breadcrumb navigation bar (after View Toggle) */
    i32 path_x = bx_view + btn_w_view + 8;
    i32 path_w = cr.w - FM_SB - (path_x - tx) - 8;
    if (path_w > 20) {
        fm_build_breadcrumbs(cur_path);
        gfx_set_clip(path_x, cr.y + 3, path_w, FM_TB - 6);
        i32 cur_bc_x = path_x;
        i32 bc_h = FM_TB - 6;
        for (int b = 0; b < s_breadcrumb_count; b++) {
            i32 text_w = gfx_str_width(s_breadcrumbs[b].name);
            i32 item_w = text_w + 10;
            if (item_w < 18) item_w = 18;
            bool is_last = (b == s_breadcrumb_count - 1);
            
            s_breadcrumbs[b].rect = rect_make(cur_bc_x, cr.y + 3, item_w, bc_h);
            bool bc_hover = (s_mouse_x >= cur_bc_x && s_mouse_x < cur_bc_x + item_w &&
                             s_mouse_y >= cr.y + 3 && s_mouse_y < cr.y + FM_TB - 3);

            u32 bg_col = is_last ? COL_ACCENT : (bc_hover ? COL_SURFACE : COL_SURFACE2);
            u32 fg_col = is_last ? COL_WHITE : (bc_hover ? COL_TEXT : COL_DIM);

            gfx_rect_rounded(cur_bc_x, cr.y + 3, item_w, bc_h, 3, bg_col);
            if (bc_hover && !is_last) gfx_rect_rounded_outline(cur_bc_x, cr.y + 3, item_w, bc_h, 3, COL_PRIMARY);
            
            gfx_str_centered(cur_bc_x, cr.y + (FM_TB - (i32)(FONT_H * sc)) / 2, item_w, s_breadcrumbs[b].name, fg_col, bg_col);
            cur_bc_x += item_w + 3;

            if (!is_last) {
                gfx_str(cur_bc_x, cr.y + (FM_TB - (i32)(FONT_H * sc)) / 2, ">", COL_MUTED, COL_SURFACE3);
                cur_bc_x += 10;
            }
        }
        gfx_clear_clip();
    } else {
        s_breadcrumb_count = 0;
    }

    /* -- Input bar (rename / new name) -- */
    if(w->tab != FM_MODE_BROWSE){
        const char *prompt = (w->tab==FM_MODE_RENAME) ? "Rename: " :
                             (w->tab==FM_MODE_NEWFILE) ? "New file: " : "New folder: ";
        i32 iy = cr.y+cr.h-24;
        gfx_rect(tx,iy,cr.w-FM_SB,24,COL_INPUT_BG);
        gfx_hline(tx,iy,cr.w-FM_SB,COL_BORDER);
        i32 pw = gfx_str_width(prompt);
        gfx_str(tx+4,iy+6,prompt,COL_YELLOW,COL_INPUT_BG);
        gfx_set_clip(tx+4+pw, iy, cr.w-FM_SB-pw-8, 24);
        gfx_str(tx+4+pw,iy+6,w->input_buf,COL_TEXT,COL_INPUT_BG);
        /* Cursor */
        if((timer_get_ticks()/30)%2==0){
            i32 cx=tx+4+pw+(i32)w->input_len*FONT_W;
            gfx_vline(cx,iy+4,16,COL_TEXT);
        }
        gfx_clear_clip();
    }

    /* -- File list -- */
    if(!w->fm_dir){
        gfx_str(tx+8,cr.y+FM_TB+8,"(no directory)",COL_MUTED,COL_SURFACE);
        return;
    }

    /* Column headers */
    i32 list_y = cr.y + FM_TB;
    gfx_rect(tx,list_y,cr.w-FM_SB,FM_HDR,COL_SURFACE3);
    if (s_grid_view) {
        gfx_str(tx + 12, list_y + 3, "Gallery / Grid View", COL_ACCENT, COL_SURFACE3);
        char item_count_str[32];
        u32 total_items = w->fm_dir ? w->fm_dir->child_count : 0;
        kutoa(total_items, item_count_str, 10);
        kstrcat(item_count_str, " items");
        gfx_str_right(tx, list_y + 3, list_rw - 8, item_count_str, COL_DIM, COL_SURFACE3);
    } else {
        gfx_str(tx+24,list_y+3,"Name",COL_DIM,COL_SURFACE3);
        gfx_str_right(tx,list_y+3,list_rw-8,"Size",COL_DIM,COL_SURFACE3);
    }
    gfx_hline(tx,list_y+FM_HDR,cr.w-FM_SB,COL_BORDER);
    list_y += FM_HDR;

    i32 list_bottom = cr.y+cr.h - (w->tab!=FM_MODE_BROWSE ? 24 : 0) - FM_STA;

    if (s_grid_view) {
        /* Grid / Gallery View */
        i32 cw = 76;
        i32 ch = 76;
        i32 cols = (list_rw - 8) / cw;
        if (cols < 1) cols = 1;

        for (u32 i = 0; i < w->fm_dir->child_count; i++) {
            i32 col = (i32)i % cols;
            i32 row = (i32)i / cols;
            i32 gx = tx + 6 + col * cw;
            i32 gy = list_y + 6 + row * ch;
            if (gy + ch > list_bottom) break;

            fs_node_t *child = w->fm_dir->children[i];
            bool sel = (i == w->fm_sel);
            bool is_hover = (s_mouse_x >= gx && s_mouse_x < gx + cw - 4 &&
                             s_mouse_y >= gy && s_mouse_y < gy + ch - 4);
            
            char child_path[64]; vfs_get_path(child, child_path, sizeof(child_path));
            bool is_cut = (g_clipboard_is_cut && g_clipboard_len > 0 && kstrcmp(child_path, g_clipboard) == 0);

            u32 card_bg = sel ? COL_SELECTION : (is_hover ? COL_SURFACE3 : COL_SURFACE2);
            u32 card_border = sel ? COL_PRIMARY : (is_hover ? COL_ACCENT : COL_BORDER);

            gfx_rect_rounded(gx, gy, cw - 4, ch - 4, 6, card_bg);
            gfx_rect_rounded_outline(gx, gy, cw - 4, ch - 4, 6, card_border);

            bool is_dir = child->type == FS_DIR;
            u32 icol = is_cut ? COL_MUTED : (is_dir ? COL_PRIMARY : (sel ? COL_ACCENT : COL_TEXT));

            /* 24px icon centered */
            fm_draw_icon(gx + (cw - 4 - 24) / 2, gy + 8, 24, child, icol);

            /* File/Folder Name centered below icon */
            gfx_set_clip(gx + 2, gy + 38, cw - 8, 16);
            gfx_str_centered(gx + 2, gy + 38, cw - 8, child->name, icol, card_bg);
            gfx_clear_clip();

            /* Subtext: size or DIR */
            if (is_dir) {
                gfx_str_centered(gx + 2, gy + 54, cw - 8, "DIR", COL_PRIMARY, card_bg);
            } else {
                char sz[16];
                if (child->size >= 1024) { kutoa(child->size / 1024, sz, 10); kstrcat(sz, "K"); }
                else { kutoa(child->size, sz, 10); kstrcat(sz, "B"); }
                gfx_str_centered(gx + 2, gy + 54, cw - 8, sz, COL_DIM, card_bg);
            }
        }
    } else {
        /* Standard List View */
        i32 fy = list_y;
        for(u32 i=0;i<w->fm_dir->child_count && fy<list_bottom; i++){
            fs_node_t *child = w->fm_dir->children[i];
            bool sel = (i == w->fm_sel);
            bool is_hover = (s_mouse_x >= tx + 1 && s_mouse_x < tx + list_rw - 1 &&
                             s_mouse_y >= fy && s_mouse_y < fy + FM_ROW);

            u32 bg = sel ? COL_SELECTION : (is_hover ? COL_SURFACE3 : (i%2==0?COL_SURFACE:COL_SURFACE2));
            /* Dim cut file */
            char child_path[64]; vfs_get_path(child, child_path, sizeof(child_path));
            bool is_cut = (g_clipboard_is_cut && g_clipboard_len > 0 && kstrcmp(child_path, g_clipboard) == 0);

            gfx_rect(tx+1,fy,list_rw-2,FM_ROW,bg);
            if(sel) gfx_rect(tx+1,fy,3,FM_ROW,COL_PRIMARY);
            else if(is_hover) gfx_rect(tx+1,fy,2,FM_ROW,COL_ACCENT);

            bool is_dir = child->type==FS_DIR;
            u32 icol = is_cut ? COL_MUTED : (is_dir ? COL_PRIMARY : (sel ? COL_ACCENT : COL_TEXT));
            
            fm_draw_icon(tx+2,fy+(FM_ROW-16)/2,16,child,icol);

            gfx_str_clipped(tx+20,fy+4,list_rw-90,child->name,icol,bg);

            if(child->type==FS_FILE){
                char sz[16];
                if(child->size>=1024){ kutoa(child->size/1024,sz,10); kstrcat(sz,"K"); }
                else { kutoa(child->size,sz,10); kstrcat(sz,"B"); }
                gfx_str_right(tx,fy+4,list_rw-8,sz,COL_DIM,bg);
            } else {
                gfx_str_right(tx,fy+4,list_rw-8,"DIR",COL_PRIMARY,bg);
            }

            fy += FM_ROW;
        }
    }

    /* -- Preview pane -- */
    {
        i32 px = tx + list_rw;
        i32 pw = cr.w - FM_SB - list_rw;
        i32 pane_y = cr.y + FM_TB + FM_HDR;
        i32 pane_h = cr.h - FM_TB - FM_HDR - FM_STA;
        
        bool pane_hover = (s_mouse_x >= px && s_mouse_x < px + pw &&
                           s_mouse_y >= pane_y && s_mouse_y < pane_y + pane_h);

        gfx_vline(px, pane_y, pane_h, COL_BORDER);
        px++;
        pw--;

        u32 pane_bg = pane_hover ? COL_SURFACE3 : COL_SURFACE2;
        gfx_rect(px, pane_y, pw, pane_h, pane_bg);

        if (pane_hover) {
            gfx_rect_outline(px, pane_y, pw, pane_h, COL_ACCENT);
        }

        if (w->fm_dir && w->fm_sel < w->fm_dir->child_count) {
            fs_node_t *sel_node = w->fm_dir->children[w->fm_sel];
            bool is_dir = sel_node->type == FS_DIR;

            /* Node Header Card inside Preview Pane */
            gfx_rect_rounded(px + 4, pane_y + 4, pw - 8, 52, 4, COL_SURFACE);
            gfx_rect_rounded_outline(px + 4, pane_y + 4, pw - 8, 52, 4, pane_hover ? COL_PRIMARY : COL_BORDER);

            fm_draw_icon(px + 10, pane_y + 12, 16, sel_node,
                         is_dir ? COL_PRIMARY : COL_ACCENT);
            
            gfx_set_clip(px + 32, pane_y + 8, pw - 44, 22);
            gfx_str(px + 32, pane_y + 12, sel_node->name, COL_TEXT, COL_SURFACE);
            gfx_clear_clip();

            if (is_dir) {
                char dcnt[32];
                kutoa(sel_node->child_count, dcnt, 10);
                kstrcat(dcnt, " item");
                if (sel_node->child_count != 1) kstrcat(dcnt, "s");
                gfx_str_clipped(px + 10, pane_y + 34, pw - 20, dcnt, COL_DIM, COL_SURFACE);

                /* Folder preview body */
                gfx_str(px + 8, pane_y + 64, "Directory contents:", COL_ACCENT, pane_bg);
                i32 sub_y = pane_y + 82;
                i32 max_sub = (pane_h - 90) / ((i32)(FONT_H * GFX_FONT_SCALE) + 4);
                if (max_sub < 1) max_sub = 1;
                for (u32 c = 0; c < sel_node->child_count && (i32)c < max_sub; c++) {
                    fs_node_t *cn = sel_node->children[c];
                    bool c_dir = cn->type == FS_DIR;
                    fm_draw_icon(px + 10, sub_y, 16, cn, c_dir ? COL_PRIMARY : COL_TEXT);
                    gfx_str_clipped(px + 30, sub_y + 2, pw - 40, cn->name, c_dir ? COL_PRIMARY : COL_TEXT, pane_bg);
                    sub_y += (i32)(FONT_H * GFX_FONT_SCALE) + 4;
                }
            } else {
                char sz[32];
                if (sel_node->size >= 1024) { kutoa(sel_node->size / 1024, sz, 10); kstrcat(sz, " KB"); }
                else { kutoa(sel_node->size, sz, 10); kstrcat(sz, " B"); }
                gfx_str_clipped(px + 10, pane_y + 34, pw - 20, sz, COL_DIM, COL_SURFACE);

                /* Hover status badge */
                if (pane_hover) {
                    gfx_str_right(px, pane_y + 34, pw - 10, "[Preview Active]", COL_GREEN, COL_SURFACE);
                }

                gfx_hline(px + 4, pane_y + 60, pw - 8, COL_BORDER);

                /* File content preview box */
                if (sel_node->size > 0 && sel_node->data) {
                    gfx_rect(px + 4, pane_y + 66, pw - 8, pane_h - 72, COL_INPUT_BG);
                    gfx_rect_outline(px + 4, pane_y + 66, pw - 8, pane_h - 72, pane_hover ? COL_ACCENT : COL_BORDER);

                    gfx_set_clip(px + 8, pane_y + 70, pw - 16, pane_h - 80);
                    char preview[256];
                    u32 plen = sel_node->size < 255u ? sel_node->size : 255u;
                    kmemcpy(preview, sel_node->data, plen);
                    preview[plen] = '\0';
                    const char *pp = preview;
                    i32 line_y = pane_y + 70;
                    i32 line_h = (i32)(FONT_H * GFX_FONT_SCALE) + 2;
                    i32 max_chars = (pw - 16) / (FONT_W * (i32)GFX_FONT_SCALE);
                    if (max_chars < 1) max_chars = 1;

                    while (*pp && line_y < pane_y + pane_h - 10) {
                        const char *end = pp;
                        i32 nc = 0;
                        while (*end && *end != '\n' && nc < max_chars) { end++; nc++; }
                        char line[64];
                        if (nc > 63) nc = 63;
                        kmemcpy(line, pp, (u32)nc); line[nc] = '\0';
                        gfx_str(px + 8, line_y, line, COL_TEXT, COL_INPUT_BG);
                        line_y += line_h;
                        if (*end == '\n') pp = end + 1;
                        else if (*end) { pp = end; break; }
                        else break;
                    }
                    gfx_clear_clip();
                } else {
                    gfx_str(px + 8, pane_y + 70, "(empty file)", COL_MUTED, pane_bg);
                }
            }
        } else {
            gfx_str(px + 8, pane_y + 20, "Select a file", COL_MUTED, pane_bg);
            gfx_str(px + 8, pane_y + 20 + (i32)(FONT_H * GFX_FONT_SCALE) + 2,
                    "to preview", COL_MUTED, pane_bg);
        }
    }

    /* Empty folder message */
    if(w->fm_dir->child_count == 0)
        gfx_str(tx+8,list_y+8,"(empty folder)",COL_MUTED,COL_SURFACE);

    /* -- Status bar -- */
    i32 sb_y = cr.y+cr.h-FM_STA;
    gfx_rect(tx,sb_y,cr.w-FM_SB,FM_STA,COL_SURFACE3);
    gfx_hline(tx,sb_y,cr.w-FM_SB,COL_BORDER);
    char status[64];
    char cnt[8]; kutoa(w->fm_dir->child_count,cnt,10);
    kstrcpy(status,cnt); kstrcat(status," item");
    if(w->fm_dir->child_count!=1) kstrcat(status,"s");
    if(w->fm_sel < w->fm_dir->child_count){
        kstrcat(status,"  |  "); kstrcat(status,w->fm_dir->children[w->fm_sel]->name);
    }
    gfx_set_clip(tx+6, sb_y, (cr.w-FM_SB)/2, FM_STA);
    gfx_str(tx+6,sb_y+4,status,COL_DIM,COL_SURFACE3);
    gfx_clear_clip();
    /* Hint */
    gfx_set_clip(tx + (cr.w-FM_SB)/2, sb_y, (cr.w-FM_SB)/2 - 4, FM_STA);
    gfx_str_right(tx,sb_y+4,cr.w-FM_SB-4,"^C=copy  ^X=cut  ^V=paste  Del=delete  r=rename",COL_MUTED,COL_SURFACE3);
    gfx_clear_clip();
}

void app_files_key(window_t *w, char c){
    if(!w->fm_dir) return;

    if(w->tab != FM_MODE_BROWSE){
        /* Input mode -- build name string */
        if(c=='\n'){
            if(w->input_len > 0){
                if(w->tab==FM_MODE_RENAME && w->fm_sel < w->fm_dir->child_count){
                    vfs_rename(w->fm_dir->children[w->fm_sel], w->input_buf);
                } else if(w->tab==FM_MODE_NEWFILE){
                    vfs_mkfile(w->fm_dir, w->input_buf);
                } else if(w->tab==FM_MODE_NEWFOLD){
                    vfs_mkdir(w->fm_dir, w->input_buf);
                }
            }
            w->tab = FM_MODE_BROWSE;
            w->input_buf[0] = '\0'; w->input_len = 0;
        } else if(c=='\b'){
            if(w->input_len>0){ w->input_len--; w->input_buf[w->input_len]='\0'; }
        } else if(c=='\x1B'){
            w->tab = FM_MODE_BROWSE;
            w->input_buf[0]='\0'; w->input_len=0;
        } else if(w->input_len < 63 && c>=32 && c<127){
            w->input_buf[w->input_len++]=c;
            w->input_buf[w->input_len]='\0';
        }
        return;
    }

    /* Browse mode keyboard shortcuts */
    u32 cnt = w->fm_dir->child_count;
    switch(c){
    case '\x41': /* arrow up (escape seq handled simple: 'A') */
    case 'k':
        if(w->fm_sel>0) w->fm_sel--; break;
    case '\x42': /* arrow down */
    case 'j':
        if(cnt>0 && w->fm_sel+1<cnt) w->fm_sel++; break;
    case '\n': /* Enter -- navigate into dir */
        if(cnt>0 && w->fm_sel<cnt){
            fs_node_t *ch=w->fm_dir->children[w->fm_sel];
            if(ch->type==FS_DIR){ w->fm_dir=ch; w->fm_sel=0; }
        }
        break;
    case '\b': /* Backspace -- go up */
        if(w->fm_dir->parent){ w->fm_dir=w->fm_dir->parent; w->fm_sel=0; }
        break;
    case '\x7F': /* Delete */
    case 'd':
        if(cnt>0 && w->fm_sel<cnt){
            vfs_delete(w->fm_dir->children[w->fm_sel]);
            if(w->fm_sel>=w->fm_dir->child_count && w->fm_sel>0) w->fm_sel--;
        }
        break;
    case '\x12': /* Ctrl+R or F2 (simplified) -- rename */
    case 'r':
        if(cnt>0 && w->fm_sel<cnt){
            kstrncpy(w->input_buf, w->fm_dir->children[w->fm_sel]->name, 63);
            w->input_len = kstrlen(w->input_buf);
            w->tab = FM_MODE_RENAME;
        }
        break;
    case 'n': /* new file */
        w->input_buf[0]='\0'; w->input_len=0;
        w->tab = FM_MODE_NEWFILE; break;
    case 'm': /* new folder */
        w->input_buf[0]='\0'; w->input_len=0;
        w->tab = FM_MODE_NEWFOLD; break;
    case 0x03: /* Ctrl+C: Copy file */
        if (cnt > 0 && w->fm_sel < cnt) {
            fs_node_t *ch = w->fm_dir->children[w->fm_sel];
            vfs_get_path(ch, g_clipboard, CLIPBOARD_SIZE-1);
            g_clipboard[CLIPBOARD_SIZE-1] = '\0';
            g_clipboard_len = kstrlen(g_clipboard);
            g_clipboard_is_cut = false;
            notify_push("Files", "Copied", COL_ACCENT);
        }
        break;
    case 0x18: /* Ctrl+X: Cut file */
        if (cnt > 0 && w->fm_sel < cnt) {
            fs_node_t *ch = w->fm_dir->children[w->fm_sel];
            vfs_get_path(ch, g_clipboard, CLIPBOARD_SIZE-1);
            g_clipboard[CLIPBOARD_SIZE-1] = '\0';
            g_clipboard_len = kstrlen(g_clipboard);
            g_clipboard_is_cut = true;
            notify_push("Files", "Cut (^V to move)", COL_YELLOW);
        }
        break;
    case 0x16: /* Ctrl+V: Paste */
        if (g_clipboard_len > 0) {
            fs_node_t *src = vfs_resolve_path(g_clipboard);
            if (src) {
                const char *fname = kstrrchr(g_clipboard, '/');
                if (fname) fname++; else fname = g_clipboard;
                if (vfs_copy(src, w->fm_dir, fname) == 0) {
                    if (g_clipboard_is_cut) {
                        vfs_delete(src);
                        g_clipboard_len = 0;
                        g_clipboard_is_cut = false;
                    }
                    notify_push("Files", "Pasted", g_theme->success);
                } else {
                    notify_push("Files", "Paste failed", g_theme->error);
                }
            } else {
                notify_push("Files", "Source not found", g_theme->error);
            }
        }
        break;
    default: break;
    }
}

void app_files_click(window_t *w, i32 x, i32 y, mouse_t *m){
    (void)m;
    s_mouse_x = x;
    s_mouse_y = y;

    rect_t cr = wm_client_rect(w);
    i32 sc = (i32)GFX_FONT_SCALE;
    i32 sb_h_h = 10 + 8 * sc;
    i32 sb_row_h = 12 + 8 * sc;

    /* Sidebar click */
    if(x < cr.x + FM_SB){
        int idx = (y - (cr.y + sb_h_h + 4)) / (sb_row_h + 4);
        if(idx>=0 && idx<FM_SB_ITEMS){
            const char *p = FM_PLACES[idx];
            if(kstrcmp(p,"~")==0) p = user_current_uid()==0?"/root":"/home/user";
            fs_node_t *d = vfs_resolve_path(p);
            if(d){ w->fm_dir=d; w->fm_sel=0; }
        }
        return;
    }

    /* -- Toolbar click -- */
    if(y >= cr.y && y < cr.y + FM_TB){
        i32 rx = x - (cr.x + FM_SB);
        i32 btn_w_up = 30 + 10 * sc;
        i32 btn_w_act = 40 + 12 * sc;
        i32 btn_w_view = 42 + 10 * sc;

        /* Up */
        if(rx>=2 && rx<2+btn_w_up && w->fm_dir && w->fm_dir->parent){
            w->fm_dir=w->fm_dir->parent; w->fm_sel=0; return;
        }
        /* Action buttons */
        i32 bx_start = btn_w_up + 6;
        for(int i=0; i<5; i++){
            i32 bx = bx_start + i * (btn_w_act + 4);
            if(rx >= bx && rx < bx + btn_w_act){
                if(i==0){ w->input_buf[0]='\0'; w->input_len=0; w->tab=FM_MODE_NEWFILE; }
                else if(i==1){ w->input_buf[0]='\0'; w->input_len=0; w->tab=FM_MODE_NEWFOLD; }
                else if(i==2 && w->fm_dir && w->fm_sel < w->fm_dir->child_count){
                    vfs_delete(w->fm_dir->children[w->fm_sel]);
                    if(w->fm_sel>=w->fm_dir->child_count && w->fm_sel>0) w->fm_sel--;
                }
                else if(i==3 && w->fm_dir && w->fm_sel < w->fm_dir->child_count){
                    kstrncpy(w->input_buf, w->fm_dir->children[w->fm_sel]->name, 63);
                    w->input_len = kstrlen(w->input_buf);
                    w->tab = FM_MODE_RENAME;
                }
                else if(i==4 && w->fm_dir && w->fm_sel < w->fm_dir->child_count){
                    fs_node_t *ch = w->fm_dir->children[w->fm_sel];
                    if(ch->type==FS_FILE){
                        window_t *ew = wm_open(APP_EDITOR,"Editor",
                            60,40,(i32)SCREEN_W*65/100,(i32)SCREEN_H*70/100);
                        if(ew){
                            kstrncpy(ew->editor_path, ch->name, FS_PATH_MAX-1);
                            win_clear(ew);
                            if(ch->size>0) win_append(ew,vfs_file_str(ch));
                            ew->editor_modified=false;
                        }
                    }
                }
                return;
            }
        }

        /* View Toggle button click */
        i32 bx_view = bx_start + 5 * (btn_w_act + 4);
        if(rx >= bx_view && rx < bx_view + btn_w_view){
            s_grid_view = !s_grid_view;
            return;
        }

        /* Breadcrumb clicks */
        for(int b = 0; b < s_breadcrumb_count; b++){
            rect_t r = s_breadcrumbs[b].rect;
            if(r.w > 0 && rect_contains(r, x, y)){
                fs_node_t *d = vfs_resolve_path(s_breadcrumbs[b].path);
                if(d){
                    w->fm_dir = d;
                    w->fm_sel = 0;
                    return;
                }
            }
        }
        return;
    }

    /* -- File list / Grid view click -- */
    i32 list_rw_c = (cr.w - FM_SB) * 62 / 100;
    i32 list_top = cr.y + FM_TB + FM_HDR;
    i32 list_bottom_c = cr.y + cr.h - (w->tab != FM_MODE_BROWSE ? 24 : 0) - FM_STA;

    if (y >= list_top && y < list_bottom_c && x >= cr.x + FM_SB && x < cr.x + FM_SB + list_rw_c) {
        if (!w->fm_dir) return;

        u32 target_idx = (u32)-1;
        if (s_grid_view) {
            i32 cw = 76;
            i32 ch = 76;
            i32 cols = (list_rw_c - 8) / cw;
            if (cols < 1) cols = 1;
            i32 rel_x = x - (cr.x + FM_SB + 6);
            i32 rel_y = y - (list_top + 6);
            if (rel_x >= 0 && rel_y >= 0) {
                i32 col = rel_x / cw;
                i32 row = rel_y / ch;
                if (col < cols) {
                    u32 idx = (u32)(row * cols + col);
                    if (idx < w->fm_dir->child_count) {
                        target_idx = idx;
                    }
                }
            }
        } else {
            u32 row = (u32)(y - list_top) / FM_ROW;
            if (row < w->fm_dir->child_count) {
                target_idx = row;
            }
        }

        if (target_idx != (u32)-1 && target_idx < w->fm_dir->child_count) {
            if (w->fm_sel == target_idx) {
                /* Second click = navigate or open file */
                fs_node_t *child = w->fm_dir->children[target_idx];
                if (child->type == FS_DIR) {
                    w->fm_dir = child;
                    w->fm_sel = 0;
                } else {
                    /* Open text file in editor */
                    window_t *ew = wm_open(APP_EDITOR, "Editor",
                        60, 40, (i32)SCREEN_W * 65 / 100, (i32)SCREEN_H * 70 / 100);
                    if (ew) {
                        kstrncpy(ew->editor_path, child->name, FS_PATH_MAX - 1);
                        win_clear(ew);
                        if (child->size > 0) win_append(ew, vfs_file_str(child));
                        ew->editor_modified = false;
                    }
                }
            }
            w->fm_sel = target_idx;
        }
    }
}
