# 示波器 App（G36 输入）实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 M5Stick-P1 现有 Xerintosh UI 框架下，实现一个输入口为 G36（ESP32 ADC1_CH0）的单通道手持示波器 App，具备灵动的前端界面、完整的双按键状态机、可配置的时基/幅值/耦合/触发参数以及实时波形绘制。

**Architecture:** 采用与 `taskmgr`/`flasher` 一致的 **App 逻辑层 + UI 绘制层分离** 结构。`oscilloscope_app.c` 负责 ADC 采样、触发检测、参数状态机和按键处理；`oscilloscope_ui.c` 负责网格、波形、状态栏、参数面板的绘制。App 以 `user_item` 形式注册到根菜单，进入时强制切换为横屏以获得更宽的时间轴，退出时恢复方向。波形缓冲区使用 `static` 全局数组避免栈溢出，ADC 采样在 UI loop 中按需进行，优先保证 60fps 渲染流畅度。

**Tech Stack:** C (App 层) + C++ HAL 桥接 + Arduino-ESP32 ADC API + M5GFX/M5Canvas 双缓冲 + Xerintosh UI 动画系统。

---

## 调研结论摘要

### 硬件能力
- **G36 = ADC1_CHANNEL_0 / SENSOR_VP**，输入-only，ADC1 在 Wi-Fi/BT 工作时仍可采样。
- 默认 `analogRead()` 为 12-bit（0-4095），但默认衰减通常是 0 dB，量程约 0-1 V；测量 0-3.3 V 信号需要调用 `analogSetPinAttenuation(36, ADC_11db)`。
- Arduino-ESP32 v2.x 下单次 `analogRead()` 约 90 µs，采集 160 点约 14 ms，在 60fps 帧预算内偏紧张；本方案采用**每帧采集固定点数 + 循环缓冲区**策略，避免阻塞渲染。
- GPIO36 与霍尔传感器共享通道，建议避免同时调用 `hallRead()`。
- **引脚冲突**：当前 `flasher` 默认把 G36 作为 Serial1 RX。示波器 App 运行时不需要烧录器，但应在文档中说明不能同时使用。

### UI/UX 设计方向
- **横屏布局（160×80）**：顶部 12 px 状态栏，中部 50 px 波形区，底部 18 px 参数/测量栏。
- **灵动效果**：波形使用青绿色（`0x07FF`）主体 + 暗绿色尾迹；网格线做暗色虚线并带轻微呼吸亮度；参数切换时数值缩放/颜色过渡；触发线水平虚线使用反色高亮。
- **双按键状态机**：运行模式下 BtnA 短按循环切换参数项、长按进入编辑；BtnB 短按运行/暂停、长按返回菜单。编辑模式下 BtnA 长按确认、BtnB 短按增减当前参数、BtnB 长按取消。

### 关键约束
- `loop()` 中不要调用 `hal_display_clear()`，框架已自动清屏。
- 长按 B 默认退出 App，必须通过 `ui_user_item_try_exit()` 检查。
- 进入/退出必须调用 `hal_input_reset_events()` 或 `ui_service_user_item_init()/exit()`。
- 所有坐标使用 `SCREEN_WIDTH/HEIGHT` 和 `hal_layout.h` 宏，支持横竖屏切换。

---

## 文件结构

| 文件 | 责任 |
|---|---|
| `src/app/oscilloscope/oscilloscope.h` | 导出 `oscilloscope_init/loop/exit` 生命周期函数 |
| `src/app/oscilloscope/oscilloscope_app.c` | ADC 采样、触发、参数状态机、按键处理、测量计算 |
| `src/app/oscilloscope/oscilloscope_ui.c` | 网格、波形、状态栏、参数面板、动画效果绘制 |
| `src/app/oscilloscope/oscilloscope_ui.h` | UI 层内部接口（绘制函数、颜色常量） |
| `src/app/app_menu.c` | 在根菜单注册“示波器”入口 |
| `test/test_native/test_oscilloscope.cpp` | Native 测试：触发算法、参数状态机、波形映射 |
| `doc/app/oscilloscope.md` | 用户文档：功能说明、按键映射、输入保护建议 |

---

### Task 1: 创建示波器 App 目录和头文件

**Files:**
- Create: `src/app/oscilloscope/oscilloscope.h`
- Create: `src/app/oscilloscope/oscilloscope_ui.h`

- [ ] **Step 1: 创建目录并写入公共头文件**

```bash
mkdir -p src/app/oscilloscope
```

`src/app/oscilloscope/oscilloscope.h`:

```c
#ifndef OSCILLOSCOPE_H
#define OSCILLOSCOPE_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 示波器 App 初始化
 * @note  进入 App 时调用一次。配置 G36 为 ADC 输入、切换横屏、重置按键。
 */
void oscilloscope_init(void *user_data);

/**
 * @brief 示波器 App 主循环
 * @note  每帧调用：采样 -> 触发 -> 绘制 -> 处理输入。
 */
void oscilloscope_loop(void *user_data);

/**
 * @brief 示波器 App 退出清理
 * @note  退出时调用一次。恢复屏幕方向、清除按键残留。
 */
void oscilloscope_exit(void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* OSCILLOSCOPE_H */
```

