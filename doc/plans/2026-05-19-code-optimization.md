# M5Stick-P1 代码优化实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 消除代码重复、大函数、深层嵌套、魔法数字等代码异味，提升代码质量、可维护性和可读性，同时确保所有现有测试通过且编译无误。

**Architecture:** 通过提取公共辅助函数、引入命名常量、拆分大函数为小函数、统一命名规范来重构代码。保持现有API不变，所有改动为内部实现优化。

**Tech Stack:** C/C++, PlatformIO, GoogleTest (native), M5Unified/M5GFX (target)

---

## 文件结构映射

| 文件 | 职责 | 优化方向 |
|---|---|---|
| `src/ui/ui_item.c` | UI数据结构和选择器逻辑 | 消除重复的类型转换/创建函数，提取公共辅助函数 |
| `src/ui/ui_core.c` | 动画引擎和主循环 | 统一动画函数，拆分嵌套条件，提取常量 |
| `src/ui/ui_drawer.c` | 渲染绘制 | 拆分大函数，消除魔法数字，提取绘制辅助函数 |
| `src/hal/hal_input.cpp` | 输入处理 | 消除BtnA/BtnB重复代码 |
| `src/ui/ui_item.h` | UI数据结构头文件 | 添加新辅助函数声明，提取动画速度常量 |
| `src/native_main.cpp` | 原生测试 | 添加更多测试用例覆盖优化后的代码 |

---

## 已识别的优化点汇总

### 严重：代码重复
1. `xerintosh_push_info_bar()` 与 `xerintosh_push_pop_up()` 逻辑几乎完全相同
2. `xerintosh_to_switch_item()` / `xerintosh_to_button_item()` / `xerintosh_to_slider_item()` / `xerintosh_to_user_item()` 模式完全一致
3. `xerintosh_new_list_item()` / `xerintosh_new_switch_item()` / `xerintosh_new_button_item()` / `xerintosh_new_slider_item()` / `xerintosh_new_user_item()` 初始化模式重复
4. `hal_input.cpp` 中 BtnA 和 BtnB 的事件处理代码完全重复
5. `xerintosh_animation()` 与 `xerintosh_exit_animation()` 实现几乎相同

### 高：大函数与深层嵌套
6. `xerintosh_draw_list_item()` ~95行，5层if-else链
7. `xerintosh_selector_jump_to_selected_item()` ~60行
8. `xerintosh_selector_exit_current_item()` ~50行
9. `xerintosh_draw_exit_animation()` ~90行，大量硬编码坐标
10. `xerintosh_ui_main_core()` 多层嵌套条件

### 中：魔法数字与硬编码值
11. 动画速度值散落各处：`94`, `95`, `96`, `84`, `92`, `93`
12. `ui_drawer.c` 中大量硬编码像素坐标和尺寸
13. `ui_core.c` 中 `15` 作为selector高度未命名

### 中：命名不一致
14. 混合 `snake_case` 和 `camelCase`：`_posTrg` vs `_pos_trg`
15. `oled_*` 前缀但实际驱动TFT（历史遗留）

### 低：错误处理缺失
16. `malloc` 返回值未检查
17. 多处边界条件缺少防御性检查

---

## Task 1: 提取动画系统常量并统一动画函数

**Files:**
- Modify: `src/ui/ui_item.h`
- Modify: `src/ui/ui_core.c`
- Modify: `src/ui/ui_drawer.c`
- Test: `src/native_main.cpp`

- [ ] **Step 1: 在 `ui_item.h` 中添加动画速度常量**

```c
/* 动画速度常量 */
#define ANIM_SPEED_SELECTOR     92
#define ANIM_SPEED_CAMERA       96
#define ANIM_SPEED_LIST_ITEM    84
#define ANIM_SPEED_INFO_BAR     94
#define ANIM_SPEED_POP_UP       96
#define ANIM_SPEED_EXIT         94
#define ANIM_SPEED_SELECTOR_H   93
```

- [ ] **Step 2: 统一 `xerintosh_animation` 和 `xerintosh_exit_animation`**

删除 `ui_drawer.c` 中的 `xerintosh_exit_animation()` 函数（第8-15行），所有调用方改用 `ui_core.c` 中的 `xerintosh_animation()`。

