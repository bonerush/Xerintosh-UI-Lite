# Xerintosh UI for M5Stick-C 知识地图

## 架构总览

本项目是将 `reference/oled-ui-Xerintosh-lite`（128×64 OLED 菜单框架）移植到 **M5Stick-C**（80×160 TFT，ESP32-PICO）的固件项目。

采用 **四层架构**：

```
Project Root
├── Xeros 内核层（协作式微内核）
│   ├── [类型系统与日志](kernel/kern-types.md)  ← 错误码、常量、日志框架
│   ├── [调度器](kernel/kern-task.md)           ← 协作式 Round-Robin + 动态栈
│   ├── [VFS 核心](kernel/kern-vfs.md)          ← inode / dentry / file 三级结构
│   ├── [设备文件系统](kernel/kern-devfs.md)     ← /dev/ 设备注册
│   ├── [/proc 与 /sys](kernel/kern-procfs-sysfs.md) ← 虚拟文件系统（含 /sys/gpio）
│   ├── [GPIO 桥接](kernel/kern-gpiofs.md)       ← /sys/gpio 引脚状态映射
│   ├── [版本信息](kernel/kern-version.md)        ← 版本号与开发者信息管理
│   ├── [可移植层](kernel/kern-port.md)          ← 调度原语抽象（FreeRTOS/原生双后端）
│   ├── [IPC 机制](kernel/kern-ipc.md)           ← pipe + 命名消息队列
│   ├── [系统调用](kernel/kern-syscall.md)       ← 统一 syscall 分发与封装
│   ├── [内核 Shell](kernel/kern-shell.md)        ← 串口交互式命令行
│   └── [物理设备](kernel/kern-devices.md)       ← /dev/fb0, /dev/input0, /dev/ttyS0
├── HAL 层（硬件抽象）
│   ├── [显示驱动](hal/display.md)      ← TFT 双缓冲 / Native 内存帧缓冲
│   ├── [输入系统](hal/input.md)        ← 按键消抖 + 短按/长按/双击状态机
│   └── [系统时钟](hal/system.md)       ← tick 与延时
├── UI 核心层（纯 C）— 2026年6月重构
│   ├── [项目系统](ui/item.md)          ← 列表项、开关、滑条、按钮、用户页（基类/树/选择器）
│   ├── [核心引擎](ui/core.md)          ← 动画插值、生命周期管理、主循环调度
│   ├── [类型派发表](ui/dispatch.md)    ← 函数指针数组 O(1) 类型路由（替代内联 switch）
│   ├── [全局上下文](ui/context.md)     ← 单例状态容器、向后兼容宏、退场动画状态
│   ├── [绘制管线](ui/drawer.md)        ← 列表外观、选择器高亮、弹窗、信息栏、退场动画、图标
│   └── [行列表动画工具](ui/ui-anim-row.md) ← 可复用行列表动画（入场滑入+高亮过渡）
├── App 层（每个 App 独立子目录）
│   ├── app_init.c/h        ← 菜单构建、管理器初始化、输入处理
│   ├── boot/               ← 开机画面
│   ├── settings/           ← 亮度/动画/方向设置 + 旋转兼容转换
│   ├── storage/            ← NVS 持久化存储
│   ├── wifi/               ← WiFi 状态机（扫描/连接/密码输入）
│   ├── bluetooth/          ← 蓝牙管理器（NimBLE 扫描/配对）
│   ├── serial_input/       ← 串口 CLI 输入（WiFi 密码/蓝牙配对码）
│   ├── serial_monitor/     ← 串口监视器 App（缓冲区/状态机/界面）
│   ├── taskmgr/             ← 任务管理器 App（进程列表/终止/保护）
│   ├── about/              ← 关于页面（版本/Logo/开发者信息）
│   ├── ui_task.c           ← UI 内核任务包装（输入→渲染→yield）
│   └── svc_mgr_helper.c/h  ← WiFi/BT 共享开关切换抽象
├── [编码风格规范](coding-style.md)     ← C OOP 命名、封装、继承规范
├── [内核优化分析](kernel-optimization-analysis.md) ← 7 模块诊断+方案+收益
├── 教程
│   └── [从零开始创建 App](tutorials/your-first-app.md) ← 面向初学者的 App 开发教程
└── 入口
    ├── src/main.cpp                   ← M5Stick-C 实际入口
    └── src/native_main.cpp            ← GoogleTest 桌面测试入口
```

## 架构层次关系

