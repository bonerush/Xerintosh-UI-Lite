# UI 核心层重构报告：脏矩形接口统一化

## 范围

- 处理诊断问题：D1（脏标志分散在 7 个文件中直接写入）、D2（`xerintosh_mark_dirty()` 未被框架自己使用）、D3（widget 动画缺失 dirty 标记一致性）
- 变更文件：
  - `src/ui/ui_dirty.h`（新增）
  - `src/ui/ui_dirty.c`（新增）
  - `src/ui/ui_context.h`（deprecation 标注）
  - `src/ui/ui_core.h`（新增 API 声明）
  - `src/ui/ui_core.c`（迁移至新 API）
  - `src/ui/ui_item_selector.c`（迁移至新 API）
  - `src/ui/ui_dispatch.c`（迁移至新 API）
  - `src/ui/ui_draw_list.c`（迁移至新 API）
  - `src/app/ui_task.c`（迁移至新 API）
  - `doc/developer-guide.md`（API 文档更新）

## 变更摘要

| 变更类型 | 数量 | 说明 |
|----------|------|------|
| 新增文件 | 2 | `ui_dirty.h` + `ui_dirty.c` |
| 新增函数 | 4 | `xerintosh_invalidate()`, `xerintosh_is_dirty()`, `xerintosh_clear_dirty()`, `xerintosh_mark_dirty()`（兼容别名）|
| 替换调用点 | 22→0 | 所有 `g_xerintosh_dirty = true` 替换为 `xerintosh_invalidate()` |
| 替换读取点 | 2→0 | `g_xerintosh_dirty` 读取替换为 `xerintosh_is_dirty()` |
| 文档更新 | 1 | `developer-guide.md` 新增脏矩形 API 参考表 |

## 详细变更

### 1. 新建 `src/ui/ui_dirty.h/c` — 脏矩形管理模块

**原因**：脏标志的访问分散在 7 个文件中，无统一 API 入口，开发者不清楚何时使用。

**实现**：创建独立模块，提供三元 API：
- `xerintosh_invalidate()` — 标记需要重绘（App 开发者唯一需要调用的接口）
- `xerintosh_is_dirty()` — 查询当前脏状态（框架内部使用）
- `xerintosh_clear_dirty()` — 清除脏标志（框架内部使用）

**影响接口**：新增 3 个 public API，原有 `xerintosh_mark_dirty()` 保留为兼容别名。

**文档更新**：`doc/developer-guide.md` 新增脏矩形 API 参考表和自动 invalidate 场景清单。

### 2. 统一所有脏标志写入点

**原因**：7 个文件中 22 处直接 `g_xerintosh_dirty = true` 难以追踪和审计。

**实现**：全部替换为 `xerintosh_invalidate()` 调用。覆盖：
- `ui_core.c`：动画运行、生命周期变更、退场动画（5 处）
- `ui_item_selector.c`：绑定、导航、确认、返回、重建锚点、安全移出（7 处）
- `ui_dispatch.c`：enter、input_next、input_prev、input_exit（4 处）
- `ui_draw_list.c`：文字滚动（1 处）
- 另有 `ui_core.c` 中 `g_xerintosh_dirty = false` → `xerintosh_clear_dirty()`
- `ui_task.c` 中 `xerintosh_get_context()->dirty` → `xerintosh_is_dirty()`

### 3. 废弃宏保留向后兼容

**原因**：现有代码和外部模块可能依赖 `g_xerintosh_dirty` 宏。

**实现**：在 `ui_context.h` 中保留宏定义但标注 `@deprecated`，`xerintosh_mark_dirty()` 保留为别名实现。

## 测试

- 新增测试：无（纯重构，行为不变）
- 验证结果：
  - `pio test -e native`：✅ 414/415 pass, 1 skipped
  - `pio run -e m5stick-c`：✅ SUCCESS
  - RAM：25.5%（无变化）

## 检查清单

- [x] 所有导出函数有模块前缀（`xerintosh_`）
- [x] 头文件有 `extern "C"` 保护
- [x] 头文件有 include guard
- [x] 没有 `nullptr`、`&` 引用出现在 C 接口中
- [x] 文档已同步更新
- [x] 硬件构建无新增警告
- [x] Native 测试全量通过

## 回滚点

- 回滚 commit：`HEAD`
- 回滚命令：`git revert HEAD`

## 遗留问题

| ID | 问题 | 后续处理 |
|----|------|----------|
| — | — | 本轮无遗留 |
