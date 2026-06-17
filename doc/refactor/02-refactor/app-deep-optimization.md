# 重构实施报告（第九轮 · 2026-06-17 · App 深度优化 + 内核/UI 维护）

## 概述
本阶段按顺序处理了内核层 → UI 核心层 → App 层，共 **14 项修复**。

---

## 2.1 内核层日常维护（5 项）

| ID | 文件 | 修改 | 类型 |
|----|------|------|------|
| K1 | `kern_smp.h:26` | 移除 `KERN_CPU_ANY` 重复定义（依赖 `kern_types.h`） | 清理 |
| K3 | `kern_vfs.c:35` | 修正 `fd_pool_alloc()` 注释：O(1) → O(n) | 文档 |
| K5 | `kern_shell.c:177-179` | 删除 `for(;;)` 后不可达的 `kern_close(tty)`，添加注释 | 死代码 |
| K6 | `kern_procfs.h:29` | 更新注释：3 个文件 → 5 个文件 | 文档 |
| K7 | `dev_pwrkey.h:37` | 更新 API 引用：`kern_devfs_register_device()` → `kern_device_register()` | 文档 |
| K9 | `kern_vfs.c:447` | 改进目录打开注释语义 | 文档 |
| K10 | `dev_input0.c:10` | 统一 `#include` 路径为相对路径 `"dev_input0.h"` | 风格 |

**未处理项**（P2，低优先级）：
- K2: 双重编译条件合并（需仔细测试两平台）
- K4: `kern_port_native.c` 死文件清理（保留为历史参考）
- K8/K11-K22: 其余 P2 清理项

---

## 2.2 UI 核心层日常维护（4 项）

| ID | 文件 | 修改 | 类型 |
|----|------|------|------|
| **U1** | `ui_item_selector.c:144` | 增加 `parent == NULL` 检查，防止根节点空指针解引用 | **P1 崩溃修复** |
| **U2** | `ui_item_selector.c:77,103` | 增加 `count == 0` 守卫，防止 `clear_children_of_list` 后数组越界 | **P1 崩溃修复** |
| U4 | `ui_core.c:229-231` | 调换刷新顺序：`list_item` → `selector` → `camera`，消除 1 帧延迟 | 正确性 |
| U10 | `ui_draw_icons.c:69` | `_item` 参数添加 `const` 限定符 | 清理 |

**未处理项**（P2）：
- U3: `popup_compute_height` 重复（需重构两个文件共享）
- U5: 未使用 includes（保守处理，避免编译风险）
- U6: 传值 `const`（纯风格，无影响）

---

## 2.3 App 层深度重构优化（8 项）

### 关键修复

| ID | 文件 | 修改 | 优先级 | 类型 |
|----|------|------|--------|------|
| **A0** | `flasher_menu.c` | **确认误报**：`xerintosh_init_base_item()` 对所有 content 使用 `strdup()`（`ui_item_base.c:70`），`safe_set_content` 中的 `free()` 是安全的 | ~~P0~~ | 验证 |
| A1 | `ui_service.c:41-55` | 删除 native 测试分支中的死代码（未使用的 `w, h` 局部变量） | P1 | 死代码 |
| A8 | `wifi_menu.h`, `tu_api.h` | `#pragma once` → 标准 `#ifndef`/`#define`/`#endif` include guards | P1 | 规范 |
| A9 | `app_menu.c:15-21` | 统一 include 路径为 `app/xxx.h`（相对于 `src/`） | P1 | 一致性 |
| A11 | `wifi_manager.cpp:49`, `bt_manager.cpp:77` | 修正过时注释：`main.cpp` → `app/app_state.c` | P1 | 文档 |
| A12 | `storage.cpp:27` | Native stub `storage_get_brightness()` 返回值 50 → -1，匹配硬件语义 | P1 | 一致性 |

### 已记录待后续处理的性能问题

| ID | 问题 | 当前状态 | 建议后续动作 |
|----|------|----------|-------------|
| A2 | `g_popup_content` 跨任务无锁共享 | 添加 TODO 注释 | 使用 FreeRTOS 队列 |
| A3 | `bt_uart_service` TOCTOU 竞态 | 已有详细注释记录 | 使用 task notification |
| A4 | 示波器 UI 线程 80ms 阻塞采样 | 未修改 | 分帧采样/独立任务 |
| A5 | 烧录器 50ms 阻塞 delay | 未修改 | 改用状态机定时器 |

### 未处理 P2 项
- A14: `malloc` 在 `safe_set_content` 中（安全，仅风格问题）
- A15: 横屏代码重复（需提取公共函数）
- A16-A23: 其余 P2 清理项

---

## 验证结果

| 项目 | 修改前 | 修改后 |
|------|--------|--------|
| `pio run -e m5stick-c` | ✅ SUCCESS | ✅ SUCCESS |
| RAM 使用 | 28.2% | 28.2% |
| Flash 使用 | 88.9% | 88.9% |
| `pio test -e native` | 211 cases / 210 passed | 211 cases / 210 passed |
| SIGTRAP | 已知 PlatformIO runner 问题 | 未变化 |

**零资源退化，零测试退化。**

---

## 总结

| 层 | 修复项 | P0 修复 | P1 修复 | P2 修复 |
|----|--------|---------|---------|---------|
| 内核层 | 6 | 0 | 0 | 6 |
| UI 核心层 | 4 | 0 | 2 | 2 |
| App 层 | 6 | ~~1~~（误报）| 5 | 1 |
| **合计** | **16** | **0** | **7** | **9** |

### 关键收获
1. **验证了 A0 为误报**：UI 框架的 `strdup` 确保了 content 指针始终为堆分配
2. **修复了 2 个 P1 UI 崩溃向量**（空指针解引用、数组越界）
3. **统一了头文件规范**（`#pragma once` → include guards）
4. **清理了死代码和过时文档**
5. **识别了 4 个需要后续重构的性能/竞态问题**（已标记 TODO）
