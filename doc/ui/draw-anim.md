# 退场动画（UI Draw Anim）

> **Parent:** [UI 核心层索引](index.md) | **Related:** [绘制管线](drawer.md), [核心引擎](core.md)

## 概述

`ui_draw_anim` 实现 UI 框架的**退场动画**，在用户按下"返回"键时播放。动画通过一个黑色遮罩从屏幕顶部向下展开，遮罩上带有像素艺术风格的沙漏图案和扫描线效果，营造流畅的页面切换体验。

---

## 动画状态机

*📄 Source: [ui_draw_anim.c](../../src/ui/ui_draw_anim.c#L122-L158)*

退场动画分为 3 个阶段，由 `g_xerintosh_exit_animation_status` 状态机控制。重构后（Task 9）所有状态已从函数局部 `static` 变量迁移到 `ui_context`。主入口 `xerintosh_draw_exit_animation()` 委托给三个子阶段函数：

```c
void xerintosh_draw_exit_animation()
{
    /* 每次新动画开始时强制重置状态，避免残留导致卡死 */
    if (g_xerintosh_exit_anim_last_finished && !g_xerintosh_exit_animation_finished) {
        g_xerintosh_exit_anim_temp_h = -8;
        g_xerintosh_exit_anim_temp_h_trg = SCREEN_HEIGHT + 8;
        g_xerintosh_exit_animation_status = 0;
    }
    g_xerintosh_exit_anim_last_finished = g_xerintosh_exit_animation_finished;

    /* 屏幕方向/尺寸切换时，防止目标值残留旧尺寸 */
    if (g_xerintosh_exit_anim_prev_screen_h != SCREEN_HEIGHT) {
        float max_h = (float)SCREEN_HEIGHT + 8.0f;
        if (g_xerintosh_exit_anim_temp_h > max_h) g_xerintosh_exit_anim_temp_h = max_h;
        if (g_xerintosh_exit_animation_status == 0) g_xerintosh_exit_anim_temp_h_trg = max_h;
        g_xerintosh_exit_anim_prev_screen_h = (int16_t)SCREEN_HEIGHT;
    }

    /* 绘制全屏黑色遮罩 */
    g_xerintosh_draw_color = COLOR_BG;
    hal_draw_fill_rect(0, 0, SCREEN_WIDTH, g_xerintosh_exit_anim_temp_h, g_xerintosh_draw_color);

    /* 委托子阶段函数 */
    uint8_t _x_hourglass_offset = SCREEN_WIDTH / 2 - 8;
    int8_t _y_hourglass = g_xerintosh_exit_anim_temp_h - SCREEN_HEIGHT / 2 - 18;
    if (_y_hourglass + 20 >= 0)
        draw_hourglass(_x_hourglass_offset, _y_hourglass);  /* 沙漏图案 */

    draw_scanlines();         /* 底部扫描线和交错像素 */

    xerintosh_animation(&g_xerintosh_exit_anim_temp_h, g_xerintosh_exit_anim_temp_h_trg, ANIM_SPEED_EXIT);

    update_exit_anim_state(); /* 状态机转换 */
}
```

### 中文伪代码拆解

```
函数 绘制退场动画() {
    // 第一步：新动画开始时重置状态（状态已从 static 迁移到 ui_context）
    if (上一帧完成 且 当前未完成) {
        遮罩当前高度 = -8
        遮罩目标高度 = 屏幕高度 + 8
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
    绘制实心矩形(0, 0, 屏幕宽, 遮罩当前高度)
    颜色 = 白

    // 第四步：在遮罩上绘制像素艺术沙漏
    沙漏X偏移 = 屏幕宽/2 - 8
    沙漏Y = 遮罩当前高度 - 屏幕高/2 - 18
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
    遮罩当前高度 向 遮罩目标高度 缓动

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

**核心思想**：用 `g_xerintosh_exit_anim_temp_h` 控制遮罩高度，经历"展开→停留→回缩"三个阶段。沙漏和扫描线都跟随遮罩边缘移动，形成"遮罩扫过屏幕"的视觉效果。重构后（Task 9）所有 `static` 局部状态已迁移到 `ui_context`，可通过 `xerintosh_context_init()` 统一重置。

---

## 状态机详解

*📄 Source: [ui_draw_anim.c](../../src/ui/ui_draw_anim.c#L91-L113)*

状态机逻辑已提取为独立函数 `update_exit_anim_state()`，使用范围判断代替浮点精确相等，避免多次循环后卡死：

```c
static void update_exit_anim_state(void)
{
    if (g_xerintosh_exit_animation_status == 0 && g_xerintosh_exit_anim_temp_h >= SCREEN_HEIGHT + 8 - 1.0f) {
        g_xerintosh_exit_anim_temp_h = SCREEN_HEIGHT + 8;
        g_xerintosh_exit_animation_status = 1;
        return;
    }
    if (g_xerintosh_exit_animation_status == 1) {
        g_xerintosh_exit_anim_temp_h_trg = -8;
        g_xerintosh_exit_animation_status = 2;
        return;
    }
    if (g_xerintosh_exit_animation_status == 2 && g_xerintosh_exit_anim_temp_h <= -8 + 1.0f) {
        g_xerintosh_exit_animation_finished = true;
        g_xerintosh_exit_animation_status = 0;
        g_xerintosh_exit_anim_temp_h = -8;
        g_xerintosh_exit_anim_temp_h_trg = SCREEN_HEIGHT + 8;
        return;
    }
}
```

状态机由 3 个阶段控制：
|------|------|------|------|
| 0 | 展开 | `g_xerintosh_exit_anim_temp_h` 从 -8 向 `SCREEN_HEIGHT+8` 增长 | 遮罩向下展开，沙漏和扫描线跟随 |
| 1 | 到达底部 | `g_xerintosh_exit_anim_temp_h >= SCREEN_HEIGHT+8-1` | 切换目标为 -8，立即进入状态2 |
| 2 | 回缩 | `g_xerintosh_exit_anim_temp_h` 向 -8 减小 | 遮罩向上回缩，完成后标记 `g_xerintosh_exit_animation_finished = true` |

**关键设计**：
- 使用范围判断（`>= max-1` 和 `<= min+1`）代替浮点精确相等，避免缓动精度累积导致卡死
- 每次新动画开始时强制重置 `g_xerintosh_exit_anim_temp_h` 和 `g_xerintosh_exit_anim_temp_h_trg`
- 监听 `SCREEN_HEIGHT` 变化，防止横竖屏切换后遮罩尺寸不匹配
- Task 9 重构后，所有状态从 `static` 局部变量迁移到 `ui_context`，支持通过 `xerintosh_context_init()` 统一重置

---

## 沙漏绘制

*📄 Source: [ui_draw_anim.c](../../src/ui/ui_draw_anim.c#L20-L64)*

沙漏绘制已提取为独立函数 `draw_hourglass()`，采用**像素艺术（pixel art）**风格，由多个精确的矩形和线条拼接而成：

```c
uint8_t _x_hourglass_offset = SCREEN_WIDTH / 2 - 8;
int8_t _y_hourglass = g_xerintosh_exit_anim_temp_h - SCREEN_HEIGHT / 2 - 18;

/* 上半部分：13x3 矩形，中间用背景色横线挖空 */
hal_draw_fill_rect(_x_hourglass_offset, _y_hourglass + 2, 13, 3, g_xerintosh_draw_color);
g_xerintosh_draw_color = COLOR_BG;
hal_draw_h_line(_x_hourglass_offset + 2, _y_hourglass + 3, 9, g_xerintosh_draw_color);
g_xerintosh_draw_color = COLOR_FG;

/* 两侧竖线 */
hal_draw_v_line(_x_hourglass_offset + 1, _y_hourglass + 4, 5, g_xerintosh_draw_color);
hal_draw_v_line(_x_hourglass_offset + 11, _y_hourglass + 4, 5, g_xerintosh_draw_color);

/* 中间收窄部分（5行，每行左右各2像素） */
for (uint8_t i = 0; i < 5; ++i) {
  int8_t _current_y = _y_hourglass + 8 + i;
  int8_t _left_x = (i < 3) ? (_x_hourglass_offset + 1 + i) : (_x_hourglass_offset + 4);
  int8_t _right_x = (i < 3) ? (_x_hourglass_offset + 10 - i) : (_x_hourglass_offset + 7);
  hal_draw_h_line(_left_x,  _current_y, 2, g_xerintosh_draw_color);
  hal_draw_h_line(_right_x, _current_y, 2, g_xerintosh_draw_color);
}

/* 下半部分（对称结构） */
for (uint8_t i = 0; i < 3; ++i) { ... }

/* 底部矩形 */
hal_draw_fill_rect(_x_hourglass_offset, _y_hourglass + 19, 13, 3, g_xerintosh_draw_color);
g_xerintosh_draw_color = COLOR_BG;
hal_draw_h_line(_x_hourglass_offset + 2, _y_hourglass + 20, 9, g_xerintosh_draw_color);
g_xerintosh_draw_color = COLOR_FG;

/* 11个颗粒点，模拟沙漏中的沙子 */
const uint8_t _points[][2] = {
  {5,7}, {7,7}, {6,8}, {6,10}, {6,14}, {6,16},
  {5,17}, {7,17}, {4,18}, {6,18}, {8,18}
};
```

**沙漏尺寸**：13 像素宽 × 21 像素高，水平居中于屏幕。

---

## 扫描线效果

*📄 Source: [ui_draw_anim.c](../../src/ui/ui_draw_anim.c#L69-L86)*

扫描线绘制已提取为独立函数 `draw_scanlines()`，两种扫描效果叠加在遮罩边缘：

1. **底部扫描线**：遮罩底部绘制 4 条水平白线
2. **交错像素扫描**：遮罩顶部附近 5 像素行，以棋盘格模式绘制像素，营造"数字消散"效果

---

## 与主循环的交互

退场动画在 `ui_core.c` 的主循环末尾调用，但双键模式下会被跳过：

*📄 Source: [ui_core.c](../../src/ui/ui_core.c#L344-L368)*

```c
/* ui_core.c */
if (!g_xerintosh_exit_animation_finished &&
    (s_dual_key_active_cb == NULL || !s_dual_key_active_cb()))
  xerintosh_draw_exit_animation();
```

`xerintosh_set_dual_key_callback()` 允许 App 层注册一个回调，返回 `true` 时表示当前处于双键按住模式（如 power_key_popup 的关机确认）。此时 UI 核心跳过退场遮罩动画，让 App 自行处理视觉反馈。

在动画播放期间，框架输入被禁止（`app_input_process()` 中检查 `g_xerintosh_exit_animation_finished`），避免用户误操作。

---

> **See Also:** [绘制管线](drawer.md) | [核心引擎](core.md) | [列表绘制](draw-list.md)
