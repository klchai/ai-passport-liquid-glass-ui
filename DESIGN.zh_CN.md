<p align="right">
  <strong>简体中文</strong> · <a href="DESIGN.md">English</a>
</p>

# AI Passport Glass System

## 范围与成熟度

本文定义硬件原生 Glass System 的长期视觉与交互边界。当前 Foundation 已实现
Tokens、四种无障碍 Profile、确定性 Motion、三键 Focus、核心内容与控制组件、动态
显示质量、八个展示场景与两个实时用量页面。组件真机成本在当前固件
刷入并完成物理测量前保持待测状态。

## 方向契约

- **核心命题：**一个具有光学活性的控制层悬浮在稳定内容之上。
- **视觉世界：**深色摄影壁纸、通栏且安静的内容画布、克制的冷色光，以及只服务于
  操作和焦点的玻璃。
- **首屏：**内容层直接说明产品；底部常驻 Platter 表达输入和页码，但不遮挡内容。
- **标志交互：**一个触发控件以轻微欠阻尼的 560 ms Morph 原位展开为 Menu，再回到
  同一物理来源。
- **风险：**RGB565、低刷新率和缺失 TE 会吞掉细微动效，因此每个采样帧的几何与
  状态都必须清晰，并在 Reduced Motion 下保持可理解。

## 分层模型

```text
壁纸 / 产品内容
      ↓
通栏弱化画布或语义化实色分组
      ↓
单一玻璃控制层
      ↓
从物理来源展开的瞬态 Menu、Focus 或反馈
```

普通列表和设置内容直接流动在画布上，不再放入重复的整页大卡片。密集控件需要
稳定阅读表面时，可保留紧凑实色分组。只有产品语义明确要求时，才允许出现多个
玻璃对象。互相重叠、用途相同的玻璃卡片不是默认布局。

## Foundations

- 间距：4、8、12、16 px。
- 圆角：控件 14 px、语义化内容分组 18 px、悬浮 Platter 22 px。
- 字体：Montserrat 20 用于页面级层次，Montserrat 14 用于控件、元信息和测量；当前
  固件不宣称支持 CJK。
- 语义颜色：正文、弱化正文、Accent、Positive、Warning、Danger、稳定内容表面和
  中性控制 Tint。
- 硬件安全区：内容左右 16 px；Header 位于 y=44 上方；Scene 位于其下；常驻控制
  Platter 位于 y=272。

可移植设计源为 `design/liquid-glass/tokens.json`，固件源为
`main/ui_glass_theme.*`。

## 材质语义

| Material | 用途 | 禁止用法 |
| --- | --- | --- |
| Regular | 常驻交互 Platter 和普通 Focus | 所有内容卡片 |
| Clear | 小型 Focus Lens 或明确的光学对比 | 大段文字内容 |
| Contrast | 高对比和减少透明度控件 | 装饰性纵深 |
| Chrome | 无 Glint 的安静状态 Chrome | 主要内容容器 |

Material Engine 使用适合 RGB565 的预计算透明度、边缘、镜面高光和 Glint Profile，
不运行 Blur Kernel。边缘只近似借用背景颜色；融合合成器继续服务于专用三卡实验，但
不会把该布局变成框架默认值。

## Motion System

| Token | 时长 | 语义 |
| --- | ---: | --- |
| Press | 120 ms | 即时物理压入，然后释放 |
| Focus | 320 ms | 单一 Focus Lens 的连续移动 |
| Materialize | 520 ms | 材质和内容逐步可用 |
| Morph | 560 ms | Trigger 原位成为 Menu、Popover 或 Sheet |
| Page | 400 ms | 适配面板可见刷新节奏的整场景连续切换 |

`ui_glass_motion.*` 提供整数 Cubic Easing、只有一次克制 Overshoot 的确定性 Spring，
以及通用 Morph Geometry。Reduced Motion 保留 80 ms Press，空间位移动效改为立即完成。

## 输入与焦点

- 浏览模式：UP/DOWN 切换上一页/下一页，OK 执行页面主操作。
- 长按 OK 进入或退出页内操作，Claude 保持只读。
- 页内操作：UP/DOWN 移动焦点或调整值，OK 激活选项。Controls 使用 OK 切换下一行，
  Kaboo 使用 UP/DOWN 前后翻卡。
- 进入页面或切换模式时，底栏短暂说明长按 OK 的作用。页眉保留完整页名，
  页内模式使用强调色标题。
- 默认关闭自动翻页与场景演示。Kaboo 每八秒轮播一次，页内操作时暂停，
  手动翻卡后延后恢复轮播。

Focus 是沿连续轨迹移动的对象，不是每个 Row 各自凭空出现和消失的边框。关闭动画后，
聚焦状态仍必须可见。

