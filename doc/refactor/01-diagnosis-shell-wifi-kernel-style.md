# 第十一轮重构前代码风格与文档一致性诊断

## 扫描范围

- `src/` 全部 `.h/.c/.cpp`
- `test/test_native/test_shell_complete.cpp`
- `doc/` 全部 `.md`
- 参考规范：`doc/coding-style.md`、`doc/refactor/01-diagnosis.md`

## 优先级定义

| 优先级 | 含义 |
|--------|------|
| P0 | 会导致崩溃、功能完全不可用、数据损坏 |
| P1 | 接口不一致、命名空间冲突风险、可维护性差 |
| P2 | 风格/文档/重复代码违反规范，建议本轮处理 |
| P3 | 轻微不一致，可后续顺带清理 |

## 通过项

| 检查项 | 结果 |
|--------|------|
| 所有 `.h` 均包含 include guard 与 `extern "C"` | ✅ |
| `.h` 中未出现 `nullptr`、`class`、`namespace`、`std::` 等 C++ 特性 | ✅ |
| `app_menu.c` 已补全蓝牙开关项（上一轮的 D9 已修复） | ✅ |
| `app_menu.c` 中 `bt_manager.h` 引用已非冗余 | ✅ |

---

## 问题清单

### P1 — 命名空间/模块前缀风险

| ID | 模块 | 文件 | 行号 | 问题描述 | 建议动作 |
|----|------|------|------|----------|----------|
| S1 | 内核/调度器 | `src/kernel/kern_sched.h` | 47-59 | 导出函数 `pick_next_ready`、`idle_entry`、`task_stack_init`、`task_write_canary`、`reap_zombies`、`task_entry_trampoline` 缺少 `kern_` 前缀 | 统一加 `kern_sched_` 或 `kern_` 前缀，或移入内部静态头 |
| S2 | 内核/同步原语 | `src/kernel/kern_sync.h` | 35-65 | `spinlock_t`/`mutex_t` 及 `spinlock_init`/`mutex_init` 等接口无 `kern_` 前缀，通用命名易与运行时库冲突 | 统一改为 `kern_spinlock_t`、`kern_mutex_t`、`kern_spinlock_init` 等 |
| S3 | 内核/Shell 补全 | `src/kernel/kern_shell_complete.h` | 28-70 | 新增接口 `shell_token_start`、`shell_parent_path`、`shell_complete_path`、`shell_complete_command` 缺少 `kern_` 前缀 | 统一改为 `kern_shell_token_start` 等，并同步更新测试文件 |
| S4 | 内核/调试串口 | `src/kernel/debug_serial.h` | 20-24 | 暴露函数 `debug_printf`、`debug_vprintf` 无模块前缀；文件位于 `src/kernel/` 应使用 `kern_` 前缀 | 改为 `kern_debug_printf`/`kern_debug_vprintf`，或合并进 `kern_log` 体系 |
| S5 | App/服务管理 | `src/app/wifi/wifi_manager.cpp` / `src/app/bluetooth/bt_manager.cpp` | 242-249 / 310-318 | WiFi 开关回调直接同步调用 `wifi_mgr_enable/disable`，BT 已采用 `request_enable/disable` + `process_requests` 异步模式，两者生命周期接口不对称 | 为 WiFi 增加 `wifi_mgr_request_enable/disable` 与 `wifi_mgr_process_requests`，统一走 `svc_mgr_helper` 模式 |

### P2 — 风格/重复/文档

| ID | 模块 | 文件 | 行号 | 问题描述 | 建议动作 |
|----|------|------|------|----------|----------|
| S6 | 内核/Shell 命令 | `src/kernel/kern_shell_cmds.c` | 1-1163 | 文件 1162 行，远超规范 400 行上限 | 按功能拆分为 `kern_shell_cmd_fs.c`、`kern_shell_cmd_sys.c`、`kern_shell_cmd_diag.c` 等 |
| S7 | 内核/Shell | `src/kernel/kern_shell.c` | 1-618 | 文件 618 行，补全/历史/行编辑/命令派发混合 | 将补全实现拆到 `kern_shell_complete.c`（与头文件配对） |
| S8 | App/WiFi | `src/app/wifi/wifi_manager.cpp` | 1-645 | 文件 645 行，状态机、弹窗、菜单回调、自动连接全部堆叠 | 拆出 `wifi_popup.c`、`wifi_auto_connect.c` 等子模块 |
| S9 | 内核/Shell 命令 | `src/kernel/kern_shell_cmds.c` | 471-481 / 776-787 / 560-567 / 814-824 | `cmd_reboot` 与 `cmd_bootloader`、`cmd_uname`/`cmd_version`/`cmd_info`、`cmd_free`/`cmd_meminfo` 存在重复逻辑 | 抽取 `kern_shell_restart()`、`kern_shell_print_version()`、`kern_shell_print_heap()` 等公共辅助 |
| S10 | 内核/Shell 命令 | `src/kernel/kern_shell_cmds.c` | 720-734 / 889-917 | `cmd_log`/`cmd_mode`/`cmd_ctrl` 重复执行 `kern_open(path, KERN_O_WRONLY)` → `kern_write` → `kern_close` | 抽取 `kern_shell_write_sys(tty, path, value)` 通用 helper |
| S11 | 蓝牙/UART | `src/app/bluetooth/bt_uart_service.h` | 34 / 40 | 回调类型 `bt_uart_rx_callback_t`、`bt_uart_connect_callback_t` 签名不统一，未带 `void *user_data` | 改为 `void (*)(void *user_data, const uint8_t *data, uint16_t len)` 等统一形式 |
| S12 | 文档/内核 | — | — | `doc/kernel/index.md` 未列出新增的 `kern_shell_complete.h` / Shell Tab 补全模块 | 补充 `kern-shell-complete.md` 或在索引中新增一行 |
| S13 | 文档/App | `doc/app/index.md` | 16-33 | `wifi/`、`bluetooth/`、`storage/`、`serial_input/`、`storage/`、`boot/`、`about/` 等源码子目录在 `doc/app/` 中无对应文档 | 补全 `wifi.md`、`bluetooth.md`、`storage.md`、`serial-input.md`、`boot.md`、`about.md` |
| S14 | 文档/Shell 命令 | `doc/kernel/kern-shell-cmds.md` | 13 / 79 | 源链接 `[kern_shell_cmds.c](../../src/kernel/kern_shell_cmds.c#L883-L926)` 已失效（实际命令表在 1069-1118 行）；文档命令表遗漏 `meminfo`、`tree`、`tasks`、`uptime` 等命令 | 刷新所有源链接行号，补全命令列表 |
| S15 | 文档/编码规范 | `doc/coding-style.md` | 54 | 源链接 `[ui_types.h](../src/ui/ui_types.h#L51)` 已漂移（L51 现在是 `xerintosh_set_font`） | 建立文档源链接自动化校验脚本，或手动重新锚定 |

