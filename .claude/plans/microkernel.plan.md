# Plan: Xeros Microkernel（微型类 Unix 内核）

**Source PRD**: 用户需求（对话式）
**Selected Milestone**: 完整内核 + UI 迁移
**Complexity**: Large

## Summary

在 M5Stick-C（ESP32-PICO，520KB SRAM）上构建一个类 Unix 协作式微内核 **Xeros**。核心包含：
1. **协作式调度器**（Cooperative Fiber Scheduler）——手写，不依赖 FreeRTOS Task API
2. **虚拟文件系统 VFS**——纯内存，一切皆文件（`/dev/*`、`/proc/*`、`/sys/*`）
3. **动态栈管理**——按需分配、上限保护、栈溢出检测
4. **IPC 机制**——pipe、message queue
5. **现有 Xerintosh UI 迁移为用户态任务**——通过 `/dev/fb0` 和 `/dev/input0` 与硬件交互

FreeRTOS 继续在底层为 WiFi/BT 服务，Xeros 运行在 Arduino `loop()` 中作为"逻辑进程层"，不与底层冲突。

---

## Design Philosophy

### 协作式而非抢占式

在 520KB SRAM 的 ESP32 上：
- **抢占式调度**需要保存完整硬件上下文（32个寄存器 + FPU + 中断状态），每次切换开销大
- **协作式调度**只需要保存/恢复栈指针和少数寄存器（用 `setjmp/longjmp`），切换代价极低
- 无真正并发 = 无复杂锁机制 = 节省大量代码和运行时开销
- 教学/演示目的下，协作式更透明、更易调试

### 动态栈模型

```
初始分配: 1KB（覆盖 90% 任务场景）
增长策略: 按需扩展，步进 1KB
硬上限: 8KB（防止内存耗尽）
保护机制: 栈底写入金丝雀值（canary），每次 yield 校验
```

### VFS 极简设计

不实现完整的 Linux VFS（无 page cache、无 dentry LRU、无块设备）。只保留核心抽象：

```c
struct file_operations {
    ssize_t (*read)(struct file *f, char *buf, size_t len);
    ssize_t (*write)(struct file *f, const char *buf, size_t len);
    int (*ioctl)(struct file *f, unsigned int cmd, unsigned long arg);
    int (*release)(struct file *f);
};
```

