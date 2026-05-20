# M5Stick-C UI 框架移植设计文档

> **Parent:** [项目知识地图](../index.md) | **Related:** [硬件 HAL](../hal/index.md), [UI 核心](../ui/index.md)

## 概述

本文档描述将 `reference/oled-ui-Xerintosh-lite`（基于 u8g2 的 128×64 OLED UI 框架）移植到 M5Stick-C（80×160 TFT，ST7735S）的设计方案。

移植策略：**先精确复制，再平台适配**。保留原始框架的所有状态逻辑、动画公式、数据结构，仅替换硬件相关代码和屏幕尺寸常量。

## 设计约束

- 语言：C 核心 + C++ HAL 桥接（M5GFX 为 C++ 库）
- 屏幕：80×160，RGB565，必须通过双缓冲防闪烁
- 去除原作者所有信息（注释头、include guard 命名等）
- 去除开屏动画（长按 2.5s 进入逻辑），UI 直接启动
- 所有技术文档用中文撰写

## 架构设计

### 模块结构

```
src/
├── hal/                     # 硬件抽象层（全新实现）
│   ├── hal_display.h/.c     # TFT 双缓冲驱动（M5Canvas）
│   ├── hal_input.h/.c       # 按键输入状态机
│   ├── hal_system.h/.c      # 系统时钟 / 延时
│   └── hal_sensor.h/.c      # 传感器 HAL（预留接口）
├── ui/                      # UI 核心（移植自参考）
│   ├── ui_core.h/.c         # 动画引擎 + 主循环
│   ├── ui_drawer.h/.c       # 渲染管道
│   ├── ui_item.h/.c         # 数据结构 + 选择器/相机逻辑
│   └── ui_draw_driver.h/.c  # 绘图 API 适配层（原 oled_* 宏的实现）
└── main.cpp                 # 应用入口 + UI 初始化 + 帧循环
```

### 分层职责

| 层级 | 职责 | 文件 |
|------|------|------|
| **HAL** | 封装所有硬件操作：TFT 绘图、按键扫描、系统 tick | `hal_*` |
| **Draw Driver** | 将原始 `oled_*` 宏替换为 HAL 调用，提供尺寸/颜色常量 | `ui_draw_driver.*` |
| **Core** | 动画更新、主循环调度、页面切换状态机 | `ui_core.*` |
| **Drawer** | 所有绘制函数：列表项、选择器、弹窗、信息栏、退场动画 | `ui_drawer.*` |
| **Item** | 数据结构（List/Switch/Slider/Button/User）、构造函数、输入响应 | `ui_item.*` |
| **App** | `main.cpp` 中初始化 M5、创建 canvas、注册传感器、运行帧循环 | `main.cpp` |

## 关键适配点

### 屏幕参数

| 参数 | 原始 (OLED) | 目标 (TFT) |
|------|------------|-----------|
| 宽度 | 128 | 80 |
| 高度 | 64 | 160 |
| 颜色 | 1-bit 单色 | RGB565 |
| 行高 | 15 | 18（按比例缩放） |
| 字体 top margin | 4 | 6 |

### 双缓冲实现

使用 `M5Canvas` 作为后缓冲：

1. `M5.begin()` 之后延迟构造：`canvas = new M5Canvas(&M5.Display)`
2. 每帧：`canvas->clear()` → 所有绘制 → `canvas->pushSprite(&M5.Display, 0, 0)`
3. 避免全局静态 `M5Canvas` 对象

### XOR 反色模拟（选择器高亮）

原始代码用 `oled_set_draw_color(2)` 实现 XOR 反色。TFT 无原生 XOR 模式，采用像素级模拟：

```c
// 伪代码：选择器区域像素 XOR
for (y = selector_y; y < selector_y + selector_h; y++) {
    for (x = selector_x; x < selector_x + selector_w; x++) {
        uint16_t pixel = canvas->readPixel(x, y);
        canvas->drawPixel(x, y, pixel ^ 0xFFFF);  // RGB565 按位取反
    }
}
```

### 字体系统

- 英文字体：使用 M5GFX 内置 `fonts::Font0` 或 `fonts::FreeMono9pt7b`
- 中文字体：使用内置点阵字库（编译时嵌入）
- 字符串宽度/高度：通过 M5Canvas 的 `textWidth()` 和 `fontHeight()` 获取

### 输入状态机

BtnA (GPIO37) / BtnB (GPIO39) 各有两个模式，通过双击切换。

| 按键 | 模式1 短按 | 模式1 长按 | 模式1 双击 | 模式2 短按 | 模式2 长按 |
|------|-----------|-----------|-----------|-----------|-----------|
| **BtnA** | 上一项 | 返回/退出 | → 模式2 | 上一项 | 快速减 |
| **BtnB** | 下一项 | 确认/进入 | → 模式2 | 下一项 | 快速加 |

