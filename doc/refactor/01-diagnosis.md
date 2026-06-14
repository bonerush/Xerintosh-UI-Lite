# 重构诊断报告

**项目**：M5Stick-P1  
**工作树**：`/Users/yukisala/Documents/PlatformIO/Projects/M5Stick-P1/.worktrees/refactor-2026-06-14-kernel-first`  
**分支**：`refactor/2026-06-14-kernel-first`  
**扫描日期**：2026-06-14  
**扫描范围**：`src/kernel/`、`src/app/`、`src/hal/`、`src/ui/`、`doc/`

## 方法

- 静态阅读 + `Grep`/`Glob`/`Read` 工具逐层扫描。
- 重点关注：代码重复、命名一致性、函数/文件长度、C/C++ 边界、`malloc` 返回值检查、`kmalloc`/`kfree` 配对、FD/内存/锁释放顺序、`extern "C"` 与 include guard、文档结构同步。
- 基线：`pio run -e m5stick-c` ✅ / `pio test -e native` ✅（371/371 通过）。

## 优先级定义

- **P0**：会导致崩溃、内存泄漏、资源泄漏、硬件损坏。
- **P1**：功能正确但可维护性差、接口不一致、重复代码、潜在竞态。
- **P2**：风格、文档、测试缺失或轻微不一致。

## 问题清单

