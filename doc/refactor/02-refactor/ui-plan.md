# 阶段 2.4 UI 核心层重构微观实施计划

## 1. 本轮 UI 层目标

### 处理的问题
- **H-P2-02**：`src/ui/ui_item.h` 缺少 `extern "C"` 保护，需补齐使其成为 C/C++ 兼容聚合头。
- **H-P2-03**：`src/ui/ui_draw_list.c` 中列表项绘制需完全走 `ui_dispatch.c` 派发表；当前工作树已初步完成，本轮做审计确认与回归测试加固。

### 明确延后的问题
- **H-P2-04**：`src/ui/ui_item_popup.c:132-270` 的 `xerintosh_push_pop_up()` 约 140 行，含自动换行、缓存、状态机与 `goto`。

> 延后理由：`xerintosh_push_pop_up()` 涉及多语言换行、弹窗缓存、状态机与动画目标值同步，拆分改动面大，且当前功能稳定。建议放到专门轮次，并配套 `test_ui_popup.cpp` 做行为回归，避免在本轮小改动中引入弹窗显示异常。

---

## 2. 子任务列表

### T1：H-P2-02 — 为 `ui_item.h` 添加 `extern "C"` 保护并明确聚合头身份

- **目标问题**：`src/ui/ui_item.h:1-36` 缺少 `extern "C"`，作为被 C++ 测试和 HAL C++ 桥接层广泛包含的聚合头，存在 C++ 名称修饰风险。
- **变更文件**：
  - `src/ui/ui_item.h`
  - `test/test_native/test_ui_dispatch.cpp`（扩展以验证 C++ 侧包含与链接）
- **步骤**：
  1. 在 `ui_item.h` 的 `#ifndef UI_ITEM_H / #define UI_ITEM_H` 之后、所有 `#include` 之前插入：
     ```c
     #ifdef __cplusplus
     extern "C" {
     #endif
     ```
  2. 在文件末尾 `#endif /* UI_ITEM_H */` 之前插入：
     ```c
     #ifdef __cplusplus
     }
     #endif
     ```
  3. 更新文件头注释，在 `@details` 段增加一句：
     > 本文件为 C/C++ 兼容聚合头；内部子头文件已各自具备 `extern "C"` 保护，此处再做一层包裹以保证 C++ 翻译单元安全包含。
  4. 在 `test_ui_dispatch.cpp` 中新增一个仅验证编译和 C++ 链接的测试：
     - `UiDispatchTest.CppCanIncludeUiItemHeader`：在 `extern "C"` 块内调用 `xerintosh_get_root_list()` 并断言非 NULL。
  5. 运行 `pio test -e native` + `pio run -e m5stick-c`。
  6. `git commit`。
- **回滚策略**：`git revert` 本次 commit，恢复 `ui_item.h` 原始内容。
- **验收标准**：
  - `ui_item.h` 在 C 和 C++ 翻译单元中均可无警告包含。
  - 新增 C++ 链接测试通过。
  - `m5stick-c` 与 `native` 构建成功，无新增警告。

---

### T2：H-P2-03 — 审计并确认 `ui_draw_list.c` 绘制路径完全走 `ui_dispatch.c`

- **目标问题**：`src/ui/ui_draw_list.c:212-228` 原存在内联 `switch (_item->type)`，需确保列表绘制统一通过 `ui_dispatch.c` 的 `xerintosh_dispatch_draw()` 路由。
- **变更文件**：
  - `src/ui/ui_draw_list.c`（按需清理残留）
  - `test/test_native/test_ui_dispatch.cpp`（可选：补充绘制派发回归测试）
- **步骤**：
  1. 使用 Grep 在工作树 `src/ui/` 下搜索 `switch\s*\(\s*.*->type\s*\)`，确认 `ui_draw_list.c` 中已无内联类型 switch。
  2. 阅读 `ui_draw_list.c` 的 `xerintosh_draw_list_item()`，确认：
     - 调用 `xerintosh_dispatch_draw(_item, _x_list_item, _y_list_item)`；
     - 调用 `xerintosh_dispatch_has_right_control(_item)`；
     - 滑块覆盖层由 `xerintosh_draw_slider_overlays()` 通过 `xerintosh_dispatch_draw_overlay()` 派发。
  3. 若发现任何残留的内联 `if/switch` 类型判断（例如对 `switch_item`/`slider_item` 的特殊分支），将其提取为 `ui_dispatch.c` 中的 handler，并注册到 `s_dispatch` 的对应字段。
  4. 在 `test_ui_dispatch.cpp` 中新增绘制派发的运行期验证：
     - `UiDispatchTest.DrawDoesNotSwitchOnType`：创建一个 `switch_item` 和一个 `slider_item`，调用 `xerintosh_dispatch_draw()` 不崩溃。
  5. 运行 `pio test -e native` + `pio run -e m5stick-c`。
  6. `git commit`（若无代码改动，可仅提交测试增强；若完全无改动，则本任务以审计报告形式记录）。
