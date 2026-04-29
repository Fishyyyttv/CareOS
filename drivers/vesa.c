/* CareOS v9 -- drivers/vesa.c
 * Bochs Graphics Adapter (BGA) mode switching.
 * QEMU -vga std exposes BGA at I/O ports 0x01CE/0x01CF (16-bit).
 * Works natively in 64-bit long mode — no real-mode trampoline needed.
 */
#include "kernel.h"
#include "gui.h"

#define BGA_INDEX  0x01CE
#define BGA_DATA   0x01CF

#define BGA_REG_ID     0
#define BGA_REG_XRES   1
#define BGA_REG_YRES   2
#define BGA_REG_BPP    3
#define BGA_REG_ENABLE 4
#define BGA_REG_VIRT_W 6
#define BGA_REG_VIRT_H 7

#define BGA_ENABLE_LFB 0x41   /* enable + linear framebuffer */
#define BGA_DISABLE    0x00

static u16  g_cur_w = 0, g_cur_h = 0;
static bool g_bga_present = false;

static void bga_write(u16 reg, u16 val) {
    outw(BGA_INDEX, reg);
    outw(BGA_DATA,  val);
}

static u16 bga_read(u16 reg) {
    outw(BGA_INDEX, reg);
    return inw(BGA_DATA);
}

static const vesa_mode_t g_modes[] = {
    {  800,  600, " 800x600"  },
    { 1024,  768, "1024x768"  },
    { 1280,  720, "1280x720"  },
    { 1280, 1024, "1280x1024" },
    { 1366,  768, "1366x768"  },
    { 1600,  900, "1600x900"  },
    { 1920, 1080, "1920x1080" },
    { 2560, 1440, "2560x1440" },
};
#define G_MODE_COUNT (sizeof(g_modes)/sizeof(g_modes[0]))

void vesa_init(void) {
    u16 id = bga_read(BGA_REG_ID);
    g_cur_w = (u16)SCREEN_W;
    g_cur_h = (u16)SCREEN_H;

    if (id < 0xB0C0 || id > 0xB0C5) {
        serial_write("[VESA] BGA not present (id=");
        char b[8]; kutoa(id, b, 16);
        serial_write(b); serial_write("), using GRUB fb\n");
        g_bga_present = false;
        return;
    }
    g_bga_present = true;
    serial_write("[VESA] BGA detected, running at GRUB resolution\n");

    /* Apply saved preference if any */
    const careos_settings_t *s = settings_get();
    if (s->vesa_w >= 800 && s->vesa_h >= 600)
        vesa_set_mode((u16)s->vesa_w, (u16)s->vesa_h);
}

int vesa_set_mode(u16 w, u16 h) {
    if (!g_bga_present) return -1;

    bga_write(BGA_REG_ENABLE, BGA_DISABLE);
    bga_write(BGA_REG_XRES,   w);
    bga_write(BGA_REG_YRES,   h);
    bga_write(BGA_REG_BPP,    32);
    bga_write(BGA_REG_VIRT_W, w);
    bga_write(BGA_REG_VIRT_H, h);
    bga_write(BGA_REG_ENABLE, BGA_ENABLE_LFB);

    u16 rw = bga_read(BGA_REG_XRES);
    u16 rh = bga_read(BGA_REG_YRES);
    if (rw != w || rh != h) {
        serial_write("[VESA] mode set failed\n");
        return -1;
    }

    g_cur_w      = w;
    g_cur_h      = h;
    SCREEN_W     = (u32)w;
    SCREEN_H     = (u32)h;
    SCREEN_PITCH = (u32)w * 4u;
    gfx_init(FRAMEBUFFER, (u32)w, (u32)h, SCREEN_PITCH);

    serial_write("[VESA] mode set: ");
    char bw[8], bh[8]; kutoa(w, bw, 10); kutoa(h, bh, 10);
    serial_write(bw); serial_write("x"); serial_write(bh); serial_write("\n");
    return 0;
}

u32         vesa_mode_count(void)   { return (u32)G_MODE_COUNT; }
vesa_mode_t vesa_mode_get(u32 idx) { return g_modes[idx < G_MODE_COUNT ? idx : 0]; }
u16         vesa_current_w(void)    { return g_cur_w; }
u16         vesa_current_h(void)    { return g_cur_h; }
