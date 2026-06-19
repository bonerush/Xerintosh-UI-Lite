# 显示驱动（HAL Display）

> **Parent:** [知识地图](../index.md) | **Related:** [输入系统](input.md), [系统时钟](system.md), [屏幕尺寸](screen.md)

## 概述

显示 HAL 负责把 UI 核心的绘制指令转换为实际的像素操作。本层提供**两套实现**：

- **M5Stick-C 真机**：基于 `M5Canvas` 的双缓冲 TFT 驱动（8-bit RGB332，256 色）
- **Native 桌面测试**：基于内存数组的软件帧缓冲 + Bresenham 算法

两套实现共享同一套 C API，UI 上层无感知。

源码按职责拆分为四个文件，统一通过 `hal_display.h` 暴露：

| 文件 | 职责 |
|---|---|
| `src/hal/hal_display_fb.cpp` | 帧缓冲/画布生命周期、方向与亮度配置 |
| `src/hal/hal_display_draw.cpp` | 绘制原语（像素、线、矩形、圆、圆角矩形） |
| `src/hal/hal_display_font.cpp` | 字体设置、字符串绘制、文本宽度/高度查询 |
| `src/hal/hal_display_adv.cpp` | 高级绘制（XOR 反色、XBM 位图、裁剪矩形、测试钩子） |

屏幕尺寸常量与运行时查询已收敛到 [`hal_screen.h`](screen.md)。

---

## 关键概念

### 屏幕参数

