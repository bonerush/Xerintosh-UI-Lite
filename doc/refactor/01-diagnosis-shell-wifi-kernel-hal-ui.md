# 诊断报告：HAL / UI 核心层（第十一轮 · 2026-06-19）

## 方法
- 扫描范围：`src/hal/*.cpp/c/h`、`src/ui/*.c/h`、`test/test_native/*`
- 重点关注：初始化顺序、`\n` 处理、事件模型、内联 switch、全局状态耦合、跨任务竞争、可测试性
- 上一轮已修复项：`hal_draw_string()` 的 `\n` 换行在 native 与硬件路径均已实现（`hal_display_font.cpp:201-205`、`257-283`）

---

## 上一轮遗留问题复核

| 原 ID | 状态 | 说明 |
|-------|------|------|
| D5 `\n` 处理 | ✅ 已修复 | 两端均按行分割绘制 |
| D6 `sm_ui.c strchr(\n)` 死代码 | ⚠️ 仍部分成立 | `sm_app.cpp` 的 BT/SER 组装逻辑确实会剥离 `\n`（`sm_app.cpp:67`、`sm_buffer.c:33`），`draw_terminal()` 中的 `\n` 分支对正常数据流不可达；但保留作为防御性代码，不会触发 |
| D7 日志行 64 字符限制 | ⚠️ 未解决 | `sm_buffer.h:21` 仍为 `SM_TERM_LINE_LEN 64` |
| D8 横屏仅 ~7 行可见 | ⚠️ 未解决 | `sm_ui.c:165` 按 `term_h/font_h` 计算，横屏 80×160 旋转后高约 80，去掉 header 后仅剩约 7 行 |

---

## 新问题清单（按 P0/P1/P2/P3 排序）

### P1（功能缺陷 / 竞态 / 层间耦合）