- **回滚策略**：若迁移了残留 switch，则 `git revert` 该 commit；若仅增强测试，则移除新增测试。
- **验收标准**：
  - `src/ui/ui_draw_list.c` 中不存在任何 `switch (.*->type)`。
  - 列表渲染在 native 和硬件构建中均正常。
  - `test_ui_dispatch.cpp` 全部通过。

---

### T3：H-P2-04 — 延后记录与拆分预研

- **目标问题**：为后续专门轮次留下可执行的问题定义与初步拆分方案。
- **变更文件**：
  - `doc/refactor/02-refactor/ui-plan.md`（本文件，已包含延后说明）
  - 不改动源码。
- **步骤**：
  1. 在 `doc/refactor/02-refactor/` 下创建占位文件 `ui-popup-deferred.md`，记录：
     - 当前 `xerintosh_push_pop_up()` 的 140 行职责清单；
     - 建议拆分出的子函数：`popup_wrap_text()`、`popup_compute_dimensions()`、`popup_update_state()`；
     - 移除 `goto calc_height` 的方案：用 `bool wrapped` 状态变量 + 提前返回替代；
     - 依赖测试：`test_ui_popup.cpp` 需覆盖 1/2/3 行换行、相同内容重置计时、超长文本截断。
  2. 不提交代码变更，仅作为 backlog 文档。
- **回滚策略**：不适用，无源码改动。
- **验收标准**：文档清晰，可供下一轮 coder agent 直接开始拆分。

---

## 3. 依赖关系图

```
T1 (ui_item.h extern "C")
        │
        │ 独立
        ▼
T2 (ui_draw_list.c 派发审计)
        │
        │ 独立
        ▼
T3 (H-P2-04 延后记录)
```

- **可并行**：T1 与 T2 互不依赖，可并行执行。
- **串行建议**：T1 优先，因为 T2 的测试文件 `test_ui_dispatch.cpp` 以 C++ 编译，需依赖 `ui_item.h` 的 `extern "C"` 正确性。

---

## 4. 风险与回退点

| 子任务 | 风险等级 | 主要风险 | 回退方式 |
|--------|----------|----------|----------|
| T1 | 低 | `extern "C"` 包裹位置错误导致 C 编译器不认识 `__cplusplus` 宏，或破坏已有 C++ 测试包含 | `git revert` 恢复头文件 |
| T2 | 低~中 | 若审计中发现未迁移的 switch，迁移时可能误改绘制行为（如 switch/slider 右侧控件坐标） | 单独 commit，`git revert` 恢复 |
| T3 | 无 | 仅文档，无源码改动 | 删除占位文档 |

**统一回退点**：每个子任务独立 commit，任意时刻可 `git reset --hard HEAD~1` 回到上一绿色基线。

---

## 5. 测试策略

### 新增/修改的测试文件
- `test/test_native/test_ui_dispatch.cpp`：
  - `CppCanIncludeUiItemHeader`：验证 C++ 侧可安全包含 `ui/ui_item.h` 并链接。
  - `DrawDoesNotSwitchOnType`：验证 `xerintosh_dispatch_draw()` 可派发 switch/slider 绘制而不依赖内联 type switch。

### 边界条件覆盖
- **C/C++ 兼容**：`ui_item.h` 在 `.c` 和 `.cpp` 文件中被包含时均无名称修饰问题。
- **绘制派发**：`switch_item`、`slider_item`、`list_item`、`button_item`、`user_item` 均通过 `xerintosh_dispatch_draw()` 路由。
- **无内联 switch**：`src/ui/ui_draw_list.c` 中不存在 `switch (.*->type)`。

### 验证命令（每个子任务必须执行）
```bash
pio test -e native
pio run -e m5stick-c
```

---

## 6. 本轮处理与延后问题 ID 汇总

- **本轮处理**：H-P2-02、H-P2-03
- **延后处理**：H-P2-04
