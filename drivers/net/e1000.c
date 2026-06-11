/* =============================================================================
 * CareOS - drivers/net/e1000.c
 * Intel 8254x (e1000) Ethernet driver — MMIO mode.
 *
 * QEMU exposes this NIC as PCI VID=0x8086 DID=0x100E.
 * The driver:
 *   1. Finds the PCI device and reads BAR0 (MMIO base).
 *   2. Resets the NIC and reads the MAC address.
 *   3. Sets up one TX descriptor ring (TX_DESC_COUNT entries).
 *   4. Sets up one RX descriptor ring (RX_DESC_COUNT entries).
 *   5. Exposes net_send_frame() and net_poll() to net.c.
 * ============================================================================= */

#include "kernel.h"

/* ── e1000 register offsets (relative to MMIO base) ────────────────────────── */
#define E1000_CTRL     0x0000  /* Device Control */
#define E1000_STATUS   0x0008  /* Device Status */
#define E1000_EECD     0x0010  /* EEPROM/Flash Control */
#define E1000_EERD     0x0014  /* EEPROM Read */
#define E1000_ICR      0x00C0  /* Interrupt Cause Read */
#define E1000_IMS      0x00D0  /* Interrupt Mask Set */
#define E1000_IMC      0x00D8  /* Interrupt Mask Clear */
#define E1000_RCTL     0x0100  /* RX Control */
#define E1000_TCTL     0x0400  /* TX Control */
#define E1000_TIPG     0x0410  /* TX Inter-Packet Gap */
#define E1000_RDBAL    0x2800  /* RX Desc Base Low */
#define E1000_RDBAH    0x2804  /* RX Desc Base High */
#define E1000_RDLEN    0x2808  /* RX Desc Ring Length */
#define E1000_RDH      0x2810  /* RX Desc Head */
#define E1000_RDT      0x2818  /* RX Desc Tail */
#define E1000_TDBAL    0x3800  /* TX Desc Base Low */
#define E1000_TDBAH    0x3804  /* TX Desc Base High */
#define E1000_TDLEN    0x3808  /* TX Desc Ring Length */
#define E1000_TDH      0x3810  /* TX Desc Head */
#define E1000_TDT      0x3818  /* TX Desc Tail */
#define E1000_RAL0     0x5400  /* Receive Address Low  [0] */
#define E1000_RAH0     0x5404  /* Receive Address High [0] */
#define E1000_MTA      0x5200  /* Multicast Table Array (128 × u32) */

/* CTRL bits */
#define E1000_CTRL_RST   (1u << 26)
#define E1000_CTRL_ASDE  (1u << 5)
#define E1000_CTRL_SLU   (1u << 6)

/* RCTL bits */
#define E1000_RCTL_EN    (1u << 1)
#define E1000_RCTL_SBP   (1u << 2)
#define E1000_RCTL_UPE   (1u << 3)
#define E1000_RCTL_MPE   (1u << 4)
#define E1000_RCTL_BAM   (1u << 15)  /* broadcast accept */
#define E1000_RCTL_BSIZE_2048  0x00000000
#define E1000_RCTL_SECRC (1u << 26)  /* strip CRC */

/* TCTL bits */
#define E1000_TCTL_EN    (1u << 1)
#define E1000_TCTL_PSP   (1u << 3)
#define E1000_TCTL_CT    (0x10u << 4)
#define E1000_TCTL_COLD  (0x40u << 12)

/* TX descriptor cmd bits */
#define E1000_TXD_CMD_EOP  0x01
#define E1000_TXD_CMD_FCS  0x02
#define E1000_TXD_CMD_RS   0x08  /* report status */

/* TX descriptor status bits */
#define E1000_TXD_STAT_DD  0x01  /* descriptor done */

/* RX descriptor status bits */
#define E1000_RXD_STAT_DD  0x01  /* descriptor done */
#define E1000_RXD_STAT_EOP 0x02  /* end of packet */

/* ── Descriptor structures ──────────────────────────────────────────────────── */
#define TX_DESC_COUNT 8
#define RX_DESC_COUNT 8
#define RX_BUF_SIZE   2048

typedef struct {
    u64 addr;        /* buffer physical address */
    u16 length;
    u16 cso;
    u8  cmd;
    u8  status;
    u8  css;
    u16 special;
} __attribute__((packed)) tx_desc_t;

typedef struct {
    u64 addr;        /* buffer physical address */
    u16 length;
    u16 checksum;
    u8  status;
    u8  errors;
    u16 special;
} __attribute__((packed)) rx_desc_t;

/* ── Driver state ───────────────────────────────────────────────────────────── */
static volatile u8 *e1000_mmio = NULL;
static bool         e1000_up   = false;

