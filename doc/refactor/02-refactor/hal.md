# Phase 2.2 HAL 层重构报告

> **Parent:** [重构跟踪](../README.md) | **Prev:** [Phase 2.1 内核层重构](kernel.md) | **Next:** [Phase 2.3 UI 核心层重构](ui.md)

## 1. 目标

补齐 HAL 层缺失的硬件抽象，减少上层代码对 Arduino/M5GFX API 的直接依赖，同时改善 native 测试环境对文本/布局模拟的真实性。

本轮重构保持**实用范围**，不触及 WiFi/BT 协议栈、M5 系统初始化等深水区。

## 2. 诊断摘要

| ID | 优先级 | 问题 | 位置 |
|---|---|---|---|
| H-P0-1 | P0 | `hal_display.cpp` 职责过重、行数超标 | `src/hal/hal_display.cpp` |
| H-P0-2 | P0 | 屏幕旋转 API 缺失，上层直接调用 `M5.Display` | `main.cpp`, `flasher_app.cpp`, `sm_app.cpp`, `dev_fb0.c` |
| H-P0-3 | P0 | 屏幕背光 API 缺失 | `main.cpp` |
| H-P1-3 | P1 | `hal_layout.h` 依赖完整 `hal_display.h`，组织松散 | `src/hal/hal_layout.h` |
| H-P1-4 | P1 | `g_screen_width/height` 链接属性隐患 | `src/hal/hal_display.h`, `src/hal/hal_display.cpp` |
| H-P1-6 | P1 | native 字体宽度恒为 0，布局测试失真 | `src/hal/hal_display.cpp` |

## 3. 实施计划

### Step 1 — 新增 HAL 显示配置 API

在 `hal_display.h` 中增加：

```c
void hal_display_set_rotation(int rotation);
int  hal_display_get_rotation(void);
void hal_display_get_size(int16_t *w, int16_t *h);
void hal_display_set_brightness(uint8_t level);
uint8_t hal_display_get_brightness(void);
```

并迁移 `main.cpp`、`flasher_app.cpp`、`sm_app.cpp`、`dev_fb0.c` 中的 `M5.Display.setRotation()` / `M5.Display.width()` / `M5.Display.height()` 调用。

### Step 2 — 拆分 `hal_display.cpp`

按职责拆成四个实现文件，保留统一头文件 `hal_display.h`：

- `hal_display_fb.cpp`：帧缓冲/画布管理（`init`/`deinit`/`clear`/`flush`）+ 方向/亮度配置
- `hal_display_draw.cpp`：绘制原语（像素/线/矩形/圆/圆角矩形）
- `hal_display_font.cpp`：字体与文本（`set_font`/`draw_string`/`get_string_width`/`get_font_height`/`get_cn_font`）
- `hal_display_adv.cpp`：高级绘制（XOR 反色矩形、XBM 位图、裁剪矩形、`hal_test_fb_read` 钩子）

native 与硬件实现通过 `#ifdef NATIVE_TEST` 分别编译到各自的 `.cpp` 中。

### Step 3 — 新增 `hal_screen.h`

仅提供屏幕尺寸访问，消除 `hal_layout.h` 对 `hal_display.h` 的依赖。

### Step 4 — 修复 `g_screen_width/height` 链接属性

将定义移入 `extern "C"` 块，或改为通过 `hal_display_get_size()` 访问。

### Step 5 — native 字体模拟器

为 native 测试实现固定宽度字体模拟：

- `hal_get_font_height()` 返回 8（保持）
- `hal_get_string_width(str)` 返回 `strlen(str) * 6` 像素
- `hal_draw_string()` 在帧缓冲上绘制 6×8 像素 ASCII 位图字体

使 UI 布局测试（如选择器宽度、文本裁剪）具有可预测的尺寸输入。

### Step 6 — 文档同步

