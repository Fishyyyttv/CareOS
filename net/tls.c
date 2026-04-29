/* net/tls.c -- TLS 1.3 client (RFC 8446)
 * Cipher suite: TLS_AES_128_GCM_SHA256
 * Key exchange: x25519
 * Certificate verification: skipped (trust all)
 */
#include "kernel.h"

/* ── crypto forward declarations ─────────────────────────────────── */
void sha256(const u8 *data, u32 len, u8 *out);
void hmac_sha256(const u8 *key, u32 klen, const u8 *msg, u32 mlen, u8 *out);
void hkdf_extract(const u8 *salt, u32 slen, const u8 *ikm, u32 ikm_len, u8 *prk);
void hkdf_expand(const u8 *prk, const u8 *info, u32 info_len, u8 *out, u32 olen);
void x25519(u8 *out, const u8 *scalar, const u8 *point);
void aes128_init(void *ctx, const u8 *key);
void aes128_gcm_encrypt(const void *ctx, const u8 *iv,
                        const u8 *aad, u32 aad_len,
                        const u8 *plain, u32 plen, u8 *cipher, u8 *tag);
int  aes128_gcm_decrypt(const void *ctx, const u8 *iv,
                        const u8 *aad, u32 aad_len,
                        const u8 *cipher, u32 clen, u8 *plain, const u8 *tag);

/* ── constants ───────────────────────────────────────────────────── */
#define TLS_RECORD_MAX    16384
#define TLS_BUF_SZ        (TLS_RECORD_MAX + 256)

/* Record content types */
#define TLS_CHANGE_CIPHER 20
#define TLS_ALERT         21
#define TLS_HANDSHAKE     22
#define TLS_APPDATA       23

/* Handshake types */
#define HS_CLIENT_HELLO    1
#define HS_SERVER_HELLO    2
#define HS_ENCRYPTED_EXT   8
#define HS_CERTIFICATE    11
#define HS_CERT_VERIFY    15
#define HS_FINISHED       20

/* Extensions */
#define EXT_SERVER_NAME       0x0000
#define EXT_SUPPORTED_GROUPS  0x000a
#define EXT_SIG_ALGS          0x000d
#define EXT_KEY_SHARE         0x0033
#define EXT_SUPPORTED_VER     0x002b
#define EXT_PSK_MODES         0x002d

/* ── TLS session state ───────────────────────────────────────────── */
typedef struct {
    int      fd;
    u8       client_random[32];
    u8       server_random[32];
    u8       my_priv[32];
    u8       my_pub[32];
    u8       handshake_secret[32];
    u8       master_secret[32];
    u8       client_hs_secret[32];
    u8       server_hs_secret[32];
    u8       client_ap_secret[32];
    u8       server_ap_secret[32];
    u8       client_hs_key[16];
    u8       client_hs_iv[12];
    u8       server_hs_key[16];
    u8       server_hs_iv[12];
    u8       client_ap_key[16];
    u8       client_ap_iv[12];
    u8       server_ap_key[16];
    u8       server_ap_iv[12];
    u8       hs_hash[32];       /* running hash of handshake messages */
    /* running SHA-256 state via transcript buffer */
    u8      *hs_transcript;
    u32      hs_transcript_len;
    u32      hs_transcript_cap;
    u64      send_seq;
    u64      recv_seq;
    bool     handshake_done;
} tls_ctx_t;

/* ── RNG ─────────────────────────────────────────────────────────── */
static void tls_rand(u8 *buf, u32 n) {
    /* use timer + counter as entropy source */
    static u32 ctr = 0x12345678;
    for(u32 i=0;i<n;i++){
        ctr = ctr*1664525u + 1013904223u + timer_get_ticks();
        buf[i] = (u8)(ctr ^ (ctr>>8) ^ (ctr>>16));
        ctr++;
    }
}

/* ── TCP send/recv wrappers ──────────────────────────────────────── */
static int tls_tcp_send(int fd, const u8 *buf, u32 len) {
    return sock_send(fd, buf, len);
}

static int tls_tcp_recv(int fd, u8 *buf, u32 maxlen) {
    return sock_recv(fd, buf, maxlen);
}

