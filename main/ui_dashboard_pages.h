#pragma once

#include <stdbool.h>
#include <stdint.h>

// Stable identities are also the bit positions in the persisted visibility
// mask (NVS "pages_v1"), so an id never moves or gets reused. Retired pages
// keep their number, never appear, and their stored bits are ignored.
typedef enum {
    PAGE_RETIRED_MOMENTS = 0,
    PAGE_RETIRED_FOCUS,
    SHOWCASE_ADJUSTMENTS,
    SHOWCASE_LISTS,
    PAGE_RETIRED_PLAYER,
    SHOWCASE_NAVIGATION,
    SHOWCASE_FEEDBACK,
    SHOWCASE_STATES,
    PAGE_KABOO,
    PAGE_CLAUDE,
    PAGE_SETTINGS,
    SHOWCASE_PAGE_COUNT,
} showcase_page_t;

// Every content page that can still appear; only these bits survive in the
// mask, so a mask saved by older firmware drops its retired pages on load.
#define UI_DASHBOARD_VISIBLE_ALL \
    ((1u << SHOWCASE_ADJUSTMENTS) | (1u << SHOWCASE_LISTS) | \
     (1u << SHOWCASE_NAVIGATION) | (1u << SHOWCASE_FEEDBACK) | \
     (1u << SHOWCASE_STATES) | (1u << PAGE_KABOO) | (1u << PAGE_CLAUDE))
// Home is the product landing page and cannot be hidden. Settings is always
// reachable as a page, but is intentionally not represented in this mask.
#define UI_DASHBOARD_ALWAYS_VISIBLE_MASK (1u << SHOWCASE_NAVIGATION)
#define UI_DASHBOARD_DEFAULT_PAGE SHOWCASE_NAVIGATION
#define UI_DASHBOARD_SETTINGS_ROWS 4u
// Presentation order: the content pages Settings lists, then Settings itself.
#define UI_DASHBOARD_CONTENT_PAGES 7u
#define UI_DASHBOARD_ORDER_COUNT (UI_DASHBOARD_CONTENT_PAGES + 1u)

extern const showcase_page_t UI_DASHBOARD_PAGE_ORDER[UI_DASHBOARD_ORDER_COUNT];

bool ui_dashboard_page_visible(uint16_t mask, showcase_page_t page);
showcase_page_t ui_dashboard_page_first(uint16_t mask);
showcase_page_t ui_dashboard_page_next(uint16_t mask, showcase_page_t current,
                                      int8_t direction);
uint16_t ui_dashboard_page_toggle(uint16_t mask, showcase_page_t page);
uint8_t ui_dashboard_settings_first(uint8_t selection);
