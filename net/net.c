/* CareOS v9 - net/net.c - TCP/IP networking stack */
#include "kernel.h"

/* Protocol numbers */
#define IP_PROTO_ICMP 1
#define IP_PROTO_TCP  6
#define IP_PROTO_UDP  17

typedef struct {
    u16 hw_type;
    u16 proto_type;
    u8  hw_size;
    u8  proto_size;
    u16 opcode;
    u8  sender_mac[6];
    u32 sender_ip;
    u8  target_mac[6];
    u32 target_ip;
} __attribute__((packed)) arp_packet_t;

typedef struct {
    u8  version_ihl;
    u8  dscp_ecn;
    u16 total_len;
    u16 id;
    u16 flags_frag;
    u8  ttl;
    u8  protocol;
    u16 checksum;
    u32 src_ip;
    u32 dst_ip;
} __attribute__((packed)) ip_header_t;

typedef struct {
    u16 src_port;
    u16 dst_port;
    u16 length;
    u16 checksum;
} __attribute__((packed)) udp_header_t;

typedef struct {
    u16 src_port;
    u16 dst_port;
    u32 seq;
    u32 ack;
    u8  data_offset;
    u8  flags;
    u16 window;
    u16 checksum;
    u16 urgent;
} __attribute__((packed)) tcp_header_t;

static u8   nic_mac[6] = {0x52,0x54,0x00,0x12,0x34,0x56};
static u32  nic_ip = 0;
static u32  nic_netmask = 0;
static u32  nic_gateway = 0;
static u32  dns_server = 0;
static bool nic_up = false;
static u8   tx_frame[2048];

/* ARP cache */
#define ARP_CACHE_SZ 32
typedef struct { u32 ip; u8 mac[6]; bool valid; } arp_entry_t;
static arp_entry_t arp_cache[ARP_CACHE_SZ];

/* Sockets */
static socket_t sockets[MAX_SOCKETS];
static u32 tcp_next_seq = 0xABCD1234;
static u16 next_port = 49152;
static u16 dns_next_id = 0x2300;
static char net_error_buf[96] = "";

/* Byte order and checksum helpers */
static u32 htonl32(u32 v){ return ((v&0xff)<<24)|((v>>8&0xff)<<16)|((v>>16&0xff)<<8)|(v>>24); }
static u16 htons16(u16 v){ return (u16)((v>>8)|(v<<8)); }
static u32 ntohl32(u32 v){ return htonl32(v); }
static u16 ntohs16(u16 v){ return htons16(v); }
static u16 be16(const u8 *p){ return (u16)((p[0]<<8)|p[1]); }
static void wr16(u8 *p, u16 v){ p[0] = (u8)(v >> 8); p[1] = (u8)(v & 0xff); }

static void net_set_error(const char *prefix, const char *detail){
    kstrncpy(net_error_buf, prefix ? prefix : "Network error", sizeof(net_error_buf) - 1);
    net_error_buf[sizeof(net_error_buf) - 1] = '\0';
    if (detail && detail[0]) {
        u32 cur = (u32)kstrlen(net_error_buf);
        u32 room = sizeof(net_error_buf) - 1 - cur;
        if (room > 2) {
            kstrcat(net_error_buf, ": ");
            cur = (u32)kstrlen(net_error_buf);
            room = sizeof(net_error_buf) - 1 - cur;
            for (u32 i = 0; detail[i] && i < room; i++) {
                net_error_buf[cur + i] = detail[i];
                net_error_buf[cur + i + 1] = '\0';
            }
        }
    }
}

static u16 ip_csum(const void *buf, u32 len){
    const u16 *p = (const u16*)buf;
    u32 s = 0;
    while (len > 1) { s += *p++; len -= 2; }
    if (len) s += *(const u8*)p;
    while (s >> 16) s = (s & 0xffff) + (s >> 16);
    return (u16)~s;
}

