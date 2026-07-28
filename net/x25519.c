/* net/x25519.c -- Curve25519 Diffie-Hellman (RFC 7748)
 * Montgomery ladder over GF(2^255-19).
 * Field elements are 5 x 51-bit limbs (radix 2^51), little-endian.
 * Field arithmetic follows the public-domain curve25519-donna-c64 design,
 * which keeps all products within a 128-bit accumulator. */
#include "kernel.h"

typedef u64 fe[5];
#define MASK51 0x7ffffffffffffULL

static u64 load64(const u8 *b) {
    u64 r=0;
    for(int i=0;i<8;i++) r |= ((u64)b[i])<<(i*8);
    return r;
}

static void fe_0(fe r){ r[0]=r[1]=r[2]=r[3]=r[4]=0; }
static void fe_1(fe r){ r[0]=1; r[1]=r[2]=r[3]=r[4]=0; }
static void fe_cp(fe d,const fe s){ for(int i=0;i<5;i++) d[i]=s[i]; }

static void fe_cswap(fe a, fe b, u64 sw) {
    u64 m = (u64)(-(i64)sw);
    for(int i=0;i<5;i++){ u64 t=(a[i]^b[i])&m; a[i]^=t; b[i]^=t; }
}

/* out = a + b (limbs grow a bit; reduced by the next mul/square) */
static void fe_add(fe out, const fe a, const fe b) {
    for(int i=0;i<5;i++) out[i]=a[i]+b[i];
}

/* out = a - b (mod p); add 2p to stay non-negative */
static void fe_sub(fe out, const fe a, const fe b) {
    out[0] = a[0] + 0xfffffffffffdaULL - b[0]; /* 2*(2^51-19) */
    out[1] = a[1] + 0xffffffffffffeULL - b[1]; /* 2*(2^51-1)  */
    out[2] = a[2] + 0xffffffffffffeULL - b[2];
    out[3] = a[3] + 0xffffffffffffeULL - b[3];
    out[4] = a[4] + 0xffffffffffffeULL - b[4];
}

static void fe_mul(fe out, const fe in, const fe in2) {
    __uint128_t t[5];
    u64 r0=in[0],r1=in[1],r2=in[2],r3=in[3],r4=in[4];
    u64 s0=in2[0],s1=in2[1],s2=in2[2],s3=in2[3],s4=in2[4];
    u64 c;

    t[0] = (__uint128_t)r0*s0;
    t[1] = (__uint128_t)r0*s1 + (__uint128_t)r1*s0;
    t[2] = (__uint128_t)r0*s2 + (__uint128_t)r2*s0 + (__uint128_t)r1*s1;
    t[3] = (__uint128_t)r0*s3 + (__uint128_t)r3*s0 + (__uint128_t)r1*s2 + (__uint128_t)r2*s1;
    t[4] = (__uint128_t)r0*s4 + (__uint128_t)r4*s0 + (__uint128_t)r3*s1 + (__uint128_t)r1*s3 + (__uint128_t)r2*s2;

    r4*=19; r1*=19; r2*=19; r3*=19;

    t[0] += (__uint128_t)r4*s1 + (__uint128_t)r1*s4 + (__uint128_t)r2*s3 + (__uint128_t)r3*s2;
    t[1] += (__uint128_t)r4*s2 + (__uint128_t)r2*s4 + (__uint128_t)r3*s3;
    t[2] += (__uint128_t)r4*s3 + (__uint128_t)r3*s4;
    t[3] += (__uint128_t)r4*s4;

                 r0=(u64)t[0]&MASK51; c=(u64)(t[0]>>51);
    t[1]+=c;     r1=(u64)t[1]&MASK51; c=(u64)(t[1]>>51);
    t[2]+=c;     r2=(u64)t[2]&MASK51; c=(u64)(t[2]>>51);
    t[3]+=c;     r3=(u64)t[3]&MASK51; c=(u64)(t[3]>>51);
    t[4]+=c;     r4=(u64)t[4]&MASK51; c=(u64)(t[4]>>51);
    r0 += c*19;  c=r0>>51; r0&=MASK51;
    r1 += c;     c=r1>>51; r1&=MASK51;
    r2 += c;

    out[0]=r0; out[1]=r1; out[2]=r2; out[3]=r3; out[4]=r4;
}

