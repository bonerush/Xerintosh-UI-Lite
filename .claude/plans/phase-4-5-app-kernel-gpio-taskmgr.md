# Phase 4-5 实施计划：App 内核化 / GPIO 桥接 / 版本信息 / 任务管理器 / 文档清理

> 创建日期: 2026-05-28
> 状态: 计划阶段
> 关联: Phase 0-3（已完成）

---

## 总览

| Phase | 内容 | 代码变更量 | 风险 |
|-------|------|-----------|------|
| 4a | App 内核化（user_item → 内核任务可见性） | ~80 行新增 | 低 |
| 4b | GPIO 桥接（/sys/gpio 文件系统） | ~250 行新增 | 中 |
| 4c | 内核版本信息与开发者名 | ~60 行新增 | 低 |
| 4d | 任务管理器 App（user_item + 保护机制） | ~300 行新增 | 中 |
| 5 | 代码清理 + 文档更新 + 提交 | ~200 行改动 | 低 |

---

## Phase 4a: App 内核化 —— 让 user_item App 对内核可见

### 现状分析

当前架构中，`user_item` App（串口监视器、关于页）运行在 **UI 任务上下文** 内：

```
kern_sched_tick()
  └── ui task (kernel thread)
        ├── app_input_process()  → 读取按键
        ├── xerintosh_ui_main_core()
        │     └── if in_user_item: app->loop_function()  ← App 逻辑在此运行
        └── hal_display_flush()
```

问题：
1. **`/proc/tasks` 不可见** —— App 没有自己的 TCB，用户无法感知其存在
2. **`kill` 无效** —— 无法从 Shell 终止 App
3. **架构割裂** —— WiFi/BT 是独立内核任务，但串口监视器等 App 不是

### 设计方案：虚任务（Virtual Task）

**不创建独立的 FreeRTOS 任务**，而是在进入 `user_item` 时向内核任务链表注册一个 **虚任务**，退出时标记为 ZOMBIE。

原因：
- 创建真实内核任务会导致 **显示屏并发访问** 问题（双任务抢 `M5Canvas`）
- 当前 `user_item` 与 UI 框架紧密耦合（通过 `in_user_item` 标志控制渲染路径）
- 虚任务方案 **最小侵入**，仅增加内核可观测性

### 实现步骤

#### Step 1: `kern_task.h` 新增 API

```c
// kern_task.h 新增
#define KERN_TASK_FLAG_VIRTUAL  0x01   // 虚任务（无独立 FreeRTOS 上下文）
extern kern_pid_t kern_task_register_virtual(const char *name);
extern void kern_task_unregister_virtual(kern_pid_t pid);
```

- `kern_task_register_virtual(name)`: 分配 TCB，设置 `rtos_handle = NULL` + `state = KERN_TASK_RUNNING`，不创建 FreeRTOS 任务
- `kern_task_unregister_virtual(pid)`: 标记为 ZOMBIE + 回收

#### Step 2: `kern_task.c` 实现

在 ESP32 分支添加虚任务注册逻辑，复用现有的 `g_task_list` 链表和 `g_task_count` 计数器。

虚任务特点：
- `stack_base = NULL`, `stack_size = 0`（无独立栈）
- `rtos_handle = NULL`（无 FreeRTOS 任务）
- `/proc/tasks` 中 `stack_usage` 显示为 `n/a`
- `pick_next_ready()` 自动跳过虚任务（不参与调度）

#### Step 3: `ui_core.c` 集成

在 `handle_user_item_enter()` 中：
```c
kern_pid_t pid = kern_task_register_virtual(item->base_item.content);
item->kernel_pid = pid;
```

在 `handle_user_item_exit()` 中：
```c
kern_task_unregister_virtual(item->kernel_pid);
item->kernel_pid = KERN_PID_INVALID;
```

#### Step 4: `xerintosh_user_item_t` 新增字段

```c
typedef struct xerintosh_user_item_t {
    // ... 现有字段 ...
    kern_pid_t kernel_pid;  // 内核任务 ID（-1 表示未注册）
} xerintosh_user_item_t;
```