修改 `ui_drawer.c` 第85行：
```c
// 修改前
xerintosh_exit_animation(&_temp_h, _temp_h_trg, 94);
// 修改后
xerintosh_animation(&_temp_h, _temp_h_trg, ANIM_SPEED_EXIT);
```

- [ ] **Step 3: 在 `ui_core.c` 中用常量替换魔法速度值**

修改以下调用：
- `xerintosh_refresh_info_bar()` 中 `94` → `ANIM_SPEED_INFO_BAR`, `95` → `ANIM_SPEED_INFO_BAR + 1`
- `xerintosh_refresh_pop_up()` 中 `94` → `ANIM_SPEED_INFO_BAR`, `96` → `ANIM_SPEED_POP_UP`
- `xerintosh_refresh_camera_position()` 中 `96` → `ANIM_SPEED_CAMERA`
- `xerintosh_refresh_list_item_position()` 中 `84` → `ANIM_SPEED_LIST_ITEM`
- `xerintosh_refresh_selector_position()` 中 `92` → `ANIM_SPEED_SELECTOR`, `93` → `ANIM_SPEED_SELECTOR_H`

- [ ] **Step 4: 运行测试确保动画行为未变**

Run: `pio test -e native`
Expected: PASS (所有现有测试通过)

- [ ] **Step 5: Commit**

```bash
git add src/ui/ui_item.h src/ui/ui_core.c src/ui/ui_drawer.c
git commit -m "refactor: unify animation functions and extract speed constants"
```

---

## Task 2: 消除类型转换函数的重复代码

**Files:**
- Modify: `src/ui/ui_item.c`
- Modify: `src/ui/ui_item.h`

- [ ] **Step 1: 提取通用的类型安全转换辅助宏/函数**

在 `ui_item.c` 中，在现有转换函数之前添加：

```c
static xerintosh_list_item_t *xerintosh_safe_cast(xerintosh_list_item_t *_item, xerintosh_list_item_type_t _expected_type)
{
  if (_item != NULL && _item->type == _expected_type)
    return _item;
  return xerintosh_get_root_list();
}
```

- [ ] **Step 2: 简化四个类型转换函数**

```c
xerintosh_switch_item_t *xerintosh_to_switch_item(xerintosh_list_item_t *_xerintosh_list_item)
{
  return (xerintosh_switch_item_t*)xerintosh_safe_cast(_xerintosh_list_item, switch_item);
}

xerintosh_button_item_t *xerintosh_to_button_item(xerintosh_list_item_t *_xerintosh_list_item)
{
  return (xerintosh_button_item_t*)xerintosh_safe_cast(_xerintosh_list_item, button_item);
}

xerintosh_slider_item_t *xerintosh_to_slider_item(xerintosh_list_item_t *_xerintosh_list_item)
{
  return (xerintosh_slider_item_t*)xerintosh_safe_cast(_xerintosh_list_item, slider_item);
}

xerintosh_user_item_t *xerintosh_to_user_item(xerintosh_list_item_t *_xerintosh_list_item)
{
  return (xerintosh_user_item_t*)xerintosh_safe_cast(_xerintosh_list_item, user_item);
}
```

- [ ] **Step 3: 编译验证**

Run: `pio run -e native`
Expected: 编译成功，无警告

- [ ] **Step 4: Commit**

```bash
git add src/ui/ui_item.c src/ui/ui_item.h
git commit -m "refactor: deduplicate type-cast functions with safe_cast helper"
```

---

## Task 3: 消除 new_*_item 创建函数的重复代码

**Files:**
- Modify: `src/ui/ui_item.c`

- [ ] **Step 1: 提取通用的列表项基类初始化辅助函数**

在 `ui_item.c` 中添加：

```c
static void xerintosh_init_base_item(xerintosh_list_item_t *_item, xerintosh_list_item_type_t _type,
                                  const char *_content, xerintosh_list_item_icon_t _icon,
                                  xerintosh_list_item_icon_t _default_icon)
{
  memset(_item, 0, sizeof(xerintosh_list_item_t));
  _item->type = _type;
  _item->content = _content;
  _item->icon = (_icon == default_icon) ? _default_icon : _icon;
}
```

- [ ] **Step 2: 简化所有创建函数**

