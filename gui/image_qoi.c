/* gui/image_qoi.c */
#ifdef HOST_TEST
typedef unsigned char u8; typedef unsigned int u32; typedef unsigned long long u64;
#else
#include "kernel.h"
#endif
#include "image_qoi.h"

#define QOI_OP_INDEX 0x00
#define QOI_OP_DIFF  0x40
#define QOI_OP_LUMA  0x80
#define QOI_OP_RUN   0xc0
#define QOI_OP_RGB   0xfe
#define QOI_OP_RGBA  0xff
#define QOI_MASK2    0xc0

static u32 rd32(const u8 *p){ return ((u32)p[0]<<24)|((u32)p[1]<<16)|((u32)p[2]<<8)|p[3]; }

int qoi_decode(const u8 *in, u32 in_len, u32 *out_px, u32 out_cap,
               u32 *out_w, u32 *out_h){
    if(!in || in_len < 22) return -1;
    if(in[0]!='q'||in[1]!='o'||in[2]!='i'||in[3]!='f') return -1;
    u32 w = rd32(in+4), h = rd32(in+8);
    if(w==0||h==0||w>4096||h>4096) return -1;
    u32 npx = w*h;
    if((u64)npx*4u > (u64)out_cap) return -1;   /* out buffer too small */
    u8 r=0,g=0,b=0,a=255;
    u8 idx[64*4]; for(int i=0;i<64*4;i++) idx[i]=0;
    u32 p = 14;           /* after header */
    u32 end = in_len - 8; /* before 8-byte end marker */
    u32 run = 0, out_i = 0;
    while(out_i < npx){
        if(run > 0){ run--; }
        else if(p < end){
            u8 op = in[p++];
            if(op==QOI_OP_RGB){ if(p+3>end) return -1; r=in[p++]; g=in[p++]; b=in[p++]; }
            else if(op==QOI_OP_RGBA){ if(p+4>end) return -1; r=in[p++]; g=in[p++]; b=in[p++]; a=in[p++]; }
            else if((op&QOI_MASK2)==QOI_OP_INDEX){ int h4=(op&63)*4; r=idx[h4]; g=idx[h4+1]; b=idx[h4+2]; a=idx[h4+3]; }
            else if((op&QOI_MASK2)==QOI_OP_DIFF){ r+=((op>>4)&3)-2; g+=((op>>2)&3)-2; b+=(op&3)-2; }
            else if((op&QOI_MASK2)==QOI_OP_LUMA){ if(p>=end) return -1; u8 b2=in[p++]; int vg=(op&63)-32; r+=vg+((b2>>4)&15)-8; g+=vg; b+=vg+(b2&15)-8; }
            else if((op&QOI_MASK2)==QOI_OP_RUN){ run=(op&63); }
            int hh=((r*3+g*5+b*7+a*11)&63)*4; idx[hh]=r; idx[hh+1]=g; idx[hh+2]=b; idx[hh+3]=a;
        } else return -1;
        out_px[out_i++] = ((u32)r<<16)|((u32)g<<8)|b;   /* 0x00RRGGBB */
    }
    *out_w = w; *out_h = h;
    return 0;
}