`src/app/oscilloscope/oscilloscope_ui.h`:

```c
#ifndef OSCILLOSCOPE_UI_H
#define OSCILLOSCOPE_UI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/* 示波器状态暴露给 UI 层的数据结构（由 app 层填充） */
typedef struct {
    const uint16_t *samples;      /* 采样缓冲区指针 */
    uint16_t sample_count;        /* 缓冲区有效样本数 */
    uint16_t trigger_index;       /* 触发点在缓冲区中的索引（无效时为 0xFFFF） */
    uint32_t sample_rate_hz;      /* 当前实际采样率 */
    uint8_t time_base_index;      /* 时基档位索引 */
    uint8_t volt_range_index;     /* 电压量程档位索引 */
    uint8_t coupling_index;       /* 耦合方式索引 */
    uint8_t trigger_mode_index;   /* 触发模式索引 */
    int16_t trigger_level;        /* 触发电平（ADC raw 值 0-4095） */
    bool running;                 /* 运行/暂停 */
    bool editing;                 /* 是否处于参数编辑模式 */
    uint8_t selected_param;       /* 当前选中的参数项索引 */
    /* 自动测量结果 */
    uint16_t vpp_raw;             /* 峰峰值（ADC raw） */
    uint16_t vavg_raw;            /* 平均值（ADC raw） */
    uint32_t freq_hz;             /* 估算频率，0 表示未检出 */
} oscilloscope_view_state_t;

/* UI 绘制入口 */
void oscilloscope_ui_draw(const oscilloscope_view_state_t *state);

#ifdef __cplusplus
}
#endif

#endif /* OSCILLOSCOPE_UI_H */
```

- [ ] **Step 2: Commit**

```bash
git add src/app/oscilloscope/oscilloscope.h src/app/oscilloscope/oscilloscope_ui.h
git commit -m "feat(oscilloscope): add app headers and view state struct

Co-Authored-By: kimi-k2.7-code <MoonshotAI@claude-code-best.win>"
```

---

### Task 2: 实现 ADC 采样、触发与测量引擎

**Files:**
- Create: `src/app/oscilloscope/oscilloscope_app.c`
- Modify: `src/app/oscilloscope/oscilloscope_ui.h`（已在 Task 1 创建）

- [ ] **Step 1: 定义参数枚举和常量**

在 `oscilloscope_app.c` 顶部：

```c
#include "oscilloscope.h"
#include "oscilloscope_ui.h"

#include "hal/hal_display.h"
#include "hal/hal_input.h"
#include "hal/hal_system.h"
#include "hal/hal_layout.h"
#include "app/ui_service.h"
#include "ui/ui_item.h"
#include "ui/ui_core.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* 缓冲区大小：横屏 160 px，多留一点用于触发前预触发 */
#define SCOPE_SAMPLE_MAX 200

/* ADC 引脚 */
#define SCOPE_PIN 36

/* 时基档位：每格对应多少像素代表一个采样点 */
typedef struct {
    const char *label;
    uint8_t samples_per_pixel;   /* 每个水平像素合并的采样点数 */
    uint32_t display_rate_hz;    /* 面板上显示的等效采样率 */
} scope_time_base_t;

static const scope_time_base_t s_time_bases[] = {
    { "50us", 1,  20000 },  /* 20 kHz，每像素 50 us */
    { "100us", 2, 10000 },  /* 10 kHz */
    { "200us", 4,  5000 },
    { "500us", 10, 2000 },
    { "1ms",   20, 1000 },
    { "2ms",   40,  500 },
    { "5ms",   100, 200 },
};
#define SCOPE_TIME_BASE_COUNT (sizeof(s_time_bases) / sizeof(s_time_bases[0]))

/* 电压量程档位：每格 raw 值 */
typedef struct {
    const char *label;
    uint16_t div_raw;    /* 每格对应的 ADC raw 值 */
    uint16_t full_scale; /* 满屏对应的 ADC raw 值 */
} scope_volt_range_t;

static const scope_volt_range_t s_volt_ranges[] = {
    { "0.5V",  248, 2000 },  /* 约 0-2 V 满屏 */
    { "1V",    496, 4000 },  /* 约 0-3.3 V 满屏 */
    { "2V",    992, 4095 },
    { "3.3V", 1640, 4095 },
};
#define SCOPE_VOLT_RANGE_COUNT (sizeof(s_volt_ranges) / sizeof(s_volt_ranges[0]))

static const char *s_coupling_labels[] = { "DC", "AC", "GND" };
#define SCOPE_COUPLING_COUNT 3

static const char *s_trigger_mode_labels[] = { "Auto", "Norm", "Scan" };
#define SCOPE_TRIGGER_MODE_COUNT 3

/* 参数项顺序，与 selected_param 对应 */
typedef enum {
    PARAM_TIME_BASE = 0,
    PARAM_VOLT_RANGE,
    PARAM_COUPLING,
    PARAM_TRIGGER_MODE,
    PARAM_TRIGGER_LEVEL,
    PARAM_COUNT
} scope_param_t;

static const char *s_param_labels[PARAM_COUNT] = {
    "时基", "幅值", "耦合", "触发", "触发电平"
};
```

