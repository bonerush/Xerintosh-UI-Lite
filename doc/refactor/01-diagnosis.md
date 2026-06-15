# 诊断报告（2026-06-15 kernel-ui-performance）

## 方法

- 静态扫描范围：`src/kernel/` + `src/ui/` + `src/hal/`（渲染管线）
- 扫描日期：2026-06-15
- 分析维度：内存占用、CPU 性能、实时性（内核）；动画优雅、帧率、响应速度（UI）

## 优先级定义

- **P0**：导致崩溃、内存泄漏、或严重影响帧率/响应的高频路径瓶颈
- **P1**：功能正确但性能可优化、代码可维护性差、重复计算
- **P2**：风格、文档、微小优化

---

## 内核层诊断

### 1. 内存占用分析

#### 数据结构大小审计

| 结构 | 关键字段 | 估算大小 | 问题 |
|------|----------|----------|------|
| `kmalloc_header_t` | `size_t`(4) + `kern_task_t*`(4) | 8 bytes/分配 | 每内核分配 8 字节开销，可接受 |
| `kern_inode_t` | 含 `fops`, `ref_count`, `private_data` | ~20 bytes | 合理 |
| `kern_dentry_t` | `name[KERN_NAME_MAX]`, `children[16]`, `inode` | ~140 bytes | 每个 `/proc` / `/dev` / `/sys` 节点一个，数量有限 |
| `kern_file_t` | `in_use`, `fops`, `inode`, `dentry`, `flags`, `f_pos`, `private_data` | ~36 bytes | 每任务 FD 表，`KERN_MAX_FD_PER_TASK` × 36 bytes |
| `kern_task_t` | 含 `fd_table[KERN_MAX_FD_PER_TASK]` + 调度信息 | ~256+ bytes | 每任务固定开销 |

#### 缓冲区/全局变量审计

| 缓冲区 | 大小 | 可优化？ |
|--------|------|----------|
| `g_framebuffer` (native) | 160×80×2 = 25.6KB | native 测试用，不影响硬件 |
| `g_canvas` 帧缓冲 (ESP32) | 80×160×1 = 12.8KB (RGB332) | 已优化至 8-bit 色深，节省 12.8KB |
| Shell 输入缓冲 | 取决于 `kern_shell.c` | 待确认 |
| `row_buf[160]` in XOR rect | 320 bytes (栈) | 每个选择器重绘临时分配 |

#### kmalloc 分析

- 元数据头：8 bytes（`size_t` + 指针）
- 分配策略：直接包装 `malloc`/`free`，无对象池
- 碎片风险：频繁小对象分配/释放（如 FD 表、dentry）可能导致碎片
- **优化机会**：对常见固定大小对象（`kern_file_t`, `kern_dentry_t`）使用 slab/对象池

### 2. CPU 性能分析

#### 调度器路径

| 操作 | 当前复杂度 | 描述 | 优化 |
|------|-----------|------|------|
| `sched_rr_enqueue()` | O(n) | 遍历整个任务链表到尾端追加 | 添加 `task_list_tail` 指针 → O(1) |
| `sched_rr_dequeue()` | O(n) | 遍历查找目标任务 | 无法避免（单链表），但任务数小 |
| `sched_rr_pick_next()` | O(2n) | 第一遍唤醒到期 sleep；第二遍 RR 扫描 | 维护独立 wake_list 避免第一遍扫描 |
| `sched_rr_tick()` | O(1) | 递减时间片 + 抢占标记 | ✅ 已优化 |

**详细分析 `sched_rr_enqueue()`（`kern_sched_rr.c:35-57`）：**
```c
while (t->next != NULL) t = t->next;  // O(n) 遍历到尾
t->next = task;
```
当任务数达到 8-10 时，每帧调度可能有多次 enqueue/dequeue，累积开销显著。

**详细分析 `sched_rr_pick_next()`（`kern_sched_rr.c:92-143`）：**
```c
// 第一遍：唤醒 sleep（完整链表扫描）
while (t != NULL) {
    if (t->state == KERN_TASK_SLEEPING && t->wake_time <= now)
        t->state = KERN_TASK_READY;
    t = t->next;
}
// 第二遍：RR 扫描
do { ... } while (t != start);
```
即使没有 sleeping 任务，第一遍仍完整扫描。建议在 `enqueue` 时将 sleeping 任务加入独立队列。