---

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│ User Space（用户态任务）                                      │
│  ┌──────────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐   │
│  │  ui_task     │ │wifi_task │ │ bt_task  │ │shell_task│   │
│  │ (Xerintosh)  │ │(daemon)  │ │(daemon)  │ │(串口Shell)│   │
│  └──────┬───────┘ └────┬─────┘ └────┬─────┘ └────┬─────┘   │
│         └──────────────┴────────────┴────────────┘          │
│                    System Call Interface                      │
│                    kern_open/read/write/close/ioctl           │
├─────────────────────────────────────────────────────────────┤
│ Xeros Kernel（内核态）                                        │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐       │
│  │ Scheduler│ │   VFS    │ │  devfs   │ │  procfs  │       │
│  │(coop)    │ │(virtual) │ │(device)  │ │(process) │       │
│  └──────────┘ └──────────┘ └──────────┘ └──────────┘       │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐                     │
│  │  sysfs   │ │  IPC     │ │Mem Mgmt  │                     │
│  │(config)  │ │(pipe/mq) │ │(dyn stack)│                     │
│  └──────────┘ └──────────┘ └──────────┘                     │
├─────────────────────────────────────────────────────────────┤
│ HAL / FreeRTOS（现有层，不动）                                 │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐       │
│  │M5Unified │ │M5GFX     │ │WiFi Stack│ │BT Stack  │       │
│  │(Arduino) │ │(disp)    │ │(FreeRTOS)│ │(FreeRTOS)│       │
│  └──────────┘ └──────────┘ └──────────┘ └──────────┘       │
└─────────────────────────────────────────────────────────────┘
```

---

## Patterns to Mirror

| Category | Source | Pattern |
|---|---|---|
| Naming | `src/hal/hal_display.h:42` | 模块前缀 + snake_case：`hal_display_init()` |
| Error handling | `src/app/serial_monitor/sm_buffer.c` | 返回 bool / int 码，上层检查 |
| C/C++ Interop | `src/hal/hal_display.h:16` | `extern "C"` guards on all headers |
| Memory | `src/ui/ui_item_base.c` | `malloc`/`free`，无智能指针（嵌入式 C） |
| Callbacks | `src/ui/ui_item.h:226` | `void (*)(void *user_data)` 统一签名 |
| File size | CLAUDE.md convention | < 400 lines per file，< 50 lines per function |

---

## Files to Change

### 新建文件（内核层）

| File | Action | Why |
|---|---|---|
| `src/kernel/kern_types.h` | CREATE | 内核基本类型、错误码、常量 |
| `src/kernel/kern_task.h/c` | CREATE | 任务控制块、调度器、动态栈 |
| `src/kernel/kern_vfs.h/c` | CREATE | 虚拟文件系统核心（inode、dentry、file） |
| `src/kernel/kern_devfs.h/c` | CREATE | 设备文件系统（注册/查找设备） |
| `src/kernel/kern_procfs.h/c` | CREATE | 进程信息文件系统 |
| `src/kernel/kern_sysfs.h/c` | CREATE | 系统配置文件系统 |
| `src/kernel/kern_ipc.h/c` | CREATE | pipe、message queue |
| `src/kernel/kern_syscall.h/c` | CREATE | 系统调用入口与分发 |
| `src/kernel/kern_init.h/c` | CREATE | 内核初始化、panic、日志 |
| `src/kernel/kern_hal_bridge.h/c` | CREATE | 内核到 HAL 的桥接层 |
| `src/kernel/kern_shell.h/c` | CREATE | 串口微型 shell（用于验证） |
| `src/kernel/devices/dev_fb0.c` | CREATE | `/dev/fb0` 帧缓冲设备 |
| `src/kernel/devices/dev_input0.c` | CREATE | `/dev/input0` 按键设备 |
| `src/kernel/devices/dev_ttyS0.c` | CREATE | `/dev/ttyS0` 串口设备 |
| `src/kernel/devices/dev_null.c` | CREATE | `/dev/null` 黑洞设备 |

### 修改文件（迁移层）

| File | Action | Why |
|---|---|---|
| `src/main.cpp` | UPDATE | 替换 loop() 为 Xeros 调度入口 |
| `src/app/app_init.c` | UPDATE | UI 初始化改为任务创建 |
| `src/ui/ui_core.c` | UPDATE | 添加 kern_yield() 到动画循环 |
| `src/app/wifi/wifi_manager.cpp` | UPDATE | 改造为独立内核任务 |
| `src/app/bluetooth/bt_manager.cpp` | UPDATE | 改造为独立内核任务 |
| `src/app/serial_monitor/sm_app.cpp` | UPDATE | 通过 `/dev/ttyS0` 读取数据 |
| `test/test_native/test_kernel.cpp` | CREATE | 内核 native 测试 |

---

## Tasks

### Phase 0: 内核骨架与基础设施（可独立验证）

**Task 0.1: 类型系统与内存管理**
- **Action**: 创建 `kern_types.h`（错误码、PID、模式位、权限），`kern_init.c`（日志、panic、内存分配包装）
- **Mirror**: 沿用项目现有的模块前缀 `kern_`，错误码参考 Linux errno 子集
- **Validate**: `pio run -e native` 编译通过

**Task 0.2: 串口日志与 Panic**
- **Action**: 实现 `kern_log(level, fmt, ...)` 和 `kern_panic(msg)`，输出到串口；panic 时打印任务栈回溯
- **Mirror**: 类似 `Serial.println()`，但带级别过滤
- **Validate**: 在 native 测试中验证 panic 能正确打印调用栈

---

### Phase 1: VFS 与设备文件系统（可独立验证）

**Task 1.1: VFS 核心**
- **Action**: 实现 inode（文件元数据）、dentry（路径节点）、file（打开实例）三级结构；实现路径解析 `kern_path_resolve()`
- **Key design**: 无内存缓存，dentry 树常驻内存；文件名最长 31 字节（节省内存）
- **Validate**: native 测试：`kern_open("/dev/null")` → `kern_write()` → `kern_close()` 成功

**Task 1.2: devfs（设备文件系统）**
- **Action**: 注册设备接口 `kern_dev_register(name, fops)`；实现 `/dev/null`
- **Validate**: native 测试向 `/dev/null` 写入 1KB，返回成功，无内存增长

**Task 1.3: 物理设备映射**
- **Action**: 
  - `dev_fb0.c`: 将 `hal_display_clear/flush/draw_pixel` 等映射到 `write()` 和 `ioctl()`
  - `dev_input0.c`: 将 `hal_input_get_event` 映射到 `read()`（阻塞/非阻塞模式）
  - `dev_ttyS0.c`: 将 `Serial.read/write` 映射到文件操作
- **Key design**: `ioctl` 用于配置（如设置显示方向），`read/write` 用于数据
- **Validate**: 硬件环境上通过 `/dev/fb0` 画一个像素点并刷新到屏幕

---

### Phase 2: 协作式调度器（可独立验证）

**Task 2.1: 任务控制块与创建**
- **Action**: 定义 `struct kern_task`（PID、状态、栈指针、栈大小、优先级、文件描述符表）；实现 `kern_spawn(entry, name, stack_min)`
- **Dynamic stack**: 从堆分配初始 1KB，记录 canary（栈底写 magic number）
- **Validate**: native 测试创建 3 个任务，验证各自 PID 唯一

**Task 2.2: 上下文切换（setjmp/longjmp）**
- **Action**: 使用 `setjmp()` 保存当前任务上下文，`longjmp()` 恢复目标任务上下文；每个任务第一次启动通过汇编跳板设置栈指针
- **Note**: ESP32 的 newlib `setjmp` 保存 `a0-a15` 和 `sp` 等寄存器，足够协程切换
- **Validate**: native 测试：任务 A 和 B 交替打印计数器，证明切换正确

**Task 2.3: 调度策略**
- **Action**: 实现 Round-Robin 调度（遍历就绪任务列表，每个任务运行直到 `kern_yield()`）；`kern_sleep_ms(ms)` 将任务移入睡眠队列，到期唤醒
- **Validate**: native 测试：3 个任务以不同频率 yield，输出符合预期时序

**Task 2.4: 动态栈扩展**
- **Action**: `kern_yield()` 时检查栈使用量（当前 SP - 栈底）；若使用率 > 80%，尝试 `realloc` 扩展 1KB（不超过上限）；若 canary 被破坏，触发 `kern_panic()`
- **Validate**: native 测试：递归函数触发自动扩展，验证扩展后数据不丢失

---

### Phase 3: procfs + sysfs（可独立验证）

**Task 3.1: procfs**
- **Action**: 挂载到 `/proc/`；实现 `/proc/tasks`（列出所有任务：PID、名称、状态、栈使用）；`/proc/<pid>/status`（详细状态）；`/proc/<pid>/stack`（栈使用量）
- **Validate**: 通过串口 shell 执行 `cat /proc/tasks`，能看到当前任务列表

**Task 3.2: sysfs**
- **Action**: 挂载到 `/sys/`；实现 `/sys/brightness`（读写亮度等级）；`/sys/rotation`（读写屏幕方向）；`/sys/anim_speed`（读写动画速度）
- **Validate**: 通过 shell 执行 `echo 8 > /sys/brightness`，屏幕亮度改变

---

### Phase 4: IPC（进程间通信）

**Task 4.1: Pipe**
- **Action**: 实现匿名 pipe（`kern_pipe(fds[2])`），底层为环形缓冲区（默认 256B）；支持阻塞读/写
- **Validate**: native 测试：任务 A 写 pipe，任务 B 读 pipe，数据完整

**Task 4.2: Message Queue**
- **Action**: 实现命名消息队列（`kern_mq_open(name)`），支持按类型接收
- **Validate**: WiFi 任务通过 mq 向 UI 任务发送"连接成功"消息

---

### Phase 5: 现有代码迁移（UI → 用户态任务）

**Task 5.1: UI 任务化**
- **Action**: 将 `xerintosh_ui_main_core()` 和 `xerintosh_ui_widget_core()` 包装为 `ui_task_main(void)`；在 `kern_init()` 中 `kern_spawn(ui_task_main, "ui", 2048)`
- **Display via VFS**: UI 任务不再直接调用 `hal_display_*`，而是通过 `kern_open("/dev/fb0")` + `write()` + `ioctl()` 操作
- **Input via VFS**: 通过 `kern_open("/dev/input0")` + `read()` 获取按键事件
- **Validate**: 硬件上菜单正常显示、按键正常响应

**Task 5.2: WiFi / BT 任务化**
- **Action**: 将 `wifi_mgr_update()` 改造为 `wifi_task_main()`，在独立任务中运行；状态变化通过 pipe/mq 通知 UI 任务
- **Validate**: WiFi 扫描时 UI 不再卡顿（因为 WiFi 任务自行 yield）

**Task 5.3: Shell 任务**
- **Action**: 创建 `shell_task_main()`，从 `/dev/ttyS0` 读取命令，解析并执行（cat、echo、ps、kill）
- **Validate**: 串口输入 `ps`，打印任务列表；输入 `cat /proc/tasks`，输出进程信息

---

### Phase 6: 系统调用接口与兼容层

**Task 6.1: Syscall 编号与分发**
- **Action**: 定义 syscall 编号（SYS_OPEN=0, SYS_READ=1, ...）；`kern_syscall(int num, ...)` 统一入口
- **Validate**: native 测试覆盖所有 syscall

**Task 6.2: 用户态 API 封装**
- **Action**: 提供 `sys_open()`、`sys_read()` 等用户态封装，内部调用 `kern_syscall()`
- **Validate**: 现有代码改用 `sys_*` 后编译通过

---

## Validation

```bash
# Phase 0-3 验证（native 环境）
./.pio/build/native/program --gtest_filter=KernelTask.*
./.pio/build/native/program --gtest_filter=KernelVFS.*
./.pio/build/native/program --gtest_filter=KernelIPC.*