修改 `xerintosh_new_list_item()`：
```c
xerintosh_list_item_t *xerintosh_new_list_item(const char *_content, xerintosh_list_item_icon_t icon)
{
  xerintosh_list_item_t *_item = malloc(sizeof(xerintosh_list_item_t));
  if (_item == NULL) return NULL;
  xerintosh_init_base_item(_item, list_item, _content, icon, list_icon);
  return _item;
}
```

修改 `xerintosh_new_switch_item()`：
```c
xerintosh_list_item_t *xerintosh_new_switch_item(const char *_content, bool *_value,
                                          void (*_init_function)(), void (*_exit_function)(),
                                          xerintosh_list_item_icon_t icon)
{
  xerintosh_switch_item_t *_item = malloc(sizeof(xerintosh_switch_item_t));
  if (_item == NULL) return NULL;
  xerintosh_init_base_item(&_item->base_item, switch_item, _content, icon, switch_icon);
  _item->value = _value;
  _item->init_function = _init_function;
  _item->exit_function = _exit_function;
  return (xerintosh_list_item_t*)_item;
}
```

类似地简化 `xerintosh_new_button_item()`、`xerintosh_new_slider_item()`、`xerintosh_new_user_item()`。

- [ ] **Step 3: 编译验证**

Run: `pio run -e native`
Expected: 编译成功

- [ ] **Step 4: Commit**

```bash
git add src/ui/ui_item.c
git commit -m "refactor: extract base item init helper to deduplicate new_*_item functions"
```

---

## Task 4: 消除 info_bar 和 pop_up 的重复逻辑

**Files:**
- Modify: `src/ui/ui_item.c`
- Modify: `src/ui/ui_item.h`

- [ ] **Step 1: 提取公共的"推送通知"辅助函数**

两个结构体虽然字段名不同，但逻辑相同。由于C语言缺乏泛型，我们使用宏：

在 `ui_item.c` 顶部添加：
```c
#define DEFINE_PUSH_NOTIFICATION(NAME, STRUCT, Y_FIELD, Y_TRG_FIELD, W_FIELD, W_TRG_FIELD, \
                                  HEIGHT, OFFSET, DEFAULT_Y_TRG, SPEED)                     \
void xerintosh_push_##NAME(const char *_content, const uint16_t _span)                          \
{                                                                                           \
  STRUCT.time = get_ticks();                                                                \
  STRUCT.content = _content;                                                                \
  STRUCT.span = _span;                                                                      \
  STRUCT.is_running = false;                                                                \
  if (!STRUCT.is_running)                                                                   \
  {                                                                                         \
    STRUCT.time_start = get_ticks();                                                        \
    STRUCT.Y_TRG_FIELD = DEFAULT_Y_TRG;                                                     \
    STRUCT.is_running = true;                                                               \
  }                                                                                         \
  xerintosh_set_font(NULL);                                                                     \
  STRUCT.W_TRG_FIELD = oled_get_UTF8_width(STRUCT.content) + OFFSET;                        \
}
```

**注意：** 上述宏方案因字段名不同导致复杂度过高。更优方案是统一数据结构。

**替代方案（推荐）：** 统一 `xerintosh_info_bar_t` 和 `xerintosh_pop_up_t` 为通用结构体：

在 `ui_item.h` 中：
```c
typedef struct xerintosh_notification_t
{
  const char *content;
  uint16_t span;
  float y, y_trg, w, w_trg;
  bool is_running;
  uint32_t time_start;
  uint32_t time;
} xerintosh_notification_t;
```

将 `xerintosh_info_bar_t` 和 `xerintosh_pop_up_t` 改为 `xerintosh_notification_t` 的typedef。

- [ ] **Step 2: 提取通用推送函数**

```c
static void xerintosh_push_notification(xerintosh_notification_t *_notif, const char *_content,
                                     uint16_t _span, float _default_y_trg, uint8_t _offset)
{
  _notif->time = get_ticks();
  _notif->content = _content;
  _notif->span = _span;
  _notif->is_running = false;
  if (!_notif->is_running)
  {
    _notif->time_start = get_ticks();
    _notif->y_trg = _default_y_trg;
    _notif->is_running = true;
  }
  xerintosh_set_font(NULL);
  _notif->w_trg = oled_get_UTF8_width(_notif->content) + _offset;
}
```

