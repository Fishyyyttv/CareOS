/* tests/host/test_qoi.c — compiled with host gcc, not the kernel toolchain.
 * Provides a tiny known QOI stream and checks the decoded pixels. */
#include <stdint.h>
#include <string.h>
#include <stdio.h>
typedef uint8_t u8; typedef uint32_t u32;
int qoi_decode(const u8*, u32, u32*, u32, u32*, u32*);
/* 1x1 red via QOI_OP_RGB */
int main(void){
  u8 s[] = {'q','o','i','f', 0,0,0,1, 0,0,0,1, 4,0,
            0xfe,255,0,0, /* RGB red */
            0,0,0,0,0,0,0,1};
  u32 px[4]={0}, w=0,h=0;
  int rc = qoi_decode(s,sizeof(s),px,sizeof(px),&w,&h);
  if(rc!=0){ printf("FAIL rc=%d\n",rc); return 1; }
  if(w!=1||h!=1){ printf("FAIL dims %u %u\n",w,h); return 1; }
  if((px[0]&0xFFFFFF)!=0xFF0000){ printf("FAIL px=0x%06x\n",px[0]&0xFFFFFF); return 1; }
  {
    /* 2x1 half-transparent green: QOI_OP_RGBA then a run of one. The
     * alpha-preserving decoder must return 0xAARRGGBB for icons. */
    u8 s2[] = {'q','o','i','f', 0,0,0,2, 0,0,0,1, 4,0,
               0xff, 0,255,0,128,   /* RGBA */
               0xc0,                /* RUN 1 */
               0,0,0,0,0,0,0,1};
    u32 px2[2]={0}, w2=0, h2=0;
    extern int qoi_decode_argb(const u8*,u32,u32*,u32,u32*,u32*);
    if (qoi_decode_argb(s2,sizeof(s2),px2,sizeof(px2),&w2,&h2)!=0 || w2!=2 ||
        px2[0]!=0x8000FF00u || px2[1]!=0x8000FF00u)
      { printf("FAIL argb px=0x%08x 0x%08x\n",px2[0],px2[1]); return 1; }
    /* The original decoder keeps its 0x00RRGGBB contract. */
    if (qoi_decode(s2,sizeof(s2),px2,sizeof(px2),&w2,&h2)!=0 || px2[0]!=0x0000FF00u)
      { printf("FAIL rgb px=0x%08x\n",px2[0]); return 1; }
  }
  printf("qoi_decode OK\n"); return 0;
}