static int tls_tcp_read_exact(int fd, u8 *buf, u32 want) {
    u32 got = 0;
    while (got < want) {
        int n = tls_tcp_recv(fd, buf + got, want - got);
        if (n <= 0) return -1;
        got += (u32)n;
    }
    return (int)got;
}

/* ── Write helpers ───────────────────────────────────────────────── */
static void w1(u8 *p, int *off, u8 v)  { p[(*off)++]=v; }
static void w2(u8 *p, int *off, u16 v) { p[(*off)++]=(u8)(v>>8); p[(*off)++]=(u8)v; }
static void w3(u8 *p, int *off, u32 v) { p[(*off)++]=(u8)(v>>16); p[(*off)++]=(u8)(v>>8); p[(*off)++]=(u8)v; }
static void wb(u8 *p, int *off, const u8 *src, u32 n) { kmemcpy(p+*off,src,n); *off+=(int)n; }
static u16 r2(const u8 *p) { return ((u16)p[0]<<8)|p[1]; }
static u32 r3(const u8 *p) { return ((u32)p[0]<<16)|((u32)p[1]<<8)|p[2]; }

/* ── Transcript hash ─────────────────────────────────────────────── */
static void transcript_add(tls_ctx_t *c, const u8 *data, u32 len) {
    if (c->hs_transcript_len + len > c->hs_transcript_cap) {
        u32 newcap = c->hs_transcript_cap + len + 4096;
        u8 *nb = (u8*)kmalloc(newcap);
        if (!nb) return;
        kmemcpy(nb, c->hs_transcript, c->hs_transcript_len);
        kfree(c->hs_transcript);
        c->hs_transcript = nb;
        c->hs_transcript_cap = newcap;
    }
    kmemcpy(c->hs_transcript + c->hs_transcript_len, data, len);
    c->hs_transcript_len += len;
}

static void transcript_hash(tls_ctx_t *c, u8 *out) {
    sha256(c->hs_transcript, c->hs_transcript_len, out);
}

/* ── TLS record send ─────────────────────────────────────────────── */
static int record_send_raw(tls_ctx_t *c, u8 type, const u8 *data, u32 len) {
    u8 hdr[5];
    hdr[0]=type; hdr[1]=0x03; hdr[2]=0x03;
    hdr[3]=(u8)(len>>8); hdr[4]=(u8)len;
    if (tls_tcp_send(c->fd, hdr, 5) < 0) return -1;
    if (tls_tcp_send(c->fd, data, len) < 0) return -1;
    return 0;
}

/* Send encrypted record (after handshake keys are available) */
static int record_send_enc(tls_ctx_t *c, u8 inner_type,
                           const u8 *data, u32 dlen,
                           const u8 *key, const u8 *base_iv, u64 *seq) {
    /* plaintext = data || inner_type */
    u8 *plain = (u8*)kmalloc(dlen + 1);
    if (!plain) return -1;
    kmemcpy(plain, data, dlen);
    plain[dlen] = inner_type;
    u32 plen = dlen + 1;

    /* ciphertext length = plen + 16 (GCM tag) */
    u32 clen = plen + 16;

    /* build IV: base_iv XOR seq */
    u8 iv[12]; kmemcpy(iv, base_iv, 12);
    for (int i=0;i<8;i++) iv[11-i] ^= (u8)((*seq)>>(i*8));

    /* AAD = TLSCiphertext header: type=23, version=0x0303, length */
    u8 aad[5];
    aad[0]=TLS_APPDATA; aad[1]=0x03; aad[2]=0x03;
    aad[3]=(u8)(clen>>8); aad[4]=(u8)clen;

    u8 *cipher = (u8*)kmalloc(clen);
    if (!cipher) { kfree(plain); return -1; }

    /* AES context */
    u8 actx[256]; /* aes128_ctx_t is ~176 bytes */
    aes128_init(actx, key);
    aes128_gcm_encrypt(actx, iv, aad, 5, plain, plen,
                       cipher, cipher + plen);

    /* send record header + ciphertext */
    u8 hdr[5];
    hdr[0]=TLS_APPDATA; hdr[1]=0x03; hdr[2]=0x03;
    hdr[3]=(u8)(clen>>8); hdr[4]=(u8)clen;
    tls_tcp_send(c->fd, hdr, 5);
    tls_tcp_send(c->fd, cipher, clen);

    (*seq)++;
    kfree(plain); kfree(cipher);
    return 0;
}

