# 诊断报告（第九轮 · 2026-06-17 · App 深度优化 + 内核/UI 维护）

## 方法
- 静态扫描范围：`src/` 全栈
- 扫描日期：2026-06-17
- 方法：3 个并行 explore agent（App/内核/UI）+ 手动深度阅读

## 优先级定义
- **P0**：会导致崩溃、内存损坏（堆/栈）、数据丢失
- **P1**：功能正确但可维护性差、性能隐患、竞态条件、接口不一致
- **P2**：风格、文档、注释、代码清理

---

## App 层问题清单（重点）

### 🔥 P0 — 崩溃/内存损坏

| ID | 模块 | 文件:行 | 问题 | 建议动作 |
|----|------|---------|------|----------|
| ~~A0~~ | 烧录器菜单 | `flasher_menu.c:71` | ~~`free()` 释放静态内存~~ → **误报**：`xerintosh_init_base_item()`（`ui_item_base.c:70`）对所有 content 使用 `strdup()`，content 始终为堆分配，`safe_set_content` 中的 `free()` 是安全的 | 无需修复，添加注释说明 |
| A1 | UI-服务 | `ui_service.c:42-48,58-65` | Native 测试路径声明未使用的局部变量 `w, h`，屏幕尺寸全局变量不更新 | 统一两路径或移除非必要分支 |

### 🔴 P1 — 正确性/可维护性/性能

| ID | 模块 | 文件:行 | 问题 | 建议动作 |
|----|------|---------|------|----------|
| A2 | WiFi 弹窗 | `wifi_manager.cpp:96` | **跨任务共享 `g_popup_content[48]` 无锁保护**：内核任务 `wifi_popup_request()` 写入，UI 任务 `wifi_popup_refresh()` 读取，`strncpy` 非原子操作 → 乱码风险 | 使用队列传递消息或添加互斥锁 |
| A3 | 蓝牙 | `bt_uart_service.cpp:278` | **deinit 时序 TOCTOU 竞态**：先设 `g_initialized = false`，再 `delay(100)` 等 poll 退出。poll 可能在检查点后、`xQueueSend` 前被抢占 → 访问已删除队列 | 用信号量/task notification 同步 poll 退出 |
| A4 | 示波器 | `oscilloscope_app.c:348-353` | **UI 线程阻塞采样 ~80ms**（2000 样本 × 40µs），期间不处理输入、不渲染 | 分帧采样或移到独立任务 |
| A5 | 烧录器 | `flasher_app.cpp:131,179` | **UI 线程 `hal_delay_ms(50)` 阻塞 DTR 脉冲**，参考已有的 `PT_PHASE_DTR_WAIT` 定时器模式改为非阻塞 | 用状态机定时器替代阻塞 delay |
| A6 | 烧录器 GPIO | `flasher_gpio.cpp:118,123` | `delay(100)` 100ms 阻塞 | 评估是否可减小延迟 |
| A7 | 示波器循环 | `ui_task.c:81` | 每帧 1ms delay 在 80ms+ 采样期间不会执行，idle 任务被饿死 | 在长操作中插入 `kern_yield()` |
| A8 | 头文件规范 | `wifi_menu.h:10`, `tu_api.h:1` | 使用 `#pragma once` 违反项目 include guard 规范 | 改为 `#ifndef`/`#define`/`#endif` |
| A9 | Include 路径 | `app_menu.c:22-26` | 同一文件中混合 `"app_state.h"` 和 `"app/token_usage/token_usage.h"` 两种风格 | 统一为相对于 `src/` 的路径 |
| A10 | App 公共 | 各 App loop | 退出检查不一致：`about.c`/`oscilloscope` 用 `ui_service_user_item_loop()`，其他 App 直接用 `ui_user_item_try_exit()` | 统一为直接调用 `ui_user_item_try_exit()` |
| A11 | WiFi 注释 | `wifi_manager.cpp:49` | 注释说 `g_wifi_on` 定义在 `main.cpp`，实际在 `app_state.c` | 修正过时注释 |
| A12 | 存储 | `storage.cpp:270-276` | `storage_get_brightness()` 硬件端 key 不存在返回 -1，native 桩返回 50，默认值语义不一致 | 统一 native 桩返回值 |
| A13 | 关机 | `shutdown_screen.c:44` | `hal_delay_ms(2000)` 在 UI 任务中阻塞 2 秒（关机路径可接受但缺注释） | 添加注释说明安全性 |

### 🟡 P2 — 代码质量/清理

