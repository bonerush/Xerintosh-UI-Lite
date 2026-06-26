# 全栈技术债务诊断报告

## 诊断方法

对 `/Users/yukisala/subject/Xerintosh/.worktrees/refactor-2026-06-27-fullstack` 的 `src/kernel/`、`src/hal/`、`src/ui/`、`src/app/`、`doc/` 进行静态扫描与代码审查。问题按 P0（高，必须修）、P1（中，建议修）、P2（低，可延后）排序。

---

## 1. Xeros 内核层（src/kernel/）

| ID | 文件 | 位置 | 优先级 | 问题 | 建议动作 | 关联测试 |
|---|---|---|---|---|---|---|
| K1 | `kern_sched.c`, `kern_task_lifecycle.c`, `kern_task_stack.c` | 多个 `#ifdef` 块 | P0 | 调度器、任务生命周期、栈增长代码在 Native ucontext / ESP32 native / ESP32 FreeRTOS 三个后端之间大量复制粘贴，任何修复需要改多处。 | 提取公共流程与后端 hook（如 `port_spawn_core`），保留 `#ifdef` 只在最底层移植文件。 | 新增 `test_kernel_port_contract.cpp` 验证三种后端行为一致。 |
| K2 | `kern_sched.h`, `kern_smp.h`, `kern_sched.c` | 全局变量 | P0 | `g_task_list`、`g_task_list_tail`、`g_next_pid`、`g_task_count`、`g_per_cpu[]` 等全局可变状态直接暴露，缺乏封装。 | 用访问器函数/结构体封装，隐藏内部指针，减少全局命名空间污染。 | 新增 `test_kernel_state_encapsulation.cpp` 验证只能通过 API 访问。 |
| K3 | `kern_sched_rr.c`, `kern_sched_fifo.c`, `kern_sync.c`, `kern_ipc.c` | 自旋锁实现 | P1 | 在 FreeRTOS 后端仍使用忙等待自旋锁，可能饿死低优先级 FreeRTOS 任务。 | 在 FreeRTOS 后端评估使用 FreeRTOS 临界区或互斥量；保留自旋锁给裸核/原生调度。 | `test_kernel_sched.cpp` 补充调度公平性测试。 |
| K4 | `kern_vfs.c` | 路径解析、FD 池 | P1 | `KERN_MAX_DENTRY_CHILDREN=256` 固定数组，`path_walk_locked` 线性扫描；FD 池固定 16 个；`kern_open` 约 60 行混合多项职责。 | 引入哈希或排序优化目录查找；拆分 `kern_open` 为 FD 分配、路径解析、inode 引用三步骤。 | 扩展 `test_kernel_vfs.cpp` 大目录与 FD 耗尽场景。 |
| K5 | `kern_resource.c` | 资源池 | P1 | `RES_POOL_SIZE=64` 静态数组，`uint64_t` 位图限制；资源释放回调可能遗漏组合顺序。 | 评估动态回退或按任务分配小型池；明确释放顺序文档。 | 新增 `test_kernel_resource_exhaustion.cpp`。 |
| K6 | `kern_kmalloc.c` | 全局统计 | P1 | `g_kmem_allocated_bytes` 为 `volatile size_t`，SMP 下非原子；守卫结构为全局静态。 | 改为原子操作或受自旋锁保护；按 NUMA/CPU 拆分统计减少竞争。 | 扩展 `test_kernel_kmalloc.cpp` 并发分配测试。 |
| K7 | `kern_task_lifecycle.c` | `kern_spawn` | P1 | 每个后端实现约 90 行，包含栈初始化、上下文设置、链表追加、调度类入队、MPU 守卫。 | 拆分为 `task_alloc`、`stack_setup`、`context_setup`、`enqueue_task` 等小函数。 | `test_kernel_task.cpp` 拆分等价测试。 |
| K8 | `kern_shell.c`, `kern_shell_cmds.c` | 主循环、命令表 | P2 | `shell_task_main` 约 150 行；`kern_shell_cmds.c` 1193 行，含 30+ 命令、历史缓冲、scope 变量。 | 将命令按功能拆到子文件；主循环拆为输入解析、历史、补全、执行四阶段。 | 新增 `test_shell_modular.cpp` 验证解析与执行分离。 |
| K9 | `kern_mpu.c` | 整个文件 | P2 | ESP32 PMS 寄存器框架 mostly stubbed，未随上下文切换完整应用。 | 补全区域配置/切换实现，或明确标记为实验性。 | 新增 `test_kernel_mpu.cpp`。 |
| K10 | `kern_port_native.c`, `kern_init.c` | TODO | P2 | `TODO: 使用 esp_timer`、`TODO: 硬件 LED 闪烁` 等遗留项。 | 在本次重构中不处理，记录到基线 TODO 清单。 | — |

### 中文伪代码拆解（K1 核心问题）

```c
函数 公共调度器初始化() {
    // 初始化全局状态
    清零任务链表
    创建 idle 任务
    注册调度类
    设置标志
}

函数 后端特定初始化() {
#ifdef NATIVE_UCONTEXT
    初始化 ucontext 上下文
#elifdef ESP32_NATIVE
    初始化 Xtensa 上下文与定时器
#elifdef FREERTOS
    创建 FreeRTOS 任务容器
#endif
}
```