## 组件与状态

每个组件支持适用的 Default、Focused、Pressed、Selected、Disabled、Loading、Error
状态。无障碍 Profile 是同一组件的 Variant，不是互不关联的复制品。

首批可复用 C API 包含 Content Panel、Glass Platter、Focus Lens、Row、Toggle 和
Slider。Morph Menu、Dock 和 Device Status 已在 Showcase 中验证，但在抽取公共 API
前不能宣称为可复用组件。完整且诚实的清单见
`design/liquid-glass/components.json`。

每个公共组件最终必须发布 RAM、Flash、典型与最坏脏区像素、典型与最坏帧耗时。未知
真机数据保持 `pending-device`，估算值不得冒充测量值。

## Patterns 与 Showcase

真机 Showcase 是八场景交互与动效作品集。它保留原组件覆盖范围，但以可信的手机式
场景承载，不再把实现分类直接展示给观众。页面顺序与稳定的内部组件标识刻意解耦：

1. Player：通栏媒体内容、播放状态，以及从原控件展开的 Quick Actions Morph
2. Home：带连续选区与内容运动的悬浮 Dock 导航，以及电量圆环和时间/日期卡片
3. Focus：分段模式、Toggle、选择项，以及唯一且连续移动的 Focus Lens
4. Controls：带动画的玻璃 Slider Knob、Stepper 和可调 Progress
5. Devices：列表遍历、持续焦点与 Row 激活反馈
6. Activity：统一的离线同步任务，覆盖 Progress、Paused 与 Error 状态
7. Moments：带摄影内容语义的主操作、玻璃操作与危险操作
8. Appearance：可选择 Standard、High Contrast、Reduced Transparency 与 Reduced
   Motion 系统 Profile

Kaboo 与 Claude 位于 Appearance 之后，分别为第九、第十页，Settings 为第十一页。
Kaboo 标明 token 计数与模型，Claude 明确百分比是已用配额。两页均显示采样新鲜度并弱化旧值。
示例内容与操作明确标记为演示。Controls 显示真实亮度与音量，以及音频不可用状态，
电量采样每分钟更新一次。Home 的电量圆环与时间/日期卡片来自设备时钟；电脑 BLE
payload 和 `ntp1.aliyun.com` 提供校时。

快速操作 Focus、Toggle、Slider 和 Segmented 时，从视觉对象当前采样位置重新
定向，不启动互相竞争的动画。High Contrast 与 Reduced Transparency 除了应用于
共享控件，也覆盖内容画布、Player、Home 与实时数据页面。


Home 是固定默认首页并始终显示。Settings 是第十一页，也始终保留。它按展示顺序
列出十个内容页，每屏四个复选框。
页内 UP/DOWN 移动选项，OK 切换可见性。启动、翻页和底栏邻居页名均遵守可见集合，
隐藏可选页面后仍可访问 Home 与 Settings，Home 不能切换关闭。设置以稳定页面 ID 的位掩码保存到应用 NVS 的 `dashboard`
命名空间、`pages_v1` 键，读写不涉及身份区或 Recovery。UI 只发布原子快照，应用
任务每秒合并保存一次；只有保存成功才显示已保存，失败保留会话设置并提示。

## Runtime 与性能

Runtime 管理壁纸合成器、语义 Mode、刷新 Timer，以及带迟滞的质量控制器：

| Quality | 刷新周期 | 动态玻璃上限 | Glint |
| --- | ---: | ---: | --- |
| Full | 10 ms | 6 | 开启 |
| Balanced | 16 ms | 3 | 开启 |
| Economy | 33 ms | 1 | 关闭 |

连续三次慢采样才允许降级，连续六次快采样才恢复，因此一次全屏过渡不会永久降低质量。
BSP 每秒提供 Update Rate、CPU Render、DMA Wait、SPI Wire-Time Floor、更新像素、
Invalidation 请求和 DMA Heap 快照。

## 禁止作为默认方案

- 把 Glass 当作通用内容卡片样式。
- 没有产品语义的嵌套或重叠 Glass。
- 脱离 Trigger 凭空出现的 Menu。
- 用创建或销毁替代对象连续路径的动效。
- Primary Focus、Morph 或 Page Transition 使用线性运动。
- 在仓库嵌入 Apple 素材、SF Symbols、UI Kit 或模板。
- 没有可复现真机回采的性能数字。

## Figma 边界

Figma Library 与 Foundations、Materials、Controls、Navigation、Presentation、Device
Patterns、Accessibility 和 Performance Annotations 对齐。Tokens 与组件清单位于
`design/liquid-glass/`。它是为 AI Passport 原创的 Library；Apple 资源只作为研究
参考，不是可再分发源素材。
