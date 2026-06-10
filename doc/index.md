# Xerintosh UI for M5Stick-C 知识地图

## 架构总览

本项目是将 `reference/oled-ui-Xerintosh-lite`（128×64 OLED 菜单框架）移植到 **M5Stick-C**（80×160 TFT，ESP32-PICO）的固件项目。

采用 **四层架构**（v2 内核架构图: [PNG](assets/diagrams/kernel-v2-architecture.drawio)）：

```
Project Root
├── Xeros 内核层（v2 可插拔微内核: SMP + MPU + 设备模型）[总览](kernel/index.md)
│   ├── 核心子系统 (v1)
│   │   ├── [初始化与日志](kernel/kern-init.md)     ← 分级日志、panic、启动序列
│   │   ├── [类型系统](kernel/kern-types.md)        ← 错误码、常量、枚举
│   │   ├── [抢占式调度器](kernel/kern-task.md)     ← TCB、yield/sleep/exit/spawn
│   │   ├── [VFS 核心](kernel/kern-vfs.md)          ← inode/dentry/file + fd_table
│   │   ├── [设备文件系统](kernel/kern-devfs.md)    ← /dev/ 目录与设备注册
│   │   ├── [/proc 与 /sys](kernel/kern-procfs-sysfs.md) ← 内核状态与配置接口
│   │   ├── [GPIO 桥接](kernel/kern-gpiofs.md)      ← /sys/gpio 引脚状态映射
│   │   ├── [可移植层](kernel/kern-port.md)         ← FreeRTOS/Native 双后端
│   │   └── [Shell 命令行](kernel/kern-shell.md)    ← 30+ 命令交互式终端
│   ├── v2 新增子系统
│   │   ├── [SMP 多核支持](kernel/kern-smp.md)      ← per-CPU 数据, 零开销退化
│   │   ├── [同步原语](kernel/kern-sync.md)         ← spinlock + mutex
│   │   ├── [可插拔调度类](kernel/kern-sched-class.md) ← class 注册/优先级链
│   │   ├── [Round-Robin 类](kernel/kern-sched-rr.md)   ← 默认两遍扫描调度
│   │   ├── [优先级 FIFO 类](kernel/kern-sched-fifo.md) ← 抢占式优先级调度
│   │   ├── [资源追踪](kernel/kern-resource.md)     ← 链表自动回收内存/FD/锁
│   │   ├── [内核分配器](kernel/kern-kmalloc.md)     ← kmalloc/kfree 统一分配
│   │   ├── [MPU 内存保护](kernel/kern-mpu.md)      ← 栈守卫 + 区域隔离
│   │   ├── [设备驱动模型](kernel/kern-device-model.md) ← kern_device_ops_t + VFS bridge
│   │   └── [版本信息](kernel/kern-version.md)       ← 版本号与开发者管理
├── HAL 层（硬件抽象）
│   ├── [显示驱动](hal/display.md)      ← TFT 双缓冲 / Native 内存帧缓冲
│   ├── [输入系统](hal/input.md)        ← 按键消抖 + 短按/长按/双击状态机
│   └── [系统时钟](hal/system.md)       ← tick 与延时
├── UI 核心层（纯 C）— 2026年6月重构
│   ├── [项目系统](ui/item.md)          ← 列表项、开关、滑条、按钮、用户页（基类/树/选择器）
│   ├── [核心引擎](ui/core.md)          ← 动画插值、生命周期管理、主循环调度
│   ├── [类型派发表](ui/dispatch.md)    ← 函数指针数组 O(1) 类型路由（替代内联 switch）
│   ├── [全局上下文](ui/context.md)     ← 单例状态容器、向后兼容宏、退场动画状态
│   └── [行列表动画工具](ui/ui-anim-row.md) ← 可复用行列表动画（入场滑入+高亮过渡）
├── App 层（每个 App 独立子目录）
│   ├── app_init.c/h        ← 菜单构建、管理器初始化、输入处理
│   ├── boot/               ← 开机画面
│   ├── settings/           ← 亮度/动画/方向设置 + 旋转兼容转换
│   ├── storage/            ← NVS 持久化存储
│   ├── wifi/               ← WiFi 状态机（扫描/连接/密码输入）
│   ├── bluetooth/          ← 蓝牙管理器（Classic BT SPP）
│   ├── serial_input/       ← 串口 CLI 输入（WiFi 密码/蓝牙配对码）
│   ├── serial_monitor/     ← 串口监视器 App（缓冲区/状态机/界面）
│   ├── taskmgr/             ← 任务管理器 App（进程列表/终止/保护）
│   ├── about/              ← 关于页面（版本/Logo/开发者信息）
│   ├── ui_task.c           ← UI 内核任务包装（输入→渲染→yield）
│   └── svc_mgr_helper.c/h  ← WiFi/BT 共享开关切换抽象
├── [编码风格规范](coding-style.md)     ← C OOP 命名、封装、继承规范
├── 教程
│   └── [从零开始创建 App](tutorials/your-first-app.md) ← 面向初学者的 App 开发教程
└── 入口
    ├── src/main.cpp                   ← M5Stick-C 实际入口
    └── src/native_main.cpp            ← GoogleTest 桌面测试入口
```