/* ── TLS record receive ──────────────────────────────────────────── */
static int record_recv(tls_ctx_t *c, u8 *type_out,
                       u8 *buf, u32 maxlen) {
    u8 hdr[5];
    int n = tls_tcp_read_exact(c->fd, hdr, 5);
    if (n < 5) return -1;
    *type_out = hdr[0];
    u32 rlen = r2(hdr+3);
    if (rlen > maxlen) return -1;
    n = tls_tcp_read_exact(c->fd, buf, rlen);
    if ((u32)n < rlen) return -1;
    return (int)rlen;
}

/* Receive and decrypt a record */
static int record_recv_dec(tls_ctx_t *c, u8 *inner_type_out,
                            u8 *plain, u32 maxlen,
                            const u8 *key, const u8 *base_iv, u64 *seq) {
    u8 type; u8 *cipher;
    u8 hdr[5];
    int n = tls_tcp_read_exact(c->fd, hdr, 5);
    if (n < 5) return -1;
    type = hdr[0];
    u32 rlen = r2(hdr+3);
    if (rlen < 17 || rlen > TLS_BUF_SZ) return -1;  /* min 1 byte + 16 tag */

    cipher = (u8*)kmalloc(rlen);
    if (!cipher) return -1;
    n = tls_tcp_read_exact(c->fd, cipher, rlen);
    if ((u32)n < rlen) { kfree(cipher); return -1; }

    /* skip ChangeCipherSpec if received (TLS 1.3 compatibility) */
    if (type == TLS_CHANGE_CIPHER) {
        kfree(cipher);
        return record_recv_dec(c, inner_type_out, plain, maxlen, key, base_iv, seq);
    }

    if (type != TLS_APPDATA) {
        /* might be an alert */
        kfree(cipher);
        return -1;
    }

    u8 iv[12]; kmemcpy(iv, base_iv, 12);
    for (int i=0;i<8;i++) iv[11-i]^=(u8)((*seq)>>(i*8));

    u8 aad[5]; kmemcpy(aad, hdr, 5);

    u32 plen = rlen - 16;
    if (plen > maxlen+1) { kfree(cipher); return -1; }

    u8 actx[256];
    aes128_init(actx, key);
    int ok = aes128_gcm_decrypt(actx, iv, aad, 5,
                                 cipher, plen, plain, cipher+plen);
    kfree(cipher);
    if (ok != 0) return -2;  /* auth failure */

    (*seq)++;
    /* inner_type is last byte of plaintext */
    *inner_type_out = plain[plen-1];
    return (int)(plen - 1);
}

/* ── Key derivation helpers (TLS 1.3 RFC 8446 §7.1) ─────────────── */
static void hkdf_expand_label_tls13(const u8 *secret, const char *label,
                                    const u8 *ctx, u8 ctx_len,
                                    u8 *out, u16 out_len) {
    /* HKDF-Expand-Label(secret, label, context, 32) */
    /* HkdfLabel = length(2) || label_len(1) || "tls13 " + label || ctx_len(1) || ctx */
    char full[64];
    u32 llen = kstrlen(label);
    u32 prefix_len = 6; /* "tls13 " */
    u32 full_len = prefix_len + llen;
    kmemcpy(full, "tls13 ", 6);
    kmemcpy(full+6, label, llen);

    u8 info[256]; int off=0;
    info[off++]=(u8)(out_len >> 8);
    info[off++]=(u8)out_len;
    info[off++]=(u8)full_len;
    kmemcpy(info+off, full, full_len); off+=(int)full_len;
    info[off++] = ctx_len;
    if (ctx && ctx_len) { kmemcpy(info+off, ctx, ctx_len); off += ctx_len; }
    hkdf_expand(secret, info, (u32)off, out, out_len);
}

static void derive_secret(const u8 *secret, const char *label,
                          const u8 *ctx_hash, u8 *out) {
    hkdf_expand_label_tls13(secret, label, ctx_hash, 32, out, 32);
}

static void derive_key_iv_from_secret(const u8 *secret, u8 *key16, u8 *iv12) {
    hkdf_expand_label_tls13(secret, "key", NULL, 0, key16, 16);
    hkdf_expand_label_tls13(secret, "iv",  NULL, 0, iv12,  12);
}

