// main/main.c —— 十页应用固件入口。
//
// 与上游 demo 版本的关键差别：本固件没有 demo 菜单。开机直接进十页轮播
// （八个 Liquid Glass 展示场景 + Kaboo token 用量 + Claude 限额），UP 翻页。
//
// BLE 是应用级常驻服务：在这里启动一次，页面切换不参与其生命周期。
// 整个固件中只有 usage_link.c 允许调用 nimble_port_init()。
#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "dashboard.h"
#include "fap_screenshot.h"
#include "usage_link.h"

#include "esp_log.h"
#include "lvgl.h"

static const char *TAG = "main";

// 按键回调运行在 button 组件的任务里，操作 LVGL 必须加锁。
static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user)
{
    (void)user;
    if (!bsp_lvgl_lock(500)) return;
    dashboard_key(btn, ev);
    bsp_lvgl_unlock();
}

void app_main(void)
{
    ESP_LOGI(TAG, "AI Passport 看板启动");

    bsp_i2c_init();
    bsp_i2c_scan();

    // 屏幕是本固件的唯一载体，失败就没有看板可言。
    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "显示/LVGL 初始化失败。"
                      "检查 SPI 接线(MOSI=%d SCLK=%d CS=%d DC=%d BL=%d)",
                 BSP_LCD_MOSI, BSP_LCD_SCLK, BSP_LCD_CS, BSP_LCD_DC, BSP_LCD_BL);
        return;
    }
    bsp_display_backlight(100);

    bool buttons_ok = (bsp_button_init(on_key, NULL) == ESP_OK);
    if (!buttons_ok) ESP_LOGE(TAG, "按键初始化失败，无法翻页");

    // 电量在这里读一次再交给 UI：I2C 读取留在 app_main，不进按键回调或
    // LVGL timer。电量计初始化失败就显示 "--%"，不阻塞启动。
    int battery_soc = (bsp_battery_init() == ESP_OK) ? bsp_battery_soc() : -1;
    dashboard_set_battery(battery_soc);

    // BLE 失败不阻塞 UI：看板会一直显示"等待 Mac"，仍可翻页。
    esp_err_t ble_err = usage_link_start();
    if (ble_err != ESP_OK) {
        ESP_LOGE(TAG, "BLE 链路启动失败: %s", esp_err_to_name(ble_err));
    }

    if (bsp_lvgl_lock(1000)) {
        dashboard_enter();
        bsp_lvgl_unlock();
    }
    fap_screenshot_start();

    ESP_LOGI(TAG, "就绪:Display=1 Button=%d BLE=%d",
             buttons_ok, ble_err == ESP_OK);
}