## 架构层次关系

![Xeros 内核 v2 架构总览](assets/diagrams/kernel-v2-architecture.png)

```
┌──────────────────────────────────────────────────────────────┐
│ App 层  (user_item, settings, wifi, bt...)                    │
├──────────────────────────────────────────────────────────────┤
│ UI 核心层 (item / core / drawer / context)                    │
├──────────────────────────────────────────────────────────────┤
│ Xeros 内核 v2 (可插拔微内核: SMP + MPU + 设备模型)            │
│  ┌─────────┐ ┌─────────┐ ┌──────────┐ ┌───────────┐        │
│  │Scheduler│ │   VFS   │ │  devfs   │ │  procfs   │        │
│  │ ┌─────┐ │ │(inode)  │ │ (/dev/*) │ │(/proc/*)  │        │
│  │ │ RR  │ │ └─────────┘ └──────────┘ └───────────┘        │
│  │ │FIFO*│ │ ┌─────────┐ ┌──────────┐ ┌───────────┐        │
│  │ └─────┘ │ │  sysfs  │ │  Shell   │ │  gpiofs   │        │
│  │可插拔类 │ │(/sys/*)  │ │(30+ cmd) │ │/sys/gpio  │        │
│  └─────────┘ └─────────┘ └──────────┘ └───────────┘        │
│  ┌─────────┐ ┌─────────┐ ┌──────────┐ ┌───────────┐        │
│  │   SMP   │ │Resource │ │   MPU    │ │  Device   │        │
│  │ per-CPU │ │Tracking │ │ Stack    │ │  Model    │        │
│  └─────────┘ └─────────┘ └──────────┘ └───────────┘        │
│  ┌─────────┐ ┌─────────┐                                    │
│  │spinlock │ │ kmalloc │   ← v2 新增: 同步 + 统一分配       │
│  │& mutex  │ │(内核分配)│                                    │
│  └─────────┘ └─────────┘                                    │
├──────────────────────────────────────────────────────────────┤
│ HAL 层 (display / input / system)                            │
├──────────────────────────────────────────────────────────────┤
│ FreeRTOS + Arduino (WiFi / BT 协议栈)                        │
└──────────────────────────────────────────────────────────────┘
```

FreeRTOS 继续在底层为 WiFi/BT 协议栈服务。SMP 模式下 Xeros 在每个 CPU 上创建 FreeRTOS 任务运行调度循环，与底层不冲突。

> **架构图表:** [内核 v2 架构图](assets/diagrams/kernel-v2-architecture.drawio) | [任务生命周期](assets/diagrams/task-lifecycle.drawio) | [VFS Bridge](assets/diagrams/vfs-bridge.drawio) | [SMP 架构](assets/diagrams/smp-architecture.drawio) | [资源追踪链](assets/diagrams/resource-tracking.drawio) | [kmalloc 布局](assets/diagrams/kmalloc-layout.drawio) | [调度类优先级链](assets/diagrams/sched-class-chain.drawio)

## 渲染管线（每帧顺序）

1. 清除后台缓冲区
2. 绘制背景层（列表项、控件）
3. 绘制选择器（XOR 反色高亮）
4. 绘制弹窗/信息栏（widget）
5. DMA 刷新到屏幕（`pushSprite`）
6. 交换缓冲区

采用**全帧重绘**，目标 60fps。

## 关键技术点

- **TFT 双缓冲**：使用 `M5Canvas` 作为后台缓冲区，必须传入父显示对象 `new M5Canvas(&M5.Display)`
- **XOR 选择器高亮**：TFT 不支持 OLED 的 `draw_color(2)` 反色，改用像素级 `color ^ 0xFFFF`
- **C 风格面向对象**：基类 `xerintosh_list_item_t` 作为结构体第一个成员，派生类通过强制类型转换实现多态
- **动画插值公式**：`current += (target - current) / (100.0f - speed)`
- **抢占式微内核**：可插拔调度类 + 动态栈管理 + VFS"一切皆文件"

## 文档树

