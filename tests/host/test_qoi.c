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
  printf("qoi_decode OK\n"); return 0;
}