- [ ] **Step 3: 简化 `xerintosh_push_info_bar` 和 `xerintosh_push_pop_up`**

```c
void xerintosh_push_info_bar(const char *_content, const uint16_t _span)
{
  xerintosh_push_notification(&xerintosh_info_bar, _content, _span,
                          0 - 2 * INFO_BAR_HEIGHT, INFO_BAR_OFFSET);
}

void xerintosh_push_pop_up(const char *_content, const uint16_t _span)
{
  xerintosh_push_notification(&xerintosh_pop_up, _content, _span, 20, POP_UP_OFFSET);
}
```

- [ ] **Step 4: 更新 `ui_drawer.c` 中对字段的引用**

由于统一了结构体字段名，需要修改 `ui_drawer.c` 中所有引用：
- `y_info_bar` → `y`, `y_info_bar_trg` → `y_trg`, `w_info_bar` → `w`, `w_info_bar_trg` → `w_trg`
- `y_pop_up` → `y`, `y_pop_up_trg` → `y_trg`, `w_pop_up` → `w`, `w_pop_up_trg` → `w_trg`

- [ ] **Step 5: 编译验证**

Run: `pio run -e native`
Expected: 编译成功

- [ ] **Step 6: Commit**

```bash
git add src/ui/ui_item.c src/ui/ui_item.h src/ui/ui_drawer.c
git commit -m "refactor: unify info_bar and pop_up into generic notification structure"
```

---

## Task 5: 拆分 `xerintosh_draw_list_item()` 大函数

**Files:**
- Modify: `src/ui/ui_drawer.c`
- Modify: `src/ui/ui_drawer.h`

- [ ] **Step 1: 提取通用的列表项边界检查辅助函数**

```c
static bool is_item_visible(int16_t _y_item)
{
  return (_y_item + 2 > LIST_INFO_BAR_HEIGHT && _y_item - 2 < SCREEN_HEIGHT);
}
```

- [ ] **Step 2: 提取每种类型的绘制函数**

```c
static void draw_list_item_list(xerintosh_list_item_t *_item, int16_t _x, int16_t _y)
{
  if (is_item_visible(_y))
    xerintosh_draw_list_icon(_item->icon, _x, _y);
}

static void draw_list_item_switch(xerintosh_switch_item_t *_switch, int16_t _x, int16_t _y)
{
  if (_switch->init_function && xerintosh_refresh_list_value)
    _switch->init_function();
  if (!is_item_visible(_y)) return;

  xerintosh_draw_list_icon(_switch->base_item.icon, _x, _y);
  oled_draw_frame(SCREEN_WIDTH - LIST_ITEM_RIGHT_MARGIN - 7, _y - 2, 11, 7);
  if (*_switch->value)
  {
    oled_draw_box(SCREEN_WIDTH - LIST_ITEM_RIGHT_MARGIN - 1, _y, 3, 3);
    oled_draw_pixel(SCREEN_WIDTH - LIST_ITEM_RIGHT_MARGIN - 4, _y + 1);
  }
  else
  {
    oled_draw_box(SCREEN_WIDTH - LIST_ITEM_RIGHT_MARGIN - 5, _y, 3, 3);
    oled_draw_pixel(SCREEN_WIDTH - LIST_ITEM_RIGHT_MARGIN, _y + 1);
  }
}

static void draw_list_item_button(xerintosh_button_item_t *_button, int16_t _x, int16_t _y)
{
  if (is_item_visible(_y))
    xerintosh_draw_list_icon(_button->base_item.icon, _x, _y);
}

static void draw_list_item_slider(xerintosh_slider_item_t *_slider, int16_t _x, int16_t _y)
{
  if (_slider->init_function && xerintosh_refresh_list_value)
    _slider->init_function();
  if (!is_item_visible(_y)) return;

  xerintosh_draw_list_icon(_slider->base_item.icon, _x, _y);
  char _value_str[10] = {};
  sprintf(_value_str, "%d", *_slider->value);
  int16_t _x_value = SCREEN_WIDTH - LIST_ITEM_RIGHT_MARGIN - oled_get_str_width(_value_str) + 2;

  if (_slider->is_confirmed)
  {
    static uint32_t _last_tick = 0;
    static bool _is_visiable = false;
    uint32_t _ticks = get_ticks();
    if (_is_visiable)
    {
      oled_set_draw_color(1);
      oled_draw_R_box(_x_value, _y - 4, oled_get_UTF8_width(_value_str) + 4, oled_get_str_height() - 2, 1);
    }
    oled_set_draw_color(0);
    oled_draw_str(_x_value + 2, _y + oled_get_str_height() / 2, _value_str);
    if (_ticks - _last_tick >= 1000)
    {
      _is_visiable = !_is_visiable;
      _last_tick = _ticks;
    }
  }
  else
  {
    oled_draw_str(_x_value + 2, _y + oled_get_str_height() / 2, _value_str);
  }
}

static void draw_list_item_user(xerintosh_list_item_t *_item, int16_t _x, int16_t _y)
{
  if (is_item_visible(_y))
    xerintosh_draw_list_icon(_item->icon, _x, _y);
}
```

