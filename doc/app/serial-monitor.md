# 串口监视器 App（Serial Monitor）

> **Parent:** [App 层索引](index.md) | **Related:** [任务管理器](taskmgr.md), [UI 核心引擎](../ui/core.md), [行列表动画](../ui/ui-anim-row.md)

## 概述

串口监视器是一个 `user_item` 全屏 App，在设备上提供**实时串口数据监视**功能。它通过 USB 串口（`/dev/ttyS0`）或蓝牙串口（BT SPP）接收数据，在 TFT 屏幕上以终端风格显示。

支持：
- 实时数据流显示（终端环形缓冲区）
- 有线串口（SER）和蓝牙串口（BT SPP）双数据源切换
- START/STOP 控制数据捕获
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
- **sm_app**：生命周期管理（init/loop/exit）、输入处理、动画状态更新、BLE 回调
- **sm_buffer**：环形缓冲区，存储终端显示内容，支持 soft-wrap
- **sm_ui**：纯绘制，不持有状态，从全局变量读取坐标和内容

---

## 界面布局

```
┌──────────────────────┐
│ [RUN] RATE:115200 [SER]  │  ← 信息栏（bar_h = HAL_ROW_H()）
├──────────────────────┤
│                      │
│  [Master]: Hello     │  ← 终端区域
│  [Slave]:  World     │
│  ...                 │
│                      │
└──────────────────────┘
```

### 信息栏

| 元素 | 位置 | 说明 |
|------|------|------|
| RUN/STOP 按钮 | 左 | 控制数据捕获启停（选中时 XOR 反色滑块覆盖） |
| 波特率/BLE 状态 | 中 | SER 模式显示 `RATE:115200`；BLE 模式显示 `BLE:OK` 或 `BLE:--` |
| SER/BLE 按钮 | 右 | 切换数据源（Serial / Bluetooth） |

### 终端区域

- **环形缓冲区**：`SM_TERM_LINES` 行（默认 20 行）× 每行 `SM_TERM_LINE_LEN` 字符（默认 64 字符）
- **自动折行**（soft-wrap）：行长超过屏幕宽度时自动换行
- **滚动**：最新数据始终在最底部可见；双击 BtnA 向下滚动，双击 BtnB 向上滚动
- **前缀标识**：`[Master]:`（主机接收，红色）/ `[Slave]:`（MCU 发出，绿色）；BLE 模式下为 `[BT-RX]:` / `[BT-TX]:`

---

## 动画系统（Phase 2）

### 入场滑入动画

进入串口监视器时，整个界面（信息栏 + 终端）从屏幕底部滑入：

