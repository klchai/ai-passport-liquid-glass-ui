<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# 字库资源（Fonts）

本目录存放项目可复用的字库资源。每个字库子目录或单个字库文件，应附说明。

## 如何使用

- 字库文件（如 `.ttf`、`.otf`、LVGL 使用的 C 数组字库等）复制到本目录，并在本项目 `README.md` 记录字名、字号、支持字符集与版权信息。
- 若需集成到 ESP-IDF 固件，参考 [`components/bsp/include/bsp_display.h`](../../components/bsp/include/bsp_display.h) 与 LVGL 字体接口，将字库转换为对应格式并放入正确资源目录。
- 字库占用 Flash 与内存，需在集成前评估 ESP32-C3 无 PSRAM 的限制（详见 `docs/hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.md`）。

## 目录说明

加入资源时请同步更新本 `README.md` 与英文 `README.md` 的索引。

### Montserrat-Medium.ttf

- 来源：LVGL 自带的 `scripts/built_in_font/Montserrat-Medium.ttf`，与界面 14 px /
  20 px 文字同一字体。放在这里是为了让数字字体能在裸检出上重新生成与校验（CI 的
  static job 正是裸检出）；`managed_components/` 被 gitignore，在那里不存在。
- 许可证：SIL Open Font License 1.1，见 `Montserrat-OFL.txt`。
- 使用方：`tools/gen_digit_font.py` 以 44 px、4 bpp、不压缩的方式把
  `0123456789:-.%BMK` 子集生成到 `main/font_digits_44.c`，供 Kaboo 页的大字
  数字使用。用 `python3 tools/gen_digit_font.py` 重新生成；`--check`（由
  `tools/validate.sh --static` 执行）在提交文件过期时失败。
- TTF 本身不会编进固件，只有子集会。