#### Step 5: `cmd_kill` 集成虚任务终止

在 `kern_shell_cmds.c` 的 `cmd_kill` 中，对虚任务的处理：
- 设置 `task->state = KERN_TASK_ZOMBIE`
- 设置 `g_xerintosh_selector.exit_requested = true`（通知 UI 任务退出当前 user_item）
- 弹出信息栏 "App terminated"

### 验证标准

| 测试 | 方法 | 预期 |
|------|------|------|
| `cat /proc/tasks` 可见 | 进入串口监视器 → shell `cat /proc/tasks` | 出现 `serial_monitor RUNNING n/a/0` |
| `kill` 可终止 | shell `kill <pid>` | 串口监视器退出，返回菜单 |
| 退出后清理 | 长按 BtnB 退出 → `cat /proc/tasks` | serial_monitor 条目消失 |
| 系统任务保护 | `kill 0` (idle) / `kill 2` (ui) | "cannot kill system task" |

---

## Phase 4b: GPIO 桥接 —— `/sys/gpio` 文件系统

### 现状分析

当前无任何 GPIO 操作。按键输入通过 M5Unified 库间接读取（`M5.BtnA/BtnB`），不暴露底层 GPIO 状态。

### 设计方案

创建 **gpiofs**（GPIO 虚拟文件系统），挂载在 `/sys/gpio` 下：

```
/sys/gpio                    ← 目录
├── list                     ← 只读：所有引脚状态表
├── 0                        ← 读写：GPIO0 状态
├── 25                       ← 读写：GPIO25（Speaker DAC）
├── 26                       ← 读写：GPIO26（LCD 背光）
├── 32                       ← 读写：GPIO32（Grove SDA）
├── 33                       ← 读写：GPIO33（Grove SCL）
├── 36                       ← 只读：GPIO36（BtnA，仅输入）
└── 37                       ← 只读：GPIO37（BtnB，仅输入）
```

### 文件格式

**`/sys/gpio/list`** (read)：
```
PIN  DIR   VAL  FUNC
0    IN    1    BOOT / I2C_SDA
25   OUT   1    Speaker DAC
26   OUT   0    LCD Backlight
32   IN    1    Grove SDA
33   IN    1    Grove SCL
36   IN    1    Button A (RTC)
37   IN    1    Button B (RTC)
```

**`/sys/gpio/<N>`** (read)：
```
pin=36 direction=in value=1 function="Button A"
```

**`/sys/gpio/<N>`** (write)：仅对输出引脚有效
```
echo 1 > /sys/gpio/25   →  GPIO25 输出高电平
echo 0 > /sys/gpio/25   →  GPIO25 输出低电平
```

### 实现步骤

#### Step 1: `src/kernel/kern_gpiofs.h`

```c
// GPIO 引脚描述符
typedef struct {
    uint8_t  pin;           // GPIO 编号
    const char *func;       // 功能描述
    bool     is_output;     // 是否可输出
} kern_gpio_pin_t;

// 注册所有引脚到 /sys/gpio/
extern void kern_gpiofs_init(void);
```

#### Step 2: `src/kernel/kern_gpiofs.c`

- 实现 `gpiofs_read()` / `gpiofs_write()` 文件操作
- `read` 时调用 `gpio_get_level(pin)` / `digitalRead(pin)` 获取实时状态
- `write` 时调用 `gpio_set_level(pin, val)` / `digitalWrite(pin, val)` 设置输出
- `list` 文件遍历所有预定义引脚，生成表格
- 写入前检查引脚方向（仅输出引脚可写）

#### Step 3: `main.cpp` 集成

在 `deferred_kernel_init()` 中添加 `kern_gpiofs_init()`，位于 `kern_sysfs_init()` 之后：

```c
kern_gpiofs_init();
Serial.println("[  OK  ] GPIO filesystem mounted at /sys/gpio");
```

#### Step 4: Shell 命令集成