| ID | 模块 | 文件 | 行号 | 问题描述 | 建议动作 | 关联测试 |
|----|------|------|------|----------|----------|----------|
| P1-1 | HAL 输入 | `hal_input_double_click.c` | 90–110 | 双击状态机在窗口超时与新的按下边沿同帧发生时，会先在第 1 步返回 `SHORT_PRESS` 并提前退出，导致本帧的按下边沿被丢弃。 | 将“检查待处理短按超时”调整到“处理按下边沿”之后，或使用事件队列保存边沿。 | `test_double_click.cpp` 新增“窗口超时同时按下”回归用例 |
| P1-2 | HAL 显示 | `hal_display_fb.cpp` | 128–135 | `hal_display_set_rotation()` 仅更新 `M5.Display.setRotation()` 和 `g_screen_width/g_screen_height`，**不重建 `M5Canvas` 精灵**。`main.cpp` 与 `ui_service.c` 目前在调用后手动补了 `hal_display_init()`，但 `/dev/fb0` 等路径没有。 | 让 `set_rotation()` 内部重新创建 sprite（或至少统一要求所有调用方在设置方向后重新 init）。 | `test_kernel_devices.cpp` 的 `/dev/fb0` rotation 用例 |
| P1-3 | UI 核心 | `ui_core.c` | 16, 357 | UI 核心层直接 `#include "app/shutdown/power_key_popup.h"` 并调用 `power_key_popup_is_dual_active()`，形成 UI → App 的反向依赖。 | 引入回调/钩子（如 `xerintosh_set_dual_key_callback`），让 App 注册；UI 核心保持对 App 零依赖。 | `test_ui_empty_root.cpp` 应能在不链接 `app/shutdown` 的情况下编译 |
| P1-4 | UI 上下文 | `ui_context.h:33,79` `ui_core.c:295-296` | 33, 79, 295–296 | `g_xerintosh_exit_requested` 是文档中供内核任务写入的“跨任务退出信号”，但当前为普通 `bool`，无 `volatile`、原子访问或互斥。 | 改为 `volatile bool` 或 FreeRTOS 事件组；并在文档中明确写入约束。 | 新增并发回归测试 |
| P1-5 | UI 类型 | `ui_types.h` | 115 | `xerintosh_is_item_visible()` 的函数声明位于 `#ifdef __cplusplus } #endif` 之后，C++ 翻译单元看到 C++ 链接声明，而其实现在 `.c` 文件中为 C 链接，存在 ODR/链接风险。 | 将该声明移入 `extern "C"` 块内。 | `test_ui_item.cpp` 已包含该头，可补充取地址断言 |
| P1-6 | HAL 显示高级 | `hal_display_adv.cpp` | native: 47–58<br>hw: 132–134 | native 的 `hal_draw_xbitmap()` 使用 `bitmap[byteIndex] & (1 << bitIndex)`（LSB 在前），而硬件 `M5GFX::drawXBitmap()` 与 XBM 标准均为 MSB 在前；同一图标在两端的渲染结果不一致。 | native 实现改为 `bitmap[byteIndex] & (1 << (7 - bitIndex))`。 | `test_hal_display.cpp` 新增 XBM 位序断言 |
| P1-7 | UI 弹窗 | `ui_item_popup.c` | 275–280 | `xerintosh_hide_pop_up()` 将弹窗移出屏幕并设 `is_running=false`，但**未调用 `xerintosh_invalidate()`**。若当前脏标志为 false，列表背景不会被重绘，弹窗残像仍留在屏幕上。 | 在函数末尾追加 `xerintosh_invalidate();`。 | 新增 `test_ui_popup.cpp` |
| P1-8 | WiFi 弹窗 | `wifi_manager.cpp` | 96–114, 131–143 | `g_popup_content` 已加 `portMUX_TYPE` 保护，但 `g_popup_active`、`g_popup_span`、`g_popup_start` 仍为普通变量，在 `wifi_popup_refresh()` 与 `wifi_popup_request()` 之间存在读写竞态。 | 将这三个字段也纳入同一 critical section，或改用 FreeRTOS 队列传递完整弹窗请求。 | 新增并发/桩测试 |
| P1-9 | HAL 字体 | `hal_display_font.cpp` | 271–274 | 硬件路径每行最多复制 255 字节到栈缓冲区，超长行会被静默截断，且不会换行补偿。 | 对超长行分块绘制，或在 API 文档中声明限制。 | `test_hal_display.cpp` 超长字符串用例 |

### P2（可维护性 / 硬编码 / 可测试性）

