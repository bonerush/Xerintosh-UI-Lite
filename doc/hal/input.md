# 输入系统（HAL Input）

> **Parent:** [知识地图](../index.md) | **Related:** [显示驱动](display.md), [系统时钟](system.md)

## 概述

输入 HAL 负责读取 M5Stick-C 的两个物理按键（BtnA / BtnB），并输出**短按、长按、双击事件**。

当前实现架构：

- **M5Unified 包办底层**：`M5.update()` 在 `main.cpp` 的 `loop()` 中每帧先行调用，负责 GPIO 读取、消抖、边沿检测
- **HAL 层做事件判断**：接收 `wasPressed()` / `wasReleased()` 边沿信号，通过双击检测状态机判断短按/长按/双击
- **双击可开关**：`hal_input_set_double_click_enabled()` 控制是否启用双击检测。菜单模式禁用（即时短按响应）；App 模式可启用以支持双击操作

---

## 关键概念

### 双实现架构

```
┌─────────────────┐     ┌─────────────────────┐
│  NATIVE_TEST    │     │    硬件环境         │
│  测试注入桩     │     │  M5Unified + 状态机 │
│  优先返回注入   │     │  返回 SHORT/LONG/DC │
│  事件，否则 NONE│     │                     │
└─────────────────┘     └─────────────────────┘
```

Native 测试环境下输入函数支持**测试事件注入**：测试代码通过 `hal_test_inject_event()` 注入按键事件，`hal_input_get_event()` 优先返回注入的事件，否则返回 `HAL_EVENT_NONE`。`hal_input_init()` / `hal_input_update()` 为空操作，`hal_input_is_pressed()` 始终返回 `false`。

### 事件类型

