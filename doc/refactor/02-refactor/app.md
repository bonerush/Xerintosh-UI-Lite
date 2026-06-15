# App 层重构报告（第二轮 kernel-deep → App）

**日期**: 2026-06-15  
**分支**: `refactor/2026-06-15-kernel-ui`  
**范围**: `src/app/` — 对齐内核新 API，消除代码异味

## 已修复问题

### P1-1: taskmgr_ui.c 直调内核 API → 通过 taskmgr 包装层

**问题**: `taskmgr_ui.c` 直接 `#include "kernel/kern_task.h"`，在渲染函数中调用
`kern_task_is_protected()` 和 `kern_task_stack_usage()`，违反分层原则。

**修复** (`src/app/taskmgr/taskmgr.h:50-52`, `taskmgr_app.c:169-187`):
- 新增 3 个包装函数：
  ```c
  bool   taskmgr_task_protected(const kern_task_t *task);
  size_t taskmgr_task_stack_usage(const kern_task_t *task);
  bool   taskmgr_task_is_virtual(const kern_task_t *task);
  ```
- `taskmgr_ui.c` 移除 `#include "kernel/kern_task.h"`，改用包装函数

### P2-1: sm_ui.c 直读全局变量 → 使用 settings getter

**问题**: `sm_ui.c:41` 直接读取 `g_serial_baud_rate` 而非通过 `settings_get_baud_rate()`

**修复** (`src/app/serial_monitor/sm_ui.c:41`):
```c
// Before
int32_t baud = settings_serial_baud_hw_value(g_serial_baud_rate);
// After
int32_t baud = settings_serial_baud_hw_value(settings_get_baud_rate());
```

### P2-2: ui_service.c 直写全局变量 → 使用 settings setter

**问题**: `ui_service.c` 直接读写 `g_is_landscape` 和 `g_screen_rotation_level`

**修复** (`src/app/ui_service.c:37-69`):
```c
// Before
s_prev_landscape = g_is_landscape;
g_is_landscape = true;
g_screen_rotation_level = ORIENTATION_LANDSCAPE;

// After
s_prev_landscape = settings_get_landscape();
settings_set_landscape(true);  // 同时设置 g_screen_rotation_level
```

## 未修复但已记录

| # | 问题 | 文件 | 原因 |
|---|------|------|------|
| 1 | `wifi_manager.cpp` ~706行 | 待未来拆分为 scan/menu/connect | 影响面大，需单独计划 |
| 2 | `flasher_app.cpp` ~525行 | 待未来拆分为 stk500/slip 协议 | 同上 |
| 3 | `storage.cpp` ~423行 | 待未来按凭据/设置/API Key 拆分 | 同上 |
| 4 | WiFi/BT 状态机重复 | 待提取公共骨架 | 当前 WiFi 复杂度远高 BT，收益有限 |
| 5 | `about.c` NATIVE_TEST 守卫不一致 | 留待未来 | 涉及 hal_input 桩行为，不改更安全 |
| 6 | `token_usage`/`flasher` 未用 ui_service | 留待未来 | flasher 生命周期复杂，需单独评估 |

## 验证结果

| 验证项 | 状态 |
|--------|------|
| `pio run -e m5stick-c` | ✅ SUCCESS |
| `pio test -e native` | ✅ 414/415 pass, 1 skipped |
| 编译警告 | ✅ 无新增警告 |
| RAM | 25.5%（+576B vs 上轮） |

## 变更文件清单

| 文件 | 变更类型 |
|------|---------|
| `src/app/taskmgr/taskmgr.h` | +3 API 声明 |
| `src/app/taskmgr/taskmgr_app.c` | +20行 包装实现 |
| `src/app/taskmgr/taskmgr_ui.c` | -1 include, +4 调用替换 |
| `src/app/serial_monitor/sm_ui.c` | 1 行 getter 替换 |
| `src/app/ui_service.c` | -2行, 用 settings API 替换 |