```
┌─────────────────────────────────────────────┐
│ App 层  (user_item, settings, wifi, bt...)  │  用户代码
├─────────────────────────────────────────────┤
│ UI 核心层 (item / core / drawer / driver)    │  菜单框架
├─────────────────────────────────────────────┤
│ Xeros 内核层                                 │
│  ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐      │
│  │Sched  │ │ VFS  │ │ devfs│ │ IPC  │       │  协作式微内核
│  │coop   │ │(vrtl)│ │(dev) │ │(pipe)│      │
│  └──────┘ └──────┘ └──────┘ └──────┘      │
│  ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐      │
│  │sysfs  │ │procfs│ │Shell │ │Syscll│      │  "一切皆文件"
│  └──────┘ └──────┘ └──────┘ └──────┘      │
├─────────────────────────────────────────────┤
│ HAL 层 (display / input / system)           │  硬件抽象
├─────────────────────────────────────────────┤
│ FreeRTOS + Arduino (WiFi / BT 协议栈)       │  底层运行时
└─────────────────────────────────────────────┘
```

FreeRTOS 继续在底层为 WiFi/BT 协议栈服务；Xeros 内核运行在 Arduino `loop()` 的单一线程内，作为"逻辑进程层"，与底层不冲突。

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
- **协作式微内核**：Round-Robin 调度 + 动态栈管理 + VFS"一切皆文件" + pipe/mq IPC

## 文档树

### 内核层
- **[内核子系统总览](kernel/index.md)** — 架构总览、子模块导航
- **[类型系统与日志](kernel/kern-types.md)** — 错误码、常量定义、日志框架
- **[协作式调度器](kernel/kern-task.md)** — 任务控制块、Round-Robin、动态栈管理
- **[VFS 虚拟文件系统](kernel/kern-vfs.md)** — inode/dentry/file 三级结构与路径解析
- **[设备文件系统](kernel/kern-devfs.md)** — /dev/ 设备注册流程
- **[/proc 与 /sys](kernel/kern-procfs-sysfs.md)** — 内核状态信息与系统配置文件系统
- **[GPIO 桥接](kernel/kern-gpiofs.md)** — /sys/gpio 引脚状态映射与读写
- **[版本信息管理](kernel/kern-version.md)** — 版本号与开发者信息集中管理
- **[可移植层](kernel/kern-port.md)** — 调度原语抽象（FreeRTOS/原生双后端）
- **[IPC 进程间通信](kernel/kern-ipc.md)** — 匿名 pipe + 命名消息队列
- **[系统调用接口](kernel/kern-syscall.md)** — 统一分发器与用户态封装
- **[内核 Shell](kernel/kern-shell.md)** — 串口交互式命令行（30+ 命令，含 scope/top/param 等）
- **[物理设备驱动](kernel/kern-devices.md)** — /dev/fb0 / /dev/input0 / /dev/ttyS0
- **[Shell 命令实现](kernel/kern-shell-cmds.md)** — 30+ 内置命令与动态注册
- **[Shell 解析器](kernel/kern-shell-parser.md)** — 输入行解析（引号支持、参数分割）
- **[内核优化分析](kernel-optimization-analysis.md)** — 7 模块诊断+方案+代码+收益

### App 层
- **[应用初始化](app/app-init.md)** — 菜单树构建、管理器初始化、按键映射
- **[设置管理](app/settings.md)** — 亮度/动画/方向/波特率配置与存储
- **[任务管理器](app/taskmgr.md)** — 进程列表查看与安全终止（动画行列表 + 横屏 3 行）
- **[串口监视器](app/serial-monitor.md)** — 串口数据监视（入场滑入动画 + 按钮平滑过渡）
- **[UI 任务](app/ui-task.md)** — 内核任务包装的 UI 主循环
- **[服务管理助手](app/svc-mgr-helper.md)** — WiFi/BT 共享开关切换抽象

### 入门教程
- **[从零开始创建 App](tutorials/your-first-app.md)** — 面向初学者的完整教程，手把手教你创建第一个 `user_item` App

### HAL 层
- **[显示驱动](hal/display.md)** — TFT/Native 双实现、绘制原语、XOR 矩形
- **[输入系统](hal/input.md)** — 按键状态机、消抖、事件派发
- **[系统时钟](hal/system.md)** — `millis()` 封装、`std::chrono` 桌面回退

### UI 核心层
- **[绘制驱动适配](ui/draw-driver.md)** — ⚠️ 已移除：原 `oled_*` → `hal_*` 宏桥接层，重构中删除
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
