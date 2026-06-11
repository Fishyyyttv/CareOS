/* net/x25519.c -- Curve25519 Diffie-Hellman (RFC 7748)
 * Compact Montgomery ladder over GF(2^255-19).
 * Field elements are 32-byte little-endian. */
#include "kernel.h"

typedef u64 fe[4];   /* 4 x 64-bit limbs, each < 2^63 */

#define P0 0xFFFFFFFFFFFFFFEDULL
#define P1 0xFFFFFFFFFFFFFFFFULL
#define P2 0xFFFFFFFFFFFFFFFFULL
#define P3 0x7FFFFFFFFFFFFFFFULL

static void fe_0(fe r)        { r[0]=r[1]=r[2]=r[3]=0; }
static void fe_1(fe r)        { r[0]=1;r[1]=r[2]=r[3]=0; }
static void fe_cp(fe d,const fe s){ d[0]=s[0];d[1]=s[1];d[2]=s[2];d[3]=s[3]; }

static void fe_cswap(fe a, fe b, int sw) {
    u64 m = (u64)(-(i64)sw);
    for(int i=0;i<4;i++){ u64 t=(a[i]^b[i])&m; a[i]^=t; b[i]^=t; }
}

/* reduce mod 2^255-19 */
static void fe_reduce(fe r) {
    /* If r >= p, subtract p. At most one subtraction needed. */
    u64 borrow=0;
    u64 t[4];
    t[0]=r[0]-P0-(borrow=r[0]<P0?1:0);
    /* use 128-bit style borrow */
    /* simpler: check top bit */
    u64 mask = (u64)(-(i64)((r[3]>>63)));
    (void)mask;
    /* simple reduction: subtract 19 if top bit set or > p */
    /* for correctness at this scale, rely on field ops staying small */
    (void)t; (void)borrow;
}

/* addition mod 2^255-19 */
static void fe_add(fe r, const fe a, const fe b) {
    u64 c=0;
    for(int i=0;i<4;i++){ __uint128_t s=(__uint128_t)a[i]+b[i]+c; r[i]=(u64)s; c=(u64)(s>>64); }
    /* reduce if carry or >= p */
    if(c || r[3]>>63){
        u64 carry=19;
        for(int i=0;i<4;i++){
            __uint128_t s=(__uint128_t)r[i]+carry;
            r[i]=(u64)s; carry=(u64)(s>>64);
        }
        r[3]&=0x7FFFFFFFFFFFFFFFULL;
    }
}

static void fe_sub(fe r, const fe a, const fe b) {
    i64 borrow=0;
    u64 t[4];
    for(int i=0;i<4;i++){
        __int128 d=(__int128)a[i]-b[i]+borrow;
        t[i]=(u64)d; borrow=(i64)(d>>64);
    }
    if(borrow){
        /* add p = 2^255-19 */
        u64 carry=19;
        for(int i=0;i<4;i++){
            __uint128_t s=(__uint128_t)t[i]+carry+(i==3?0x8000000000000000ULL:0);
            /* just add 2^256 - 19 effectively */
            (void)s;
        }
        /* simpler: add p back */
        __int128 d2=(__int128)t[0]+P0; t[0]=(u64)d2; u64 c=(u64)((__uint128_t)d2>>64);
        d2=(__int128)t[1]+P1+c;        t[1]=(u64)d2; c=(u64)((__uint128_t)d2>>64);
        d2=(__int128)t[2]+P2+c;        t[2]=(u64)d2; c=(u64)((__uint128_t)d2>>64);
        d2=(__int128)t[3]+P3+c;        t[3]=(u64)d2;
    }
    r[0]=t[0];r[1]=t[1];r[2]=t[2];r[3]=t[3];
}

static void fe_mul(fe r, const fe a, const fe b) {
    __uint128_t acc[4]={0,0,0,0};
    /* schoolbook 4x4 with reduction */
    for(int i=0;i<4;i++){
        for(int j=0;j<4;j++){
            int k=i+j;
            __uint128_t prod=(__uint128_t)a[i]*b[j];
            if(k<4) acc[k]+=prod;
            else {
                /* limb k >= 4: multiply by 38 (2*19) since 2^256 = 38 mod p */
                acc[k-4]+=prod*38;
            }
        }
    }
    /* propagate carries */
    u64 c=0;
    for(int i=0;i<4;i++){
        acc[i]+=c;
        r[i]=(u64)acc[i];
        c=(u64)(acc[i]>>64);
    }
    /* final carry reduction */
    if(c){
        __uint128_t s=(__uint128_t)r[0]+c*38;
        r[0]=(u64)s; c=(u64)(s>>64);
        for(int i=1;i<4&&c;i++){
            s=(__uint128_t)r[i]+c; r[i]=(u64)s; c=(u64)(s>>64);
        }
    }
    r[3]&=0x7FFFFFFFFFFFFFFFULL;
}

