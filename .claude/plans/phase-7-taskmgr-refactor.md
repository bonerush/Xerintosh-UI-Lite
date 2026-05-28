# Phase 7: 任务管理器重构计划

> 日期: 2026-05-28
> 状态: 计划阶段

## 问题诊断

### Bug 1: 进入即卡死

可能原因（无法在 native 复现，需要硬件调试）：

| 可能原因 | 概率 | 说明 |
|---------|------|------|
| 冗余 `hal_display_flush()` + `hal_display_clear()` 与框架冲突 | 高 | taskmgr_loop 内部调用了 clear/flush，而框架也会在外部调用。双缓冲翻转可能使 M5Canvas 状态异常 |
| 双重 `hal_input_update()` → 按键事件队列溢出 | 中 | app_input_process 已调用一次，taskmgr 又调用一次 |
| `kern_task_stack_usage()` 对某些任务返回异常值 | 低 | 返回 0，应无问题 |
| `hal_draw_fill_rect` 全屏绘制过于频繁 | 低 | 每帧绘制选中行的高亮背景 |

### Bug 2: 保护列表不匹配

`kern_task_is_protected()` 检查英文名 "taskmgr"，但虚任务注册时使用的名称是中文 "任务管理器"。导致任务管理器可以 kill 自身。

### Bug 3: 代码风格不一致

taskmgr 不同于 serial_monitor 的清晰模式：`init → loop(读事件→逻辑→draw) → exit`，taskmgr 将 clear/flush/update 混入业务逻辑。

---

## 重构方案

完全参照 `serial_monitor` 的模式重写：

### 文件结构

```
src/app/taskmgr/
├── taskmgr.h         ← 生命周期声明（不变）
├── taskmgr_app.c     ← 状态管理 + 生命周期（原 taskmgr.c 的核心逻辑）
└── taskmgr_ui.c      ← 纯渲染（标题栏 + 列表 + 弹窗 + 信息栏）
```

### 生命周期对齐

| 函数 | serial_monitor | taskmgr（重构后） |
|------|---------------|-------------------|
| `init` | 初始化状态、重置事件、设置双击 | 初始化选择态、刷新列表、重置事件 |
| `loop` | 读事件 → 逻辑 → draw() | 读事件 → 逻辑 → draw() |
| `exit` | 清理缓冲区、恢复方向、重置事件 | 重置确认态、重置事件 |
| `draw` | draw_info_bar() + draw_terminal() | draw_header() + draw_list() + draw_confirm() + draw_footer() |

### 关键变更

1. **移除冗余 HAL 调用**：loop 中不调 `hal_input_update()` / `hal_display_clear()` / `hal_display_flush()`
2. **修复保护列表**：将中文名 "任务管理器" 加入 `protected_names`
3. **确认弹窗改用 `xerintosh_push_pop_up()`**：与框架一致的 UI 体验
4. **减少全局状态变量**：统一到单个结构体
5. **分离渲染到 `taskmgr_ui.c`**：遵循 serial_monitor 的 sm_app/sm_ui 分离模式

### 文件变更

| 文件 | 操作 | 说明 |
|------|------|------|
| `src/app/taskmgr/taskmgr.h` | 不变 | 生命周期接口 |
| `src/app/taskmgr/taskmgr.c` → `taskmgr_app.c` | 重写 | 状态管理 + 输入处理 |
| `src/app/taskmgr/taskmgr_ui.c` | 新建 | 纯渲染函数 |
| `src/kernel/kern_task.c` | 修改 | 保护列表加 "任务管理器" |