static u16 pseudo_csum(u32 sip, u32 dip, u8 proto, const void *data, u16 dlen){
    struct { u32 s, d; u8 z, p; u16 l; } ph;
    ph.s = htonl32(sip);
    ph.d = htonl32(dip);
    ph.z = 0;
    ph.p = proto;
    ph.l = htons16(dlen);

    u32 s = 0;
    const u16 *p = (const u16*)&ph;
    for (u32 i = 0; i < sizeof(ph) / 2; i++) s += p[i];

    p = (const u16*)data;
    u32 n = dlen;
    while (n > 1) { s += *p++; n -= 2; }
    if (n) s += *(const u8*)p;

    while (s >> 16) s = (s & 0xffff) + (s >> 16);
    return (u16)~s;
}

void net_set_ip(u32 ip, u32 nm, u32 gw){
    nic_ip = ip;
    nic_netmask = nm;
    nic_gateway = gw;
}

void net_configure(u32 ip, u32 netmask, u32 gateway, const u8 *mac){
    nic_ip = ip;
    nic_netmask = netmask;
    nic_gateway = gateway;
    if (mac) kmemcpy(nic_mac, mac, 6);
    nic_up = (ip != 0);
}

int net_dhcp_renew(void){
    bool have_link = e1000_is_up();
    const careos_settings_t *cfg = settings_get();
    bool wifi_sim = cfg && cfg->wifi_connected;

    if (!have_link && !wifi_sim) {
        nic_up = false;
        nic_ip = (127u<<24)|(0u<<16)|(0u<<8)|1u;
        nic_netmask = (255u<<24)|(0u<<16)|(0u<<8)|0u;
        nic_gateway = 0;
        dns_server = 0;
        serial_write("[NET] DHCP failed: no active link\n");
        return -1;
    }

    nic_up = true;
    nic_ip = (10u<<24)|(0u<<16)|(2u<<8)|15u;
    nic_netmask = (255u<<24)|(255u<<16)|(255u<<8)|0u;
    nic_gateway = (10u<<24)|(0u<<16)|(2u<<8)|2u;
    if (dns_server == 0) dns_server = (10u<<24)|(0u<<16)|(2u<<8)|3u;
    serial_write("[NET] DHCP lease renewed: 10.0.2.15/24 gw 10.0.2.2\n");
    return 0;
}

void net_set_dns_server(u32 ip){ dns_server = ip; }
u32 net_get_dns_server(void){ return dns_server; }

void net_init(void){
    kmemset(arp_cache, 0, sizeof(arp_cache));
    kmemset(sockets, 0, sizeof(sockets));

    if (e1000_is_up()) {
        nic_up = true;
        kmemcpy(nic_mac, (void*)e1000_get_mac(), 6);
        nic_ip = (10u<<24)|(0u<<16)|(2u<<8)|15u;
        nic_netmask = (255u<<24)|(255u<<16)|(255u<<8)|0u;
        nic_gateway = (10u<<24)|(0u<<16)|(2u<<8)|2u;
        dns_server = (10u<<24)|(0u<<16)|(2u<<8)|3u;
        serial_write("[NET] e1000 up, IP=10.0.2.15\n");
    } else {
        nic_up = false;
        nic_ip = (127u<<24)|(0u<<16)|(0u<<8)|1u;
        nic_netmask = (255u<<24)|(0u<<16)|(0u<<8)|0u;
        nic_gateway = 0;
        dns_server = 0;
        serial_write("[NET] No NIC, loopback only\n");
    }

    for (int i = 0; i < ARP_CACHE_SZ; i++) arp_cache[i].valid = false;

    /* Auto-restore user Wi-Fi profile if marked connected. */
    {
        const careos_settings_t *cfg = settings_get();
        if (cfg && cfg->wifi_connected) {
            (void)net_dhcp_renew();
        }
    }
}

void net_get_mac(u8 mac[6]){ kmemcpy(mac, nic_mac, 6); }
void net_poll(void){ if (nic_up) e1000_poll(); }

/* ARP */
u8 *arp_lookup(u32 ip){
    static u8 bcast[6] = {0xff,0xff,0xff,0xff,0xff,0xff};
    if (ip == 0xFFFFFFFFu) return bcast;
    for (int i = 0; i < ARP_CACHE_SZ; i++)
        if (arp_cache[i].valid && arp_cache[i].ip == ip) return arp_cache[i].mac;
    return NULL;
}

