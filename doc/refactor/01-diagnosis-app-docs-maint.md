# 诊断报告（第七轮 · 2026-06-16 · app-docs-maint）

## 方法
- 静态扫描范围：`src/app/` + `src/ui/` + `src/kernel/` + `doc/`
- 扫描日期：2026-06-16
- 工具：explore agent × 3 并行扫描

## 优先级定义
- **P0**：文件严重超标（>400行超过30%）、会导致崩溃/内存泄漏
- **P1**：函数超标、接口不一致、API 混用、可维护性差
- **P2**：风格、文档、测试缺失

---

## 一、App 层问题清单

### P0（必须处理）

| ID | 问题 | 文件 | 行数 | 超标 |
|----|------|------|------|------|
| A-P0-1 | 文件严重超标 | `wifi_manager.cpp` | 706 | +77% |
| A-P0-2 | 文件严重超标 | `flasher_app.cpp` | 525 | +31% |

### P1（建议处理）

| ID | 问题 | 影响模块 | 详情 |
|----|------|----------|------|
| A-P1-1 | `app_menu_build()` 84 行 | app_menu.c | 含 7 个 user_item + 4 个 switch/slider，建议提取 build_baud_submenu() |
| A-P1-2 | `wifi_mgr_update()` 142 行 | wifi_manager.cpp | 超大 switch 状态机，建议拆分为 per-state 函数 |
| A-P1-3 | `flasher_loop()` 119 行 | flasher_app.cpp | 透传+协议解析+动画混合 |
| A-P1-4 | `serial_monitor_loop()` 103 行 | sm_app.cpp | 输入+BLE+动画+绘制杂糅 |
| A-P1-5 | `taskmgr_loop()` 93 行 | taskmgr_app.c | 输入+确认+动画混合 |
| A-P1-6 | `ui_service` 使用不一致 | about, flasher, sm, taskmgr, tu | 部分用 ui_service_user_item_init/exit，部分裸调 hal_input_reset_events |
| A-P1-7 | 文件超标 | `bt_uart_service.cpp` | 423 行，+6% |
| A-P1-8 | 环形缓冲区重复 | bt_uart_service + sm_buffer | 两处独立实现本质相同的 ringbuf |

### P2（优化建议）

| ID | 问题 | 文件 | 详情 |
|----|------|------|------|
| A-P2-1 | `draw_terminal()` 74 行 | sm_ui.c | 建议拆分 soft-wrap 逻辑 |
| A-P2-2 | `rebuild_network_list()` 74 行 | wifi_manager.cpp | 建议拆分已保存/可用网络构建 |
| A-P2-3 | `flasher_menu_process_input()` 68 行 | flasher_menu.c | 强制解除状态机内联过长 |
| A-P2-4 | 亮度转换非线性 | settings.c | 低亮度区跳跃大（level 1→2 从 25→51） |
| A-P2-5 | 波特率转换签名不一致 | settings.c | 参数 vs 全局变量 |
| A-P2-6 | NATIVE_TEST 守卫不一致 | about.c vs 其他 | about 包裹守卫，oscilloscope 未包裹 |
| A-P2-7 | sm_app.h 暴露过多全局变量 | sm_app.h | UI 层直接访问数据层全局状态 |
| A-P2-8 | `bt_uart_poll()` 59 行 | bt_uart_service.cpp | 含注释略超标 |

---

## 二、文档体系问题清单

由 explore agent 扫描 doc/ 目录（89 个 .md 文件，18,037 行）完成。核心发现：

### P0（必须处理）

| ID | 问题 | 文件 | 详情 |
|----|------|------|------|
| D-P0-1 | 源链接全部偏移 | `doc/kernel/kern-device.md` | 所有 kern_device.c 行号偏移 ~35 行 |
| D-P0-2 | 完全断裂的链接 | `doc/app/app-init.md` | 链接指向不存在的函数 |

### P1（建议处理）

| ID | 问题 | 文件 | 详情 |
|----|------|------|------|
| D-P1-1 | API 命名新旧混用 | 多个 doc/ 文件 | `xerintosh_` vs `astra_` 前缀混用 |
| D-P1-2 | 缺失文档 | doc/app/ | oscilloscope、flasher、token_usage、shutdown 4 个 App 无独立文档 |
| D-P1-3 | 缺失文档 | doc/kernel/ | kern_port、kern_sync 等新增模块无文档 |
| D-P1-4 | 索引缺失 | doc/index.md | 缺少 oscilloscope、flasher、ui_dirty 条目 |
| D-P1-5 | doc/ 结构未镜像 src/ | doc/app/ | serial_monitor 有 3 个源码文件但只有 1 个文档条目 |
| D-P1-6 | 过时描述 | doc/developer-guide.md | user_item 模板未反映 ui_service 最新 API |

