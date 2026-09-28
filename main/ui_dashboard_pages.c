#include "ui_dashboard_pages.h"

const showcase_page_t UI_DASHBOARD_PAGE_ORDER[UI_DASHBOARD_ORDER_COUNT] = {
    SHOWCASE_NAVIGATION, SHOWCASE_ADJUSTMENTS, SHOWCASE_LISTS,
    SHOWCASE_FEEDBACK, SHOWCASE_STATES, PAGE_KABOO, PAGE_CLAUDE,
    PAGE_SETTINGS,
};

bool ui_dashboard_page_visible(uint16_t mask, showcase_page_t page)
{
    if (page == PAGE_SETTINGS) return true;
    uint16_t visible = (uint16_t)((mask & UI_DASHBOARD_VISIBLE_ALL) |
                                  UI_DASHBOARD_ALWAYS_VISIBLE_MASK);
    return page >= 0 && page < PAGE_SETTINGS &&
           (visible & (1u << page)) != 0;
}

showcase_page_t ui_dashboard_page_first(uint16_t mask)
{
    // Home is the stable landing page, independent of presentation order and
    // of which optional pages were last enabled.
    (void)mask;
    return UI_DASHBOARD_DEFAULT_PAGE;
}

showcase_page_t ui_dashboard_page_next(uint16_t mask, showcase_page_t current,
                                      int8_t direction)
{
    unsigned position = 0;
    while (position < UI_DASHBOARD_ORDER_COUNT &&
           UI_DASHBOARD_PAGE_ORDER[position] != current) ++position;
    // A page outside the order (a retired id) resumes at the landing page.
    if (position == UI_DASHBOARD_ORDER_COUNT) {
        return ui_dashboard_page_first(mask);
    }
    for (unsigned i = 0; i < UI_DASHBOARD_ORDER_COUNT; ++i) {
        position = (position +
                    (direction < 0 ? UI_DASHBOARD_ORDER_COUNT - 1u : 1u)) %
                   UI_DASHBOARD_ORDER_COUNT;
        if (ui_dashboard_page_visible(mask, UI_DASHBOARD_PAGE_ORDER[position])) {
            return UI_DASHBOARD_PAGE_ORDER[position];
        }
    }
    return PAGE_SETTINGS;
}

uint16_t ui_dashboard_page_toggle(uint16_t mask, showcase_page_t page)
{
    mask = (mask & UI_DASHBOARD_VISIBLE_ALL) |
           UI_DASHBOARD_ALWAYS_VISIBLE_MASK;
    // Home cannot be hidden, and retired pages have nothing to toggle.
    if (page >= 0 && page < PAGE_SETTINGS && page != SHOWCASE_NAVIGATION &&
        (UI_DASHBOARD_VISIBLE_ALL & (1u << page)) != 0) {
        mask ^= (1u << page);
    }
    return mask;
}

uint8_t ui_dashboard_settings_first(uint8_t selection)
{
    if (selection >= UI_DASHBOARD_CONTENT_PAGES) selection = 0;
    return selection / UI_DASHBOARD_SETTINGS_ROWS * UI_DASHBOARD_SETTINGS_ROWS;
}