static void arp_set(u32 ip, const u8 mac[6]){
    for (int i = 0; i < ARP_CACHE_SZ; i++) {
        if (!arp_cache[i].valid || arp_cache[i].ip == ip) {
            arp_cache[i].ip = ip;
            kmemcpy(arp_cache[i].mac, mac, 6);
            arp_cache[i].valid = true;
            return;
        }
    }
    arp_cache[0].ip = ip;
    kmemcpy(arp_cache[0].mac, mac, 6);
    arp_cache[0].valid = true;
}

void arp_request(u32 tgt){
    u8 *f = tx_frame;
    kmemset(f, 0xff, 6);
    kmemcpy(f + 6, nic_mac, 6);
    f[12] = 0x08;
    f[13] = 0x06;

    arp_packet_t *a = (arp_packet_t*)(f + 14);
    a->hw_type = htons16(1);
    a->proto_type = htons16(0x0800);
    a->hw_size = 6;
    a->proto_size = 4;
    a->opcode = htons16(1);
    kmemcpy(a->sender_mac, nic_mac, 6);
    a->sender_ip = htonl32(nic_ip);
    kmemset(a->target_mac, 0, 6);
    a->target_ip = htonl32(tgt);

    net_send_frame(f, 14 + sizeof(arp_packet_t));
}

static void arp_reply(const arp_packet_t *req){
    u32 sender_ip = ntohl32(req->sender_ip);
    u8 *f = tx_frame;
    kmemcpy(f, (void*)req->sender_mac, 6);
    kmemcpy(f + 6, nic_mac, 6);
    f[12] = 0x08;
    f[13] = 0x06;

    arp_packet_t *a = (arp_packet_t*)(f + 14);
    a->hw_type = htons16(1);
    a->proto_type = htons16(0x0800);
    a->hw_size = 6;
    a->proto_size = 4;
    a->opcode = htons16(2);
    kmemcpy(a->sender_mac, nic_mac, 6);
    a->sender_ip = htonl32(nic_ip);
    kmemcpy(a->target_mac, (void*)req->sender_mac, 6);
    a->target_ip = htonl32(sender_ip);

    net_send_frame(f, 14 + sizeof(arp_packet_t));
}

/* IPv4 */
static u16 ip_id = 0;
void ip_send(u32 dst, u8 proto, const u8 *data, u32 dlen){
    u32 nexthop = dst;
    if (nic_netmask && (dst & nic_netmask) != (nic_ip & nic_netmask)) nexthop = nic_gateway;

    u8 *dst_mac = arp_lookup(nexthop);
    if (!dst_mac) {
        arp_request(nexthop);
        timer_wait(5);
        dst_mac = arp_lookup(nexthop);
        if (!dst_mac) return;
    }

    u8 *f = tx_frame;
    kmemcpy(f, dst_mac, 6);
    kmemcpy(f + 6, nic_mac, 6);
    f[12] = 0x08;
    f[13] = 0x00;

    ip_header_t *h = (ip_header_t*)(f + 14);
    u32 tot = 20 + dlen;
    h->version_ihl = 0x45;
    h->dscp_ecn = 0;
    h->total_len = htons16((u16)tot);
    h->id = htons16(ip_id++);
    h->flags_frag = 0;
    h->ttl = 64;
    h->protocol = proto;
    h->checksum = 0;
    h->src_ip = htonl32(nic_ip);
    h->dst_ip = htonl32(dst);
    h->checksum = ip_csum(h, 20);

    kmemcpy(f + 34, data, dlen);
    net_send_frame(f, 14 + tot);
}

/* ICMP */
typedef struct { u8 type, code; u16 csum, id, seq; } __attribute__((packed)) icmp_t;

void icmp_ping(u32 dst, u32 seq){
    u8 buf[sizeof(icmp_t) + 32];
    icmp_t *ic = (icmp_t*)buf;
    ic->type = 8;
    ic->code = 0;
    ic->csum = 0;
    ic->id = htons16(0x1234);
    ic->seq = htons16((u16)seq);
    kmemset(buf + sizeof(icmp_t), 'A', 32);
    ic->csum = ip_csum(buf, sizeof(buf));
    ip_send(dst, IP_PROTO_ICMP, buf, sizeof(buf));
}