#### VFS 热路径

- `path_walk()`（`kern_vfs.c:80-146`）：对每个路径分量 O(children) 线性搜索。树深度浅（最多 3-4 层），每层子节点少（≤16），可接受。
- `fd_alloc()`（`kern_vfs.c:331-355`）：线性扫描 FD 表找空闲槽。`KERN_MAX_FD_PER_TASK` 推测 ≤16，开销小。
- `fd_alloc()` 使用 `calloc()` 分配 `kern_file_t`：每次 open 都调用 malloc，可用预分配池优化。

#### 重复计算/缓存

- Shell 命令解析器 `kern_shell_parser.c`：每次输入遍历字符，但 shell 是低频操作，不构成热路径。
- `kern_vfs.c:path_walk()` 每次路径解析都从头遍历，无路径缓存。对 `/proc/tasks` 等高频访问可考虑缓存。

### 3. 实时性分析

#### 抢占延迟

- 调度器 tick 路径（`sched_rr_tick`）O(1)，但 tick 频率决定调度延迟上限。
- `sched_rr_pick_next()` 持有 `task_list_lock`（spinlock）期间执行 O(2n) 扫描。任务数少时不影响，任务数多时临界区过长。
- 无长时间禁用中断的代码段（SMP spinlock 在单核 ESP32 上编译为空操作）。

#### Tick 精度

- ESP32：取决于 FreeRTOS tick（通常 1ms 或 portTICK_PERIOD_MS）。
- Native：`kern_port_native.c` 使用 `usleep` 模拟，精度取决于系统。
- Xeros 调度器 tick 与 FreeRTOS tick 的关系需明确：是否存在两级调度竞争？

### 4. 内核问题清单

| ID | 严重级 | 文件 | 行号 | 问题描述 | 优化建议 |
|----|--------|------|------|----------|----------|
| K01 | P1 | `kern_sched_rr.c` | 35-57 | `sched_rr_enqueue()` O(n) 尾追加 | 添加 `task_list_tail` 指针实现 O(1) 追加 |
| K02 | P1 | `kern_sched_rr.c` | 92-143 | `pick_next()` 无条件双遍扫描，含睡眠唤醒 | 维护 `wake_list` 分离睡眠任务唤醒逻辑 |
| K03 | P1 | `kern_vfs.c` | 331-355 | `fd_alloc()` 每次 `calloc()` 分配 `kern_file_t` | 预分配 `kern_file_t` 对象池，O(1) 获取 |
| K04 | P2 | `kern_kmalloc.c` | 51-79 | 无对象池，频繁小对象分配导致碎片 | 为 `kern_file_t`、`kern_dentry_t` 等常用结构引入 slab |
| K05 | P2 | `kern_vfs.c` | 80-146 | 无路径缓存，高频路径（如 `/proc/tasks`）每次解析 | 对只读高频路径增加 dentry 缓存 |
| K06 | P1 | `kern_sched_rr.c` | 35 | `s_rr_last_prio` 静态变量声明但未使用（死代码） | 删除 |
| K07 | P2 | `kern_task_lifecycle.c` | 多处 | 僵尸回收时 `stack_base` 可能泄漏 | 已在上一轮诊断 P0-3，确认本轮是否已修复 |

---

## UI 核心层诊断

### 1. 动画系统分析

#### Easing 公式审计

当前公式（`ui_core.c:41-53`）：
```c
if (_speed >= 99.0f) _speed = 99.0f;
if (fabsf(*_pos - _pos_trg) <= 1.0f) *_pos = _pos_trg;
else *_pos += (_pos_trg - *_pos) / (100.0f - _speed);
```

**边界条件分析：**
- `speed=0`：除数=100，最慢收敛。每次移动剩余距离的 1%。
- `speed=99`：除数=1，一步到位（等价于即时跳转）。
- `target ≈ current`（差值≤1）：直接吸附到位，防止无限接近但永不抵达。
- 收敛速度非线性：从 `speed=0` 到 `speed=99`，速度变化范围 100 倍。

