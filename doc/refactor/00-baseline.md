# 重构基线报告

## 分支与 Commit

- 分支：`refactor/2026-06-14-kernel-ui`
- 起始 commit：`6411c578124f1bf029169ffe1f5dfbef9fddeee5`
- Worktree：`.worktrees/refactor-2026-06-14-kernel-ui`

## 构建基线

- `pio run -e m5stick-c`：✅ PASS（Took 26.36s）
- `pio test -e native`：✅ PASS（348 test cases succeeded）

> 硬件构建日志：`doc/refactor/assets/build-m5stick-c.log`
> Native 测试日志：`doc/refactor/assets/test-native.log`

## 代码规模

| 范围 | 文件数 | 代码行数（估算） |
|------|--------|------------------|
| `src/` C/C++ 源文件 | ~149 | 17,417 |
| `src/` 头文件 | - | 5,466 |
| `doc/` Markdown | 55 | 14,192 |
| `src/kernel/` + `src/ui/` 合计 | 49 | 11,189 |

> 注：`cloc` 未安装，使用 `find + wc -l` 统计。

## 已知问题（来自代码/文档扫描）

1. `src/kernel/kern_devfs.c` 暴露 `kern_devfs_register_device()`，而 `kern_device_register()` 也是 public 入口，双轨 API 需统一。
2. `src/kernel/kern_port_freertos.c:39` 的 `kern_port_native_sched_init()` 长达 376 行，职责过重。
3. `src/kernel/kern_init.c:121` 存在 TODO：`/* TODO: 硬件 LED 闪烁 */`。
4. `src/kernel/kern_port_native.c:198` 存在 TODO：`/* TODO: 使用 esp_timer 或简单的忙等待 */`。
5. UI 层已无明显的内联 `switch (item_type)` 残余，但需验证 `ui_dispatch.c` 是否覆盖所有生命周期点。

## 超长函数抽样（>50 行）

| 行数 | 文件 | 函数 | 所属层 |
|------|------|------|--------|
| 690 | `src/app/wifi/wifi_manager.cpp` | `wifi_mgr_init` | App |
| 406 | `src/app/storage/storage.cpp` | `storage_init` | App |
| 376 | `src/kernel/kern_port_freertos.c` | `kern_port_native_sched_init` | Kernel |
| 284 | `src/app/bluetooth/bt_uart_service.cpp` | `bt_uart_poll` | App |
| 208 | `src/app/serial_input/serial_input.cpp` | `serial_request_wifi_password` | App |
| 206 | `src/app/bluetooth/bt_manager.cpp` | `bt_mgr_init` | App |
| 183 | `src/hal/hal_input.cpp` | `hal_input_init` | HAL |
| 132 | `src/app/taskmgr/taskmgr_app.c` | `taskmgr_get_count` | App |

## 本次重构范围

- [x] 内核层（重点）
- [x] UI 核心层（重点）
- [ ] HAL 层（仅同步必要改动）
- [ ] App 层（仅同步必要改动）
- [x] 文档体系（随接口变化同步）

## 下一步

进入阶段 1：扫描与诊断。