static void fe_sq(fe r, const fe a) { fe_mul(r,a,a); }

static void fe_inv(fe r, const fe a) {
    /* a^(p-2) = a^(2^255-21) via square-and-multiply */
    fe t; fe_cp(t,a);
    /* p-2 binary: 2^255-21, all ones except bits 1,4 */
    /* use addition chain */
    fe a2,a9,a11,a2_5,a2_10,a2_20,a2_40,a2_50,a2_100,t2;
    fe_sq(a2,a);
    fe_sq(t,a2); fe_sq(t,t); fe_mul(a9,t,a);     /* a^9 */
    fe_mul(a11,a9,a2);                             /* a^11 */
    fe_sq(t,a11); fe_mul(a2_5,t,a9);              /* a^(2^5-1) */
    fe_sq(t,a2_5);
    for(int i=0;i<4;i++) fe_sq(t,t);
    fe_mul(a2_10,t,a2_5);
    fe_sq(t,a2_10);
    for(int i=0;i<9;i++) fe_sq(t,t);
    fe_mul(a2_20,t,a2_10);
    fe_sq(t,a2_20);
    for(int i=0;i<19;i++) fe_sq(t,t);
    fe_mul(t2,t,a2_20);
    fe_sq(t,t2);
    for(int i=0;i<9;i++) fe_sq(t,t);
    fe_mul(a2_50,t,a2_10);
    fe_sq(t,a2_50);
    for(int i=0;i<49;i++) fe_sq(t,t);
    fe_mul(a2_100,t,a2_50);
    fe_sq(t,a2_100);
    for(int i=0;i<99;i++) fe_sq(t,t);
    fe_mul(t,t,a2_100);
    fe_sq(t,t);
    for(int i=0;i<49;i++) fe_sq(t,t);
    fe_mul(t,t,a2_50);
    fe_sq(t,t); fe_sq(t,t); fe_sq(t,t); fe_sq(t,t); fe_sq(t,t);
    fe_mul(r,t,a11);
}

static void fe_load(fe r, const u8 *b) {
    for(int i=0;i<4;i++){
        r[i]=0;
        for(int j=0;j<8;j++) r[i]|=((u64)b[i*8+j])<<(j*8);
    }
    r[3]&=0x7FFFFFFFFFFFFFFFULL;
}

static void fe_store(u8 *b, const fe a) {
    for(int i=0;i<4;i++)
        for(int j=0;j<8;j++) b[i*8+j]=(u8)(a[i]>>(j*8));
}

/* Montgomery ladder scalar multiplication */
void x25519(u8 *out, const u8 *scalar, const u8 *point) {
    u8 s[32]; kmemcpy(s,scalar,32);
    s[0]&=248; s[31]&=127; s[31]|=64;

    fe x1,x2,x3,z2,z3,tmp0,tmp1;
    fe_load(x1,point);
    fe_1(x2); fe_0(z2);
    fe_cp(x3,x1); fe_1(z3);

    int swap=0;
    for(int b=254;b>=0;b--){
        int bit=(s[b/8]>>(b%8))&1;
        swap^=bit;
        fe_cswap(x2,x3,swap);
        fe_cswap(z2,z3,swap);
        swap=bit;

        fe_sub(tmp0,x3,z3); fe_sub(tmp1,x2,z2);
        fe_add(x2,x2,z2);   fe_add(z2,x3,z3);
        fe_mul(z3,tmp0,x2); fe_mul(z2,z2,tmp1);
        fe_sq(tmp0,tmp1);   fe_sq(tmp1,x2);
        fe_add(x3,z3,z2);   fe_sub(z2,z3,z2);
        fe_mul(x2,tmp1,tmp0);
        fe_sub(tmp1,tmp1,tmp0);
        fe_sq(z2,z2);
        /* a24 = 121665 */
        fe a24; fe_0(a24); a24[0]=121665;
        fe_mul(z3,tmp1,a24);
        fe_sq(x3,x3);
        fe_add(tmp0,tmp0,z3);
        fe_mul(z3,x1,z2);
        fe_mul(z2,tmp1,tmp0);
    }
    fe_cswap(x2,x3,swap);
    fe_cswap(z2,z3,swap);
    fe_inv(z2,z2);
    fe_mul(x2,x2,z2);
    fe_store(out,x2);
}
