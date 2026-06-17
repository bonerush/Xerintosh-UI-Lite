# 核心引擎（UI Core）

> **Parent:** [UI 核心层索引](index.md) | **Related:** [项目系统](item.md), [绘制管线](drawer.md), [类型派发表](dispatch.md), [全局上下文](context.md)

## 概述

`ui_core` 是 UI 框架的**动画引擎与主循环调度器**。它负责：

1. **动画插值**：将每个元素的当前坐标平滑过渡到目标坐标
2. **生命周期管理**：管理 `user_item` 的进入→运行→退出全流程
3. **帧渲染**：协调相机滚动、列表项位置、选择器移动
4. **主循环调度**：统一编排上述三者的执行顺序
5. **Widget 刷新**：驱动信息栏和弹窗的位置动画

在 UI 重构（Tasks 1-10）后，原先约 60 行的 `xerintosh_ui_main_core()` 被拆分为三个职责清晰的函数，并将确认操作的逻辑委托给类型派发表。

---

## 主循环架构

### 重构后的三层结构

*📄 Source: [ui_core.c](../../src/ui/ui_core.c#L336-L359)*

```c
void xerintosh_ui_main_core()
{
  if (!g_in_xerintosh) return;

  xerintosh_ui_update_lifecycle();   // 第一层：生命周期
  xerintosh_ui_render_frame();       // 第二层：帧渲染
                                      // 第三层：退场动画（遮罩覆盖在所有内容之上）
  if (!g_xerintosh_exit_animation_finished && !power_key_popup_is_dual_active())
    xerintosh_draw_exit_animation();
}
```

```
调用栈（per frame）:
main.cpp loop()
  └─► xerintosh_ui_main_core()
        ├─► xerintosh_ui_update_lifecycle()
        │     └─► [user_item] → init / loop / exit 回调
        ├─► xerintosh_ui_render_frame()
        │     ├─► [user_item 内部] return（App 自行绘制）
        │     └─► [列表模式] camera → list_item → selector → draw_list
        └─► xerintosh_draw_exit_animation()  ← 遮罩层
```

### 与重构前的对比

| 维度 | 重构前 | 重构后 |
|------|--------|--------|
| `xerintosh_ui_main_core()` 行数 | ~63 行 | 11 行 |
| 职责分离 | 全部混在一个函数中 | 3 个独立函数，各司其职 |
| user_item 生命周期 | 散落在 if/else 分支中 | 集中在 `xerintosh_ui_update_lifecycle()` |
| 列表渲染 | 与生命周期交织 | 独立为 `xerintosh_ui_render_frame()` |
| 确认操作 | 内联 switch/if 链 | 委托给 `xerintosh_dispatch_enter()` |

---

## 第一层：user_item 生命周期管理

### 四阶段状态机

*📄 Source: [ui_core.c](../../src/ui/ui_core.c#L272-L317)*

```c
static void xerintosh_ui_update_lifecycle(void)
{
  xerintosh_list_item_t *item = g_xerintosh_selector.selected_item;
  if (item->type != user_item) return;

  xerintosh_user_item_t *user = xerintosh_to_user_item(item);

  /* 阶段 1：进入 — 退场动画到达中间态后触发 init */
  if (!user->in_user_item && user->entering_user_item
      && g_xerintosh_exit_animation_status == 1)
  {
    if (user->init_function != NULL)
      user->init_function(item->user_data);
    user->in_user_item = 1;
    user->entering_user_item = false;
  }

  /* 阶段 2：运行 — 每帧调用 loop */
  if (user->in_user_item && user->loop_function != NULL)
    user->loop_function(item->user_data);

  /* 外部 kill 请求 */
  if (g_xerintosh_exit_requested) {
    g_xerintosh_exit_requested = false;
    user->exiting_user_item = true;
  }

  /* 阶段 3：退出 — 退场动画到达中间态后触发 exit */
  if (user->exiting_user_item && g_xerintosh_exit_animation_status == 1)
  {
    if (user->exit_function != NULL)
      user->exit_function(item->user_data);
    user->in_user_item = 0;
    user->exiting_user_item = false;
  }

  /* 阶段 4：兜底清理 — 防止退出状态残留 */
  if (user->exiting_user_item && g_xerintosh_exit_animation_finished) {
    user->in_user_item = 0;
    user->exiting_user_item = false;
  }
}
```

#### 中文伪代码拆解

```
函数 更新生命周期() {
    当前项 = 选择器.选中项
    if (当前项不是用户App类型) return

    用户项 = 安全转换为用户App(当前项)

    // ═══ 阶段1: 进入 ═══
    if (还没进入 且 正在进入 且 退场动画阶段==1) {
        // 退场动画遮罩已到达底部 → 执行App初始化
        if (存在初始化回调) {
            初始化回调(用户数据)
        }
        标记已进入 = true
        标记正在进入 = false
    }

    // ═══ 阶段2: 运行 ═══
    if (已进入 且 存在循环回调) {
        循环回调(用户数据)     // App的主循环，每帧执行
    }

    // ═══ 外部终止信号 ═══
    if (外部请求退出) {
        外部请求退出 = false   // 消费信号
        标记正在退出 = true     // 触发退出流程
    }

    // ═══ 阶段3: 退出 ═══
    if (正在退出 且 退场动画阶段==1) {
        // 退场动画遮罩已到达底部 → 执行App清理
        if (存在退出回调) {
            退出回调(用户数据)
        }
        标记已进入 = false
        标记正在退出 = false
    }

    // ═══ 阶段4: 兜底 ═══
    if (正在退出 且 动画已完成) {
        // 安全网：即使阶段3遗漏了清理，也强制重置
        标记已进入 = false
        标记正在退出 = false
    }
}
```

### user_item 生命周期时序图

```
用户按下确认键
  │
  ├─► xerintosh_dispatch_enter(user_item)
  │     └─► entering_user_item = true
  │         exit_animation_finished = false
  │         exit_animation_status = 0
  │
  ├─► [退场动画播放中...]
  │     └─► exit_animation_status: 0 → 1（遮罩到达底部）
  │
  ├─► xerintosh_ui_update_lifecycle() 检测到 status==1
  │     └─► init_function(user_data)   ← App 初始化
  │         in_user_item = 1
  │
  ├─► [App 运行中，每帧 loop_function(user_data)]
  │
  ├─► App 内部调用 ui_user_item_try_exit(HAL_EVENT_LONG_PRESS)
  │     或外部设置 g_xerintosh_exit_requested = true
  │     └─► exiting_user_item = true
  │
  ├─► [退场动画播放中...]
  │
  ├─► xerintosh_ui_update_lifecycle() 检测到 status==1
  │     └─► exit_function(user_data)   ← App 清理
  │         in_user_item = 0
  │
  └─► 返回列表模式
```

### 关键设计决策

**为什么进入/退出都在 `exit_animation_status == 1` 时触发？**

退场动画是一个"沙漏遮罩"：阶段 0 向下展开 → 阶段 1 到达底部（屏幕全黑）→ 阶段 2 向上回缩。在阶段 1（遮罩覆盖全屏）时切换内容，用户看不到切换过程，实现无缝衔接。

**为什么需要阶段 4 兜底？**

如果 App 的 `exit_function` 中发生异常导致 `exiting_user_item` 未正确清零，阶段 4 在动画完全结束后强制重置状态，防止 App 卡在"正在退出"的中间态无法再次进入。

---

## 第二层：帧渲染

### 渲染分支

*📄 Source: [ui_core.c](../../src/ui/ui_core.c#L322-L330)*

```c
static void xerintosh_ui_render_frame(void)
{
  if (xerintosh_is_in_user_item()) return; /* user_item 自行绘制 */

  xerintosh_refresh_camera_position();
  xerintosh_refresh_list_item_position();
  xerintosh_refresh_selector_position();
  xerintosh_draw_list();
}
```

#### 中文伪代码拆解

```
函数 渲染帧() {
    if (当前处于用户App内部) return  // App自己负责绘制

    // 列表模式渲染管线:
    刷新相机位置()          // 确保选择器在可视区域
    刷新列表项位置()        // 所有子项的Y坐标动画插值
    刷新选择器位置()        // 选择器框的大小和位置动画
    绘制列表()              // 委托 drawer 绘制背景+项+选择器高亮
}
```

**分支逻辑**：
- **user_item 内部**：return 早退，由 App 自己的 `loop_function` 调用 hal_* 绘制
- **列表模式**：执行完整的列表渲染管线

### user_item 状态查询

*📄 Source: [ui_core.c](../../src/ui/ui_core.c#L24-L31)*

```c
bool xerintosh_is_in_user_item()
{
  xerintosh_list_item_t *sel = g_xerintosh_selector.selected_item;
  if (sel == NULL) return false;

  xerintosh_user_item_t *user = xerintosh_to_user_item(sel);
  return (user != NULL && user->in_user_item);
}
```

两个条件必须同时满足：
1. 当前选中项类型是 `user_item`
2. 已经完成进入初始化（`in_user_item == true`）

在"正在进入但尚未初始化"的过渡期间（entering=true, in_user=false），此函数返回 false，框架继续执行列表渲染。

---

## 动画系统

### 缓动公式

*📄 Source: [ui_core.c](../../src/ui/ui_core.c#L47-L67)*

```c
bool xerintosh_animation(float *_pos, float _pos_trg, float _speed)
{
  if (*_pos != _pos_trg)
  {
    if (!g_anim_enabled) {
      *_pos = _pos_trg;
      return true;
    }
    /* 速度边界裁剪 */
    if (_speed > ANIM_SPEED_MAX) _speed = ANIM_SPEED_MAX;
    if (_speed < ANIM_SPEED_MIN) _speed = ANIM_SPEED_MIN;
    if (fabsf(*_pos - _pos_trg) <= ANIM_SNAP_THRESHOLD) {
      *_pos = _pos_trg;
      return true;
    }
    *_pos += (_pos_trg - *_pos) / (100.0f - _speed);
    xerintosh_invalidate();  /* 动画进行中，标记需要重绘 */
    return false;
  }
  return true;
}
```

#### 中文伪代码拆解

```
函数 动画插值(当前位置指针, 目标位置, 速度0~99) {
    if (当前位置 == 目标位置) return   // 已到位，无需动画

    if (动画被全局禁用) {
        当前位置 = 目标位置            // 直接跳变
        return
    }

    if (abs(当前 - 目标) <= 1.0) {
        当前位置 = 目标位置            // 距离足够近，吸附到位
    } else {
        // 指数衰减缓动（exponential ease-out）
        // 距离越远移动越快，越近移动越慢
        当前位置 += (目标 - 当前) / (100 - 速度)
        xerintosh_invalidate()    // 副作用：标记 UI 需重绘，驱动下一帧刷新
    }
}
```

**核心思想**：这是一个**指数衰减缓动**。每次调用只移动剩余距离的一个比例，比例由 `speed` 控制。`speed` 越接近 ANIM_SPEED_MAX(99.0f)，除数越小，移动越快。速度上限为 ANIM_SPEED_MAX，保证除数至少为 1，避免除零。速度下限为 ANIM_SPEED_MIN(0.0f)，防止异常负值导致反向运动。

**返回值**：`true` 表示位置已稳定在目标（已到位或已吸附），`false` 表示动画仍在进行中。调用方可据此判断是否需要继续重绘。

### 动画速度参考表

| 宏常量 | 计算 | 典型值 (g_anim_speed=92) | 用途 |
|--------|------|--------------------------|------|
| `ANIM_SPEED_LIST_ITEM` | `speed - 2` | 90 | 列表项Y坐标插值 |
| `ANIM_SPEED_SELECTOR` | `speed` | 92 | 选择器Y/W移动 |
| `ANIM_SPEED_SELECTOR_H` | `speed + 1` | 93 | 选择器高度变化 |
| `ANIM_SPEED_CAMERA` | `speed + 4` | 96 | 相机视口滚动 |
| `ANIM_SPEED_INFO_BAR` | `speed + 2` | 94 | 信息栏Y坐标 |
| `ANIM_SPEED_INFO_BAR_W` | `speed + 3` | 95 | 信息栏宽度 |
| `ANIM_SPEED_POP_UP_Y` | `speed + 2` | 94 | 弹窗Y坐标 |
| `ANIM_SPEED_POP_UP_W` | `speed + 4` | 96 | 弹窗宽度 |
| `ANIM_SPEED_EXIT` | `speed + 2` | 94 | 退场遮罩高度 |

### 动画内部常量

*📄 Source: [ui_types.h](../../src/ui/ui_types.h#L34-L36)*

| 常量 | 值 | 用途 |
|------|-----|------|
| `ANIM_SPEED_MAX` | 99.0f | 速度上限（防止分母趋零） |
| `ANIM_SPEED_MIN` | 0.0f | 速度下限（防负值反向） |
| `ANIM_SNAP_THRESHOLD` | 1.0f | 吸附阈值（diff < 此值时直接跳转） |

### 弹簧动画（Spring Animation）

*📄 Source: [ui_core.c](../../src/ui/ui_core.c#L93-L115)*

自 Round 10 起，选择器（selector）的 Y/W/H 动画从一阶指数衰减升级为**二阶弹簧-阻尼器模型**，产生"QQ弹弹"的弹性过渡效果。

```c
void xerintosh_spring_animation(float *_pos, float *_vel, float _pos_trg,
                                 float _stiffness, float _damping)
{
  if (!g_anim_enabled) {
    *_pos = _pos_trg;
    *_vel = 0.0f;
    return;
  }

  /* F = k*(target - x) - c*v   （二阶弹簧-阻尼器离散模型） */
  float force = _stiffness * (_pos_trg - *_pos) - _damping * (*_vel);
  *_vel += force;
  *_pos += *_vel;

  /* 靠近目标(<0.5px)且速度足够小(<0.5px/frame)时吸附到位 */
  if (fabsf(*_pos - _pos_trg) < 0.5f && fabsf(*_vel) < 0.5f) {
    *_pos = _pos_trg;
    *_vel = 0.0f;
  }
}
```

#### 中文伪代码拆解

```
函数 弹簧动画(当前位置, 当前速度, 目标位置, 刚度k, 阻尼c) {
    if (动画全局禁用) {
        当前位置 = 目标位置
        当前速度 = 0
        return
    }

    // 弹簧力 + 阻尼力 = 合外力
    合力 = k * (目标 - 当前位置) - c * 当前速度
    当前速度 += 合力     // 加速度积分
    当前位置 += 当前速度  // 速度积分

    // 吸附：当接近目标且速度很小时，直接锁定
    if (|当前位置 - 目标| < 0.5 且 |当前速度| < 0.5) {
        当前位置 = 目标位置
        当前速度 = 0
    }
}
```

#### 参数选择与效果

| 参数 | 值 | ζ（阻尼比） | 超调量 | 稳定帧数 | 视觉效果 |
|------|-----|-----------|--------|---------|---------|
| `g_spring_stiffness_selector` | 0.20f | — | — | — | 控制回弹"硬度" |
| `g_spring_damping_selector` | 0.35f | ζ ≈ 0.39 | ~20% | ~17帧 | 1-2次可见弹跳 |

**注意**：这两个参数是**运行时可调全局变量**（`extern float`），而非 `#define` 编译期常量。默认值由 `SPRING_STIFFNESS_SELECTOR_DEFAULT`（0.20f）和 `SPRING_DAMPING_SELECTOR_DEFAULT`（0.35f）定义，用户可在 设置 > 弹簧硬度 / 反弹力度 中实时调整。

**核心思想**：基于经典控制理论的二阶弹簧-阻尼器模型：
- `stiffness` (k)：刚度系数，越大系统响应越快、振荡频率越高
- `damping` (c)：粘性阻尼系数，越大能量衰减越快、弹跳越少
- 阻尼比 ζ = c / (2√k) = 0.35 / (2√0.20) ≈ 0.39 < 1，产生欠阻尼响应（有弹性）
- 当前参数组合经过手动逐帧仿真验证，在 80x160 屏幕上产生舒适的弹性过渡

**与一阶动画的关系**：
- **弹簧动画**：仅用于选择器（高亮框 Y/W/H），提供弹性过渡
- **一阶动画 `xerintosh_animation()`**：用于列表项、相机、信息栏、弹窗、退场动画，保持平滑无弹跳

---

## 相机系统

### 视口滚动算法

*📄 Source: [ui_core.c](../../src/ui/ui_core.c#L170-L184)*

```c
void xerintosh_refresh_camera_position()
{
  /* 向下越界检测：选择器底部超出屏幕 */
  if (g_xerintosh_camera.selector->y_selector_trg + 15 + g_xerintosh_camera.y_camera_trg > SCREEN_HEIGHT)
    g_xerintosh_camera.y_camera_trg = SCREEN_HEIGHT - g_xerintosh_camera.selector->y_selector_trg - 15;

  /* 向上越界检测：选择器顶部超出屏幕 */
  if (g_xerintosh_camera.selector->y_selector_trg + g_xerintosh_camera.y_camera_trg < 0)
    g_xerintosh_camera.y_camera_trg = 0 - g_xerintosh_camera.selector->y_selector_trg + LIST_FONT_TOP_MARGIN;

  xerintosh_animation(&g_xerintosh_camera.x_camera, g_xerintosh_camera.x_camera_trg, ANIM_SPEED_CAMERA);
  xerintosh_animation(&g_xerintosh_camera.y_camera, g_xerintosh_camera.y_camera_trg, ANIM_SPEED_CAMERA);
}
```

#### 中文伪代码拆解

```
函数 刷新相机位置() {
    // 15 为选择器高度（含上下边框各2px）

    选择器底部 = 选择器目标Y + 15 + 相机当前目标Y
    if (选择器底部 > 屏幕高度) {
        相机目标Y = 屏幕高度 - 选择器目标Y - 15
        // 相机向上滚动，使选择器底部对齐到屏幕底部
    }

    选择器顶部 = 选择器目标Y + 相机当前目标Y
    if (选择器顶部 < 0) {
        相机目标Y = -选择器目标Y + 字体顶部边距
        // 相机向下滚动，使选择器顶部回到可视区
    }

    动画插值(相机当前X, 相机目标X, 速度96)
    动画插值(相机当前Y, 相机目标Y, 速度96)
}
```

**关键理解**：相机偏移量通常为负数。当列表向下滚动时，`y_camera` 变为负值，绘制时每个列表项的 `y + y_camera` 变小，从而在屏幕上向上移动。

---

## 选择器位置刷新（含宽度缓存）

*📄 Source: [ui_core.c](../../src/ui/ui_core.c#L230-L253)*

```c
void xerintosh_refresh_selector_position()
{
  if (g_xerintosh_selector.selected_item == NULL) return;

  xerintosh_set_font(hal_get_cn_font());
  g_xerintosh_selector.y_selector_trg = g_xerintosh_selector.selected_item->y_list_item_trg - hal_get_font_height() + 1;
  g_xerintosh_selector.w_selector_trg = xerintosh_dispatch_measure(g_xerintosh_selector.selected_item);
  g_xerintosh_selector.h_selector_trg = hal_get_font_height() + 4;
  if (g_spring_anim_mode) {
    xerintosh_spring_animation(&g_xerintosh_selector.y_selector, &g_xerintosh_selector.v_y_selector,
                                g_xerintosh_selector.y_selector_trg,
                                g_spring_stiffness_selector, g_spring_damping_selector);
    xerintosh_spring_animation(&g_xerintosh_selector.w_selector, &g_xerintosh_selector.v_w_selector,
                                g_xerintosh_selector.w_selector_trg,
                                g_spring_stiffness_selector, g_spring_damping_selector);
    xerintosh_spring_animation(&g_xerintosh_selector.h_selector, &g_xerintosh_selector.v_h_selector,
                                g_xerintosh_selector.h_selector_trg,
                                g_spring_stiffness_selector, g_spring_damping_selector);
  } else {
    xerintosh_animation(&g_xerintosh_selector.y_selector, g_xerintosh_selector.y_selector_trg, ANIM_SPEED_SELECTOR);
    xerintosh_animation(&g_xerintosh_selector.w_selector, g_xerintosh_selector.w_selector_trg, ANIM_SPEED_SELECTOR);
    xerintosh_animation(&g_xerintosh_selector.h_selector, g_xerintosh_selector.h_selector_trg, ANIM_SPEED_SELECTOR_H);
  }
}
```

> **Round 10 变更**：选择器动画根据 `g_spring_anim_mode` 全局标志动态选择——`true` 时使用 `xerintosh_spring_animation()`（二阶弹簧-阻尼器），`false` 时回退到 `xerintosh_animation()`（一阶指数衰减）。速度状态 `v_y/w/h_selector` 存储在 `xerintosh_selector_t` 结构体中。宽度计算由 `xerintosh_dispatch_measure()` 派发，不再使用旧版缓存。

### 选择器宽度缓存机制

**重构前**：每帧无条件调用 `hal_get_string_width()`，即使选中项没有变化。

**重构后**：引入"内容指针比较"缓存。`content` 是 `const char *`，直接指向字符串字面量。当选中项切换时，`content` 指针会变化。指针比较（1 次 CPU 指令）替代字符串宽度测量（遍历 UTF-8 字符表），在 99% 的帧中跳过测量。

---

## Widget 刷新

*📄 Source: [ui_core.c](../../src/ui/ui_core.c#L260-L266)*

```c
void xerintosh_ui_widget_core()
{
  xerintosh_refresh_info_bar();
  xerintosh_refresh_pop_up();
  xerintosh_draw_info_bar();
  xerintosh_draw_pop_up();
}
```

Widget（信息栏 + 弹窗）的刷新独立于主渲染。它在主循环之后调用，确保弹窗绘制在所有列表内容之上。

---

## 公共 API 总览

| 函数 | 签名 | 说明 |
|------|------|------|
| `xerintosh_animation` | `(float*, float, float) → bool` | 通用缓动动画插值（true=已到位，false=进行中） |
| `xerintosh_spring_animation` | `(float*, float*, float, float, float) → void` | 二阶弹簧-阻尼器动画（位置+速度状态） |
| `xerintosh_animate_unified` | `(float*, float*, float, float) → bool` | 统一动画调度，根据 `g_spring_anim_mode` 自动选择弹簧或缓动 |
| `xerintosh_is_in_user_item` | `(void) → bool` | 查询是否处于 user_item 运行态 |
| `xerintosh_init_core` | `(void) → void` | 初始化列表、选择器、相机绑定 |
| `xerintosh_refresh_camera_position` | `(void) → void` | 自动调整视口保证选择器可见 |
| `xerintosh_refresh_list_item_position` | `(void) → void` | 刷新当前菜单所有子项的Y坐标插值 |
| `xerintosh_refresh_selector_position` | `(void) → void` | 刷新选择器Y/W/H（弹簧或缓动，按模式选择） |
| `xerintosh_ui_main_core` | `(void) → void` | **主循环入口**（每帧由 main.cpp 调用） |
| `xerintosh_ui_widget_core` | `(void) → void` | Widget 刷新调度（信息栏 + 弹窗） |

---

## 与其他组件的关系

- **ui_item**：读取 `g_xerintosh_selector`、`g_xerintosh_camera` 状态，修改目标坐标
- **ui_dispatch.c**：`xerintosh_selector_jump_to_selected_item()` 委托给 `xerintosh_dispatch_enter()`
- **ui_drawer**：`xerintosh_ui_render_frame()` 调用 `xerintosh_draw_list()`
- **ui_context**：所有 `g_*` 宏通过 `xerintosh_get_context()` 访问全局状态
- **main.cpp**：每帧调用顺序：`input_process()` → `xerintosh_ui_main_core()` → `xerintosh_ui_widget_core()` → `hal_display_flush()`

---

> **See Also:** [项目系统](item.md) | [绘制管线](drawer.md) | [类型派发表](dispatch.md) | [全局上下文](context.md) | [输入系统](../hal/input.md)
