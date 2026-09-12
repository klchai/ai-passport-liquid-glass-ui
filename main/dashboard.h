// main/dashboard.h —— 看板应用的入口（实现在 showcase_scenes.c）。
//
// 八个展示场景 + Kaboo 用量 + Claude 限额 + Settings，共用一个
// runtime、一块屏幕、一套 header/footer 与一个 200ms 主定时器。
// 实时数据由 usage_link.c 经 BLE 接收，本模块只渲染。
#pragma once

#include "bsp_button.h"

// 电量百分比，-1 表示不可用（显示 "--%"）。可从后台任务安全发布，
// LVGL timer 读取原子快照；I2C 不在 LVGL timer 或按键回调执行。
void dashboard_set_battery(int soc);

// 建屏并载入。调用方必须持有 LVGL 锁。
void dashboard_enter(void);

// 停 timer、删屏。不影响 BLE 链路（那是应用级常驻服务）。
void dashboard_exit(void);

// 浏览：UP / DOWN 上下翻页，OK 主操作；长按 OK 进入/退出页内模式。
void dashboard_key(bsp_btn_t button, bsp_btn_ev_t event);
