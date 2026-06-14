# 2.3 UI 核心层重构报告

> **状态**：`DONE`
> **日期**：2026-06-14
> **分支**：`refactor/2026-06-14-kernel-ui`

---

## 目标

1. 消灭 UI 源码中所有 item 类型处理的内联 switch/if 链，全部通过 `ui_dispatch.c` 派发表路由。
2. 增强空根节点、NULL 选择器等边界场景的安全防护。
3. 补充 native 回归测试，覆盖空根节点与派发表全生命周期行为。

---

## 变更文件

| 文件 | 变更类型 | 说明 |
|------|----------|------|
| `src/ui/ui_core.c` | 修改 | 空选择器防护；init_core 空根处理；refresh_* 系列函数 NULL/父项保护；measure 走派发表 |
| `src/ui/ui_item_selector.c` | 修改 | 导航/退出函数通过 `xerintosh_dispatch_input_*` 路由；移除内联 slider/user 类型判断；移除已迁移的 `handle_user_item_exit` |
| `src/ui/ui_draw_list.c` | 修改 | 移除本地 draw switch/slider/icon-only 实现；绘制与覆盖层走派发表；文字布局宽度通过 `xerintosh_dispatch_has_right_control()` 判断 |
| `src/ui/ui_item_list.c` | 修改 | `xerintosh_destroy_item_tree()` 使用 `xerintosh_dispatch_destroy()` 调用 user_item destroy_callback |
| `src/ui/ui_dispatch.c` | 重写 | 扩展为完整 lifecycle vtable：enter / input_next / input_prev / input_exit / measure / draw / draw_overlay / destroy / has_right_control |
| `src/ui/ui_item_core.h` | 修改 | 新增 8 个公开派发 API 声明 |
| `test/test_native/test_ui_empty_root.cpp` | 新增 | 空根节点回归测试 |
| `test/test_native/test_ui_dispatch.cpp` | 新增 | 派发表 enter/input/measure/destroy/right-control 行为测试 |
| `doc/ui/dispatch.md` | 待更新 | 派发表文档需同步扩展后的 vtable |

---

## 设计决策

### 1. 全生命周期派发表

原先 `ui_dispatch.c` 仅处理 `enter`。重构后将所有“按类型不同”的行为集中到一张 vtable：

```c
typedef struct {
    void (*enter)(xerintosh_list_item_t *);
    bool (*input_next)(xerintosh_list_item_t *);
    bool (*input_prev)(xerintosh_list_item_t *);
    bool (*input_exit)(xerintosh_list_item_t *);
    int16_t (*measure)(xerintosh_list_item_t *);
    void (*draw)(xerintosh_list_item_t *, int16_t, int16_t);
    void (*draw_overlay)(xerintosh_list_item_t *);
    void (*destroy)(xerintosh_list_item_t *);
    bool (*has_right_control)(xerintosh_list_item_t *);
} xerintosh_dispatch_vtable_t;
```

输入类函数返回 `bool`：
- `true` 表示输入已被类型特定逻辑消费（如 slider 编辑模式增减值、user_item 运行态忽略导航）。
- `false` 表示未消费，调用方执行默认导航。

### 2. 空根节点安全

- `xerintosh_init_core()` 在根节点无子项时不再尝试绑定 selector 到 root（root 无 parent，绑定会失败）。
- `xerintosh_ui_main_core()` 在选择器未选中任何项时直接返回，避免后续生命周期/渲染函数解引用 NULL。
- 选择器导航函数（next/prev/jump/exit）均增加 NULL 检查。
- 位置刷新函数增加 `selected_item` 与 `parent` 空指针防护。

### 3. 行为保持

- `xerintosh_selector_go_next_item` / `go_prev_item` 的 slider 编辑模式增减值逻辑原样迁移到 `dispatch_input_next_slider` / `dispatch_input_prev_slider`。
- `xerintosh_selector_exit_current_item` 的 slider 取消编辑、user_item 触发退出逻辑迁移到 `dispatch_input_exit_slider` / `dispatch_input_exit_user`。
- `handle_user_item_exit` 从 `ui_item_selector.c` 迁移到 `ui_dispatch.c` 内部，作为 `dispatch_input_exit_user` 的实现细节。
- `ui_draw_list.c` 的 switch/slider 绘制与覆盖层绘制原样迁移到 `dispatch_draw_switch` / `dispatch_draw_slider` / `dispatch_draw_overlay_slider`。
- `xerintosh_destroy_item_tree` 的 user_item destroy_callback 调用逻辑迁移到 `dispatch_destroy_user`。

---

## 验证结果

### Native 测试

```bash
$ pio test -e native
================ 366 test cases: 366 succeeded ================
```

新增测试：
- `UiEmptyRootTest.CoreMainLoopHandlesEmptyRoot`
- `UiEmptyRootTest.SelectorNavigationHandlesEmptyRoot`
- `UiDispatchTest.SwitchEnterTogglesValueAndFiresCallback`
- `UiDispatchTest.ButtonEnterFiresCallback`
- `UiDispatchTest.SliderInputNextInEditModeIncrementsValue`
- `UiDispatchTest.SliderInputNextInNormalModeDoesNotConsume`
- `UiDispatchTest.SliderInputExitRestoresBackupValue`
- `UiDispatchTest.MeasureReturnsFullWidthForSwitchAndSlider`
- `UiDispatchTest.HasRightControlOnlyForSwitchAndSlider`
- `UiDispatchTest.DestroyCallsUserItemDestroyCallback`

### 硬件构建

```bash
$ pio run -e m5stick-c
RAM:   [==        ]  22.3% (used 73216 bytes from 327680 bytes)
Flash: [========= ]  88.0% (used 1845241 bytes from 2097152 bytes)
========================= [SUCCESS] =========================
```

无新增编译警告。

---

## 已知遗留

- `doc/ui/dispatch.md` 仍只描述旧的 `enter` 派发表，需在阶段 2.5 文档同步中更新为完整 vtable。
- `xerintosh_is_in_user_item()` 在 `ui_core.c` 中仍使用 `xerintosh_to_user_item()` 判断类型，未纳入 vtable；因其为纯状态查询且逻辑极简，保留为可接受例外。

---

## 回滚策略

所有变更均通过 `git revert` 可回滚：
- `ui_dispatch.c` 重写为单一 commit。
- 各调用方修改分散在对应文件的 commit 中。
- 测试文件为独立新增，可单独移除。

---

> **验收标准**：native 测试 366/366 通过，硬件构建成功，无新增警告，源码中不再存在 item 类型内联 switch。