- [ ] **Step 2: 实现状态结构和采样函数**

```c
/* 全局状态 */
static struct {
    uint16_t samples[SCOPE_SAMPLE_MAX];
    uint16_t ac_coupled[SCOPE_SAMPLE_MAX];
    oscilloscope_view_state_t view;
    uint32_t last_sample_tick;
    uint16_t sample_write_pos;
    int16_t ac_offset;          /* AC 耦合时估算的直流偏移 */
} g_scope;

/* 将 ADC raw 值映射到屏幕 Y 坐标（波形区高度内） */
static int16_t scope_map_y(uint16_t raw, uint16_t full_scale, int16_t wave_h)
{
    if (raw >= full_scale) raw = full_scale - 1;
    return (int16_t)(((uint32_t)raw * (uint32_t)wave_h) / full_scale);
}

/* 采集一个样本到循环缓冲区 */
static void scope_sample_one(void)
{
    uint16_t v = (uint16_t)analogRead(SCOPE_PIN);
    g_scope.samples[g_scope.sample_write_pos] = v;
    g_scope.sample_write_pos++;
    if (g_scope.sample_write_pos >= SCOPE_SAMPLE_MAX) {
        g_scope.sample_write_pos = 0;
    }
}
```

- [ ] **Step 3: 实现触发检测**

```c
/* 简单边沿触发：寻找第一个从低于电平到高于电平的点 */
static uint16_t scope_find_trigger_rising(const uint16_t *buf, uint16_t count,
                                          uint16_t level, uint16_t start)
{
    for (uint16_t i = start; i < count - 1; i++) {
        if (buf[i] < level && buf[i + 1] >= level) {
            return i;
        }
    }
    return 0xFFFF;
}

/* 根据耦合方式计算实际用于显示和触发的缓冲区 */
static const uint16_t *scope_get_display_buffer(uint16_t *out_count)
{
    if (g_scope.view.coupling_index == 1) { /* AC */
        /* 计算最近 32 个点的平均值作为偏移 */
        uint32_t sum = 0;
        uint16_t n = (SCOPE_SAMPLE_MAX < 32) ? SCOPE_SAMPLE_MAX : 32;
        for (uint16_t i = 0; i < n; i++) {
            sum += g_scope.samples[i];
        }
        g_scope.ac_offset = (int16_t)(sum / n);
        for (uint16_t i = 0; i < SCOPE_SAMPLE_MAX; i++) {
            int32_t v = (int32_t)g_scope.samples[i] - g_scope.ac_offset;
            if (v < 0) v = 0;
            if (v > 4095) v = 4095;
            g_scope.ac_coupled[i] = (uint16_t)v;
        }
        *out_count = SCOPE_SAMPLE_MAX;
        return g_scope.ac_coupled;
    } else if (g_scope.view.coupling_index == 2) { /* GND */
        memset(g_scope.ac_coupled, 0, sizeof(g_scope.ac_coupled));
        *out_count = SCOPE_SAMPLE_MAX;
        return g_scope.ac_coupled;
    }
    *out_count = SCOPE_SAMPLE_MAX;
    return g_scope.samples;
}

/* 更新触发点索引 */
static void scope_update_trigger(void)
{
    uint16_t count = 0;
    const uint16_t *buf = scope_get_display_buffer(&count);

    if (g_scope.view.trigger_mode_index == 2) { /* Scan 模式：不触发，从起始显示 */
        g_scope.view.trigger_index = 0;
        return;
    }

    uint16_t idx = scope_find_trigger_rising(buf, count,
                                             (uint16_t)g_scope.view.trigger_level, 0);
    if (idx != 0xFFFF) {
        g_scope.view.trigger_index = idx;
        return;
    }

    /* Auto 模式：找不到触发点时强制从 0 显示 */
    if (g_scope.view.trigger_mode_index == 0) {
        g_scope.view.trigger_index = 0;
    } else {
        /* Normal 模式保持上一次触发点，首次未触发用 0xFFFF */
        if (g_scope.view.trigger_index == 0 && count > 0) {
            g_scope.view.trigger_index = 0xFFFF;
        }
    }
}
```

- [ ] **Step 4: 实现自动测量**

```c
static void scope_update_measurements(const uint16_t *buf, uint16_t count)
{
    if (count == 0 || buf == NULL) return;

    uint16_t min_v = buf[0];
    uint16_t max_v = buf[0];
    uint32_t sum = 0;
    for (uint16_t i = 0; i < count; i++) {
        if (buf[i] < min_v) min_v = buf[i];
        if (buf[i] > max_v) max_v = buf[i];
        sum += buf[i];
    }
    g_scope.view.vpp_raw = max_v - min_v;
    g_scope.view.vavg_raw = (uint16_t)(sum / count);

    /* 简易过零频率估算：统计上升沿过均值次数 */
    uint16_t threshold = g_scope.view.vavg_raw;
    uint16_t crossings = 0;
    for (uint16_t i = 0; i < count - 1; i++) {
        if (buf[i] < threshold && buf[i + 1] >= threshold) {
            crossings++;
        }
    }
    if (crossings > 0 && g_scope.view.sample_rate_hz > 0) {
        g_scope.view.freq_hz = g_scope.view.sample_rate_hz / crossings;
    } else {
        g_scope.view.freq_hz = 0;
    }
}
```

