# 显示驱动（HAL Display）

> **Parent:** [知识地图](../index.md) | **Related:** [输入系统](input.md), [系统时钟](system.md)

## 概述

显示 HAL 负责把 UI 核心的绘制指令转换为实际的像素操作。本层提供**两套实现**：

- **M5Stick-C 真机**：基于 `M5Canvas` 的双缓冲 TFT 驱动（16-bit RGB565）
- **Native 桌面测试**：基于内存数组的软件帧缓冲 + Bresenham 算法

两套实现共享同一套 C API，UI 上层无感知。

---

## 关键概念

### 屏幕参数

*📄 Source: [hal_display.h](../../src/hal/hal_display.h#L8-L12)*

```c
#define SCREEN_WIDTH  80
#define SCREEN_HEIGHT 160
#define COLOR_BG      0x0000   // 黑色
#define COLOR_FG      0xFFFF   // 白色
#define COLOR_ACCENT  0x07E0   // 绿色（保留）
```

### 双缓冲架构（真机）

*📄 Source: [hal_display.cpp](../../src/hal/hal_display.cpp#L254-L274)*

```c
static M5Canvas* g_canvas = nullptr;

void hal_display_init(void) {
    if (!g_canvas) {
        g_canvas = new M5Canvas(&M5.Display);
    }
    g_canvas->createSprite(SCREEN_WIDTH, SCREEN_HEIGHT);
    g_canvas->setColorDepth(16);
}

void hal_display_clear(void) {
    if (g_canvas) {
        g_canvas->fillScreen(COLOR_BG);
    }
}

void hal_display_flush(void) {
    if (g_canvas) {
        g_canvas->pushSprite(&M5.Display, 0, 0);
    }
}
```

#### 中文伪代码拆解

```
函数 显示初始化() {
    if (画布指针为空) {
        // 关键：必须传入父显示对象，否则 pushSprite 会失败
        画布 = 新建 M5Canvas(地址取 M5.Display)
    }
    画布.创建精灵(80, 160)
    画布.设置色深(16位)
}

函数 显示清空() {
    画布.填充屏幕(黑色)
}

函数 显示刷新() {
    // 将后台画布一次性推送到 TFT
    画布.推送精灵到(地址取 M5.Display, x=0, y=0)
}
```

### Native 内存帧缓冲（测试）

*📄 Source: [hal_display.cpp](../../src/hal/hal_display.cpp#L6-L247)*

```c
static uint16_t g_framebuffer[SCREEN_WIDTH * SCREEN_HEIGHT];

void hal_display_init(void) {
    memset(g_framebuffer, 0, sizeof(g_framebuffer));
}

void hal_draw_pixel(int16_t x, int16_t y, uint16_t color) {
    if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) return;
    g_framebuffer[y * SCREEN_WIDTH + x] = color;
}
```

#### 中文伪代码拆解

```
变量 帧缓冲数组[宽 × 高]    // 16-bit 每像素

函数 绘制像素(x, y, 颜色) {
    if (x 或 y 超出屏幕边界) return   // 裁剪
    帧缓冲[y × 宽 + x] = 颜色
}
```

Native 环境下所有复杂图形（线、圆、圆角矩形）都通过**软件算法**实现：
- 直线：[Bresenham 算法](../../src/hal/hal_display.cpp#L26-L46)
- 圆和圆角矩形：[中点圆算法](../../src/hal/hal_display.cpp#L158-L189)

### XOR 反色矩形（选择器高亮）

TFT 不支持 OLED 的 `draw_color(2)` 反色模式。我们采用**像素级 XOR** 实现选择器高亮。

*📄 Source: [hal_display.cpp](../../src/hal/hal_display.cpp#L345-L355)*

```c
void hal_draw_xor_rect(int16_t x, int16_t y, int16_t w, int16_t h) {
    if (!g_canvas) return;
    for (int16_t row = 0; row < h; row++) {
        for (int16_t col = 0; col < w; col++) {
            int16_t px = x + col;
            int16_t py = y + row;
            uint16_t color = g_canvas->readPixel(px, py);
            g_canvas->drawPixel(px, py, color ^ 0xFFFF);
        }
    }
}
```

#### 中文伪代码拆解

```
函数 绘制XOR反色矩形(x, y, 宽, 高) {
    for (行 = 0; 行 < 高; 行++) {
        for (列 = 0; 列 < 宽; 列++) {
            像素坐标 = (x + 列, y + 行)
            原色 = 画布.读取像素(像素坐标)
            // 按位异或 0xFFFF 实现反色（RGB565 全通道翻转）
            画布.绘制像素(像素坐标, 原色 XOR 0xFFFF)
        }
    }
}
```

**核心思想**：将目标矩形区域内的每个像素与 `0xFFFF` 做按位异或。对于 RGB565 格式，这等价于把每个颜色通道取反，从而实现“黑变白、白变黑”的反色效果。

### 字体与文本

真机环境下直接委托给 M5GFX 的文本 API：

*📄 Source: [hal_display.cpp](../../src/hal/hal_display.cpp#L312-L343)*

```c
void hal_draw_utf8(int16_t x, int16_t y, const char* str, uint16_t color) {
    if (!g_canvas || !str) return;
    g_canvas->setTextColor(color);
    g_canvas->setCursor(x, y);
    g_canvas->print(str);
}

int16_t hal_get_utf8_width(const char* str) {
    if (!g_canvas || !str) return 0;
    return g_canvas->textWidth(str);
}

int16_t hal_get_font_height(void) {
    if (!g_canvas) return 8;
    return g_canvas->fontHeight();
}
```

Native 测试环境目前将文本绘制设为空实现（只返回固定字高 8px），因为测试框架主要验证动画和逻辑，不验证字体渲染。

---

## 与其他组件的关系

- **ui_draw_driver**：通过宏把 `oled_draw_*` 映射到本层的 `hal_draw_*`
- **ui_drawer**：调用 `hal_draw_xor_rect()` 实现选择器反色高亮
- **main.cpp**：每帧调用 `hal_display_clear()` → 绘制 → `hal_display_flush()`

---

> **See Also:** [输入系统](input.md) | [绘制管线](../ui/drawer.md) | [绘制驱动适配](../ui/draw-driver.md)
