/* net/aes_gcm.c -- AES-128 + GCM authenticated encryption */
#include "kernel.h"

/* ── AES-128 ─────────────────────────────────────────────────────── */
static const u8 sbox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

static const u8 rcon[11] = {0x00,0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36};

static u8 xtime(u8 a) { return (u8)((a<<1)^(a&0x80?0x1b:0)); }
static u8 mul(u8 a, u8 b) {
    u8 r=0; for(int i=0;i<8;i++){if(b&1)r^=a;a=xtime(a);b>>=1;} return r;
}

typedef struct { u32 rk[44]; } aes128_ctx_t;

void aes128_init(void *vctx, const u8 *key) {
    aes128_ctx_t *ctx = (aes128_ctx_t*)vctx;
    u8 *w = (u8*)ctx->rk;
    kmemcpy(w, key, 16);
    for (int i=16; i<176; i+=4) {
        u8 t[4]; kmemcpy(t, w+i-4, 4);
        if ((i%16)==0) {
            u8 tmp=t[0]; t[0]=sbox[t[1]]^rcon[i/16]; t[1]=sbox[t[2]];
            t[2]=sbox[t[3]]; t[3]=sbox[tmp];
        }
        for(int j=0;j<4;j++) w[i+j]=w[i-16+j]^t[j];
    }
}

void aes128_encrypt(const aes128_ctx_t *ctx, const u8 *in, u8 *out) {
    const u8 *rk = (const u8*)ctx->rk;
    u8 s[16];
    for(int i=0;i<16;i++) s[i]=in[i]^rk[i];
    for(int r=1;r<=10;r++){
        /* SubBytes */
        for(int i=0;i<16;i++) s[i]=sbox[s[i]];
        /* ShiftRows */
        u8 t;
        t=s[1];s[1]=s[5];s[5]=s[9];s[9]=s[13];s[13]=t;
        t=s[2];s[2]=s[10];u8 t2=s[6];s[6]=s[14];s[14]=t;s[10]=t2;
        t=s[15];s[15]=s[11];s[11]=s[7];s[7]=s[3];s[3]=t;
        /* MixColumns (skip on last round) */
        if(r<10){
            for(int c=0;c<4;c++){
                u8 a0=s[c*4],a1=s[c*4+1],a2=s[c*4+2],a3=s[c*4+3];
                s[c*4]  =mul(0x02,a0)^mul(0x03,a1)^a2^a3;
                s[c*4+1]=a0^mul(0x02,a1)^mul(0x03,a2)^a3;
                s[c*4+2]=a0^a1^mul(0x02,a2)^mul(0x03,a3);
                s[c*4+3]=mul(0x03,a0)^a1^a2^mul(0x02,a3);
            }
        }
        /* AddRoundKey */
        for(int i=0;i<16;i++) s[i]^=rk[r*16+i];
    }
    kmemcpy(out, s, 16);
}

/* ── GCM ─────────────────────────────────────────────────────────── */
static void gcm_mul(u8 *x, const u8 *h) {
    u8 z[16], v[16];
    kmemset(z,0,16);
    kmemcpy(v,h,16);
    for(int i=0;i<16;i++){
        for(int b=7;b>=0;b--){
            if(x[i]&(1<<b)){
                for(int j=0;j<16;j++) z[j]^=v[j];
            }
            u8 lsb=v[15]&1;
            for(int j=15;j>0;j--) v[j]=(v[j]>>1)|(v[j-1]<<7);
            v[0]>>=1;
            if(lsb) v[0]^=0xe1;
        }
    }
    kmemcpy(x,z,16);
}

static void gcm_ghash(const u8 *h, const u8 *data, u32 dlen, u8 *tag) {
    for(u32 i=0;i<dlen;i+=16){
        u8 blk[16]={0};
        u32 n=dlen-i; if(n>16)n=16;
        kmemcpy(blk,data+i,n);
        for(int j=0;j<16;j++) tag[j]^=blk[j];
        gcm_mul(tag,h);
    }
}

static void gcm_inc32(u8 *ctr) {
    for(int i=15;i>=12;i--) if(++ctr[i]) break;
}