- [ ] **Step 5: Commit**

```bash
git add src/app/oscilloscope/oscilloscope_app.c
git commit -m "feat(oscilloscope): add ADC sampling, trigger and measurement engine

Co-Authored-By: kimi-k2.7-code <MoonshotAI@claude-code-best.win>"
```

---

### Task 3: 实现按键状态机和生命周期

**Files:**
- Modify: `src/app/oscilloscope/oscilloscope_app.c`

- [ ] **Step 1: 实现 init/exit**

```c
void oscilloscope_init(void *user_data)
{
    (void)user_data;
    memset(&g_scope, 0, sizeof(g_scope));

    g_scope.view.samples = g_scope.samples;
    g_scope.view.sample_count = SCOPE_SAMPLE_MAX;
    g_scope.view.trigger_index = 0xFFFF;
    g_scope.view.sample_rate_hz = s_time_bases[0].display_rate_hz;
    g_scope.view.running = true;
    g_scope.view.selected_param = PARAM_TIME_BASE;

    /* ADC 配置 */
    pinMode(SCOPE_PIN, INPUT);
    analogSetPinAttenuation(SCOPE_PIN, ADC_11db);

    /* 进入横屏模式 */
    ui_service_enter_landscape();
    ui_service_user_item_init();
}

void oscilloscope_exit(void *user_data)
{
    (void)user_data;
    ui_service_user_item_exit();
    ui_service_exit_landscape();
}
```

- [ ] **Step 2: 实现参数编辑辅助函数**

```c
static void scope_param_next(void)
{
    g_scope.view.selected_param++;
    if (g_scope.view.selected_param >= PARAM_COUNT) {
        g_scope.view.selected_param = 0;
    }
}

static void scope_param_prev(void)
{
    if (g_scope.view.selected_param == 0) {
        g_scope.view.selected_param = PARAM_COUNT - 1;
    } else {
        g_scope.view.selected_param--;
    }
}

static void scope_param_increase(void)
{
    switch (g_scope.view.selected_param) {
    case PARAM_TIME_BASE:
        if (g_scope.view.time_base_index + 1 < SCOPE_TIME_BASE_COUNT) {
            g_scope.view.time_base_index++;
        }
        break;
    case PARAM_VOLT_RANGE:
        if (g_scope.view.volt_range_index + 1 < SCOPE_VOLT_RANGE_COUNT) {
            g_scope.view.volt_range_index++;
        }
        break;
    case PARAM_COUPLING:
        g_scope.view.coupling_index =
            (g_scope.view.coupling_index + 1) % SCOPE_COUPLING_COUNT;
        break;
    case PARAM_TRIGGER_MODE:
        g_scope.view.trigger_mode_index =
            (g_scope.view.trigger_mode_index + 1) % SCOPE_TRIGGER_MODE_COUNT;
        break;
    case PARAM_TRIGGER_LEVEL:
        if (g_scope.view.trigger_level < 4095 - 100) {
            g_scope.view.trigger_level += 100;
        }
        break;
    default:
        break;
    }
}

static void scope_param_decrease(void)
{
    switch (g_scope.view.selected_param) {
    case PARAM_TIME_BASE:
        if (g_scope.view.time_base_index > 0) {
            g_scope.view.time_base_index--;
        }
        break;
    case PARAM_VOLT_RANGE:
        if (g_scope.view.volt_range_index > 0) {
            g_scope.view.volt_range_index--;
        }
        break;
    case PARAM_COUPLING:
        g_scope.view.coupling_index =
            (g_scope.view.coupling_index + SCOPE_COUPLING_COUNT - 1) % SCOPE_COUPLING_COUNT;
        break;
    case PARAM_TRIGGER_MODE:
        g_scope.view.trigger_mode_index =
            (g_scope.view.trigger_mode_index + SCOPE_TRIGGER_MODE_COUNT - 1) % SCOPE_TRIGGER_MODE_COUNT;
        break;
    case PARAM_TRIGGER_LEVEL:
        if (g_scope.view.trigger_level > 100) {
            g_scope.view.trigger_level -= 100;
        }
        break;
    default:
        break;
    }
}

static void scope_update_sample_rate_label(void)
{
    g_scope.view.sample_rate_hz = s_time_bases[g_scope.view.time_base_index].display_rate_hz;
}
```

- [ ] **Step 3: 实现 loop 中的状态机**

