/* =============================================================================
 * CareOS - drivers/speaker.c
 * PC Speaker driver (Programmable Interval Timer Channel 2)
 * ============================================================================= */

#include "kernel.h"

/* ── PIT Ports ── */
#define PIT_CH2          0x42
#define PIT_CMD          0x43
#define PIT_SPKR         0x61

/* Play a frequency on the PC speaker */
void speaker_play(u32 freq) {
    if (freq == 0) {
        /* Stop sound */
        u8 tmp = inl(PIT_SPKR);
        outl(PIT_SPKR, tmp & 0xFC);
        return;
    }

    u32 div = 1193180 / freq;
    
    /* Set PIT channel 2 to mode 3 (square wave) */
    outl(PIT_CMD, 0xB6);
    outl(PIT_CH2, (u8)(div & 0xFF));
    outl(PIT_CH2, (u8)((div >> 8) & 0xFF));

    /* Connect PIT2 to speaker */
    u8 tmp = inl(PIT_SPKR);
    if ((tmp & 0x03) != 0x03) {
        outl(PIT_SPKR, tmp | 0x03);
    }
}

/* Stop sound */
void speaker_stop(void) {
    u8 tmp = inl(PIT_SPKR);
    outl(PIT_SPKR, tmp & 0xFC);
}

/* Beep for a duration (blocks) */
void speaker_beep(u32 freq, u32 ms) {
    speaker_play(freq);
    timer_wait(ms);
    speaker_stop();
}

/* Play a system startup melody */
void speaker_startup(void) {
    speaker_beep(523, 100); // C5
    speaker_beep(659, 100); // E5
    speaker_beep(784, 100); // G5
    speaker_beep(1046, 200); // C6
}

/* System error sound */
void speaker_error(void) {
    speaker_beep(440, 100);
    timer_wait(50);
    speaker_beep(440, 200);
}