### P2（优化建议）

| ID | 问题 | 详情 |
|----|------|
| D-P2-1~10 | 次要链接偏移、注释风格不一致、中英文混用等 | 详见 explore agent 报告 |

---

## 三、UI 核心层维护检查

| ID | 严重度 | 问题 | 位置 |
|----|--------|------|------|
| UI-1 | P1 | `xerintosh_draw_exit_animation()` 115 行（整个 ui_draw_anim.c 仅此函数） | `ui_draw_anim.c:21-135` |
| UI-2 | P1 | `xerintosh_draw_list_item()` 76 行 | `ui_draw_list.c:119-194` |
| UI-3 | P1 | `xerintosh_draw_list_appearance()` 62 行 | `ui_draw_list.c:51-112` |
| UI-4 | P1 | `xerintosh_draw_pop_up()` 66 行 | `ui_draw_widgets.c:92-157` |
| UI-5 | P2 | `ui_dispatch.c` 431 行，+31 行 | `ui_dispatch.c` |
| UI-6 | P2 | 绘制函数对全局状态耦合度高 | ui_draw_*.c |

**结论**：派发表覆盖度 A、动画公式已封装为 `xerintosh_ease()` ✅。主要问题是 4 个绘制函数过长 + 全局状态耦合。

---

## 四、内核层维护检查

| ID | 严重度 | 问题 | 位置 |
|----|--------|------|------|
| K-1 | P1 | `kern_sched_fifo.c` 缺少 SMP 自旋锁（pick_next/tick） | `kern_sched_fifo.c:81-116` |
| K-2 | P1 | native 测试下栈内存从不释放（未纳入资源追踪） | `kern_task_lifecycle.c` |
| K-3 | P1 | `reap_zombies()` 遍历 `cls->task_list` 非 `g_task_list`，可能遗留 ZOMBIE | `kern_task_lifecycle.c:502-527` |
| K-4 | P2 | `kern_shell_cmds.c` 970 行 | +570 行 |
| K-5 | P2 | `kern_task_lifecycle.c` 561 行 | +161 行 |
| K-6 | P2 | `kern_vfs.c` 557 行 | +157 行 |
| K-7 | P2 | `kern_port_freertos.c` 437 行 | +37 行 |
| K-8 | P2 | `kern_task_kill()` 用 `return 0` 代替 `return KERN_OK` | 风格不一致 |
| K-9 | P2 | 三重 `#ifdef` (NATIVE_TEST/XEROS_NATIVE/ESP32) 代码膨胀 | kern_task_lifecycle, kern_sched |

**结论**：设备注册已统一 ✅、错误码基本一致 ✅。主要问题是 4 个超长文件 + FIFO SMP 锁缺失 + native 栈泄漏。

---

## 本轮重构排期

| 子阶段 | 模块 | 处理问题 | 预估工作量 |
|--------|------|----------|-----------|
| 2.4 | App 层 | A-P0-1, A-P0-2, A-P1-1~8, A-P2-1~8 | 主要 |
| 2.5 | 文档体系 | D-P0-1, D-P0-2, D-P1-1~6, D-P2-* | 主要 |
| 2.3m | UI 维护 | UI-1, UI-2, UI-3, UI-4 | 小 |
| 2.1m | 内核维护 | K-1, K-2, K-3, K-8 | 小（安全修复优先） |

---

## 关联测试建议

| 重构动作 | 已有测试 | 建议新增 |
|----------|----------|----------|
| 拆分 wifi_manager.cpp | 无 | test_wifi_menu.cpp: rebuild_network_list 菜单结构验证 |
| 拆分 flasher_app.cpp | test_flasher.cpp | test_flasher_proto_STK500.cpp / SLIP.cpp |
| 统一 ui_service | test_ui_service_landscape.cpp | test_ui_service_lifecycle.cpp: 验证各 App init/exit |
| 提取公共 ringbuf | test_sm_buffer.cpp | bt_uart ringbuf 迁移到共用测试 |
| app_menu_build() 拆分 | test_app_menu_safety.cpp | 验证拆分后菜单结构不变 |
| 修复内核锁 | test_kern_sched.cpp | 新增 FIFO SMP 并发测试 |
