#pragma once

#include <stdint.h>

typedef enum {
    DASHBOARD_PREFS_SAVED,
    DASHBOARD_PREFS_PENDING,
    DASHBOARD_PREFS_ERROR,
} dashboard_preferences_status_t;

// Initialize and flush only in the application task, outside the LVGL lock.
void dashboard_preferences_init(void);
void dashboard_preferences_flush(void);

// Atomic snapshots/publication; safe in UI and button callbacks, no flash I/O.
uint16_t dashboard_preferences_pages(void);
void dashboard_preferences_set_pages(uint16_t mask);
dashboard_preferences_status_t dashboard_preferences_status(void);
