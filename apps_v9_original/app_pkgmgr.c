/* CareOS v9 -- apps/app_pkgmgr.c -- Package Manager */
#include "apps_common.h"

#define PKG_TAB_INSTALLED  0
#define PKG_TAB_INSTALL    1
#define PKG_TAB_CREATE     2
#define PKG_TAB_ABOUT      3

void app_pkgmgr_init(window_t *w){
    w->pkgmgr_tab = PKG_TAB_INSTALLED;
    w->pkgmgr_sel = 0;
    w->input_buf[0] = '\0';
    w->input_len = 0;
    kstrcpy(w->pkgmgr_status, "Ready");
}

void app_pkgmgr_draw(window_t *w){
    rect_t cr = wm_client_rect(w);
    gfx_rect(cr.x,cr.y,cr.w,cr.h,COL_SURFACE);

    /* Header */
    gfx_gradient_rect(cr.x,cr.y,cr.w,30,rgb(0x0e,0x10,0x1e),COL_SURFACE);
    gfx_str(cr.x+8,cr.y+8,"CareOS Package Manager",COL_TEXT,COL_TRANSPARENT);
    gfx_str_right(cr.x,cr.y+8,cr.w-8,"[ .care format ]",COL_PRIMARY,COL_TRANSPARENT);
    gfx_hline(cr.x,cr.y+30,cr.w,COL_BORDER);

    /* Tabs */
    const char *tabs[]={"Installed","Install .care","Create Package","About",NULL};
    for(int i=0;tabs[i];i++){
        i32 tx=cr.x+4+i*((cr.w-8)/4);
        i32 tw=(cr.w-8)/4-4;
        u32 bg=(u32)i==w->pkgmgr_tab?COL_SELECTION:COL_SURFACE2;
        gfx_rect_rounded(tx,cr.y+32,tw,22,4,bg);
        if((u32)i==w->pkgmgr_tab) gfx_hline(tx,cr.y+54,tw,COL_PRIMARY);
        gfx_str_centered(tx,cr.y+36,tw,tabs[i],(u32)i==w->pkgmgr_tab?COL_TEXT:COL_DIM,bg);
    }
    gfx_hline(cr.x,cr.y+56,cr.w,COL_BORDER);

    i32 y = cr.y+60;
    i32 content_h = cr.h - 80; /* leave room for status bar */

    if(w->pkgmgr_tab==PKG_TAB_INSTALLED){
        /* Column headers */
        gfx_rect(cr.x,y,cr.w,16,COL_SURFACE3);
        gfx_str(cr.x+8,y+3,"Name",COL_DIM,COL_SURFACE3);
        gfx_str(cr.x+140,y+3,"Version",COL_DIM,COL_SURFACE3);
        gfx_str(cr.x+210,y+3,"Category",COL_DIM,COL_SURFACE3);
        gfx_str(cr.x+310,y+3,"Description",COL_DIM,COL_SURFACE3);
        gfx_hline(cr.x,y+16,cr.w,COL_BORDER);
        y+=16;

        u32 cnt = carepkg_count();
        u32 shown = 0;
        for(u32 i=0; i<cnt && y<cr.y+content_h; i++){
            char name[32],ver[16],desc[128],cat[32]; bool installed;
            if(!carepkg_get_info(i,name,ver,desc,cat,&installed)) continue;
            if(!installed) continue;
            bool sel=(i==w->pkgmgr_sel);
            u32 bg=sel?COL_SELECTION:(shown%2==0?COL_SURFACE:COL_SURFACE2);
            gfx_rect(cr.x+1,y,cr.w-2,22,bg);
            if(sel) gfx_vline(cr.x+1,y,22,COL_GREEN);
            /* .care badge */
            gfx_rect_rounded(cr.x+4,y+4,32,14,3,rgb(0x06,0x3a,0x1e));
            gfx_str(cr.x+6,y+6,".care",COL_GREEN,rgb(0x06,0x3a,0x1e));
            gfx_str(cr.x+42,y+4,name,COL_TEXT,bg);
            gfx_str(cr.x+142,y+4,ver,COL_ACCENT,bg);
            gfx_str(cr.x+212,y+4,cat[0]?cat:"Utility",COL_DIM,bg);
            gfx_str_clipped(cr.x+312,y+4,cr.w-420,desc,COL_MUTED,bg);
            /* Remove button */
            i32 rx=cr.x+cr.w-72;
            u32 rbg=sel?rgb(0x3a,0x0a,0x0a):COL_SURFACE2;
            gfx_rect_rounded(rx,y+3,66,16,3,rbg);
            gfx_str_centered(rx,y+6,66,"Uninstall",COL_RED,rbg);
            y+=22; shown++;
        }
        if(shown==0){
            gfx_str(cr.x+8,y+8,"No packages installed.",COL_MUTED,COL_SURFACE);
            gfx_str(cr.x+8,y+26,"Switch to 'Install .care' tab to install packages.",COL_DIM,COL_SURFACE);
        }

    } else if(w->pkgmgr_tab==PKG_TAB_INSTALL){
        gfx_str(cr.x+8,y,"Browse VFS for .care files to install:",COL_DIM,COL_SURFACE); y+=22;

        /* List /tmp dir for .care files */
        fs_node_t *tmpd = vfs_find(vfs_root(),"tmp");
        if(tmpd){
            for(u32 i=0; i<tmpd->child_count && y<cr.y+content_h; i++){
                fs_node_t *f = tmpd->children[i];
                /* Check if it's a .care or .cpk file */
                const char *ext = kstrrchr(f->name,'.');
                bool is_care = ext && (kstrcmp(ext,".care")==0 || kstrcmp(ext,".cpk")==0);
                if(!is_care) continue;
                bool sel=((u32)i==w->pkgmgr_sel);
                u32 bg=sel?COL_SELECTION:COL_SURFACE2;
                gfx_rect(cr.x+4,y,cr.w-8,24,bg);
                if(sel) gfx_vline(cr.x+4,y,24,COL_PRIMARY);
                gfx_rect_rounded(cr.x+8,y+4,32,16,3,rgb(0x0a,0x20,0x3e));
                gfx_str(cr.x+10,y+7,".care",COL_PRIMARY,rgb(0x0a,0x20,0x3e));
                gfx_str(cr.x+46,y+6,f->name,COL_TEXT,bg);
                /* Size */
                char sz[12]; kutoa(f->size,sz,10); kstrcat(sz,"B");
                gfx_str(cr.x+200,y+6,sz,COL_DIM,bg);
                /* Install button */
                i32 ib_x = cr.x+cr.w-70;
                u32 ib_bg = sel ? COL_PRIMARY : COL_SURFACE3;
                gfx_rect_rounded(ib_x,y+4,64,16,3,ib_bg);
                gfx_str_centered(ib_x,y+7,64,"Install",sel?COL_WHITE:COL_PRIMARY,ib_bg);
                y+=24;
            }
        }
        /* Hint */
        gfx_str(cr.x+8,cr.y+content_h-20,"Tip: Use terminal 'carepkg create <name>' to make a .care template",COL_MUTED,COL_SURFACE);

    } else if(w->pkgmgr_tab==PKG_TAB_CREATE){
        gfx_str(cr.x+8,y,"Create a new .care package template:",COL_TEXT,COL_SURFACE); y+=28;
        gfx_hline(cr.x+4,y,cr.w-8,COL_BORDER); y+=12;

        /* Name field */
        gfx_str(cr.x+8,y,"Package name:",COL_DIM,COL_SURFACE);
        gfx_rect(cr.x+120,y-3,cr.w-180,22,COL_INPUT_BG);
        gfx_rect_outline(cr.x+120,y-3,cr.w-180,22,COL_BORDER);
        gfx_str(cr.x+124,y+1,w->input_buf,COL_TEXT,COL_INPUT_BG);
        if((timer_get_ticks()/30)%2==0){
            i32 cx=cr.x+124+(i32)w->input_len*FONT_W;
            gfx_vline(cx,y-1,18,COL_TEXT);
        }
        y+=32;

        /* Create button */
        bool can_create = w->input_len > 0;
        u32 btn_bg = can_create ? COL_PRIMARY : COL_SURFACE2;
        gfx_rect_rounded(cr.x+8,y,100,26,5,btn_bg);
        gfx_str_centered(cr.x+8,y+7,100,"Create Template",can_create?COL_WHITE:COL_MUTED,btn_bg);
        y+=40;

        gfx_hline(cr.x+4,y,cr.w-8,COL_BORDER); y+=12;
        gfx_str(cr.x+8,y,"Template will be created at /tmp/<name>.care",COL_DIM,COL_SURFACE); y+=18;
        gfx_str(cr.x+8,y,"Edit it in the Editor app, then install from 'Install .care' tab.",COL_MUTED,COL_SURFACE);

    } else { /* About */
        const char *lines[]={
            ".care Package Format \xe2\x80\x93 CareOS v6",
            "",
            "A .care file is a plain-text bundle that can be",
            "installed with carepkg or the Package Manager UI.",
            "",
            "Format:",
            "  CARE 1.0",
            "  name=myapp",
            "  version=1.0.0",
            "  description=My app",
            "  author=You",
            "  exec=main",
            "  icon=generic",
            "  category=Utilities",
            "  permissions=fs.read,gui",
            "  ---FILES---",
            "  FILE main",
            "  #!/bin/sh",
            "  echo Hello!",
            "  ---ENDFILE---",
            "  ---END---",
            "",
            "Packages install to /apps/<name>/",
            "carepkg install/remove/list/info/create",
            NULL
        };
        for(int i=0; lines[i] && y<cr.y+content_h; i++){
            u32 col = (lines[i][0]=='.'||lines[i][0]=='c') ? COL_PRIMARY :
                      (lines[i][0]==' ') ? COL_ACCENT : COL_TEXT;
            gfx_str(cr.x+8,y,lines[i],col,COL_SURFACE);
            y+=14;
        }
    }

    /* Status bar */
    i32 sb_y = cr.y+cr.h-22;
    gfx_rect(cr.x,sb_y,cr.w,22,COL_SURFACE3);
    gfx_hline(cr.x,sb_y,cr.w,COL_BORDER);
    char status[64]; kstrcpy(status,"Status: "); kstrcat(status,w->pkgmgr_status);
    gfx_str(cr.x+8,sb_y+5,status,COL_DIM,COL_SURFACE3);
    /* Package count */
    char cnt_str[32]; kstrcpy(cnt_str,"Installed: ");
    char cnt[8]; kutoa(carepkg_count(),cnt,10); kstrcat(cnt_str,cnt);
    gfx_str_right(cr.x,sb_y+5,cr.w-8,cnt_str,COL_DIM,COL_SURFACE3);
}

