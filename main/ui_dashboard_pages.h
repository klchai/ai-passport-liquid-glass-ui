#pragma once

#include <stdbool.h>
#include <stdint.h>

// Stable identities are also the bit positions in the persisted visibility mask.
typedef enum {
    SHOWCASE_BUTTONS = 0,
    SHOWCASE_SELECTION,
    SHOWCASE_ADJUSTMENTS,
    SHOWCASE_LISTS,
    SHOWCASE_OVERLAYS,
    SHOWCASE_NAVIGATION,
    SHOWCASE_FEEDBACK,
    SHOWCASE_STATES,
    PAGE_KABOO,
    PAGE_CLAUDE,
    PAGE_SETTINGS,
    SHOWCASE_PAGE_COUNT,
} showcase_page_t;

#define UI_DASHBOARD_VISIBLE_ALL ((1u << PAGE_SETTINGS) - 1u)
// Home is the product landing page and cannot be hidden. Settings is always
// reachable as a page, but is intentionally not represented in this mask.
#define UI_DASHBOARD_ALWAYS_VISIBLE_MASK (1u << SHOWCASE_NAVIGATION)
#define UI_DASHBOARD_DEFAULT_PAGE SHOWCASE_NAVIGATION
#define UI_DASHBOARD_SETTINGS_ROWS 4u

extern const showcase_page_t UI_DASHBOARD_PAGE_ORDER[SHOWCASE_PAGE_COUNT];

bool ui_dashboard_page_visible(uint16_t mask, showcase_page_t page);
showcase_page_t ui_dashboard_page_first(uint16_t mask);
showcase_page_t ui_dashboard_page_next(uint16_t mask, showcase_page_t current,
                                      int8_t direction);
uint16_t ui_dashboard_page_toggle(uint16_t mask, showcase_page_t page);
uint8_t ui_dashboard_settings_first(uint8_t selection);
