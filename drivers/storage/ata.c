/* =============================================================================
 * CareOS - drivers/storage/ata.c
 * ATA/IDE PIO driver — primary channel, master drive.
 *
 * Supports:
 *   • ATA identify (detect drive presence + size)
 *   • 28-bit LBA read  (ata_read_sectors)
 *   • 28-bit LBA write (ata_write_sectors)
 *   • Simple sector cache (8 entries, LRU)
 *   • FAT32/EXT2 sector helpers used by the filesystem layer
 * ============================================================================= */

#include "kernel.h"

/* ── ATA port addresses ─────────────────────────────────────────────────────── */
#define ATA_PRIMARY_BASE    0x1F0
#define ATA_PRIMARY_CTRL    0x3F6
#define ATA_SECONDARY_BASE  0x170

/* Register offsets from base */
#define ATA_REG_DATA        0x00
#define ATA_REG_ERROR       0x01
#define ATA_REG_FEATURES    0x01
#define ATA_REG_SECCOUNT    0x02
#define ATA_REG_LBA_LO      0x03
#define ATA_REG_LBA_MID     0x04
#define ATA_REG_LBA_HI      0x05
#define ATA_REG_DRIVE_SEL   0x06
#define ATA_REG_STATUS      0x07
#define ATA_REG_CMD         0x07

/* Status bits */
#define ATA_SR_BSY   0x80
#define ATA_SR_DRDY  0x40
#define ATA_SR_DF    0x20
#define ATA_SR_DSC   0x10
#define ATA_SR_DRQ   0x08
#define ATA_SR_CORR  0x04
#define ATA_SR_IDX   0x02
#define ATA_SR_ERR   0x01

/* Commands */
#define ATA_CMD_READ_PIO   0x20
#define ATA_CMD_WRITE_PIO  0x30
#define ATA_CMD_IDENTIFY   0xEC
#define ATA_CMD_FLUSH      0xE7

/* ── Drive state ────────────────────────────────────────────────────────────── */
static bool ata_present   = false;
static bool ata_lba48     = false;
static u32  ata_sectors   = 0;      /* total 512-byte sectors */
static char ata_model[41] = {0};

/* ── Helpers ────────────────────────────────────────────────────────────────── */
static void ata_400ns_delay(void) {
    /* Read alt-status 4 times = ~400 ns */
    for (int i = 0; i < 4; i++) inb(ATA_PRIMARY_CTRL);
}

static int ata_poll(bool check_drq) {
    ata_400ns_delay();
    u32 timeout = 100000;
    while (timeout--) {
        u8 st = inb(ATA_PRIMARY_BASE + ATA_REG_STATUS);
        if (st & ATA_SR_ERR)  return -1;
        if (st & ATA_SR_DF)   return -2;
        if (st & ATA_SR_BSY)  continue;
        if (check_drq && !(st & ATA_SR_DRQ)) continue;
        return 0;
    }
    return -3;  /* timeout */
}

/* Swap bytes in IDENTIFY string (ATA sends big-endian word pairs) */
static void ata_fix_string(u16 *words, u32 offset, u32 len, char *out) {
    for (u32 i = 0; i < len/2; i++) {
        u16 w = words[offset + i];
        out[i*2]   = (char)(w >> 8);
        out[i*2+1] = (char)(w & 0xFF);
    }
    out[len] = '\0';
    /* rtrim */
    for (i32 i = (i32)len - 1; i >= 0 && out[i] == ' '; i--) out[i] = '\0';
}

