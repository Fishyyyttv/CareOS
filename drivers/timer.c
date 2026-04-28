/* =============================================================================
 * CareOS - drivers/timer.c
 * PIT (Programmable Interval Timer) driver
 * ============================================================================= */
#include "kernel.h"

static volatile u32 ticks = 0;

void timer_tick_advance(void) {
    ticks++;
}

static void timer_irq(registers_t *r) {
    (void)r;
    timer_tick_advance();
}

void timer_init(u32 hz) {
    register_interrupt_handler(IRQ0, timer_irq);

    u32 divisor = 1193180 / hz;
    outb(PIT_CMD, 0x36);                     /* Channel 0, lobyte/hibyte, mode 3 */
    outb(PIT_CHANNEL0, (u8)(divisor & 0xFF));
    outb(PIT_CHANNEL0, (u8)((divisor >> 8) & 0xFF));
}

u32 timer_get_ticks(void) { return ticks; }

void timer_wait(u32 ms) {
    u32 target = ticks + (ms * PIT_HZ / 1000) + 1;
    while (ticks < target) {
        __asm__ volatile ("hlt");
    }
}
