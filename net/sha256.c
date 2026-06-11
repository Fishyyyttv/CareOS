/* net/sha256.c -- SHA-256 */
#include "kernel.h"

#define ROR32(x,n) (((x)>>(n))|((x)<<(32-(n))))
#define CH(e,f,g)  (((e)&(f))^(~(e)&(g)))
#define MAJ(a,b,c) (((a)&(b))^((a)&(c))^((b)&(c)))
#define EP0(a) (ROR32(a,2)^ROR32(a,13)^ROR32(a,22))
#define EP1(e) (ROR32(e,6)^ROR32(e,11)^ROR32(e,25))
#define SIG0(x)(ROR32(x,7)^ROR32(x,18)^((x)>>3))
#define SIG1(x)(ROR32(x,17)^ROR32(x,19)^((x)>>10))

static const u32 K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

static void sha256_block(u32 *h, const u8 *block) {
    u32 w[64], a,b,c,d,e,f,g,hh,t1,t2;
    for (int i=0;i<16;i++)
        w[i]=((u32)block[i*4]<<24)|((u32)block[i*4+1]<<16)|
             ((u32)block[i*4+2]<<8)|(u32)block[i*4+3];
    for (int i=16;i<64;i++)
        w[i]=SIG1(w[i-2])+w[i-7]+SIG0(w[i-15])+w[i-16];
    a=h[0];b=h[1];c=h[2];d=h[3];e=h[4];f=h[5];g=h[6];hh=h[7];
    for (int i=0;i<64;i++){
        t1=hh+EP1(e)+CH(e,f,g)+K[i]+w[i];
        t2=EP0(a)+MAJ(a,b,c);
        hh=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
    }
    h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;
    h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=hh;
}

void sha256(const u8 *data, u32 len, u8 *out) {
    u32 h[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
               0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    u8 buf[64];
    u32 i, rem = len & 63;

    /* full blocks */
    for (i=0; i+64<=len; i+=64) sha256_block(h, data+i);

    /* padding */
    kmemcpy(buf, data+i, rem);
    buf[rem] = 0x80;
    if (rem < 56) {
        kmemset(buf+rem+1, 0, 55-rem);
    } else {
        kmemset(buf+rem+1, 0, 63-rem);
        sha256_block(h, buf);
        kmemset(buf, 0, 56);
    }
    u64 bits = (u64)len * 8;
    buf[56]=(u8)(bits>>56); buf[57]=(u8)(bits>>48);
    buf[58]=(u8)(bits>>40); buf[59]=(u8)(bits>>32);
    buf[60]=(u8)(bits>>24); buf[61]=(u8)(bits>>16);
    buf[62]=(u8)(bits>>8);  buf[63]=(u8)(bits);
    sha256_block(h, buf);

    for (int j=0;j<8;j++){
        out[j*4]=(u8)(h[j]>>24); out[j*4+1]=(u8)(h[j]>>16);
        out[j*4+2]=(u8)(h[j]>>8); out[j*4+3]=(u8)h[j];
    }
}

void hmac_sha256(const u8 *key, u32 klen, const u8 *msg, u32 mlen, u8 *out) {
    u8 k[64], ipad[64], opad[64], inner[32];
    kmemset(k, 0, 64);
    if (klen > 64) sha256(key, klen, k);
    else kmemcpy(k, key, klen);
    for (int i=0;i<64;i++) { ipad[i]=k[i]^0x36; opad[i]=k[i]^0x5c; }

    /* inner = SHA256(ipad || msg) — done in two-shot via manual padding */
    /* use a temporary buffer approach */
    u8 *tmp = (u8*)kmalloc(64 + mlen);
    if (!tmp) return;
    kmemcpy(tmp, ipad, 64);
    kmemcpy(tmp+64, msg, mlen);
    sha256(tmp, 64+mlen, inner);
    kmemcpy(tmp, opad, 64);
    kmemcpy(tmp+64, inner, 32);
    sha256(tmp, 64+32, out);
    kfree(tmp);
}

/* HKDF-Extract: PRK = HMAC-SHA256(salt, ikm) */
void hkdf_extract(const u8 *salt, u32 slen,
                  const u8 *ikm,  u32 ikm_len, u8 *prk) {
    if (!salt || slen == 0) {
        u8 zeros[32]; kmemset(zeros,0,32);
        hmac_sha256(zeros,32,ikm,ikm_len,prk);
    } else {
        hmac_sha256(salt,slen,ikm,ikm_len,prk);
    }
}

/* HKDF-Expand: fills `out` with `olen` bytes */
void hkdf_expand(const u8 *prk, const u8 *info, u32 info_len,
                 u8 *out, u32 olen) {
    u8 t[32]; u8 cnt=1;
    u32 done=0;
    u8 *buf = (u8*)kmalloc(32 + info_len + 1);
    if (!buf) return;
    while (done < olen) {
        u32 off=0;
        if (cnt > 1) { kmemcpy(buf, t, 32); off=32; }
        kmemcpy(buf+off, info, info_len); off+=info_len;
        buf[off++]=cnt++;
        hmac_sha256(prk,32,buf,off,t);
        u32 n = olen-done; if (n>32) n=32;
        kmemcpy(out+done, t, n);
        done+=n;
    }
    kfree(buf);
}
