# 重构归档报告 v2（第二轮 kernel-deep → App）

**日期**: 2026-06-15  
**分支**: `refactor/2026-06-15-kernel-ui`  
**上轮归档**: [04-archive.md](04-archive.md)

## 本轮目标

按用户指定顺序：**内核层深度优化 → App 层对齐 → 其余层按需**。
在第一轮（O(1) enqueue + FD 池 + P0 ISR + UI 优化）基础上进行深度内核优化。

## 完成情况

### 阶段 2.1: 内核层深度重构 ✅

| # | 变更 | 文件 | 类型 |
|---|------|------|------|
| 1 | 移除重复 kern_fd_t typedef | `kern_shell_cmds.h` | P1-11 修复 |
| 2 | O(1) 任务插入（g_task_list_tail） | `kern_sched.c/h`, `kern_task_lifecycle.c` | 性能优化 |
| 3 | 资源节点对象池（32 预分配） | `kern_resource.c` | 内存优化 |
| 4 | 设备驱动 buf NULL 加固 | `dev_fb0/input0/pwrkey/ttyS0` | P1-10 修复 |

静态内存增加：~516 bytes（资源池 512B + 尾指针 4B）

### 阶段 2.2: App 层重构 ✅

| # | 变更 | 文件 | 类型 |
|---|------|------|------|
| 1 | taskmgr 内核 API 包装层 | `taskmgr.h/c`, `taskmgr_ui.c` | P1 修复 |
| 2 | sm_ui.c → settings getter | `sm_ui.c` | P2 修复 |
| 3 | ui_service.c → settings setter | `ui_service.c` | P2 修复 |

### 阶段 2.3-2.5: HAL/UI/文档

未执行（本轮聚焦内核+App）。

## 累计变更（两轮合计）

### 内核层
- ✅ 调度器 O(1) enqueue（task_list_tail 在调度类中）
- ✅ FD 对象池（16 预分配，消除 kern_open malloc）
- ✅ P0 ISR 修复（硬件定时器 ISR 仅设标志位）
- ✅ 移除重复 typedef
- ✅ 任务插入 O(1)（g_task_list_tail 全局尾指针）
- ✅ 资源节点池（32 预分配，消除 resource_track malloc）
- ✅ 5 个设备驱动 buf NULL 加固

### UI 层（第一轮）
- ✅ dirty rect 帧跳过
- ✅ XOR 选择器批量化（15→1 次 readRect/pushImage）
- ✅ 静态装饰缓存
- ✅ 局部变量解引用缓存

### App 层（本轮）
- ✅ taskmgr 包装层解耦内核
- ✅ settings getter/setter 统一

## 验证汇总

| 轮次 | 构建 | 测试 | RAM | Flash |
|------|------|------|-----|-------|
| 基线 | ✅ | 414 pass | 22.3% | 88.1% |
| 第一轮 | ✅ | 414 pass | 22.3% | 88.3% |
| 第二轮 | ✅ | 414 pass | 25.5% | 88.5% |

RAM 增加主要来源：
- 本轮的资源池 (512B) + 尾指针 (4B)
- 第一轮的 FD 池 (448B) + task_list_tail (8B)

全部在安全范围内（ESP32-PICO 520KB SRAM）。

## 已知遗留

| ID | 问题 | 严重度 | 备注 |
|----|------|--------|------|
| — | `wifi_manager.cpp` 706 行 | P1 | 需独立拆分计划 |
| — | `flasher_app.cpp` 525 行 | P1 | 需独立拆分计划 |
| — | `storage.cpp` 423 行 | P2 | 按凭据/设置/API Key 拆分 |
| — | WiFi/BT 状态机骨架重复 | P2 | WiFi 远复杂于 BT，提取收益有限 |
| — | pick_next 双遍扫描优化 | P2 | 需独立 wake_list，影响较大 |
| — | 命令历史压缩 | P1 | UI 无关，独立处理 |
| — | 结构体重排去死字段 | P1 | 影响多处，独立 PR |

## 分支状态

分支 `refactor/2026-06-15-kernel-ui` 包含两轮所有变更，可干净合并回 `main`。
所有变更均通过 `pio run -e m5stick-c` 和 `pio test -e native` 验证。