/* UDP */
static void udp_send(u32 dst, u16 sp, u16 dp, const u8 *data, u32 dlen){
    u32 tot = 8 + dlen;
    u8 *buf = (u8*)kmalloc(tot);
    if (!buf) return;

    udp_header_t *u = (udp_header_t*)buf;
    u->src_port = htons16(sp);
    u->dst_port = htons16(dp);
    u->length = htons16((u16)tot);
    u->checksum = 0;
    kmemcpy(buf + 8, data, dlen);
    u->checksum = pseudo_csum(nic_ip, dst, IP_PROTO_UDP, buf, (u16)tot);

    ip_send(dst, IP_PROTO_UDP, buf, tot);
    kfree(buf);
}

/* TCP helpers */
static void tcp_flags(socket_t *s, u8 flags, const u8 *data, u32 dlen){
    u32 tot = 20 + dlen;
    u8 *buf = (u8*)kmalloc(tot);
    if (!buf) return;

    tcp_header_t *t = (tcp_header_t*)buf;
    t->src_port = htons16(s->local_port);
    t->dst_port = htons16(s->remote_port);
    t->seq = htonl32(s->seq);
    t->ack = htonl32(s->ack);
    t->data_offset = (u8)((20/4) << 4);
    t->flags = flags;
    t->window = htons16(65535);
    t->checksum = 0;
    t->urgent = 0;
    if (dlen) kmemcpy(buf + 20, data, dlen);

    t->checksum = pseudo_csum(nic_ip, s->remote_ip, IP_PROTO_TCP, buf, (u16)tot);
    ip_send(s->remote_ip, IP_PROTO_TCP, buf, tot);
    kfree(buf);

    s->seq += dlen;
    if (flags & 0x02) s->seq++;
    if (flags & 0x01) s->seq++;
}

/* Socket API */
int sock_create(sock_type_t type){
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (sockets[i].type == SOCK_UNUSED) {
            kmemset(&sockets[i], 0, sizeof(socket_t));
            sockets[i].type = type;
            sockets[i].state = SOCK_CLOSED;
            sockets[i].local_ip = nic_ip;
            sockets[i].local_port = next_port++;
            if (!next_port) next_port = 49152;
            return i;
        }
    }
    return -1;
}

int sock_connect(int fd, u32 dip, u16 dp){
    if (fd < 0 || fd >= MAX_SOCKETS) return -1;
    socket_t *s = &sockets[fd];

    if (s->type == SOCK_UDP) {
        s->remote_ip = dip;
        s->remote_port = dp;
        return 0;
    }

    s->remote_ip = dip;
    s->remote_port = dp;
    s->seq = tcp_next_seq;
    tcp_next_seq += 0x1000;
    s->ack = 0;
    s->state = SOCK_SYN_SENT;

    tcp_flags(s, 0x02, NULL, 0);
    u32 deadline = timer_get_ticks() + 500;
    while (timer_get_ticks() < deadline) {
        net_poll();
        if (s->state == SOCK_ESTABLISHED) return 0;
        timer_wait(5);
    }

    s->state = SOCK_CLOSED;
    return -1;
}

int sock_send(int fd, const u8 *data, u32 len){
    if (fd < 0 || fd >= MAX_SOCKETS) return -1;
    socket_t *s = &sockets[fd];

    if (s->type == SOCK_TCP) {
        if (s->state != SOCK_ESTABLISHED) return -1;
        tcp_flags(s, 0x18, data, len);
        return (int)len;
    }

    if (s->type == SOCK_UDP) {
        if (s->remote_ip == 0 || s->remote_port == 0) return -1;
        udp_send(s->remote_ip, s->local_port, s->remote_port, data, len);
        return (int)len;
    }

    return -1;
}

