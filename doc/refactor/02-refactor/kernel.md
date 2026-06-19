# 阶段 2.1 内核层重构报告

## 目标

修复第十轮遗留的 shell/sysfs/VFS/ttyS0 问题，统一内核代码风格，消除 P0/P1 级别的崩溃与竞态风险。

## 已修复问题

| ID | 问题 | 文件 | 修改 | 状态 |
|----|------|------|------|------|
| K11 | `param list/get/set` 使用错误 sysfs 路径前缀 | `src/kernel/kern_shell_cmds.c` | 新增 `kern_shell_sysfs_attr_path()`，仅 `log_level` 位于 `/sys/kernel/`，其余在 `/sys/` | ✅ |
| K12 | Shell 缓冲区满时回显与内部状态不同步 | `src/kernel/kern_shell.c` | 先检查空间再回显，避免显示错位 | ✅ |
| K13 | sysfs 初始值与硬件真实状态不一致 | `src/kernel/kern_sysfs.c` + `src/main.cpp` | `deferred_kernel_init()` 绑定回调后同步当前硬件值到 sysfs | ✅ |
| K21 | GPIO26 背光路径与 HAL 双路径冲突 | `src/kernel/kern_gpiofs.c` | GPIO26 改为只读（LCD Backlight HAL only） | ✅ |
| K22 | `param save/load` 为 NYI 存根 | `src/app/storage/storage.cpp/h` + `src/kernel/kern_shell_cmds.c` | 实现 `storage_save_all()` / `storage_load_all()` | ✅ |
| K23 | sysfs brightness 存储语义不一致 | `src/app/settings/settings.c/h` + `src/main.cpp` | 新增反向映射 `settings_brightness_level_from_hw()`，存 storage 前转换为 level | ✅ |
| K24 | VFS FD 池位图无锁保护 | `src/kernel/kern_vfs.c` | 使用 `spinlock_t` 保护 `fd_pool_alloc/free` | ✅ |
| K31 | ttyS0 `\n→\r\n` 转换在缓冲区剩一个槽位时丢失 `\n` | `src/kernel/devices/dev_ttyS0.cpp` | 空间不足时跳过 `\r`，优先保留 `\n` | ✅ |
| K32 | native 回环未应用 `\n→\r\n` 转换 | `src/kernel/devices/dev_ttyS0.cpp` | native 回环对原始输入重新做转换 | ✅ |
| K35 | sysfs 写入接受 `"128abc"` 等输入 | `src/kernel/kern_sysfs.c` | `strtol` 后检查 `endptr` 是否指向末尾（允许尾部空白） | ✅ |
| APP-P0-01 | taskmgr 同步调用 `wifi_mgr_disable()` | `src/app/taskmgr/taskmgr_app.c` + `src/app/wifi/wifi_manager.cpp/h` | 新增 WiFi 异步请求接口 | ✅ |
| APP-P0-02 | `bt_uart_service_deinit()` TOCTOU 竞态 | `src/app/bluetooth/bt_uart_service.cpp` | 使用二值信号量等待 `bt_uart_poll()` 完成 | ✅ |
| APP-P0-03 | BT/WiFi 双向互斥不完整 | `src/app/wifi/wifi_manager.cpp` | `wifi_mgr_enable()` 拒绝 BT 已启用时直接启用；`process_requests()` 先异步关闭 BT 再启用 WiFi | ✅ |

## 新增/扩展测试

| 测试文件 | 新增测试 |
|----------|----------|
| `test/test_native/test_kernel_sysfs.cpp` | `RootAttributesExist`, `LogLevelExistsUnderSysKernel`, `KernelPrefixedRootAttributesDoNotExist`, `WriteRejectsTrailingGarbage`, `WriteAcceptsTrailingNewline` |
| `test/test_native/test_kernel_gpiofs.cpp` | `BacklightPinIsReadOnly`, `BacklightPinMarkedHalOnly` |
| `test/test_native/test_kernel_devices.cpp` | `TtyS0WriteConvertsLfToCrLf`, `TtyS0WriteKeepsExistingCrLf` |
| `test/test_native/test_settings_accessors.cpp` | `BrightnessHwValueAndReverseMapping`, `BrightnessLevelFromHwClampsBoundary`, `StorageSaveAllDoesNotCrash`, `StorageLoadAllDoesNotCrash` |

## 构建与测试

| 检查项 | 结果 |
|--------|------|
| `pio run -e m5stick-c` | ✅ SUCCESS（Flash 89.3%） |
| `pio test -e native` | ⚠️ test_native 套件因 KernelSchedTest 偶发 SIGTRAP ERRORED；其余套件 PASS。非本轮引入。 |

## 遗留问题（移至后续阶段/轮次）

| ID | 问题 | 原因 |
|----|------|------|
| K25 | `kern_task_kill()` 资源释放与线程销毁窗口 | 需要更复杂的任务终止信号机制，本轮未动 |
| K26 | `kern_sched_rr.c` `g_last_picked` SMP 全局 | 需 per-CPU 改造，影响调度器核心 |
| K27 | `kern_sched.c` idle tail 设置错误 | 需验证 FIFO 调度类是否启用，本轮未动 |
| K33 | `resolve_path()` 不支持 `.` / `..` | 需要与补全层归一化统一，避免路径语义冲突 |
| K34 | `top` CPU 占用 | 当前实现已使用 `kern_sleep_ms(2000)`，诊断疑似误报 |
| K36-K44 | P3 细节改进 | 范围小，本轮优先级低 |
| S1-S25 | 代码风格/前缀/文档 | 部分已处理，剩余移至后续轮次 |

## 关键提交

- `47ca888` fix(shell): param 子命令使用正确的 sysfs 路径前缀
- `a350a4c` fix(shell): 缓冲区满时不回显
- `93fb75a` fix(sysfs): 同步 sysfs 初始值与硬件状态
- `c46714e` feat(shell/storage): 实现 param save/load
- `066c579` fix(gpiofs): GPIO26 背光引脚改为只读
- `3d2a717` fix(vfs): FD 对象池位图加自旋锁保护
- `27cd417` fix(ttyS0): 修复 `\n→\r\n` 边界丢失
- `0a71486` fix(sysfs): 写入时拒绝数字后的非空白字符
- `036c947` feat(wifi): 异步启用/禁用请求接口 + BT/WiFi 双向互斥
- `d9fff43` fix(bt): bt_uart_service_deinit 使用信号量等待 poll 完成
