# 诊断报告：shell-wifi-kernel（第十一轮 · 2026-06-19）

## 方法

- 静态扫描范围：`src/kernel/`、`src/ui/`、`src/hal/`、`src/app/`
- 扫描方式：4 个并行 explore agent 分模块深度诊断
- 分报告：
  - [内核/Shell 层](./01-diagnosis-shell-wifi-kernel-kernel.md)
  - [App 层](./01-diagnosis-shell-wifi-kernel-app.md)
  - [HAL/UI 核心层](./01-diagnosis-shell-wifi-kernel-hal-ui.md)
  - [代码风格/文档](./01-diagnosis-shell-wifi-kernel-style.md)

## 优先级定义

- **P0**：会导致崩溃、功能完全不可用、数据损坏
- **P1**：功能正确但可维护性差、竞态条件、接口不一致
- **P2**：风格、文档、测试缺失
- **P3**：轻微不一致，可后续顺带清理

---

## 核心问题汇总（本轮必做）

### P0

| ID | 模块 | 文件 | 问题 | 建议动作 |
|----|------|------|------|----------|
| K11 | Shell / sysfs | `src/kernel/kern_shell_cmds.c` | `param list/get/set` 路径错误，4/5 属性无法访问 | 修正 known 表路径前缀 |
| K12 | Shell 输入 | `src/kernel/kern_shell.c` | 缓冲区满时回显与内部状态不同步 | 回显前检查空间或回退光标 |
| K13 | sysfs / HAL | `kern_sysfs.c` + `settings.c` + `main.cpp` | sysfs 初始值与真实硬件状态不一致 | 启动时同步 sysfs 初值 |
| APP-P0-01 | taskmgr / WiFi | `src/app/taskmgr/taskmgr_app.c` | UI 任务仍同步调用 `wifi_mgr_disable()` | 增加异步请求接口 |
| APP-P0-02 | 蓝牙 UART | `src/app/bluetooth/bt_uart_service.cpp` | `bt_uart_service_deinit()` TOCTOU 竞态 | 任务通知/信号量替代 delay |
| APP-P0-03 | WiFi/BT 互斥 | `wifi_manager.cpp` / `bt_manager.cpp` | BT 关闭 WiFi 但 WiFi 不检查 BT | 双向互斥检查 |

### P1

| ID | 模块 | 文件 | 问题 | 建议动作 |
|----|------|------|------|----------|
| K21 | gpiofs / HAL | `kern_gpiofs.c` + `hal_display_fb.cpp` | GPIO26 背光双路径 | 统一背光控制 |
| K22 | Shell | `kern_shell_cmds.c` | `param save/load` 存根 | 调用 storage 接口实现 |
| K23 | sysfs / Storage | `main.cpp` | sysfs brightness 存储语义不一致 | 反向映射为 level |
| K24 | VFS | `kern_vfs.c` | FD 池位图无锁 | 加锁保护 |
| APP-P1-01 | WiFi popup | `wifi_manager.cpp` | popup 标志仍有数据竞争 | 全部字段纳入 critical section |
| APP-P1-04 | WiFi 扫描 | `wifi_manager.cpp` | 扫描启动代码重复 | 抽取统一函数 |
| APP-P1-05 | WiFi 扫描 | `wifi_manager.cpp` | 扫描超时未停止底层扫描 | 超时分支调用 `esp_wifi_scan_stop()` |
| P1-1 | HAL 输入 | `hal_input_double_click.c` | 双击超时与按下同帧时事件丢失 | 调整事件处理顺序 |
| P1-2 | HAL 显示 | `hal_display_fb.cpp` | `set_rotation()` 不重建 sprite | 统一重建 sprite |
| P1-3 | UI 核心 | `ui_core.c` | UI 核心反向依赖 App | 引入回调解耦 |
| P1-4 | UI 上下文 | `ui_context.h` / `ui_core.c` | `g_xerintosh_exit_requested` 非原子 | 改为 volatile / 事件组 |

### P2

| ID | 模块 | 文件 | 问题 | 建议动作 |
|----|------|------|------|----------|
| S6 | Shell | `kern_shell_cmds.c` | 文件 1162 行，超 400 行上限 | 按功能拆分 |
| S7 | Shell | `kern_shell.c` | 补全/历史/行编辑/派发混合 | 拆分补全实现到 `.c` |
| S8 | WiFi | `wifi_manager.cpp` | 文件 645 行，职责堆叠 | 拆出 popup/auto_connect 子模块 |
| S9/S10 | Shell | `kern_shell_cmds.c` | 多组命令重复逻辑 | 抽取公共 helper |
| P2-2 | UI 绘制 | `ui_draw_*.c` | 依赖全局 draw color | 参数下传或局部化 |
| P2-3 | UI 布局 | `ui_draw_list.c` 等 | 硬编码像素值 | 使用 HAL 字体/屏幕常量 |
| D7/D8 | 串口监视器 | `sm_buffer.h` / `sm_ui.c` | 64 字符截断、横屏仅 7 行 | 提升行长度、优化横屏布局 |

---

## 本轮重构排期

按工作流要求顺序：**内核 → HAL → UI → App → 文档**

| 子阶段 | 模块 | 处理的问题 ID |
|--------|------|---------------|
| 2.1 | 内核层 | K11, K12, K13, K21, K22, K23, K24, K31-K37, K41-K44, S1-S4, S6, S7, S9, S10, S16-S19 |
| 2.2 | HAL 层 | P1-1, P1-2, P1-6, P1-9, P2-1, P2-14, P3-6 |
| 2.3 | UI 核心层 | P1-3, P1-4, P1-5, P1-7, P2-2 至 P2-12, P2-15, P3-1 至 P3-5 |
| 2.4 | App 层 | APP-P0-01 至 APP-P0-03, APP-P1-01 至 APP-P1-06, APP-P2-01 至 APP-P2-05, APP-P3-01 至 APP-P3-04, S5, S8, S11 |
| 2.5 | 文档体系 | S12-S15, 以及各层 public API 变更同步 |

---

## 测试缺口（本轮优先补充）

| 模块 | 缺失测试 |
|------|----------|
| Shell | `param` 路径回归、缓冲区溢出回显、top CPU 占用 |
| VFS | 多任务 FD 分配竞态 |
| ttyS0 | `\n→\r\n` 边界、native 回环一致性 |
| WiFi | 状态机、扫描超时、BT/WiFi 互斥 |
| BT | `bt_uart_service_deinit` 竞态 |
| HAL | 双击超时同时按下、XBM 位序、rotation sprite 重建 |
| UI | popup 隐藏残像、选择器祖父 NULL、全局 draw color 解耦 |