*📄 Source: [sm_app.cpp](../../src/app/serial_monitor/sm_app.cpp#L105-L106)*

```c
// init(): 起始位置
sm_entry_offset = (float)SCREEN_HEIGHT;
```

*📄 Source: [sm_app.cpp](../../src/app/serial_monitor/sm_app.cpp#L202)*

```c
// loop(): 每帧更新
xerintosh_animation(&sm_entry_offset, 0.0f, ANIM_SPEED_EXIT);
```

*📄 Source: [sm_ui.c](../../src/app/serial_monitor/sm_ui.c#L30-L31, L159)*

```c
// draw(): 叠加偏移
int16_t entry = (sm_entry_offset < 1.0f) ? 0 : (int16_t)sm_entry_offset;
int16_t bar_y = HAL_MARGIN_SM + entry;
int16_t term_y = HAL_HEADER_BOTTOM() + HAL_MARGIN_SM + entry;
```

缓存优化：当 `sm_entry_offset < 1.0f` 时跳过整数转换和叠加（动画已完成）。

### 按钮选中平滑过渡

替代了原有的闪烁机制。两个按钮（RUN/STOP 和 SER/BLE）各有一个 alpha 值（0.0~100.0）：

*📄 Source: [sm_app.cpp](../../src/app/serial_monitor/sm_app.cpp#L204-L208)*

```c
// 全局动画变量（范围 [0, 100]，确保 xerintosh_animation 差值 > 1.0，触发逐帧动画）
float sm_btn_alpha_0;  // 按钮 0 (RUN/STOP) 的高亮强度
float sm_btn_alpha_1;  // 按钮 1 (SER/BLE) 的高亮强度

// loop(): 每帧更新目标
float trg0 = (sm_selected == 0) ? 100.0f : 0.0f;
float trg1 = (sm_selected == 1) ? 100.0f : 0.0f;
xerintosh_animation(&sm_btn_alpha_0, trg0, ANIM_SPEED_SELECTOR);
xerintosh_animation(&sm_btn_alpha_1, trg1, ANIM_SPEED_SELECTOR);
```

*📄 Source: [sm_ui.c](../../src/app/serial_monitor/sm_ui.c#L69-L72)*

```c
// draw_info_bar(): XOR 反色滑块
float t = sm_btn_alpha_1 / 100.0f;
int16_t slider_x = (int16_t)(start_x + (mode_x - start_x) * t);
int16_t slider_w = (int16_t)(start_w + (mode_w - start_w) * t);
hal_draw_xor_rect(slider_x + 1, bar_y + 1, slider_w - 2, bar_h - 2);
```

**关键设计**：信息栏先统一绘制白色文字，再用 `hal_draw_xor_rect()` 绘制反色滑块覆盖选中区域。滑块内白字变黑、黑底变白，滑块外保持白字，无需分别计算文字颜色。

---

## 操作说明

| 操作 | 按键 | 说明 |
|------|------|------|
| 切换按钮 | BtnA 短按 / BtnB 短按 | RUN/STOP ↔ SER/BLE（任一短按键均可切换） |
| 执行操作 | BtnA 长按 | 当前选中按钮的功能（启动/停止/切换 SER↔BLE） |
| 向下滚动 | BtnA 双击 | 终端向下滚动一行 |
| 向上滚动 | BtnB 双击 | 终端向上滚动一行 |
| 退出 | BtnB 长按 | 返回主菜单 |

---

## 状态变量

*📄 Source: [sm_app.h](../../src/app/serial_monitor/sm_app.h#L28-L42)*

| 变量 | 类型 | 说明 |
|------|------|------|
| `sm_running` | `bool` | 是否正在捕获串口数据 |
| `sm_source` | `sm_source_t` | 当前数据源：`SM_SOURCE_SER` 或 `SM_SOURCE_BLE` |
| `sm_selected` | `uint8_t` | 当前选中按钮（0=RUN/STOP, 1=SER/BLE） |
| `sm_bt_connected` | `bool` | BLE 客户端是否已连接 |
| `sm_buffer` | `sm_buffer_t` | 终端环形缓冲区 |
| `sm_entry_offset` | `float` | 入场滑入偏移（SCREEN_HEIGHT → 0） |
| `sm_btn_alpha_1` | `float` | 按钮 1 高亮强度（0.0~100.0） |

注意：`sm_btn_alpha_0` 是 `sm_app.cpp` 中的 `static` 变量，不在 `sm_app.h` 中暴露。动画变量范围是 `[0, 100]` 而非 `[0, 1]`，以确保 `xerintosh_animation()` 的差值大于 1.0 从而触发逐帧插值。

---

## 数据源切换（SER ↔ BLE）

*📄 Source: [sm_app.cpp](../../src/app/serial_monitor/sm_app.cpp#L150-L181)*

```c
if (event_a == HAL_EVENT_LONG_PRESS) {
    if (sm_selected == 0) {
        sm_running = !sm_running;   // 启动/停止数据捕获
    } else {
        // 切换数据源 SER ↔ BLE
        if (sm_source == SM_SOURCE_SER) {
            if (!bt_mgr_is_enabled()) {
                bt_mgr_request_enable();
                sm_bt_lazy_inited = true;
            }
            sm_source = SM_SOURCE_BLE;
            sm_bt_connected = bt_uart_is_connected();
        } else {
            sm_source = SM_SOURCE_SER;
        }
        sm_buffer_clear(&sm_buffer);  // 切换源时清空缓冲区，避免混淆
    }
}
```

- **SER 模式**：数据来自硬件 `Serial`（USB UART），由 `serial_monitor_update()` 在 `main.cpp` 的 `loop()` 中读取
- **BLE 模式**：数据来自蓝牙 SPP，由 `bt_uart_drain_rx_queue()` 在 `serial_monitor_loop()` 中消费，通过 `sm_on_bt_rx()` 回调写入缓冲区
- **懒加载蓝牙**：首次切换到 BLE 时若蓝牙未启用，自动请求启用；退出串口监视器时若蓝牙是由本 App 懒加载的，自动释放以归还内存

---

## 注册到菜单

*📄 Source: [app_init.c](../../src/app/app_init.c#L154-L155)*

```c
xerintosh_list_item_t* item3 = xerintosh_new_user_item(
    "串口监视器", serial_monitor_init, serial_monitor_loop, serial_monitor_exit, default_icon);
```

---

> **See Also:** [任务管理器](taskmgr.md) | [UI 核心引擎](../ui/core.md) | [行列表动画](../ui/ui-anim-row.md)