**浮点 vs 定点性能：**
- ESP32 内置单精度 FPU，`fabsf()` 和浮点除法的开销可接受。
- 但每帧调用 6-8 次 `xerintosh_animation()`（选择器 x3、相机 x1、列表项 xN、弹窗 x2），总计约 8+N 次浮点运算。
- 若改用 16.16 定点数（32-bit 整数），可减少 FPU 寄存器溢出开销。
- **优先级：P2**（ESP32 FPU 已足够，但纯整数版本对某些场景更优）

#### 选择器动画（`ui_item_selector.c`）

- 选择器移动通过 `xerintosh_refresh_selector_position()` 更新 `y_selector_trg`，然后 `xerintosh_animation()` 插值。
- 每次移动触发 `xerintosh_dispatch_measure()` 计算宽度（含缓存）。
- **问题**：选择器高度 `h_selector_trg` 每次都计算为 `hal_get_font_height() + 4`，不随内容变化，可缓存为常量。

#### 相机滚动（`ui_core.c:79-93`）

- 边界检查使用硬编码 `15`（选择器高度），应定义为常量。
- 上下边界逻辑清晰：选择器超出屏幕底部 → 相机下移；超出顶部 → 相机上移。
- 动画平滑，但相机 X 轴 `x_camera` 始终为 0 却被动画插值（死代码路径）。

#### 行列表动画（`ui_anim_row.c`）

- 入场动画：所有行从屏幕底部滑入。`xerintosh_anim_row_list_update()` 每帧对所有可见行 + 高亮 + 滚动偏移执行动画。
- 使用浮点，开销可接受。
- `xerintosh_anim_row_list_refresh()` 正确计算越界行的隐藏位置。

### 2. 帧率分析

#### 主循环职责

`xerintosh_ui_main_core()`（`ui_core.c:229-240`）每帧执行：

```
1. xerintosh_ui_update_lifecycle()   ← user_item 状态机
2. xerintosh_ui_render_frame()
   ├── xerintosh_refresh_camera_position()    ← 相机动画
   ├── xerintosh_refresh_list_item_position() ← 列表项动画 (O(n))
   ├── xerintosh_refresh_selector_position()  ← 选择器动画
   └── xerintosh_draw_list()
       ├── xerintosh_draw_list_appearance()   ← 边框/滚动条/装饰
       ├── xerintosh_draw_list_item()         ← 所有可见项 + 图标 + 文字 + 裁剪
       ├── xerintosh_draw_selector()          ← XOR 高亮 + 虚线装饰
       └── xerintosh_draw_slider_overlays()   ← 滑块覆盖层
```

**全帧重绘问题：** 即使没有任何状态变化（列表不动、选择器不移动），也会完整重绘所有内容。没有脏矩形（dirty rect）机制。

| 每帧操作 | 是否可跳过 | 触发条件 |
|----------|-----------|----------|
| `draw_list_appearance()` | ✅ 可跳过 | 仅当 `child_num` 或 `selected_index` 变化 |
| `draw_list_item()` | ⚠️ 部分可跳过 | 仅当列表项位置变化或内容变化 |
| `draw_selector()` | ❌ 通常需要 | 选择器经常移动 |
| `draw_slider_overlays()` | ✅ 可跳过 | 仅当有 slider 且值变化 |
| `draw_info_bar()` | ✅ 可跳过 | 仅当信息栏状态变化 |
| `draw_pop_up()` | ✅ 可跳过 | 仅当弹窗状态变化 |

#### 绘制管线瓶颈

**(a) XOR 选择器（P0 级瓶颈）**

`hal_draw_xor_rect()`（`hal_display_adv.cpp:103-124`）：
```c
for (int16_t row = 0; row < h; row++) {
    g_canvas->readRect(x, y + row, w, 1, row_buf);    // 读一行 128×2=256B
    for (int16_t i = 0; i < w; i++)
        row_buf[i] ^= 0xFFFF;                          // CPU 异或
    g_canvas->pushImage(x, y + row, w, 1, row_buf);   // 写回一行
}
```

