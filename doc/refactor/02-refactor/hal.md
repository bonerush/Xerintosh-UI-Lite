# HAL 层重构报告

## 范围

- 处理诊断问题：H-P1-01、H-P2-01
- 延后问题：H-P1-02

## 变更摘要

| 变更类型 | 数量 | 说明 |
|----------|------|------|
| 修改文件 | 2 | HAL 源码与文档 |
| 新增文件 | 1 | `test_hal_system.cpp` |
| 新增 helper | 1 | `hal_display_create_sprite()` |
| 行为修复 | 1 | Native `hal_delay_ms()` 实现真实延时 |
| 新增测试 | 3 | HAL display / system |

## 详细变更

### 1. T1：封装 M5Canvas 色深 + 创建精灵顺序（H-P1-01）
**原因**：H-P1-01  
**实现**：
- 在 `src/hal/hal_display_fb.cpp` 中新增 `static hal_display_create_sprite()` helper。
- `hal_display_init()` 中原有的 `setColorDepth(8)` + `createSprite()` 替换为 helper 调用。
- 更新 `doc/hal/display.md` 关键顺序说明。
**影响接口**：无 public API 变化（helper 为 static）。  
**文档更新**：`doc/hal/display.md` 已更新。

### 2. T2：Native `hal_delay_ms()` 实现真实延时（H-P2-01）
**原因**：H-P2-01  
**实现**：
- `src/hal/hal_system.cpp` 的 `NATIVE_TEST` 分支中，`hal_delay_ms()` 改为 `std::this_thread::sleep_for(std::chrono::milliseconds(ms))`。
- 新增 `test/test_native/test_hal_system.cpp` 覆盖 50ms 延时和零值安全。
- 更新 `doc/hal/system.md` 删除“空实现”表述。
**影响接口**：无 public API 变化。  
**文档更新**：`doc/hal/system.md` 已更新。

## 测试

- 新增/修改测试：
  - `test/test_native/test_hal_display.cpp`：追加 `HalDisplayInit.InitDoesNotCrash`
  - `test/test_native/test_hal_system.cpp`：新建，含 `DelayAtLeast50ms`、`DelayZeroDoesNotCrash`
- 验证结果：
  - `pio test -e native`：✅ PASS（412 个测试用例，1 个 skipped，411 个 succeeded）
  - `pio run -e m5stick-c`：✅ PASS（Flash 88.0%，RAM 22.3%）

## 检查清单

- [x] 所有导出函数有模块前缀
- [x] 头文件有 `extern "C"` 保护
- [x] 头文件有 include guard
- [x] 结构体继承时基类放第一位（无新增继承）
- [x] 类型转换有安全检查
- [x] 回调统一带 `user_data`（无新增回调）
- [x] 没有 `nullptr`、`&` 引用出现在 C 接口中
- [x] 文档已同步更新
- [x] 新增/修改代码有 native 测试覆盖
- [x] 硬件构建无新增警告

## 回滚点

- 每个子任务均为独立 commit，可单独 `git revert <commit>`。
- 统一回滚到阶段 2.3 开始前：`git reset --hard 598f7a3`（APP-06 提交）。

## 遗留问题

| ID | 问题 | 后续处理 |
|----|------|----------|
| H-P1-02 | 输入事件模型统一（简单状态机与双击状态机共享 `btn_state.dc`） | 延后到专门输入子系统轮次，需硬件验证 |