/* ── Send ClientHello ────────────────────────────────────────────── */
static int send_client_hello(tls_ctx_t *c, const char *hostname) {
    u8 *buf = (u8*)kmalloc(1024);
    if (!buf) return -1;
    int off=0;

    /* Generate ephemeral key pair */
    tls_rand(c->my_priv, 32);
    c->my_priv[0] &= 248; c->my_priv[31] &= 127; c->my_priv[31] |= 64;
    static const u8 basepoint[32] = {9};
    x25519(c->my_pub, c->my_priv, basepoint);

    /* client_random */
    tls_rand(c->client_random, 32);

    /* --- build ClientHello body --- */
    /* legacy version */
    w2(buf,&off, 0x0303);
    /* random */
    wb(buf,&off, c->client_random, 32);
    /* session id: 32 random bytes */
    w1(buf,&off, 32);
    u8 sess[32]; tls_rand(sess,32);
    wb(buf,&off, sess, 32);
    /* cipher suites: TLS_AES_128_GCM_SHA256 (0x1301) only */
    w2(buf,&off, 2);  /* length */
    w2(buf,&off, 0x1301);
    /* compression methods: none */
    w1(buf,&off, 1); w1(buf,&off, 0);

    /* extensions */
    int ext_len_pos = off; off+=2;
    int ext_start = off;

    /* SNI */
    u32 hostlen = kstrlen(hostname);
    w2(buf,&off, EXT_SERVER_NAME);
    w2(buf,&off, (u16)(hostlen+5));
    w2(buf,&off, (u16)(hostlen+3));
    w1(buf,&off, 0);
    w2(buf,&off, (u16)hostlen);
    wb(buf,&off, (const u8*)hostname, hostlen);

    /* supported_groups: x25519 (0x001d) */
    w2(buf,&off, EXT_SUPPORTED_GROUPS);
    w2(buf,&off, 4); w2(buf,&off, 2); w2(buf,&off, 0x001d);

    /* sig_algs */
    w2(buf,&off, EXT_SIG_ALGS);
    w2(buf,&off, 8); w2(buf,&off, 6);
    w2(buf,&off, 0x0403); /* ecdsa_secp256r1_sha256 */
    w2(buf,&off, 0x0804); /* rsa_pss_rsae_sha256 */
    w2(buf,&off, 0x0401); /* rsa_pkcs1_sha256 */

    /* supported_versions: TLS 1.3 (0x0304) */
    w2(buf,&off, EXT_SUPPORTED_VER);
    w2(buf,&off, 3); w1(buf,&off, 2); w2(buf,&off, 0x0304);

    /* psk_key_exchange_modes: psk_dhe_ke (1) */
    w2(buf,&off, EXT_PSK_MODES);
    w2(buf,&off, 2); w1(buf,&off, 1); w1(buf,&off, 1);

    /* key_share: x25519 */
    w2(buf,&off, EXT_KEY_SHARE);
    w2(buf,&off, 38); /* ext data len */
    w2(buf,&off, 36); /* client key share list len */
    w2(buf,&off, 0x001d); /* x25519 group */
    w2(buf,&off, 32);
    wb(buf,&off, c->my_pub, 32);

    /* fill extensions length */
    u16 ext_len = (u16)(off - ext_start);
    buf[ext_len_pos]   = (u8)(ext_len>>8);
    buf[ext_len_pos+1] = (u8)ext_len;

    /* wrap in handshake header */
    u8 *hs = (u8*)kmalloc(off+4);
    if (!hs) { kfree(buf); return -1; }
    hs[0]=HS_CLIENT_HELLO;
    { int _o=1; w3(hs,&_o,(u32)off); }
    kmemcpy(hs+4, buf, off);
    u32 hs_len = (u32)off+4;
    kfree(buf);

    transcript_add(c, hs, hs_len);
    int r = record_send_raw(c, TLS_HANDSHAKE, hs, hs_len);
    kfree(hs);
    return r;
}