| ID | 模块 | 文件 | 行号 | 问题描述 | 建议动作 | 关联测试 |
|----|------|------|------|----------|----------|----------|
| P2-1 | HAL 字体 | `hal_display_font.cpp` | 216–219 | native `hal_get_string_width()` 直接 `strlen(str)*(FONT_W+1)`，对 `\n` 或多字节字符会高估宽度，与硬件 `textWidth()` 行为不一致。 | 统计可见字符数；对 `\n` 不计宽度，或对 UTF-8 做基本宽度估算。 | `test_hal_display.cpp` |
| P2-2 | UI 绘制 | `ui_draw_list.c` `ui_draw_widgets.c` `ui_draw_anim.c` `ui_draw_icons.c` | 多处 | 大量绘制函数依赖全局 `g_xerintosh_draw_color`，通过副作用传递颜色，难以单测且容易在图标绘制中被临时修改后未恢复。 | 将颜色作为参数下传，或把 `g_xerintosh_draw_color` 限制为局部静态。 | `test_ui_dispatch.cpp` |
| P2-3 | UI 布局 | `ui_draw_list.c` `ui_draw_icons.c` `ui_dispatch.c` | 54–75, 21–59, 227–238 | 装饰线、图标、开关/滑块右侧控件大量使用硬编码像素值，未跟随字体高度或屏幕方向变化。 | 使用 `hal_get_font_height()`、`LIST_ITEM_*_MARGIN`、`SCREEN_WIDTH/HEIGHT` 推导。 | `test_ui_service_landscape.cpp` |
| P2-4 | UI 列表 | `ui_draw_list.c` | 82–88 | 滚动条长度缓存 `_cached_child_num/_cached_length` 仅以 `child_num` 为 key；屏幕高度变化（旋转）后长度不会重新计算。 | 缓存 key 同时包含 `SCREEN_HEIGHT`，或每帧直接计算（开销极小）。 | `test_ui_service_landscape.cpp` |
| P2-5 | UI 上下文 | `ui_context.c` | 22–23 | `s_info_bar`/`s_pop_up` 初始宽度硬编码为 80，未使用 `SCREEN_WIDTH`。 | 初始化时使用 `SCREEN_WIDTH`。 | `test_ui_service_landscape.cpp` |
| P2-6 | UI 派发 | `ui_dispatch.c` | 193–197 | `dispatch_measure_text()` 以 `item->content` 指针作为缓存 key；若字符串内容被原地修改，缓存宽度会失效。 | 增加长度/哈希校验，或取消缓存（文本宽度测量开销很小）。 | `test_ui_dispatch.cpp` |
| P2-7 | UI 派发 | `ui_dispatch.c` | 28–39 | `dispatch_itoa()` 对 `INT16_MIN`（-32768）取反会溢出，结果可能错误。 | 使用 `int32_t tmp = -val;` 中转。 | `test_ui_dispatch.cpp` 新增 slider 极值用例 |
| P2-8 | UI 派发 | `ui_dispatch.c` | 386–389 | `type_in_range()` 依赖枚举顺序 `button_item` 为最后一个；若枚举顺序调整会越界。 | 改为 `item->type < item_type_count` 或显式枚举边界检查。 | `test_ui_dispatch.cpp` |
| P2-9 | UI 选择器 | `ui_item_selector.c` | 162–174 | `xerintosh_selector_exit_current_item()` 访问 `selected_item->parent->parent->child_num`，未校验祖父节点是否为 NULL。 | 增加空指针检查。 | `test_ui_empty_root.cpp` |
| P2-10 | UI 弹窗 | `ui_item_popup.c:86-96`<br>`ui_draw_widgets.c:76-86` | 86–96, 76–86 | `popup_compute_height()` 在两个文件中重复实现。 | 抽到共享头或单一实现文件。 | 编译检查 |
| P2-11 | UI 弹窗 | `ui_item_popup.c` | 46–77 | `find_wrap_break()` 对中文字符宽度按固定 12px 估算，与实际字体宽度可能不符，导致换行位置不准。 | 使用 `hal_get_string_width()` 逐字符测量，或维护更精确的宽度表。 | `test_ui_popup.cpp` |
| P2-12 | UI 弹窗 | `ui_item_popup.c` | 255 | `xerintosh_push_pop_up()` 仅保存 `_content` 指针，未拷贝内容；调用方若复用/释放缓冲区会导致弹窗文本错乱。 | 在 push 时将内容拷贝到内部缓冲区。 | 新增生命周期测试 |
| P2-13 | 串口监视器 | `sm_buffer.h:21`<br>`sm_ui.c:165` | 21, 165 | 终端行长度仍限制 64 字符；横屏可见行仅约 7 行。 | 提升 `SM_TERM_LINE_LEN`；横屏模式下可考虑更小字体或分栏。 | `test_ui_service_landscape.cpp` |
| P2-14 | HAL 显示设备 | `dev_fb0.c` | 103–106 | `DEV_FB_IOCTL_SET_ROTATION` 直接调用 `hal_display_set_rotation()` 而不重建 sprite，与 `main.cpp` 的做法不一致。 | 在 ioctl 中补充 `hal_display_init()`，或统一由 HAL 内部处理。 | `test_kernel_devices.cpp` |
| P2-15 | UI 列表 | `ui_draw_list.c` | 175 | 裁剪矩形宽度 `_avail_width` 在小屏幕上可能为负，传给 `hal_set_clip_rect()` 行为未定义。 | 钳位 `_avail_width > 0`。 | `test_ui_item.cpp` |

