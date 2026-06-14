# Xerintosh UI 核心层重构前静态诊断报告

**工作树**：`/Users/yukisala/Documents/PlatformIO/Projects/M5Stick-P1/.worktrees/refactor-2026-06-14-kernel-ui`  
**扫描范围**：`src/ui/`  
**诊断日期**：2026-06-14  
**基线状态**：`pio run -e m5stick-c` / `pio test -e native` 均通过

---

## 一、P0 级问题（必须优先修复）

| ID | 模块 | 文件 | 问题描述 | 建议重构动作 | 关联测试 |
|---|---|---|---|---|---|
| P0-1 | UI Core / 初始化 | `src/ui/ui_core.c:94-117` | 空根节点边界未处理：根节点无 parent，`xerintosh_bind_item_to_selector()` 会失败，后续主循环解引用 `selected_item` 将崩溃。 | 在 `xerintosh_init_core()` 中显式处理空根；所有主循环路径增加 `selected_item != NULL` 防御。 | 新增 `test_ui_empty_root.cpp` |
| P0-2 | UI Dispatch | `src/ui/ui_dispatch.c` | 派发表仅覆盖 `enter` 生命周期点。`init`、`draw`、`input`、`exit`、`destroy` 等仍散落各处。 | 扩展 `ui_dispatch.c` 为多维派发表；将分散的类型处理逻辑迁移到对应 handler。 | 新增 `test_ui_dispatch.cpp` |
| P0-3 | Item Base / 指针安全 | `src/ui/ui_item_base.c:114-173` | `xerintosh_new_switch_item()` 与 `xerintosh_new_slider_item()` 未校验 `_value` 是否为 `NULL`。 | 在构造函数中增加 NULL 校验；派发表中也增加运行时断言/空指针保护。 | 新增 `test_ui_item_null_value.cpp` |

## 二、P1 级问题（建议本次重构修复）

| ID | 模块 | 文件 | 问题描述 | 建议重构动作 | 关联测试 |
|---|---|---|---|---|---|
| P1-1 | Item Base / 类型转换 | `src/ui/ui_item_list.c:143-145` | 销毁 user_item 时直接使用 `(xerintosh_user_item_t *)_item` 强制转换。 | 改为 `xerintosh_to_user_item(_item)`。 | `test_ui_item.cpp` 扩展断言 |
| P1-2 | Widget / 重复代码 | `src/ui/ui_item_popup.c:86-96` 与 `src/ui/ui_draw_widgets.c:76-86` | `popup_compute_height()` 在两个文件中重复定义。 | 提取到公共模块（如 `ui_widget_utils.c/h`）。 | 新增 `test_ui_widget_utils.cpp` |
| P1-3 | UI Core / 命名 | `src/ui/ui_core.c:39-51` | `xerintosh_animation()` 实际是缓动插值，命名不精确。 | 重命名为 `xerintosh_ease()`，保留旧名作为 deprecated 别名。 | 已有测试间接覆盖 |
| P1-4 | Popup / 超长函数 | `src/ui/ui_item_popup.c:132-270` | `xerintosh_push_pop_up()` 约 140 行，含自动换行、缓存、高度计算、状态机，使用 `goto`。 | 拆分为子函数；移除 `goto`。 | 新增 `test_ui_popup.cpp` |
| P1-5 | Draw List / 超长函数 | `src/ui/ui_draw_list.c:198-288` | `xerintosh_draw_list_item()` 约 90 行，混合图标/文字/跑马灯/类型判断。 | 拆分为 `draw_item_icon()`、`draw_item_text()`、`draw_item_marquee()`；将类型 switch 替换为绘制派发表。 | 新增 `test_ui_draw_list.cpp` |
| P1-6 | Draw Anim / 超长函数 | `src/ui/ui_draw_anim.c:21-135` | `xerintosh_draw_exit_animation()` 约 115 行，混合状态机/沙漏/扫描线。 | 拆分为 `exit_anim_update_state()`、`draw_hourglass()`、`draw_scanlines()`。 | `test_exit_animation.cpp` |
| P1-7 | Drawer / 全局状态依赖 | `src/ui/ui_draw_*.c` | 绘制函数直接读写 `g_xerintosh_*` 全局状态，无法注入测试上下文。 | 引入绘制上下文参数，或提供 `ui_drawer_set_context()`。 | 新增基于 HAL stub 的绘制测试 |
| P1-8 | UI Context / 耦合 | `src/ui/ui_context.c/h`、`src/ui/ui_item.h` | `ui_context.h` 与 `ui_item.h` 循环包含；`ui_item.h` 通过宏暴露全局实例。 | 将 `g_*` 宏标记为 deprecated；打破循环包含。 | `test_ui_context.cpp` |
| P1-9 | Item List / 选择器悬空 | `src/ui/ui_item_list.c:78-104`、`src/ui/ui_item_list.c:111-122` | 移除/清空子树时未检查选择器是否位于该子树内，释放后选择器指针悬空。 | 在移除/清空前调用 `ui_selector_safety_move_out()`。 | 新增 `test_ui_selector_safety.cpp` |