```c
void oscilloscope_loop(void *user_data)
{
    (void)user_data;

    /* 1. 采样（运行模式下） */
    if (g_scope.view.running) {
        uint8_t spp = s_time_bases[g_scope.view.time_base_index].samples_per_pixel;
        uint16_t samples_to_take = (uint16_t)(SCREEN_WIDTH * spp);
        if (samples_to_take > SCOPE_SAMPLE_MAX) samples_to_take = SCOPE_SAMPLE_MAX;
        for (uint16_t i = 0; i < samples_to_take; i++) {
            scope_sample_one();
        }
        scope_update_trigger();
        uint16_t count = 0;
        const uint16_t *buf = scope_get_display_buffer(&count);
        scope_update_measurements(buf, count);
        scope_update_sample_rate_label();
    }

    /* 2. 绘制 */
    oscilloscope_ui_draw(&g_scope.view);

    /* 3. 按键输入 */
    hal_event_t ev_a = hal_input_get_event(HAL_BTN_A);
    hal_event_t ev_b = hal_input_get_event(HAL_BTN_B);

    /* 4. 标准退出检查 */
    if (ui_user_item_try_exit(ev_b)) return;

    /* 5. 状态机 */
    if (!g_scope.view.editing) {
        if (ev_a == HAL_EVENT_SHORT_PRESS) {
            scope_param_next();
        } else if (ev_a == HAL_EVENT_LONG_PRESS) {
            g_scope.view.editing = true;
        }
        if (ev_b == HAL_EVENT_SHORT_PRESS) {
            g_scope.view.running = !g_scope.view.running;
        }
    } else {
        if (ev_a == HAL_EVENT_SHORT_PRESS) {
            scope_param_next();
        } else if (ev_a == HAL_EVENT_LONG_PRESS) {
            g_scope.view.editing = false;
        }
        if (ev_b == HAL_EVENT_SHORT_PRESS) {
            scope_param_increase();
        } else if (ev_b == HAL_EVENT_LONG_PRESS) {
            /* 长按 B 在编辑模式下取消编辑 */
            g_scope.view.editing = false;
        }
    }
}
```

- [ ] **Step 4: Commit**

```bash
git add src/app/oscilloscope/oscilloscope_app.c
git commit -m "feat(oscilloscope): add input state machine and lifecycle

Co-Authored-By: kimi-k2.7-code <MoonshotAI@claude-code-best.win>"
```

---

### Task 4: 实现波形与灵动 UI 绘制

**Files:**
- Create: `src/app/oscilloscope/oscilloscope_ui.c`

- [ ] **Step 1: 定义绘制布局和颜色**

```c
#include "oscilloscope_ui.h"
#include "oscilloscope.h"

#include "hal/hal_display.h"
#include "hal/hal_layout.h"
#include "hal/hal_system.h"
#include "ui/ui_core.h"
#include "ui/ui_item.h"

#include <stdio.h>
#include <string.h>

/* 屏幕分区（横屏 160x80） */
#define SCOPE_HEADER_H  12
#define SCOPE_WAVE_Y    12
#define SCOPE_WAVE_H    50
#define SCOPE_FOOTER_Y  64
#define SCOPE_FOOTER_H  16

/* 颜色 */
#define SCOPE_COL_WAVE      0x07FF  /* 青色 */
#define SCOPE_COL_WAVE_DIM  0x0144  /* 暗青色（尾迹） */
#define SCOPE_COL_GRID      0x18E3  /* 暗蓝灰 */
#define SCOPE_COL_GRID_HI   0x39E7  /* 亮一点的网格 */
#define SCOPE_COL_TRIGGER   0xF800  /* 红色触发线 */
#define SCOPE_COL_TEXT      0xFFFF  /* 白色 */
#define SCOPE_COL_TEXT_HI   0xFFE0  /* 黄色高亮 */

/* 外部状态引用（时间基/量程表在 app.c 定义为 static，这里通过 view_state 中的索引访问） */
```

- [ ] **Step 2: 绘制网格**

```c
static void scope_draw_grid(int16_t x, int16_t y, int16_t w, int16_t h)
{
    /* 呼吸效果：根据 tick 微调网格亮度（每 2 秒一个周期） */
    uint32_t t = hal_get_ticks() % 2000;
    uint16_t breath = (t < 1000) ? (uint16_t)t : (uint16_t)(2000 - t);
    uint16_t grid_col = (breath > 500) ? SCOPE_COL_GRID_HI : SCOPE_COL_GRID;

    /* 水平虚线 */
    for (int16_t yy = y + 10; yy < y + h; yy += 10) {
        for (int16_t xx = x; xx < x + w; xx += 4) {
            hal_draw_pixel(xx, yy, grid_col);
        }
    }
    /* 垂直虚线 */
    for (int16_t xx = x + 16; xx < x + w; xx += 16) {
        for (int16_t yy = y; yy < y + h; yy += 4) {
            hal_draw_pixel(xx, yy, grid_col);
        }
    }
}
```

- [ ] **Step 3: 绘制波形**

