# 屏幕尺寸自适应布局重构计划

## 目标
1. 创建 `src/hal/hal_layout.h` — HAL级布局原语模块，所有app统一使用
2. 重构所有app的硬编码像素值为布局原语
3. Task Manager 标题水平居中

## 架构

```
新增: src/hal/hal_layout.h  → 所有 app 的布局计算统一源
修改: taskmgr / sm_ui / about / boot → 替换硬编码为 HAL 宏
不动: src/ui/ (菜单框架) / wifi-mgr / bt-mgr (无自绘 UI)
```

## hal_layout.h API 三层

### 层1: 原始常量
- `HAL_MARGIN_SM (2)` — 小边距（按钮padding, 分隔线间距）
- `HAL_MARGIN_MD (4)` — 中边距（标准左右缩进）
- `HAL_MARGIN_LG (8)` — 大边距（区块间间距）

### 层2: 语义尺寸
- `HAL_ROW_H()` = `hal_get_font_height() + 2*HAL_MARGIN_SM` — 标准行高

### 层3: 区域对齐
- `HAL_CENTER_X(w)` — 宽度 w 的元素水平居中
- `HAL_CENTER_Y(h)` — 高度 h 的元素垂直居中
- `HAL_RIGHT_X(w)` — 右对齐
- `HAL_LEFT_X()` — 左对齐（标准缩进, = HAL_MARGIN_MD）
- `HAL_TEXT_BASELINE(row_top_y)` — 行内文字基线
- `HAL_HEADER_TOP()` / `HAL_HEADER_BOTTOM()` — Header区域边界
- `HAL_FOOTER_TOP()` / `HAL_FOOTER_BOTTOM()` — Footer区域边界
- `HAL_BODY_TOP()` / `HAL_BODY_BOTTOM()` / `HAL_BODY_HEIGHT()` — Body区域

## 实施步骤

### Step 1: 创建 hal_layout.h
- 新文件: `src/hal/hal_layout.h`
- 包含 hal_display.h
- 全部用 #define 宏（C兼容，零运行时开销）
- extern "C" 守卫

### Step 2: Boot Screen (src/app/boot/boot_screen.c)
- 替换 `(sw - body_w)/2 - 4` → `HAL_CENTER_X(body_w) - HAL_MARGIN_SM*2`
- 替换 `+8` → `HAL_MARGIN_LG`

### Step 3: About Page (src/app/about/about.c)
- `ABOUT_LEFT_MARGIN` → `HAL_MARGIN_MD`
- `ABOUT_SEP_GAP` → `HAL_MARGIN_MD + HAL_MARGIN_SM*3`
- `ABOUT_INFO_GAP` → `HAL_MARGIN_MD + HAL_MARGIN_SM*3 + ...`
- 居中用 `HAL_CENTER_Y()`

### Step 4: Serial Monitor (src/app/serial_monitor/sm_ui.c)
- `bar_h` → `HAL_ROW_H()`
- `bar_y=1` → `HAL_MARGIN_SM`
- `start_x=2` → `HAL_MARGIN_SM`
- `spacing` 中的 `4` → `HAL_MARGIN_MD*2`
- `max_line_width` 中的 `4` → `HAL_MARGIN_MD*2`
- 文字 x `2` → `HAL_MARGIN_SM`

### Step 5: Task Manager 常量 (src/app/taskmgr/taskmgr.h)
- `TASKMGR_HEADER_Y` → `HAL_HEADER_TOP()`
- `TASKMGR_LEFT_MARGIN` → `HAL_MARGIN_MD`
- 新增 `TASKMGR_HEADER_H`, `TASKMGR_FOOTER_H`

### Step 6: Task Manager draw_header (居中标题)
- `hal_draw_string(TASKMGR_LEFT_MARGIN, ...)` → 使用 `HAL_CENTER_X(title_w)`
- 分隔线 `sep_y` → `HAL_HEADER_BOTTOM()`

### Step 7: Task Manager draw_footer
- `footer_baseline` → `HAL_FOOTER_BOTTOM() - HAL_MARGIN_SM`
- 分隔线 → `HAL_FOOTER_TOP()`

### Step 8: Task Manager draw_confirm_overlay
- `bw=130` → `SCREEN_WIDTH * 7 / 8`
- `cy = SCREEN_HEIGHT/2 - 12` → `HAL_CENTER_Y(bh)`
- 文字使用 `HAL_CENTER_X()`

### Step 9: Task Manager visible_lines + init
- `header_h` → `TASKMGR_HEADER_H`
- `footer_h` → `TASKMGR_FOOTER_H`
- `list_top` → `HAL_HEADER_BOTTOM()`

### Step 10: 硬件测试
- `pio run -e m5stick-c -t upload`
- 目视检查所有界面

## 每步验证
```bash
pio run -e native && pio run -e m5stick-c
```

## 不变更范围
- src/ui/ui_item.h (框架常量)
- src/ui/ui_drawer.c (框架渲染)
- wifi/bt manager (无自绘UI)
- ui_anim_row (动画引擎)