int sock_recv(int fd, u8 *buf, u32 maxlen){
    if (fd < 0 || fd >= MAX_SOCKETS) return -1;
    socket_t *s = &sockets[fd];

    u32 deadline = timer_get_ticks() + 2000;
    while (timer_get_ticks() < deadline) {
        net_poll();
        if (s->rx_len > 0) {
            u32 n = s->rx_len < maxlen ? s->rx_len : maxlen;
            kmemcpy(buf, s->rx_buf, n);
            if (n < s->rx_len) {
                u32 remain = s->rx_len - n;
                for (u32 i = 0; i < remain; i++) s->rx_buf[i] = s->rx_buf[n + i];
                s->rx_len = remain;
            } else {
                s->rx_len = 0;
            }
            return (int)n;
        }
        timer_wait(10);
    }
    return 0;
}

void sock_close(int fd){
    if (fd < 0 || fd >= MAX_SOCKETS) return;
    socket_t *s = &sockets[fd];
    if (s->type == SOCK_TCP && s->state == SOCK_ESTABLISHED) tcp_flags(s, 0x01, NULL, 0);
    kmemset(s, 0, sizeof(socket_t));
}

/* Incoming frame dispatch */
void net_handle_frame(const u8 *frame, u32 len){
    if (len < 14) return;
    u16 et = (u16)((frame[12] << 8) | frame[13]);

    if (et == 0x0806 && len >= 14 + sizeof(arp_packet_t)) {
        const arp_packet_t *a = (const arp_packet_t*)(frame + 14);
        u16 op = ntohs16(a->opcode);
        u32 sender_ip = ntohl32(a->sender_ip);
        u32 target_ip = ntohl32(a->target_ip);
        arp_set(sender_ip, a->sender_mac);
        if (op == 1 && target_ip == nic_ip) arp_reply(a);
        return;
    }

    if (et != 0x0800 || len < 34) return;

    const ip_header_t *iph = (const ip_header_t*)(frame + 14);
    u32 ihl = (iph->version_ihl & 0x0f) * 4;
    const u8 *pl = (const u8*)iph + ihl;
    u32 total_len = ntohs16(iph->total_len);
    u32 dst_ip = ntohl32(iph->dst_ip);
    if (ihl < 20 || total_len < ihl || len < 14 + total_len) return;
    if (dst_ip != nic_ip && dst_ip != 0xffffffffu) return;
    u32 plen = total_len - ihl;

    if (iph->protocol == IP_PROTO_UDP && plen >= 8) {
        const udp_header_t *u = (const udp_header_t*)pl;
        u16 dp = ntohs16(u->dst_port);
        for (int i = 0; i < MAX_SOCKETS; i++) {
            if (sockets[i].type == SOCK_UDP && sockets[i].local_port == dp) {
                u32 n = plen - 8;
                if (n > NET_BUF_SIZE) n = NET_BUF_SIZE;
                kmemcpy(sockets[i].rx_buf, pl + 8, n);
                sockets[i].rx_len = n;
            }
        }
        return;
    }

    if (iph->protocol == IP_PROTO_TCP && plen >= 20) {
        const tcp_header_t *t = (const tcp_header_t*)pl;
        u16 sp = ntohs16(t->src_port);
        for (int i = 0; i < MAX_SOCKETS; i++) {
            socket_t *s = &sockets[i];
            if (s->type != SOCK_TCP || s->remote_port != sp) continue;

            if (s->state == SOCK_SYN_SENT && (t->flags & 0x12) == 0x12) {
                s->ack = htonl32(t->seq) + 1;
                s->state = SOCK_ESTABLISHED;
                tcp_flags(s, 0x10, NULL, 0);
            } else if (s->state == SOCK_ESTABLISHED && (t->flags & 0x08)) {
                u32 doff = (t->data_offset >> 4) * 4;
                const u8 *td = pl + doff;
                u32 tl = plen - doff;
                if (tl > NET_BUF_SIZE - s->rx_len) tl = NET_BUF_SIZE - s->rx_len;
                kmemcpy(s->rx_buf + s->rx_len, td, tl);
                s->rx_len += tl;
                s->ack = htonl32(t->seq) + tl;
                tcp_flags(s, 0x10, NULL, 0);
            }
        }
    }
}

