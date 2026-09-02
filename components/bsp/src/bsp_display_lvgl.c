// components/bsp/src/bsp_display_lvgl.c
// LVGL 接入单独成文件:不用 LVGL 的开发者删掉本文件 + idf_component.yml 里的两条依赖即可。
#include "bsp_display.h"
#include "bsp_pins.h"
#include "esp_lvgl_port.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

#include <inttypes.h>

#define LVGL_DRAW_BUFFER_LINES 20
#define PERF_REPORT_PERIOD_US  (1 * 1000 * 1000)

static const char *TAG = "bsp_lvgl";

static lv_display_t *s_disp;
static bsp_display_perf_snapshot_t s_last_perf;
static bool s_last_perf_ready;
static uint32_t s_perf_sample_sequence;
static portMUX_TYPE s_perf_snapshot_lock = portMUX_INITIALIZER_UNLOCKED;

typedef struct {
    int64_t refresh_start_us;
    int64_t render_start_us;
    int64_t flush_wait_start_us;
    int64_t report_start_us;
    uint64_t submit_us_sum;
    uint64_t render_us_sum;
    uint64_t flush_wait_us_sum;
    uint64_t pixels_sum;
    uint64_t invalidation_requests_sum;
    uint64_t invalidation_pixels_sum;
    uint32_t submit_us_max;
    uint32_t frames;
    uint32_t flushes;
    uint32_t pending_invalidation_requests;
    uint32_t pending_invalidation_pixels;
    uint32_t frame_render_us;
    uint32_t frame_flush_wait_us;
    uint32_t frame_pixels;
    uint32_t frame_invalidation_requests;
    uint32_t frame_invalidation_pixels;
    uint16_t frame_flushes;
    bool rendered;
    bool rendering;
} display_perf_t;

static display_perf_t s_perf;

static void perf_reset_frame(int64_t now_us)
{
    s_perf.refresh_start_us = now_us;
    s_perf.frame_render_us = 0;
    s_perf.frame_flush_wait_us = 0;
    s_perf.frame_pixels = 0;
    s_perf.frame_flushes = 0;
    s_perf.frame_invalidation_requests =
        s_perf.pending_invalidation_requests;
    s_perf.frame_invalidation_pixels = s_perf.pending_invalidation_pixels;
    s_perf.pending_invalidation_requests = 0;
    s_perf.pending_invalidation_pixels = 0;
    s_perf.rendered = false;
    s_perf.rendering = false;
}

