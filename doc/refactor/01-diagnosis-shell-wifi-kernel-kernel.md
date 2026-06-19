# 诊断报告：Shell / WiFi / 内核深度诊断（第十一轮 · 2026-06-19）

## 方法
- 静态扫描范围：`src/kernel/`（重点 `kern_shell*.c`、`kern_vfs.c`、`kern_devfs.c`、`kern_task_lifecycle.c`、`kern_resource.c`、`devices/dev_ttyS0.cpp`、`devices/dev_input0.c`、`devices/dev_pwrkey.c`、`kern_sysfs.c`、`kern_gpiofs.c`）
- 交叉参考：`src/app/settings/settings.c`、`src/main.cpp`、`src/hal/hal_display_fb.cpp`
- 参考报告：`doc/refactor/01-diagnosis.md`（第十轮）
- 关联测试：`test/test_native/test_shell_complete.cpp`、`test/test_native/test_kernel_vfs.cpp`、`test/test_native/test_kernel_resource.cpp`、`test/test_native/test_kernel_sysfs.cpp`

## 优先级定义
- **P0**：功能完全不可用、数据不一致导致用户误操作、崩溃风险
- **P1**：竞态条件、资源泄漏风险、接口不一致、可维护性问题
- **P2**：边界行为异常、测试与硬件不一致、可简化点
- **P3**：风格、文档、微小改进

---

## 问题清单

### P0

| ID | 模块 | 文件 | 行号 | 问题描述 | 建议动作 | 关联测试 |
|----|------|------|------|----------|----------|----------|
| **K11** | Shell / sysfs | `kern_shell_cmds.c` | 745-763 | `param list/get/set` 路径错误。`known[]` 与 `snprintf` 均使用 `/sys/kernel/%s`，但 `brightness`/`rotation`/`anim_speed`/`anim_enabled` 实际注册在 `/sys/` 根目录，仅 `log_level` 在 `/sys/kernel/`。导致 `param list` 对 4/5 个文件报错，`param get brightness` 等无法工作。 | 修正 `known[]` 为实际路径；`param get/set` 根据属性名选择 `/sys/` 或 `/sys/kernel/` 前缀；或统一 sysfs 注册路径。 | `test_kernel_sysfs.cpp` |
| **K12** | Shell 输入 | `kern_shell.c` | 584, 605-608 | 缓冲区溢出时显示与内部状态不同步。普通字符先通过 `kern_write` 回显，再检查 `pos < SHELL_BUF_SIZE - 1`。当 `pos` 已达上限时，字符被显示但未被记录到 `line`，后续退格/补全/执行将出现错位。 | 在回显前先检查空间，或回显后若未写入 `line` 则发送退格擦除。 | 新增回归测试 |
| **K13** | sysfs / HAL | `kern_sysfs.c` + `settings.c` + `main.cpp` | 46, 17-22, 191-193 | sysfs 初始值与真实硬件状态不一致。`/sys/brightness` 默认 `255`，但启动后实际 PWM 为 `127`（level 5）；`/sys/rotation` 默认 `0`，实际为横屏 `1`；`/sys/anim_speed` 默认 `92`，实际为 `65`。用户 `cat` 后写入相同值可能无变化，产生“设置失效”错觉。 | 在 `main.cpp` 的 `deferred_kernel_init()` 中通过 `kern_sysfs_update()` 将 sysfs 初值同步为当前硬件真实值。 | `test_kernel_sysfs.cpp` |

### P1