*📄 Source: [hal_display.h](../../src/hal/hal_display.h#L22-L33) / [hal_screen.h](../../src/hal/hal_screen.h#L19-L36)*

```c
#ifdef NATIVE_TEST
#define SCREEN_WIDTH  80   /* native 测试环境屏幕宽度 */
#define SCREEN_HEIGHT 160  /* native 测试环境屏幕高度 */
#else
#define SCREEN_WIDTH  g_screen_width
#define SCREEN_HEIGHT g_screen_height
#endif

#define COLOR_BG      0x0000  /* 背景色：黑色 */
#define COLOR_FG      0xFFFF  /* 前景色：白色 */
#define COLOR_ACCENT  0x07E0  /* 强调色：绿色 */
#define COLOR_RED     0xF800  /* 红色：用于 Master 前缀 */
```

### 双缓冲架构（真机）

*📄 Source: [hal_display_fb.cpp](../../src/hal/hal_display_fb.cpp#L86-L115)*

```c
M5Canvas* g_canvas = nullptr;       /* 离屏画布 */
static int g_rotation = 0;          /* 当前屏幕方向 */
static uint8_t g_brightness = 128;  /* 当前背光亮度 */

static void hal_display_create_sprite(M5Canvas* canvas,
                                      int16_t w, int16_t h,
                                      uint8_t depth)
{
    canvas->setColorDepth(depth);
    canvas->createSprite(w, h);
}

void hal_display_init(void) {
    if (!g_canvas) {
        g_canvas = new M5Canvas(&M5.Display);
    }
    g_screen_width = M5.Display.width();
    g_screen_height = M5.Display.height();
    hal_display_create_sprite(g_canvas, g_screen_width, g_screen_height, 8);
}
```

#### 中文伪代码拆解

```
函数 显示初始化() {
    if (画布指针为空) {
        // 关键：必须传入父显示对象，否则 pushSprite 会失败
        画布 = 新建 M5Canvas(地址取 M5.Display)
    }
    屏幕宽 = M5.Display.宽度()
    屏幕高 = M5.Display.高度()
    // 由 helper 统一保证：先 setColorDepth，再 createSprite
    创建精灵_封装(画布, 宽, 高, 8位)
}
```

**关键顺序**：`setColorDepth(8)` 必须**在** `createSprite()` 之前调用。若顺序颠倒，alpha=0 会导致所有绘制不可见（黑屏）。该顺序已封装到 `hal_display_create_sprite()` helper 中，由调用方统一保证，避免人工维护时顺序被颠倒。8-bit RGB332 相比 16-bit 节省 12.8KB 内存，确保 ESP32-PICO 无 PSRAM 时也能同时运行 Classic BT SPP + UI 渲染。

*📄 Source: [hal_display_fb.cpp](../../src/hal/hal_display_fb.cpp#L122-L126)*

```c
void hal_display_deinit(void) {
    if (g_canvas) {
        g_canvas->deleteSprite();  /* 释放 80×160×8bit = 12.8KB 帧缓冲 */
    }
}
```

### Native 内存帧缓冲（测试）

*📄 Source: [hal_display_fb.cpp](../../src/hal/hal_display_fb.cpp#L18-L31)*

```c
uint16_t g_framebuffer[SCREEN_WIDTH * SCREEN_HEIGHT];  /* RGB565 帧缓冲区 */
extern uint16_t *g_font_fb;  /* 定义在 hal_display_font.cpp，native 字体层 */

void hal_display_init(void) {
    memset(g_framebuffer, 0, sizeof(g_framebuffer));
    if (g_font_fb != NULL) {
        memset(g_font_fb, 0, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint16_t));
    }
}

/**
 * @brief 清屏（填充指定颜色）
 * @param color 16 位 RGB565 颜色
 */
void hal_display_clear_color(uint16_t color) {
    for (size_t i = 0; i < sizeof(g_framebuffer) / sizeof(g_framebuffer[0]); i++) {
        g_framebuffer[i] = color;
    }
}

/**
 * @brief 清屏（填充背景色）
 */
void hal_display_clear(void) {
    hal_display_clear_color(COLOR_BG);
}
```

*📄 Source: [hal_display_draw.cpp](../../src/hal/hal_display_draw.cpp#L20-L23)*

```c
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
- 直线：[Bresenham 算法](../../src/hal/hal_display_draw.cpp#L28-L48)
- 圆和圆角矩形：[中点圆算法](../../src/hal/hal_display_draw.cpp#L102-L140)

### 显示配置

上层不再直接调用 `M5.Display.setRotation()` / `setBrightness()`。HAL 提供统一配置接口：

*📄 Source: [hal_display.h](../../src/hal/hal_display.h#L238-L262)*

```c
void hal_display_set_rotation(int rotation);
int  hal_display_get_rotation(void);
void hal_display_set_brightness(uint8_t level);
uint8_t hal_display_get_brightness(void);
```

*📄 Source: [hal_display_fb.cpp](../../src/hal/hal_display_fb.cpp#L128-L147)*

```c
void hal_display_set_rotation(int rotation) {
    if (rotation < 0 || rotation > 3) rotation = 0;
    g_rotation = rotation;
    M5.Display.setRotation(rotation);
    g_screen_width = M5.Display.width();
    g_screen_height = M5.Display.height();

    /* 重建 M5Canvas 精灵以匹配新的屏幕方向（P1-2）
     * 调用方无需再手动调用 hal_display_init() */
    if (g_canvas) {
        g_canvas->deleteSprite();
        hal_display_create_sprite(g_canvas, g_screen_width, g_screen_height, 8);
    }
}

void hal_display_set_brightness(uint8_t level) {
    g_brightness = level;
    M5.Display.setBrightness(level);
}
```

`/dev/fb0` 的 `DEV_FB_IOCTL_SET_ROTATION` 也转发到 `hal_display_set_rotation()`，实现 Shell 与 sysfs 对屏幕方向的统一控制。方向切换时会自动删除并重建 `M5Canvas` 精灵，保证缓冲区尺寸与新的 `SCREEN_WIDTH` / `SCREEN_HEIGHT` 一致。

### 清屏

*📄 Source: [hal_display.h](../../src/hal/hal_display.h#L50-L58) / [hal_display_fb.cpp](../../src/hal/hal_display_fb.cpp#L156-L171)*

```c
void hal_display_clear(void);           /* 填充背景色 COLOR_BG */
void hal_display_clear_color(uint16_t color);  /* 填充指定 RGB565 颜色 */
```

`hal_display_clear()` 是常用入口，内部调用 `hal_display_clear_color(COLOR_BG)`。`hal_display_clear_color()` 在需要非黑背景时使用（例如关机画面、启动画面）。两套实现都保证全屏填充：真机通过 `M5Canvas::fillScreen()`，Native 通过遍历 `g_framebuffer` 数组。

### XOR 反色矩形（选择器高亮）

TFT 不支持 OLED 的 `draw_color(2)` 反色模式。我们采用**像素级 XOR** 实现选择器高亮。

*📄 Source: [hal_display_adv.cpp](../../src/hal/hal_display_adv.cpp#L103-L127)*

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

    /* 静态缓冲：一次读取整个选择器区域，XOR 后一次写回，避免逐行 readRect/pushImage
     * 最大尺寸：横屏下选择器宽度 ≤160，高度 ≤22（字体+边距），160×22 = 3520 像素 = 7040 字节
     * 竖屏下只需 80×22 = 1760 像素。比之前的 160×30(9600B) 节省最多 2560B */
    #define XOR_BUF_MAX_PX 3520
    static uint16_t xor_buf[XOR_BUF_MAX_PX];
    int total = w * h;
    if (total > XOR_BUF_MAX_PX) return;

    g_canvas->readRect(x, y, w, h, xor_buf);
    for (int i = 0; i < total; i++)
        xor_buf[i] ^= 0xFFFF;
    g_canvas->pushImage(x, y, w, h, xor_buf);
}
```

#### 中文伪代码拆解

```
函数 绘制XOR反色矩形(x, y, 宽, 高) {
    裁剪到画布边界
    计算总像素数 = 宽 × 高
    if (总像素数 > 3520) return   // 超出静态缓冲容量

    一次性读取整个矩形区域到 静态缓冲[3520]
    for (i = 0; i < 总像素数; i++) {
        静态缓冲[i] = 静态缓冲[i] XOR 0xFFFF   // RGB565 全通道翻转
    }
    一次性推送整个矩形区域回画布
}
```

**核心思想**：将目标矩形区域内的每个像素与 `0xFFFF` 做按位异或。对于 RGB565 格式，这等价于把每个颜色通道取反，从而实现“黑变白、白变黑”的反色效果。

硬件实现使用**批量读写**：`readRect` 一次性读取整个选择器区域到静态 `xor_buf[3520]`（7040 字节），XOR 后 `pushImage` 一次性写回，相比逐行操作减少 SPI 调用次数。Native 实现则直接逐像素异或帧缓冲。

### 字体与文本

真机环境下直接委托给 M5GFX 的文本 API，同时支持 `\n` 换行：

*📄 Source: [hal_display_font.cpp](../../src/hal/hal_display_font.cpp#L246-L284)*

```c
void hal_draw_string(int16_t x, int16_t y, const char* str, uint16_t color) {
    if (!g_canvas || !str) return;
    g_canvas->setTextColor(color);
    g_canvas->setTextDatum(lgfx::v1::baseline_left);

    const char *p = str;
    const char *line_start = p;
    int16_t line_y = y;
    int16_t font_h = g_canvas->fontHeight();

    while (1) {
        if (*p == '\n' || *p == '\0') {
            size_t len = p - line_start;
            if (len > 0) {
                char buf[256];
                if (len >= sizeof(buf)) len = sizeof(buf) - 1;
                memcpy(buf, line_start, len);
                buf[len] = '\0';
                g_canvas->drawString(buf, x, line_y);
            }
            if (*p == '\0') break;
            line_y += font_h;
            line_start = ++p;
        } else {
            p++;
        }
    }
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

*📄 Source: [hal_display.h](../../src/hal/hal_display.h#L169-L188)*

```c
/* hal_draw_utf8 是 hal_draw_string 的别名（M5GFX drawString 本身即 UTF-8 兼容） */
#define hal_draw_utf8(x, y, str, color) hal_draw_string(x, y, str, color)

/* hal_get_utf8_width 是 hal_get_string_width 的别名 */
#define hal_get_utf8_width(str) hal_get_string_width(str)
```

Native 测试环境提供**固定宽度 ASCII 字体模拟**（6×8 位图字体），使 UI 布局测试获得可预测的尺寸输入：

- `hal_get_font_height()` 返回 `8`
- `hal_get_string_width(str)` 返回 `strlen(str) * 7`（含 1px 字间距）
- `hal_draw_string()` 将字符写入独立的字体层，最终通过 `hal_test_fb_read()` 与帧缓冲叠加输出

*📄 Source: [hal_display_font.cpp](../../src/hal/hal_display_font.cpp#L195-L211)*

```c
void hal_draw_string(int16_t x, int16_t y, const char* str, uint16_t color) {
    if (!str) return;
    font_fb_init_once();
    int16_t cx = x;
    int16_t cy = y - FONT_H + 1;
    while (*str) {
        if (*str == '\n') {
            cx = x;
            cy += FONT_H;
            str++;
            continue;
        }
        font_draw_char(cx, cy, *str, color);
        cx += FONT_W + 1;
        str++;
    }
}

int16_t hal_get_string_width(const char* str) {
    if (!str) return 0;
    size_t len = strlen(str);
    return (int16_t)(len * (FONT_W + 1));
}
```

### 字体设置

*📄 Source: [hal_display.h](../../src/hal/hal_display.h#L156-L160) / [hal_display_font.cpp](../../src/hal/hal_display_font.cpp#L188-L190) / [hal_display_font.cpp](../../src/hal/hal_display_font.cpp#L246-L252)*

```c
void hal_set_font(const void* font);
```

`hal_set_font()` 将当前绘图字体切换到指定字体指针。真机环境下该指针为 `lgfx::v1::IFont*`，会调用 `g_canvas->setFont()`；若传入 `NULL` 则回退到 `fonts::Font0`。Native 桩实现直接忽略参数。上层代码应优先使用 `xerintosh_set_font()`（带缓存），仅在 `user_item` 等需要临时切换字体的场景直接调用 `hal_set_font()`。

`hal_get_cn_font()` 返回项目子集中文字体指针（U8G2 格式，仅包含源码使用的 844 个汉字），供 `hal_set_font()` 切换中文字体渲染。Native 测试环境返回 `NULL`（桩实现）。

*📄 Source: [hal_display.h](../../src/hal/hal_display.h#L196-L200) / [hal_display_adv.cpp](../../src/hal/hal_display_adv.cpp#L151-L157)（硬件实现） / [hal_display_font.cpp](../../src/hal/hal_display_font.cpp#L223-L228)（native 桩）*

### 测试钩子

*📄 Source: [hal_display_adv.cpp](../../src/hal/hal_display_adv.cpp#L77-L86)*

```c
uint16_t hal_test_fb_read(int16_t x, int16_t y)
{
    if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) return 0;
    uint16_t v = g_framebuffer[y * SCREEN_WIDTH + x];
    if (g_font_fb != NULL) {
        uint16_t fv = g_font_fb[y * SCREEN_WIDTH + x];
        if (fv != 0) v = fv;
    }
    return v;
}
```

仅当定义了 `NATIVE_TEST` 时暴露，用于 `/dev/fb0` 与内核设备测试验证像素写入结果。

---

## 与其他组件的关系

- **ui_drawer**：调用 `hal_draw_xor_rect()` 实现选择器反色高亮
- **main.cpp**：每帧调用 `hal_display_clear()` → 绘制 → `hal_display_flush()`
- **hal_layout.h**：提供基于 `hal_get_font_height()` 和 `SCREEN_WIDTH/HEIGHT` 的布局宏
- **hal_screen.h**：集中定义运行时屏幕尺寸 `g_screen_width` / `g_screen_height`
- **dev_fb0.c**：通过 `hal_display_*` 原语实现 `/dev/fb0` 设备协议

---

> **See Also:** [输入系统](input.md) | [屏幕尺寸](screen.md) | [绘制管线](../ui/drawer.md)
