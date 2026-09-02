<p align="right">
  <strong>简体中文</strong> · <a href="PRODUCT.md">English</a>
</p>

# 产品定义

<!-- impeccable:product-schema 1 -->

## 平台

面向 ESP32-C3 AI Passport 的嵌入式固件。

## 用户

- 在真机上安装并评审社区固件的 AI Passport 玩家。
- 需要复用硬件原生 UI 原语，而不是复制某个展示页面的开源固件开发者。
- 需要让设计工具与 RGB565 Runtime 使用同一套语义的设计师。

## 产品目标

为 240 × 320 AI Passport 提供一套原创、开源、精致的玻璃风格 UI 基建。
同一套系统应贯通设计规范、Tokens、Motion、Components、Runtime 渲染、产品
Patterns、真机 Showcase 与可量化的性能测试。

## 差异化定位

它同时是硬件专用设计系统、组件库、渲染 Runtime 与 Benchmark 套件。每个视觉
原语都公开交互状态和显示成本，使开发者能够按自己的动画因地制宜，而不继承 Demo
私有布局。

## 使用闭环

主要评审闭环为：设计规范 → 固件构建 → USB 分段刷写 → 实体按键交互 → 串口性能
遥测与屏幕回采。设备输入为 UP、DOWN、OK 三键，而不是触摸。

## 能力与约束

- ESP32-C3、8 MB Flash、无 PSRAM、ESP-IDF 5.5.3。
- ST7789P3 240 × 320 RGB565 SPI 屏幕，无可读 framebuffer，也未引出 TE 信号。
- LVGL、显示 DMA、音频、网络与任务共享内部 RAM。
- 必须保留仓库定义的永久 Recovery 与小程序 BLE 安装兼容契约。
- 可复用硬件能力放入 `components/bsp`；UI 状态、动效、组件、Patterns 与 Demo
  放入 `main`。

## 品牌承诺

- 视觉语言研究现代 Apple 界面的交互与材质原则，但所有资源和实现必须原创且适配
  本硬件。
- 内容位于不透明基础层；玻璃仅服务于导航、控件、焦点、菜单、瞬态反馈，以及明确
  标注的材质实验页。
- Menu 与 Popover 必须从触发控件原位形变，不得作为无空间来源的矩形凭空出现。
- 不嵌入、不再分发 Apple UI Kit、SF Symbols、模板及其它受限 Apple 资源。

## 已有证据

- 光学材质配置：`main/ui_glass_optics.*`。
- RGB565 融合合成与脏区规划：`main/ui_glass_compositor.*` 和
  `main/liquid_glass_compositor_core.*`。
- 三卡连续动效研究：`main/liquid_glass_motion.*` 与
  `main/demo_liquid_glass.c`。
- 仓库已有真机截图回采与显示遥测能力；没有真机采样时不得编造生产性能结论。

## 产品原则

1. 内容优先，玻璃用于表达交互与层级。
2. 单一连贯控制层优于多层半透明装饰。
3. 动效必须保留对象身份与空间来源。
4. 性能成本是每个组件公开契约的一部分。
5. 无障碍模式改变材质与动效语义，而不只是换颜色。

## 无障碍与包容性

系统必须提供高对比度、减少透明度和减少动画模式。三键导航中的焦点始终可见，所有
关键状态在没有动画时也必须能被理解。
