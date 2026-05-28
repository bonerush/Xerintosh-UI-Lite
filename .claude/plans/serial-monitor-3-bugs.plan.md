# Plan: 串口监视器三个 Bug 修复

**Complexity**: Medium
**涉及文件**: `src/ui/ui_item.c`, `src/ui/ui_core.c`, `src/app/serial_monitor.cpp`

## 总览

三个 Bug 之间存在因果关系，根因是 `xerintosh_new_user_item()` 中结构体字段未初始化。修复根因（Bug 1）即可连带解决 Bug 3。Bug 2 是独立的布局计算问题。

---

## Bug 1: 选择即触发进入（根因 Bug）

### 症状
开机后主菜单选择器移动到「串口监视器」时（短按），未长按确认就直接进入 App。进入 App 再退出后，问题消失。

### 根因
`src/ui/ui_item.c:297-308` — `xerintosh_new_user_item()`:

```c
xerintosh_list_item_t *xerintosh_new_user_item(...)
{
    xerintosh_user_item_t *_item = (xerintosh_user_item_t*)malloc(sizeof(xerintosh_user_item_t));
    // ...
    xerintosh_init_base_item(&_item->base_item, user_item, _content, icon, user_icon);
    // 仅设置了 init_function / loop_function / exit_function
    // 以下 5 个字段从未显式初始化：
    //   in_user_item, entering_user_item, exiting_user_item,
    //   user_item_inited, user_item_looping
}
```

`xerintosh_init_base_item()` 只 `memset(_item, 0, sizeof(xerintosh_list_item_t))`，即只归零了基类部分（180 字节），**派生结构体的 5 个 bool 字段未被覆盖**。

在 ESP32（FreeRTOS + Arduino）上，`malloc` **不保证归零**。如果 `in_user_item` 恰好是非零值：
1. `xerintosh_is_in_user_item()` 返回 `true`
2. `app_input_process()` 提前返回（不处理菜单输入）
3. `xerintosh_ui_main_core()` 直接调用 `serial_monitor_loop()` 绘制 App 界面
4. 用户看到的是未经过 `serial_monitor_init()` 初始化的串口监视器

首次退出后，`in_user_item` 被正确置为 `0`，所以第二次操作恢复正常。

### 修复
在 `xerintosh_new_user_item()` 中显式初始化 5 个派生字段：

```c
_item->in_user_item = false;
_item->entering_user_item = false;
_item->exiting_user_item = false;
_item->user_item_inited = false;
_item->user_item_looping = false;
```

**文件**: `src/ui/ui_item.c` — 在 `xerintosh_new_user_item()` 中，`xerintosh_init_base_item()` 调用之后、设置三回调之前插入这 5 行。

### 为什么不用 memset 整个结构体？
若改为 `memset(_item, 0, sizeof(xerintosh_user_item_t))`，必须在 `memset` **之后**设置 `init_function` 等回调指针。当前代码是 `memset` 在前、赋值在后，顺序上没问题，但只改了基类大小的 memset。两种方式等价，显式初始化派生字段更清晰、更易维护。

---

## Bug 2: 终端栏顶部文字与边框重合

### 症状
信息栏（顶部控制区）的波特率文字（如 `RATE:115200`）与终端区域的大矩形边框顶部重叠，且文字底部超出边框。

### 根因
`src/app/serial_monitor.cpp` — `draw_info_bar()` 和 `draw_terminal()`:

```c
// draw_info_bar: 信息栏高度 = font_h + 1 (≈9px)
int16_t bar_h = font_h + 1;
int16_t bar_y = 1;

// 文字纵坐标
int16_t rate_y_text = bar_y + (bar_h + font_h) / 2 - 3;
//                    = 1 + (9 + 8) / 2 - 3 = 6

// draw_terminal: 终端边框起点
int16_t term_y = bar_h + 3;  // = 9 + 3 = 12
```

以默认字体高度 8px 计算：
- 文字绘制在 y=6，M5GFX 的 `drawString` 以顶部为基准，文字占据 y=6..14
- 终端边框从 y=12 开始
- **重叠区域**: y=12,13,14（3px），且文字底部超出到 y=14，视觉上「捅穿」了边框

### 修复
方案 A（推荐）：增大信息栏与终端之间的间距，将 `term_y = bar_h + 3` 改为 `term_y = bar_h + 4` 或 `term_y = bar_h + font_h + 1`。

