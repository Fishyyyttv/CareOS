#include "pure.h"
#include "blit.h"
#include <assert.h>

static void test_clip_inside(void){
    blit_box_t b; assert(blit_clip(10,10,5,5, 0,0,100,100, 100,100, &b));
    assert(b.x0==10 && b.y0==10 && b.x1==15 && b.y1==15);
}
static void test_clip_partial_and_empty(void){
    blit_box_t b;
    assert(blit_clip(-3,-3,10,10, 0,0,100,100, 100,100, &b));
    assert(b.x0==0 && b.y0==0 && b.x1==7 && b.y1==7);
    assert(blit_clip(0,0,50,50, 10,10,5,5, 100,100, &b));
    assert(b.x0==10 && b.x1==15);
    assert(!blit_clip(200,0,10,10, 0,0,100,100, 100,100, &b));
}
static void test_clip_rejects_bad_sizes(void){
    blit_box_t b;
    assert(!blit_clip(0,0,0,5, 0,0,100,100, 100,100, &b));
    assert(!blit_clip(0,0,5,-1, 0,0,100,100, 100,100, &b));
    assert(!blit_clip(0,0,5,5, 0,0,100,100, 0,100, &b));
    /* huge sizes must not overflow into a bogus in-range box */
    assert(blit_clip(0x7FFFFFF0,0,0x7FFFFFF0,5, 0,0,100,100, 100,100, &b) == false);
}
static void test_copy_rows_padded_pitch(void){
    u32 src[4*2] = {1,2,3,4, 5,6,7,8};
    u8 fb[2*32]; memset(fb, 0xEE, sizeof fb);             /* pitch 32 > 4*4 */
    blit_copy_rows(fb, 32, 32, src, 4, 0,0,4,2, 16,8,0);
    u32 r0[4], r1[4]; memcpy(r0, fb, 16); memcpy(r1, fb+32, 16);
    assert(r0[0]==1 && r0[3]==4 && r1[0]==5 && r1[3]==8);
    assert(fb[16]==0xEE);                                  /* padding untouched */
}
static void test_copy_rows_24bpp(void){
    u32 src[2] = {0x00112233, 0x00445566};
    u8 fb[6] = {0};
    blit_copy_rows(fb, 6, 24, src, 2, 0,0,2,1, 16,8,0);
    assert(fb[0]==0x33 && fb[1]==0x22 && fb[2]==0x11);
    assert(fb[3]==0x66 && fb[4]==0x55 && fb[5]==0x44);
}
static void test_copy_rows_bgr(void){
    u32 src[1] = {0x00AABBCC};
    u32 fb[1] = {0};
    blit_copy_rows((u8*)fb, 4, 32, src, 1, 0,0,1,1, 0,8,16);
    assert(fb[0] == 0x00CCBBAA);
}
static void test_copy_rows_subrect_offset(void){
    /* x/y offsets index both buffers: copy only pixel (1,1) of a 2x2 */
    u32 src[4] = {1,2,3,4};
    u32 fb[4] = {0,0,0,0};
    blit_copy_rows((u8*)fb, 8, 32, src, 2, 1,1,1,1, 16,8,0);
    assert(fb[0]==0 && fb[1]==0 && fb[2]==0 && fb[3]==4);
}
int main(void){
    test_clip_inside(); test_clip_partial_and_empty(); test_clip_rejects_bad_sizes();
    test_copy_rows_padded_pitch(); test_copy_rows_24bpp(); test_copy_rows_bgr();
    test_copy_rows_subrect_offset();
    puts("blit OK"); return 0;
}
