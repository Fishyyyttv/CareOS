/* CareOS v9 -- apps/app_files.c -- File Manager v9 */
#include "apps_common.h"

/* fm_mode stored in w->tab: 0=browse, 1=rename_input, 2=new_file_input, 3=new_folder_input */
#define FM_MODE_BROWSE   0
#define FM_MODE_RENAME   1
#define FM_MODE_NEWFILE  2
#define FM_MODE_NEWFOLD  3

#define FM_SB  128   /* sidebar width */
#define FM_TB  26    /* toolbar height */
#define FM_ROW 20    /* row height */
#define FM_SB_ITEMS 6

static const char *FM_PLACES[FM_SB_ITEMS] = {"/","~","/home","/etc","/usr/bin","/var/log"};

void app_files_init(window_t *w){
    w->fm_dir = vfs_resolve_path("/home/user");
    if(!w->fm_dir) w->fm_dir = vfs_root();
    w->fm_sel  = 0;
    w->tab     = FM_MODE_BROWSE;
    w->input_buf[0] = '\0';
    w->input_len = 0;
}

/* Draw small folder or file icon (8x8) */
static void fm_draw_icon(i32 x, i32 y, bool is_dir, u32 color){
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

void app_files_draw(window_t *w){
    rect_t cr = wm_client_rect(w);
    gfx_rect(cr.x,cr.y,cr.w,cr.h,COL_SURFACE);

    /* -- Sidebar -- */
    gfx_rect(cr.x,cr.y,FM_SB,cr.h,COL_SURFACE2);
    gfx_vline(cr.x+FM_SB,cr.y,cr.h,COL_BORDER);

    /* Sidebar header */
    gfx_rect(cr.x,cr.y,FM_SB,18,COL_SURFACE3);
    gfx_str(cr.x+6,cr.y+4,"Bookmarks",COL_DIM,COL_SURFACE3);
    gfx_hline(cr.x,cr.y+18,FM_SB,COL_BORDER);

    char cur_path[64]; vfs_get_path(w->fm_dir,cur_path,sizeof(cur_path));
    if(!cur_path[0]){cur_path[0]='/';cur_path[1]='\0';}

    for(int i=0;i<FM_SB_ITEMS;i++){
        i32 py = cr.y+20+i*22;
        const char *pl = FM_PLACES[i];
        /* Resolve ~ for display */
        char resolved[64]; kstrcpy(resolved, pl);
        if(kstrcmp(pl,"~")==0){
            kstrcpy(resolved, user_current_uid()==0?"/root":"/home/user");
        }
        bool active = (kstrcmp(cur_path,resolved)==0);
        u32 bg = active ? COL_SELECTION : COL_SURFACE2;
        gfx_rect(cr.x+2,py,FM_SB-4,18,bg);
        if(active) gfx_rect(cr.x,py,3,18,COL_PRIMARY);
        gfx_str(cr.x+8,py+4,pl,active?COL_TEXT:COL_DIM,bg);
    }

    /* -- Toolbar -- */
    i32 tx = cr.x+FM_SB;
    gfx_rect(tx,cr.y,cr.w-FM_SB,FM_TB,COL_SURFACE3);
    gfx_hline(tx,cr.y+FM_TB,cr.w-FM_SB,COL_BORDER);

    /* Up button */
    bool can_up = w->fm_dir && w->fm_dir->parent;
    u32 upc = can_up ? COL_TEXT : COL_MUTED;
    gfx_rect_rounded(tx+2,cr.y+3,28,FM_TB-6,3,COL_SURFACE2);
    gfx_str(tx+7,cr.y+7,"^ Up",upc,COL_SURFACE2);

    /* Action buttons */
    struct { const char *lbl; u32 color; } btns[]={
        {"+File",COL_GREEN},{"+Dir",COL_PRIMARY},{"Del",COL_RED},{"Ren",COL_YELLOW},{"Open",COL_ACCENT}
    };
    for(int i=0;i<5;i++){
        i32 bx = tx+34+i*52;
        gfx_rect_rounded(bx,cr.y+3,48,FM_TB-6,3,COL_SURFACE2);
        gfx_str_centered(bx,cr.y+7,48,btns[i].lbl,btns[i].color,COL_SURFACE2);
    }

    /* Path breadcrumb (after buttons) */
    i32 path_x = tx+34+5*52+6;
    gfx_str_clipped(path_x,cr.y+7,cr.w-FM_SB-(path_x-tx)-4,cur_path,COL_ACCENT,COL_SURFACE3);

    /* -- Input bar (rename / new name) -- */
    if(w->tab != FM_MODE_BROWSE){
        const char *prompt = (w->tab==FM_MODE_RENAME) ? "Rename: " :
                             (w->tab==FM_MODE_NEWFILE) ? "New file: " : "New folder: ";
        i32 iy = cr.y+cr.h-24;
        gfx_rect(tx,iy,cr.w-FM_SB,24,COL_INPUT_BG);
        gfx_hline(tx,iy,cr.w-FM_SB,COL_BORDER);
        i32 pw = gfx_str_width(prompt);
        gfx_str(tx+4,iy+6,prompt,COL_YELLOW,COL_INPUT_BG);
        gfx_str(tx+4+pw,iy+6,w->input_buf,COL_TEXT,COL_INPUT_BG);
        /* Cursor */
        if((timer_get_ticks()/30)%2==0){
            i32 cx=tx+4+pw+(i32)w->input_len*FONT_W;
            gfx_vline(cx,iy+4,16,COL_TEXT);
        }
    }

    /* -- File list -- */
    if(!w->fm_dir){
        gfx_str(tx+8,cr.y+FM_TB+8,"(no directory)",COL_MUTED,COL_SURFACE);
        return;
    }

    /* Column headers */
    i32 list_y = cr.y + FM_TB;
    gfx_rect(tx,list_y,cr.w-FM_SB,16,COL_SURFACE3);
    gfx_str(tx+24,list_y+3,"Name",COL_DIM,COL_SURFACE3);
    gfx_str_right(tx,list_y+3,cr.w-FM_SB-8,"Size",COL_DIM,COL_SURFACE3);
    gfx_hline(tx,list_y+16,cr.w-FM_SB,COL_BORDER);
    list_y += 16;

    i32 list_bottom = cr.y+cr.h - (w->tab!=FM_MODE_BROWSE ? 24 : 0) - 20; /* leave room for status */

    u32 visible = 0;
    i32 fy = list_y;
    for(u32 i=0;i<w->fm_dir->child_count && fy<list_bottom; i++){
        fs_node_t *child = w->fm_dir->children[i];
        bool sel = (i == w->fm_sel);
        u32 bg = sel ? COL_SELECTION : (i%2==0?COL_SURFACE:COL_SURFACE2);

        gfx_rect(tx+1,fy,cr.w-FM_SB-2,FM_ROW,bg);
        if(sel) gfx_rect(tx+1,fy,3,FM_ROW,COL_PRIMARY);

        bool is_dir = child->type==FS_DIR;
        u32 icol = is_dir ? COL_PRIMARY : COL_TEXT;
        fm_draw_icon(tx+6,fy+4,is_dir,icol);

        gfx_str_clipped(tx+20,fy+4,cr.w-FM_SB-90,child->name,icol,bg);

        if(child->type==FS_FILE){
            char sz[16];
            if(child->size>=1024){ kutoa(child->size/1024,sz,10); kstrcat(sz,"K"); }
            else { kutoa(child->size,sz,10); kstrcat(sz,"B"); }
            gfx_str_right(tx,fy+4,cr.w-FM_SB-8,sz,COL_DIM,bg);
        } else {
            gfx_str_right(tx,fy+4,cr.w-FM_SB-8,"DIR",COL_PRIMARY,bg);
        }

        fy += FM_ROW;
        visible++;
    }

    /* Empty folder message */
    if(w->fm_dir->child_count == 0)
        gfx_str(tx+8,list_y+8,"(empty folder)",COL_MUTED,COL_SURFACE);

    /* -- Status bar -- */
    i32 sb_y = cr.y+cr.h-20;
    gfx_rect(tx,sb_y,cr.w-FM_SB,20,COL_SURFACE3);
    gfx_hline(tx,sb_y,cr.w-FM_SB,COL_BORDER);
    char status[64];
    char cnt[8]; kutoa(w->fm_dir->child_count,cnt,10);
    kstrcpy(status,cnt); kstrcat(status," item");
    if(w->fm_dir->child_count!=1) kstrcat(status,"s");
    if(w->fm_sel < w->fm_dir->child_count){
        kstrcat(status,"  |  "); kstrcat(status,w->fm_dir->children[w->fm_sel]->name);
    }
    gfx_str(tx+6,sb_y+4,status,COL_DIM,COL_SURFACE3);
    /* Hint */
    gfx_str_right(tx,sb_y+4,cr.w-FM_SB-4,"Dbl-click=open  Del=delete  F2=rename",COL_MUTED,COL_SURFACE3);
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
    default: break;
    }
}

