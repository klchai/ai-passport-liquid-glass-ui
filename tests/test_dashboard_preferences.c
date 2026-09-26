#include "dashboard_preferences.h"
#include "ui_dashboard_pages.h"
#include "nvs.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static uint16_t disk, pending;
static bool exists, fail_init, fail_commit, change_during_commit;
static unsigned writes;
esp_err_t nvs_flash_init(void) { return fail_init ? 2 : ESP_OK; }
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle)
{
    assert(strcmp(name, "dashboard") == 0 && mode == NVS_READWRITE);
    *handle = 1;
    return ESP_OK;
}
esp_err_t nvs_get_u16(nvs_handle_t handle, const char *key, uint16_t *value)
{
    assert(handle == 1 && strcmp(key, "pages_v1") == 0);
    if (!exists) return ESP_ERR_NVS_NOT_FOUND;
    *value = disk;
    return ESP_OK;
}
esp_err_t nvs_set_u16(nvs_handle_t handle, const char *key, uint16_t value)
{
    assert(handle == 1 && strcmp(key, "pages_v1") == 0);
    pending = value;
    ++writes;
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t handle)
{
    assert(handle == 1);
    if (fail_commit) return 2;
    disk = pending;
    exists = true;
    if (change_during_commit) {
        change_during_commit = false;
        dashboard_preferences_set_pages(1u << PAGE_CLAUDE);
    }
    return ESP_OK;
}
void nvs_close(nvs_handle_t handle) { assert(handle == 1); }

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "unavailable") == 0) {
        fail_init = true;
        dashboard_preferences_init();
        assert(dashboard_preferences_pages() == UI_DASHBOARD_VISIBLE_ALL);
        dashboard_preferences_set_pages(0);
        dashboard_preferences_flush();
        assert(dashboard_preferences_pages() == UI_DASHBOARD_ALWAYS_VISIBLE_MASK &&
               writes == 0);
        assert(dashboard_preferences_status() == DASHBOARD_PREFS_ERROR);
        puts("Preferences unavailable: PASS (live changes, no erase)");
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "restart") == 0) {
        exists = true;
        disk = 0; // An intentional all-hidden mask must survive restart.
        dashboard_preferences_init();
        assert(dashboard_preferences_pages() == UI_DASHBOARD_ALWAYS_VISIBLE_MASK);
        assert(ui_dashboard_page_first(dashboard_preferences_pages()) == SHOWCASE_NAVIGATION);
        dashboard_preferences_flush();
        assert(writes == 0);
        puts("Preferences restart: PASS");
        return 0;
    }
    dashboard_preferences_init();
    assert(dashboard_preferences_pages() == UI_DASHBOARD_VISIBLE_ALL);
    dashboard_preferences_flush();
    assert(writes == 0);
    dashboard_preferences_set_pages(0);
    assert(dashboard_preferences_pages() == UI_DASHBOARD_ALWAYS_VISIBLE_MASK);
    assert(dashboard_preferences_status() == DASHBOARD_PREFS_PENDING);
    assert(writes == 0); // Button publication never performs storage I/O.
    dashboard_preferences_set_pages(1u << PAGE_KABOO);
    change_during_commit = true;
    dashboard_preferences_flush();
    assert(disk == ((1u << PAGE_KABOO) | UI_DASHBOARD_ALWAYS_VISIBLE_MASK));
    assert(dashboard_preferences_status() == DASHBOARD_PREFS_PENDING);
    dashboard_preferences_flush();
    assert(disk == ((1u << PAGE_CLAUDE) | UI_DASHBOARD_ALWAYS_VISIBLE_MASK));
    assert(dashboard_preferences_status() == DASHBOARD_PREFS_SAVED);
    dashboard_preferences_set_pages(0);
    fail_commit = true;
    dashboard_preferences_flush();
    assert(dashboard_preferences_status() == DASHBOARD_PREFS_ERROR);
    assert(disk == ((1u << PAGE_CLAUDE) | UI_DASHBOARD_ALWAYS_VISIBLE_MASK));
    fail_commit = false;
    dashboard_preferences_flush();
    assert(disk == UI_DASHBOARD_ALWAYS_VISIBLE_MASK &&
           dashboard_preferences_status() == DASHBOARD_PREFS_SAVED);
    unsigned before = writes;
    dashboard_preferences_flush();
    assert(writes == before);
    puts("Preferences save/race/retry: PASS");
    return 0;
}