*📄 Source: [hal_input.h](../../src/hal/hal_input.h#L34-L39)*

```c
typedef enum {
    HAL_EVENT_NONE = 0,       /* 无事件 */
    HAL_EVENT_SHORT_PRESS,    /* 短按 */
    HAL_EVENT_LONG_PRESS,     /* 长按 */
    HAL_EVENT_DOUBLE_CLICK    /* 双击 */
} hal_event_t;
```

### 内部按键状态

*📄 Source: [hal_input.cpp](../../src/hal/hal_input.cpp#L103-L108)*

```c
struct btn_state {
    hal_input_dc_state_t dc;  /* 双击检测状态机 */
};

static struct btn_state g_btn_a;  /* 按键 A 状态 */
static struct btn_state g_btn_b;  /* 按键 B 状态 */
```

状态机核心字段定义在 `hal_input_dc_state_t` 中：

*📄 Source: [hal_input_double_click.h](../../src/hal/hal_input_double_click.h#L31-L39)*

```c
typedef struct {
    bool pressed;                  /* 是否处于按下态 */
    uint32_t press_time;           /* 本次按下起始时间戳 */
    bool long_fired;               /* 长按事件是否已触发 */
    uint32_t press_duration_ms;    /* 当前按下持续时间 */
    uint32_t last_release_ms;      /* 上次释放的时间戳 */
    bool pending_short_press;      /* 有待处理的短按（等待窗口期确认） */
    bool in_double_click_sequence; /* 当前处于双击序列中的第二次按下 */
} hal_input_dc_state_t;
```

### 核心事件检测

根据 `g_double_click_enabled` 全局开关，选择使用双击状态机或简单状态机：

*📄 Source: [hal_input.cpp](../../src/hal/hal_input.cpp#L137-L144)*

```c
static hal_event_t check_button_event(struct btn_state *st, bool wasPressed, bool wasReleased)
{
    uint32_t now = millis();
    if (g_double_click_enabled) {
        return hal_input_dc_process(&st->dc, wasPressed, wasReleased, now);
    }
    return hal_input_simple_process(&st->dc, wasPressed, wasReleased, now);
}
```

#### 简单状态机（无双击，即时响应）

*📄 Source: [hal_input_double_click.c](../../src/hal/hal_input_double_click.c#L199-L239)*

```c
hal_event_t hal_input_simple_process(hal_input_dc_state_t *st,
                                      bool was_pressed,
                                      bool was_released,
                                      uint32_t now_ms)
{
    if (was_pressed) {
        st->pressed = true;
        st->press_time = now_ms;
        st->long_fired = false;
        st->press_duration_ms = 0;
    }

    if (was_released) {
        st->pressed = false;
        st->press_duration_ms = 0;
        if (!st->long_fired) {
            st->last_release_ms = 0;
            st->pending_short_press = false;
            st->in_double_click_sequence = false;
            return HAL_EVENT_SHORT_PRESS;   // 立即返回，无窗口期延迟
        }
        st->long_fired = false;
        st->last_release_ms = 0;
        st->pending_short_press = false;
        st->in_double_click_sequence = false;
    }

    if (st->pressed && !st->long_fired) {
        st->press_duration_ms = now_ms - st->press_time;
        if (st->press_duration_ms >= LONG_PRESS_DURATION_MS) {
            st->long_fired = true;
            return HAL_EVENT_LONG_PRESS;
        }
    }

    return HAL_EVENT_NONE;
}
```

#### 双击状态机（含 300ms 窗口期）

*📄 Source: [hal_input_double_click.c](../../src/hal/hal_input_double_click.c#L85-L150)*

```c
hal_event_t hal_input_dc_process(hal_input_dc_state_t *st,
                                  bool was_pressed,
                                  bool was_released,
                                  uint32_t now_ms)
{
    /* 第一步：检查是否有超时等待的短按 */
    if (st->pending_short_press &&
        (now_ms - st->last_release_ms) > DOUBLE_CLICK_WINDOW_MS) {
        st->pending_short_press = false;
        st->last_release_ms = 0;
        return HAL_EVENT_SHORT_PRESS;
    }

    if (was_pressed) {
        if (st->pending_short_press &&
            (now_ms - st->last_release_ms) <= DOUBLE_CLICK_WINDOW_MS) {
            st->in_double_click_sequence = true;
            st->pending_short_press = false;
        }
        st->pressed = true;
        st->press_time = now_ms;
        st->long_fired = false;
        st->press_duration_ms = 0;
    }

    if (was_released) {
        st->pressed = false;
        st->press_duration_ms = 0;

        if (!st->long_fired) {
            if (st->in_double_click_sequence) {
                st->in_double_click_sequence = false;
                st->last_release_ms = 0;
                return HAL_EVENT_DOUBLE_CLICK;   // 双击序列完成
            }
            st->pending_short_press = true;     // 第一次释放，等待窗口期
            st->last_release_ms = now_ms;
        } else {
            st->long_fired = false;
            st->pending_short_press = false;
            st->in_double_click_sequence = false;
            st->last_release_ms = 0;
        }
    }

    if (st->pressed && !st->long_fired) {
        st->press_duration_ms = now_ms - st->press_time;
        if (st->press_duration_ms >= LONG_PRESS_DURATION_MS) {
            st->long_fired = true;
            st->pending_short_press = false;
            st->in_double_click_sequence = false;
            st->last_release_ms = 0;
            return HAL_EVENT_LONG_PRESS;
        }
    }

    return HAL_EVENT_NONE;
}
```

#### 中文伪代码拆解（简单状态机）

```
函数 检测按键事件(状态, 是否刚按下, 是否刚松开) {
    if (刚按下) {
        标记为按下态
        记录按下时刻
        重置长按标记
    }

    if (刚松开) {
        标记为松开态
        if (长按从未触发) {
            return 短按事件      // 短按在松开时立即确认
        }
        重置所有标志
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
- **双击**（仅启用时）：300ms 窗口期内两次按下释放触发双击；超时时返回短按

### 事件输出 API

*📄 Source: [hal_input.cpp](../../src/hal/hal_input.cpp#L149-L169)*

```c
hal_event_t hal_input_get_event(hal_button_t btn)
{
  /* 启动保护：忽略开机后首 300ms 内的所有按键事件 */
  if (millis() - g_boot_time_ms < BOOT_INPUT_GUARD_MS) {
      return HAL_EVENT_NONE;
  }

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

**为什么 `M5.update()` 在 main 中先行调用**：M5Unified 的 `wasPressed()` / `wasReleased()` 是边沿敏感 API，它们只在 `M5.update()` 执行后的那一帧返回 true。因此 `main.cpp` 每帧先 `M5.update()`（在 `hal_input_update()` 内），再 `hal_input_get_event()`，才能捕获到正确的边沿。

### 长按进度查询

*📄 Source: [hal_input.cpp](../../src/hal/hal_input.cpp#L174-L193)*

```c
bool hal_input_is_pressed(hal_button_t btn) {
    struct btn_state *st = NULL;
    if (btn == HAL_BTN_A) {
        st = &g_btn_a;
        bool pressed = M5.BtnA.isPressed();
        if (pressed && st->dc.pressed) {
            st->dc.press_duration_ms = millis() - st->dc.press_time;
        }
        return pressed;
    }
    if (btn == HAL_BTN_B) {
        st = &g_btn_b;
        bool pressed = M5.BtnB.isPressed();
        if (pressed && st->dc.pressed) {
            st->dc.press_duration_ms = millis() - st->dc.press_time;
        }
        return pressed;
    }
    return false;
}
```

*📄 Source: [hal_input.cpp](../../src/hal/hal_input.cpp#L198-L204)*

```c
uint32_t hal_input_get_press_duration(hal_button_t btn) {
    struct btn_state *st = NULL;
    if (btn == HAL_BTN_A) st = &g_btn_a;
    else if (btn == HAL_BTN_B) st = &g_btn_b;
    else return 0;
    return st->dc.press_duration_ms;
}
```

`hal_input_is_pressed()` 在返回按键当前物理状态的同时，**更新 `press_duration_ms`**。UI 层（如长按提示条）可以每帧调用此函数获取实时进度，再通过 `hal_input_get_press_duration()` 读取具体毫秒数。

### 阈值常量

*📄 Source: [hal_input_double_click.h](../../src/hal/hal_input_double_click.h#L23-L24)*

| 常量 | 值 | 含义 |
|------|-----|------|
| `LONG_PRESS_DURATION_MS` | 500 | 长按触发阈值（毫秒） |
| `DOUBLE_CLICK_WINDOW_MS` | 300 | 双击窗口期（毫秒） |

*📄 Source: [hal_input.cpp](../../src/hal/hal_input.cpp#L95)*

| 常量 | 值 | 含义 |
|------|-----|------|
| `BOOT_INPUT_GUARD_MS` | 300 | 启动后首 300ms 忽略输入，防止 GPIO 上电毛刺 |

### 双击开关与重置

*📄 Source: [hal_input.h](../../src/hal/hal_input.h#L80-L92)*

```c
void hal_input_set_double_click_enabled(bool enabled);
void hal_input_reset_events(void);
```

- `hal_input_set_double_click_enabled(true)`：启用双击检测（含 300ms 窗口延迟）。App 模式（如串口监视器）中可启用以支持双击滚动。
- `hal_input_set_double_click_enabled(false)`：禁用双击，短按即时响应。菜单导航中禁用，避免延迟感。
- `hal_input_reset_events()`：在 `user_item` 的 `init/exit` 中调用，清除跨模式的残留状态，防止进入/退出 App 时按键边沿丢失导致的僵死状态。

---

## 与其他组件的关系

- **main.cpp**：每帧调用 `M5.update()` → `hal_input_update()` → `hal_input_get_event()` → 将事件传递给 UI 层
- **hal_system**：硬件实现依赖 `millis()` 获取时间基准
- **ui_item**：`xerintosh_selector_go_next_item()` 等函数消费 `HAL_EVENT_SHORT_PRESS` / `HAL_EVENT_LONG_PRESS`

---

> **See Also:** [显示驱动](display.md) | [项目系统](../ui/item.md) | [核心引擎](../ui/core.md)