- 更新 `doc/hal/display.md` 反映拆分后的文件结构
- 更新 `doc/hal/index.md` 与 `CLAUDE.md` 中 HAL 层文件列表
- 更新 `doc/coding-style.md` 明确上层禁止直接包含 Arduino/M5GFX 头文件

## 4. 验证标准

- `pio test -e native` 通过。
- `pio run -e m5stick-c` 编译通过且无新 warning。
- 上层代码不再直接调用 `M5.Display.setRotation()` / `setBrightness()` / `width()` / `height()`。

## 5. 实际完成摘要

### 5.1 新增 / 拆分文件

| 文件 | 说明 |
|---|---|
| `src/hal/hal_display.h` | 统一显示 API 头文件；新增 `hal_display_set_rotation/get_rotation/set_brightness/get_brightness` |
| `src/hal/hal_display_fb.cpp` | 帧缓冲/画布生命周期、方向与亮度配置 |
| `src/hal/hal_display_draw.cpp` | 绘制原语（像素、线、矩形、圆、圆角矩形） |
| `src/hal/hal_display_font.cpp` | 字体与文本；native 环境实现 6×8 ASCII 位图字体模拟 |
| `src/hal/hal_display_adv.cpp` | XOR 反色矩形、XBM 位图、裁剪矩形、`hal_test_fb_read` 测试钩子 |
| `src/hal/hal_screen.h/c` | 运行时屏幕尺寸查询，集中定义 `g_screen_width/height` |

### 5.2 上层迁移

- `src/main.cpp`：`M5.Display.setBrightness()` / `setRotation()` / `width()` / `height()` → HAL API
- `src/app/flasher/flasher_app.cpp`：同上
- `src/app/serial_monitor/sm_app.cpp`：同上
- `src/kernel/devices/dev_fb0.c`：`DEV_FB_IOCTL_SET_ROTATION` 现在调用 `hal_display_set_rotation()`

### 5.3 关键修复

- `src/hal/hal_screen.c` 添加 `#include <stddef.h>`，修复 `NULL` 未声明错误。
- 在 `src/hal/hal_screen.c` 硬件分支定义 `g_screen_width = 160; g_screen_height = 80;`，修复链接器未定义引用。
- `src/hal/hal_display_fb.cpp` 修复未闭合 `#ifdef NATIVE_TEST` 与重复硬件代码块。
- `src/hal/hal_display_adv.cpp` 将中文字体对象 `cn_font` 定义迁入本文件，避免跨编译单元未声明错误。
- native 环境下，全局帧缓冲 `g_framebuffer` 改由 `hal_display_fb.cpp` 单独定义，其余拆分文件使用 `extern` 共享，避免多份缓冲区导致测试失败。
- native 字体层 `g_font_fb` 改为外部链接，`hal_display_init()` 初始化时同步清空字体层，保证 `/dev/fb0` 像素测试不受前序测试文本残留影响。

### 5.4 文档同步

- 重写 `doc/hal/display.md`，反映拆分后的文件结构与新的旋转/亮度 API。
- 新增 `doc/hal/screen.md` 说明 `hal_screen.h/c` 的设计与用法。
- 更新 `doc/hal/index.md`、`doc/index.md`、`CLAUDE.md` 中的 HAL 文件列表。

### 5.5 验证结果

| 检查项 | 结果 |
|--------|------|
| `pio test -e native` | **371/371 passed** |
| `pio run -e m5stick-c` | **SUCCESS**（Flash 88.0%，RAM 22.3%） |
| 新增 warning/error | 无 |
| 上层直接调用 `M5.Display.setRotation/setBrightness/width/height` | 已清零 |

### 5.6 已知未处理

- `src/hal/hal_layout.h` 仍为获取 `hal_get_font_height()` 而包含 `hal_display.h`。由于字体高度是布局计算的必要输入，当前保持该依赖；若未来需要进一步解耦，可将 `hal_get_font_height()` 的前向声明移入 `hal_layout.h` 并改用 `hal_screen.h`。