static void perf_report(int64_t now_us)
{
    int64_t period_us = now_us - s_perf.report_start_us;
    if (period_us < PERF_REPORT_PERIOD_US) return;

    if (s_perf.frames > 0) {
        uint32_t update_fps_x10 = (uint32_t)(s_perf.frames * 10000000ULL /
                                             (uint64_t)period_us);
        uint32_t submit_avg_x10 = (uint32_t)(s_perf.submit_us_sum * 10ULL /
                                             s_perf.frames / 1000ULL);
        uint64_t cpu_render_us_sum = s_perf.render_us_sum >=
                                     s_perf.flush_wait_us_sum
                                     ? s_perf.render_us_sum -
                                       s_perf.flush_wait_us_sum
                                     : 0;
        uint32_t render_avg_x10 = (uint32_t)(cpu_render_us_sum * 10ULL /
                                             s_perf.frames / 1000ULL);
        uint32_t wait_avg_x10 = (uint32_t)(s_perf.flush_wait_us_sum * 10ULL /
                                           s_perf.frames / 1000ULL);
        uint32_t wire_avg_x10 = (uint32_t)(s_perf.pixels_sum * 16ULL *
                                           10000000ULL /
                                           BSP_LCD_PCLK_HZ / s_perf.frames /
                                           1000ULL);
        uint32_t pixels_avg = (uint32_t)(s_perf.pixels_sum / s_perf.frames);
        uint32_t flushes_x10 = s_perf.flushes * 10U / s_perf.frames;
        uint32_t invalidations_x10 = (uint32_t)(
            s_perf.invalidation_requests_sum * 10ULL / s_perf.frames);
        uint32_t invalidated_pixels_avg = (uint32_t)(
            s_perf.invalidation_pixels_sum / s_perf.frames);
        size_t dma_free = heap_caps_get_free_size(MALLOC_CAP_DMA |
                                                   MALLOC_CAP_INTERNAL);
        size_t dma_largest = heap_caps_get_largest_free_block(MALLOC_CAP_DMA |
                                                               MALLOC_CAP_INTERNAL);

        bsp_display_perf_snapshot_t snapshot = {
            .sample_sequence = ++s_perf_sample_sequence,
            .sample_duration_ms = (uint32_t)(period_us / 1000),
            .updates_per_second_x10 = update_fps_x10,
            .submit_avg_ms_x10 = submit_avg_x10,
            .submit_max_ms_x10 = s_perf.submit_us_max / 100,
            .render_avg_ms_x10 = render_avg_x10,
            .flush_wait_avg_ms_x10 = wait_avg_x10,
            .pixels_per_update = pixels_avg,
            .invalidation_requests_x10 = invalidations_x10,
            .invalidated_pixels_per_update = invalidated_pixels_avg,
            .wire_min_ms_x10 = wire_avg_x10,
            .dma_free_bytes = (uint32_t)dma_free,
            .dma_largest_block_bytes = (uint32_t)dma_largest,
        };
        portENTER_CRITICAL(&s_perf_snapshot_lock);
        s_last_perf = snapshot;
        s_last_perf_ready = true;
        portEXIT_CRITICAL(&s_perf_snapshot_lock);

        // submit 不包含最后一笔异步 DMA；wire-min 是纯像素线速下限，不含命令开销。
        ESP_LOGI(TAG,
                 "LCD perf %" PRIu32 ".%" PRIu32 " upd/s | submit avg=%" PRIu32
                 ".%" PRIu32 " max=%" PRIu32 ".%" PRIu32 "ms | render=%" PRIu32
                 ".%" PRIu32 "ms wait=%" PRIu32 ".%" PRIu32 "ms | px=%" PRIu32
                 " flush=%" PRIu32 ".%" PRIu32 " | inv-req=%" PRIu32 ".%" PRIu32
                 " req-px=%" PRIu32 " | wire-min=%" PRIu32 ".%" PRIu32
                 "ms @%dMHz | DMA heap=%u/%u",
                 update_fps_x10 / 10, update_fps_x10 % 10,
                 submit_avg_x10 / 10, submit_avg_x10 % 10,
                 s_perf.submit_us_max / 1000, (s_perf.submit_us_max / 100) % 10,
                 render_avg_x10 / 10, render_avg_x10 % 10,
                 wait_avg_x10 / 10, wait_avg_x10 % 10,
                 pixels_avg, flushes_x10 / 10, flushes_x10 % 10,
                 invalidations_x10 / 10, invalidations_x10 % 10,
                 invalidated_pixels_avg,
                 wire_avg_x10 / 10, wire_avg_x10 % 10,
                 BSP_LCD_PCLK_HZ / 1000000,
                 (unsigned)dma_free, (unsigned)dma_largest);
    }

    s_perf.report_start_us = now_us;
    s_perf.submit_us_sum = 0;
    s_perf.render_us_sum = 0;
    s_perf.flush_wait_us_sum = 0;
    s_perf.pixels_sum = 0;
    s_perf.invalidation_requests_sum = 0;
    s_perf.invalidation_pixels_sum = 0;
    s_perf.submit_us_max = 0;
    s_perf.frames = 0;
    s_perf.flushes = 0;
}

