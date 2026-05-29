# 图标绘制（UI Draw Icons）

> **Parent:** [UI 核心层索引](index.md) | **Related:** [绘制管线](drawer.md), [列表绘制](draw-list.md)

## 概述

`ui_draw_icons` 是 UI 框架的**图标绘制模块**，负责绘制列表项左侧的内置图标（列表、开关、加号、滑条、用户、旗帜、电源）和自定义 XBM 位图图标。

---

## 内置图标

*📄 Source: [ui_draw_icons.c](../../src/ui/ui_draw_icons.c#L20-L60)*

```c
void xerintosh_draw_list_icon(xerintosh_list_item_icon_t icon, uint16_t x, uint16_t y)
{
  switch (icon) {
  case list_icon:
    /* 三条不等长横线，模拟列表外观 */
    hal_draw_h_line(2 + x, y - 2, 4, g_xerintosh_draw_color);
    hal_draw_h_line(2 + x, y,     5, g_xerintosh_draw_color);
    hal_draw_h_line(2 + x, y + 2, 3, g_xerintosh_draw_color);
    break;

  case switch_icon:
    /* 圆形开关 */
    hal_draw_circle(4 + x, y + 1, 3, g_xerintosh_draw_color);
    hal_draw_v_line(4 + x, y, 3, g_xerintosh_draw_color);
    break;

  case plus_icon:
    /* "+" 号，圆形背景 */
    hal_draw_circle(4 + x, y + 1, 3, g_xerintosh_draw_color);
    hal_draw_v_line(4 + x, y, 3, g_xerintosh_draw_color);
    hal_draw_h_line(3 + x, y + 1, 3, g_xerintosh_draw_color);
    break;

  case slider_icon:
    /* 两条竖线 + 两个方块，模拟滑条 */
    hal_draw_v_line(3 + x, y - 1, 5, g_xerintosh_draw_color);
    hal_draw_v_line(6 + x, y - 1, 5, g_xerintosh_draw_color);
    hal_draw_fill_rect(2 + x, y - 2, 3, 3, g_xerintosh_draw_color);
    hal_draw_fill_rect(5 + x, y + 2, 3, 3, g_xerintosh_draw_color);
    break;

  case user_icon:
    /* 短横线 "-" */
    hal_draw_string(2 + x, y + hal_get_font_height() / 2, "-", g_xerintosh_draw_color);
    break;

  case flag_icon:
    /* 旗帜：竖线 + 填充矩形 */
    hal_draw_v_line(6 + x, y - 1, 5, g_xerintosh_draw_color);
    hal_draw_fill_rect(3 + x, y - 2, 4, 3, g_xerintosh_draw_color);
    break;

  case power_icon:
    /* 电源符号：圆 + 竖线，顶部两个像素用背景色覆盖 */
    hal_draw_circle(4 + x, y + 1, 3, g_xerintosh_draw_color);
    hal_draw_v_line(4 + x, y - 2, 3, g_xerintosh_draw_color);
    g_xerintosh_draw_color = COLOR_BG;
    hal_draw_pixel(x + 3, y - 2, g_xerintosh_draw_color);
    hal_draw_pixel(x + 5, y - 2, g_xerintosh_draw_color);
    g_xerintosh_draw_color = COLOR_FG;
    break;

  default:
    break;
  }
}
```

### 图标类型对照

| 枚举值 | 外观 | 绘制内容 | 用途 |
|--------|------|----------|------|
| `list_icon` | 三条不等长横线（长4/5/3） | `hal_draw_h_line` ×3 | 普通列表项 |
| `switch_icon` | 圆形 + 竖线 | `hal_draw_circle` + `hal_draw_v_line` | 开关项 |
| `plus_icon` | 圆形 + 十字 | `hal_draw_circle` + `hal_draw_v_line` + `hal_draw_h_line` | 按钮项 |
| `slider_icon` | 两条竖线 + 两个方块 | `hal_draw_v_line` ×2 + `hal_draw_fill_rect` ×2 | 滑条项 |
| `user_icon` | 短横线 "-" | `hal_draw_string("-")` | 用户自定义页 |
| `flag_icon` | 竖线 + 矩形旗面 | `hal_draw_v_line` + `hal_draw_fill_rect` | 标记/状态项 |
| `power_icon` | 电源符号（圆+竖线，顶部缺口） | `hal_draw_circle` + `hal_draw_v_line` + 背景色像素覆盖 | 电源控制 |
| `custom_icon` | 自定义 XBM | `hal_draw_xbitmap` | 开发者自定义图标 |

---

## 自定义位图图标

*📄 Source: [ui_draw_icons.c](../../src/ui/ui_draw_icons.c#L69-L78)*

列表项支持通过 `bitmap_data` 字段使用自定义 XBM（X BitMap）格式图标：

```c
void xerintosh_draw_item_bitmap(xerintosh_list_item_t *_item, uint16_t x, uint16_t y)
{
  if (_item == NULL || _item->bitmap_data == NULL ||
      _item->bitmap_w == 0 || _item->bitmap_h == 0)
    return;

  int16_t draw_x = x;
  int16_t draw_y = y - _item->bitmap_h / 2;
  hal_draw_xbitmap(draw_x, draw_y, _item->bitmap_w, _item->bitmap_h, _item->bitmap_data);
}
```

### 中文伪代码拆解

```
函数 绘制自定义位图图标(项, 屏幕X, 屏幕Y) {
    if (项为空 或 没有位图数据 或 宽高为0) return

    绘制X = 屏幕X
    绘制Y = 屏幕Y - 位图高度/2   // Y为中心线，图标垂直居中

    调用HAL绘制XBM位图(绘制X, 绘制Y, 宽, 高, 数据)
}
```

### 如何使用自定义图标

```c
/* 定义 XBM 数据（8x8 像素） */
static const uint8_t my_icon_bits[] = {
    0x18, 0x3C, 0x7E, 0xDB, 0xDB, 0x7E, 0x3C, 0x18
};

/* 创建列表项并绑定图标 */
xerintosh_list_item_t* item = xerintosh_new_list_item("自定义项", custom_icon);
item->bitmap_data = my_icon_bits;
item->bitmap_w = 8;
item->bitmap_h = 8;
```

**XBM 格式**：每行像素以字节为单位存储，最低有效位（LSB）对应最左侧像素。宽度不是 8 的倍数时，每行向上取整到整字节。

---

## 图标尺寸规范

| 参数 | 值 | 说明 |
|------|-----|------|
| 图标宽度 | 8-13 像素 | 根据图标类型略有不同 |
| 图标高度 | 6-8 像素 | 根据图标类型略有不同 |
| 绘制基准点 | `(x, y)` | `x` 为左上角，`y` 为中心线，图标上下对称分布 |
| 自定义位图限制 | 最大 32x32 | 受 `bitmap_w`/`bitmap_h` 字段为 `uint8_t` 限制 |

---

> **See Also:** [绘制管线](drawer.md) | [列表绘制](draw-list.md) | [项目系统](item.md)