## 三、P2 级问题（可选改进）

| ID | 模块 | 文件 | 问题描述 | 建议重构动作 | 关联测试 |
|---|---|---|---|---|---|
| P2-1 | Draw List / 类型派发 | `src/ui/ui_draw_list.c:212-228` | 列表绘制中仍有内联 `switch (_item->type)`。 | 替换为 `s_draw_dispatch[_item->type](item, x, y)`。 | `test_ui_dispatch.cpp` |
| P2-2 | Item Base / 生命周期回调 | `src/ui/ui_item_list.c:143-148` | `destroy_callback` 仅在 `user_data != NULL` 时调用。 | 改为 `if (user->destroy_callback != NULL)`。 | 新增 `test_ui_destroy_callback.cpp` |
| P2-3 | Popup / 缓冲区截断 | `src/ui/ui_item_popup.c:16-19` | 弹窗换行缓冲区固定 48 字节，长文本或 UTF-8 可能被截断。 | 动态分配或 UTF-8 安全截断。 | 新增 `test_ui_popup_wrap.cpp` |
| P2-4 | Draw List / 滚动条缓存 | `src/ui/ui_draw_list.c:161-167` | 滚动条高度缓存未关联 `SCREEN_HEIGHT`，横竖屏切换后可能错误。 | 缓存键增加 `SCREEN_HEIGHT`。 | 新增 `test_ui_scrollbar.cpp` |
| P2-5 | Slider / 初始值范围 | `src/ui/ui_item_base.c:156-173` | `xerintosh_new_slider_item()` 未校验初始值是否在 `[min, max]`。 | 创建时钳位初始值。 | 新增 `test_ui_slider_range.cpp` |
| P2-6 | Documentation / 代码不一致 | `doc/ui/draw-list.md:16`、`doc/ui/drawer.md:176` | 文档仍写 `static bool is_item_visible()`，而代码已重构为公开 API。 | 同步文档代码片段与行号链接。 | — |
| P2-7 | Core / 选择器宽度缓存 | `src/ui/ui_core.c:141-145` | 选择器宽度缓存基于 `content` 指针比较；原地修改字符串会失效。 | 文档中明确禁止原地修改，或增加 CRC/长度校验。 | — |

## 四、合规性检查结论

| 检查项 | 结果 | 说明 |
|---|---|---|
| 内联 `switch` 未走 `ui_dispatch.c` | ⚠️ 存在 | `enter` 已派发；`draw` 仍内联 |
| `ui_dispatch.c` 生命周期覆盖 | ❌ 不全 | 仅 `enter`；缺少 `init/draw/input/exit/destroy` |
| 动画插值公式分散 | ✅ 已封装 | 集中在 `xerintosh_animation()`，但命名建议优化 |
| 基类/派生类转换经 `xerintosh_safe_cast` | ⚠️ 大部分 | `ui_item_list.c:145` 存在裸强转 |
| 绘制函数可单元测试 | ❌ 较差 | 过度依赖全局状态 |
| `ui_context.c` 耦合 | ⚠️ 偏高 | 循环包含 + 全局宏 |
| 边界条件 | ❌ 有空根崩溃风险 | `xerintosh_init_core()` 未处理空根 |
| 超长函数 | ⚠️ 存在 | `xerintosh_push_pop_up`、`xerintosh_draw_exit_animation`、`xerintosh_draw_list_item` 均超 50 行 |
| 空指针/悬空指针 | ❌ 存在 | switch/slider value NULL；选择器移除后悬空 |
| 模块前缀 / `extern "C"` / include guard | ✅ 合规 | 所有 `src/ui/*.h` 均具备 |
| 文档与代码一致 | ⚠️ 存在 | `is_item_visible` 签名未同步 |

## 五、推荐重构顺序

1. **P0-1** 空根节点防御 + **P0-3** switch/slider NULL 校验（安全基线）
2. **P0-2** 扩展 `ui_dispatch.c` 为多维生命周期派发表（架构核心）
3. **P1-1** 统一使用 `xerintosh_to_*` + **P1-9** 选择器悬空修复
4. **P1-2** 提取 `popup_compute_height` + **P1-4/1-5/1-6** 拆分超长函数
5. **P1-7/1-8** 绘制上下文解耦 + 全局状态显式化
6. **P2-1~P2-7** 文档同步、缓存失效、边界加固等收尾
