// Fake RTC that advances time each second using a hardware timer.
// Only relies on libgba / standard C facilities so it works under mGBA.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gba_timers.h>

#include "driver/rtc.h"

// Prescaler 1024 -> 16,777,216 / 1024 = 16384 ticks/sec
#define TICKS_PER_SEC 16384U

static int rtc_initialized = 0;
static u16 last_timer = 0;
static u32 accum_ticks = 0; // accumulated ticks < TICKS_PER_SEC

// Internal numeric time representation (not BCD)
static u8 year   = 25; // 0-99
static u8 month  = 11; // 1-12
static u8 day    = 10; // 1-31 (simplified month lengths handled)
static u8 wday   = 1;  // 1-7
static u8 hour   = 13;  // 0-23
static u8 minute = 37;  // 0-59
static u8 second = 0;  // 0-59

static const u8 days_in_month_table[12] = {31,28,31,30,31,30,31,31,30,31,30,31};

static void init_timer_if_needed(void) {
    if (!rtc_initialized) {
        REG_TM3CNT_H = 0;        // stop
        REG_TM3CNT_L = 0;        // clear counter
        accum_ticks = 0;
        last_timer = 0;
        // Enable Timer 3, prescaler 1024 (bits 0-1 = 3), bit 7 = enable
        REG_TM3CNT_H = 0x0080 | 0x0003;
        rtc_initialized = 1;
    }
}

// Advance internal clock based on elapsed timer ticks.
static void rtc_update(void) {
    if (!rtc_initialized) return; // not enabled yet
    u16 cur = REG_TM3CNT_L;
    u16 delta = (u16)(cur - last_timer); // handles wrap naturally
    last_timer = cur;
    accum_ticks += delta;
    while (accum_ticks >= TICKS_PER_SEC) {
        accum_ticks -= TICKS_PER_SEC;
        // advance one second
        second++;
        if (second >= 60) { second = 0; minute++; }
        if (minute >= 60) { minute = 0; hour++; }
        if (hour >= 24) { hour = 0; day++; wday++; if (wday > 7) wday = 1; }
        // handle month/day rollover (simplified, ignores leap years)
        u8 dim = days_in_month_table[(month-1) % 12];
        if (day > dim) { day = 1; month++; if (month > 12) { month = 1; year = (year + 1) % 100; } }
    }
}

static u8 bcd(u8 v) { return _BCD(v); }

// Public API compatibility -------------------------------------------------
void rtc_enable(void) { init_timer_if_needed(); }
void rtc_disenable(void) { /* keep running to preserve time */ }
void rtc_cmd(int v) { (void)v; }
void rtc_data(int v) { (void)v; }
int rtc_read(void) { return 0; }

int rtc_get(u8 *data) {
    if (!data) return -1;
    rtc_update();
    // Return BCD-coded full date/time: YY MM DD WKD HH MM SS
    data[0] = bcd(year);
    data[1] = bcd(month);
    data[2] = bcd(day);
    data[3] = bcd(wday); // weekday
    data[4] = bcd(hour);
    data[5] = bcd(minute);
    data[6] = bcd(second);
    return 0;
}

int rtc_gettime(u8 *data) {
    if (!data) return -1;
    rtc_update();
    data[0] = bcd(hour);
    data[1] = bcd(minute);
    data[2] = bcd(second);
    return 0;
}

void rtc_set(u8 *data) {
    // Expect numeric values (same semantics as real driver before BCD conversion).
    if (!data) return;
    // Order: YY,MM,DD,WKD,HH,MM,SS
    year   = data[0] % 100;
    month  = data[1] ? (data[1] % 13) : 1;
    if (month == 0) month = 1;
    day    = data[2] ? data[2] : 1;
    wday   = data[3] ? ((data[3]-1)%7)+1 : 1;
    hour   = data[4] % 24;
    minute = data[5] % 60;
    second = data[6] % 60;
}
