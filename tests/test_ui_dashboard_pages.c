#include "ui_dashboard_pages.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    // The order lists every live content page exactly once, then Settings.
    unsigned listed = 0;
    for (unsigned i = 0; i < UI_DASHBOARD_CONTENT_PAGES; ++i) {
        showcase_page_t page = UI_DASHBOARD_PAGE_ORDER[i];
        assert(page >= 0 && page < PAGE_SETTINGS && !(listed & (1u << page)));
        listed |= 1u << page;
    }
    assert(listed == UI_DASHBOARD_VISIBLE_ALL);
    assert(UI_DASHBOARD_PAGE_ORDER[UI_DASHBOARD_CONTENT_PAGES] == PAGE_SETTINGS);

    // Retired ids keep their bit but never appear, whatever a stored mask says.
    const showcase_page_t retired[] = {
        PAGE_RETIRED_MOMENTS, PAGE_RETIRED_FOCUS, PAGE_RETIRED_PLAYER,
    };
    for (unsigned i = 0; i < sizeof(retired) / sizeof(retired[0]); ++i) {
        assert(!(UI_DASHBOARD_VISIBLE_ALL & (1u << retired[i])));
        assert(!ui_dashboard_page_visible(0xffff, retired[i]));
        assert(ui_dashboard_page_toggle(0xffff, retired[i]) == UI_DASHBOARD_VISIBLE_ALL);
        assert(ui_dashboard_page_toggle(0, retired[i]) == UI_DASHBOARD_ALWAYS_VISIBLE_MASK);
        assert(ui_dashboard_page_next(0xffff, retired[i], 1) == SHOWCASE_NAVIGATION);
        assert(ui_dashboard_page_next(0xffff, retired[i], -1) == SHOWCASE_NAVIGATION);
    }

    assert(ui_dashboard_page_first(UI_DASHBOARD_VISIBLE_ALL) == SHOWCASE_NAVIGATION);
    assert(ui_dashboard_page_first(0) == SHOWCASE_NAVIGATION);
    assert(ui_dashboard_page_first(1u << PAGE_CLAUDE) == SHOWCASE_NAVIGATION);
    assert(!ui_dashboard_page_visible(0xffff, SHOWCASE_PAGE_COUNT));
    assert(!ui_dashboard_page_visible(0xffff, (showcase_page_t)-1));
    assert(ui_dashboard_page_toggle(0, PAGE_SETTINGS) == UI_DASHBOARD_ALWAYS_VISIBLE_MASK);
    assert(ui_dashboard_page_toggle(0xffff, PAGE_SETTINGS) == UI_DASHBOARD_VISIBLE_ALL);
    assert(ui_dashboard_page_toggle(0, SHOWCASE_NAVIGATION) ==
           UI_DASHBOARD_ALWAYS_VISIBLE_MASK);
    // Every mask older firmware could have stored, retired bits included.
    for (uint16_t mask = 0; mask < (1u << PAGE_SETTINGS); ++mask) {
        uint16_t normalized = (mask & UI_DASHBOARD_VISIBLE_ALL) |
                              UI_DASHBOARD_ALWAYS_VISIBLE_MASK;
        unsigned count = 1;
        for (unsigned page = 0; page < PAGE_SETTINGS; ++page) {
            count += !!(normalized & (1u << page));
        }
        unsigned seen = 0;
        showcase_page_t first = ui_dashboard_page_first(mask), page = first;
        for (unsigned i = 0; i < count; ++i) {
            assert(ui_dashboard_page_visible(mask, page));
            assert(!(seen & (1u << page)));
            seen |= 1u << page;
            showcase_page_t next = ui_dashboard_page_next(mask, page, 1);
            assert(ui_dashboard_page_next(mask, next, -1) == page);
            page = next;
        }
        assert(page == first);
        assert(seen == (normalized | (1u << PAGE_SETTINGS)));
        for (unsigned hidden = 0; hidden < PAGE_SETTINGS; ++hidden) {
            assert(ui_dashboard_page_visible(mask, ui_dashboard_page_next(mask, hidden, 1)));
            assert(ui_dashboard_page_visible(mask, ui_dashboard_page_next(mask, hidden, -1)));
            assert(ui_dashboard_page_toggle(ui_dashboard_page_toggle(mask, hidden), hidden) == normalized);
        }
    }
    for (uint8_t selection = 0; selection < UI_DASHBOARD_CONTENT_PAGES; ++selection) {
        uint8_t first = ui_dashboard_settings_first(selection);
        assert(first <= selection && selection < first + UI_DASHBOARD_SETTINGS_ROWS);
        assert(first % UI_DASHBOARD_SETTINGS_ROWS == 0);
    }
    assert(ui_dashboard_settings_first(255) == 0);
    puts("Dashboard visibility: PASS (all 1024 stored masks, both directions)");
    return 0;
}