/* DNS */
static int dns_encode_name(const char *host, u8 *out, u32 max);

static int dns_skip_name(const u8 *pkt, u32 len, u32 *off){
    u32 o = *off;
    if (o >= len) return -1;

    while (o < len) {
        u8 c = pkt[o++];
        if (c == 0) { *off = o; return 0; }
        if ((c & 0xC0) == 0xC0) {
            if (o >= len) return -1;
            o++;
            *off = o;
            return 0;
        }
        if (c > 63 || o + c > len) return -1;
        o += c;
    }
    return -1;
}

static int dns_encode_name(const char *host, u8 *out, u32 max){
    u32 o = 0;
    const char *seg = host;
    const char *p = host;

    while (1) {
        if (*p == '.' || *p == '\0') {
            u32 len = (u32)(p - seg);
            if (len == 0 || len > 63) return -1;
            if (o + 1 + len >= max) return -1;
            out[o++] = (u8)len;
            for (u32 i = 0; i < len; i++) out[o++] = (u8)seg[i];
            if (*p == '\0') break;
            seg = p + 1;
        }
        p++;
    }

    if (o + 1 > max) return -1;
    out[o++] = 0;
    return (int)o;
}

static int dns_query_a(const char *host, u32 *out){
    if (!host || !out || !nic_up || dns_server == 0) return -1;

    int fd = sock_create(SOCK_UDP);
    if (fd < 0) return -1;

    socket_t *s = &sockets[fd];
    s->remote_ip = dns_server;
    s->remote_port = 53;

    u8 req[256];
    kmemset(req, 0, sizeof(req));

    u16 txid = ++dns_next_id;
    wr16(req + 0, txid);
    wr16(req + 2, 0x0100);
    wr16(req + 4, 1);

    int qn = dns_encode_name(host, req + 12, sizeof(req) - 12 - 4);
    if (qn < 0) { sock_close(fd); return -1; }

    u32 qoff = 12u + (u32)qn;
    wr16(req + qoff + 0, 1);
    wr16(req + qoff + 2, 1);

    if (sock_send(fd, req, qoff + 4) <= 0) { sock_close(fd); return -1; }

    u8 resp[512];
    int n = sock_recv(fd, resp, sizeof(resp));
    sock_close(fd);
    if (n < 12) return -1;

    if (be16(resp + 0) != txid) return -1;
    u16 flags = be16(resp + 2);
    if ((flags & 0x8000) == 0) return -1;
    if ((flags & 0x000f) != 0) return -1;

    u16 qd = be16(resp + 4);
    u16 an = be16(resp + 6);
    if (an == 0) return -1;

    u32 off = 12;
    for (u16 i = 0; i < qd; i++) {
        if (dns_skip_name(resp, (u32)n, &off) != 0) return -1;
        if (off + 4 > (u32)n) return -1;
        off += 4;
    }

    for (u16 i = 0; i < an; i++) {
        if (dns_skip_name(resp, (u32)n, &off) != 0) return -1;
        if (off + 10 > (u32)n) return -1;

        u16 type = be16(resp + off + 0);
        u16 cls  = be16(resp + off + 2);
        u16 rdlen = be16(resp + off + 8);
        off += 10;

        if (off + rdlen > (u32)n) return -1;
        if (type == 1 && cls == 1 && rdlen == 4) {
            *out = ((u32)resp[off] << 24) | ((u32)resp[off+1] << 16) |
                   ((u32)resp[off+2] << 8) | (u32)resp[off+3];
            return 0;
        }
        off += rdlen;
    }

    return -1;
}