| ID | 模块 | 文件:行 | 问题 | 建议动作 |
|----|------|---------|------|----------|
| A14 | 烧录器菜单 | `flasher_menu.c:68` | `malloc`/`free` 动态分配，嵌入式不推荐 | 如 A0 修复后可能不再需要 |
| A15 | App 公共 | `ui_service.c:35-68` | 进出横屏代码高度重复（两处几乎相同的 `#ifndef NATIVE_TEST`/`#else` 块） | 提取公共函数 |
| A16 | 关机弹窗 | `power_key_popup.c:77` | `static char msg[32]` 不需要 static（`push_pop_up` 会复制） | 改为局部数组 |
| A17 | 串口监视器 | `sm_ui.c:59-60` | `hal_get_string_width()` 热路径中重复计算 | 缓存到局部变量 |
| A18 | 示波器 UI | `oscilloscope_ui.c:206-210` | 每帧 `hal_get_string_width` 计算 4 个固定字符串 | 缓存常值 |
| A19 | 任务管理器 UI | `taskmgr_ui.c:95-97` | 标题字符串宽度每帧重复计算 | 缓存 |
| A20 | 串口监视器 | `sm_app.cpp` / `serial_input.cpp` | `serial_monitor_update` 与 `serial_poll` 潜在竞争 Serial 读取 | 集中 Serial 读取到单一任务 |
| A21 | 存储 | `storage.cpp:18` | Native 桩 `storage_get_brightness()` 返回 50（旧格式值），触发兼容性路径 | 返回新格式默认值 5 |
| A22 | 串口监视器 | `sm_app.cpp:207-212` | `s_prev_ble_active` 仅用于一次性调试日志 | 删除或改为 `#if` 守卫 |
| A23 | 关于 | `about.c:98` | `ui_service_user_item_loop()` 仅是 `ui_user_item_try_exit()` 的包装，冗余间接调用 | 直接调用 `ui_user_item_try_exit()` |

---

## 内核层问题清单（日常维护）

全部为 P2 级别。

| ID | 文件:行 | 问题 | 建议动作 |
|----|---------|------|----------|
| K1 | `kern_types.h:50` + `kern_smp.h:26` | `KERN_CPU_ANY` 重复定义 | 移除 `kern_smp.h` 中的定义 |
| K2 | `kern_task_stack.c:68-124` | 两分支代码完全重复 | 合并条件 |
| K3 | `kern_vfs.c:35` | `fd_pool_alloc()` 注释说 O(1) 实际 O(n) | 使用 `__builtin_ctz` 或修正注释 |
| K4 | `kern_port_native.c` | 文件被条件编译完全排除，永不编译 | 清理或保留为参考并加注释 |
| K5 | `kern_shell.c:178` | `kern_close(tty)` 在 `for(;;)` 后不可达 | 删除死代码 |
| K6 | `kern_procfs.h:29` | 注释说 3 个文件实际注册 5 个 | 更新注释 |
| K7 | `dev_pwrkey.h:37` | 注释引用已删除的 API | 更新为 `kern_device_register()` |
| K8 | `kern_sysfs.c:171-175` | 重复对静态变量赋初始值 | 删除或加注释 |

---

## UI 核心层问题清单（日常维护）

| ID | 文件:行 | 问题 | 优先级 | 建议动作 |
|----|---------|------|--------|----------|
| **U1** | `ui_item_selector.c:144` | `selected_item->parent` 可能 NULL，直接解引用 | **P1** | 增加 NULL 检查 |
| **U2** | `ui_item_selector.c:66-87,95-117` | `child_num == 0` 时数组越界 | **P1** | 增加 `count == 0` 守卫 |
| U3 | `ui_item_popup.c:86` + `ui_draw_widgets.c:76` | `popup_compute_height` 重复定义 | P2 | 提取到公共位置 |
| U4 | `ui_core.c:229-233` | 相机/选择器刷新顺序导致 1 帧延迟 | P2 | 调换顺序 |
| U5 | 6 个 UI 文件 | 未使用的 `#include` | P2 | 删除多余包含 |
| U6 | `ui_draw_icons.c:69` | `_item` 参数应为 `const` | P2 | 添加 const |

---

## 本轮重构排期

| 阶段 | 模块 | 处理问题 | 重点 |
|------|------|----------|------|
| 2.1 | 内核层 | K1-K8 | 日常清理：修注释、去重、删死代码 |
| 2.2 | UI 核心层 | U1-U6 | 修复 2 个 P1 崩溃风险 + 日常清理 |
| 2.3 | App 层 | A0-A23 | **修复 P0 heap corruption + P1 竞态/性能 + P2 清理** |

---

## 统计数据

| 层 | P0 | P1 | P2 | 合计 |
|----|----|----|----|------|
| App 层 | **1** | **12** | **10** | **23** |
| 内核层 | 0 | 0 | 8 | 8 |
| UI 核心层 | 0 | 2 | 4 | 6 |
| **总计** | **1** | **14** | **22** | **37** |