**核心问题**：高层逻辑（链表、idle、类注册）与后端细节（上下文类型、定时器）耦合在同一段代码中，导致复制粘贴。

---

## 2. HAL 层（src/hal/）

| ID | 文件 | 位置 | 优先级 | 问题 | 建议动作 | 关联测试 |
|---|---|---|---|---|---|---|
| H1 | `hal_display_fb.cpp`, `hal_display_draw.cpp`, `hal_display_adv.cpp`, `hal_display_font.cpp` | 全局 `g_canvas` | P1 | `extern lgfx::LGFX_Sprite* g_canvas` 为全局裸指针，手动 new/delete，部分快路径缺少 NULL 检查。 | 封装为 `hal_display_ctx_t`；用 RAII 管理生命周期；所有绘制入口检查画布。 | 扩展 `test_hal_display.cpp` 空画布安全测试。 |
| H2 | `hal_input.cpp`, `hal_input_double_click.c` | 全局按钮状态 | P1 | `g_btn_a`、`g_btn_b`、`g_double_click_enabled`、`g_boot_time_ms` 为全局可变状态；单击/双击/长按状态机分散在两文件。 | 将按钮状态封装为 `hal_input_ctx_t`；统一事件模型；`double_click_enabled` 改为 per-button。 | 扩展 `test_double_click.cpp` 与 `test_hal_input.cpp`。 |
| H3 | `hal_display_*.cpp` | 多个文件 | P1 | 显示相关职责边界模糊：`fb` 管旋转/亮度，`draw` 管图元，`adv` 管 XOR/XBM，`font` 管字体，但部分常量重复定义。 | 明确分层：`hal_display_fb` 负责 framebuffer 生命周期；`hal_display_draw` 负责图元；常量集中到 `hal_layout.h`。 | 新增 `test_hal_layout_constants.cpp`。 |
| H4 | `hal_system.cpp` | tick/delay 实现 | P2 | native 与 ESP32 的 tick 语义通过 `#ifdef` 切换，时间基准可能不一致。 | 统一用 `hal_get_ticks_ms()` 返回单调毫秒；ESP32 使用 `esp_timer_get_time()`，native 使用 `std::chrono`。 | 扩展 `test_hal_system.cpp` 单调性测试。 |
| H5 | `hal_power_off.cpp` | 关机序列 | P2 | `TODO: 待 display HAL 迁移完成后调用显示休眠`。 | 在 HAL 显示重构后补全。 | — |

---

## 3. UI 核心层（src/ui/）

| ID | 文件 | 位置 | 优先级 | 问题 | 建议动作 | 关联测试 |
|---|---|---|---|---|---|---|
| U1 | `ui_draw_list.c` | 多处魔法数字 | P1 | 列表绘制、滚动条、选择器装饰使用大量未命名像素常量。 | 在 `ui_types.h` 或 `ui_layout.h` 定义 `LIST_DECO_H_LINE1_LEN`、`SCROLLBAR_WIDTH`、`SELECTOR_DASH_EXTEND` 等常量。 | 新增 `test_ui_layout_constants.cpp`。 |
| U2 | `ui_core.c` | 188, 192, 114 | P1 | 选择器高度 `15`、`spring_dt = 1.10f` 等魔法值。 | 定义 `SELECTOR_HEIGHT`/`SPRING_DT_SCALE`；或从字体高度派生。 | 扩展 `test_spring_anim.cpp` 收敛帧数测试。 |
| U3 | `ui_draw_list.c` | 121-200 | P1 | `xerintosh_draw_list_item` 约 80 行，混合 dispatch、位图、文本滚动、裁剪、双缓冲循环。 | 拆分为 `draw_item_icon`、`draw_item_text`、`draw_item_scroll`。 | 新增 `test_ui_draw_list_item.cpp`。 |
| U4 | `ui_draw_list.c`, `ui_core.c` | 127, 205-208, 219-223 | P0 | 对 `selected_item->parent` 和 `root->child_list_item[0]` 缺少 NULL/空列表检查，存在崩溃风险。 | 在绘制与初始化入口增加空指针和 `child_num == 0` 保护。 | 扩展 `test_ui_empty_root.cpp` 与 `test_ui_core_fixes.cpp`。 |
| U5 | `ui_dirty.c` / `ui_dirty.h` | 全部 | P1 | 脏矩形追踪是单个布尔标志，不是区域，强制每帧全屏重绘。 | 升级到 `xerintosh_dirty_region_t`，支持单矩形或矩形并集，透传给 HAL。 | 新增 `test_ui_dirty_region.cpp` 与性能基准。 |
| U6 | `ui_context.h` | 83 | P2 | 已标记 `@deprecated` 的 `g_xerintosh_dirty` 宏仍保留，可能误导使用。 | 移除或加 `__attribute__((deprecated))` 编译器警告。 | 编译 `-Werror=deprecated` 检查。 |
| U7 | `ui_item_selector.c` | 多处 | P2 | 弹簧速度复位模式 `v_y_selector = v_w_selector = v_h_selector = 0.0f` 重复 6 次。 | 引入 `selector_reset_velocity()` 辅助函数。 | 扩展 `test_ui_core_fixes.cpp`。 |
| U8 | `ui_draw_anim.c` | 20-64, 94-112 | P1 | 退出动画的沙漏像素坐标、扫描线偏移、状态阈值全为魔法数字。 | 定义 `EXIT_ANIM_OVERDRAW`、`EXIT_ANIM_SNAP_PX`、沙漏字形表。 | 新增 `test_exit_animation_state.cpp`。 |
| U9 | `ui_item_popup.c` | 17-19 | P2 | 静态换行缓冲区 48 字节，复制截断逻辑未使用 `sizeof` 一致保护。 | 统一使用 `sizeof`；增加 assert。 | 新增 `test_ui_popup_wrap.cpp`。 |
| U10 | `ui_draw_list.c` | 269-290 | P2 | `SCROLL_CYCLE_MS = 3000` 内嵌为未命名常量。 | 移到 `ui_types.h` 为 `TEXT_SCROLL_CYCLE_MS`。 | 扩展 `test_marquee.cpp`。 |

