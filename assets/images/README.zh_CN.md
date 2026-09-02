<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# 图片资源（Images）

本目录存放项目可复用的图片资源，如 UI 图标、背景、RGB565 资源等。

## 如何使用

- 图片文件复制到本目录，并在本项目 `README.md` 记录分辨率、格式、用途与来源。
- 与固件集成时，参考 [`components/bsp/include/bsp_display.h`](../../components/bsp/include/bsp_display.h) 与相关示例分支的图片资源管线，转换为固件所需格式（如 RGB565 数组）。
- 图片资源占用 Flash 与内存，集成前请评估 ESP32-C3 无 PSRAM 的限制。

## Liquid Glass 壁纸

- `liquid_glass_wallpaper_source.jpg`：720 × 960 的源裁切图，用于评审和后续重新处理。
- `liquid_glass_wallpaper.rgb565`：240 × 320 小端 RGB565 固件资源；通过 `python tools/build_liquid_glass_wallpaper.py` 生成。
- 集成方式：由 `main/CMakeLists.txt` 嵌入，`main/ui_glass.c` 直接从 Flash 渲染，不分配全屏解码缓冲区。
- 优化方式：静态蓝色调与标题可读性渐变已预烘焙进 RGB565 资源，重绘时无需再对两层全屏透明对象做 alpha 合成。
- 来源：[Unsplash glass wallpaper 搜索页](https://unsplash.com/s/photos/glass-wallpaper)，图片资源 `photo-1706101299176-292d8c5e470e`。
- 许可：[Unsplash License](https://unsplash.com/license)。