方案 B：减小信息栏内文字的 y 坐标，使文字底部不超过 bar_h + 2。

推荐方案 A，因为改动最小（一行），且效果立竿见影。

**文件**: `src/app/serial_monitor.cpp:249` — `term_y = bar_h + 3;` 改为 `term_y = bar_h + 5;`

---

## Bug 3: 首次进入时长按 RUN/NORM 无效，弹沙漏并重进

### 症状
刚进入串口监视器时（首次操作），长按 RUN 或 NORM 按钮没有效果。长按会弹出沙漏动画，然后再次进入界面。必须返回主菜单再重新进入才能正常操作。

### 根因
**这是 Bug 1 的连锁反应。**

当 Bug 1 触发时（`in_user_item` 垃圾值为 true），进入流程跳过了 `serial_monitor_init()`：
- `hal_input_set_double_click_enabled(true)` **未被调用** → 双击检测未启用，简单的状态机在运行
- `hal_input_reset_events()` **未被调用** → 按键状态机有前一个操作的残留状态

由于 App 未正常初始化：
- `sm_running`、`sm_debug`、`sm_selected` 依赖静态变量初始化为 0（侥幸正确）
- 但按键状态机状态不可预测，可能导致异常的长按/退出事件

此外，退出流程中 `exiting_user_item` 永远不会被重置为 `false`：

```c
// src/ui/ui_core.c:222-227
if (_selected_user_item->exiting_user_item && g_xerintosh_exit_animation_status == 1)
{
    if (_selected_user_item->exit_function != NULL)
        _selected_user_item->exit_function();
    _selected_user_item->in_user_item = 0;
    // ⚠️ exiting_user_item 未重置为 false
}
```

虽然当前代码中这不直接导致 Bug（因为 `handle_user_item_enter()` 会在下次进入时将其重置），但从状态一致性角度应该修复。

### 修复
1. **主要修复**：Bug 1 修复后，`serial_monitor_init()` 每次都会被正常调用，此问题自动解决。
2. **防御性加固**：在 `xerintosh_ui_main_core()` 退出完成后，将 `exiting_user_item` 置回 `false`。

```c
// src/ui/ui_core.c:227 之后添加：
_selected_user_item->exiting_user_item = false;
```

**文件**: `src/ui/ui_core.c:222-227` — 在 `in_user_item = 0` 之后添加 `_selected_user_item->exiting_user_item = false;`

---

## 模式遵循

| 类型 | 参考 | 模式 |
|------|------|------|
| 初始化 | `slider_item` 显式初始化 `is_confirmed = false` | 派生字段必须在构造函数中显式赋初值 |
| 状态重置 | `hal_input_reset_events()` 模式 | 跨上下文切换时必须调用 reset 系列函数 |
| 防御检查 | `app_input_process()` 双重守卫 | 退出动画期间禁止框架输入 |

## 文件变更清单

| 文件 | 操作 | 原因 |
|------|------|------|
| `src/ui/ui_item.c` | EDIT | 在 `xerintosh_new_user_item()` 中初始化 5 个派生 bool 字段 |
| `src/ui/ui_core.c` | EDIT | 退出完成后重置 `exiting_user_item = false` |
| `src/app/serial_monitor.cpp` | EDIT | 增大 `term_y` 间距修复文字边框重合 |

## 验证

```bash
# 编译检查
pio run -e m5stick-c

# 运行 native 测试
pio test -e native

# 运行特定测试
./.pio/build/native/program --gtest_filter=UserItemTest.*
./.pio/build/native/program --gtest_filter=SerialMonitorTest.*
```

## 风险

| 风险 | 可能性 | 缓解 |
|------|--------|------|
| 修复后退出动画行为变化（exiting_user_item 重置） | 低 | 仅影响退出完成后的状态清理，不影响动画逻辑 |
| info bar 间距调整后终端可见行数减少 1 行 | 低 | term_y 增加 2px 仅减少约 0.25 行高度，用户不可见 |

## 验收标准

- [ ] 主菜单中短按选择「串口监视器」仅选中，不进入 App
- [ ] 长按确认后正常进入串口监视器（带沙漏动画）
- [ ] 终端栏顶部文字不与矩形边框重合
- [ ] 进入串口监视器后，长按 BtnA 可正常切换 RUN/STOP 和 NORM/DEBUG
- [ ] 返回主菜单后再次进入，所有功能正常
- [ ] Native 测试全部通过
