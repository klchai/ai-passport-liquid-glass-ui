#include "time_sync.h"

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <stdatomic.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#define TIME_SYNC_NAMESPACE       "clock"
#define TIME_SYNC_UNIX_KEY        "unix_v1"
#define TIME_SYNC_TZ_KEY          "tz_v1"
#define TIME_SYNC_MIN_UNIX        1577836800u // 2020-01-01T00:00:00Z
#define TIME_SYNC_SAVE_INTERVAL_US (60LL * 1000000LL)
#define TIME_SYNC_NTP_INTERVAL_MS (60u * 60u * 1000u)
#define TIME_SYNC_DEFAULT_TZ_MINUTES 480 // Asia/Shanghai until a host reports one

static atomic_bool s_initialized;
static atomic_bool s_computer_pending;
static atomic_bool s_ntp_pending;
static atomic_uint_fast32_t s_computer_unix;
static atomic_int s_computer_tz;
static atomic_int s_tz_offset_minutes;

static nvs_handle_t s_nvs;
static bool s_nvs_available;
static int64_t s_last_save_us;
static bool s_sntp_started;

static void set_system_time(uint32_t unix_time);

static bool plausible_unix(uint32_t value)
{
    return value >= TIME_SYNC_MIN_UNIX;
}

static int build_month(const char *text)
{
    static const char *const names[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
    };
    for (int month = 0; month < 12; ++month) {
        if (strncmp(text, names[month], 3) == 0) return month + 1;
    }
    return 0;
}

static int64_t days_from_civil(int year, unsigned month, unsigned day)
{
    year -= month <= 2;
    int era = (year >= 0 ? year : year - 399) / 400;
    unsigned yoe = (unsigned)(year - era * 400);
    int month_prime = (int)month + (month > 2 ? -3 : 9);
    unsigned doy = (153u * (unsigned)month_prime + 2u) / 5u + day - 1u;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

static bool seed_from_build_time(void)
{
    // __DATE__ and __TIME__ are a useful first-boot anchor when a board is
    // flashed and left offline. A later BLE/NTP sync replaces this value.
    const char *date = __DATE__;
    const char *clock = __TIME__;
    int month = build_month(date);
    int day = date[4] == ' ' ? date[5] - '0'
                             : (date[4] - '0') * 10 + date[5] - '0';
    int year = (date[7] - '0') * 1000 + (date[8] - '0') * 100 +
               (date[9] - '0') * 10 + date[10] - '0';
    int hour = (clock[0] - '0') * 10 + clock[1] - '0';
    int minute = (clock[3] - '0') * 10 + clock[4] - '0';
    int second = (clock[6] - '0') * 10 + clock[7] - '0';
    if (!month || day < 1 || day > 31 || year < 2020 ||
        hour > 23 || minute > 59 || second > 59) return false;

    int64_t local_seconds = days_from_civil(year, (unsigned)month,
                                            (unsigned)day) * 86400 +
                            hour * 3600 + minute * 60 + second;
    int64_t utc_seconds = local_seconds -
                          (int64_t)TIME_SYNC_DEFAULT_TZ_MINUTES * 60;
    if (utc_seconds < TIME_SYNC_MIN_UNIX || utc_seconds > UINT32_MAX) return false;
    set_system_time((uint32_t)utc_seconds);
    return true;
}

static void set_system_time(uint32_t unix_time)
{
    struct timeval tv = {
        .tv_sec = (time_t)unix_time,
        .tv_usec = 0,
    };
    (void)settimeofday(&tv, NULL);
}

static void persist_clock(uint32_t unix_time)
{
    if (!s_nvs_available || !plausible_unix(unix_time)) return;
    int16_t tz = (int16_t)atomic_load(&s_tz_offset_minutes);
    if (nvs_set_u32(s_nvs, TIME_SYNC_UNIX_KEY, unix_time) != ESP_OK ||
        nvs_set_i16(s_nvs, TIME_SYNC_TZ_KEY, tz) != ESP_OK ||
        nvs_commit(s_nvs) != ESP_OK) {
        return;
    }
    s_last_save_us = esp_timer_get_time();
}

static void sntp_time_synced(struct timeval *tv)
{
    // SNTP has already applied tv to the system clock. Defer the flash write
    // to the application task so the lwIP/SNTP callback stays non-blocking.
    if (tv && tv->tv_sec >= (time_t)TIME_SYNC_MIN_UNIX) {
        atomic_store(&s_ntp_pending, true);
    }
}

static void wifi_time_event(void *arg, esp_event_base_t base,
                            int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;
    if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        (void)esp_wifi_connect();
    } else if (base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP &&
               s_sntp_started) {
        // A fresh DHCP lease is the point at which DNS and UDP are usable.
        // Restarting here makes a newly connected Wi-Fi network sync promptly
        // instead of waiting for the hourly poll interval.
        (void)esp_sntp_restart();
    }
}