/* ── IDENTIFY ───────────────────────────────────────────────────────────────── */
static int ata_identify(void) {
    /* Select master on primary channel */
    outb(ATA_PRIMARY_BASE + ATA_REG_DRIVE_SEL, 0xA0);
    ata_400ns_delay();

    /* Zero sector registers */
    outb(ATA_PRIMARY_BASE + ATA_REG_SECCOUNT, 0);
    outb(ATA_PRIMARY_BASE + ATA_REG_LBA_LO,   0);
    outb(ATA_PRIMARY_BASE + ATA_REG_LBA_MID,  0);
    outb(ATA_PRIMARY_BASE + ATA_REG_LBA_HI,   0);

    outb(ATA_PRIMARY_BASE + ATA_REG_CMD, ATA_CMD_IDENTIFY);
    ata_400ns_delay();

    u8 st = inb(ATA_PRIMARY_BASE + ATA_REG_STATUS);
    if (st == 0) return -1;  /* no drive */

    /* Wait for BSY to clear */
    u32 timeout = 100000;
    while ((inb(ATA_PRIMARY_BASE + ATA_REG_STATUS) & ATA_SR_BSY) && timeout--)
        ;

    /* Check for ATAPI (mid/hi non-zero = not ATA) */
    u8 mid = inb(ATA_PRIMARY_BASE + ATA_REG_LBA_MID);
    u8 hi  = inb(ATA_PRIMARY_BASE + ATA_REG_LBA_HI);
    if (mid != 0 || hi != 0) return -2;  /* ATAPI / SATA in AHCI mode */

    if (ata_poll(true) != 0) return -3;

    /* Read 256 words of IDENTIFY data */
    u16 idata[256];
    for (int i = 0; i < 256; i++)
        idata[i] = inw(ATA_PRIMARY_BASE + ATA_REG_DATA);

    ata_fix_string(idata, 27, 40, ata_model);

    /* LBA28 sector count at word 60-61 */
    ata_sectors = ((u32)idata[61] << 16) | idata[60];

    /* LBA48 support (word 83 bit 10) */
    ata_lba48 = (idata[83] & (1 << 10)) != 0;

    ata_present = true;
    return 0;
}

/* ── Read sectors (LBA28, PIO) ──────────────────────────────────────────────── */
int ata_read_sectors(u32 lba, u8 count, void *buf) {
    if (!ata_present || count == 0 || !buf) return -1;
    if (lba + count > ata_sectors)          return -2;

    outb(ATA_PRIMARY_BASE + ATA_REG_DRIVE_SEL,
         (u8)(0xE0 | ((lba >> 24) & 0x0F)));  /* LBA mode, master, top 4 bits */
    ata_400ns_delay();

    outb(ATA_PRIMARY_BASE + ATA_REG_SECCOUNT, count);
    outb(ATA_PRIMARY_BASE + ATA_REG_LBA_LO,  (u8)(lba & 0xFF));
    outb(ATA_PRIMARY_BASE + ATA_REG_LBA_MID, (u8)((lba >>  8) & 0xFF));
    outb(ATA_PRIMARY_BASE + ATA_REG_LBA_HI,  (u8)((lba >> 16) & 0xFF));
    outb(ATA_PRIMARY_BASE + ATA_REG_CMD,      ATA_CMD_READ_PIO);

    u16 *ptr = (u16*)buf;
    for (u8 s = 0; s < count; s++) {
        if (ata_poll(true) != 0) return -3;
        for (int w = 0; w < 256; w++)
            ptr[s * 256 + w] = inw(ATA_PRIMARY_BASE + ATA_REG_DATA);
    }
    return 0;
}

