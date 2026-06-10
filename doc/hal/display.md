# 显示驱动（HAL Display）

> **Parent:** [知识地图](../index.md) | **Related:** [输入系统](input.md), [系统时钟](system.md)

## 概述

显示 HAL 负责把 UI 核心的绘制指令转换为实际的像素操作。本层提供**两套实现**：

- **M5Stick-C 真机**：基于 `M5Canvas` 的双缓冲 TFT 驱动（8-bit RGB332，256 色）
- **Native 桌面测试**：基于内存数组的软件帧缓冲 + Bresenham 算法

两套实现共享同一套 C API，UI 上层无感知。

---

## 关键概念

### 屏幕参数

*📄 Source: [hal_display.h](../../src/hal/hal_display.h#L21-L35)*

```c
#ifdef NATIVE_TEST
#define SCREEN_WIDTH  80   /* native 测试环境屏幕宽度 */
#define SCREEN_HEIGHT 160  /* native 测试环境屏幕高度 */
#else
extern int16_t g_screen_width;   /* 运行时屏幕宽度（硬件环境从 M5.Display 读取） */
extern int16_t g_screen_height;  /* 运行时屏幕高度 */
#define SCREEN_WIDTH  g_screen_width
#define SCREEN_HEIGHT g_screen_height
#endif

#define COLOR_BG      0x0000  /* 背景色：黑色 */
#define COLOR_FG      0xFFFF  /* 前景色：白色 */
#define COLOR_ACCENT  0x07E0  /* 强调色：绿色 */
#define COLOR_RED     0xF800  /* 红色：用于 Master 前缀 */
```

### 双缓冲架构（真机）

*📄 Source: [hal_display.cpp](../../src/hal/hal_display.cpp#L353-L401)*

```c
static M5Canvas* g_canvas = nullptr;  /* 离屏画布 */
int16_t g_screen_width = 160;         /* 默认屏幕宽度，init 时从硬件读取 */
int16_t g_screen_height = 80;         /* 默认屏幕高度 */

void hal_display_init(void) {
    if (!g_canvas) {
        g_canvas = new M5Canvas(&M5.Display);
    }
    g_canvas->setColorDepth(8);   /* 8-bit 色深：RGB332, 256 色，每像素 1 字节 */
    g_screen_width = M5.Display.width();
    g_screen_height = M5.Display.height();
    g_canvas->createSprite(g_screen_width, g_screen_height);
}

void hal_display_deinit(void) {
    if (g_canvas) {
        g_canvas->deleteSprite();  /* 释放 80×160×8bit = 12.8KB 帧缓冲 */
    }
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
    // 必须在 createSprite 之前设置色深
    画布.设置色深(8位)
    屏幕宽 = M5.Display.宽度()
    屏幕高 = M5.Display.高度()
    画布.创建精灵(宽, 高)
}

函数 显示反初始化() {
    if (画布存在) {
        画布.删除精灵()   // 释放 12.8KB 帧缓冲
    }
}

函数 显示清空() {
    画布.填充屏幕(黑色)
}

函数 显示刷新() {
    // 将后台画布一次性推送到 TFT
    画布.推送精灵到(地址取 M5.Display, x=0, y=0)
}
```

**关键顺序**：`setColorDepth(8)` 必须**在** `createSprite()` 之前调用。若顺序颠倒，alpha=0 会导致所有绘制不可见（黑屏）。8-bit RGB332 相比 16-bit 节省 12.8KB 内存，确保 ESP32-PICO 无 PSRAM 时也能同时运行 Classic BT SPP + UI 渲染。

### Native 内存帧缓冲（测试）

*📄 Source: [hal_display.cpp](../../src/hal/hal_display.cpp#L19-L47)*

```c
static uint16_t g_framebuffer[SCREEN_WIDTH * SCREEN_HEIGHT];  /* RGB565 帧缓冲区 */

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
- 直线：[Bresenham 算法](../../src/hal/hal_display.cpp#L52-L72)
- 圆和圆角矩形：[中点圆算法](../../src/hal/hal_display.cpp#L126-L203)

### XOR 反色矩形（选择器高亮）

TFT 不支持 OLED 的 `draw_color(2)` 反色模式。我们采用**像素级 XOR** 实现选择器高亮。

*📄 Source: [hal_display.cpp](../../src/hal/hal_display.cpp#L510-L531)*

```c
void hal_draw_xor_rect(int16_t x, int16_t y, int16_t w, int16_t h) {
    if (!g_canvas || w <= 0 || h <= 0) return;

    int16_t cw = g_canvas->width();
    int16_t ch = g_canvas->height();
    /* 裁剪 */
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > cw) w = cw - x;
    if (y + h > ch) h = ch - y;
    if (w <= 0 || h <= 0) return;

    /* 行缓冲：最大 160×2 = 320 字节，栈安全 */
    uint16_t row_buf[160];

    for (int16_t row = 0; row < h; row++) {
        g_canvas->readRect(x, y + row, w, 1, row_buf);
        for (int16_t i = 0; i < w; i++)
            row_buf[i] ^= 0xFFFF;
        g_canvas->pushImage(x, y + row, w, 1, row_buf);
    }
}
```

#### 中文伪代码拆解

```
函数 绘制XOR反色矩形(x, y, 宽, 高) {
    for (行 = 0; 行 < 高; 行++) {
        读取一行像素到 行缓冲[160]
        for (列 = 0; 列 < 宽; 列++) {
            行缓冲[列] = 行缓冲[列] XOR 0xFFFF   // RGB565 全通道翻转
        }
        推送一行像素回画布
    }
}
```

**核心思想**：将目标矩形区域内的每个像素与 `0xFFFF` 做按位异或。对于 RGB565 格式，这等价于把每个颜色通道取反，从而实现“黑变白、白变黑”的反色效果。

硬件实现使用 `readRect`/`pushImage` 逐行操作，栈上固定 `row_buf[160]`（320 字节），无需 `malloc`。Native 实现则直接逐像素异或帧缓冲。

### 字体与文本

真机环境下直接委托给 M5GFX 的文本 API：

*📄 Source: [hal_display.cpp](../../src/hal/hal_display.cpp#L466-L501)*

```c
void hal_draw_string(int16_t x, int16_t y, const char* str, uint16_t color) {
    if (!g_canvas || !str) return;
    g_canvas->setTextColor(color);
    g_canvas->setTextDatum(lgfx::v1::baseline_left);
    g_canvas->drawString(str, x, y);
}

int16_t hal_get_string_width(const char* str) {
    if (!g_canvas || !str) return 0;
    return g_canvas->textWidth(str);
}

int16_t hal_get_font_height(void) {
    if (!g_canvas) return 8;
    return g_canvas->fontHeight();
}
```

*📄 Source: [hal_display.h](../../src/hal/hal_display.h#L169-L183)*

```c
/* hal_draw_utf8 是 hal_draw_string 的别名（M5GFX drawString 本身即 UTF-8 兼容） */
#define hal_draw_utf8(x, y, str, color) hal_draw_string(x, y, str, color)

/* hal_get_utf8_width 是 hal_get_string_width 的别名 */
#define hal_get_utf8_width(str) hal_get_string_width(str)
```

Native 测试环境目前将文本绘制设为空实现（只返回固定字高 8px），因为测试框架主要验证动画和逻辑，不验证字体渲染。

---

## 与其他组件的关系

- **ui_draw_driver**：通过宏把 `oled_draw_*` 映射到本层的 `hal_draw_*`
- **ui_drawer**：调用 `hal_draw_xor_rect()` 实现选择器反色高亮
- **main.cpp**：每帧调用 `hal_display_clear()` → 绘制 → `hal_display_flush()`
- **hal_layout.h**：提供基于 `hal_get_font_height()` 和 `SCREEN_WIDTH/HEIGHT` 的布局宏

---

> **See Also:** [输入系统](input.md) | [绘制管线](../ui/drawer.md)
