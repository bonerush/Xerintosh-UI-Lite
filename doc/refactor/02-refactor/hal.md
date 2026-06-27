# HAL 层重构报告

> **Parent:** [doc/refactor/README.md](../README.md)

## 变更摘要

本次 HAL 层重构针对阶段 1 诊断中识别的 4 个问题（H1-H4）进行了原子级修改：

| 问题 ID | 描述 | 对应修改 |
|---------|------|----------|
| H1 | `g_canvas` 全局裸指针未封装，跨文件直接 extern | 新增 `hal_display_canvas()` / `hal_display_framebuffer()` 访问器，将 `g_canvas` / `g_framebuffer` 改为 `static` |
| H2 | 按钮状态分散在 `g_btn_a`/`g_btn_b`，双击开关为全局布尔 | 引入 `hal_input_context_t` 集中管理，新增 per-button 双击开关 API |
| H3 | 屏幕尺寸常量重复：`SCREEN_WIDTH/HEIGHT` 与 `HAL_SCREEN_WIDTH/HEIGHT` 并存 | 移除 `SCREEN_WIDTH/HEIGHT`，统一使用 `HAL_SCREEN_WIDTH/HEIGHT` |
| H4 | tick 接口语义不统一：`hal_get_ticks()` 在不同后端含义不同 | 引入显式 `hal_get_ticks_ms()`，保留 `hal_get_ticks()` 为兼容别名 |

---

## 文件变更清单

### 新增文件

- `test/test_native/test_hal_input.cpp` — per-button 双击开关测试

### 修改文件

- `src/hal/hal_display.h` — 移除 `SCREEN_WIDTH/HEIGHT`，新增画布/帧缓冲访问器声明
- `src/hal/hal_display_fb.cpp` — 封装 `g_canvas`/`g_framebuffer`，实现访问器
- `src/hal/hal_display_draw.cpp` — 改用访问器获取画布/帧缓冲
- `src/hal/hal_display_font.cpp` — 改用访问器获取画布
- `src/hal/hal_display_adv.cpp` — 改用访问器获取画布/帧缓冲
- `src/hal/hal_input.h` — 新增 per-button 双击开关 API
- `src/hal/hal_input.cpp` — 重构为统一输入上下文
- `src/hal/hal_system.h` — 声明 `hal_get_ticks_ms()`，保留 `hal_get_ticks()` 别名
- `src/hal/hal_system.cpp` — 实现重命名为 `hal_get_ticks_ms()`
- `src/hal/hal_screen.h`、`src/hal/hal_screen.c`、`src/hal/hal_layout.h` — 统一屏幕尺寸常量
- `test/test_native/test_hal_display.cpp` — 新增屏幕常量统一性与访问器测试
- `test/test_native/test_hal_system.cpp` — 新增 tick 单调性测试

---

## 测试

### 新增测试

| 测试文件 | 测试名 | 验证点 |
|----------|--------|--------|
| `test/test_native/test_hal_display.cpp` | `HalLayoutTest.ScreenConstantsAreUnified` | `HAL_SCREEN_WIDTH/HEIGHT` 为唯一来源 |
| `test/test_native/test_hal_display.cpp` | `HalDisplayTest.CanvasAccessorReturnsNullBeforeInit` | 初始化前画布访问器返回 `nullptr` |
| `test/test_native/test_hal_display.cpp` | `HalDisplayTest.FramebufferAccessorIsNonNull` | 帧缓冲访问器返回有效可写内存 |
| `test/test_native/test_hal_system.cpp` | `HalSystemTest.TicksMsIsAvailableAndMonotonic` | `hal_get_ticks_ms()` 单调递增 |
| `test/test_native/test_hal_input.cpp` | `HalInputTest.DoubleClickCanBeEnabledPerButton` | per-button 双击开关与全局开关兼容 |

### 验证命令

```bash
pio test -e native
pio run -e m5stick-c
pio run -e m5stick-c-native
```

### 验证结果

- `pio test -e native`：**通过**（538 cases，2 skipped，536 succeeded）
- `pio run -e m5stick-c`：**通过**
- `pio run -e m5stick-c-native`：**通过**

无新增编译警告。

---

## 风险说明

1. **LovyanGFX 兼容性**：`hal_display_canvas()` 返回不透明指针，硬件后端通过 `reinterpret_cast` 转换为 `lgfx::LGFX_Sprite*`。该转换与原有 `g_canvas` 类型一致，无 ABI 风险。
2. **per-button 双击行为**：`hal_input_set_double_click_enabled(true)` 仍同时设置两个按键的开关，保持向后兼容；新增 API 允许单独控制。
3. **内存占用**：上下文重构未新增静态数组或全局变量，RAM/Flash 占用与基线基本持平。

---

## 提交记录

```
refactor(hal): unify screen size constants to HAL_SCREEN_*
refactor(hal): introduce hal_get_ticks_ms() as unified tick source
refactor(hal): encapsulate canvas and framebuffer accessors
refactor(hal): unify input state into context and add per-button double-click flag
```

---

> **See Also:** [kernel.md](kernel.md) | [ui.md](ui.md) | [app.md](app.md) | [docs.md](docs.md)
