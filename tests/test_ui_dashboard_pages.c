#include "ui_dashboard_pages.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    assert(ui_dashboard_page_first(UI_DASHBOARD_VISIBLE_ALL) == SHOWCASE_NAVIGATION);
    assert(ui_dashboard_page_first(0) == SHOWCASE_NAVIGATION);
    assert(ui_dashboard_page_first(1u << PAGE_CLAUDE) == SHOWCASE_NAVIGATION);
    assert(!ui_dashboard_page_visible(0xffff, SHOWCASE_PAGE_COUNT));
    assert(!ui_dashboard_page_visible(0xffff, (showcase_page_t)-1));
    assert(ui_dashboard_page_toggle(0, PAGE_SETTINGS) == UI_DASHBOARD_ALWAYS_VISIBLE_MASK);
    assert(ui_dashboard_page_toggle(0xffff, PAGE_SETTINGS) == UI_DASHBOARD_VISIBLE_ALL);
    assert(ui_dashboard_page_toggle(0, SHOWCASE_NAVIGATION) ==
           UI_DASHBOARD_ALWAYS_VISIBLE_MASK);
    for (uint16_t mask = 0; mask <= UI_DASHBOARD_VISIBLE_ALL; ++mask) {
        uint16_t normalized = mask | UI_DASHBOARD_ALWAYS_VISIBLE_MASK;
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
    for (uint8_t selection = 0; selection < PAGE_SETTINGS; ++selection) {
        uint8_t first = ui_dashboard_settings_first(selection);
        assert(first <= selection && selection < first + UI_DASHBOARD_SETTINGS_ROWS);
        assert(first % UI_DASHBOARD_SETTINGS_ROWS == 0);
    }
    assert(ui_dashboard_settings_first(255) == 0);
    puts("Dashboard visibility: PASS (all 1024 masks, both directions)");
    return 0;
}
