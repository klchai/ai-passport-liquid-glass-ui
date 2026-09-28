#include "ui_dashboard_pages.h"

const showcase_page_t UI_DASHBOARD_PAGE_ORDER[SHOWCASE_PAGE_COUNT] = {
    SHOWCASE_OVERLAYS, SHOWCASE_NAVIGATION, SHOWCASE_SELECTION,
    SHOWCASE_ADJUSTMENTS, SHOWCASE_LISTS, SHOWCASE_FEEDBACK,
    SHOWCASE_BUTTONS, SHOWCASE_STATES, PAGE_KABOO, PAGE_CLAUDE, PAGE_SETTINGS,
};

bool ui_dashboard_page_visible(uint16_t mask, showcase_page_t page)
{
    if (page == PAGE_SETTINGS) return true;
    return page >= 0 && page < PAGE_SETTINGS &&
           ((mask | UI_DASHBOARD_ALWAYS_VISIBLE_MASK) & (1u << page)) != 0;
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
    while (position < SHOWCASE_PAGE_COUNT &&
           UI_DASHBOARD_PAGE_ORDER[position] != current) ++position;
    if (position == SHOWCASE_PAGE_COUNT) return ui_dashboard_page_first(mask);
    for (unsigned i = 0; i < SHOWCASE_PAGE_COUNT; ++i) {
        position = (position + (direction < 0 ? SHOWCASE_PAGE_COUNT - 1u : 1u))
                   % SHOWCASE_PAGE_COUNT;
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
    if (page >= 0 && page < PAGE_SETTINGS &&
        page != SHOWCASE_NAVIGATION) {
        mask ^= (1u << page);
    }
    return mask;
}

uint8_t ui_dashboard_settings_first(uint8_t selection)
{
    if (selection >= PAGE_SETTINGS) selection = 0;
    return selection / UI_DASHBOARD_SETTINGS_ROWS * UI_DASHBOARD_SETTINGS_ROWS;
}