static void fe_sq(fe out, const fe in) {
    __uint128_t t[5];
    u64 r0=in[0],r1=in[1],r2=in[2],r3=in[3],r4=in[4];
    u64 c;
    u64 d0=r0*2, d1=r1*2, d2=r2*2*19, d419=r4*19, d4=d419*2;

    t[0] = (__uint128_t)r0*r0 + (__uint128_t)d4*r1 + (__uint128_t)d2*r3;
    t[1] = (__uint128_t)d0*r1 + (__uint128_t)d4*r2 + (__uint128_t)r3*(r3*19);
    t[2] = (__uint128_t)d0*r2 + (__uint128_t)r1*r1 + (__uint128_t)d4*r3;
    t[3] = (__uint128_t)d0*r3 + (__uint128_t)d1*r2 + (__uint128_t)r4*d419;
    t[4] = (__uint128_t)d0*r4 + (__uint128_t)d1*r3 + (__uint128_t)r2*r2;

                 r0=(u64)t[0]&MASK51; c=(u64)(t[0]>>51);
    t[1]+=c;     r1=(u64)t[1]&MASK51; c=(u64)(t[1]>>51);
    t[2]+=c;     r2=(u64)t[2]&MASK51; c=(u64)(t[2]>>51);
    t[3]+=c;     r3=(u64)t[3]&MASK51; c=(u64)(t[3]>>51);
    t[4]+=c;     r4=(u64)t[4]&MASK51; c=(u64)(t[4]>>51);
    r0 += c*19;  c=r0>>51; r0&=MASK51;
    r1 += c;     c=r1>>51; r1&=MASK51;
    r2 += c;

    out[0]=r0; out[1]=r1; out[2]=r2; out[3]=r3; out[4]=r4;
}

static void fe_sq_times(fe out, const fe in, int n) {
    fe t; fe_cp(t,in);
    for(int i=0;i<n;i++) fe_sq(t,t);
    fe_cp(out,t);
}

/* out = 1/z  (z^(p-2)) via the donna addition chain */
static void fe_inv(fe out, const fe z) {
    fe a,t0,b,c;
    fe_sq_times(a,z,1);                 /* 2 */
    fe_sq_times(t0,a,2);                /* 8 */
    fe_mul(b,t0,z);                     /* 9 */
    fe_mul(a,b,a);                      /* 11 */
    fe_sq_times(t0,a,1);                /* 22 */
    fe_mul(b,t0,b);                     /* 2^5 - 1 */
    fe_sq_times(t0,b,5);                fe_mul(b,t0,b);   /* 2^10 - 1 */
    fe_sq_times(t0,b,10);               fe_mul(c,t0,b);   /* 2^20 - 1 */
    fe_sq_times(t0,c,20);               fe_mul(t0,t0,c);  /* 2^40 - 1 */
    fe_sq_times(t0,t0,10);              fe_mul(b,t0,b);   /* 2^50 - 1 */
    fe_sq_times(t0,b,50);               fe_mul(c,t0,b);   /* 2^100 - 1 */
    fe_sq_times(t0,c,100);              fe_mul(t0,t0,c);  /* 2^200 - 1 */
    fe_sq_times(t0,t0,50);              fe_mul(t0,t0,b);  /* 2^250 - 1 */
    fe_sq_times(t0,t0,5);               fe_mul(out,t0,a); /* 2^255 - 21 */
}

static void fe_expand(fe out, const u8 *in) {
    out[0] =  load64(in)       & MASK51;
    out[1] = (load64(in+6)>>3) & MASK51;
    out[2] = (load64(in+12)>>6)& MASK51;
    out[3] = (load64(in+19)>>1)& MASK51;
    out[4] = (load64(in+24)>>12)& MASK51;
}

