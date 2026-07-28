/* =============================================================================
 * CareOS - kernel/power.c
 * Machine power control: shut down and reboot.
 *
 * A full ACPI implementation (parse the RSDP -> FADT, write SLP_TYPa|SLP_EN to
 * PM1a_CNT) is a lot of table walking for a hobby kernel that runs under QEMU.
 * The emulator "shutdown ports" below are what QEMU/Bochs/VirtualBox expose for
 * exactly this, and reboot is a pulse of the 8042 reset line -- both are one
 * instruction and work on the targets CareOS actually boots on. Each path falls
 * back to halting the CPU so a failure never runs off into garbage.
 * ============================================================================= */
#include "kernel.h"

void power_shutdown(void) {
    serial_write("  [power] shutdown requested\n");
    __asm__ volatile("cli");
    outw(0x0604, 0x2000);   /* QEMU >= 2.0 ACPI                 */
    outw(0xB004, 0x2000);   /* Bochs / older QEMU               */
    outw(0x4004, 0x3400);   /* VirtualBox                       */
    for (;;) __asm__ volatile("hlt");
}

void power_reboot(void) {
    serial_write("  [power] reboot requested\n");
    __asm__ volatile("cli");
    /* Pulse the 8042 keyboard-controller reset line (bit 0 of the command). */
    u8 status = 0x02;
    while (status & 0x02) status = inb(0x64);
    outb(0x64, 0xFE);
    /* If the controller ignored it, fall through to a hard halt. */
    for (;;) __asm__ volatile("hlt");
}