`cat /sys/gpio/list` 和 `cat /sys/gpio/36` 通过 VFS 自动工作，无需新增 Shell 命令。

### 验证标准

| 测试 | 方法 | 预期 |
|------|------|------|
| `cat /sys/gpio/list` | shell 命令 | 显示 7 个引脚状态表 |
| `cat /sys/gpio/36` | shell 命令 | `pin=36 direction=in value=1`（按下 BtnA 变 0） |
| `echo 1 > /sys/gpio/25` | shell 命令（如果扬声器可用） | GPIO25 输出高电平 |
| `echo 1 > /sys/gpio/36` | shell 命令 | `gpio: permission denied (input-only)` |

---

## Phase 4c: 内核版本信息与开发者名

### 现状分析

版本号硬编码在两处：
- `kern_procfs.c:123`: `"Xeros 0.1.0 compiled " __DATE__`
- `kern_shell_cmds.c:455`: `"Xeros 0.1.0 ESP32-PICO compiled " __DATE__`

无开发者名，无统一版本管理。

### 设计方案

#### Step 1: 创建 `src/kernel/kern_version.h`

```c
#ifndef KERN_VERSION_H
#define KERN_VERSION_H

#define XEROS_VERSION_MAJOR  0
#define XEROS_VERSION_MINOR  2
#define XEROS_VERSION_PATCH  0
#define XEROS_VERSION_STRING "0.2.0"
#define XEROS_DEVELOPER      "YukiSala"
#define XEROS_CODENAME       "M5Stick-P1"
#define XEROS_PLATFORM       "ESP32-PICO"

#endif
```

#### Step 2: 更新引用处

**`kern_procfs.c:procfs_version_generate()`**：
```c
snprintf(content, max_len,
    "Xeros " XEROS_VERSION_STRING " (" XEROS_CODENAME ")\n"
    "Developer: " XEROS_DEVELOPER "\n"
    "Platform: " XEROS_PLATFORM "\n"
    "Compiled: " __DATE__ " " __TIME__ "\n");
```

**`kern_shell_cmds.c:cmd_uname()`**：同步使用 `XEROS_VERSION_STRING` 和 `XEROS_DEVELOPER`。

#### Step 3: 新增 `/proc/developer` 文件

在 `kern_procfs.c` 添加 `KERN_PROCFS_DEVELOPER = 5`，注册 `/proc/developer`：
```
Developer: YukiSala
Project: M5Stick-P1 (Xerintosh UI)
```

### 验证标准

| 测试 | 预期 |
|------|------|
| `cat /proc/version` | `Xeros 0.2.0 (M5Stick-P1)\nDeveloper: YukiSala\nPlatform: ESP32-PICO\nCompiled: May 28 2026 ...` |
| `cat /proc/developer` | `Developer: YukiSala\nProject: M5Stick-P1 (Xerintosh UI)` |
| `uname` | `Xeros 0.2.0 (M5Stick-P1) ESP32-PICO compiled May 28 2026 ...` |

---

## Phase 4d: 任务管理器 App

### 设计方案

一个新的 `user_item` App —— **任务管理器**，替代当前空的"关于"页面。

### 功能

1. **显示所有内核任务**（从 `kern_task_list_head()` 遍历）
2. **每行格式**：`PID NAME STATE STACK`
3. **选择器导航**：短按 BtnA/BtnB 上下移动
4. **终止任务**：长按 BtnA 对选中任务执行 kill
5. **保护机制**：系统关键任务不可终止
6. **确认弹窗**：终止前显示确认提示

### 受保护任务列表

```c
static const char *g_protected_tasks[] = {
    "idle",     // PID 0, 调度器必须
    "shell",    // Shell 自身
    "ui",       // UI 框架
    "taskmgr",  // 任务管理器自身
    NULL
};
```

保护逻辑：
- 在 `cmd_kill` 中检查任务名是否在保护列表中
- 若匹配则拒绝并返回 "cannot kill system task"

### 实现步骤

#### Step 1: 创建文件

```
src/app/taskmgr/
├── taskmgr.h        ← App 生命周期声明
└── taskmgr.c        ← 完整实现
```

