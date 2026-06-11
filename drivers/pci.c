/* =============================================================================
 * CareOS - drivers/pci.c
 * PCI bus enumeration — brute-force scan of bus 0-7, device 0-31, function 0-7
 * ============================================================================= */

#include "kernel.h"

/* ── PCI config-space ports ─────────────────────────────────────────────────── */
#define PCI_CONFIG_ADDR  0xCF8
#define PCI_CONFIG_DATA  0xCFC

#define PCI_MAX_DEVICES  64

static pci_device_t pci_devices[PCI_MAX_DEVICES];
static u32          pci_dev_count = 0;

/* ── Low-level read ─────────────────────────────────────────────────────────── */
u32 pci_read32(u8 bus, u8 dev, u8 func, u8 offset) {
    u32 addr = (1u << 31)
             | ((u32)bus  << 16)
             | ((u32)dev  << 11)
             | ((u32)func <<  8)
             | (offset & 0xFC);
    outl(PCI_CONFIG_ADDR, addr);
    return inl(PCI_CONFIG_DATA);
}

u16 pci_read16(u8 bus, u8 dev, u8 func, u8 offset) {
    u32 v = pci_read32(bus, dev, func, offset & ~3u);
    return (u16)((v >> ((offset & 2) * 8)) & 0xFFFF);
}

u8 pci_read8(u8 bus, u8 dev, u8 func, u8 offset) {
    u32 v = pci_read32(bus, dev, func, offset & ~3u);
    return (u8)((v >> ((offset & 3) * 8)) & 0xFF);
}

void pci_write32(u8 bus, u8 dev, u8 func, u8 offset, u32 val) {
    u32 addr = (1u << 31)
             | ((u32)bus  << 16)
             | ((u32)dev  << 11)
             | ((u32)func <<  8)
             | (offset & 0xFC);
    outl(PCI_CONFIG_ADDR, addr);
    outl(PCI_CONFIG_DATA, val);
}

/* ── PCI class/subclass names (abbreviated) ────────────────────────────────── */
/* ── PCI Vendor/Device names lookup ────────────────────────────────────────── */
const char *pci_vendor_name(u16 vid) {
    switch (vid) {
    case 0x8086: return "Intel";
    case 0x10EC: return "Realtek";
    case 0x1AF4: return "VirtIO";
    case 0x1234: return "QEMU";
    case 0x1022: return "AMD";
    case 0x10DE: return "NVIDIA";
    default:     return "Unknown";
    }
}

const char *pci_device_name(u16 vid, u16 did) {
    if (vid == 0x8086) {
        switch (did) {
        case 0x1237: return "440FX Chipset";
        case 0x7000: return "PIIX3 ISA Bridge";
        case 0x7010: return "PIIX3 IDE Controller";
        case 0x7113: return "PIIX4 Power Mgmt";
        case 0x100E: return "82540EM (e1000)";
        case 0x10D3: return "82574L (e1000e)";
        case 0x2415: return "AC'97 Audio";
        default:     break;
        }
    } else if (vid == 0x1234) {
        if (did == 0x1111) return "Std VGA Adapter";
    } else if (vid == 0x1AF4) {
        if (did >= 0x1000 && did <= 0x103F) return "VirtIO Device";
    }
    return "Generic Peripheral";
}

static const char *pci_class_name(u8 cls, u8 sub) {
    switch (cls) {
    case 0x00: return "Legacy";
    case 0x01:
        switch (sub) {
        case 0x01: return "IDE Controller";
        case 0x06: return "SATA (AHCI)";
        case 0x08: return "NVMe";
        default:   return "Storage";
        }
    case 0x02:
        switch (sub) {
        case 0x00: return "Ethernet";
        case 0x80: return "Network (other)";
        default:   return "Network";
        }
    case 0x03: return "Display/VGA";
    case 0x04: return "Multimedia";
    case 0x05: return "Memory";
    case 0x06:
        switch (sub) {
        case 0x00: return "Host Bridge";
        case 0x01: return "ISA Bridge";
        case 0x04: return "PCI-PCI Bridge";
        default:   return "Bridge";
        }
    case 0x07: return "Serial/Comm";
    case 0x08: return "System";
    case 0x09: return "Input";
    case 0x0C:
        switch (sub) {
        case 0x03: return "USB";
        case 0x05: return "SMBus";
        default:   return "Serial Bus";
        }
    default:   return "Unknown";
    }
}

