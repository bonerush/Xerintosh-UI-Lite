# 串口监视器 App（Serial Monitor）

> **Parent:** [App 层索引](index.md) | **Related:** [任务管理器](taskmgr.md), [UI 核心引擎](../ui/core.md), [行列表动画](../ui/ui-anim-row.md)

## 概述

串口监视器是一个 `user_item` 全屏 App，在设备上提供**实时串口数据监视**功能。它通过 USB 串口（`/dev/ttyS0`）接收数据，在 TFT 屏幕上以终端风格显示。

支持：
- 实时数据流显示（终端环形缓冲区）
- START/STOP 控制数据捕获
- NORM/DEBUG 模式切换（显示原始字节 vs ASCII）
- 入场滑入动画（Phase 2 新增）
- 按钮选中平滑过渡动画（Phase 2 新增）

*📄 Source: [sm_app.h](../../src/app/serial_monitor/sm_app.h), [sm_app.cpp](../../src/app/serial_monitor/sm_app.cpp), [sm_ui.c](../../src/app/serial_monitor/sm_ui.c), [sm_buffer.c/h](../../src/app/serial_monitor/sm_buffer.c/h)*

---

## 架构

```
sm_app.cpp (状态机 + 主循环)
├── sm_app.h             ← 全局状态变量（shared state）
├── sm_buffer.c/h        ← 终端环形缓冲区（数据层）
├── sm_ui.c/h            ← 绘制实现（信息栏 + 终端区域）
└── ui_core.h            ← xerintosh_animation() 缓动函数
```

职责分离：
- **sm_app**：生命周期管理（init/loop/exit）、输入处理、动画状态更新
- **sm_buffer**：环形缓冲区，存储终端显示内容，支持 soft-wrap
- **sm_ui**：纯绘制，不持有状态，从全局变量读取坐标和内容

---

## 界面布局

```
┌──────────────────────┐
│ [RUN] 115200 [NORM]  │  ← 信息栏（bar_h = font_h + 1）
├──────────────────────┤
│                      │
│  Hello World\r\n     │  ← 终端区域（term_h = SCREEN_HEIGHT - bar_h - 5）
│  > help\r\n          │     字体高 12px，可见 ~8 行（横屏）
│  ...                 │
│                      │
└──────────────────────┘
```

### 信息栏

| 元素 | 位置 | 说明 |
|------|------|------|
| START/STOP 按钮 | 左 | 控制数据捕获启停（选中时白底黑字） |
| 波特率 | 中 | 显示当前串口波特率（如 `115200`） |
| NORM/DBG 按钮 | 右 | 切换显示模式（Normal / Debug） |

### 终端区域

- **环形缓冲区**：`SM_TERM_LINES` 行 × 每行 `SM_LINE_BUF_SIZE` 字符
- **自动折行**（soft-wrap）：行长超过屏幕宽度时自动换行
- **滚动**：最新数据始终在最底部可见
- **底部留白**：2px 边距，防止最后一行被截断

---

## 动画系统（Phase 2）

### 入场滑入动画

进入串口监视器时，整个界面（信息栏 + 终端）从屏幕底部滑入：

```c
// 全局动画变量
float sm_entry_offset;  // SCREEN_HEIGHT → 0，叠加到所有 Y 坐标

// init(): 起始位置
sm_entry_offset = (float)SCREEN_HEIGHT;

// loop(): 每帧更新
xerintosh_animation(&sm_entry_offset, 0.0f, ANIM_SPEED_EXIT);

// draw(): 叠加偏移
int16_t entry = (sm_entry_offset < 1.0f) ? 0 : (int16_t)sm_entry_offset;
bar_y = 1 + entry;
term_y = bar_h + 5 + entry;
```

缓存优化：当 `sm_entry_offset < 1.0f` 时跳过整数转换和叠加（动画已完成）。

### 按钮选中平滑过渡

替代了原有的 500ms 周期闪烁机制。两个按钮（START/STOP 和 NORM/DBG）各有一个 `alpha` 值（0.0~1.0）：

```c
// 全局动画变量
float sm_btn_alpha_0;  // 按钮 0 (START/STOP) 的高亮强度
float sm_btn_alpha_1;  // 按钮 1 (NORM/DBG) 的高亮强度

// loop(): 每帧更新目标
float trg0 = (sm_selected == 0) ? 1.0f : 0.0f;
float trg1 = (sm_selected == 1) ? 1.0f : 0.0f;
xerintosh_animation(&sm_btn_alpha_0, trg0, ANIM_SPEED_SELECTOR_H);
xerintosh_animation(&sm_btn_alpha_1, trg1, ANIM_SPEED_SELECTOR_H);

// draw_button(): 阈值判断
bool is_selected = (alpha > 0.5f);
if (is_selected) {
    bg_color = COLOR_FG;   // 白底
    text_color = COLOR_BG;  // 黑字
} else {
    bg_color = COLOR_BG;   // 黑底
    text_color = COLOR_FG;  // 白字
}
```

**关键修复**（Phase 2 对抗审查发现）：原有代码的 `if/else` 分支颜色值完全相同（白底黑字 vs 白底黑字），导致选中/未选中按钮在视觉上无法区分。修复后两个分支的颜色对精确互逆：`{FG,BG}` ↔ `{BG,FG}`。

---

## 操作说明

| 操作 | 按键 | 说明 |
|------|------|------|
| 切换按钮 | BtnA 短按 | START/STOP ↔ NORM/DBG |
| 执行操作 | BtnA 长按 | 当前选中按钮的功能（启动/停止/切换模式） |
| 退出 | BtnB 长按 | 返回主菜单 |

---

## 状态变量

| 变量 | 类型 | 说明 |
|------|------|------|
| `sm_running` | `bool` | 是否正在捕获串口数据 |
| `sm_debug` | `bool` | 是否处于 DEBUG 模式（显示原始字节） |
| `sm_selected` | `uint8_t` | 当前选中按钮（0=START/STOP, 1=NORM/DBG） |
| `sm_blink_tick` | `uint32_t` | 闪烁计时器（保留向后兼容，动画模式不再使用） |
| `sm_blink_on` | `bool` | 闪烁相位（保留向后兼容） |
| `sm_buffer` | `sm_buffer_t` | 终端环形缓冲区 |
| `sm_entry_offset` | `float` | 入场滑入偏移（Phase 2） |
| `sm_btn_alpha_0` | `float` | 按钮 0 高亮强度（Phase 2） |
| `sm_btn_alpha_1` | `float` | 按钮 1 高亮强度（Phase 2） |

---

## 注册到菜单

在 `app_init.c` 中：

```c
xerintosh_list_item_t* sm_item = xerintosh_new_user_item(
    "Serial Monitor", serial_monitor_init, serial_monitor_loop,
    serial_monitor_exit, user_icon);
```

---

> **See Also:** [任务管理器](taskmgr.md) | [UI 核心引擎](../ui/core.md) | [行列表动画](../ui/ui-anim-row.md)