#### Step 2: `taskmgr.h`

```c
#ifndef TASKMGR_H
#define TASKMGR_H
void taskmgr_init(void);
void taskmgr_loop(void);
void taskmgr_exit(void);
#endif
```

#### Step 3: `taskmgr.c` 实现

**状态机**：
```
IDLE → 显示任务列表 → 用户选择 → CONFIRM → kill → 刷新列表
```

**渲染**（在 `loop_function` 中）：
- 每帧清屏 + 绘制标题栏 "Task Manager"
- 遍历 `kern_task_list_head()` 绘制任务行
- 选择器高亮当前行
- 信息栏显示选中任务详情

**输入处理**：
- BtnA 短按：下一任务
- BtnB 短按：上一任务  
- BtnA 长按：终止选中任务（带确认弹窗）
- BtnB 长按：退出任务管理器

**确认弹窗**：使用 `xerintosh_push_pop_up("Kill <name>? Hold A to confirm")` + 倒计时

#### Step 4: `cmd_kill` 增强

在 `kern_shell_cmds.c:cmd_kill()` 中添加：
```c
// 检查保护列表
static bool is_protected_task(const char *name) {
    const char *protected[] = {"idle", "shell", "ui", "taskmgr", NULL};
    for (int i = 0; protected[i] != NULL; i++) {
        if (strcmp(name, protected[i]) == 0) return true;
    }
    return false;
}

if (is_protected_task(task->name)) {
    sh_println(tty, "kill: cannot kill system task");
    return;
}
```

#### Step 5: `app_init.c` 菜单注册

将"关于"替换为"任务管理器"：
```c
xerintosh_list_item_t* item2 = xerintosh_new_user_item(
    "任务管理器", taskmgr_init, taskmgr_loop, taskmgr_exit, user_icon);
```

#### Step 6: `CMakeLists.txt` / build 配置

在 `platformio.ini` 或构建脚本中添加 `src/app/taskmgr/` 到 include 路径。

### 验证标准

| 测试 | 方法 | 预期 |
|------|------|------|
| 进入任务管理器 | 菜单选择"任务管理器" | 显示所有运行中任务 |
| 查看任务列表 | 浏览列表 | 显示 idle/shell/ui/wifi-mgr/bt-mgr/taskmgr |
| 终止普通任务 | 用脚本 spawn 测试任务 → kill | 任务消失，内存回收 |
| 拒绝系统任务 | 尝试 kill idle/shell/ui | "cannot kill system task" |
| Shell kill 保护 | `kill 0` / `kill 1` | "cannot kill system task" |

---

## Phase 5: 代码清理 + 文档更新 + 提交

### Step 1: 代码清理

| 操作 | 文件 | 说明 |
|------|------|------|
| 拆分 | `kern_shell_cmds.c` (673 行) | 拆为 `_file_cmds.c` (文件操作) + `_sys_cmds.c` (系统命令)，每个 <400 行 |
| 清理 | `kern_task.c` (778 行) | 提取 `kern_task_virtual.c`（虚任务逻辑） |
| 更新 | `kern_procfs.c` | 新增 `/proc/developer` 文件类型 |
| 更新 | `kern_shell_cmds.c` | 更新 `cmd_uname()` 版本号来源 + `cmd_kill()` 保护列表 |

### Step 2: 文档更新

| 新建文档 | 说明 |
|----------|------|
| `doc/kernel/kern-gpiofs.md` | GPIO 文件系统架构、引脚表、读写协议 |
| `doc/kernel/kern-version.md` | 版本号管理规范、开发者信息 |
| `doc/app/taskmgr.md` | 任务管理器使用说明、保护机制 |

| 更新文档 | 变更 |
|----------|------|
| `doc/index.md` | 添加 gpiofs / version / taskmgr 索引条目 |
| `doc/kernel/index.md` | 添加 GPIO 文件系统章节 |
| `doc/kernel/kern-procfs-sysfs.md` | 补充 `/sys/gpio` 和 `/proc/developer` |
| `doc/kernel/kern-task.md` | 补充虚任务概念 |

