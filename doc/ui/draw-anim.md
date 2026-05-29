# 退场动画（UI Draw Anim）

> **Parent:** [UI 核心层索引](index.md) | **Related:** [绘制管线](drawer.md), [核心引擎](core.md)

## 概述

`ui_draw_anim` 实现 UI 框架的**退场动画**，在用户按下"返回"键时播放。动画通过一个黑色遮罩从屏幕顶部向下展开，遮罩上带有像素艺术风格的沙漏图案和扫描线效果，营造流畅的页面切换体验。

---

## 动画状态机

*📄 Source: [ui_draw_anim.c](../../src/ui/ui_draw_anim.c#L14-L143)*

退场动画分为 3 个阶段，由 `g_xerintosh_exit_animation_status` 状态机控制：

```c
void xerintosh_draw_exit_animation()
{
  static float _temp_h = -8;          /* 遮罩当前高度 */
  static float _temp_h_trg = -999;    /* 遮罩目标高度 */
  static bool _last_finished = true;  /* 上一帧是否已完成 */

  /* 每次新动画开始时强制重置状态 */
  if (_last_finished && !g_xerintosh_exit_animation_finished)
  {
    _temp_h = -8;
    _temp_h_trg = SCREEN_HEIGHT + 8;
    g_xerintosh_exit_animation_status = 0;
  }
  _last_finished = g_xerintosh_exit_animation_finished;

  /* 屏幕方向切换时防止残留旧尺寸目标值 */
  static int16_t _prev_screen_height = -1;
  if (_prev_screen_height != SCREEN_HEIGHT)
  {
    float max_h = SCREEN_HEIGHT + 8;
    if (_temp_h > max_h) _temp_h = max_h;
    if (g_xerintosh_exit_animation_status == 0) _temp_h_trg = max_h;
    _prev_screen_height = SCREEN_HEIGHT;
  }

  /* 绘制全屏黑色遮罩，高度由 _temp_h 控制 */
  g_xerintosh_draw_color = COLOR_BG;
  hal_draw_fill_rect(0, 0, SCREEN_WIDTH, _temp_h, g_xerintosh_draw_color);
  g_xerintosh_draw_color = COLOR_FG;

  /* 沙漏绘制（像素艺术风格） */
  uint8_t _x_hourglass_offset = SCREEN_WIDTH / 2 - 8;
  int8_t _y_hourglass = _temp_h - SCREEN_HEIGHT / 2 - 18;
  if (_y_hourglass + 20 >= 0)
  {
    /* 沙漏上半部分、中间填充、下半部分、颗粒点 */
    hal_draw_fill_rect(_x_hourglass_offset, _y_hourglass + 2, 13, 3, ...);
    /* ... 详细像素绘制 ... */
  }

  /* 底部扫描线和交错像素效果 */
  if (_temp_h + 3 >= 0)
    for (uint8_t i = 0; i <= 3; ++i)
      hal_draw_h_line(0, _temp_h + i, SCREEN_WIDTH, g_xerintosh_draw_color);

  for (int16_t i = 0; i <= SCREEN_WIDTH; i += 2)
    for (int16_t j = _temp_h - 5; j <= _temp_h - 1; j++)
    {
      if (j % 2 == 0) hal_draw_pixel(i + 1, j, g_xerintosh_draw_color);
      if (j % 2 == 1) hal_draw_pixel(i, j, g_xerintosh_draw_color);
    }

  /* 缓动动画 */
  xerintosh_animation(&_temp_h, _temp_h_trg, ANIM_SPEED_EXIT);

  /* 状态机转换 */
  if (g_xerintosh_exit_animation_status == 0 && _temp_h >= SCREEN_HEIGHT + 8 - 1.0f)
  {
    _temp_h = SCREEN_HEIGHT + 8;
    g_xerintosh_exit_animation_status = 1;
    return;
  }
  if (g_xerintosh_exit_animation_status == 1)
  {
    _temp_h_trg = -8;
    g_xerintosh_exit_animation_status = 2;
    return;
  }
  if (g_xerintosh_exit_animation_status == 2 && _temp_h <= -8 + 1.0f)
  {
    g_xerintosh_exit_animation_finished = true;
    g_xerintosh_exit_animation_status = 0;
    _temp_h = -8;
    _temp_h_trg = SCREEN_HEIGHT + 8;
    return;
  }
}
```

### 中文伪代码拆解

```
函数 绘制退场动画() {
    静态变量 遮罩高度 = -8
    静态变量 遮罩目标 = -999
    静态变量 上一帧完成 = true

    // 第一步：新动画开始时重置状态
    if (上一帧完成 且 当前未完成) {
        遮罩高度 = -8
        遮罩目标 = 屏幕高度 + 8
        动画状态 = 0
    }
    上一帧完成 = 当前完成标志

    // 第二步：屏幕方向切换保护
    if (屏幕高度变了) {
        限制遮罩高度不超过新屏幕高度
        if (状态0) 遮罩目标 = 新屏幕高度 + 8
    }

    // 第三步：绘制黑色遮罩（从顶部向下展开）
    颜色 = 黑
    绘制实心矩形(0, 0, 屏幕宽, 遮罩高度)
    颜色 = 白

    // 第四步：在遮罩上绘制像素艺术沙漏
    沙漏X偏移 = 屏幕宽/2 - 8
    沙漏Y = 遮罩高度 - 屏幕高/2 - 18
    if (沙漏在可视区内) {
        绘制沙漏上半部分（矩形+横线挖空）
        绘制中间收窄部分（循环画短横线）
        绘制下半部分（对称结构）
        绘制颗粒点（_points数组定义11个像素点）
    }

    // 第五步：底部扫描线效果
    if (遮罩底部在可视区) {
        绘制4条水平扫描线（遮罩底部）
    }
    // 交错像素扫描（棋盘格效果）
    for (x每隔2像素) {
        for (y在遮罩顶部附近5像素) {
            if (y偶数) 画像素(x+1, y)
            if (y奇数) 画像素(x, y)
        }
    }

    // 第六步：缓动动画
    遮罩高度 向 遮罩目标 缓动

    // 第七步：状态机转换
    if (状态0 且 遮罩到达底部) {
        切换到状态1
    }
    if (状态1) {
        遮罩目标 = -8   // 开始回缩
        切换到状态2
    }
    if (状态2 且 遮罩回缩到顶部) {
        标记动画完成
        重置所有状态
    }
}
```

**核心思想**：用 `_temp_h` 控制遮罩高度，经历"展开→停留→回缩"三个阶段。沙漏和扫描线都跟随遮罩边缘移动，形成"遮罩扫过屏幕"的视觉效果。

---

## 状态机详解

| 状态 | 名称 | 条件 | 行为 |
|------|------|------|------|
| 0 | 展开 | `_temp_h` 从 -8 向 `SCREEN_HEIGHT+8` 增长 | 遮罩向下展开，沙漏和扫描线跟随 |
| 1 | 到达底部 | `_temp_h >= SCREEN_HEIGHT+8-1` | 切换目标为 -8，立即进入状态2 |
| 2 | 回缩 | `_temp_h` 向 -8 减小 | 遮罩向上回缩，完成后标记 `g_xerintosh_exit_animation_finished = true` |

**关键设计**：
- 使用范围判断（`>= max-1` 和 `<= min+1`）代替浮点精确相等，避免缓动精度累积导致卡死
- 每次新动画开始时强制重置 `_temp_h` 和 `_temp_h_trg`，防止 static 变量残留导致异常
- 监听 `SCREEN_HEIGHT` 变化，防止横竖屏切换后遮罩尺寸不匹配

---

## 沙漏绘制

*📄 Source: [ui_draw_anim.c](../../src/ui/ui_draw_anim.c#L55-L102)*

沙漏采用**像素艺术（pixel art）**风格，由多个精确的矩形和线条拼接而成：

```c
uint8_t _x_hourglass_offset = SCREEN_WIDTH / 2 - 8;
int8_t _y_hourglass = _temp_h - SCREEN_HEIGHT / 2 - 18;

/* 上半部分：13x3 矩形，中间用背景色横线挖空 */
hal_draw_fill_rect(_x_hourglass_offset, _y_hourglass + 2, 13, 3, COLOR_FG);
hal_draw_h_line(_x_hourglass_offset + 2, _y_hourglass + 3, 9, COLOR_BG);

/* 两侧竖线 */
hal_draw_v_line(_x_hourglass_offset + 1, _y_hourglass + 4, 5, COLOR_FG);
hal_draw_v_line(_x_hourglass_offset + 11, _y_hourglass + 4, 5, COLOR_FG);

/* 中间收窄部分（5行，每行左右各2像素） */
for (uint8_t i = 0; i < 5; ++i) {
  int8_t left_x  = (i < 3) ? (offset + 1 + i) : (offset + 4);
  int8_t right_x = (i < 3) ? (offset + 10 - i) : (offset + 7);
  hal_draw_h_line(left_x,  _y_hourglass + 8 + i, 2, COLOR_FG);
  hal_draw_h_line(right_x, _y_hourglass + 8 + i, 2, COLOR_FG);
}

/* 下半部分（对称结构） */
for (uint8_t i = 0; i < 3; ++i) { ... }

/* 底部矩形 */
hal_draw_fill_rect(_x_hourglass_offset, _y_hourglass + 19, 13, 3, COLOR_FG);

/* 11个颗粒点，模拟沙漏中的沙子 */
const uint8_t _points[][2] = {
  {5,7}, {7,7}, {6,8}, {6,10}, {6,14}, {6,16},
  {5,17}, {7,17}, {4,18}, {6,18}, {8,18}
};
```

**沙漏尺寸**：13 像素宽 × 21 像素高，水平居中于屏幕。

---

## 扫描线效果

*📄 Source: [ui_draw_anim.c](../../src/ui/ui_draw_anim.c#L104-L117)*

两种扫描效果叠加在遮罩边缘：

1. **底部扫描线**：遮罩底部绘制 4 条水平白线
2. **交错像素扫描**：遮罩顶部附近 5 像素行，以棋盘格模式绘制像素，营造"数字消散"效果

---

## 与主循环的交互

退场动画在 `ui_core.c` 的主循环末尾调用：

```c
/* ui_core.c */
if (!g_xerintosh_exit_animation_finished)
  xerintosh_draw_exit_animation();
```

在动画播放期间，框架输入被禁止（`app_input_process()` 中检查 `g_xerintosh_exit_animation_finished`），避免用户误操作。

---

> **See Also:** [绘制管线](drawer.md) | [核心引擎](core.md) | [列表绘制](draw-list.md)
