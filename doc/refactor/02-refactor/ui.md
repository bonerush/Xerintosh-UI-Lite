# UI 核心层重构报告

## 范围

- 处理诊断问题：H-P2-02、H-P2-03
- 延后问题：H-P2-04

## 变更摘要

| 变更类型 | 数量 | 说明 |
|----------|------|------|
| 修改文件 | 2 | UI 头文件与测试 |
| 新增文件 | 1 | `ui-popup-deferred.md` |
| 接口修复 | 1 | `ui_item.h` 添加 `extern "C"` 保护 |
| 回归加固 | 1 | 绘制派发审计与测试 |
| 新增测试 | 3 | C++ 链接、绘制派发、列表绘制 |

## 详细变更

### 1. T1：为 `ui_item.h` 添加 `extern "C"` 保护（H-P2-02）
**原因**：H-P2-02  
**实现**：
- 在 `src/ui/ui_item.h` 中添加 `extern "C"` 包裹，使其成为 C/C++ 兼容聚合头。
- 更新头注释说明聚合头身份。
- 在 `test/test_native/test_ui_dispatch.cpp` 中新增 C++ 链接回归测试。
**影响接口**：无 public API 变化。  
**文档更新**：头注释已更新，阶段 2.5 同步 `doc/ui/item.md`。

### 2. T2：审计列表绘制派发（H-P2-03）
**原因**：H-P2-03  
**实现**：
- 审计 `src/ui/ui_draw_list.c`，确认无按 `item->type` 的内联 switch 残留。
- 列表项绘制通过 `xerintosh_dispatch_draw()` 路由。
- 右侧控件判断通过 `xerintosh_dispatch_has_right_control()`。
- 滑块覆盖层通过 `xerintosh_dispatch_draw_overlay()`。
- 新增回归测试覆盖绘制派发。
**影响接口**：无 public API 变化。  
**文档更新**：阶段 2.5 同步 `doc/ui/draw-list.md`。

### 3. T3：Pop-up 拆分延后记录（H-P2-04）
**原因**：H-P2-04  
**处理**：未改动源码，在 `doc/refactor/02-refactor/ui-popup-deferred.md` 中记录问题、拆分方案和依赖测试，供后续专门轮次处理。

## 测试

- 新增/修改测试：
  - `test/test_native/test_ui_dispatch.cpp`：
    - `UiDispatchTest.CppCanIncludeUiItemHeader`
    - `UiDispatchTest.DrawDoesNotSwitchOnType`
    - `UiDispatchTest.ListDrawUsesDispatch`
- 验证结果：
  - `pio test -e native`：✅ PASS（415 个测试用例，1 个 skipped，414 个 succeeded）
  - `pio run -e m5stick-c`：✅ PASS（Flash 88.0%，RAM 22.3%）

## 检查清单

- [x] 所有导出函数有模块前缀
- [x] 头文件有 `extern "C"` 保护
- [x] 头文件有 include guard
- [x] 结构体继承时基类放第一位（无新增继承）
- [x] 类型转换有安全检查
- [x] 回调统一带 `user_data`（无新增回调）
- [x] 没有 `nullptr`、`&` 引用出现在 C 接口中
- [ ] 文档已同步更新（阶段 2.5 统一处理）
- [x] 新增/修改代码有 native 测试覆盖
- [x] 硬件构建无新增警告

## 回滚点

- 每个子任务均为独立 commit，可单独 `git revert <commit>`。
- 统一回滚到阶段 2.4 开始前：`git reset --hard 42d7ec3`（HAL T2 提交）。

## 遗留问题

| ID | 问题 | 后续处理 |
|----|------|----------|
| H-P2-04 | `xerintosh_push_pop_up()` 约 140 行，含 `goto`，需拆分 | 延后到专门 pop-up 轮次，已记录拆分方案 |
