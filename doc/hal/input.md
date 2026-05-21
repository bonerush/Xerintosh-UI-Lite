# 输入系统（HAL Input）

> **Parent:** [知识地图](../index.md) | **Related:** [显示驱动](display.md), [系统时钟](system.md)

## 概述

输入 HAL 负责读取 M5Stick-C 的两个物理按键（BtnA / BtnB），并输出**短按、长按事件**。

与早期版本不同，当前实现已大幅简化：

- **M5Unified 包办底层**：`M5.update()` 在 `main.cpp` 的 `loop()` 中每帧先行调用，负责 GPIO 读取、消抖、边沿检测
- **HAL 层只做事件判断**：接收 `wasPressed()` / `wasReleased()` 边沿信号，用极简状态机判断短按还是长按
- **双击检测已移除**：旧版本的双击切模式逻辑已废弃，`hal_input_get_mode()` 为预留空接口

---

## 关键概念

### 双实现架构

```
┌─────────────────┐     ┌─────────────────────┐
│  NATIVE_TEST    │     │    硬件环境         │
│  所有函数空桩   │     │  M5Unified + 状态机 │
│  返回 NONE    │     │  返回 SHORT/LONG    │
└─────────────────┘     └─────────────────────┘
```

Native 测试环境下所有输入函数返回空状态，便于在桌面端运行 UI 逻辑单元测试而不依赖真实硬件。

### 内部按键状态

*📄 Source: [hal_input.cpp](../../src/hal/hal_input.cpp#L63-L68)*

```c
struct btn_state {
    bool pressed;              /* 是否处于按下态 */
    uint32_t press_time;       /* 按下起始时间 */
    bool long_fired;           /* 长按事件是否已触发 */
    uint32_t press_duration_ms; /* 当前按下持续时间 */
};
```

每个按键（A / B）维护一个 `btn_state` 实例。对比旧版本的 `hal_button_state_t`，字段从 9 个精简到 4 个，消抖计数器、双击窗口、连发时刻等均已移除。

### 核心事件检测

*📄 Source: [hal_input.cpp](../../src/hal/hal_input.cpp#L95-L123)*

```c
static hal_event_t check_button_event(struct btn_state *st,
                                      bool wasPressed, bool wasReleased)
{
  if (wasPressed)
  {
    st->pressed = true;
    st->press_time = millis();
    st->long_fired = false;
    st->press_duration_ms = 0;
  }
  if (wasReleased)
  {
    st->pressed = false;
    st->press_duration_ms = 0;
    if (!st->long_fired)
    {
      return HAL_EVENT_SHORT_PRESS;
    }
  }
  if (st->pressed && !st->long_fired)
  {
    st->press_duration_ms = millis() - st->press_time;
    if (st->press_duration_ms >= LONG_PRESS_DURATION_MS)
    {
      st->long_fired = true;
      return HAL_EVENT_LONG_PRESS;
    }
  }
  return HAL_EVENT_NONE;
}
```

#### 中文伪代码拆解

```
函数 检测按键事件(状态指针, 是否刚按下, 是否刚松开) {
    if (刚按下) {
        标记为按下态
        记录按下时刻
        重置长按标记
    }

    if (刚松开) {
        标记为松开态
        if (长按从未触发) {
            return 短按事件      // 短按在松开时确认
        }
    }

    if (正按住 且 长按未触发) {
        按住时长 = 当前时间 - 按下时刻
        if (按住时长 >= 500ms) {
            标记长按已触发
            return 长按事件      // 长按在按住过程中确认
        }
    }

    return 无事件
}
```

**关键理解**：
- **短按**在松开时确认：只要按住期间没有触发过长按，松开后就是短按
- **长按**在按住过程中确认：一旦达到阈值立即返回，无需等待松开
- **长按触发后松开不再产生短按**：防止同一个按键动作产生两个事件

### 事件输出 API

*📄 Source: [hal_input.cpp](../../src/hal/hal_input.cpp#L128-L143)*

```c
hal_event_t hal_input_get_event(hal_button_t btn)
{
  struct btn_state *st = NULL;
  if (btn == HAL_BTN_A) st = &g_btn_a;
  else if (btn == HAL_BTN_B) st = &g_btn_b;
  else return HAL_EVENT_NONE;

  if (btn == HAL_BTN_A)
  {
    return check_button_event(st, M5.BtnA.wasPressed(), M5.BtnA.wasReleased());
  }
  else
  {
    return check_button_event(st, M5.BtnB.wasPressed(), M5.BtnB.wasReleased());
  }
}
```

#### 中文伪代码拆解

```
函数 获取按键事件(按键编号) {
    找到对应按键的状态结构

    if (按键A) {
        return 检测事件(状态A, M5.BtnA.刚按下, M5.BtnA.刚松开)
    } else {
        return 检测事件(状态B, M5.BtnB.刚按下, M5.BtnB.刚松开)
    }
}
```

**为什么 `M5.update()` 在 main 中先行调用**：M5Unified 的 `wasPressed()` / `wasReleased()` 是边沿敏感 API，它们只在 `M5.update()` 执行后的那一帧返回 true。因此 `main.cpp` 每帧先 `M5.update()`，再 `hal_input_get_event()`，才能捕获到正确的边沿。

### 长按进度查询

*📄 Source: [hal_input.cpp](../../src/hal/hal_input.cpp#L148-L167)*

```c
bool hal_input_is_pressed(hal_button_t btn) {
    struct btn_state *st = NULL;
    if (btn == HAL_BTN_A) {
        st = &g_btn_a;
        bool pressed = M5.BtnA.isPressed();
        if (pressed && st->pressed) {
            st->press_duration_ms = millis() - st->press_time;
        }
        return pressed;
    }
    /* ... BtnB 同理 ... */
}
```

*📄 Source: [hal_input.cpp](../../src/hal/hal_input.cpp#L180-L186)*

```c
uint32_t hal_input_get_press_duration(hal_button_t btn) {
    struct btn_state *st = NULL;
    if (btn == HAL_BTN_A) st = &g_btn_a;
    else if (btn == HAL_BTN_B) st = &g_btn_b;
    else return 0;
    return st->press_duration_ms;
}
```

`hal_input_is_pressed()` 在返回按键当前物理状态的同时，**更新 `press_duration_ms`**。UI 层（如长按提示条）可以每帧调用此函数获取实时进度，再通过 `hal_input_get_press_duration()` 读取具体毫秒数。

### 阈值常量

*📄 Source: [hal_input.cpp](../../src/hal/hal_input.cpp#L58)*

| 常量 | 值 | 含义 |
|------|-----|------|
| `LONG_PRESS_DURATION_MS` | 500 | 长按触发阈值（毫秒） |

旧版本中的 `DEBOUNCE_FRAMES`、`SHORT_PRESS_MS`、`LONG_PRESS_REPEAT_MS`、`DOUBLE_CLICK_MS` 等常量已全部移除，因为消抖和边沿检测已交由 M5Unified 处理。

---

## 与其他组件的关系

- **main.cpp**：每帧调用 `M5.update()` → `hal_input_get_event()` → 将事件传递给 UI 层
- **hal_system**：依赖 `millis()` 获取时间基准
- **ui_item**：`xerintosh_selector_go_next_item()` 等函数消费 `HAL_EVENT_SHORT_PRESS` / `HAL_EVENT_LONG_PRESS`

---

> **See Also:** [显示驱动](display.md) | [项目系统](../ui/item.md) | [核心引擎](../ui/core.md)
