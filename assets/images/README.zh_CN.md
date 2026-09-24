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

- `liquid_glass_wallpaper.rgb565`：240 × 320 小端 RGB565 固件资源，画面是石墨灰底上的
  丝绸褶线；通过 `python tools/build_liquid_glass_wallpaper.py` 生成（需要 `ffmpeg`）。
- 来源：该脚本中定义的原创程序化画面，以 720 × 960、16 位精度渲染，因此脚本本身就是
  可编辑源文件。如需改用其他图片，传入 `--source <image>`。
- 集成方式：由 `main/CMakeLists.txt` 嵌入，`main/ui_glass.c` 直接从 Flash 渲染，不分配全屏解码缓冲区。
- 优化方式：静态中性色调与标题可读性渐变已预烘焙进 RGB565 资源，重绘时无需再对两层
  全屏透明对象做 alpha 合成。4 × 4 有序抖动避免深色渐变在 RGB565 下出现色带。
- 光学边取色：脚本会打印 `main/ui_glass_optics.c` 所需的 `WALLPAPER_CENTER_SAMPLES`
  数据行，每次更换资源都要同步更新；`tools/validate.sh` 会运行 `--check-samples`，
  两者不一致时直接失败。
- 许可：原创内容，适用仓库[许可证](../../LICENSE)。