static void start_saved_wifi(void)
{
    // Network credentials are optional. If the Wi-Fi page or another setup
    // tool has stored an SSID, connect as a station; otherwise leave the radio
    // off and keep the BLE/computer clock path lightweight.
    esp_netif_t *sta = esp_netif_create_default_wifi_sta();
    if (!sta) return;

    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&config) != ESP_OK) {
        esp_netif_destroy_default_wifi(sta);
        return;
    }
    wifi_config_t saved = { 0 };
    if (esp_wifi_set_storage(WIFI_STORAGE_FLASH) != ESP_OK ||
        esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK ||
        esp_wifi_get_config(WIFI_IF_STA, &saved) != ESP_OK ||
        saved.sta.ssid[0] == '\0') {
        (void)esp_wifi_deinit();
        esp_netif_destroy_default_wifi(sta);
        return;
    }

    if (esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                   wifi_time_event, NULL) != ESP_OK ||
        esp_wifi_start() != ESP_OK) {
        (void)esp_wifi_deinit();
        esp_netif_destroy_default_wifi(sta);
        return;
    }
}

void time_sync_init(void)
{
    if (atomic_exchange(&s_initialized, true)) return;

    atomic_store(&s_computer_pending, false);
    atomic_store(&s_ntp_pending, false);
    atomic_store(&s_tz_offset_minutes, TIME_SYNC_DEFAULT_TZ_MINUTES);
    s_last_save_us = esp_timer_get_time();

    // Keep the last known clock when booting without BLE or Wi-Fi. Never erase
    // NVS here: the partition is shared with the rest of the application.
    if (nvs_flash_init() == ESP_OK &&
        nvs_open(TIME_SYNC_NAMESPACE, NVS_READWRITE, &s_nvs) == ESP_OK) {
        s_nvs_available = true;
        uint32_t unix_time = 0;
        int16_t tz = 0;
        if (nvs_get_u32(s_nvs, TIME_SYNC_UNIX_KEY, &unix_time) == ESP_OK &&
            plausible_unix(unix_time)) {
            set_system_time(unix_time);
        } else {
            (void)seed_from_build_time();
        }
        if (nvs_get_i16(s_nvs, TIME_SYNC_TZ_KEY, &tz) == ESP_OK &&
            tz >= -24 * 60 && tz <= 24 * 60) {
            atomic_store(&s_tz_offset_minutes, tz);
        }
    } else {
        (void)seed_from_build_time();
    }

    // Bring up the TCP/IP core before SNTP creates its timer/callback path.
    // No station credentials are required here; if Wi-Fi is added or started
    // by another service later, the already-running SNTP client can sync.
    esp_err_t netif_error = esp_netif_init();
    if (netif_error != ESP_OK && netif_error != ESP_ERR_INVALID_STATE) return;

    // Use credentials already stored by a Wi-Fi setup flow when available.
    // With no SSID this is a no-op and does not leave the radio running.
    esp_err_t event_error = esp_event_loop_create_default();
    if (event_error == ESP_OK || event_error == ESP_ERR_INVALID_STATE) {
        // Also watch for a Wi-Fi service that is configured after this module
        // starts; that path should trigger an immediate SNTP retry as well.
        (void)esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                         wifi_time_event, NULL);
        start_saved_wifi();
    }

    // SNTP retries while the interface is offline and starts succeeding as
    // soon as Wi-Fi provides a route. The computer timestamp remains the
    // immediate synchronization path over the existing BLE connection.
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
    esp_sntp_setservername(0, "ntp1.aliyun.com");
    esp_sntp_set_sync_interval(TIME_SYNC_NTP_INTERVAL_MS);
    esp_sntp_set_time_sync_notification_cb(sntp_time_synced);
    esp_sntp_init();
    s_sntp_started = true;
}

void time_sync_offer_computer(uint32_t unix_time, int16_t tz_offset_minutes)
{
    if (!plausible_unix(unix_time) ||
        tz_offset_minutes < -24 * 60 || tz_offset_minutes > 24 * 60) {
        return;
    }
    atomic_store(&s_computer_unix, unix_time);
    atomic_store(&s_computer_tz, tz_offset_minutes);
    atomic_store(&s_computer_pending, true);
}

void time_sync_poll(void)
{
    if (!atomic_load(&s_initialized)) return;

    bool force_save = atomic_exchange(&s_ntp_pending, false);
    if (atomic_exchange(&s_computer_pending, false)) {
        uint32_t unix_time = (uint32_t)atomic_load(&s_computer_unix);
        int tz = atomic_load(&s_computer_tz);
        atomic_store(&s_tz_offset_minutes, tz);
        set_system_time(unix_time);
        force_save = true;
    }

    time_t now = time(NULL);
    if (now < (time_t)TIME_SYNC_MIN_UNIX) return;
    int64_t now_us = esp_timer_get_time();
    if (force_save || now_us - s_last_save_us >= TIME_SYNC_SAVE_INTERVAL_US) {
        persist_clock((uint32_t)now);
    }
}

bool time_sync_get_wall_clock(time_sync_wall_clock_t *out)
{
    if (!out) return false;
    time_t now = time(NULL);
    if (now < (time_t)TIME_SYNC_MIN_UNIX) return false;

    int64_t local_seconds = (int64_t)now +
                            (int64_t)atomic_load(&s_tz_offset_minutes) * 60;
    if (local_seconds < 0) return false;
    time_t adjusted = (time_t)local_seconds;
    struct tm local_tm;
    if (!gmtime_r(&adjusted, &local_tm)) return false;

    out->hour = local_tm.tm_hour;
    out->minute = local_tm.tm_min;
    out->second = local_tm.tm_sec;
    out->year = local_tm.tm_year + 1900;
    out->month = local_tm.tm_mon + 1;
    out->day = local_tm.tm_mday;
    out->weekday = local_tm.tm_wday;
    return true;
}