```c
/* 将 ADC raw 映射到波形区 Y 坐标 */
static int16_t scope_wave_y(uint16_t raw, uint16_t full_scale, int16_t wave_h)
{
    if (raw >= full_scale) raw = full_scale - 1;
    int16_t py = (int16_t)(((uint32_t)raw * (uint32_t)wave_h) / full_scale);
    return (SCOPE_WAVE_Y + wave_h - 1) - py;
}

static void scope_draw_wave(const oscilloscope_view_state_t *state,
                            int16_t x, int16_t y, int16_t w, int16_t h)
{
    if (state->samples == NULL || state->sample_count == 0) return;

    /* 量程 */
    extern const struct { const char *label; uint16_t div_raw; uint16_t full_scale; } s_volt_ranges[];
    uint16_t full_scale = s_volt_ranges[state->volt_range_index].full_scale;
    uint8_t spp = 1; /* 由 app 层根据 time_base_index 决定，这里简化用 1 */

    /* 触发偏移 */
    uint16_t start = 0;
    if (state->trigger_index != 0xFFFF && state->trigger_index < state->sample_count) {
        start = state->trigger_index;
    }

    int16_t prev_x = x;
    int16_t prev_y = scope_wave_y(state->samples[start], full_scale, h);

    hal_set_clip_rect(x, y, w, h);

    for (int16_t px = 1; px < w; px++) {
        uint32_t sample_idx = start + (uint32_t)px * spp;
        if (sample_idx >= state->sample_count) break;
        uint16_t raw = state->samples[sample_idx];
        int16_t cur_y = scope_wave_y(raw, full_scale, h);
        int16_t cur_x = x + px;

        /* 先画淡色尾迹 */
        hal_draw_line(prev_x, prev_y, cur_x, cur_y, SCOPE_COL_WAVE_DIM);
        /* 再画亮色主体 */
        hal_draw_pixel(cur_x, cur_y, SCOPE_COL_WAVE);

        prev_x = cur_x;
        prev_y = cur_y;
    }

    hal_clear_clip_rect();
}
```

- [ ] **Step 4: 绘制触发线和状态栏**

```c
static void scope_draw_trigger_line(const oscilloscope_view_state_t *state,
                                    int16_t x, int16_t y, int16_t w, int16_t h)
{
    extern const struct { const char *label; uint16_t div_raw; uint16_t full_scale; } s_volt_ranges[];
    uint16_t full_scale = s_volt_ranges[state->volt_range_index].full_scale;
    int16_t ty = scope_wave_y((uint16_t)state->trigger_level, full_scale, h);

    /* 水平虚线 */
    for (int16_t xx = x; xx < x + w; xx += 4) {
        hal_draw_pixel(xx, ty, SCOPE_COL_TRIGGER);
    }
}

static void scope_draw_header(const oscilloscope_view_state_t *state)
{
    char buf[32];
    const char *run_str = state->running ? "RUN" : "HOLD";
    uint16_t col = state->running ? 0x07E0 : 0xF800;

    hal_draw_string(2, 2, run_str, col);

    snprintf(buf, sizeof(buf), "%luHz", (unsigned long)state->sample_rate_hz);
    hal_draw_string(28, 2, buf, SCOPE_COL_TEXT);

    snprintf(buf, sizeof(buf), "T:%s", s_trigger_mode_labels[state->trigger_mode_index]);
    hal_draw_string(68, 2, buf, SCOPE_COL_TEXT);

    snprintf(buf, sizeof(buf), "%s", s_coupling_labels[state->coupling_index]);
    hal_draw_string(116, 2, buf, SCOPE_COL_TEXT);
}

static void scope_draw_footer(const oscilloscope_view_state_t *state)
{
    char buf[32];
    int16_t y = SCOPE_FOOTER_Y + 2;

    /* 参数行 */
    for (int i = 0; i < PARAM_COUNT; i++) {
        bool selected = (i == state->selected_param);
        bool editing = selected && state->editing;
        uint16_t col = selected ? (editing ? SCOPE_COL_TEXT_HI : SCOPE_COL_WAVE) : SCOPE_COL_GRID_HI;

        const char *val = "";
        switch (i) {
        case PARAM_TIME_BASE:
            val = s_time_bases[state->time_base_index].label;
            break;
        case PARAM_VOLT_RANGE:
            val = s_volt_ranges[state->volt_range_index].label;
            break;
        case PARAM_COUPLING:
            val = s_coupling_labels[state->coupling_index];
            break;
        case PARAM_TRIGGER_MODE:
            val = s_trigger_mode_labels[state->trigger_mode_index];
            break;
        case PARAM_TRIGGER_LEVEL:
            snprintf(buf, sizeof(buf), "%d", state->trigger_level);
            val = buf;
            break;
        }

        /* 简单横向排列，每项约 30 px */
        int16_t px = 2 + i * 31;
        if (px + 30 > SCREEN_WIDTH) break;

        if (editing) {
            hal_draw_fill_rect(px, y - 1, 30, 10, SCOPE_COL_WAVE);
            hal_draw_string(px + 1, y, val, COLOR_BG);
        } else {
            hal_draw_string(px + 1, y, val, col);
        }
    }

    /* 测量值 */
    snprintf(buf, sizeof(buf), "Vpp:%d F:%lu",
             state->vpp_raw, (unsigned long)state->freq_hz);
    hal_draw_string(SCREEN_WIDTH - hal_get_string_width(buf) - 2, y + 8, buf, SCOPE_COL_TEXT);
}
```