void app_files_click(window_t *w, i32 x, i32 y, mouse_t *m){
    (void)m;

    /* Sidebar click */
    if(x < FM_SB){
        int idx = (y-20) / 22;
        if(idx>=0 && idx<FM_SB_ITEMS){
            const char *p = FM_PLACES[idx];
            if(kstrcmp(p,"~")==0) p = user_current_uid()==0?"/root":"/home/user";
            fs_node_t *d = vfs_resolve_path(p);
            if(d){ w->fm_dir=d; w->fm_sel=0; }
        }
        return;
    }

    /* -- Toolbar -- */
    if(y < FM_TB){
        i32 rx = x - FM_SB;
        /* Up */
        if(rx>=2 && rx<30 && w->fm_dir && w->fm_dir->parent){
            w->fm_dir=w->fm_dir->parent; w->fm_sel=0; return;
        }
        /* +File */
        if(rx>=34 && rx<82){ w->input_buf[0]='\0'; w->input_len=0; w->tab=FM_MODE_NEWFILE; return; }
        /* +Dir */
        if(rx>=86 && rx<134){ w->input_buf[0]='\0'; w->input_len=0; w->tab=FM_MODE_NEWFOLD; return; }
        /* Del */
        if(rx>=138 && rx<186 && w->fm_dir && w->fm_sel < w->fm_dir->child_count){
            vfs_delete(w->fm_dir->children[w->fm_sel]);
            if(w->fm_sel>=w->fm_dir->child_count && w->fm_sel>0) w->fm_sel--;
            return;
        }
        /* Rename */
        if(rx>=190 && rx<238 && w->fm_dir && w->fm_sel < w->fm_dir->child_count){
            kstrncpy(w->input_buf, w->fm_dir->children[w->fm_sel]->name, 63);
            w->input_len = kstrlen(w->input_buf);
            w->tab = FM_MODE_RENAME;
            return;
        }
        /* Open */
        if(rx>=242 && rx<290 && w->fm_dir && w->fm_sel < w->fm_dir->child_count){
            fs_node_t *ch = w->fm_dir->children[w->fm_sel];
            if(ch->type==FS_FILE){
                /* Open in editor */
                window_t *ew = wm_open(APP_EDITOR,"Editor",
                    60,40,(i32)SCREEN_W*65/100,(i32)SCREEN_H*70/100);
                if(ew){
                    kstrncpy(ew->editor_path, ch->name, FS_PATH_MAX-1);
                    /* Copy file content to editor buffer */
                    win_clear(ew);
                    if(ch->size>0){ win_append(ew,ch->data); }
                    ew->editor_modified=false;
                }
            }
            return;
        }
        return;
    }

    /* -- File list -- */
    i32 list_top = FM_TB + 16; /* toolbar + column headers */
    if(y >= list_top){
        u32 row = (u32)(y - list_top) / FM_ROW;
        if(w->fm_dir && row < w->fm_dir->child_count){
            if(w->fm_sel == row){
                /* Second click = navigate/open */
                fs_node_t *child = w->fm_dir->children[row];
                if(child->type==FS_DIR){ w->fm_dir=child; w->fm_sel=0; }
                else {
                    /* Open text file in editor */
                    window_t *ew = wm_open(APP_EDITOR,"Editor",
                        60,40,(i32)SCREEN_W*65/100,(i32)SCREEN_H*70/100);
                    if(ew){
                        kstrncpy(ew->editor_path, child->name, FS_PATH_MAX-1);
                        win_clear(ew);
                        if(child->size>0) win_append(ew,child->data);
                        ew->editor_modified=false;
                    }
                }
            }
            w->fm_sel = row;
        }
    }
}