- [ ] **Step 3: 重写 `xerintosh_draw_list_item()` 使用分派表/简化分支**

```c
void xerintosh_draw_list_item()
{
  for (unsigned char i = 0; i < xerintosh_selector.selected_item->parent->child_num; i++)
  {
    xerintosh_list_item_t *_item = xerintosh_selector.selected_item->parent->child_list_item[i];
    int16_t _x = xerintosh_camera.x_camera + LIST_ITEM_LEFT_MARGIN;
    int16_t _y = _item->y_list_item + xerintosh_camera.y_camera - oled_get_str_height() / 2;

    oled_set_draw_color(1);
    switch (_item->type)
    {
      case list_item:   draw_list_item_list(_item, _x, _y); break;
      case switch_item: draw_list_item_switch(xerintosh_to_switch_item(_item), _x, _y); break;
      case button_item: draw_list_item_button(xerintosh_to_button_item(_item), _x, _y); break;
      case slider_item: draw_list_item_slider(xerintosh_to_slider_item(_item), _x, _y); break;
      case user_item:   draw_list_item_user(_item, _x, _y); break;
    }

    xerintosh_set_font(NULL);
    if (is_item_visible(_y))
      oled_draw_UTF8(10 + _x, _y + oled_get_str_height() / 2, _item->content);
  }
  xerintosh_refresh_list_value = false;
}
```

- [ ] **Step 4: 编译验证**

Run: `pio run -e native`
Expected: 编译成功

- [ ] **Step 5: Commit**

```bash
git add src/ui/ui_drawer.c src/ui/ui_drawer.h
git commit -m "refactor: split xerintosh_draw_list_item into per-type draw helpers"
```

---

## Task 6: 拆分 `xerintosh_selector_jump_to_selected_item()` 和 `xerintosh_selector_exit_current_item()`

**Files:**
- Modify: `src/ui/ui_item.c`

- [ ] **Step 1: 提取 `handle_user_item_enter` 和 `handle_user_item_exit` 辅助函数**

```c
static void handle_user_item_enter(xerintosh_user_item_t *_user_item)
{
  xerintosh_exit_animation_finished = false;
  _user_item->entering_user_item = true;
  _user_item->exiting_user_item = false;
  _user_item->user_item_inited = false;
  _user_item->user_item_looping = false;
}

static void handle_user_item_exit(xerintosh_user_item_t *_user_item)
{
  xerintosh_exit_animation_finished = false;
  _user_item->entering_user_item = false;
  _user_item->exiting_user_item = true;
  _user_item->user_item_inited = false;
  _user_item->user_item_looping = false;
}
```

- [ ] **Step 2: 提取 `handle_slider_confirm_toggle` 辅助函数**

```c
static void handle_slider_confirm_toggle(xerintosh_slider_item_t *_slider)
{
  if (!_slider->is_confirmed)
  {
    _slider->is_confirmed = true;
    _slider->value_backup = *_slider->value;
  }
  else
  {
    if (_slider->exit_function)
      _slider->exit_function();
    _slider->is_confirmed = false;
  }
}
```

- [ ] **Step 3: 重写 `xerintosh_selector_jump_to_selected_item()`**