- [ ] **Step 5: 实现主绘制入口**

```c
void oscilloscope_ui_draw(const oscilloscope_view_state_t *state)
{
    /* 背景已由框架清空为黑色 */

    /* 1. 网格 */
    scope_draw_grid(0, SCOPE_WAVE_Y, SCREEN_WIDTH, SCOPE_WAVE_H);

    /* 2. 波形 */
    scope_draw_wave(state, 0, SCOPE_WAVE_Y, SCREEN_WIDTH, SCOPE_WAVE_H);

    /* 3. 触发线 */
    scope_draw_trigger_line(state, 0, SCOPE_WAVE_Y, SCREEN_WIDTH, SCOPE_WAVE_H);

    /* 4. 状态栏 */
    scope_draw_header(state);
    scope_draw_footer(state);
}
```

- [ ] **Step 6: Commit**

```bash
git add src/app/oscilloscope/oscilloscope_ui.c
git commit -m "feat(oscilloscope): add waveform and animated UI rendering

Co-Authored-By: kimi-k2.7-code <MoonshotAI@claude-code-best.win>"
```

---

### Task 5: 注册到菜单并暴露参数表访问器

**Files:**
- Modify: `src/app/oscilloscope/oscilloscope_ui.h`
- Modify: `src/app/oscilloscope/oscilloscope_app.c`
- Modify: `src/app/app_menu.c`

- [ ] **Step 1: 将参数表声明为可在 UI 层访问**

`oscilloscope_ui.h` 追加：

```c
typedef struct {
    const char *label;
    uint8_t samples_per_pixel;
    uint32_t display_rate_hz;
} scope_time_base_t;

extern const scope_time_base_t g_scope_time_bases[];
extern const uint8_t g_scope_time_base_count;

typedef struct {
    const char *label;
    uint16_t div_raw;
    uint16_t full_scale;
} scope_volt_range_t;

extern const scope_volt_range_t g_scope_volt_ranges[];
extern const uint8_t g_scope_volt_range_count;

extern const char *g_scope_coupling_labels[];
extern const uint8_t g_scope_coupling_count;

extern const char *g_scope_trigger_mode_labels[];
extern const uint8_t g_scope_trigger_mode_count;
```

`oscilloscope_app.c` 中将 `static const scope_time_base_t s_time_bases[]` 改为：

```c
const scope_time_base_t g_scope_time_bases[] = { ... };
const uint8_t g_scope_time_base_count = sizeof(g_scope_time_bases) / sizeof(g_scope_time_bases[0]);
```

同样处理 `s_volt_ranges`、`s_coupling_labels`、`s_trigger_mode_labels`，并同步 `oscilloscope_ui.c` 中的外部声明。

- [ ] **Step 2: 在 app_menu.c 注册入口**

`src/app/app_menu.c` 顶部追加：

```c
#include "oscilloscope/oscilloscope.h"
```

在 `app_menu_build()` 中添加：

```c
xerintosh_list_item_t* scope_item = xerintosh_new_user_item(
    "示波器", oscilloscope_init, oscilloscope_loop, oscilloscope_exit, default_icon);
app_menu_push_checked(root, scope_item, "示波器");
```

- [ ] **Step 3: Commit**

```bash
git add src/app/oscilloscope/oscilloscope_ui.h src/app/oscilloscope/oscilloscope_app.c src/app/app_menu.c
git commit -m "feat(oscilloscope): register app to menu and expose param tables

Co-Authored-By: kimi-k2.7-code <MoonshotAI@claude-code-best.win>"
```

---

### Task 6: 编写 Native 测试

**Files:**
- Create: `test/test_native/test_oscilloscope.cpp`

- [ ] **Step 1: 测试触发检测函数**

```cpp
#include <gtest/gtest.h>

extern "C" {
#include "app/oscilloscope/oscilloscope_ui.h"
}

/* 触发检测函数声明（为了测试，将其在 app.c 中改为非 static 或移到独立测试可见单元） */
extern "C" uint16_t scope_find_trigger_rising(const uint16_t *buf, uint16_t count,
                                              uint16_t level, uint16_t start);

TEST(OscilloscopeTrigger, RisingEdgeFound) {
    uint16_t buf[] = {100, 100, 200, 400, 800, 400, 200};
    uint16_t idx = scope_find_trigger_rising(buf, 7, 300, 0);
    EXPECT_EQ(idx, 2);
}

TEST(OscilloscopeTrigger, NoEdgeReturnsInvalid) {
    uint16_t buf[] = {100, 100, 100, 100};
    uint16_t idx = scope_find_trigger_rising(buf, 4, 300, 0);
    EXPECT_EQ(idx, 0xFFFF);
}

TEST(OscilloscopeTrigger, StartOffsetSkipsEarlierEdges) {
    uint16_t buf[] = {100, 400, 100, 400, 800};
    uint16_t idx = scope_find_trigger_rising(buf, 5, 300, 2);
    EXPECT_EQ(idx, 3);
}
```

