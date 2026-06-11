/* =============================================================================
 * CareOS - drivers/rtc.c
 * Real-Time Clock driver (CMOS RTC, ports 0x70 / 0x71)
 * ============================================================================= */

#include "kernel.h"

/* ── CMOS registers ─────────────────────────────────────────────────────────── */
#define CMOS_ADDR   0x70
#define CMOS_DATA   0x71

#define RTC_SECONDS  0x00
#define RTC_MINUTES  0x02
#define RTC_HOURS    0x04
#define RTC_WEEKDAY  0x06
#define RTC_DAY      0x07
#define RTC_MONTH    0x08
#define RTC_YEAR     0x09
#define RTC_CENTURY  0x32   /* may not exist on all hardware */
#define RTC_STATUS_A 0x0A
#define RTC_STATUS_B 0x0B

/* ── Helpers ────────────────────────────────────────────────────────────────── */
static u8 cmos_read(u8 reg) {
    outb(CMOS_ADDR, reg | 0x80);   /* NMI disabled while reading */
    io_wait();
    return inb(CMOS_DATA);
}

/* Wait until the RTC is not in an update cycle */
static void rtc_wait_ready(void) {
    /* Status register A bit 7 = update in progress */
    while (cmos_read(RTC_STATUS_A) & 0x80)
        ;
}

/* BCD to binary conversion */
static u8 bcd2bin(u8 bcd) {
    return (u8)(((bcd >> 4) * 10) + (bcd & 0x0F));
}

/* ── Public API ─────────────────────────────────────────────────────────────── */
void rtc_read(rtc_time_t *t) {
    if (!t) return;

    /* Read twice to detect an update mid-read */
    rtc_time_t a, b;
    u8 status_b;

    do {
        rtc_wait_ready();
        a.second = cmos_read(RTC_SECONDS);
        a.minute = cmos_read(RTC_MINUTES);
        a.hour   = cmos_read(RTC_HOURS);
        a.day    = cmos_read(RTC_DAY);
        a.month  = cmos_read(RTC_MONTH);
        a.year   = cmos_read(RTC_YEAR);

        rtc_wait_ready();
        b.second = cmos_read(RTC_SECONDS);
        b.minute = cmos_read(RTC_MINUTES);
        b.hour   = cmos_read(RTC_HOURS);
        b.day    = cmos_read(RTC_DAY);
        b.month  = cmos_read(RTC_MONTH);
        b.year   = cmos_read(RTC_YEAR);
    } while (a.second != b.second || a.minute != b.minute);

    status_b = cmos_read(RTC_STATUS_B);

    /* Convert BCD to binary if needed (bit 2 of status B = binary mode) */
    if (!(status_b & 0x04)) {
        a.second = bcd2bin(a.second);
        a.minute = bcd2bin(a.minute);
        a.hour   = bcd2bin(a.hour & 0x7F);   /* strip AM/PM bit first */
        a.day    = bcd2bin(a.day);
        a.month  = bcd2bin(a.month);
        a.year   = bcd2bin(a.year);
    }

    /* 12-hour → 24-hour conversion if bit 1 of status B is clear */
    if (!(status_b & 0x02)) {
        bool pm = (cmos_read(RTC_HOURS) & 0x80) != 0;
        if (pm && a.hour != 12) a.hour += 12;
        else if (!pm && a.hour == 12) a.hour = 0;
    }

    /* Full 4-digit year: CMOS stores only 2 digits */
    u16 full_year = a.year;
    u8  century   = cmos_read(RTC_CENTURY);
    if (century != 0) {
        if (!(status_b & 0x04)) century = bcd2bin(century);
        full_year = (u16)(century * 100 + a.year);
    } else {
        full_year = (u16)(a.year < 70 ? 2000 + a.year : 1900 + a.year);
    }

    t->second = a.second;
    t->minute = a.minute;
    t->hour   = a.hour;
    t->day    = a.day;
    t->month  = a.month;
    t->year   = full_year;
}

/* Format as "HH:MM:SS" into buf (must be ≥ 9 bytes) */
void rtc_format_time(const rtc_time_t *t, char *buf) {
    if (!t || !buf) return;
    char tmp[4];

    /* Hours */
    if (t->hour < 10) { buf[0] = '0'; kutoa(t->hour, tmp, 10); buf[1] = tmp[0]; }
    else { kutoa(t->hour, buf, 10); }
    buf[2] = ':';
    /* Minutes */
    if (t->minute < 10) { buf[3] = '0'; kutoa(t->minute, tmp, 10); buf[4] = tmp[0]; }
    else { kutoa(t->minute, buf + 3, 10); }
    buf[5] = ':';
    /* Seconds */
    if (t->second < 10) { buf[6] = '0'; kutoa(t->second, tmp, 10); buf[7] = tmp[0]; }
    else { kutoa(t->second, buf + 6, 10); }
    buf[8] = '\0';
}

/* Format as "YYYY-MM-DD" into buf (must be ≥ 11 bytes) */
void rtc_format_date(const rtc_time_t *t, char *buf) {
    if (!t || !buf) return;
    char tmp[8];

    kutoa(t->year, buf, 10);
    u32 l = kstrlen(buf);
    buf[l++] = '-';
    if (t->month < 10) { buf[l++] = '0'; }
    kutoa(t->month, tmp, 10); buf[l++] = tmp[0]; if (t->month >= 10) buf[l++] = tmp[1];
    buf[l++] = '-';
    if (t->day < 10) { buf[l++] = '0'; }
    kutoa(t->day, tmp, 10); buf[l++] = tmp[0]; if (t->day >= 10) buf[l++] = tmp[1];
    buf[l] = '\0';
}

void rtc_init(void) {
    /* No IRQ8 setup needed for polling mode */
    serial_write("[rtc] CMOS RTC driver ready\n");
}