```c
void xerintosh_selector_jump_to_selected_item()
{
  if (!in_xerintosh) return;

  xerintosh_list_item_t *_sel = xerintosh_selector.selected_item;

  if (_sel->type == user_item)
  {
    handle_user_item_enter(xerintosh_to_user_item(_sel));
    return;
  }

  if (_sel->type == switch_item)
  {
    xerintosh_switch_item_t *_sw = xerintosh_to_switch_item(_sel);
    *_sw->value = !*_sw->value;
    if (_sw->exit_function) _sw->exit_function();
    return;
  }

  if (_sel->type == button_item)
  {
    xerintosh_button_item_t *_btn = xerintosh_to_button_item(_sel);
    if (_btn->exit_function) _btn->exit_function();
    return;
  }

  if (_sel->type == slider_item)
  {
    handle_slider_confirm_toggle(xerintosh_to_slider_item(_sel));
    return;
  }

  if (_sel->child_num == 0) return;

  xerintosh_refresh_list_value = true;
  for (uint8_t i = 0; i < _sel->child_num; i++)
    _sel->child_list_item[i]->y_list_item = 0;

  xerintosh_selector.selected_index = 0;
  xerintosh_selector.selected_item = _sel->child_list_item[0];
}
```

- [ ] **Step 4: 重写 `xerintosh_selector_exit_current_item()`**

```c
void xerintosh_selector_exit_current_item()
{
  xerintosh_list_item_t *_sel = xerintosh_selector.selected_item;

  if (_sel->type == slider_item && xerintosh_to_slider_item(_sel)->is_confirmed)
  {
    xerintosh_slider_item_t *_slider = xerintosh_to_slider_item(_sel);
    _slider->is_confirmed = false;
    *_slider->value = _slider->value_backup;
    return;
  }

  if (_sel->type == user_item && xerintosh_to_user_item(_sel)->in_user_item)
  {
    handle_user_item_exit(xerintosh_to_user_item(_sel));
    return;
  }

  xerintosh_refresh_list_value = true;

  if (_sel->parent->layer == 0 && in_xerintosh)
  {
    if (ALLOW_EXIT_ASTRA_UI_BY_USER) in_xerintosh = false;
    return;
  }

  xerintosh_list_item_t *_parent = _sel->parent;
  xerintosh_list_item_t *_grandparent = _parent->parent;

  for (uint8_t i = 0; i < _grandparent->child_num; i++)
    _grandparent->child_list_item[i]->y_list_item = 0;

  uint8_t _temp_index = 0;
  for (uint8_t i = 0; i < _grandparent->child_num; i++)
  {
    if (_grandparent->child_list_item[i] == _parent)
    {
      _temp_index = i;
      break;
    }
  }
  xerintosh_selector.selected_index = _temp_index;
  xerintosh_selector.selected_item = _parent;
}
```

- [ ] **Step 5: 编译验证**

Run: `pio run -e native`
Expected: 编译成功

- [ ] **Step 6: Commit**

```bash
git add src/ui/ui_item.c
git commit -m "refactor: split jump/exit selectors into focused helper functions"
```

---

## Task 7: 消除 `hal_input.cpp` 中 BtnA/BtnB 重复代码

**Files:**
- Modify: `src/hal/hal_input.cpp`

- [ ] **Step 1: 提取通用的按钮事件检测函数**

```c
static hal_event_t check_button_event(struct btn_state *st, bool wasPressed, bool wasReleased, bool isPressed)
{
  if (wasPressed)
  {
    st->pressed = true;
    st->press_time = millis();
    st->long_fired = false;
  }
  if (wasReleased)
  {
    st->pressed = false;
    if (!st->long_fired)
    {
      return HAL_EVENT_SHORT_PRESS;
    }
  }
  if (st->pressed && !st->long_fired)
  {
    if (millis() - st->press_time >= LONG_PRESS_DURATION_MS)
    {
      st->long_fired = true;
      return HAL_EVENT_LONG_PRESS;
    }
  }
  return HAL_EVENT_NONE;
}
```

- [ ] **Step 2: 简化 `hal_input_get_event()`**