/* ── Process ServerHello ─────────────────────────────────────────── */
static int process_server_hello(tls_ctx_t *c, const u8 *hs, u32 len) {
    if (len < 6) return -1;
    /* hs[0]=type, hs[1..3]=length, hs[4..5]=version */
    const u8 *p = hs + 6; /* skip type+len+version */
    kmemcpy(c->server_random, p, 32); p+=32;
    u8 sess_len=*p++; p+=sess_len;
    /* u16 cipher = */ r2(p); p+=2;
    p++; /* compression */

    /* parse extensions looking for key_share */
    u16 ext_total = r2(p); p+=2;
    const u8 *ext_end = p + ext_total;
    u8 server_pub[32]={0};
    bool got_key=false;
    while(p+4 <= ext_end){
        u16 etype=r2(p); u16 elen=r2(p+2); p+=4;
        if(etype==EXT_KEY_SHARE && elen>=36){
            /* u16 group = */ r2(p);
            /* u16 klen  = */ r2(p+2);
            kmemcpy(server_pub, p+4, 32);
            got_key=true;
        }
        p+=elen;
    }
    if(!got_key) return -1;

    /* ECDH */
    u8 shared[32];
    x25519(shared, c->my_priv, server_pub);

    /* TLS 1.3 key schedule */
    u8 zeros[32]={0};
    u8 early_secret[32];
    hkdf_extract(NULL,0, zeros,32, early_secret);

    u8 derived_secret[32];
    u8 empty_hash[32]; sha256((const u8*)"",0,empty_hash);
    derive_secret(early_secret, "derived", empty_hash, derived_secret);

    hkdf_extract(derived_secret,32, shared,32, c->handshake_secret);

    /* transcript hash up to and including ServerHello */
    transcript_add(c, hs, len);
    u8 th[32]; transcript_hash(c, th);

    derive_secret(c->handshake_secret,"c hs traffic",th, c->client_hs_secret);
    derive_secret(c->handshake_secret,"s hs traffic",th, c->server_hs_secret);

    derive_key_iv_from_secret(c->client_hs_secret, c->client_hs_key, c->client_hs_iv);
    derive_key_iv_from_secret(c->server_hs_secret, c->server_hs_key, c->server_hs_iv);
    /* The above is incorrect raw hkdf_expand — use derive_key_iv instead */
    (void)zeros;
    c->send_seq=0; c->recv_seq=0;
    return 0;
}

/* ── Derive application keys ─────────────────────────────────────── */
static void derive_app_keys(tls_ctx_t *c) {
    u8 zeros[32]={0};
    u8 empty_hash[32]; sha256((const u8*)"",0,empty_hash);
    u8 derived[32];
    derive_secret(c->handshake_secret,"derived",empty_hash,derived);

    u8 master[32];
    hkdf_extract(derived,32,zeros,32,master);
    kmemcpy(c->master_secret,master,32);

    u8 th[32]; transcript_hash(c,th);
    derive_secret(master,"c ap traffic",th,c->client_ap_secret);
    derive_secret(master,"s ap traffic",th,c->server_ap_secret);
    derive_key_iv_from_secret(c->client_ap_secret, c->client_ap_key, c->client_ap_iv);
    derive_key_iv_from_secret(c->server_ap_secret, c->server_ap_key, c->server_ap_iv);
    c->send_seq=0; c->recv_seq=0;
}

/* ── Process EncryptedExtensions + Certificate + Finished ────────── */
static int process_server_hs_messages(tls_ctx_t *c) {
    u8 *plain = (u8*)kmalloc(TLS_BUF_SZ);
    if(!plain) return -1;

    bool got_finished=false;
    int attempts=0;
    while(!got_finished && attempts++ < 20){
        u8 inner_type;
        int n = record_recv_dec(c, &inner_type, plain, TLS_BUF_SZ-1,
                                 c->server_hs_key, c->server_hs_iv,
                                 &c->recv_seq);
        if(n<0){ kfree(plain); return -1; }

        if(inner_type==TLS_HANDSHAKE){
            const u8 *p=plain; u32 rem=(u32)n;
            while(rem>=4){
                u8 htype=p[0];
                u32 hlen=r3(p+1);
                if(hlen+4>rem) break;
                transcript_add(c, p, hlen+4);
                if(htype==HS_FINISHED){
                    got_finished=true;
                    /* verify server Finished MAC (skip for now — trust all) */
                }
                p+=4+hlen; rem-=4+hlen;
            }
        }
    }
    kfree(plain);
    if(!got_finished) return -1;

    derive_app_keys(c);
    return 0;
}

