# 关机模块

> **Parent:** [App 层索引](index.md) | **Source:** [shutdown_screen.c](../../src/app/shutdown/shutdown_screen.c), [power_key_popup.c](../../src/app/shutdown/power_key_popup.c)

## 概述

关机模块由两个子模块组成：

- **shutdown_screen** — 复用 Xerintosh logo 绘制关机画面（"GOOD BYE!"），延时 2 秒后进入 ESP32 深度睡眠
- **power_key_popup** — 检测 A+B 双键同时长按 3 秒，显示倒计时弹窗，超时后触发硬件断电

M5StickC 的电源键仅连接到 AXP192 PMU，无法通过软件直接检测。因此采用 A+B 双键长按作为替代关机方案。

---

## 架构

```
app_input_process()  每帧调用
        │
        ▼
power_key_popup_update()
        │
        ├─ A+B 双键同时按下？
        │   ├─ 是 → 记录起始时间，显示倒计时弹窗
        │   │       持续 ≥ 3s → shutdown_screen_show() → hal_power_off_hw()
        │   └─ 否 → 松手冷却 300ms，恢复正常输入
        │
        └─ power_key_popup_is_dual_active() == true 时
            隔离正常按钮事件（防止误触导航）
```

*📄 Source: [power_key_popup.c](../../src/app/shutdown/power_key_popup.c#L48-L99)*

---

## API 参考

### shutdown_screen

| 函数 | 签名 | 说明 |
|------|------|------|
| `shutdown_screen_show()` | `(void) → void` | 显示关机画面（logo + "GOOD BYE!"），延时 2 秒 |
| `shutdown_screen_power_off()` | `(void) → void` | 进入 ESP32 深度睡眠（native 环境为空操作） |

*📄 Source: [shutdown_screen.h](../../src/app/shutdown/shutdown_screen.h)*

### power_key_popup

| 函数 | 签名 | 说明 |
|------|------|------|
| `power_key_popup_init()` | `(void) → void` | 初始化弹窗状态（重置所有内部标志） |
| `power_key_popup_update()` | `(void) → void` | 每帧调用，检测双键并管理弹窗生命周期 |
| `power_key_popup_is_visible()` | `(void) → bool` | 查询弹窗是否正在显示 |
| `power_key_popup_is_dual_active()` | `(void) → bool` | 查询是否处于双键模式或松手冷却期（用于隔离正常按钮事件） |

*📄 Source: [power_key_popup.h](../../src/app/shutdown/power_key_popup.h)*

---

## 关键实现细节

### 双键检测状态机

```
idle ──(A+B 同时按下)──→ 双键模式
                              │
                  ┌───────────┼───────────┐
                  ▼           ▼           ▼
             显示弹窗     持续 ≥ 3s    松手
             倒计时       触发关机     → 冷却 300ms
                                    → dismiss 弹窗
                                    → 重置状态
```

- **冷却期**（`DUAL_RELEASE_COOLDOWN_MS = 300ms`）：松手后短暂隔离输入，防止双键松开时的残留按键事件误触导航
- **防重复触发**：`g_dual_shutdown_triggered` 标志确保关机只触发一次

*📄 Source: [power_key_popup.c](../../src/app/shutdown/power_key_popup.c#L26-L31)*

### 关机画面复用开机 logo

`shutdown_screen_show()` 复用 `boot_screen_draw_logo()` 绘制 Macintosh 风格 logo，仅将底部文字从开机信息改为 "GOOD BYE!"。

*📄 Source: [shutdown_screen.c](../../src/app/shutdown/shutdown_screen.c#L24-L45)*

---

## 与其他模块的关系

- **hal_power_key / hal_power_off**：底层硬件电源键检测与 AXP192 断电封装
- **boot_screen**：复用 logo 绘制函数
- **app_input**：每帧调用 `power_key_popup_update()`，并根据 `power_key_popup_is_dual_active()` 隔离正常按钮事件
- **ui_item**：使用 `xerintosh_push_pop_up()` / `xerintosh_dismiss_pop_up()` 管理倒计时弹窗

---

> **See Also:** [开机画面](index.md#开机画面) | [App 层索引](index.md)