void app_pkgmgr_key(window_t *w, char c){
    if(w->pkgmgr_tab==PKG_TAB_CREATE){
        if(c=='\b'){ if(w->input_len>0){w->input_len--;w->input_buf[w->input_len]='\0';} }
        else if(c=='\n'){
            if(w->input_len>0){
                carepkg_run("create", w->input_buf);
                kstrcpy(w->pkgmgr_status, "Template created in /tmp/");
                kstrcat(w->pkgmgr_status, w->input_buf);
                kstrcat(w->pkgmgr_status, ".care");
                w->input_buf[0]='\0'; w->input_len=0;
            }
        } else if(c>=32&&c<127&&w->input_len<30){
            w->input_buf[w->input_len++]=c;
            w->input_buf[w->input_len]='\0';
        }
    }
}

void app_pkgmgr_click(window_t *w, i32 x, i32 y){
    rect_t cr = wm_client_rect(w);

    /* Tab bar */
    if(y < cr.y+58){
        i32 tab_w = (cr.w-8)/4;
        int t = (x - cr.x - 4) / tab_w;
        if(t>=0 && t<=3){ w->pkgmgr_tab=(u32)t; w->pkgmgr_sel=0; }
        return;
    }

    if(w->pkgmgr_tab==PKG_TAB_INSTALLED){
        i32 list_y = cr.y+76;
        u32 row = (u32)(y-list_y)/22;
        w->pkgmgr_sel = row;
        /* Click on Uninstall button */
        if(x >= cr.x+cr.w-72){
            u32 shown=0;
            for(u32 i=0; i<carepkg_count(); i++){
                char name[32],ver[16],desc[128],cat[32]; bool installed;
                if(!carepkg_get_info(i,name,ver,desc,cat,&installed)) continue;
                if(!installed) continue;
                if(shown==row){
                    carepkg_remove(name);
                    kstrcpy(w->pkgmgr_status,"Removed: "); kstrcat(w->pkgmgr_status,name);
                    break;
                }
                shown++;
            }
        }
    } else if(w->pkgmgr_tab==PKG_TAB_INSTALL){
        i32 list_y = cr.y+82;
        u32 row = (u32)(y-list_y)/24;
        w->pkgmgr_sel = row;
        /* Click on Install button */
        if(x >= cr.x+cr.w-70){
            fs_node_t *tmpd = vfs_find(vfs_root(),"tmp");
            if(tmpd){
                u32 shown=0;
                for(u32 i=0; i<tmpd->child_count; i++){
                    fs_node_t *f = tmpd->children[i];
                    const char *ext = kstrrchr(f->name,'.');
                    bool is_care = ext&&(kstrcmp(ext,".care")==0||kstrcmp(ext,".cpk")==0);
                    if(!is_care) continue;
                    if(shown==row){
                        char path[FS_PATH_MAX]; kstrcpy(path,"/tmp/"); kstrcat(path,f->name);
                        int r = carepkg_install(path);
                        kstrcpy(w->pkgmgr_status, r==0?"Installed: ":"Failed: ");
                        kstrcat(w->pkgmgr_status, f->name);
                        break;
                    }
                    shown++;
                }
            }
        }
    } else if(w->pkgmgr_tab==PKG_TAB_CREATE){
        /* Create button */
        if(x>=cr.x+8&&x<cr.x+108 && y>=cr.y+120&&y<cr.y+146){
            if(w->input_len>0){
                carepkg_run("create",w->input_buf);
                kstrcpy(w->pkgmgr_status,"Template: /tmp/"); kstrcat(w->pkgmgr_status,w->input_buf); kstrcat(w->pkgmgr_status,".care");
                w->input_buf[0]='\0'; w->input_len=0;
            }
        }
    }
}
