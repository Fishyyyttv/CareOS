/* CareOS v9 -- apps/app_users.c -- User Manager */
#include "apps_common.h"

void app_users_init(window_t *w){ w->um_sel=0; w->um_input_name[0]='\0'; w->um_input_pass[0]='\0'; w->um_field=0; }
void app_users_draw(window_t *w){
    rect_t cr=wm_client_rect(w);
    gfx_rect(cr.x,cr.y,cr.w,cr.h,COL_SURFACE);
    gfx_str(cr.x+8,cr.y+8,"User Accounts",COL_TEXT,COL_SURFACE); 
    gfx_hline(cr.x,cr.y+24,cr.w,COL_BORDER);
    /* User list */
    gfx_str(cr.x+8,cr.y+28,"  UID   USERNAME       ADMIN   HOME",COL_YELLOW,COL_SURFACE);
    i32 y=cr.y+44;
    for(u32 uid=0;uid<1010&&y<cr.y+cr.h-80;uid++){
        user_t *u=user_get_by_uid(uid==0?0:(uid>0?uid+999:1000));
        if(!u||!u->active) { if(uid>0&&uid<3) continue; else if(uid>4) break; continue; }
        bool sel=(u->uid==w->um_sel);
        u32 bg=sel?COL_SELECTION:COL_SURFACE;
        gfx_rect(cr.x+4,y,cr.w-8,18,bg);
        char line[80]; char n[8]; line[0]='\0'; kstrcat(line,"  ");
        kutoa(u->uid,n,10); kstrcat(line,n);
        while(kstrlen(line)<8) kstrcat(line," ");
        kstrcat(line,u->name);
        while(kstrlen(line)<24) kstrcat(line," ");
        kstrcat(line,u->is_root?"yes":"no");
        while(kstrlen(line)<33) kstrcat(line," ");
        kstrcat(line,u->home);
        gfx_str(cr.x+8,y+3,line,COL_TEXT,bg);
        y+=18;
    }
    /* Add user form */
    gfx_hline(cr.x,cr.y+cr.h-76,cr.w,COL_BORDER);
    gfx_str(cr.x+8,cr.y+cr.h-72,"Add User:",COL_DIM,COL_SURFACE);
    gfx_str(cr.x+8,cr.y+cr.h-56,"Username:",COL_MUTED,COL_SURFACE);
    gfx_rect(cr.x+76,cr.y+cr.h-60,120,18,w->um_field==0?COL_INPUT_BG:COL_SURFACE2);
    gfx_rect_outline(cr.x+76,cr.y+cr.h-60,120,18,w->um_field==0?COL_PRIMARY:COL_BORDER);
    gfx_str(cr.x+80,cr.y+cr.h-57,w->um_input_name,COL_TEXT,w->um_field==0?COL_INPUT_BG:COL_SURFACE2);
    gfx_str(cr.x+8,cr.y+cr.h-36,"Password:",COL_MUTED,COL_SURFACE);
    gfx_rect(cr.x+76,cr.y+cr.h-40,120,18,w->um_field==1?COL_INPUT_BG:COL_SURFACE2);
    gfx_rect_outline(cr.x+76,cr.y+cr.h-40,120,18,w->um_field==1?COL_PRIMARY:COL_BORDER);
    gfx_str(cr.x+80,cr.y+cr.h-37,"\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2",COL_DIM,w->um_field==1?COL_INPUT_BG:COL_SURFACE2);
    gfx_rect_rounded(cr.x+cr.w-70,cr.y+cr.h-44,60,22,4,COL_PRIMARY);
    gfx_str_centered(cr.x+cr.w-70,cr.y+cr.h-40,60,"Create",COL_WHITE,COL_PRIMARY);
}
void app_users_key(window_t *w,char c){
    if(c=='\t'){ w->um_field=1-w->um_field; return; }
    if(c=='\n'){ /* Create user */
        if(kstrlen(w->um_input_name)>0&&kstrlen(w->um_input_pass)>0){
            if(user_create(w->um_input_name, w->um_input_pass)==0)
                notify_push("User Created",w->um_input_name,COL_GREEN);
            else notify_push("Error","User creation failed",COL_RED);
            w->um_input_name[0]='\0'; w->um_input_pass[0]='\0';
        }
        return;
    }
    if(c=='\b'){
        if(w->um_field==0){ u32 l=kstrlen(w->um_input_name); if(l>0) w->um_input_name[l-1]='\0'; }
        else { u32 l=kstrlen(w->um_input_pass); if(l>0) w->um_input_pass[l-1]='\0'; }
        return;
    }
    if(c>=32&&c<127){
        if(w->um_field==0){ u32 l=kstrlen(w->um_input_name); if(l<31){w->um_input_name[l]=c;w->um_input_name[l+1]='\0';} }
        else { u32 l=kstrlen(w->um_input_pass); if(l<31){w->um_input_pass[l]=c;w->um_input_pass[l+1]='\0';} }
    }
}
void app_users_click(window_t *w,i32 x,i32 y){
    rect_t cr=wm_client_rect(w);
    if(y>cr.y+cr.h-60&&x>cr.x+76&&x<cr.x+196&&y>cr.y+cr.h-60&&y<cr.y+cr.h-42) w->um_field=0;
    if(y>cr.y+cr.h-40&&x>cr.x+76&&x<cr.x+196&&y>cr.y+cr.h-42&&y<cr.y+cr.h-22) w->um_field=1;
    if(x>cr.x+cr.w-70&&y>cr.y+cr.h-44){ /* Create button */
        app_users_key(w,'\n');
    }
}
