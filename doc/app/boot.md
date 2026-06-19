# 开机画面（Boot Screen）

> **Parent:** [App 层索引](index.md) | **Related:** [关于页面](about.md), [入口流程](../index.md)

## 概述

`boot_screen` 模块负责设备上电后显示 Macintosh 128K 风格的开机画面：白色圆角矩形机身、黑色屏幕区域、软盘槽与 "Xerintosh" 品牌文字。画面元素被封装为可复用的 `boot_screen_draw_logo()`，供开机画面和关于页面共用。

## 关键概念

### Logo 绘制

*📄 Source: [boot_screen.c](../../src/app/boot/boot_screen.c#L22-L52)*

```c
void boot_screen_draw_logo(int16_t ox, int16_t oy, int16_t scale)
{
    int16_t body_w = 44 * scale / 100;
    int16_t body_h = 60 * scale / 100;
    if (body_w < 20) body_w = 20;
    if (body_h < 20) body_h = 20;

    int16_t bezel    = (scale < 80) ? 2 : 3;
    int16_t screen_w = body_w - bezel * 2;
    int16_t screen_h = body_h * 45 / 100;
    int16_t screen_x = ox + bezel;
    int16_t screen_y = oy + bezel;

    /* 机身（白色圆角矩形轮廓） */
    hal_draw_round_rect(ox, oy, body_w, body_h, 3, COLOR_FG);

    /* 屏幕区域（黑色填充） */
    hal_draw_fill_rect(screen_x, screen_y, screen_w, screen_h, COLOR_BG);

    /* 屏幕边框（白色） */
    hal_draw_rect(screen_x, screen_y, screen_w, screen_h, COLOR_FG);

    /* 软盘槽（白色填充） */
    hal_draw_fill_rect(slot_x, slot_y, slot_w, slot_h, COLOR_FG);
}
```

`scale` 为百分比缩放因子（100 表示原始大小）。函数内部会根据缩放值动态计算机身、屏幕、软盘槽的尺寸与位置，并自动 clamp 最小尺寸，确保在小尺寸下仍可辨识。

### 完整开机画面

*📄 Source: [boot_screen.c](../../src/app/boot/boot_screen.c#L57-L79)*

```c
void boot_screen_show(void)
{
    int16_t body_w = 44;
    int16_t body_h = 60;
    int16_t body_x = HAL_CENTER_X(body_w);
    int16_t body_y = HAL_CENTER_Y(body_h) - HAL_MARGIN_SM * 2;

    hal_display_clear();

    boot_screen_draw_logo(body_x, body_y, 100);

    const char* label = "Xerintosh";
    hal_set_font(NULL);
    int16_t tw = hal_get_string_width(label);
    int16_t tx = HAL_CENTER_X(tw);
    int16_t ty = body_y + body_h + HAL_MARGIN_LG;
    hal_draw_string(tx, ty, label, COLOR_FG);

    hal_display_flush();
}
```

`boot_screen_show()` 先清屏，再居中绘制 logo 与品牌文字，最后调用 `hal_display_flush()` 将后台缓冲区推送到 TFT。注意延时由 `main.cpp` 的 `setup()` 控制，本函数内部不阻塞，避免触发看门狗。

## 公共 API

*📄 Source: [boot_screen.h](../../src/app/boot/boot_screen.h#L22-L31)*

```c
void boot_screen_show(void);
void boot_screen_draw_logo(int16_t ox, int16_t oy, int16_t scale);
```

| 函数 | 说明 |
|------|------|
| `boot_screen_show()` | 显示完整开机画面并刷新屏幕 |
| `boot_screen_draw_logo()` | 在指定位置以指定缩放绘制设备 logo |

---

> **See Also:** [关于页面](about.md) | [应用初始化](app-init.md) | [主入口点](../../src/main.cpp)
