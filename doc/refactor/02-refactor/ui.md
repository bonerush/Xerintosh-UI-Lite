# 重构报告：UI 核心层（第十一轮 · 2026-06-19）

## 范围

`src/ui/*`、`src/app/app_init.c`（仅回调注册）、`test/test_native/test_ui_core_fixes.cpp`。

## 目标

- 消除 UI 核心层对 App 层的反向依赖。
- 加固跨任务信号、选择器安全、列表绘制边界。
- 统一弹窗高度计算，清理硬编码尺寸与重复代码。

## 处理的问题清单

| ID | 优先级 | 文件 | 修复内容 |
|----|--------|------|----------|
| P1-3 | P1 | `src/ui/ui_core.c/h`, `src/app/app_init.c` | UI 核心不再直接 `#include <app/shutdown/power_key_popup.h>`；改为 `xerintosh_set_dual_key_callback()` 注册钩子，App 在 `app_init_managers()` 中注册 `power_key_popup_is_dual_active()`。 |
| P1-4 | P1 | `src/ui/ui_context.h` | `exit_requested` 改为 `volatile bool`，避免其他任务/ISR写入后被编译器缓存。 |
| P1-5 | P1 | `src/ui/ui_types.h` | 将 `xerintosh_is_item_visible()` 声明移入 `extern "C"` 块，避免 C/C++ 链接风险。 |
| P1-7 | P1 | `src/ui/ui_item_popup.c` | `xerintosh_hide_pop_up()` 末尾追加 `xerintosh_invalidate()`，防止弹窗残像。 |
| P2-4 | P2 | `src/ui/ui_draw_list.c` | 滚动条长度缓存 key 增加 `SCREEN_HEIGHT`，屏幕旋转后自动失效重算。 |
| P2-5 | P2 | `src/ui/ui_context.c` | `info_bar`/`pop_up` 初始宽度在 `xerintosh_context_init()` 中按 `SCREEN_WIDTH` 设置；静态初始化使用 `0`，避免硬件路径 `SCREEN_WIDTH` 为变量导致编译错误。 |
| P2-7 | P2 | `src/ui/ui_dispatch.c` | `dispatch_itoa()` 用 `int32_t` 中转，修复 `INT16_MIN` 取反溢出。 |
| P2-8 | P2 | `src/ui/ui_dispatch.c`, `src/ui/ui_types.h` | 枚举增加 `item_type_count` 哨兵，`type_in_range()` 改为 `< item_type_count`，不再依赖枚举顺序。 |
| P2-9 | P2 | `src/ui/ui_item_selector.c` | `xerintosh_selector_exit_current_item()` 增加祖父节点 NULL 检查。 |
| P2-10 | P2 | `src/ui/ui_widget.h`, `src/ui/ui_item_popup.c`, `src/ui/ui_draw_widgets.c` | 将 `popup_compute_height()` 提取为 `ui_widget.h` 中的 `static inline`，消除两份重复实现。 |
| P2-15 | P2 | `src/ui/ui_draw_list.c` | 文字裁剪宽度 `_avail_width` 钳位到 `>=1`，避免负宽传给 HAL。 |
| P3-2 | P3 | `src/ui/ui_item_selector.c` | 初始选择器高度硬编码 `160` 改为 `SCREEN_HEIGHT`。 |
| P3-4 | P3 | `src/ui/ui_core.h` | 修复嵌套/未闭合 Doxygen 注释块。 |
| P3-5 | P3 | `src/ui/ui_item_selector.c`, `src/ui/ui_item_list.c` | `ui_selector_safety_move_out()` 改进为跳过待移除子树根节点本身，必要时回退到父项；`xerintosh_remove_item_from_list()` 在销毁前调用它，避免悬垂指针。 |

## 新增/修改的 Public API

| API | 文件 | 说明 |
|-----|------|------|
| `void xerintosh_set_dual_key_callback(bool (*cb)(void))` | `src/ui/ui_core.h` | App 层注册双键检测回调；UI 核心据此在双键模式下跳过退场动画。 |
| `item_type_count` | `src/ui/ui_types.h` | `xerintosh_list_item_type_t` 末尾哨兵，用于边界检查。 |
| `volatile bool exit_requested` | `src/ui/ui_context.h` | 跨任务退出信号，外部写入需使用 `volatile` 语义。 |

## 测试

- 新增 `test/test_native/test_ui_core_fixes.cpp`，覆盖：
  - `HidePopUpInvalidates`
  - `SliderInt16MinDrawDoesNotCrash`
  - `DispatchWithInvalidTypeIsNoOp`
  - `SelectorSafetyMoveOutFromSubtree`
  - `RemoveItemMovesSelectorSafely`
  - `ExitCurrentItemGuardsNullGrandparent`
  - `MainLoopWithoutDualKeyCallbackDoesNotCrash`
- 调整 `test/test_native/test_kernel_devices.cpp` 中 `TtyS0ConcurrentReadWrite` 的测试数据，避开 `\n/\r`，以适配 native 回环的 `\n→\r\n` 转换。

## 验证结果

- 硬件构建：`pio run -e m5stick-c` ✅ 通过（无新增警告）。
- UI 相关 native 测试：
  ```bash
  ./.pio/build/native/program --gtest_filter=UiCoreFixesTest.*:UiDispatchTest.*:UiEmptyRootTest.*:UiItemTest.*
  ```
  23 个测试全部通过 ✅。
- 全量 `pio test -e native` 仍受第十轮遗留的 `SIGTRAP/SIGHUP` teardown 问题影响，在测试套件完成前异常退出；本次 UI 修改未引入新的测试失败。

## 提交记录

```
17e921d ui(core): low-risk fixes - popup invalidate, shared popup height, bounds, hardcoded dims
5fd14b7 ui(selector/context): safety move-out, volatile exit flag, responsive widths
4a50f78 ui(core): decouple power_key_popup via registered dual-key callback
77ae250 test(ui): regression tests for UI core fixes
```

## 后续工作

- 阶段 2.4 App 层重构。
- 阶段 2.5 同步更新 `doc/ui/core.md`、`doc/ui/dispatch.md`、`doc/ui/item.md` 中涉及的新 API 与枚举哨兵说明。