### P3（风格 / 轻微硬编码 / 文档）

| ID | 模块 | 文件 | 行号 | 问题描述 | 建议动作 | 关联测试 |
|----|------|------|------|----------|----------|----------|
| P3-1 | UI 核心 | `ui_core.c` | 197–198 | `xerintosh_init_list()` 中 `g_xerintosh_selector.h_selector = 160` 硬编码。 | 改为 `SCREEN_HEIGHT`。 | `test_ui_empty_root.cpp` |
| P3-2 | UI 选择器 | `ui_item_selector.c` | 49–50 | 初始选择器坐标 `2 * SCREEN_HEIGHT` 合理，但 `h_selector = 160` 硬编码。 | 改为 `SCREEN_HEIGHT`。 | `test_ui_empty_root.cpp` |
| P3-3 | UI 图标 | `ui_draw_icons.c` | 21–59 | 图标绘制仍使用 `switch(icon)`，与 `ui_dispatch.c` 的“消灭 switch”目标不一致。 | 改为静态函数指针表。 | `test_ui_dispatch.cpp` |
| P3-4 | UI 核心头 | `ui_core.h` | 111–119 | 注释块嵌套了未闭合的 `/**`，Doxygen 解析可能异常。 | 修复注释结构。 | 文档生成 |
| P3-5 | UI 列表 | `ui_item_list.c` | 101–103 | `xerintosh_remove_item_from_list()` 在销毁子树前未检查选择器是否指向该子树，可能产生悬垂指针。 | 调用 `ui_selector_safety_move_out()` 后再销毁。 | `test_ui_item.cpp` |
| P3-6 | HAL 输入 | `hal_input.cpp` | 139, 152, 179 | 硬件路径多处使用 `millis()`，而 HAL system 已提供 `hal_get_ticks()`；语义虽同但接口不一致。 | 统一改为 `hal_get_ticks()`。 | `test_hal_system.cpp` |

---

## 本轮重构建议排期

| 子阶段 | 处理 ID | 说明 |
|--------|---------|------|
| 2.1 HAL 输入 | P1-1, P3-6 | 修复双击竞争，统一 tick 接口 |
| 2.2 HAL 显示 | P1-2, P1-6, P1-9, P2-1, P2-14 | 旋转重建 sprite、XBM 位序、字体宽度一致性 |
| 2.3 UI 核心 | P1-3, P1-4, P1-7, P2-4, P2-5, P2-8, P3-1, P3-2, P3-4 | 解耦、状态安全、布局响应式 |
| 2.4 UI 派发 / 绘制 | P2-2, P2-3, P2-6, P2-7, P2-9, P2-10, P2-11, P2-12, P2-15, P3-3, P3-5 | 消灭全局 draw color、消除重复、增强边界检查 |
| 2.5 串口监视器 | D7, D8, P2-13 | 行长度与横屏可见行优化 |
| 2.6 文档 / 测试 | P1-5, P1-8 | 链接声明、WiFi 弹窗并发保护 |

---

## 关键结论
1. **`hal_draw_string()` 的 `\n` 处理已完整**，两端路径均按行分割。
2. **最需优先修复的是 HAL 输入双击竞争（P1-1）和 HAL 显示旋转后 sprite 生命周期（P1-2）**，前者会导致按键事件丢失，后者在非常规调用路径下会出现画面方向异常。
3. **UI 核心对 App 层的反向依赖（P1-3）** 与 **跨任务信号 `g_xerintosh_exit_requested` 的非原子访问（P1-4）** 是本轮架构清理的重点。
4. `sm_ui.c` 的 `\n` 分支当前为防御性代码（上游已剥离 `\n`），可保留或加注释说明；真正影响体验的是 **64 字符截断** 与 **横屏可见行不足**。
