#pragma once

#include <stdbool.h>
#include <stdint.h>

// The dashboard clock is set from the computer payload when BLE is connected,
// and from ntp1.aliyun.com whenever a network interface is available. The
// system clock keeps advancing between synchronizations, including offline.
void time_sync_init(void);

// Called by the application task once per second. Applies deferred updates
// from the BLE host and persists a recent timestamp for the next boot.
void time_sync_poll(void);

// Non-blocking offer used by the NimBLE write callback. The application task
// performs settimeofday/NVS work on its next poll.
void time_sync_offer_computer(uint32_t unix_time, int16_t tz_offset_minutes);

typedef struct {
    int hour;
    int minute;
    int second;
    int year;
    int month;
    int day;
    int weekday; // 0 = Sunday ... 6 = Saturday
} time_sync_wall_clock_t;

// Read the current system clock with the last known timezone offset. Returns
// false until a plausible (2020 or newer) timestamp has been synchronized.
bool time_sync_get_wall_clock(time_sync_wall_clock_t *out);
