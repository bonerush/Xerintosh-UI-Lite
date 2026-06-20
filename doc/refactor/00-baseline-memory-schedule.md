# 重构基线报告：memory-schedule

## 分支与 Commit

- 分支：`refactor/2026-06-19-memory-schedule`
- 起始 commit：`c6757e3`
- 工作区：`.worktrees/refactor-2026-06-19-memory-schedule`

## 构建基线

| 目标 | 命令 | 结果 | 说明 |
|------|------|------|------|
| 硬件构建 | `pio run -e m5stick-c` | ❌ FAIL | 网络问题：无法下载 `m5stack/M5Unified @ ^0.2.14`（SSL EOF / read timeout） |
| Native 测试 | `pio test -e native` | ⚠️ PARTIAL | `test_native` 环境崩溃（SIGTRAP），其余两个环境通过；230/231 条用例成功 |

> 硬件构建失败为环境/网络问题，非代码回归。后续在可联网环境或缓存命中时需重新验证。

## Native 测试失败清单（基线现状）

| 测试 | 失败数 | 关键错误 |
|------|--------|----------|
| `KernelVFSTest.MaxFdPerTask` | 5 | `fd` 返回 `-24`（KERN_EMFILE），超出每任务 FD 上限 |
| `KernelVFSTest.FdNamespaceIsolated` | 5 | 同上，跨任务 FD 命名空间隔离用例 |
| `ShellCompleteTest.CompleteRelativeFromRoot` 等 | 7 | `kern_dentry_register` 返回 `-2`（KERN_ENOENT） |
| `SpringAnimTest.OvershootsWithLowDamping` | 1 | 欠阻尼弹簧未过冲 |
| `test_native` 整体 | - | 运行末段 `SIGTRAP` 崩溃（输出被截断，未显示最后一个失败用例） |

> 以上失败在起始 commit `c6757e3` 已存在，属于本轮重构前的基线问题，需在重构后确认无新增失败。

## 代码规模

`cloc` 未安装，使用 `find` + `wc` 统计：

| 类别 | 文件数 | 行数 |
|------|--------|------|
| C + 头文件 | 约 180 | 22,517 |
| C++ 文件 | 约 20 | 5,682 |
| Markdown 文档 | 122 | 21,845 |
| 总计（src + doc） | 304 | 50,044 |

## 已知问题（TODO/FIXME 扫描）

| ID | 文件 | 行号 | 内容 | 优先级 |
|----|------|------|------|--------|
| T1 | `src/kernel/kern_init.c` | 121 | `/* TODO: 硬件 LED 闪烁 */` | P2 |
| T2 | `src/kernel/kern_port_native.c` | 198 | `/* TODO: 使用 esp_timer 或简单的忙等待 */` | P2 |
| T3 | `src/kernel/devices/dev_ttyS0.cpp` | 20 | `/* TODO(phase 2.4): 迁移到 app/flasher/flasher.h 声明 */` | P1 |

## 本次重构范围

- [x] 内核层：内存分配（kmalloc / 任务栈）
- [x] 内核层：调度机制（RR / FIFO / 时间片 / 优先级）
- [ ] HAL 层：本次不涉及
- [ ] UI 核心层：本次不涉及
- [ ] App 层：仅在诊断阶段分析 WiFi/BT 内存使用，不做 App 层改动
- [x] 文档体系：`doc/kernel/` 内存与调度相关文档同步

## 关键源码速览

| 文件 | 职责 | 与本次重构关系 |
|------|------|----------------|
| `src/kernel/kern_kmalloc.c/h` | 内核统一分配器（header + 任务追踪） | 可能需增加内存统计/碎片缓解 |
| `src/kernel/kern_task_stack.c` | 任务栈分配、canary、使用率查询 | 重点：ESP32 FreeRTOS 栈由底层管理，Xeros 只记录大小 |
| `src/kernel/kern_task_lifecycle.c` | `kern_spawn`：栈大小选择 `stack_min > 0 ? stack_min : KERN_STACK_MIN/KERN_PORT_STACK_MIN` | 重点：按需分配入口 |
| `src/kernel/kern_sched.c` | 调度器初始化、tick、idle | 可能需增加运行时栈监控 |
| `src/kernel/kern_sched_rr.c` | RR 时间片调度 | 可能需按任务需求动态调整时间片 |
| `src/kernel/kern_sched_fifo.c` | 优先级 FIFO 调度 | 抢占边界已清晰 |
| `src/kernel/kern_sched_class.c/h` | 可插拔调度类接口 | 接口稳定，可能扩展 `mem_pressure` 回调 |
| `src/kernel/kern_types.h` | `KERN_STACK_MIN/MAX/GROW`、`KERN_MAX_TASKS` | 常量可能需调整 |
