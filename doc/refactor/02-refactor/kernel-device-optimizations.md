# 内核层设备优化报告（第八轮 · 2026-06-16）

> **分支**: `refactor/2026-06-16-app-transition-device-optimizations`  
> **Commit**: `8ea81ce`  
> **范围**: 仅保留内核层设备优化；UI / App 过渡动画与 API 审计改动已按用户要求回滚。

## 变更摘要

| # | 变更 | 文件 | 对应诊断 ID | 效果 |
|---|------|------|-------------|------|
| 1 | 设备注册表加锁与失败回滚 | `src/kernel/kern_device.c/h` | P1-12, P1-6 | 多任务注册安全、部分失败可清理 |
| 2 | `/dev/ttyS0` 临界区 + 统一 ring buffer | `src/kernel/devices/dev_ttyS0.cpp/h` | P0-1, P0-2, P1-11, P2-5, P2-6 | 消除 head/tail 竞争、统一 Native/硬件代码路径 |
| 3 | `/dev/fb0` 清屏颜色协议修复 + 参数校验 | `src/kernel/devices/dev_fb0.c` | P0-3, P1-8, P1-9 | 协议与实现一致、非法参数返回 `KERN_EINVAL` |
| 4 | `/dev/input0` 事件环形队列 | `src/kernel/devices/dev_input0.c` | P1-10 | 避免连续事件丢失 |
| 5 | `/sys/gpio` 临界区 + 方向缓存 + 边界检查 | `src/kernel/kern_gpiofs.c/h` | P1-1 ~ P1-5, P2-1, P2-2 | GPIO 操作线程安全、方向不再虚报、引脚号可校验 |
| 6 | HAL 清屏颜色支持 | `src/hal/hal_display.h`, `src/hal/hal_display_fb.cpp` | P0-3 | 为 `fb0` 提供 `hal_display_clear_color()` |
| 7 | Native 测试覆盖 | `test/test_native/test_kernel_device.cpp`, `test_kernel_devices.cpp`, `test_kernel_gpiofs.cpp` | — | 新增设备注册、ttyS0、fb0、input0、gpiofs 测试 |

---

## 详细变更

### 1. 设备注册表加锁与失败回滚

**问题** (`kern_device.c`):
- `g_device_list` 遍历/插入/删除未加锁，并发注册时可能出现链表损坏。
- `kern_devices_init()` 中单个设备注册失败后直接返回，已注册设备残留。

**修复**:
- 新增 `kern_device_register()` / `kern_device_unregister()` 内部互斥锁（单核下临界区退化）。
- 注册失败时沿链表回滚并反注册已加入的设备。
- `kern_device_find()` 读路径加读锁，防止遍历期间节点被释放。

### 2. `/dev/ttyS0`

**问题**:
- ring buffer 的 `head`/`tail` 为普通变量，仅靠 `count` 原子无法保证多字节竞态。
- C++ 全局构造函数中初始化 UART，顺序不可控。
- Native 与硬件实现分支重复，维护成本高。

**修复**:
- 将 `head`/`tail` 改为 `volatile uint16_t`，在 `read`/`write` 入口进入临界区。
- 新增显式 `dev_ttyS0_init()`，由 `kern_devices_init()` 调用，替代全局构造。
- 抽取 `ttyS0_rb_push()` / `ttyS0_rb_pop()` 统一 ring buffer 辅助函数，Native/硬件共用。
- 在 `kern_device.h` 中增加桥接状态辅助，避免跨模块裸 `extern`。

### 3. `/dev/fb0`

**问题**:
- `DEV_FB_CMD_CLEAR` 协议字含 `color` 参数，但实现直接调用无参 `hal_display_clear()`。
- `DEV_FB_CMD_SET_ROTATION` 未校验 `arg`。
- `DEV_FB_CMD_FILL_RECT` 未校验 `w`/`h` 为正。

**修复**:
- 新增 `hal_display_clear_color(uint16_t color)`，由 `fb0` clear 命令调用。
- rotation 命令校验 `arg < 4`，否则返回 `KERN_EINVAL`。
- fill_rect 命令校验 `w > 0 && h > 0`。