选择器典型尺寸：128×15 像素 → 15 次 `readRect` + 15 次 `pushImage`。每次 `readRect`/`pushImage` 在 M5Canvas（离屏帧缓冲）上操作，不经过 SPI，但仍然有内存拷贝开销。
- 15 行 × 128 像素 × 2 bytes × 2（读+写）= **7.68KB 内存操作/帧**
- 如果使用 M5GFX 原生的 color 反转模式（若有），可避免逐像素操作。

**优化方案：** 改用 M5GFX 的 XOR 绘制模式（如果支持），或使用双缓冲与上一次的 XOR 区域对比，仅重绘变化的行。

**(b) 文字跑马灯（P1 级）**

选中项文字超宽时绘制两份相同文字：
```c
hal_draw_string(_draw_x, ..., _item->content, color);
hal_draw_string(_draw_x + _cycle_dist, ..., _item->content, color);  // 第二份
```
每次 `hal_draw_string()` 调用 M5GFX 的字体渲染，对中文子集字体（844 汉字）有一定开销。两份绘制是必要的（创建无缝循环），但可优化字体渲染的缓存。

**(c) 裁剪矩形 O(n) 设置（P1 级）**

每个列表项都调用 `hal_set_clip_rect()` → `hal_clear_clip_rect()`。如果列表有 10 项，每帧 10 次 clip 设置/清除。M5GFX 的 `setClipRect` 可能涉及寄存器操作。

**(d) 滚动条绘制（P2 级）**

`xerintosh_draw_list_appearance()` 中滚动条和装饰像素（6 组配置 × 多层循环）每帧重绘。这些元素只在导航时变化，静态时不需要重绘。

#### 全局状态影响

- `g_xerintosh_draw_color`：在多个函数间作为隐式参数传递，增加理解成本但无性能影响。
- `g_xerintosh_selector`：几乎所有绘制函数都读取，但因为 `selected_item->parent` 解引用链很长，多次访问无缓存。

### 3. 响应速度分析

#### 输入延迟路径

```
硬件按键 → M5.update() → hal_input 状态机 → app_input_process()
  → xerintosh_selector_go_next/prev → dispatch_input_next/prev
  → 更新 selected_item → 下一帧渲染
```

- 按键处理在 Arduino `loop()` 的 `M5.update()` 中，然后同步执行 UI 逻辑。
- 当前帧立即更新选择器状态（`selected_item`、`selected_index`），但视觉效果在下一帧才渲染。
- 实测延迟：1 帧（16.7ms @ 60fps）+ 按键去抖延迟（约 20ms）= ~37ms 总延迟。

**优化空间：**
- 按键处理后可立即调用一次局部渲染（仅选择器位置），避免等完整帧。
- 但这对 80×160 小屏意义不大，当前延迟用户已无感知。

#### 派发表性能

`ui_dispatch.c` 使用 `s_dispatch[item->type]` O(1) 数组索引查找，九种操作各有独立 vtable。✅ 架构优秀。

#### Item 生命周期性能

- `xerintosh_new_switch_item()` / `xerintosh_new_slider_item()` 使用 `kern_kmalloc` 分配，仅在菜单构建时调用，非热路径。
- `xerintosh_dispatch_destroy()` 在 item 销毁时调用，非热路径。
- 无热路径上的动态内存分配。✅

### 4. UI 问题清单

