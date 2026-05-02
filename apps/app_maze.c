#include "kernel.h"
#include "../gui/gui.h"

#define MAZE_W 21
#define MAZE_H 15

static char maze[MAZE_H][MAZE_W+1];
static i32 player_x = 1;
static i32 player_y = 1;
static bool maze_won = false;
static u32 level = 1;

static u32 rand_state = 12345;
static u32 get_rand(void) {
    rand_state = rand_state * 1103515245 + 12345;
    return (rand_state / 65536) % 32768;
}

static void generate_maze(void) {
    for (int y=0; y<MAZE_H; y++) {
        for (int x=0; x<MAZE_W; x++) maze[y][x] = '#';
        maze[y][MAZE_W] = '\0';
    }
    
    int stack_x[200], stack_y[200];
    int top = 0;
    
    int cx = 1, cy = 1;
    maze[cy][cx] = '.';
    stack_x[top] = cx; stack_y[top] = cy;
    top++;
    
    int dx[] = {0, 1, 0, -1};
    int dy[] = {-1, 0, 1, 0};
    
    while(top > 0) {
        cx = stack_x[top-1];
        cy = stack_y[top-1];
        
        int n[4], n_count = 0;
        for (int i=0; i<4; i++) {
            int nx = cx + dx[i]*2;
            int ny = cy + dy[i]*2;
            if (nx > 0 && nx < MAZE_W-1 && ny > 0 && ny < MAZE_H-1 && maze[ny][nx] == '#') {
                n[n_count++] = i;
            }
        }
        
        if (n_count > 0) {
            int d = n[get_rand() % n_count];
            maze[cy + dy[d]][cx + dx[d]] = '.';
            maze[cy + dy[d]*2][cx + dx[d]*2] = '.';
            stack_x[top] = cx + dx[d]*2;
            stack_y[top] = cy + dy[d]*2;
            top++;
        } else {
            top--;
        }
    }
    
    maze[1][1] = 'S';
    maze[MAZE_H-2][MAZE_W-2] = 'E';
    player_x = 1;
    player_y = 1;
}

void app_maze_init(window_t *w) {
    w->app = APP_MAZE;
    w->win_buffer.pixels = NULL; 
    rand_state = timer_get_ticks();
    level = 1;
    generate_maze();
    maze_won = false;
}

void app_maze_draw(window_t *w) {
    rect_t cr = wm_client_rect(w);
    gfx_rect(cr.x, cr.y, cr.w, cr.h, g_theme->surface);
    
    i32 tile_w = cr.w / MAZE_W;
    i32 tile_h = cr.h / MAZE_H;
    
    i32 ox = cr.x + (cr.w - tile_w * MAZE_W) / 2;
    i32 oy = cr.y + (cr.h - tile_h * MAZE_H) / 2;
    
    for (i32 y = 0; y < MAZE_H; y++) {
        for (i32 x = 0; x < MAZE_W; x++) {
            i32 dx = ox + x * tile_w;
            i32 dy = oy + y * tile_h;
            char c = maze[y][x];
            
            if (c == '#') {
                gfx_rect(dx, dy, tile_w, tile_h, g_theme->surface3);
                gfx_rect_outline(dx, dy, tile_w, tile_h, g_theme->border);
            } else if (c == 'E') {
                gfx_rect(dx, dy, tile_w, tile_h, g_theme->success);
            }
            
            if (x == player_x && y == player_y) {
                gfx_circle_fill(dx + tile_w/2, dy + tile_h/2, tile_w/2 - 2, COL_ACCENT);
            }
        }
    }
    
    char lvl_str[32]; ksprintf(lvl_str, "Level %d", level);
    gfx_str_centered_ex(cr.x, cr.y + 4, cr.w, lvl_str, COL_TEXT, COL_TRANSPARENT, FONT_H2);
    
    if (maze_won) {
        gfx_rect_blend(cr.x, cr.y, cr.w, cr.h, COL_BLACK, 150);
        gfx_str_centered_ex(cr.x, cr.y + cr.h/2 - 20, cr.w, "LEVEL CLEARED!", g_theme->success, COL_TRANSPARENT, FONT_H1);
        gfx_str_centered(cr.x, cr.y + cr.h/2 + 20, cr.w, "Press Enter for next level", COL_WHITE, COL_TRANSPARENT);
    }
}

void app_maze_key(window_t *w, char c) {
    if (maze_won) {
        if (c == '\n') {
            level++;
            generate_maze();
            maze_won = false;
        }
        return;
    }
    
    i32 nx = player_x, ny = player_y;
    
    if (c == 'w' || c == 'W') ny--;
    if (c == 's' || c == 'S') ny++;
    if (c == 'a' || c == 'A') nx--;
    if (c == 'd' || c == 'D') nx++;
    
    if (nx >= 0 && nx < MAZE_W && ny >= 0 && ny < MAZE_H) {
        if (maze[ny][nx] != '#') {
            player_x = nx;
            player_y = ny;
            
            if (maze[ny][nx] == 'E') {
                maze_won = true;
                speaker_beep(523, 100);
                timer_wait(100);
                speaker_beep(659, 100);
                timer_wait(100);
                speaker_beep(784, 200);
            }
        } else {
            speaker_beep(100, 50);
        }
    }
}