static void perf_event_cb(lv_event_t *event)
{
    int64_t now_us = esp_timer_get_time();

    switch (lv_event_get_code(event)) {
    case LV_EVENT_INVALIDATE_AREA: {
        // Count application invalidations before LVGL's containment pass.
        // get_max_row() emits the same event while rendering to round draw
        // buffers; excluding that phase keeps the metric actionable.
        const lv_area_t *area = lv_event_get_param(event);
        if (!s_perf.rendering && area) {
            s_perf.pending_invalidation_requests++;
            s_perf.pending_invalidation_pixels +=
                (uint32_t)lv_area_get_size(area);
        }
        break;
    }
    case LV_EVENT_REFR_START:
        perf_reset_frame(now_us);
        break;
    case LV_EVENT_RENDER_START:
        s_perf.rendered = true;
        s_perf.rendering = true;
        s_perf.render_start_us = now_us;
        break;
    case LV_EVENT_RENDER_READY:
        s_perf.frame_render_us += (uint32_t)(now_us - s_perf.render_start_us);
        s_perf.rendering = false;
        break;
    case LV_EVENT_FLUSH_START: {
        const lv_area_t *area = lv_event_get_param(event);
        if (area) {
            s_perf.frame_pixels += (uint32_t)lv_area_get_size(area);
            s_perf.frame_flushes++;
        }
        break;
    }
    case LV_EVENT_FLUSH_WAIT_START:
        s_perf.flush_wait_start_us = now_us;
        break;
    case LV_EVENT_FLUSH_WAIT_FINISH:
        s_perf.frame_flush_wait_us +=
            (uint32_t)(now_us - s_perf.flush_wait_start_us);
        break;
    case LV_EVENT_REFR_READY:
        if (s_perf.rendered && s_perf.frame_pixels > 0) {
            uint32_t submit_us = (uint32_t)(now_us - s_perf.refresh_start_us);
            s_perf.submit_us_sum += submit_us;
            s_perf.render_us_sum += s_perf.frame_render_us;
            s_perf.flush_wait_us_sum += s_perf.frame_flush_wait_us;
            s_perf.pixels_sum += s_perf.frame_pixels;
            s_perf.flushes += s_perf.frame_flushes;
            s_perf.invalidation_requests_sum +=
                s_perf.frame_invalidation_requests;
            s_perf.invalidation_pixels_sum +=
                s_perf.frame_invalidation_pixels;
            if (submit_us > s_perf.submit_us_max) s_perf.submit_us_max = submit_us;
            s_perf.frames++;
        }
        perf_report(now_us);
        break;
    default:
        break;
    }
}

lv_display_t *bsp_lvgl_init(void) {
    if (s_disp) return s_disp;
    if (!bsp_display_panel()) {
        ESP_LOGE(TAG, "请先成功调用 bsp_display_init()");
        return NULL;
    }

    const lvgl_port_cfg_t pc = ESP_LVGL_PORT_INIT_CONFIG();
    if (lvgl_port_init(&pc) != ESP_OK) {
        ESP_LOGE(TAG, "lvgl_port_init 失败");
        return NULL;
    }

    const lvgl_port_display_cfg_t dc = {
        .panel_handle = bsp_display_panel(),
        .io_handle    = bsp_display_io(),
        // ⚠ C3 无 PSRAM,DMA 只能用内部 RAM。
        // 两个 20 行缓冲共 19.2KB，允许 CPU 合成下一条带时并行发送上一条带；
        // 不使用曾导致 I2S 等外设 NO_MEM 的 40 行双缓冲(≈38.4KB)。
        .buffer_size   = (uint32_t)BSP_LCD_W * LVGL_DRAW_BUFFER_LINES,
        .double_buffer = true,
        .hres = BSP_LCD_W, .vres = BSP_LCD_H,
        // 旋转/镜像必须在这里配:esp_lvgl_port 注册显示时会重新下发 MADCTL,
        // 覆盖 bsp_display.c 里 esp_lcd_panel_mirror() 的设置。
        .rotation = { .swap_xy = false, .mirror_x = false, .mirror_y = false },
        // swap_bytes:LVGL 输出小端 RGB565,ST7789 走 SPI 要大端 → 需交换高低字节。
        .flags = { .buff_dma = true, .swap_bytes = true },
    };
    s_disp = lvgl_port_add_disp(&dc);
    if (!s_disp) { ESP_LOGE(TAG, "lvgl_port_add_disp 失败"); return NULL; }

    s_perf.report_start_us = esp_timer_get_time();
    lv_display_add_event_cb(s_disp, perf_event_cb, LV_EVENT_ALL, NULL);

    ESP_LOGI(TAG, "LVGL 就绪 buffer=%d lines x%d (%u bytes), full-frame wire-min=%ums",
             LVGL_DRAW_BUFFER_LINES, dc.double_buffer ? 2 : 1,
             (unsigned)(dc.buffer_size * 2U *
                        (dc.double_buffer ? 2U : 1U)),
             (unsigned)((uint64_t)BSP_LCD_W * BSP_LCD_H * 16ULL * 1000ULL /
                        BSP_LCD_PCLK_HZ));
    return s_disp;
}

bool bsp_lvgl_lock(int timeout_ms) { return lvgl_port_lock(timeout_ms); }
void bsp_lvgl_unlock(void)         { lvgl_port_unlock(); }

bool bsp_display_perf_get(bsp_display_perf_snapshot_t *snapshot)
{
    if (!snapshot) return false;
    portENTER_CRITICAL(&s_perf_snapshot_lock);
    bool ready = s_last_perf_ready;
    if (ready) *snapshot = s_last_perf;
    portEXIT_CRITICAL(&s_perf_snapshot_lock);
    return ready;
}
