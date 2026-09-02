<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# AI Passport Glass 设计包

本目录是原创硬件原生 Glass System 与设计工具之间的边界。这里不包含 Apple UI
Kit、SF Symbols、Apple 截图或可再分发的 Apple 模板。

## 文件

- `tokens.json` 使用 Design Tokens Community Group 格式，可通过兼容的 Token
  工作流导入，或转换成 Figma Variables。
- `components.json` 是实现清单：`implemented` 表示已有可复用 C 公共 API；
  `showcase` 表示 Demo 已展示但仍需抽取；`planned` 不代表已经交付。

C 端语义 Tokens 以 `main/ui_glass_theme.*` 为准，已实现组件以
`main/ui_glass_widgets.*` 为准。修改时必须同步代码和设计导出。

## Figma Library 结构

从 `tokens.json` 建立 `Color`、`Spacing`、`Radius`、`Motion` 四个 Variables
Collection。组件的状态和无障碍模式使用 Variants，不复制成互不关联的组件。Library
页面顺序为：

1. Foundations
2. Materials
3. Controls
4. Navigation
5. Presentation
6. Device patterns
7. Accessibility
8. Performance annotations

每个组件页记录 Anatomy、正确用法、禁止用法、全部输入状态、动效来源，以及
`components.json` 中对应条目的链接。真机数据在实际回采前保持
`pending-device`，不得用估算值冒充测量结果。
