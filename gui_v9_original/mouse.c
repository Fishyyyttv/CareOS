/* =============================================================================
 * CareOS gui/mouse.c -- PS/2 mouse driver
 *
 * Movement profile (v9):
 *   M-01 -- Q8 fixed-point sub-pixel accumulator (256 units = 1 px).
 *   M-02 -- Atomic delta consume for low input latency.
 *   M-03 -- 5-zone acceleration curve for precision and speed.
 *   M-04 -- Small jitter deadzone + per-packet clamp to prevent jump spikes.
 *   M-05 -- Adaptive temporal smoothing: less smoothing at high speed.
 *   M-06 -- IntelliMouse scroll wheel probe.
 *   M-07 -- Larger 13x19 cursor with shadow for better visibility.
 * ============================================================================= */
#include "kernel.h"
#include "gui.h"

#define MOUSE_IRQ            (IRQ0 + 12)
#define MOUSE_RESOLUTION_CMD 0x02  /* 4 counts/mm */
#define MOUSE_SAMPLE_HZ      100
#define MOUSE_DEADZONE_RAW   1
#define MOUSE_MAX_RAW_DELTA  24

/* M-01: sub-pixel accumulators in Q8 fixed-point (256 units = 1 pixel) */
static volatile i32  accum_dx_q8 = 0, accum_dy_q8 = 0;
static volatile bool btn_left = false, btn_right = false;
static volatile i32  accum_scroll = 0;
static u8 cycle = 0;
static u8 pkt[4];
static bool has_scroll_wheel = false;

static void ps2w(void) { u32 t = 100000; while (t-- && (inb(0x64) & 2)) {} }
static void ps2r(void) { u32 t = 100000; while (t-- && !(inb(0x64) & 1)) {} }
static void mwrite(u8 c) { ps2w(); outb(0x64, 0xD4); ps2w(); outb(0x60, c); }
static u8   mread(void)  { ps2r(); return inb(0x60); }

static inline i32 accel_q8(i16 raw) {
    i32 v = (i32)raw;
    if (v > MOUSE_MAX_RAW_DELTA) v = MOUSE_MAX_RAW_DELTA;
    if (v < -MOUSE_MAX_RAW_DELTA) v = -MOUSE_MAX_RAW_DELTA;
    
    /* 1:1 pure raw tracking (256 units = 1 pixel) */
    return v * 256; 
}

/* M-02: delta accumulated inside ISR for minimal latency */
static void mouse_irq(registers_t *r) {
    (void)r;
    u8 st = inb(0x64);
    if (!(st & 0x01)) return;
    if (!(st & 0x20)) { inb(0x60); return; }

    u8 b = inb(0x60);
    u8 pkt_size = has_scroll_wheel ? 4 : 3;

    switch (cycle) {
    case 0:
        if (!(b & 0x08)) return;
        pkt[0] = b;
        cycle = 1;
        break;
    case 1:
        pkt[1] = b;
        cycle = 2;
        break;
    case 2:
        pkt[2] = b;
        if (!has_scroll_wheel) {
            cycle = 0;
        } else {
            cycle = 3;
            break;
        }
        /* Fall through for 3-byte packet processing */
        if (pkt[0] & 0xC0) break;

        btn_left  = (pkt[0] & 1) != 0;
        btn_right = (pkt[0] & 2) != 0;

        {
            i16 dx = (i16)(u8)pkt[1]; if (pkt[0] & 0x10) dx -= 256;
            i16 dy = (i16)(u8)pkt[2]; if (pkt[0] & 0x20) dy -= 256;

            accum_dx_q8 += accel_q8(dx);
            accum_dy_q8 -= accel_q8(dy); /* PS/2 Y is bottom-up */
        }
        break;
    case 3:
        pkt[3] = b;
        cycle = 0;
        if (pkt[0] & 0xC0) break;

        btn_left  = (pkt[0] & 1) != 0;
        btn_right = (pkt[0] & 2) != 0;

        {
            i16 dx = (i16)(u8)pkt[1]; if (pkt[0] & 0x10) dx -= 256;
            i16 dy = (i16)(u8)pkt[2]; if (pkt[0] & 0x20) dy -= 256;

            accum_dx_q8 += accel_q8(dx);
            accum_dy_q8 -= accel_q8(dy);
        }

        /* M-06: scroll wheel (4th byte, signed) */
        {
            i8 sw = (i8)pkt[3];
            if (sw) accum_scroll += (i32)sw;
        }
        break;
    }
}

/* M-06: probe for IntelliMouse scroll wheel */
static void mouse_try_intellimouse(void) {
    /* Magic sequence: set sample rate 200, 100, 80 */
    mwrite(0xF3); mread(); mwrite(200); mread();
    mwrite(0xF3); mread(); mwrite(100); mread();
    mwrite(0xF3); mread(); mwrite(80);  mread();

    /* Read device ID */
    mwrite(0xF2); mread();
    u8 id = mread();
    if (id == 3) {
        has_scroll_wheel = true;
        serial_write("  [mouse] IntelliMouse scroll wheel detected\n");
    } else {
        has_scroll_wheel = false;
    }
}