### Step 3: 提交

```
git add -A
git commit -m "feat: Phase 4-5 — App kernel visibility, GPIO bridge, version info, task manager

Phase 4a: Virtual task registration for user_item apps (/proc/tasks visibility)
Phase 4b: GPIO filesystem bridge (/sys/gpio/*)
Phase 4c: Centralized version header (kern_version.h) + /proc/developer
Phase 4d: Task manager app with kill protection for system tasks
Phase 5: Code split (kern_shell_cmds → _file/_sys), docs update

Co-Authored-By: deepseek-v4-pro <deepseek-ai@claude-code-best.win>"
```

---

## 依赖关系

```
Phase 4c (版本信息)  ← 无依赖，可最先实施
        ↓
Phase 4a (App 内核化) ← 依赖 kern_task 现有 API
        ↓
Phase 4d (任务管理器) ← 依赖 Phase 4a 虚任务 + Phase 4c 保护列表
        ↓
Phase 4b (GPIO 桥接)  ← 独立，可与 4a/4c/4d 并行
        ↓
Phase 5 (清理+文档)   ← 依赖以上全部
```

---

## 风险与应对

| 风险 | 等级 | 应对 |
|------|------|------|
| 虚任务 ZOMBIE 后 TCB 内存泄漏 | 低 | 在 `kern_sched_tick()` 中自动回收 ZOMBIE 虚任务 |
| GPIO 写入导致硬件冲突（如误写背光引脚） | 中 | 仅允许在未使用的引脚上写操作；G26/G36/G37 设为只读 |
| 任务管理器终止自身 | 中 | 保护列表包含 "taskmgr"，且退出前先 unregister 虚任务 |
| kern_shell_cmds.c 拆分后链接错误 | 低 | 保持所有 static 函数可见性，通过新的 internal header 共享 |

---

## 文件变更清单

### 新建文件 (8)
| 文件 | 行数预估 |
|------|---------|
| `src/kernel/kern_version.h` | ~30 |
| `src/kernel/kern_gpiofs.h` | ~35 |
| `src/kernel/kern_gpiofs.c` | ~250 |
| `src/app/taskmgr/taskmgr.h` | ~20 |
| `src/app/taskmgr/taskmgr.c` | ~280 |
| `doc/kernel/kern-gpiofs.md` | ~80 |
| `doc/kernel/kern-version.md` | ~40 |
| `doc/app/taskmgr.md` | ~60 |

### 修改文件 (12)
| 文件 | 变更量 |
|------|--------|
| `src/kernel/kern_task.h` | +15 行（虚任务 API） |
| `src/kernel/kern_task.c` | +50 行（虚任务实现 + ZOMBIE 回收） |
| `src/ui/ui_item.h` | +2 行（kernel_pid 字段） |
| `src/ui/ui_core.c` | +10 行（enter/exit 时注册/注销虚任务） |
| `src/kernel/kern_shell_cmds.c` | 拆分 + `cmd_kill` 保护列表 + `cmd_uname` 版本号更新 |
| `src/kernel/kern_procfs.c` | +15 行（新增 /proc/developer + 版本号来源更新） |
| `src/kernel/kern_procfs.h` | +2 行（developer 文件类型枚举） |
| `src/app/app_init.c` | -1+1（"关于" → "任务管理器"） |
| `src/main.cpp` | +3 行（gpiofs_init 调用） |
| `doc/index.md` | +15 行 |
| `doc/kernel/index.md` | +8 行 |
| `doc/kernel/kern-procfs-sysfs.md` | +15 行 |
| `doc/kernel/kern-task.md` | +10 行 |

---

## 总代码量估算

| 类别 | 行数 |
|------|------|
| 新增代码 | ~800 行 |
| 修改代码 | ~120 行 |
| 文档 | ~250 行 |
| **合计** | **~1170 行** |

---

*本计划待用户确认后开始实施。所有代码变更将遵循 `doc/coding-style.md` 规范。*