# Phase 1.3 / 3.2 验证（硬件环境）
pio run -e m5stick-c --target upload
# 串口输入：cat /proc/tasks
# 串口输入：echo 5 > /sys/brightness

# Phase 5 验证（硬件环境）
# 确认菜单正常导航、WiFi 扫描不卡顿
```

---

## Risks

| Risk | Likelihood | Mitigation |
|---|---|---|
| **setjmp/longjmp 在 ESP32 上不保存完整协程上下文** | MEDIUM | 先在 native 环境验证，硬件上通过简单任务切换测试；若失败，改用手写汇编 `swapcontext`（Xtensa 有 `entry`/`retw` 指令） |
| **动态栈 realloc 导致指针悬空** | HIGH | 栈内只存局部变量和返回地址，不存全局指针；扩展时数据 memcpy 到新地址；task struct 中保存的是栈基址而非内部指针 |
| **M5GFX 帧缓冲操作通过 VFS 后性能下降** | MEDIUM | `dev_fb0` 的 `write()` 直接透传到底层 HAL，无额外拷贝；复杂绘制仍用原有 HAL API，VFS 只用于刷新和配置 |
| **WiFi/BT Arduino 库依赖 FreeRTOS，与 Xeros 任务模型冲突** | HIGH | WiFi/BT 管理器仍跑在 Arduino loop() 的任务中（FreeRTOS 任务），Xeros 任务通过 IPC 与其通信；不改造 Arduino 库内部 |
| **520KB SRAM 不足以支撑多任务栈** | MEDIUM | 初始栈 1KB，上限 8KB；限制并发任务数 ≤ 8；提供 `kern_task_stack_usage()` 供监控 |
| **上下文切换时机不当导致 HAL 状态不一致** | MEDIUM | 所有 HAL 操作在单个任务内完成，不跨任务共享 HAL 状态；yield 点选择在帧边界或 I/O 等待处 |

---

## Acceptance Criteria

- [ ] Phase 0-3 全部通过 native 测试
- [ ] Phase 1.3 能在硬件上通过 `/dev/fb0` 绘制像素
- [ ] Phase 2.3 多任务在硬件上交替运行不崩溃
- [ ] Phase 3.2 串口 shell 能读写 `/sys/` 和 `/proc/`
- [ ] Phase 5.1 UI 任务通过 VFS 访问显示和输入，功能无损
- [ ] Phase 5.2 WiFi 扫描时 UI 仍能响应按键
- [ ] 所有新文件 < 400 行，函数 < 50 行
- [ ] 所有头文件有 `extern "C"` guards 和 include guards