static tx_desc_t tx_descs[TX_DESC_COUNT] __attribute__((aligned(16)));
static rx_desc_t rx_descs[RX_DESC_COUNT] __attribute__((aligned(16)));
static u8        tx_bufs[TX_DESC_COUNT][2048] __attribute__((aligned(16)));
static u8        rx_bufs[RX_DESC_COUNT][RX_BUF_SIZE] __attribute__((aligned(16)));
static u32       tx_tail = 0;
static u32       rx_tail = 0;

/* MAC that was read from device */
static u8 e1000_mac[6];

/* ── Register helpers ───────────────────────────────────────────────────────── */
static void e1000_wr(u32 reg, u32 val) {
    *(volatile u32*)(e1000_mmio + reg) = val;
}
static u32 e1000_rd(u32 reg) {
    return *(volatile u32*)(e1000_mmio + reg);
}

/* ── EEPROM read (for MAC address) ──────────────────────────────────────────── */
static u16 e1000_eeprom_read(u8 addr) {
    e1000_wr(E1000_EERD, (u32)addr << 8 | 0x01);
    u32 timeout = 100000;
    while (!(e1000_wr(E1000_EERD, e1000_rd(E1000_EERD)), e1000_rd(E1000_EERD) & 0x10) && timeout--)
        ;
    return (u16)((e1000_rd(E1000_EERD) >> 16) & 0xFFFF);
}

/* ── Initialise rings ───────────────────────────────────────────────────────── */
static void e1000_init_rx(void) {
    for (u32 i = 0; i < RX_DESC_COUNT; i++) {
        rx_descs[i].addr   = (u64)(uintptr_t)rx_bufs[i];
        rx_descs[i].status = 0;
    }
    rx_tail = RX_DESC_COUNT - 1;

    e1000_wr(E1000_RDBAL, (u32)(uintptr_t)rx_descs);
    e1000_wr(E1000_RDBAH, 0);
    e1000_wr(E1000_RDLEN, RX_DESC_COUNT * sizeof(rx_desc_t));
    e1000_wr(E1000_RDH,   0);
    e1000_wr(E1000_RDT,   rx_tail);

    e1000_wr(E1000_RCTL,
             E1000_RCTL_EN | E1000_RCTL_SBP | E1000_RCTL_UPE |
             E1000_RCTL_MPE | E1000_RCTL_BAM |
             E1000_RCTL_BSIZE_2048 | E1000_RCTL_SECRC);
}

static void e1000_init_tx(void) {
    kmemset(tx_descs, 0, sizeof(tx_descs));
    for (u32 i = 0; i < TX_DESC_COUNT; i++) {
        tx_descs[i].addr   = (u64)(uintptr_t)tx_bufs[i];
        tx_descs[i].status = E1000_TXD_STAT_DD;  /* all free initially */
    }
    tx_tail = 0;

    e1000_wr(E1000_TDBAL, (u32)(uintptr_t)tx_descs);
    e1000_wr(E1000_TDBAH, 0);
    e1000_wr(E1000_TDLEN, TX_DESC_COUNT * sizeof(tx_desc_t));
    e1000_wr(E1000_TDH,   0);
    e1000_wr(E1000_TDT,   0);

    e1000_wr(E1000_TCTL,
             E1000_TCTL_EN | E1000_TCTL_PSP | E1000_TCTL_CT | E1000_TCTL_COLD);
    e1000_wr(E1000_TIPG, 0x0060200A);  /* recommended inter-packet gap */
}

/* ── Public: send a raw Ethernet frame ──────────────────────────────────────── */
void net_send_frame(const u8 *frame, u32 len) {
    if (!e1000_up || !frame || len > 2048) return;

    tx_desc_t *d = &tx_descs[tx_tail];
    /* Wait for previous transmit to finish */
    u32 timeout = 10000;
    while (!(d->status & E1000_TXD_STAT_DD) && timeout--) {
        __asm__ volatile ("pause");
    }

    kmemcpy(tx_bufs[tx_tail], frame, len);
    d->length = (u16)len;
    d->cmd    = E1000_TXD_CMD_EOP | E1000_TXD_CMD_FCS | E1000_TXD_CMD_RS;
    d->status = 0;

    tx_tail = (tx_tail + 1) % TX_DESC_COUNT;
    e1000_wr(E1000_TDT, tx_tail);
}

/* ── Public: poll for received frames ───────────────────────────────────────── */
/* Called from net.c's net_poll().  For each received frame it calls
   net_handle_frame() which is implemented in net.c. */
extern void net_handle_frame(const u8 *frame, u32 len);