/* Encrypt+authenticate. iv=12 bytes, tag=16 bytes output */
void aes128_gcm_encrypt(const void *vctx,
                        const u8 *iv,
                        const u8 *aad, u32 aad_len,
                        const u8 *plain, u32 plen,
                        u8 *cipher, u8 *tag) {
    const aes128_ctx_t *ctx = (const aes128_ctx_t*)vctx;
    /* H = AES(key, 0) */
    u8 h[16]={0}; aes128_encrypt(ctx,h,h);

    /* J0 = IV || 0x00000001 */
    u8 j0[16]={0};
    kmemcpy(j0,iv,12); j0[15]=1;

    /* encrypt counter starting at J0+1 */
    u8 ctr[16]; kmemcpy(ctr,j0,16); gcm_inc32(ctr);
    for(u32 i=0;i<plen;i+=16){
        u8 ks[16]; aes128_encrypt(ctx,ctr,ks); gcm_inc32(ctr);
        u32 n=plen-i; if(n>16)n=16;
        for(u32 j=0;j<n;j++) cipher[i+j]=plain[i+j]^ks[j];
    }

    /* GHASH(H, AAD || CT) */
    u8 gtag[16]={0};
    gcm_ghash(h,aad,aad_len,gtag);
    gcm_ghash(h,cipher,plen,gtag);
    /* lengths block */
    u8 lb[16]={0};
    u64 al=(u64)aad_len*8, pl=(u64)plen*8;
    lb[4]=(u8)(al>>24);lb[5]=(u8)(al>>16);lb[6]=(u8)(al>>8);lb[7]=(u8)al;
    lb[12]=(u8)(pl>>24);lb[13]=(u8)(pl>>16);lb[14]=(u8)(pl>>8);lb[15]=(u8)pl;
    for(int i=0;i<16;i++) gtag[i]^=lb[i];
    gcm_mul(gtag,h);

    /* tag = GHASH ^ AES(J0) */
    u8 s[16]; aes128_encrypt(ctx,j0,s);
    for(int i=0;i<16;i++) tag[i]=gtag[i]^s[i];
}

/* Decrypt+verify. Returns 0 on auth success, -1 on tag mismatch */
int aes128_gcm_decrypt(const void *vctx,
                       const u8 *iv,
                       const u8 *aad, u32 aad_len,
                       const u8 *cipher, u32 clen,
                       u8 *plain, const u8 *tag) {
    const aes128_ctx_t *ctx = (const aes128_ctx_t*)vctx;
    u8 h[16]={0}; aes128_encrypt(ctx,h,h);
    u8 j0[16]={0}; kmemcpy(j0,iv,12); j0[15]=1;

    /* verify tag first */
    u8 gtag[16]={0};
    gcm_ghash(h,aad,aad_len,gtag);
    gcm_ghash(h,cipher,clen,gtag);
    u8 lb[16]={0};
    u64 al=(u64)aad_len*8, pl=(u64)clen*8;
    lb[4]=(u8)(al>>24);lb[5]=(u8)(al>>16);lb[6]=(u8)(al>>8);lb[7]=(u8)al;
    lb[12]=(u8)(pl>>24);lb[13]=(u8)(pl>>16);lb[14]=(u8)(pl>>8);lb[15]=(u8)pl;
    for(int i=0;i<16;i++) gtag[i]^=lb[i];
    gcm_mul(gtag,h);
    u8 s[16]; aes128_encrypt(ctx,j0,s);
    u8 expected[16];
    for(int i=0;i<16;i++) expected[i]=gtag[i]^s[i];

    int ok=0;
    for(int i=0;i<16;i++) ok|=(expected[i]^tag[i]);
    if(ok) return -1;

    /* decrypt */
    u8 ctr[16]; kmemcpy(ctr,j0,16); gcm_inc32(ctr);
    for(u32 i=0;i<clen;i+=16){
        u8 ks[16]; aes128_encrypt(ctx,ctr,ks); gcm_inc32(ctr);
        u32 n=clen-i; if(n>16)n=16;
        for(u32 j=0;j<n;j++) plain[i+j]=cipher[i+j]^ks[j];
    }
    return 0;
}