### 4. `/dev/input0`

**问题**:
- `read()` 返回第一个事件后直接 `break`，若缓冲区中同时存在多个事件会丢失。

**修复**:
- 引入小型环形事件队列（默认 8 事件）。
- `dev_input0_push_event()` 入队，`read()` 按 FIFO 出队；队列满时覆盖最旧事件并记录溢出标记。

### 5. `/sys/gpio` (`kern_gpiofs`)

**问题**:
- `digitalRead/Write/pinMode` 直接调用无临界区。
- `gpio_get_dir()` 固定返回 0，方向虚报。
- 每次 `write` 都重复 `pinMode(pin, OUTPUT)`。
- 初始化 `calloc` 失败时仍设置 `initialized = true`。
- 硬编码最大引脚号 `128`。

**修复**:
- read/write/direction 操作均进入临界区。
- 内部维护 `g_gpio_dir[]` 方向缓存，`get_dir()` 返回真实状态。
- 仅在方向变化时调用 `pinMode()`。
- 初始化失败返回 `KERN_ENOMEM` 并不置标志，调用方可重试。
- 最大引脚号改用 `HAL_GPIO_MAX_PIN` 宏（硬件 40，Native 测试可覆盖）。

### 6. HAL 清屏颜色

- `hal_display.h` 增加 `hal_display_clear_color(uint16_t color)` 声明。
- `hal_display_fb.cpp` 在 Native 与 TFT 后端均实现颜色清屏，供 `fb0` 调用。

---

## 验证结果

| 项目 | 结果 |
|------|------|
| 硬件构建 `pio run -e m5stick-c` | ✅ SUCCESS（RAM 28.1%，Flash 88.9%） |
| Native 测试 `pio test -e native` | ✅ 443 test cases：1 skipped，442 succeeded |
| 新增测试 | `test_kernel_device.cpp`、`test_kernel_devices.cpp`、`test_kernel_gpiofs.cpp` 全部通过 |

> 注：测试总数比基线（426 succeeded）增加，来自新增的内核设备/设备注册/GPIOFS 测试用例。

---

## 回滚说明

第八轮原计划同时包含：
- 2.3 UI 核心层过渡动画基础设施
- 2.4 App 层过渡动画 + API 调用审计修复
- 2.5 文档体系同步
- 3 集成验证
- 4 归档

上述 UI / App 改动已在 `fd5abf8` 及之前提交中完成，但经实际测试后用户决定**回滚所有 UI 相关变更**，仅保留内核层设备优化（本报告）。当前分支 `HEAD` 位于 `8ea81ce`，后续如要重新引入过渡动画，可从 `fd5abf8` 之前的提交历史恢复。

---

## 变更文件列表

| 文件 | 说明 |
|------|------|
| `src/kernel/kern_device.c` | 设备注册表加锁、失败回滚 |
| `src/kernel/kern_device.h` | 锁类型与桥接辅助声明 |
| `src/kernel/devices/kern_devices.c` | 设备初始化路径适配新 API |
| `src/kernel/devices/kern_devices.h` | 注释修正 |
| `src/kernel/devices/dev_fb0.c` | clear/rotation/fill_rect 修复 |
| `src/kernel/devices/dev_input0.c` | 事件环形队列 |
| `src/kernel/devices/dev_ttyS0.cpp` | 临界区、统一 ring buffer、显式 init |
| `src/kernel/devices/dev_ttyS0.h` | 桥接状态声明 |
| `src/kernel/kern_gpiofs.c` | GPIO 临界区、方向缓存、边界检查 |
| `src/kernel/kern_gpiofs.h` | 方向缓存与引脚宏 |
| `src/hal/hal_display.h` | `hal_display_clear_color()` 声明 |
| `src/hal/hal_display_fb.cpp` | TFT/Native 颜色清屏实现 |
| `src/main.cpp` | 显式调用 `dev_ttyS0_init()` |
| `test/test_native/test_kernel_device.cpp` | 设备注册表测试 |
| `test/test_native/test_kernel_devices.cpp` | fb0/input0/ttyS0 测试 |
| `test/test_native/test_kernel_gpiofs.cpp` | GPIOFS 测试 |
