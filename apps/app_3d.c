#include "kernel.h"
#include "../gui/gui.h"

#define MAP_W 10
#define MAP_H 10
#define TILE_SIZE 64

static const char map[MAP_H][MAP_W+1] = {
    "##########",
    "#........#",
    "#.##.##..#",
    "#........#",
    "#.##.##..#",
    "#........#",
    "#.##.##..#",
    "#........#",
    "#.##.##..#",
    "##########"
};

static i32 px = TILE_SIZE + TILE_SIZE / 2;
static i32 py = TILE_SIZE + TILE_SIZE / 2;
static i32 pa = 45; 

static i32 i_sin(i32 deg) {
    static const i32 t[16] = {0, 28, 55, 82, 108, 131, 153, 173, 191, 206, 219, 230, 239, 246, 251, 254};
    while(deg < 0) deg += 360;
    deg %= 360;
    if (deg >= 270) return -i_sin(360 - deg);
    if (deg >= 180) return -i_sin(deg - 180);
    if (deg > 90) return i_sin(180 - deg);
    if (deg == 90) return 256;
    i32 idx = (deg * 16) / 90;
    return t[idx];
}
static i32 i_cos(i32 deg) { return i_sin(deg + 90); }

void app_3d_init(window_t *w) {
    w->app = APP_3D;
    w->win_buffer.pixels = NULL;
    px = TILE_SIZE + TILE_SIZE / 2;
    py = TILE_SIZE + TILE_SIZE / 2;
    pa = 45;
}

void app_3d_draw(window_t *w) {
    rect_t cr = wm_client_rect(w);
    i32 sw = cr.w, sh = cr.h;
    
    gfx_rect(cr.x, cr.y, sw, sh / 2, rgb(30, 30, 40)); /* Ceiling */
    gfx_rect(cr.x, cr.y + sh / 2, sw, sh / 2, rgb(60, 60, 70)); /* Floor */
    
    i32 fov = 60;
    i32 half_fov = fov / 2;
    
    for (i32 x = 0; x < sw; x += 2) {
        i32 ray_angle = pa - half_fov + (x * fov) / sw;
        while (ray_angle < 0) ray_angle += 360;
        ray_angle %= 360;
        
        i32 r_sin = i_sin(ray_angle);
        i32 r_cos = i_cos(ray_angle);
        
        i32 rx = px, ry = py;
        i32 depth = 0;
        i32 max_depth = TILE_SIZE * 12;
        
        while (depth < max_depth) {
            depth += 4; /* Faster step to prevent freeze */
            i32 rx = px + (r_cos * depth) / 256;
            i32 ry = py + (r_sin * depth) / 256;
            
            i32 mx = rx / TILE_SIZE;
            i32 my = ry / TILE_SIZE;
            if (mx >= 0 && mx < MAP_W && my >= 0 && my < MAP_H) {
                if (map[my][mx] == '#') break;
            } else break;
        }
        
        i32 diff = ray_angle - pa;
        while(diff < -180) diff += 360;
        while(diff > 180) diff -= 360;
        depth = (depth * i_cos(diff)) / 256;
        
        if (depth == 0) depth = 1;
        i32 wall_h = (TILE_SIZE * sh) / depth;
        if (wall_h > sh) wall_h = sh;
        
        i32 wall_top = cr.y + (sh / 2) - (wall_h / 2);
        
        i32 shade = 255 - (depth * 255) / max_depth;
        if (shade < 0) shade = 0;
        if (shade > 255) shade = 255;
        
        i32 color = rgb(shade, shade / 2, shade / 2); /* Reddish walls */
        gfx_rect(cr.x + x, wall_top, 2, wall_h, color);
    }
    
    gfx_str_centered_ex(cr.x, cr.y + 4, cr.w, "WASD to move", COL_WHITE, COL_TRANSPARENT, FONT_H2);
}

void app_3d_key(window_t *w, char c) {
    i32 s = i_sin(pa);
    i32 c_ = i_cos(pa);
    
    i32 speed = 12;
    
    if (c == 'a' || c == 'A') pa -= 10;
    if (c == 'd' || c == 'D') pa += 10;
    
    i32 nx = px, ny = py;
    if (c == 'w' || c == 'W') {
        nx += (c_ * speed) / 256;
        ny += (s * speed) / 256;
    }
    if (c == 's' || c == 'S') {
        nx -= (c_ * speed) / 256;
        ny -= (s * speed) / 256;
    }
    
    i32 mx = nx / TILE_SIZE;
    i32 my = ny / TILE_SIZE;
    if (mx > 0 && mx < MAP_W - 1 && my > 0 && my < MAP_H - 1) {
        if (map[my][mx] != '#') {
            px = nx;
            py = ny;
        }
    }
    
    while(pa < 0) pa += 360;
    pa %= 360;
}