| ID | 模块 | 文件 | 行号 | 问题描述 | 建议动作 | 关联测试 |
|----|------|------|------|----------|----------|----------|
| **K21** | gpiofs / HAL | `kern_gpiofs.c` + `hal_display_fb.cpp` | 44, 112-121, 140-143 | `io set 26` 直接通过 `digitalWrite()` 操作 GPIO26，而 HAL 背光通过 `M5.Display.setBrightness()` 走 PWM。两条路径互不知晓，导致 sysfs `/sys/brightness` 与实际背光状态脱节。 | 将 GPIO26 从 `g_gpio_pins[]` 移除或标记为只读；背光统一由 `hal_display_set_brightness()` / sysfs brightness 控制。 | `test_kernel_gpiofs.cpp` |
| **K22** | Shell | `kern_shell_cmds.c` | 765-768 | `param save` / `param load` 仍为存根，仅打印 `NYI`，用户无法通过 Shell 持久化/恢复参数。 | 调用 `storage_save_all()` / `storage_load_all()` 或 `settings_save_to_storage()` / `settings_load_from_storage()`；如暂无统一接口，先实现对应存储写入。 | 新增回归测试 |
| **K23** | sysfs / Storage | `main.cpp` | 260-267 | sysfs brightness 回调把 `val`（0-255）直接写入 `storage_set_brightness(val)`，但 storage 层期望存储的是 `level * 10`。写入 255 后下次启动会被 `settings_load_from_storage()` 裁剪为 level 10，与用户期望不符。 | 在 sysfs 回调中先将 0-255 反向映射为 level（或扩展 storage API 直接存储 sysfs 原始值），保持 UI / Shell / sysfs 语义一致。 | `test_settings_accessors.cpp` |
| **K24** | VFS | `kern_vfs.c` | 31-45 | 全局 FD 池位图 `g_fd_pool_bitmap` 在多任务环境下无锁保护。`fd_pool_alloc/free` 可能被抢占，导致位图损坏或同一 `kern_file_t` 被重复分配。 | 在 FD 池操作中加入自旋锁或互斥锁；或在单核场景下使用 `portENTER_CRITICAL`。 | `test_kernel_vfs.cpp`（多任务 FD 测试） |
| **K25** | 任务生命周期 | `kern_task_lifecycle.c` | 471-486 | `kern_task_kill()` 对非虚任务先 `kern_resource_release_all()` 关闭 FD/释放资源，再设置 `ZOMBIE`，最后 `kern_port_thread_kill()`。在 `release_all` 与线程销毁之间存在窗口，目标线程若继续执行会访问已释放资源。 | 先标记 `ZOMBIE` 或发送终止信号让目标线程自行退出；资源释放应在线程真正停止后执行。 | `test_kernel_task.cpp` |
| **K26** | 调度器 / SMP | `kern_sched_rr.c` | 118 | `g_last_picked` 是全局变量，但 SMP 下每个 CPU 应独立维护上一轮选择，否则多核会互相跳过任务或产生不公平调度。 | 将 `g_last_picked` 移入 `g_per_cpu[]`（参考 `kern_smp.h` 的 per-CPU 宏）。 | `test_kernel_smp.cpp` |
| **K27** | 调度器 / SMP | `kern_sched.c` | 200-211 | ESP32 SMP 初始化时，`idle1` 通过头插法挂到 `g_task_list`，`sched_class_rr.task_list_tail` 被错误设为 `idle1`（实际 tail 应为 `idle0`）。若启用 FIFO 调度类或依赖 tail 的代码，会导致链表遍历异常。 | 修正 tail 为循环创建后的最后一个 idle（`idle0`），或统一使用 `sched_class_rr` 的 enqueue 接口。 | `test_kernel_sched.cpp` |

### P2

| ID | 模块 | 文件 | 行号 | 问题描述 | 建议动作 | 关联测试 |
|----|------|------|------|----------|----------|----------|
| **K31** | ttyS0 | `dev_ttyS0.cpp` | 111-123 | `\n→\r\n` 转换在 TX 缓冲区只剩一个空闲槽位时：先写入 `\r`，随后检查 `g_tx_count >= TTY_TX_BUF_SIZE` 并 `break`，导致原始 `\n` 丢失。 | 优先保证 `\n` 被写入；当空间不足时丢弃 `\r` 而非 `\n`，或整段跳过转换。 | 新增边界测试 |
| **K32** | ttyS0 / Native | `dev_ttyS0.cpp` | 136-145 | Native 回环测试直接复制原始输入 `in[]` 到 RX buffer，未应用 `\n→\r\n` 转换，导致 native 测试与硬件行为不一致。 | 在回环路径中复用转换后的缓冲区，或单独对 `in` 做相同转换。 | `test_kernel_devices.cpp` |
| **K33** | Shell / VFS | `kern_shell_cmds.c` | 56-72 | `resolve_path()` 不规范化 `.` / `..`，而 VFS `path_walk()` 也不支持，导致 `cd ../..` 等生成非法路径。 | 在 `resolve_path()` 后加入规范化步骤（参考 `kern_shell.c` 的 `shell_normalize_path()`），或在 VFS 层支持 `.` / `..`。 | `test_shell_complete.cpp`（已覆盖补全层归一化） |
| **K34** | Shell | `kern_shell_cmds.c` | 707-716 | `top` 使用非阻塞 `kern_read(tty, &c, 1)`，每次循环立即返回，CPU 占用高，可能影响 UI 渲染和 WiFi/BT 任务。 | 在 `kern_read` 返回 0 时使用 `kern_sleep_ms(50)` 降低轮询频率。 | 新增回归测试 |
| **K35** | sysfs | `kern_sysfs.c` | 146-151 | `strtol` 检查仅要求 `endptr != tmp`，接受 `"128abc"` 等输入为 `128`，可能不符合严格命令行语义。 | 检查 `endptr` 是否指向输入末尾（允许尾部 `\r\n`），否则返回 `KERN_EINVAL`。 | `test_kernel_sysfs.cpp` |
| **K36** | Shell | `kern_shell_cmds.c` | 875 | `scope start <ms>` 使用 `atoi(argv[2])`，未校验负数、零或极大值，可能导致 `g_scope_period_ms` 异常。 | 使用 `strtol` 并限定合理范围（如 50-60000 ms）。 | 新增回归测试 |
| **K37** | 资源追踪 | `kern_resource.c` | 139-162 | `kern_resource_release_all()` 持锁遍历并调用 `release` 回调。若回调（如未来用户自定义回调）内部调用 `kern_resource_track/untrack`，将产生死锁或链表损坏。 | 先收集所有 `release` 函数指针和 `ptr` 到临时数组，释放锁后再逐个调用；或对释放回调约定“不得操作 resource 链表”。 | `test_kernel_resource.cpp` |