void e1000_poll(void) {
    if (!e1000_up) return;

    for (u32 i = 0; i < RX_DESC_COUNT; i++) {
        u32 idx = (rx_tail + 1 + i) % RX_DESC_COUNT;
        rx_desc_t *d = &rx_descs[idx];
        if (!(d->status & E1000_RXD_STAT_DD)) break;
        if (d->status & E1000_RXD_STAT_EOP) {
            net_handle_frame(rx_bufs[idx], d->length);
        }
        /* Give the descriptor back */
        d->status = 0;
        rx_tail   = idx;
        e1000_wr(E1000_RDT, rx_tail);
    }
}

/* ── Public: get MAC ────────────────────────────────────────────────────────── */
const u8 *e1000_get_mac(void) { return e1000_mac; }
bool      e1000_is_up(void)   { return e1000_up; }

/* ── Initialise ─────────────────────────────────────────────────────────────── */
void e1000_init(void) {
    /* Find PCI device */
    pci_device_t *dev = pci_find_device(0x8086, 0x100E);
    if (!dev) {
        /* Try 82540EM (another common QEMU e1000 DID) */
        dev = pci_find_device(0x8086, 0x100F);
    }
    if (!dev) {
        serial_write("[e1000] not found on PCI bus\n");
        return;
    }

    /* BAR0 = MMIO base (bit 0 clear = memory-mapped) */
    u32 bar0 = dev->bar[0] & ~0xF;
    if (bar0 == 0) {
        serial_write("[e1000] BAR0 is zero\n");
        return;
    }

    serial_write("[e1000] init start\n");
    char buf[12];
    serial_write("[e1000] raw BAR0=0x");
    kutoa(dev->bar[0], buf, 16); serial_write(buf); serial_write("\n");

    serial_write("[e1000] mmio BAR0=0x");
    kutoa(bar0, buf, 16); serial_write(buf); serial_write("\n");

    e1000_mmio = (volatile u8*)(uintptr_t)bar0;

    /* Map NIC MMIO before touching registers */
    paging_map_mmio(bar0, 0x20000);

    serial_write("[e1000] mmio mapped\n");

    /* Enable PCI bus-master + memory access */
    u16 cmd = pci_read16(dev->bus, dev->device, dev->function, 0x04);
    pci_write32(dev->bus, dev->device, dev->function, 0x04,
                cmd | 0x04 | 0x02);

    serial_write("[e1000] pci command set\n");
    serial_write("[e1000] resetting NIC\n");

    /* Reset NIC */
    e1000_wr(E1000_CTRL, e1000_rd(E1000_CTRL) | E1000_CTRL_RST);
    timer_wait(10);

    /* Link up + auto-negotiate */
    e1000_wr(E1000_CTRL,
             (e1000_rd(E1000_CTRL) & ~E1000_CTRL_RST) |
             E1000_CTRL_ASDE | E1000_CTRL_SLU);

    /* Read MAC from EEPROM */
    u16 w0 = e1000_eeprom_read(0);
    u16 w1 = e1000_eeprom_read(1);
    u16 w2 = e1000_eeprom_read(2);
    e1000_mac[0] = (u8)(w0 & 0xFF); e1000_mac[1] = (u8)(w0 >> 8);
    e1000_mac[2] = (u8)(w1 & 0xFF); e1000_mac[3] = (u8)(w1 >> 8);
    e1000_mac[4] = (u8)(w2 & 0xFF); e1000_mac[5] = (u8)(w2 >> 8);

    /* Program receive address filter */
    u32 ral = (u32)e1000_mac[0] | ((u32)e1000_mac[1] << 8) |
              ((u32)e1000_mac[2] << 16) | ((u32)e1000_mac[3] << 24);
    u32 rah = (u32)e1000_mac[4] | ((u32)e1000_mac[5] << 8) | (1u << 31);
    e1000_wr(E1000_RAL0, ral);
    e1000_wr(E1000_RAH0, rah);

    /* Clear multicast table */
    for (u32 i = 0; i < 128; i++)
        e1000_wr(E1000_MTA + i * 4, 0);

    /* Disable interrupts (we poll) */
    e1000_wr(E1000_IMC, 0xFFFFFFFF);

    e1000_init_rx();
    e1000_init_tx();

    e1000_up = true;

    serial_write("[e1000] MAC=");
    for (int i = 0; i < 6; i++) {
        char tmp[4]; kutoa(e1000_mac[i], tmp, 16);
        if (e1000_mac[i] < 16) serial_write("0");
        serial_write(tmp);
        if (i < 5) serial_write(":");
    }
    serial_write("\n");
}