---

## 4. App 层（src/app/）

| ID | 文件 | 位置 | 优先级 | 问题 | 建议动作 | 关联测试 |
|---|---|---|---|---|---|---|
| A1 | `app_menu.c` | 整个文件 | P1 | 菜单树构建集中在一个文件，长度随应用增加而膨胀；职责为“注册所有 item”。 | 拆分为 `app_menu_core.c`（框架）和 `app_menu_entries.c`（条目注册），或按功能分组。 | 新增 `test_app_menu_structure.cpp`。 |
| A2 | 各 `user_item` 应用 | init/loop/exit | P1 | 各 App 生命周期回调命名、参数、返回值不统一；部分 App 在 `loop` 中处理状态机，部分在 UI task 中。 | 统一 `user_item_callbacks_t` 模板；每个 App 提供 `init/loop/exit` 并注册到表。 | 扩展 `test_app_state.cpp`。 |
| A3 | `settings/settings.c` | levels→hw values | P2 | 亮度、动画速度、弹簧刚度/阻尼等的 levels→hw values 转换分散在各调用点。 | 集中到 `settings/settings.c`，提供 `settings_level_to_hw()` / `settings_hw_to_level()`。 | 扩展 `test_settings_accessors.cpp`。 |
| A4 | `app_input.c`, `app_init.c` | 输入/弹窗耦合 | P2 | `app_input.c` 直接处理电源键弹窗与 dual-key 回调，逻辑与 UI 核心边界不够清晰。 | 将弹窗/双键逻辑封装到 `power_key_popup.c` 的独立 API，由 `app_input.c` 调用。 | 扩展 `test_power_key_popup.cpp`。 |

---

## 5. 文档体系（doc/）

| ID | 文件 | 位置 | 优先级 | 问题 | 建议动作 | 关联产物 |
|---|---|---|---|---|---|---|
| D1 | `doc/` 整体 | 目录结构 | P1 | `doc/` 按主题而非源码结构组织，缺少 `doc/ui/` 与 `doc/app/`；新开发者难以从源码找到对应文档。 | 让 `doc/` 镜像 `src/` 结构，保留 `architecture/` 作为交叉引用。 | 新增 `doc/ui/index.md`、`doc/app/index.md`。 |
| D2 | `doc/index.md` | 当前项目状态 | P2 | 项目状态段落已部分过时（WiFi 已验证、调度器已稳定等），未记录本次重构。 | 更新状态为 2026-06-27；新增重构记录章节。 | 更新后的 `doc/index.md`。 |
| D3 | `src/ui/ui_item_core.h` | 注释引用 | P2 | 引用 `doc/tutorials/api-templates.md`，但该文件不存在。 | 创建 `doc/tutorials/api-templates.md` 或修正引用。 | 新增/修正文档。 |

---

## 优先级汇总

| 优先级 | 数量 | 核心主题 |
|---|---|---|
| P0 | 5 | 内核后端代码重复、全局状态暴露、UI 空指针风险 |
| P1 | 14 | 自旋锁、VFS/FD 池、HAL 全局状态、UI 魔法数字、App 生命周期、文档结构 |
| P2 | 12 | 大文件拆分、TODO、过时文档、deprecated 宏 |

---

## 重构顺序建议

严格按 **kernel → HAL → UI → App → docs** 串行执行：

1. **kernel**：先封装全局调度状态（K2），再提取后端公共流程（K1），最后统一错误码与边界测试。
2. **HAL**：封装 `g_canvas`（H1），统一输入事件模型（H2），集中屏幕常量（H3）。
3. **UI**：先修复空指针/空列表保护（U4），再提取常量与拆分绘制函数（U1-U3, U8），最后升级脏矩形（U5）。
4. **App**：拆分 `app_menu.c`（A1），统一 user_item 生命周期（A2）。
5. **docs**：镜像 `src/` 结构（D1），更新 `index.md`（D2）。

---

> **Parent:** [README.md](README.md)