### P3 — 轻微前缀/命名不一致

| ID | 模块 | 文件 | 行号 | 问题描述 | 建议动作 |
|----|------|------|------|----------|----------|
| S16 | 内核/调度类 | `src/kernel/kern_sched_rr.h` / `kern_sched_fifo.h` | 19 / 19 | 导出实例 `sched_class_rr`、`sched_class_fifo` 缺少 `kern_` 前缀 | 改为 `kern_sched_class_rr`、`kern_sched_class_fifo` |
| S17 | 内核/调度器 | `src/kernel/kern_sched.h` | 27-42 | 全局变量 `g_task_list`、`g_next_pid`、`g_task_list_lock` 等缺少模块名，且通过公开头暴露 | 加 `kern_sched_` 前缀，或改为静态 + getter |
| S18 | 内核/Shell | `src/kernel/kern_shell_cmds_internal.h` | 28-32 | 全局变量 `g_scope_running`、`g_scope_vars` 等只有 `g_` 无前缀 | 改为 `g_kern_shell_scope_*` |
| S19 | 内核/设备 | `src/kernel/devices/dev_ttyS0.h` 等 | 26 / 32 / 38 | 设备接口使用 `dev_ttyS0_*` 前缀，而内核模块统一前缀应为 `kern_` | 统一为 `kern_dev_ttyS0_*`，或更新规范允许 `dev_` 子前缀 |
| S20 | App/示波器 | `src/app/oscilloscope/oscilloscope_input.h` | 11-12 | 导出函数 `scope_sync_sample_rate`、`scope_handle_input` 使用 `scope_` 而非模块名 `oscilloscope_` | 改为 `oscilloscope_sync_sample_rate` 等 |
| S21 | App/蓝牙 | `src/app/bluetooth/bt_uart_service.h` | 49-121 | 文件名为 `bt_uart_service`，导出函数前缀为 `bt_uart_`，与模块名不一致 | 统一为 `bt_uart_service_*` 或重命名文件为 `bt_uart.h` |
| S22 | App/串口输入 | `src/app/serial_input/serial_input.h` | 40-73 | 文件名为 `serial_input`，导出函数前缀为 `serial_` | 统一为 `serial_input_*` |
| S23 | App/串口监视器 | `src/app/serial_monitor/*.h` | — | 模块内同时存在 `serial_monitor_*`、`sm_*`、`sm_buffer_*` 三种前缀 | 统一为 `serial_monitor_*` 或 `sm_` |
| S24 | 字体数据 | `src/fonts/cn_font_subset.h` | 7 | 导出数组 `lgfx_cn_font_subset` 无项目/模块前缀 | 改为 `font_cn_subset` 或 `xerintosh_font_cn_subset` |
| S25 | App/全局状态 | `src/app/app_state.h` | — | 跨模块全局变量 `g_wifi_on`、`g_bt_on` 直接暴露（上一轮 D14/D15 遗留） | 提供 `app_state_wifi_on()` 等访问器 |

---

## 本轮重构建议排期

| 子阶段 | 范围 | 处理 ID |
|--------|------|---------|
| 2.1 | 内核前缀统一 | S1-S4, S16-S19 |
| 2.2 | Shell 命令拆分与去重 | S6, S7, S9, S10, S18 |
| 2.3 | WiFi/BT 服务接口统一 | S5, S8, S11 |
| 2.4 | 文档补全与链接刷新 | S12-S15 |
| 2.5 | P3 命名清理 | S20-S25 |

---

## 备注

- 所有扫描结果均未触发 `extern "C"` 缺失或 include guard 缺失，新增 `kern_shell_complete.h` 在格式保护上合规。
- `test_shell_complete.cpp` 作为 C++ 测试文件使用 `nullptr`/`class` 等符合预期，无 C 接口污染问题；但若 S3 改名，需同步更新测试用例中的函数名。
