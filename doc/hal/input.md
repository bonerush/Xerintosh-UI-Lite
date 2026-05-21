# 输入系统（HAL Input）

> **Parent:** [知识地图](../index.md) | **Related:** [显示驱动](display.md), [系统时钟](system.md)

## 概述

输入 HAL 负责读取 M5Stick-C 的两个物理按键（BtnA / BtnB），并输出**高级事件**：短按、长按、双击切换模式。

核心挑战是**机械按键抖动**和**多模式切换**。本模块采用“原始读取 → 消抖 → 状态机 → 事件输出”四级流水线。

---

## 关键概念

### 按键状态结构

*📄 Source: [hal_input.h](../../src/hal/hal_input.h#L27-L37)*

```c
typedef struct {
    bool pressed;           // 当前是否按下
    bool mode;              // 0=模式1, 1=模式2（双击切换）
    uint32_t pressTime;     // 本次按下时刻
    uint32_t lastReleaseTime; // 上次松开时刻（用于双击检测）
    uint8_t debounceCount;  // 消抖计数器
    bool debouncedState;    // 消抖后的稳定状态
    bool lastRawState;      // 上一次的原始电平
    bool longPressFired;    // 本次长按是否已经触发过
    uint32_t lastRepeatTime; // 长按连发时刻
} hal_button_state_t;
```

每个按键维护一个这样的状态机实例，所有时间单位均为毫秒（`hal_get_ticks()`）。

### 消抖逻辑

*📄 Source: [hal_input.cpp](../../src/hal/hal_input.cpp#L40-L56)*

```c
static void hal_input_update_button(hal_button_t btn) {
    hal_button_state_t* state = &g_buttons[btn];
    bool raw = hal_input_read_raw(btn);

    // Debounce: require 3 consecutive same readings
    if (raw == state->lastRawState) {
        if (state->debounceCount < DEBOUNCE_FRAMES) {
            state->debounceCount++;
        }
    } else {
        state->debounceCount = 0;
        state->lastRawState = raw;
    }

    bool newState = (state->debounceCount >= DEBOUNCE_FRAMES)
                        ? state->lastRawState
                        : state->debouncedState;
    bool changed = (newState != state->debouncedState);
    state->debouncedState = newState;
```

#### 中文伪代码拆解

```
函数 更新按键(按键编号) {
    原始电平 = 读取GPIO(按键编号)

    // 第一步：消抖计数
    if (原始电平 == 上一次原始电平) {
        消抖计数++
    } else {
        消抖计数 = 0
        上一次原始电平 = 原始电平
    }

    // 第二步：生成稳定状态
    if (消抖计数 >= 3帧) {
        稳定状态 = 原始电平
    } else {
        稳定状态 = 保持原状态   // 变化被忽略
    }
}
```

**为什么需要 3 帧**：机械按键按下/松开时电平会在几毫秒内反复跳动。连续 3 帧读到相同值才认为是真实状态，可有效消除抖动。

### 事件检测

*📄 Source: [hal_input.cpp](../../src/hal/hal_input.cpp#L58-L101)*

```c
uint32_t now = hal_get_ticks();

if (changed) {
    if (newState) {
        // 刚按下
        state->pressTime = now;
        state->longPressFired = false;
        state->lastRepeatTime = now;
        state->pressed = true;
    } else {
        // 刚松开
        state->pressed = false;
        uint32_t duration = now - state->pressTime;
        if (duration < SHORT_PRESS_MS) {
            // 检测双击
            if ((now - state->lastReleaseTime) < DOUBLE_CLICK_MS) {
                state->mode = !state->mode;
                state->lastReleaseTime = 0;
            } else {
                state->lastReleaseTime = now;
            }
        }
    }
} else {
    if (newState) {
        uint32_t duration = now - state->pressTime;
        if (duration >= LONG_PRESS_MS) {
            if (!state->longPressFired) {
                state->longPressFired = true;
                state->lastRepeatTime = now;
            } else if ((now - state->lastRepeatTime) >= LONG_PRESS_REPEAT_MS) {
                state->lastRepeatTime = now;
            }
        }
    } else {
        // 松开状态：衰减双击窗口
        if (state->lastReleaseTime != 0 &&
            (now - state->lastReleaseTime) >= DOUBLE_CLICK_MS) {
            state->lastReleaseTime = 0;
        }
    }
}
```

#### 中文伪代码拆解

```
函数 状态机(稳定状态是否变化) {
    当前时间 = 获取毫秒Tick()

    if (状态发生变化) {
        if (变为按下) {
            记录按下时刻
            重置长按标记
        } else {
            // 变为松开
            按住时长 = 当前时间 - 按下时刻

            if (按住时长 < 200ms) {
                // 可能是短按，也可能是双击的一部分
                if (距离上次松开 < 300ms) {
                    模式 = !模式      // 双击成功，切换模式
                    上次松开时刻 = 0   // 防止三连击
                } else {
                    上次松开时刻 = 当前时间
                }
            }
        }
    } else {
        if (当前处于按住状态) {
            按住时长 = 当前时间 - 按下时刻
            if (按住时长 >= 500ms) {
                if (还没触发过长按) {
                    标记长按已触发
                } else if (距离上次连发 >= 100ms) {
                    记录本次连发时刻    // 支持长按连续触发
                }
            }
        } else {
            // 持续松开：如果双击窗口超时，清零窗口
            if (上次松开时刻 != 0 且 距离上次松开 >= 300ms) {
                上次松开时刻 = 0
                // 此时上层会收到 HAL_EVENT_SHORT_PRESS
            }
        }
    }
}
```

### 事件输出 API

*📄 Source: [hal_input.cpp](../../src/hal/hal_input.cpp#L108-L141)*

```c
hal_event_t hal_input_get_event(hal_button_t btn) {
    hal_button_state_t* state = &g_buttons[btn];

    if (!state->debouncedState) {
        // 松开状态：检查是否有待派发的短按
        if (state->lastReleaseTime != 0) {
            uint32_t now = hal_get_ticks();
            if ((now - state->lastReleaseTime) >= DOUBLE_CLICK_MS) {
                state->lastReleaseTime = 0;
                return HAL_EVENT_SHORT_PRESS;
            }
        }
        return HAL_EVENT_NONE;
    }

    // 按住状态
    uint32_t now = hal_get_ticks();
    uint32_t duration = now - state->pressTime;

    if (duration >= LONG_PRESS_MS) {
        if (state->longPressFired && (now - state->lastRepeatTime) >= LONG_PRESS_REPEAT_MS) {
            state->lastRepeatTime = now;
            return HAL_EVENT_LONG_PRESS;
        }
        if (!state->longPressFired) {
            state->longPressFired = true;
            state->lastRepeatTime = now;
            return HAL_EVENT_LONG_PRESS;
        }
    }

    return HAL_EVENT_NONE;
}
```

#### 中文伪代码拆解

```
函数 获取按键事件(按键编号) {
    if (按键处于松开状态) {
        if (存在待确认的短按) {
            if (双击窗口已超时) {
                清空待确认标记
                return 短按事件
            }
        }
        return 无事件
    }

    // 按键正被按住
    按住时长 = 当前时间 - 按下时刻

    if (按住时长 >= 500ms) {
        if (已触发过长按 且 到达连发间隔) {
            更新连发时刻
            return 长按事件      // 连续触发
        }
        if (从未触发过长按) {
            标记已触发
            return 长按事件      // 首次触发
        }
    }

    return 无事件
}
```

### 阈值常量

*📄 Source: [hal_input.cpp](../../src/hal/hal_input.cpp#L18-L22)*

| 常量 | 值 | 含义 |
|------|-----|------|
| `DEBOUNCE_FRAMES` | 3 | 消抖需要连续 3 帧相同读数 |
| `SHORT_PRESS_MS` | 200 | 短按上限（超过则不是短按） |
| `LONG_PRESS_MS` | 500 | 长按触发阈值 |
| `LONG_PRESS_REPEAT_MS` | 100 | 长按连发间隔 |
| `DOUBLE_CLICK_MS` | 300 | 双击判定窗口 |

---

## 与其他组件的关系

- **main.cpp**：每帧调用 `hal_input_update()` → 读取 `hal_input_get_event()`
- **hal_system**：依赖 `hal_get_ticks()` 获取时间基准
- **ui_item**：`xerintosh_selector_go_next_item()` 等函数消费按键事件

---

> **See Also:** [显示驱动](display.md) | [项目系统](../ui/item.md) | [核心引擎](../ui/core.md)