/* ── Send client Finished ────────────────────────────────────────── */
static int send_client_finished(tls_ctx_t *c) {
    u8 th[32]; transcript_hash(c,th);
    u8 finished_key[32];
    hkdf_expand_label_tls13(c->client_hs_secret, "finished", NULL, 0, finished_key, 32);
    u8 verify[32];
    hmac_sha256(finished_key,32,th,32,verify);

    u8 hs[36];
    hs[0]=HS_FINISHED;
    hs[1]=0; hs[2]=0; hs[3]=32;
    kmemcpy(hs+4,verify,32);

    transcript_add(c,hs,36);
    return record_send_enc(c, TLS_HANDSHAKE, hs,36,
                           c->client_hs_key, c->client_hs_iv, &c->send_seq);
}

/* ── Full TLS handshake ──────────────────────────────────────────── */
static tls_ctx_t *tls_connect(int fd, const char *hostname) {
    tls_ctx_t *c = (tls_ctx_t*)kmalloc(sizeof(tls_ctx_t));
    if(!c) return NULL;
    kmemset(c,0,sizeof(*c));
    c->fd=fd;
    c->hs_transcript_cap=8192;
    c->hs_transcript=(u8*)kmalloc(c->hs_transcript_cap);
    if(!c->hs_transcript){ kfree(c); return NULL; }

    /* 1. Send ClientHello */
    if(send_client_hello(c,hostname)<0) goto fail;

    /* 2. Receive ServerHello */
    {
        u8 type; u8 *buf=(u8*)kmalloc(TLS_BUF_SZ);
        if(!buf) goto fail;
        int n=record_recv(c,&type,buf,TLS_BUF_SZ);
        if(n<0||type!=TLS_HANDSHAKE){ kfree(buf); goto fail; }
        if(process_server_hello(c,buf,(u32)n)<0){ kfree(buf); goto fail; }
        kfree(buf);
    }

    /* 3. Process EncryptedExtensions + Certificate + Finished */
    if(process_server_hs_messages(c)<0) goto fail;

    /* 4. Send client Finished */
    if(send_client_finished(c)<0) goto fail;

    c->send_seq=0;
    c->handshake_done=true;
    return c;

fail:
    kfree(c->hs_transcript);
    kfree(c);
    return NULL;
}

/* ── Public API ──────────────────────────────────────────────────── */

/* HTTPS GET — returns body length or -1 on failure */
int https_get(const char *hostname, const char *path,
              char *resp_buf, u32 maxlen) {
    /* resolve DNS */
    u32 ip=0;
    if(dns_resolve(hostname,&ip)<0) return -1;

    /* TCP connect to port 443 */
    int fd=sock_create(SOCK_TCP);
    if(fd<0) return -1;
    if(sock_connect(fd,ip,443)<0){ sock_close(fd); return -1; }

    /* TLS handshake */
    tls_ctx_t *tls=tls_connect(fd,hostname);
    if(!tls){ sock_close(fd); return -1; }

    /* Send HTTP/1.0 GET */
    char req[512];
    ksprintf(req,"GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: CareOS/9\r\nConnection: close\r\n\r\n",
             path, hostname);
    record_send_enc(tls, TLS_APPDATA,
                    (const u8*)req, kstrlen(req),
                    tls->client_ap_key, tls->client_ap_iv,
                    &tls->send_seq);

    /* receive response */
    u32 total=0;
    u8 *tmp=(u8*)kmalloc(TLS_BUF_SZ);
    if(!tmp) goto done;

    for(int iter=0;iter<512&&total<maxlen-1;iter++){
        u8 inner;
        int n=record_recv_dec(tls,&inner,tmp,TLS_BUF_SZ-1,
                               tls->server_ap_key,tls->server_ap_iv,
                               &tls->recv_seq);
        if(n<=0) break;
        if(inner==TLS_APPDATA){
            u32 copy=(u32)n;
            if(total+copy>maxlen-1) copy=maxlen-1-total;
            kmemcpy(resp_buf+total,tmp,copy);
            total+=copy;
        } else if(inner==TLS_ALERT) break;
    }
    kfree(tmp);
    resp_buf[total]='\0';

done:
    kfree(tls->hs_transcript);
    kfree(tls);
    sock_close(fd);
    return (int)total;
}
