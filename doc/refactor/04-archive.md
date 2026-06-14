# 重构归档报告

## 归档信息

- **工作树**：`/Users/yukisala/Documents/PlatformIO/Projects/M5Stick-P1/.worktrees/refactor-2026-06-14-kernel-first`
- **分支**：`refactor/2026-06-14-kernel-first`
- **基线 commit**：`a4d8bab2703908ff0c4934daf97c1bced671eb40`
- **归档日期**：2026-06-14

## 本轮重构目标

按用户要求，本轮优先集中优化内核层，其次优化依赖内核的上层 App，最后处理 HAL/UI/文档等其他部分。

## 各阶段产出

| 阶段 | 状态 | 产物 |
|------|------|------|
| 0 基线建立 | ✅ DONE | `00-baseline.md` |
| 1 扫描与诊断 | ✅ DONE | `01-diagnosis.md` |
| 2.1 内核层重构 | ✅ DONE | `02-refactor/kernel.md`、`02-refactor/kernel-plan.md` |
| 2.2 App 上层重构 | ✅ DONE | `02-refactor/app.md`、`02-refactor/app-plan.md` |
| 2.3 HAL 层重构 | ✅ DONE | `02-refactor/hal.md`、`02-refactor/hal-plan.md` |
| 2.4 UI 核心层重构 | ✅ DONE | `02-refactor/ui.md`、`02-refactor/ui-plan.md`、`02-refactor/ui-popup-deferred.md` |
| 2.5 文档体系同步 | ✅ DONE | `02-refactor/docs.md` |
| 3 集成验证 | ✅ DONE | `03-integration.md` |
| 4 文档归档 | ✅ DONE | `04-archive.md` |

## 关键决策记录

1. **FD 表改为 per-task**：将全局 `g_fd_table` 移入 `kern_task_t`，解决跨任务 FD 共享问题，同时避免任务 A 关闭任务 B 的 FD。
2. **资源节点使用 untracked 分配器**：`kern_resource.c` 的节点本身通过 `kern_kmalloc_untracked` 分配，避免 `kern_kmalloc` → `kern_resource_track` 递归。
3. **Native 任务栈归属任务自身**：`kern_task_stack.c` 使用 `kern_kmalloc_for_task`，任务退出时通过资源链表自动释放栈。
4. **inode 引用计数**：`kern_vfs_unlink` 不再直接释放 inode，打开中的文件在 unlink 后仍可读写。
5. **调度类 ID 同步**：`scheduler_class_id` 在 enqueue/dequeue 时同步，为后续调度器扩展做准备。
6. **错误码统一**：调度类、procfs、设备 ops 均返回 `kern_err_t`。
7. **BT 生命周期助手**：重建 `svc_mgr_helper`，避免 UI 任务直接同步调用 `bt_mgr_disable()`。
8. **横屏 helper 提取**：`ui_service_enter_landscape` / `ui_service_exit_landscape` 供 serial_monitor 和 flasher 复用。

## 测试与构建

- `pio test -e native`：415 个测试用例，414 通过，1 跳过。
- `pio run -e m5stick-c`：SUCCESS，Flash 88.0%，RAM 22.3%。

## 变更范围

- 80 个文件变更
- +3,661 / -978 行
- 新增 40+ native 测试用例

## 遗留问题

| ID | 问题 | 建议后续处理 |
|----|------|--------------|
| K-P1-02 | `path_walk()` 不支持 `.` / `..` | 单独 VFS 功能增强轮次 |
| K-P1-04 | `kmalloc_header_t` 未显式对齐 | 内存子系统优化轮次 |
| K-P1-05 | `kern_krealloc()` 非原子语义 | 内存子系统优化轮次 |
| K-P1-06 | FIFO 调度抢占语义 | 调度器专门轮次 |
| K-P2-01 | `kern_shell_cmds.c` 文件过长 | Shell 子系统拆分轮次 |
| K-P2-02 | sysfs 路径不一致 | 文件系统接口统一轮次 |
| K-P2-04 | `minprintf` `%zd` 支持 | 格式化库增强轮次 |
| A-P1-02 | `wifi_manager.cpp` 过长 | WiFi 状态机重构轮次 |
| A-P1-03 | WiFi/BT 状态机重复 | 提取公共异步服务生命周期模板 |
| A-P1-04 | taskmgr 硬编码任务名 | 任务退出回调或服务禁用机制设计 |
| H-P1-02 | 输入事件模型统一 | 输入子系统专门轮次，需硬件验证 |
| H-P2-04 | `xerintosh_push_pop_up()` 拆分 | Pop-up 专门轮次，拆分方案已记录 |

## 后续建议

1. 合并 `refactor/2026-06-14-kernel-first` 到 `main`。
2. 在硬件上烧录验证关键用户路径（菜单、串口监视器、烧录器、BLE、Token Usage、设置）。
3. 根据遗留问题优先级安排下一轮重构。