/* ── Scan ───────────────────────────────────────────────────────────────────── */
void pci_scan(void) {
    pci_dev_count = 0;

    for (u16 bus = 0; bus < 8; bus++) {
        for (u8 dev = 0; dev < 32; dev++) {
            for (u8 func = 0; func < 8; func++) {
                u32 id = pci_read32((u8)bus, dev, func, 0x00);
                if ((id & 0xFFFF) == 0xFFFF) continue;  /* no device */

                if (pci_dev_count >= PCI_MAX_DEVICES) goto done;

                pci_device_t *d = &pci_devices[pci_dev_count++];
                d->bus      = (u8)bus;
                d->device   = dev;
                d->function = func;
                d->vendor_id = (u16)(id & 0xFFFF);
                d->device_id = (u16)((id >> 16) & 0xFFFF);

                u32 cls_reg = pci_read32((u8)bus, dev, func, 0x08);
                d->class_code = (u8)((cls_reg >> 24) & 0xFF);
                d->subclass   = (u8)((cls_reg >> 16) & 0xFF);
                d->prog_if    = (u8)((cls_reg >> 8)  & 0xFF);
                d->revision   = (u8)(cls_reg & 0xFF);

                u32 hdr = pci_read32((u8)bus, dev, func, 0x0C);
                d->header_type = (u8)((hdr >> 16) & 0xFF);

                d->irq = pci_read8((u8)bus, dev, func, 0x3C);

                /* Read first two BARs */
                d->bar[0] = pci_read32((u8)bus, dev, func, 0x10);
                d->bar[1] = pci_read32((u8)bus, dev, func, 0x14);
                d->bar[2] = pci_read32((u8)bus, dev, func, 0x18);
                d->bar[3] = pci_read32((u8)bus, dev, func, 0x1C);
                d->bar[4] = pci_read32((u8)bus, dev, func, 0x20);
                d->bar[5] = pci_read32((u8)bus, dev, func, 0x24);

                serial_write("[pci] ");
                char tmp[8];
                kutoa(bus, tmp, 10); serial_write(tmp); serial_write(":");
                kutoa(dev, tmp, 10); serial_write(tmp); serial_write(".");
                kutoa(func, tmp, 10); serial_write(tmp);
                serial_write("  VID="); kutoa(d->vendor_id, tmp, 16); serial_write(tmp);
                serial_write(" DID="); kutoa(d->device_id, tmp, 16); serial_write(tmp);
                serial_write("  "); serial_write(pci_class_name(d->class_code, d->subclass));
                serial_write("\n");

                serial_write("    BAR0="); kutoa(d->bar[0], tmp, 16); serial_write(tmp);
                serial_write(" BAR1=");     kutoa(d->bar[1], tmp, 16); serial_write(tmp);
                serial_write(" BAR2=");     kutoa(d->bar[2], tmp, 16); serial_write(tmp);
                serial_write(" BAR3=");     kutoa(d->bar[3], tmp, 16); serial_write(tmp);
                serial_write(" BAR4=");     kutoa(d->bar[4], tmp, 16); serial_write(tmp);
                serial_write(" BAR5=");     kutoa(d->bar[5], tmp, 16); serial_write(tmp);
                serial_write("\n");

                /* If not multi-function, skip remaining functions */
                if (func == 0 && !(d->header_type & 0x80)) break;
            }
        }
    }
done:
    serial_write("[pci] scan complete, ");
    char tmp[8]; kutoa(pci_dev_count, tmp, 10);
    serial_write(tmp); serial_write(" devices\n");
}

/* ── Find a device by class/subclass ────────────────────────────────────────── */
pci_device_t *pci_find_class(u8 cls, u8 sub) {
    for (u32 i = 0; i < pci_dev_count; i++)
        if (pci_devices[i].class_code == cls &&
            pci_devices[i].subclass   == sub)
            return &pci_devices[i];
    return NULL;
}

/* Find by vendor + device ID */
pci_device_t *pci_find_device(u16 vendor, u16 device) {
    for (u32 i = 0; i < pci_dev_count; i++)
        if (pci_devices[i].vendor_id == vendor &&
            pci_devices[i].device_id == device)
            return &pci_devices[i];
    return NULL;
}

u32 pci_device_count(void) { return pci_dev_count; }
pci_device_t *pci_device_get(u32 idx) {
    return idx < pci_dev_count ? &pci_devices[idx] : NULL;
}

/* ── List PCI devices to terminal ───────────────────────────────────────────── */
void pci_list(void) {
    terminal_writeln("BUS  DEV  FUN  VENDOR  DEVICE  CLASS                IRQ");
    terminal_writeln("---- ---- ---- ------  ------  -------------------- ---");
    for (u32 i = 0; i < pci_dev_count; i++) {
        pci_device_t *d = &pci_devices[i];
        char buf[80];
        char tmp[8];

        kutoa(d->bus, tmp, 10);      kstrcpy(buf, tmp); kstrcat(buf, "    ");
        buf[4] = '\0';
        kutoa(d->device, tmp, 10);   kstrcat(buf, tmp); kstrcat(buf, "    ");
        buf[9] = '\0';
        kutoa(d->function, tmp, 10); kstrcat(buf, tmp); kstrcat(buf, "    ");
        buf[14] = '\0';

        char vid[6]; kutoa(d->vendor_id, vid, 16);
        kstrcat(buf, vid); kstrcat(buf, "    ");

        char did[6]; kutoa(d->device_id, did, 16);
        kstrcat(buf, did); kstrcat(buf, "  ");

        /* Show vendor + device name */
        const char *vname = pci_vendor_name(d->vendor_id);
        const char *dname = pci_device_name(d->vendor_id, d->device_id);
        kstrcat(buf, vname); kstrcat(buf, " "); kstrcat(buf, dname);
        
        terminal_writeln(buf);
    }
}

/* ── Initialise ─────────────────────────────────────────────────────────────── */
void pci_init(void) {
    pci_scan();
    /* Do not auto-map all PCI BARs here.
       Let each driver map only the MMIO region it actually owns. */
}

/* ── Alias expected by shell.c ───────────────────────────────────────────── */
void pci_list_devices(void) { pci_list(); }