int dns_resolve(const char *host, u32 *out){
    if (!host || !out) return -1;

    /* Numeric IPv4 fast-path */
    u32 nums[4];
    int ni = 0;
    u32 cur = 0;
    bool in_num = false;
    const char *p = host;

    while (*p && ni < 4) {
        if (*p >= '0' && *p <= '9') {
            cur = cur * 10 + (u32)(*p - '0');
            in_num = true;
            if (cur > 255) break;
        } else if (*p == '.' && in_num) {
            nums[ni++] = cur;
            cur = 0;
            in_num = false;
        } else {
            break;
        }
        p++;
    }
    if (in_num && ni == 3) {
        nums[3] = cur;
        ni = 4;
    }
    if (ni == 4) {
        *out = (nums[0] << 24) | (nums[1] << 16) | (nums[2] << 8) | nums[3];
        return 0;
    }

    /* Built-in fallback map */
    struct { const char *n; u32 ip; } known[] = {
        {"localhost", 0x7f000001u},
        {"example.com", (93u<<24)|(184u<<16)|(216u<<8)|34u},
        {"google.com",  (142u<<24)|(250u<<16)|(72u<<8)|14u},
        {"www.google.com",  (142u<<24)|(250u<<16)|(72u<<8)|4u},
        {"careos.dev",  (192u<<24)|(168u<<16)|(1u<<8)|1u},
        {NULL, 0}
    };
    for (int i = 0; known[i].n; i++)
        if (kstrcmp(known[i].n, host) == 0) {
            *out = known[i].ip;
            return 0;
        }

    if (dns_query_a(host, out) == 0) return 0;
    return -1;
}

/* HTTP GET */
int http_get(const char *host, u16 port, const char *path, char *resp, u32 maxlen){
    u32 ip;
    net_error_buf[0] = '\0';
    if (dns_resolve(host, &ip) != 0) {
        net_set_error("DNS lookup failed", host);
        return -1;
    }

    int sock = sock_create(SOCK_TCP);
    if (sock < 0) {
        net_set_error("No sockets available", host);
        return -1;
    }
    if (sock_connect(sock, ip, port) != 0) {
        sock_close(sock);
        net_set_error("TCP connect failed", host);
        return -1;
    }

    char req[1024];
    kstrcpy(req, "GET ");
    kstrcat(req, path);
    kstrcat(req, " HTTP/1.0\r\nHost: ");
    kstrcat(req, host);
    kstrcat(req, "\r\nUser-Agent: CareOS/9\r\nAccept: text/html,*/*\r\nConnection: close\r\n\r\n");

    sock_send(sock, (const u8*)req, (u32)kstrlen(req));

    u32 got = 0;
    u8 buf[512];
    int n;
    while ((n = sock_recv(sock, buf, sizeof(buf))) > 0 && got < maxlen - 1) {
        u32 c = (u32)n;
        if (got + c >= maxlen) c = maxlen - got - 1;
        kmemcpy(resp + got, buf, c);
        got += c;
    }
    resp[got] = '\0';
    sock_close(sock);
    if (got == 0) net_set_error("No response body", host);
    return (int)got;
}

/* Accessors expected by shell.c / apps.c / wm.c */
bool net_is_up(void){ return nic_up; }
u32 net_get_ip(void){ return nic_ip; }
const char *net_last_error(void){ return net_error_buf; }

void http_decode_chunked(char *buf, u32 len, u32 *out_len) {
    char *src = buf, *dst = buf;
    char *end = buf + len;
    *out_len = 0;
    while (src < end) {
        while (src < end && (*src == '\r' || *src == '\n')) src++;
        u32 chunk_size = 0;
        while (src < end && *src != '\r' && *src != '\n') {
            char h = *src++;
            u32 v = 0;
            if (h>='0'&&h<='9') v=(u32)(h-'0');
            else if (h>='a'&&h<='f') v=(u32)(h-'a'+10);
            else if (h>='A'&&h<='F') v=(u32)(h-'A'+10);
            else break;
            chunk_size = chunk_size * 16 + v;
        }
        while (src < end && (*src == '\r' || *src == '\n')) src++;
        if (chunk_size == 0) break;
        u32 avail = (u32)(end - src);
        u32 to_copy = avail < chunk_size ? avail : chunk_size;
        if (dst != src) kmemcpy(dst, src, to_copy);
        dst += to_copy; src += chunk_size;
        *out_len += to_copy;
        while (src < end && (*src == '\r' || *src == '\n')) src++;
    }
    *dst = '\0';
}