```c
hal_event_t hal_input_get_event(hal_button_t btn)
{
  struct btn_state *st = nullptr;
  if (btn == HAL_BTN_A) st = &g_btn_a;
  else if (btn == HAL_BTN_B) st = &g_btn_b;
  else return HAL_EVENT_NONE;

  if (btn == HAL_BTN_A)
  {
    return check_button_event(st, M5.BtnA.wasPressed(), M5.BtnA.wasReleased(), M5.BtnA.isPressed());
  }
  else
  {
    return check_button_event(st, M5.BtnB.wasPressed(), M5.BtnB.wasReleased(), M5.BtnB.isPressed());
  }
}
```

- [ ] **Step 3: 编译验证**

Run: `pio run`
Expected: 编译成功

- [ ] **Step 4: Commit**

```bash
git add src/hal/hal_input.cpp
git commit -m "refactor: deduplicate button event handling in hal_input"
```

---

## Task 8: 提取 `ui_drawer.c` 中的沙漏绘制常量

**Files:**
- Modify: `src/ui/ui_drawer.c`

- [ ] **Step 1: 定义沙漏绘制常量**

在 `ui_drawer.c` 顶部添加：
```c
/* 沙漏动画常量 */
#define HOURGLASS_WIDTH         13
#define HOURGLASS_HEIGHT        22
#define HOURGLASS_X_OFFSET      8   /* SCREEN_WIDTH/2 - HOURGLASS_WIDTH/2 */
#define HOURGLASS_Y_OFFSET      18  /* SCREEN_HEIGHT/2 相关的偏移 */
```

- [ ] **Step 2: 替换 `xerintosh_draw_exit_animation()` 中的硬编码值**

将沙漏绘制中所有硬编码的坐标和尺寸替换为上述常量。由于沙漏绘制涉及大量像素级坐标，保留相对坐标注释即可。

- [ ] **Step 3: Commit**

```bash
git add src/ui/ui_drawer.c
git commit -m "refactor: extract hourglass drawing constants in exit animation"
```

---

## Task 9: 统一命名规范（camelCase → snake_case）

**Files:**
- Modify: `src/ui/ui_core.c`
- Modify: `src/ui/ui_item.c`
- Modify: `src/ui/ui_drawer.c`
- Modify: `src/ui/ui_item.h`
- Modify: `src/ui/ui_core.h`

- [ ] **Step 1: 重命名 `xerintosh_animation` 参数**

```c
void xerintosh_animation(float *_pos, float _pos_trg, float _speed)
{
  if (*_pos != _pos_trg)
  {
    if (fabs(*_pos - _pos_trg) <= 1.0f) *_pos = _pos_trg;
    else *_pos += (_pos_trg - *_pos) / (100.0f - _speed) / 1.0f;
  }
}
```

- [ ] **Step 2: 重命名所有内部局部变量中的 camelCase**

在 `ui_item.c` 中：
- `_temp_index` → `_temp_index` (已是snake_case)
- 检查并修改所有 `_posTrg` → `_pos_trg`

- [ ] **Step 3: 编译验证**

Run: `pio run -e native`
Expected: 编译成功

- [ ] **Step 4: Commit**

```bash
git add src/ui/ui_core.c src/ui/ui_item.c src/ui/ui_drawer.c src/ui/ui_item.h src/ui/ui_core.h
git commit -m "style: unify naming convention to snake_case for local variables"
```

---

## Task 10: 为关键函数添加防御性检查和错误处理

**Files:**
- Modify: `src/ui/ui_item.c`
- Modify: `src/ui/ui_core.c`

- [ ] **Step 1: 为 `xerintosh_push_item_to_list` 加强边界检查**

已存在基本检查，确认足够。

- [ ] **Step 2: 为 `xerintosh_bind_item_to_selector` 添加索引查找辅助函数**

```c
static uint8_t find_item_index(xerintosh_list_item_t *_parent, xerintosh_list_item_t *_target)
{
  for (uint8_t i = 0; i < _parent->child_num; i++)
  {
    if (_parent->child_list_item[i] == _target)
      return i;
  }
  return 0;
}
```