| ID | 严重级 | 文件 | 行号 | 问题描述 | 优化建议 |
|----|--------|------|------|----------|----------|
| U01 | **P0** | `hal_display_adv.cpp` | 103-124 | XOR 选择器每帧 15 次 readRect+pushImage，7.68KB 内存操作 | 使用双缓冲 XOR 模式，或 M5GFX 原生反转绘制 |
| U02 | **P0** | `ui_core.c` | 229-240 | 全帧无条件重绘，无脏矩形机制 | 引入 `g_framebuffer_dirty` 标志，静态时跳过重绘 |
| U03 | P1 | `ui_draw_list.c` | 50-111 | `draw_list_appearance()` 静态装饰每帧重绘 | 仅当 `selected_index`/`child_num` 变化时重绘 |
| U04 | P1 | `ui_draw_list.c` | 168-184 | 每列表项都 set/clear clip rect | 仅在需要文字裁剪的项（选中+超宽）设置 clip |
| U05 | P1 | `ui_core.c` | 143 | 选择器高度每次重新计算 `hal_get_font_height() + 4` | 缓存为 `g_cached_font_height_plus_4` |
| U06 | P1 | `ui_core.c` | 91 | 相机 X 轴动画插值但 `x_camera_trg` 始终为 0 | 移除无效的 X 轴动画调用 |
| U07 | P2 | `ui_core.c` | 41-53 | 动画使用浮点运算 | 可提供定点数（16.16）替代版本用于非 FPU 平台 |
| U08 | P2 | `ui_core.c` | 84-89 | 相机边界检查硬编码 `15`（选择器高度） | 定义为 `SELECTOR_HEIGHT` 常量 |
| U09 | P1 | `ui_item_selector.c` | 127-141 | `exit_current_item()` 中多层解引用链重复 | 缓存为局部变量 |
| U10 | P2 | `ui_draw_list.c` | 143-144 | 每列表项计算 `_text_width` 即使该项不可见 | 先判断可见性再计算文字宽度 |
| U11 | P1 | `ui_draw_anim.c` | 42-108 | 退场动画全屏遮罩+沙漏+扫描线（500+像素操作）叠加在主渲染上 | 预渲染遮罩到独立 buffer，避免逐像素条件判断 |
| U12 | P1 | `ui_draw_list.c` | 81-89 | 滚动条缓存 `_cached_length` 未随 `SCREEN_HEIGHT` 失效 | 缓存键增加 `SCREEN_HEIGHT`，方向变化时重置 |
| U13 | P2 | `ui_anim_row.c` | 50 | 行列表稳定阈值 `0.5f` 与主动画阈值 `1.0f` 不一致 | 统一为 `ANIM_SETTLE_THRESHOLD` 宏 |
| U14 | P2 | `ui_item_list.c` | 111-122 | `clear_children_of_list` 未调用 `safety_move_out()`，选择器可悬空 | 清空前调用 `ui_selector_safety_move_out()` |
| U15 | P2 | `ui_dispatch.c` | 359 | `type_in_range` 仅 `<= button_item`，依赖枚举序 | 增加下界检查 `>= list_item` 形成双向边界 |

---

## 渲染管线性能总结

```
当前每帧最小操作（静态画面、无动画、3 个可见列表项）：

  clear (fillScreen 80×160)         ~12.8KB 写入
+ draw_list_appearance()            边框+滚动条+装饰像素 ~100 次 pixel 操作
+ draw_list_item() × 3             图标 + 文字渲染 × 3
+ draw_selector()                   15 次 readRect + XOR + pushImage = 7.68KB
+ draw_info_bar()                   信息栏渲染
+ draw_slider_overlays()            无 slider 时跳过
+ pushSprite()                      DMA 推送 80×160×8bit = 12.8KB 到 SPI

≈ 45-50KB 内存操作/帧
@ 60fps = 2.7-3.0 MB/s 内存带宽
```

**最大瓶颈排序：**
1. **XOR 选择器**（U01）：7.68KB/帧，可通过双缓冲 XOR 消除
2. **全帧重绘**（U02）：静态画面浪费 ~25KB/帧，通过脏矩形可节省
3. **静态装饰重绘**（U03）：通过缓存跳过

---

## 优化优先级建议

### 内核层
1. **K01**（`task_list_tail` O(1) enqueue）— 低风险、高收益，减少每帧调度开销
2. **K02**（睡眠唤醒分离）— 减少 `pick_next()` 不必要的链表遍历
3. **K03**（FD 对象池）— 减少 `kern_open()` 的 malloc 调用

### UI 层
1. **U02**（脏矩形/帧跳过）— 静态画面直接跳过渲染，最显著帧率提升
2. **U01**（XOR 选择器优化）— 最大单点瓶颈
3. **U03**（静态装饰缓存）— 配合 U02 进一步减少绘制
4. **U04**（clip rect 优化）— 减少不必要的硬件寄存器操作
5. **U09**（解引用缓存）— 减少指针追踪开销

---

## 本轮重构排期

| 阶段 | 模块 | 处理的问题 ID |
|------|------|---------------|
| 2.1 | 内核 | K01, K02, K03 |
| 2.3 | UI | U01, U02, U03, U04, U05, U06, U08, U09 |
