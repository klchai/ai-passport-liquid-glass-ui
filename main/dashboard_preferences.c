#include "dashboard_preferences.h"
#include "ui_dashboard_pages.h"

#include "nvs.h"
#include "nvs_flash.h"
#include <stdatomic.h>

static atomic_uint s_desired = UI_DASHBOARD_VISIBLE_ALL;
static atomic_uint s_saved = UI_DASHBOARD_VISIBLE_ALL;
static atomic_bool s_available;
static atomic_bool s_save_failed;
static nvs_handle_t s_handle;

void dashboard_preferences_init(void)
{
    // Never erase NVS to recover: other app services share this partition.
    if (nvs_flash_init() != ESP_OK ||
        nvs_open("dashboard", NVS_READWRITE, &s_handle) != ESP_OK) return;
    uint16_t mask = UI_DASHBOARD_VISIBLE_ALL;
    esp_err_t error = nvs_get_u16(s_handle, "pages_v1", &mask);
    if (error != ESP_OK && error != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(s_handle);
        return;
    }
    mask = (mask & UI_DASHBOARD_VISIBLE_ALL) |
           UI_DASHBOARD_ALWAYS_VISIBLE_MASK;
    atomic_store(&s_desired, mask);
    atomic_store(&s_saved, mask);
    atomic_store(&s_available, true);
}

uint16_t dashboard_preferences_pages(void)
{
    return (uint16_t)atomic_load(&s_desired);
}

void dashboard_preferences_set_pages(uint16_t mask)
{
    atomic_store(&s_desired, (mask & UI_DASHBOARD_VISIBLE_ALL) |
                             UI_DASHBOARD_ALWAYS_VISIBLE_MASK);
}

dashboard_preferences_status_t dashboard_preferences_status(void)
{
    if (!atomic_load(&s_available)) return DASHBOARD_PREFS_ERROR;
    if (atomic_load(&s_desired) == atomic_load(&s_saved)) return DASHBOARD_PREFS_SAVED;
    return atomic_load(&s_save_failed) ? DASHBOARD_PREFS_ERROR : DASHBOARD_PREFS_PENDING;
}

void dashboard_preferences_flush(void)
{
    if (!atomic_load(&s_available)) return;
    uint16_t mask = dashboard_preferences_pages();
    if (mask == atomic_load(&s_saved)) return;
    esp_err_t error = nvs_set_u16(s_handle, "pages_v1", mask);
    if (error == ESP_OK) error = nvs_commit(s_handle);
    atomic_store(&s_save_failed, error != ESP_OK);
    // If UI changed the selection during commit, it remains pending for the
    // next application tick. Never acknowledge a newer, unwritten mask.
    if (error == ESP_OK) atomic_store(&s_saved, mask);
}