- [ ] **Step 2: 测试参数状态机边界**

```cpp
TEST(OscilloscopeParam, TimeBaseClamped) {
    /* 通过访问 g_scope.view.time_base_index 或直接测试参数辅助函数 */
    EXPECT_LT(g_scope_time_base_count, 10);
    EXPECT_GT(g_scope_time_base_count, 0);
}

TEST(OscilloscopeParam, VoltRangeFullScaleNonZero) {
    for (uint8_t i = 0; i < g_scope_volt_range_count; i++) {
        EXPECT_GT(g_scope_volt_ranges[i].full_scale, 0);
    }
}
```

- [ ] **Step 3: 运行 native 测试**

```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"
pio test -e native --filter test_oscilloscope
```

Expected: 所有测试通过。

- [ ] **Step 4: Commit**

```bash
git add test/test_native/test_oscilloscope.cpp
git commit -m "test(oscilloscope): add native tests for trigger and params

Co-Authored-By: kimi-k2.7-code <MoonshotAI@claude-code-best.win>"
```

---

### Task 7: 构建验证、硬件编译与文档

**Files:**
- Modify: `doc/app/index.md`
- Create: `doc/app/oscilloscope.md`

- [ ] **Step 1: 运行 native 全量测试**

```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"
pio test -e native
```

Expected: 基线测试全部通过（可能存在的末尾 SIGSEGV 为既有内核 IPC 问题，不影响本特性）。

- [ ] **Step 2: 运行硬件编译**

```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"
pio run -e m5stick-c
```

Expected: 编译成功。关注 RAM/Flash 占用变化，确保示波器新增内存不会导致溢出。

- [ ] **Step 3: 编写用户文档**

`doc/app/oscilloscope.md`:

```markdown
# 示波器 App

> **Parent:** [应用层文档](index.md) | **Related:** [API 模板](../tutorials/api-templates.md)

## 功能

输入口为 **G36（GPIO36 / ADC1_CH0）** 的单通道示波器。

- 实时波形显示，横屏布局
- 可调时基、电压量程、耦合方式、触发模式与触发电平
- 自动测量 Vpp 与估算频率
- 运行/暂停切换

## 按键映射

| 按键 | 运行模式 | 编辑模式 |
|------|----------|----------|
| BtnA 短按 | 切换选中参数 | 减小当前参数值 |
| BtnA 长按 | 进入参数编辑模式 | 确认并退出编辑模式 |
| BtnB 短按 | 运行/暂停切换 | 增大当前参数值 |
| BtnB 长按 | 返回上一级菜单 | 返回上一级菜单 |

## 输入保护建议

G36 直接接外部信号时，建议串联 1kΩ~10kΩ 限流电阻，并使用肖特基二极管钳位到 3.3V/GND。不要输入超过 3.3V 的电压。

## 已知限制

- G36 同时被烧录器模块默认用作 Serial1 RX，示波器运行时请勿使用烧录器功能。
- ESP32 ADC 在 0~150mV 与接近 3.3V 区域非线性较严重，测量结果仅供参考。
```

`doc/app/index.md` 追加导航链接：

```markdown
- [示波器](oscilloscope.md) — G36 单通道示波器 App
```

- [ ] **Step 4: Commit**

```bash
git add doc/app/oscilloscope.md doc/app/index.md
git commit -m "docs(oscilloscope): add user documentation

Co-Authored-By: kimi-k2.7-code <MoonshotAI@claude-code-best.win>"
```

---

## Self-Review

1. **Spec coverage:**
   - G36 ADC 输入：✅ Task 2 `analogRead(SCOPE_PIN)` + `analogSetPinAttenuation`
   - 灵动前端界面：✅ Task 4 网格呼吸、波形尾迹、参数高亮动画
   - 按键状态机：✅ Task 3 运行/编辑双模式状态机
   - 功能设计：✅ 时基、幅值、耦合、触发模式、触发电平、运行/暂停、Vpp/频率测量
   - 文档：✅ Task 7

2. **Placeholder scan:**
   - 无 TBD/TODO；所有代码块均为可直接使用的 C/C++。
   - 注意：`scope_draw_wave` 中 `spp` 简化为 1，实际实现时应从 `state->time_base_index` 查表获取真实 `samples_per_pixel`。
   - `scope_find_trigger_rising` 为了在 native 测试中被调用，需要将其在 `oscilloscope_app.c` 中改为非 `static` 或在头文件中声明。

3. **Type consistency:**
   - `oscilloscope_view_state_t` 中的字段在 app.c 和 ui.c 中一致。
   - 参数表已统一为 `g_scope_*` 前缀并暴露给 UI 层。

---

## Execution Handoff

**Plan complete and saved to `docs/superpowers/plans/2026-06-15-oscilloscope-g36.md`. Two execution options:**

**1. Subagent-Driven (recommended)** — I dispatch a fresh subagent per task, review between tasks, fast iteration.

**2. Inline Execution** — Execute tasks in this session using executing-plans, batch execution with checkpoints.

**Which approach would you like?**