### P3

| ID | 模块 | 文件 | 行号 | 问题描述 | 建议动作 | 关联测试 |
|----|------|------|------|----------|----------|----------|
| **K41** | Shell | `kern_shell_cmds.c` | 753-764 | `param get/set` 未校验参数数量，缺少子命令错误提示，且写入后不读取回显。 | 对 `get/set` 增加参数校验；写入后可选显示当前值。 | 新增回归测试 |
| **K42** | Shell / 安全 | `kern_shell_cmds.c` | 964-984 | `cmd_dskey` 对长度 ≤8 的 key 仅显示 `****`，用户无法确认 key 是否真的存在。 | 显示更明确的提示，如 `Current key: **** (length N)`。 | 新增回归测试 |
| **K43** | Shell | `kern_shell_cmds.c` | 475, 781 | `reboot` / `bootloader` 使用 `volatile uint32_t` 忙等待，时间不精确且阻塞调度器。 | 使用 `kern_sleep_ms()` 或 `vTaskDelay()`（ESP32）让出 CPU。 | 新增回归测试 |
| **K44** | ttyS0 | `dev_ttyS0.cpp` | 21-22, 203-204 | 同时存在 `extern bool g_flasher_bridge_active` 与 `s_ttyS0_bridge_active`，TODO 已标注迁移，但当前两者并存导致桥接状态维护重复。 | 移除对 `g_flasher_bridge_active` 的依赖，统一使用 `dev_ttyS0_set_bridge_active()`。 | `test_kernel_devices.cpp` |

---

## 上一轮修复回归验证

| 上一轮 ID | 修复状态 | 新发现边界问题 |
|-----------|----------|----------------|
| D1 相对路径 | ✅ `kern_shell_cmds.c` 中 `cat/cp/rm/mkdir/touch/echo/hexdump/ls/cd/tree` 均已使用 `resolve_path()` | K33：仍不支持 `.` / `..` 规范化 |
| D2 io 与 HAL 背光独立 | ⚠️ 仍存在 | K21：GPIO26 仍可直接 digitalWrite，与 sysfs brightness 双路径 |
| D3 param save/load 存根 | ⚠️ 仍存在 | K22：仍需实现 |
| D4 sysfs 背光节点审计 | ⚠️ 部分解决：sysfs 写入确实触发 `hal_display_set_brightness` | K13/K23：初始值不一致、存储语义不一致 |

---

## 本轮重构排期建议

| 子阶段 | 模块 | 处理的问题 ID |
|--------|------|---------------|
| 2.1.1 | Shell | K11, K12, K33, K34, K36, K41, K42, K43 |
| 2.1.2 | VFS / 调度 | K24, K26, K27 |
| 2.1.3 | 任务生命周期 | K25, K37 |
| 2.1.4 | sysfs / HAL / gpiofs | K13, K21, K22, K23, K35 |
| 2.1.5 | ttyS0 | K31, K32, K44 |
| 2.1.6 | 测试 | 补充 K11/K12/K31/K34 回归用例 |
