// main/dashboard.h —— 十页应用的入口（实现在 showcase_scenes.c）。
//
// 八个 Liquid Glass 展示场景 + Kaboo token 用量 + Claude 限额，共用一个
// runtime、一块屏幕、一套 header/footer 与一个 200ms 主定时器。
// 实时数据由 usage_link.c 经 BLE 接收，本模块只渲染。
#pragma once

#include "bsp_button.h"

// 电量百分比，-1 表示不可用（显示 "--%"）。在 enter() 之前由 app_main 读一次
// 传入：I2C 读取不放进 LVGL timer 或按键回调。
void dashboard_set_battery(int soc);

// 建屏并载入。调用方必须持有 LVGL 锁。
void dashboard_enter(void);

// 停 timer、删屏。不影响 BLE 链路（那是应用级常驻服务）。
void dashboard_exit(void);

// UP = 下一页，UP 双击 = 上一页；DOWN / OK 在页内有各自语义。
void dashboard_key(bsp_btn_t button, bsp_btn_ev_t event);