| ID | 模块 | 文件（行号） | 问题 | 优先级 | 建议重构动作 | 关联测试 |
|----|------|--------------|------|--------|--------------|----------|
| K-P0-01 | 内核-VFS | `src/kernel/kern_vfs.c:29-30`、`312-355` | 文件描述符表 `g_fd_table` 是全局单表，非每任务隔离；`kern_open()` 返回的 FD 直接写入全局表，跨任务共享槽位。 | P0 | 将 FD 表移入 `kern_task_t`，或实现每任务 FD 命名空间；`kern_close` 按任务释放。 | `test_kernel_vfs.cpp`、`test_kernel_resource.cpp` |
| K-P0-02 | 内核-资源追踪 | `src/kernel/kern_resource.c:24`、`57` | 资源追踪节点使用 `malloc`/`free` 分配，不走 `kern_kmalloc`/`kern_kfree`，任务退出时无法通过 `kern_resource_release_all` 自动回收节点本身。 | P0 | `kern_resource_track` 改用 `kern_kmalloc`；`kern_resource_release_all` 释放节点时走 `kern_kfree` 语义，或保持与任务生命周期一致。 | `test_kernel_resource.cpp` |
| K-P0-03 | 内核-任务栈 | `src/kernel/kern_task_stack.c:27`、`51` | Native / `XEROS_NATIVE_SCHED` 路径下任务栈使用 `malloc` 分配，未走 `kern_kmalloc`，任务退出时栈内存不纳入资源追踪。 | P0 | `task_stack_init()` 改用 `kern_kmalloc`；TCB 本身在 `kern_spawn()` 中也应统一分配策略。 | `test_kernel_stack.cpp`、`test_kernel_task.cpp` |
| K-P0-04 | 内核-VFS | `src/kernel/kern_vfs.c:184-215` | `kern_vfs_unlink()` 释放 dentry 时未释放关联的 `inode`，也不检查是否有打开引用，导致 inode 泄漏。 | P0 | 为 inode/dentry 引入引用计数；unlink 时引用计数归零才释放。 | `test_kernel_vfs.cpp` |
| K-P1-01 | 内核-类型系统 | `src/kernel/kern_sched_class.c:17-21`、`kern_procfs.c:229-253`、`kern_gpiofs.c:222-236`、`devices/dev_ttyS0.cpp:48-122` | 多处 API / 回调仍返回 `int` 而非 `kern_err_t`（如 `kern_sched_class_register`、`procfs_register_file`、设备 ops）。 | P1 | 统一返回类型为 `kern_err_t`；更新头文件声明。 | 全部 kernel native 测试 |
| K-P1-02 | 内核-VFS | `src/kernel/kern_vfs.c:40-106` | `path_walk()` 不处理 `.` / `..`，也不支持相对路径；遇到特殊分量直接按普通名查找。 | P1 | 实现 `.`/`..` 回退；路径解析前严格校验总长度。 | `test_kernel_vfs.cpp` |
| K-P1-03 | 内核-VFS | `src/kernel/kern_vfs.c:347-355` | `kern_open()` 在 `kern_resource_track()` 失败时未关闭已分配的 FD，导致 FD 泄漏。 | P1 | 失败分支调用 `kern_close(fd)` 并清空 `g_fd_table[fd]`。 | `test_kernel_vfs.cpp` |
| K-P1-04 | 内核-内存分配器 | `src/kernel/kern_kmalloc.c:25-28` | `kmalloc_header_t` 未显式对齐；在某些平台上用户数据指针可能不对齐，导致硬件异常或性能损失。 | P1 | 为头结构体增加 `__attribute__((aligned))` 或按最大对齐需求分配。 | `test_kernel_kmalloc.cpp` |
| K-P1-05 | 内核-内存分配器 | `src/kernel/kern_kmalloc.c:108-154` | `kern_krealloc()` 先 `kern_resource_untrack` 旧指针再 `realloc`；若 `realloc` 移动内存且后续 `kern_resource_track` 失败，旧数据指针已失效且未回滚。 | P1 | 重构为“先分配新块 → 迁移数据 → 再释放旧块”的原子语义。 | `test_kernel_kmalloc.cpp` |
| K-P1-06 | 内核-调度器 | `src/kernel/kern_sched_fifo.c:39-44` | `sched_fifo_enqueue()` 在任务刚被加入链表时检查 `task->state == KERN_TASK_READY` 触发抢占；若任务从阻塞态入队，状态尚未更新，可能错误抢占。 | P1 | 明确抢占语义：由调用者保证入队前状态已就绪，或把抢占判断移到 `pick_next`/`tick`。 | `test_kernel_sched.cpp` |
| K-P1-07 | 内核-资源追踪 | `src/kernel/kern_resource.c:15-86` | `kern_resource_track/untrack/release_all` 未对 `task->resource_head` 加锁；SMP 或多任务并发操作链表可能损坏。 | P1 | 增加 per-task 自旋锁 / 临界区保护，或复用 `g_task_list_lock`。 | `test_kernel_resource.cpp`、`test_kernel_sync.cpp` |
| K-P1-08 | 内核-调度类 | `src/kernel/kern_task.h:66`、`kern_task_lifecycle.c:46,116,172` | TCB 字段 `scheduler_class_id` 预留但未显式初始化，任务也未通过 `class->enqueue()` 注册，调度类与 TCB 关联缺失。 | P1 | 在 `kern_spawn()` 显式置 `scheduler_class_id = -1`；任务入队/出队时同步更新。 | `test_kernel_sched.cpp` |
| K-P2-01 | 内核-Shell | `src/kernel/kern_shell_cmds.c`（971 行） | 远超 400 行文件限制，包含 30+ 命令实现与命令表。 | P2 | 按命令类别拆分为 `kern_shell_cmd_*.c`。 | 编译全量检查 |
| K-P2-02 | 内核-sysfs | `src/kernel/kern_sysfs.c:194-198` | `log_level` 注册在 `/sys/kernel/`，其余属性注册在 `/sys/`，路径不一致。 | P2 | 统一所有 sysfs 属性到 `/sys/kernel/`。 | `test_kernel_sysfs.cpp` |
| K-P2-03 | 内核-RR 调度 | `src/kernel/kern_sched_rr.c:35` | `s_rr_last_prio` 声明后未使用。 | P2 | 删除死代码。 | `test_kernel_sched.cpp` |
| K-P2-04 | 内核-格式化 | `src/kernel/kern_minprintf.c:83-243` | `%zd` 等长度修饰符处理不完整。 | P2 | 补充 `z` / `l` / `ll` 修饰符解析。 | `test_kernel_init.cpp` |
| A-P1-01 | App-菜单构建 | `src/app/app_menu.c:49-106` | `app_menu_build()` 调用大量 `xerintosh_new_*_item()` 均未检查返回值；malloc 失败时继续 `xerintosh_push_item_to_list()`，导致空指针解引用。 | P1 | 每个创建点检查 NULL；失败时跳过或回滚已创建项。 | `test_ui_item.cpp` |
| A-P1-02 | App-WiFi | `src/app/wifi/wifi_manager.cpp`（706 行） | 文件过长；状态机包含启用、禁用、扫描、连接、自动连接、弹窗等，职责过重。 | P1 | 拆分为 `wifi_mgr_state.c`、`wifi_mgr_menu.c`、`wifi_mgr_connect.c`。 | 新增 `test_wifi_manager.cpp` |
| A-P1-03 | App-WiFi/BT | `src/app/wifi/wifi_manager.cpp:191-281`、`src/app/bluetooth/bt_manager.cpp:87-154` | enable/disable/request 异步状态机模式在 WiFi 与 BT 中重复实现，且 BT 管理器依赖 WiFi 管理器内部状态。 | P1 | 提取公共“异步服务生命周期”模板（`svc_mgr_helper`），统一 enable/disable/request/process 接口。 | `test_app_state.cpp` |
| A-P1-04 | App-任务管理器 | `src/app/taskmgr/taskmgr_app.c:184-189` | 通过 `strcmp(t->name, "wifi-mgr")` / `"bt-mgr"` 硬编码任务名来调用禁用逻辑，与 WiFi/BT 模块紧耦合。 | P1 | 引入任务退出回调注册机制，或在 taskmgr 中不直接操作具体服务。 | `test_kernel_task.cpp` |
| A-P1-05 | App-串口监视器/烧录器 | `src/app/serial_monitor/sm_app.cpp:114-121`、`src/app/flasher/flasher_app.cpp:372-379` | 两者都包含“进入时临时横屏、退出时恢复”的重复代码块。 | P1 | 提取公共 `app_force_landscape()` / `app_restore_landscape()` 到 `ui_service.c`。 | `test_serial_monitor.cpp`、`test_flasher.cpp` |
| A-P1-06 | App-串口监视器 | `src/app/serial_monitor/sm_app.cpp:161-164` | 串口监视器直接调用 `bt_mgr_request_enable()` 管理 BT 生命周期，绕过统一服务助手。 | P1 | 通过 `svc_mgr_helper` 提供的统一接口请求 BT/WiFi 切换。 | `test_serial_monitor.cpp` |
| A-P2-01 | App-设置 | `src/app/settings/settings.c:97-100` | `settings_set_rotation()` 未校验输入范围，可写入非法方向值。 | P2 | 增加 `ORIENTATION_PORTRAIT` / `ORIENTATION_LANDSCAPE` 校验。 | `test_settings_accessors.cpp` |
| A-P2-02 | App-Token Usage | `src/app/token_usage/tu_app.cpp:57-61` | 每 30 秒或手动刷新时，即使 API key 为空也调用 `tu_api_fetch_deepseek()`。 | P2 | 在 key 为空时跳过网络请求并提示用户配置。 | `test_tu_api.cpp` |
| H-P1-01 | HAL-显示 | `src/hal/hal_display_fb.cpp:86-93` | `setColorDepth(8)` 与 `createSprite()` 顺序依赖人工记忆，无断言或封装保护。 | P1 | 在 `hal_display_init()` 中断言 `setColorDepth` 先于 `createSprite`；封装 `hal_display_create_sprite()`。 | hardware smoke |
| H-P1-02 | HAL-输入 | `src/hal/hal_input.cpp:137-144`、`src/hal/hal_input_double_click.c` | 简单状态机与双击状态机通过同一个 `btn_state.dc` 传递事件，事件消费边界不清晰，可能产生竞争或丢失长按/双击。 | P1 | 统一事件模型：在 `hal_input_update()` 中一次性读取边沿并分发到单一状态机。 | `test_double_click.cpp`、`test_double_click_switch.cpp` |
| H-P2-01 | HAL-系统 | `src/hal/hal_system.cpp:39-41` | Native 环境下 `hal_delay_ms()` 为空操作，与硬件环境语义不一致；依赖延时的 native 测试可能不可靠。 | P2 | Native 后端实现忙等待或基于 `std::this_thread::sleep_for`。 | `test_hal_native.cpp` |
| H-P2-02 | UI-头文件 | `src/ui/ui_item.h:1-36` | 聚合头文件缺少 `extern "C"` 保护，且直接包含 HAL C++ 头。 | P2 | 添加 `extern "C"` 包裹；文档说明该头为 C/C++ 兼容聚合头。 | 编译全量检查 |
| H-P2-03 | UI-绘制 | `src/ui/ui_draw_list.c:212-228` | 列表绘制中仍存在内联 `switch (_item->type)`，未完全走 `ui_dispatch.c`。 | P2 | 扩展绘制派发表，移除内联 switch。 | `test_ui_dispatch.cpp` |
| H-P2-04 | UI-Popup | `src/ui/ui_item_popup.c:132-270` | `xerintosh_push_pop_up()` 约 140 行，含自动换行、缓存、状态机，使用 `goto`。 | P2 | 拆分为子函数并移除 `goto`。 | `test_ui_popup.cpp` |

## 本轮重构排期（按用户要求调整顺序）

| 阶段 | 模块 | 处理的问题 ID |
|------|------|---------------|
| 2.1 | 内核层 | K-P0-01、K-P0-02、K-P0-03、K-P0-04、K-P1-01 ~ K-P1-08 |
| 2.2 | App 上层 | A-P1-01 ~ A-P1-06、A-P2-01、A-P2-02 |
| 2.3 | HAL 层 | H-P1-01、H-P1-02、H-P2-01 |
| 2.4 | UI 核心层 | H-P2-02、H-P2-03、H-P2-04 |
| 2.5 | 文档体系 | 同步所有 public API 变化与源链接 |

> 说明：K-P0-01（FD 表隔离）会改动 `kern_task_t` 结构，影响 VFS/Shell/资源子系统，建议作为内核层首个子任务，并优先补充测试。

## 统计

- **问题总数**：27
- **P0 / P1 / P2**：4 / 16 / 7
- **内核层问题数**：16
- **App 层问题数**：8
- **HAL/UI 层问题数**：5