void mouse_init(void) {
    ps2w(); outb(0x64, 0xA8);
    ps2w(); outb(0x64, 0x20); ps2r();
    u8 cfg = inb(0x60);
    cfg |= 0x02;
    cfg &= ~0x20;
    ps2w(); outb(0x64, 0x60); ps2w(); outb(0x60, cfg);

    mwrite(0xFF); mread(); mread(); mread();  /* reset */
    mwrite(0xF6); mread();                    /* defaults */

    /* M-06: try to detect scroll wheel before setting resolution */
    mouse_try_intellimouse();

    /* Resolution: 2 = 4 counts/mm */
    mwrite(0xE8); mread(); mwrite(MOUSE_RESOLUTION_CMD); mread();

    /* Sample rate */
    mwrite(0xF3); mread(); mwrite(MOUSE_SAMPLE_HZ); mread();

    mwrite(0xF4); mread();  /* enable */

    register_interrupt_handler(MOUSE_IRQ, mouse_irq);
    outb(0xA1, inb(0xA1) & ~(1 << 4));
    outb(0x21, inb(0x21) & ~(1 << 2));
    serial_write("  [mouse] PS/2 ready, 5-zone accel, adaptive smooth");
    if (has_scroll_wheel) serial_write(", scroll wheel");
    serial_write("\n");
}

static i32 cur_x = -1, cur_y = -1;
/* M-01: sub-pixel remainder storage between frames */
static i32 sub_x_q8 = 0, sub_y_q8 = 0;
/* M-05: adaptive temporal smoothing state */
static i32 vel_x_q8 = 0, vel_y_q8 = 0;
static bool prev_left = false, prev_right = false;

void mouse_update(mouse_t *m) {
    if (cur_x < 0) { cur_x = (i32)SCREEN_W / 2; cur_y = (i32)SCREEN_H / 2; }

    /* M-02: atomic consume */
    __asm__ volatile("cli");
    i32 dx_q8 = accum_dx_q8; accum_dx_q8 = 0;
    i32 dy_q8 = accum_dy_q8; accum_dy_q8 = 0;
    bool lb = btn_left, rb = btn_right;
    i32 scroll = accum_scroll; accum_scroll = 0;
    __asm__ volatile("sti");

    /* Apply runtime sensitivity from persistent settings. */
    {
        u32 sens = 100;
        const careos_settings_t *cfg = settings_get();
        if (cfg) sens = cfg->mouse_sensitivity;
        if (sens < 40) sens = 40;
        if (sens > 200) sens = 200;
        dx_q8 = (dx_q8 * (i32)sens) / 100;
        dy_q8 = (dy_q8 * (i32)sens) / 100;
    }

    /* M-05: Raw responsiveness (no heavy temporal smoothing) */
    vel_x_q8 = dx_q8;
    vel_y_q8 = dy_q8;

    /* M-01: add raw delta to sub-pixel remainder */
    sub_x_q8 += vel_x_q8;
    sub_y_q8 += vel_y_q8;

    i32 step_x = sub_x_q8 >> 8;
    i32 step_y = sub_y_q8 >> 8;
    cur_x += step_x;
    cur_y += step_y;

    /* Keep signed sub-pixel remainders to avoid drift bias */
    sub_x_q8 -= step_x << 8;
    sub_y_q8 -= step_y << 8;

    /* Clamp to screen */
    if (cur_x < 0) cur_x = 0;
    if (cur_y < 0) cur_y = 0;
    if (cur_x >= (i32)SCREEN_W) cur_x = (i32)SCREEN_W - 1;
    if (cur_y >= (i32)SCREEN_H) cur_y = (i32)SCREEN_H - 1;

    m->x = cur_x;
    m->y = cur_y;
    m->left = lb;
    m->right = rb;
    m->left_clicked   = lb && !prev_left;
    m->left_released  = !lb && prev_left;
    m->right_clicked  = rb && !prev_right;
    m->right_released = !rb && prev_right;
    m->scroll_delta   = scroll;
    prev_left  = lb;
    prev_right = rb;
}

/* M-07: Larger 13x19 arrow cursor with shadow for depth */
static const u16 cur_fill[19] = {
    0b1000000000000, 0b1100000000000, 0b1110000000000, 0b1111000000000,
    0b1111100000000, 0b1111110000000, 0b1111111000000, 0b1111111100000,
    0b1111111110000, 0b1111111111000, 0b1111111000000, 0b1111011000000,
    0b1100011000000, 0b1000001100000, 0b0000001100000, 0b0000000110000,
    0b0000000110000, 0b0000000000000, 0b0000000000000,
};
static const u16 cur_outl[19] = {
    0b1100000000000, 0b1110000000000, 0b1111000000000, 0b1111100000000,
    0b1111110000000, 0b1111111000000, 0b1111111100000, 0b1111111110000,
    0b1111111111000, 0b1111111111100, 0b1111111111000, 0b1111111100000,
    0b1110111100000, 0b1100011110000, 0b1000011110000, 0b0000001111000,
    0b0000001111000, 0b0000000110000, 0b0000000000000,
};

void mouse_draw_cursor(i32 x, i32 y) {
    /* Shadow pass (offset by 1,1, dark) */
    for (i32 row = 0; row < 19; row++) {
        for (i32 col = 0; col < 13; col++) {
            u16 bit = (u16)(1 << (12 - col));
            if (cur_outl[row] & bit)
                gfx_setpixel(x + col + 1, y + row + 1, rgb(0x08,0x08,0x10));
        }
    }
    /* Main cursor */
    for (i32 row = 0; row < 19; row++) {
        for (i32 col = 0; col < 13; col++) {
            u16 bit = (u16)(1 << (12 - col));
            if (!(cur_outl[row] & bit)) continue;
            gfx_setpixel(x + col, y + row, (cur_fill[row] & bit) ? COL_WHITE : COL_BLACK);
        }
    }
}
