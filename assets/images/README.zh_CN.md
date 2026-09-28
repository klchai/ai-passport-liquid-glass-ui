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

- `liquid_glass_wallpaper.lgp8`：240 × 320 索引色（"LGP8"）固件资源，画面是石墨灰底上的
  丝绸褶线。文件由 12 字节文件头、256 项小端 RGB565 调色板和每像素一个调色板索引组成，
  共 77,324 字节。抖动后的画面只用到 137 种颜色，因此这种编码是无损的；字节布局见
  `main/liquid_glass_compositor_core.h`。通过 `python tools/build_liquid_glass_wallpaper.py`
  生成（需要 `ffmpeg`）。`--from-rgb565 <raster>` 可把现有的 240 × 320 小端 RGB565
  栅格重新编码，不需要 `ffmpeg`。颜色超过 256 种的栅格会被直接拒绝，而不是再量化一次。
- 来源：该脚本中定义的原创程序化画面，以 720 × 960、16 位精度渲染，因此脚本本身就是
  可编辑源文件。如需改用其他图片，传入 `--source <image>`。
- 集成方式：由 `main/CMakeLists.txt` 嵌入。`main/ui_glass_compositor.c` 启动时校验一次，
  把 512 字节调色板复制到 RAM，再从 Flash 逐行解码、直接写入 LVGL 绘制缓冲区，不分配
  全屏解码缓冲区。
- 优化方式：静态中性色调与标题可读性渐变已预烘焙进资源，重绘时无需再对两层全屏透明
  对象做 alpha 合成。4 × 4 有序抖动避免深色渐变在 RGB565 下出现色带。每像素 1 字节，
  从 Flash 读取的数据量是原 RGB565 栅格的一半；页面内容画布下方的压暗效果由合成器用
  预先着色的调色板副本解码得到。
- 光学边取色：脚本会打印 `main/ui_glass_optics.c` 所需的 `WALLPAPER_CENTER_SAMPLES`
  数据行，每次更换资源都要同步更新；`tools/validate.sh` 会运行 `--check-samples`，
  两者不一致时直接失败。
- 许可：原创内容，适用仓库[许可证](../../LICENSE)。