时序参数：
- 消抖：连续 3 帧一致确认
- 短按阈值：< 200ms
- 长按阈值：> 500ms，之后每 100ms 重复触发
- 双击阈值：两次按下间隔 < 300ms

### 渲染管道（每帧顺序）

```
1. input_process()              → 更新按键状态机
2. xerintosh_ui_main_core()         → 更新动画 + 业务逻辑
3. canvas->clear()              → 清空后缓冲
4. xerintosh_draw_list()            → 背景层：列表外观 + 列表项 + 选择器
5. xerintosh_draw_widget()          → 前景层：信息栏 + 弹窗
6. canvas->pushSprite()         → DMA 推送到屏幕
7. delay(1)                     → 帧率控制
```

### 动画引擎（保留原始公式）

```c
void xerintosh_animation(float *_pos, float _posTrg, float _speed) {
    if (fabs(*_pos - _posTrg) <= 1.0f) *_pos = _posTrg;
    else *_pos += (_posTrg - *_pos) / (100.0f - _speed) / 1.0f;
}
```

速度参数保留原始值：
- 列表项展开：84
- 选择器移动：92-93
- 相机滚动：96
- 弹窗/信息栏：94-96

### 退场动画

保留原始退场动画逻辑（沙漏图标 + 棋盘格遮罩），但：
- 将 `OLED_WIDTH` / `OLED_HEIGHT` 替换为新的屏幕常量
- 将 `oled_set_draw_color()` 调用替换为 RGB565 颜色常量

### 颜色常量（RGB565）

| 名称 | 值 | 用途 |
|------|-----|------|
| `COLOR_BG` | `0x0000` | 背景（黑） |
| `COLOR_FG` | `0xFFFF` | 前景（白） |
| `COLOR_ACCENT` | `0x07E0` | 强调色（绿） |

## 数据结构（保留原始设计）

C 风格 OOP：
- 所有 UI 元素以 `xerintosh_list_item_t` 为基结构体（第一个成员）
- 派生类型：`switch_item_t`、`slider_item_t`、`button_item_t`、`user_item_t`
- 类型安全转换：通过 `type` 枚举检查后再做指针强转

## 初始化流程

```c
void setup() {
    M5.begin();
    hal_display_init();       // 创建 M5Canvas
    hal_system_init();        // 初始化 tick 计数器
    hal_input_init();         // 配置 GPIO37/39

    xerintosh_init_core();        // 初始化 UI 核心
    // 构建菜单树...
    in_xerintosh = true;          // 直接启动 UI（无长按门槛）
}

void loop() {
    input_process();
    xerintosh_ui_main_core();
    xerintosh_ui_widget_core();
    hal_display_flush();
    delay(1);
}
```

## 测试策略

- 使用 `env:native` 环境编译桌面测试
- 为 HAL 层编写 mock 实现（内存 framebuffer + 模拟按键）
- 核心动画函数和状态机用 GoogleTest 覆盖

## 风险与缓解

| 风险 | 缓解措施 |
|------|---------|
| TFT XOR 模拟性能不足 | 先实现正确性，再测帧率；若不足可退化为颜色反色重绘 |
| 中文字体占用 Flash 过大 | 仅嵌入常用字，使用字模工具按需生成 |
| ESP32 未初始化变量 | 所有全局/静态变量显式初始化，尤其布尔标志 |
| M5Canvas parent 指针失效 | 使用 `new M5Canvas(&M5.Display)` 并显式 `pushSprite(&M5.Display, 0, 0)` |

## 移植文件对应关系

| 原始文件 | 移植后文件 | 变更说明 |
|---------|-----------|---------|
| `xerintosh_ui_core.h/.c` | `ui_core.h/.c` | 重命名，去除作者注释，适配尺寸常量 |
| `xerintosh_ui_drawer.h/.c` | `ui_drawer.h/.c` | 重命名，去除作者注释，颜色调用适配 |
| `xerintosh_ui_item.h/.c` | `ui_item.h/.c` | 重命名，去除作者注释 |
| `xerintosh_ui_draw_driver.h/.c` | `ui_draw_driver.h/.c` | 将宏替换为 HAL 函数调用 |
| （无） | `hal_display.h/.c` | 全新：M5Canvas 双缓冲封装 |
| （无） | `hal_input.h/.c` | 全新：按键状态机 |
| （无） | `hal_system.h/.c` | 全新：tick / delay |
| （无） | `main.cpp` | 全新：入口 + 帧循环 |

---

> **See Also:** [UI 核心模块](../ui/core.md) | [HAL 显示层](../hal/display.md) | [输入状态机](../hal/input.md)