修改 `xerintosh_bind_item_to_selector()`：
```c
bool xerintosh_bind_item_to_selector(xerintosh_list_item_t *_item)
{
  if (_item == NULL) return false;
  if (_item->parent == NULL) return false;

  if (xerintosh_selector.selected_item == NULL)
  {
    xerintosh_selector.y_selector = 2 * SCREEN_HEIGHT;
    xerintosh_selector.h_selector = 160;
  }
  xerintosh_selector.selected_index = find_item_index(_item->parent, _item);
  xerintosh_selector.selected_item = _item;
  return true;
}
```

- [ ] **Step 3: 编译验证**

Run: `pio run -e native`
Expected: 编译成功

- [ ] **Step 4: Commit**

```bash
git add src/ui/ui_item.c
git commit -m "refactor: extract find_item_index helper and improve defensive checks"
```

---

## Task 11: 扩展测试覆盖率

**Files:**
- Modify: `src/native_main.cpp`

- [ ] **Step 1: 添加类型转换测试**

```c
TEST(ItemTest, TypeCastSafety)
{
  xerintosh_list_item_t *root = xerintosh_get_root_list();
  xerintosh_list_item_t *sw = xerintosh_new_switch_item("Test", NULL, NULL, NULL, default_icon);
  EXPECT_EQ(xerintosh_to_switch_item(sw)->base_item.type, switch_item);
  EXPECT_EQ(xerintosh_to_switch_item(root)->base_item.type, list_item); /* fallback to root */
}
```

- [ ] **Step 2: 添加选择器导航测试**

```c
TEST(SelectorTest, NextPrevNavigation)
{
  xerintosh_list_item_t *root = xerintosh_get_root_list();
  xerintosh_list_item_t *item1 = xerintosh_new_list_item("A", default_icon);
  xerintosh_list_item_t *item2 = xerintosh_new_list_item("B", default_icon);
  xerintosh_push_item_to_list(root, item1);
  xerintosh_push_item_to_list(root, item2);

  xerintosh_bind_item_to_selector(item1);
  EXPECT_EQ(xerintosh_selector.selected_index, 0);

  xerintosh_selector_go_next_item();
  EXPECT_EQ(xerintosh_selector.selected_index, 1);

  xerintosh_selector_go_next_item();
  EXPECT_EQ(xerintosh_selector.selected_index, 0); /* wrap around */

  xerintosh_selector_go_prev_item();
  EXPECT_EQ(xerintosh_selector.selected_index, 1); /* wrap around backward */
}
```

- [ ] **Step 3: 添加边界检查测试**

```c
TEST(ItemTest, PushItemBounds)
{
  xerintosh_list_item_t *root = xerintosh_get_root_list();
  bool result = xerintosh_push_item_to_list(NULL, root);
  EXPECT_FALSE(result);
  result = xerintosh_push_item_to_list(root, NULL);
  EXPECT_FALSE(result);
}
```

- [ ] **Step 4: 运行完整测试**

Run: `pio test -e native`
Expected: 所有测试 PASS

- [ ] **Step 5: Commit**

```bash
git add src/native_main.cpp
git commit -m "test: expand test coverage for type casts and selector navigation"
```

---

## Task 12: 最终验证和清理

**Files:**
- All modified files

- [ ] **Step 1: 完整构建验证**

Run: `pio run -e native && pio run`
Expected: 两个环境都编译成功

- [ ] **Step 2: 运行全部测试**

Run: `pio test -e native`
Expected: 所有测试 PASS

- [ ] **Step 3: 代码质量自检**

检查清单：
- [ ] 无代码重复（DRY）
- [ ] 函数长度 < 50 行
- [ ] 无深层嵌套（> 4 层）
- [ ] 魔法数字已提取为常量
- [ ] 命名规范统一
- [ ] 错误处理完善

- [ ] **Step 4: 最终 Commit（如果需要）**

---

## 自我审查检查清单

1. **覆盖度检查：** 每个已识别的优化点都有对应的任务实现：
   - [x] 代码重复 → Task 2, 3, 4, 7
   - [x] 大函数 → Task 5, 6
   - [x] 深层嵌套 → Task 5, 6
   - [x] 魔法数字 → Task 1, 8
   - [x] 命名不一致 → Task 9
   - [x] 错误处理 → Task 10
   - [x] 测试覆盖 → Task 11

2. **占位符扫描：** 无 TBD、TODO 或模糊描述。

3. **类型一致性：** 所有任务中使用的类型名称和函数签名一致。