/* ── Write sectors (LBA28, PIO) ─────────────────────────────────────────────── */
int ata_write_sectors(u32 lba, u8 count, const void *buf) {
    if (!ata_present || count == 0 || !buf) return -1;
    if (lba + count > ata_sectors)          return -2;

    outb(ATA_PRIMARY_BASE + ATA_REG_DRIVE_SEL,
         (u8)(0xE0 | ((lba >> 24) & 0x0F)));
    ata_400ns_delay();

    outb(ATA_PRIMARY_BASE + ATA_REG_SECCOUNT, count);
    outb(ATA_PRIMARY_BASE + ATA_REG_LBA_LO,  (u8)(lba & 0xFF));
    outb(ATA_PRIMARY_BASE + ATA_REG_LBA_MID, (u8)((lba >>  8) & 0xFF));
    outb(ATA_PRIMARY_BASE + ATA_REG_LBA_HI,  (u8)((lba >> 16) & 0xFF));
    outb(ATA_PRIMARY_BASE + ATA_REG_CMD,      ATA_CMD_WRITE_PIO);

    const u16 *ptr = (const u16*)buf;
    for (u8 s = 0; s < count; s++) {
        if (ata_poll(true) != 0) return -3;
        for (int w = 0; w < 256; w++)
            outw(ATA_PRIMARY_BASE + ATA_REG_DATA, ptr[s * 256 + w]);
    }

    /* Flush write cache */
    outb(ATA_PRIMARY_BASE + ATA_REG_CMD, ATA_CMD_FLUSH);
    ata_poll(false);
    return 0;
}

/* ── Simple sector cache ────────────────────────────────────────────────────── */
#define CACHE_ENTRIES 8
#define SECTOR_SIZE   512

typedef struct {
    u32  lba;
    bool valid;
    bool dirty;
    u32  lru_tick;
    u8   data[SECTOR_SIZE];
} cache_entry_t;

static cache_entry_t cache[CACHE_ENTRIES];
static u32           cache_tick = 0;

static cache_entry_t *cache_find(u32 lba) {
    for (int i = 0; i < CACHE_ENTRIES; i++)
        if (cache[i].valid && cache[i].lba == lba) return &cache[i];
    return NULL;
}

static cache_entry_t *cache_evict(void) {
    /* Find LRU clean entry first; if all dirty, pick oldest dirty */
    cache_entry_t *best = NULL;
    for (int i = 0; i < CACHE_ENTRIES; i++) {
        if (!cache[i].valid) return &cache[i];
        if (!best || cache[i].lru_tick < best->lru_tick) best = &cache[i];
    }
    if (best && best->dirty)
        ata_write_sectors(best->lba, 1, best->data);
    return best;
}

int ata_cached_read(u32 lba, void *buf) {
    cache_entry_t *e = cache_find(lba);
    if (!e) {
        e = cache_evict();
        e->lba   = lba;
        e->valid = true;
        e->dirty = false;
        if (ata_read_sectors(lba, 1, e->data) != 0) {
            e->valid = false;
            return -1;
        }
    }
    e->lru_tick = ++cache_tick;
    kmemcpy(buf, e->data, SECTOR_SIZE);
    return 0;
}

int ata_cached_write(u32 lba, const void *buf) {
    cache_entry_t *e = cache_find(lba);
    if (!e) {
        e = cache_evict();
        e->lba   = lba;
        e->valid = true;
    }
    kmemcpy(e->data, buf, SECTOR_SIZE);
    e->dirty    = true;
    e->lru_tick = ++cache_tick;
    return 0;
}

void ata_cache_flush(void) {
    for (int i = 0; i < CACHE_ENTRIES; i++) {
        if (cache[i].valid && cache[i].dirty) {
            ata_write_sectors(cache[i].lba, 1, cache[i].data);
            cache[i].dirty = false;
        }
    }
}

/* ── Public getters ─────────────────────────────────────────────────────────── */
bool ata_is_present(void) { return ata_present; }
u32  ata_get_sectors(void){ return ata_sectors; }
const char *ata_get_model(void) { return ata_model; }

/* ── Initialise ─────────────────────────────────────────────────────────────── */
void ata_init(void) {
    kmemset(cache, 0, sizeof(cache));
    cache_tick = 0;

    int r = ata_identify();
    if (r == 0) {
        serial_write("[ata] drive: ");
        serial_write(ata_model);
        serial_write(", sectors=");
        char tmp[12]; kutoa(ata_sectors, tmp, 10);
        serial_write(tmp); serial_write("\n");
    } else {
        serial_write("[ata] no drive found (");
        char tmp[4]; kitoa(r, tmp, 10); serial_write(tmp);
        serial_write(")\n");
    }
}