### 内核层
- **[内核子系统总览](kernel/index.md)** — v2 架构总览、模块导航
- **[初始化与日志](kernel/kern-init.md)** — 内核启动入口、分级日志、panic 处理
- **[类型系统](kernel/kern-types.md)** — 错误码、PID、任务状态、CPU 标识
- **[抢占式调度器](kernel/kern-task.md)** — TCB、yield/sleep/exit/spawn、虚任务
- **[SMP 多核支持](kernel/kern-smp.md)** — per-CPU 数组、宏零开销退化、多核启动
- **[同步原语](kernel/kern-sync.md)** — spinlock_t + mutex_t、原子操作、单核退化
- **[可插拔调度类](kernel/kern-sched-class.md)** — 调度类接口、注册与优先级链
- **[Round-Robin 调度](kernel/kern-sched-rr.md)** — 默认两遍扫描、时间片递减
- **[优先级 FIFO](kernel/kern-sched-fifo.md)** — 优先级排序入队、抢占触发
- **[VFS 虚拟文件系统](kernel/kern-vfs.md)** — inode/dentry/file 三级结构、fd_table
- **[设备文件系统](kernel/kern-devfs.md)** — /dev/ 设备注册（新旧 API 双轨）
- **[设备驱动模型](kernel/kern-device-model.md)** — kern_device_ops_t + VFS bridge 桥接
- **[/proc 与 /sys](kernel/kern-procfs-sysfs.md)** — 内核状态信息与系统配置
- **[GPIO 桥接](kernel/kern-gpiofs.md)** — /sys/gpio 引脚状态映射
- **[资源追踪](kernel/kern-resource.md)** — 链表自动回收内存/互斥锁/FD
- **[内核分配器](kernel/kern-kmalloc.md)** — kmalloc/kfree 统一分配 + 元数据头
- **[MPU 内存保护](kernel/kern-mpu.md)** — 区域配置/栈守卫/ESP32 PMS 编码
- **[可移植层](kernel/kern-port.md)** — FreeRTOS/Native 多后端调度原语
- **[内核 Shell](kernel/kern-shell.md)** — 串口交互式命令行（30+ 命令）
- **[Shell 命令实现](kernel/kern-shell-cmds.md)** — 30+ 内置命令与动态注册
- **[Shell 解析器](kernel/kern-shell-parser.md)** — 输入行解析（引号支持）
- **[物理设备驱动](kernel/kern-devices.md)** — /dev/fb0 / /dev/input0 / /dev/ttyS0
- **[版本信息](kernel/kern-version.md)** — 版本号与开发者信息管理

### App 层
- **[应用初始化](app/app-init.md)** — 菜单树构建、管理器初始化、按键映射
- **[设置管理](app/settings.md)** — 亮度/动画/方向/波特率配置与存储
- **[任务管理器](app/taskmgr.md)** — 进程列表查看与安全终止（动画行列表 + 横屏 3 行）
- **[串口监视器](app/serial-monitor.md)** — 串口数据监视（入场滑入动画 + 按钮平滑过渡）
- **[UI 任务](app/ui-task.md)** — 内核任务包装的 UI 主循环
- **[服务管理助手](app/svc-mgr-helper.md)** — WiFi/BT 共享开关切换抽象

### 入门教程
- **[从零开始创建 App](tutorials/your-first-app.md)** — 面向初学者的完整教程，手把手教你创建第一个 `user_item` App
- **[API 调用模板](tutorials/api-templates.md)** — 12 个可直接复制的 API 调用模板，涵盖 UI 组件、服务模块、HAL 层与常见陷阱

### HAL 层
- **[显示驱动](hal/display.md)** — TFT/Native 双实现、绘制原语、XOR 矩形
- **[输入系统](hal/input.md)** — 按键状态机、消抖、事件派发
- **[系统时钟](hal/system.md)** — `millis()` 封装、`std::chrono` 桌面回退

### UI 核心层
🛈 原 `draw-driver` 模块已在 UI 重构中移除，其 `oled_*` → `hal_*` 桥接功能已分散到各调用方
- **[项目系统](ui/item.md)** — 列表项类型层次、构造与挂载、选择器与相机、确认/回退
- **[核心引擎](ui/core.md)** — 动画系统、生命周期管理、主循环调度、渲染分支
- **[类型派发表](ui/dispatch.md)** — 函数指针数组 O(1) 类型路由（替代内联 switch）
- **[全局上下文](ui/context.md)** — 单例状态容器、向后兼容宏、退场动画状态迁移
- **[绘制管线](ui/drawer.md)** — 列表外观绘制、选择器渲染、弹窗与信息栏、退场动画、图标
- **[行列表动画工具](ui/ui-anim-row.md)** — 可复用行列表动画（入场滑入 + 高亮平滑过渡）

## 快速链接

- [主入口点](../src/main.cpp)
- [测试入口点](../src/native_main.cpp)
- [PlatformIO 配置](../platformio.ini)
- [编码风格规范](coding-style.md)
- [开发者指南](developer-guide.md)
