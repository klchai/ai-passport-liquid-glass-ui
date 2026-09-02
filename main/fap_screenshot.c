#include "fap_screenshot.h"
#include "bsp_display.h"
#include "bsp_pins.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "src/core/lv_obj_draw_private.h"
#include "src/core/lv_refr_private.h"
#include "src/display/lv_display_private.h"
#include "src/draw/lv_draw_private.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#if LVGL_VERSION_MAJOR != 9 || LVGL_VERSION_MINOR != 5
#error "FAP screenshot renderer is pinned to LVGL 9.5 private rendering APIs"
#endif

static const char *TAG = "fap_shot";

#define SCREENSHOT_COMMAND      "FAP_SCREENSHOT_V1"
#define SCREENSHOT_COMMAND_SIZE (sizeof(SCREENSHOT_COMMAND) - 1)
#define SCREENSHOT_ROWS         8
#define USB_WRITE_SLICE         512

static TaskHandle_t s_task;

static bool usb_write_all(const void *buffer, size_t length)
{
    const uint8_t *cursor = buffer;
    int empty_writes = 0;
    while (length > 0) {
        size_t requested = length < USB_WRITE_SLICE ? length : USB_WRITE_SLICE;
        int written = usb_serial_jtag_write_bytes(cursor, requested, pdMS_TO_TICKS(500));
        if (written > 0) {
            cursor += written;
            length -= (size_t)written;
            empty_writes = 0;
            continue;
        }
        if (++empty_writes >= 3) return false;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return true;
}

// 将当前活动 screen 的指定行渲染到小缓冲。私有 API 耦合被隔离在本文件并由版本门禁保护。
static void render_rows(lv_obj_t *screen, lv_draw_buf_t *buffer,
                        int32_t y_start, int32_t row_count)
{
    const int32_t width = buffer->header.w;
    const lv_area_t area = {
        .x1 = 0,
        .y1 = y_start,
        .x2 = width - 1,
        .y2 = y_start + row_count - 1,
    };
    lv_draw_buf_clear(buffer, NULL);

    lv_layer_t layer;
    lv_layer_init(&layer);
    layer.draw_buf = buffer;
    layer.buf_area = area;
    layer.color_format = LV_COLOR_FORMAT_RGB565;
    layer._clip_area = area;
    layer.phy_clip_area = area;
    lv_draw_unit_send_event(NULL, LV_EVENT_CHILD_CREATED, &layer);

    lv_display_t *previous_display = lv_refr_get_disp_refreshing();
    lv_display_t *display = lv_obj_get_display(screen);
    lv_layer_t *previous_layer = display->layer_head;
    display->layer_head = &layer;
    lv_refr_set_disp_refreshing(display);

    // A screenshot must reproduce the final composited screen, not only the
    // first opaque object found in this row band. The top-object shortcut can
    // legally choose a full-width content canvas and then omit later siblings
    // such as rows, focus lenses, headers, and battery text. Rendering from the
    // screen root is slower but deterministic and this path is diagnostic-only.
    lv_obj_redraw(&layer, screen);

    layer.all_tasks_added = true;
    while (layer.draw_task_head) {
        lv_draw_dispatch_wait_for_request();
        lv_draw_dispatch();
    }

    display->layer_head = previous_layer;
    lv_refr_set_disp_refreshing(previous_display);
    lv_draw_unit_send_event(NULL, LV_EVENT_SCREEN_LOAD_START, &layer);
    lv_draw_unit_send_event(NULL, LV_EVENT_CHILD_DELETED, &layer);
}

static void send_screenshot(void)
{
    const uint32_t width = BSP_LCD_W;
    const uint32_t height = BSP_LCD_H;
    const uint32_t row_bytes = width * 2;
    const uint32_t payload_size = row_bytes * height;

    if (!bsp_lvgl_lock(1000)) {
        ESP_LOGW(TAG, "LVGL lock timed out");
        return;
    }
    lv_obj_t *screen = lv_screen_active();
    // A capture is allowed to arrive immediately after a page callback changed
    // intrinsic label text. Resolve pending coordinates before the first row so
    // every band observes one coherent header/footer layout.
    lv_obj_update_layout(screen);

    const size_t buffer_size = (size_t)row_bytes * SCREENSHOT_ROWS;
    void *pixel_storage = heap_caps_malloc(buffer_size, MALLOC_CAP_8BIT);
    lv_draw_buf_t draw_buffer;
    if (!pixel_storage ||
        lv_draw_buf_init(&draw_buffer, width, SCREENSHOT_ROWS,
                         LV_COLOR_FORMAT_RGB565, row_bytes,
                         pixel_storage, buffer_size) != LV_RESULT_OK) {
        heap_caps_free(pixel_storage);
        bsp_lvgl_unlock();
        ESP_LOGE(TAG, "unable to allocate %u-row capture buffer", SCREENSHOT_ROWS);
        return;
    }
    lv_draw_buf_set_flag(&draw_buffer, LV_IMAGE_FLAGS_MODIFIABLE);
    lv_draw_buf_t *buffer = &draw_buffer;

    esp_log_level_t previous_log_level = esp_log_level_get("*");
    esp_log_level_set("*", ESP_LOG_NONE);
    char header[64];
    int header_size = snprintf(header, sizeof(header),
        SCREENSHOT_COMMAND " %lu %lu RGB565LE %lu\n",
        (unsigned long)width, (unsigned long)height, (unsigned long)payload_size);
    bool sent = header_size > 0 && usb_write_all(header, (size_t)header_size);

    for (uint32_t y = 0; sent && y < height; y += SCREENSHOT_ROWS) {
        uint32_t rows = y + SCREENSHOT_ROWS <= height ? SCREENSHOT_ROWS : height - y;
        render_rows(screen, buffer, (int32_t)y, (int32_t)rows);
        const uint8_t *data = buffer->data;
        for (uint32_t row = 0; sent && row < rows; ++row) {
            sent = usb_write_all(data + (size_t)row * buffer->header.stride, row_bytes);
        }
    }

    esp_log_level_set("*", previous_log_level);
    heap_caps_free(pixel_storage);
    bsp_lvgl_unlock();
    if (sent) ESP_LOGI(TAG, "screenshot sent: %lux%lu", (unsigned long)width,
                       (unsigned long)height);
    else ESP_LOGW(TAG, "screenshot transfer aborted");
}

static void screenshot_task(void *argument)
{
    (void)argument;
    size_t matched = 0;
    while (true) {
        uint8_t input[32];
        int read_size = usb_serial_jtag_read_bytes(input, sizeof(input), pdMS_TO_TICKS(50));
        if (read_size < 0) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        if (read_size == 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        for (int i = 0; i < read_size; ++i) {
            char current = (char)input[i];
            if (current == SCREENSHOT_COMMAND[matched]) {
                if (++matched == SCREENSHOT_COMMAND_SIZE) {
                    matched = 0;
                    send_screenshot();
                }
            } else {
                matched = current == SCREENSHOT_COMMAND[0] ? 1 : 0;
            }
        }
    }
}

void fap_screenshot_start(void)
{
    if (s_task) return;
    if (!usb_serial_jtag_is_driver_installed()) {
        usb_serial_jtag_driver_config_t config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
        config.rx_buffer_size = 256;
        config.tx_buffer_size = 1024;
        esp_err_t error = usb_serial_jtag_driver_install(&config);
        if (error != ESP_OK) {
            ESP_LOGE(TAG, "USB Serial/JTAG driver init failed: %s", esp_err_to_name(error));
            return;
        }
    }
    usb_serial_jtag_vfs_use_driver();
    if (xTaskCreate(screenshot_task, "fap_shot", 8192, NULL, 3, &s_task) != pdPASS) {
        s_task = NULL;
        ESP_LOGE(TAG, "screenshot task creation failed");
        return;
    }
    ESP_LOGI(TAG, "FAP_SCREENSHOT_V1 ready (%u-row buffer)", SCREENSHOT_ROWS);
}