static void fe_contract(u8 *out, const fe input) {
    u64 t[5]; for(int i=0;i<5;i++) t[i]=input[i];

    #define CARRY \
        t[1]+=t[0]>>51; t[0]&=MASK51; \
        t[2]+=t[1]>>51; t[1]&=MASK51; \
        t[3]+=t[2]>>51; t[2]&=MASK51; \
        t[4]+=t[3]>>51; t[3]&=MASK51; \
        t[0]+=19*(t[4]>>51); t[4]&=MASK51;
    CARRY; CARRY;

    /* At this point 0 <= t < 2^255. Add 19 and carry to detect >= p. */
    t[0]+=19; CARRY;

    /* Subtract p by adding 2^255-19 and masking off 2^255. */
    t[0]+=0x8000000000000ULL-19;
    t[1]+=0x8000000000000ULL-1;
    t[2]+=0x8000000000000ULL-1;
    t[3]+=0x8000000000000ULL-1;
    t[4]+=0x8000000000000ULL-1;
    t[1]+=t[0]>>51; t[0]&=MASK51;
    t[2]+=t[1]>>51; t[1]&=MASK51;
    t[3]+=t[2]>>51; t[2]&=MASK51;
    t[4]+=t[3]>>51; t[3]&=MASK51;
    t[4]&=MASK51;
    #undef CARRY

    u64 w0 =  t[0]        | (t[1]<<51);
    u64 w1 = (t[1]>>13)   | (t[2]<<38);
    u64 w2 = (t[2]>>26)   | (t[3]<<25);
    u64 w3 = (t[3]>>39)   | (t[4]<<12);
    for(int i=0;i<8;i++) out[i]    =(u8)(w0>>(i*8));
    for(int i=0;i<8;i++) out[8+i]  =(u8)(w1>>(i*8));
    for(int i=0;i<8;i++) out[16+i] =(u8)(w2>>(i*8));
    for(int i=0;i<8;i++) out[24+i] =(u8)(w3>>(i*8));
}

/* Montgomery ladder scalar multiplication */
void x25519(u8 *out, const u8 *scalar, const u8 *point) {
    u8 s[32]; kmemcpy(s,scalar,32);
    s[0]&=248; s[31]&=127; s[31]|=64;

    fe x1,x2,z2,x3,z3;
    fe_expand(x1,point);
    fe_1(x2); fe_0(z2);
    fe_cp(x3,x1); fe_1(z3);

    u64 swap=0;
    for(int b=254;b>=0;b--){
        u64 bit=(s[b/8]>>(b%8))&1;
        swap^=bit;
        fe_cswap(x2,x3,swap);
        fe_cswap(z2,z3,swap);
        swap=bit;

        /* RFC 7748 Montgomery ladder step */
        fe A,AA,B,BB,E,C,D,DA,CB,t;
        fe_add(A,x2,z2);   fe_sq(AA,A);
        fe_sub(B,x2,z2);   fe_sq(BB,B);
        fe_sub(E,AA,BB);
        fe_add(C,x3,z3);
        fe_sub(D,x3,z3);
        fe_mul(DA,D,A);
        fe_mul(CB,C,B);
        fe_add(t,DA,CB);   fe_sq(x3,t);            /* x3 = (DA+CB)^2 */
        fe_sub(t,DA,CB);   fe_sq(t,t); fe_mul(z3,x1,t); /* z3 = x1*(DA-CB)^2 */
        fe_mul(x2,AA,BB);                          /* x2 = AA*BB */
        { fe a24; fe_0(a24); a24[0]=121665;
          fe_mul(t,a24,E); fe_add(t,AA,t); fe_mul(z2,E,t); } /* z2 = E*(AA+a24*E) */
    }
    fe_cswap(x2,x3,swap);
    fe_cswap(z2,z3,swap);
    fe_inv(z2,z2);
    fe_mul(x2,x2,z2);
    fe_contract(out,x2);
}
