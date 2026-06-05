# Xerintosh UI for M5Stick-C 知识地图

## 架构总览

本项目是将 `reference/oled-ui-Xerintosh-lite`（128×64 OLED 菜单框架）移植到 **M5Stick-C**（80×160 TFT，ESP32-PICO）的固件项目。

采用 **三层架构**：

```
Project Root
├── 内核层: FreeRTOS（ESP32 原生抢占式调度器）
│   └── [版本信息](kernel/kern-version.md)      ← 版本兼容宏（kern_version_compat.h）
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
│   ├── bluetooth/          ← 蓝牙管理器（NimBLE 扫描/配对）
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

```
┌──────────────────────────────────────────────────────────────┐
│ App 层  (user_item, settings, wifi, bt...)                    │
├──────────────────────────────────────────────────────────────┤
│ UI 核心层 (item / core / drawer / context)                    │
├──────────────────────────────────────────────────────────────┤
│ HAL 层 (display / input / system)                            │
├──────────────────────────────────────────────────────────────┤
│ FreeRTOS (ESP32 原生抢占式调度器)                             │
└──────────────────────────────────────────────────────────────┘
```

FreeRTOS 是 ESP32 Arduino 框架的原生调度器；所有 App 任务通过 `xTaskCreatePinnedToCore()` 创建。

> **架构图表:** [任务生命周期](assets/diagrams/task-lifecycle.drawio)

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
- **FreeRTOS 调度**：所有任务通过 `xTaskCreatePinnedToCore()` 创建，抢占式调度自动管理。

## 文档树

### 内核层
- **[版本信息](kernel/kern-version.md)** — 版本兼容宏（kern_version_compat.h）

### App 层
- **[应用初始化](app/app-init.md)** — 菜单树构建、管理器初始化、按键映射
- **[设置管理](app/settings.md)** — 亮度/动画/方向/波特率配置与存储
- **[任务管理器](app/taskmgr.md)** — FreeRTOS 任务查看与安全终止
- **[串口监视器](app/serial-monitor.md)** — 串口数据监视（入场滑入动画 + 按钮平滑过渡）
- **[UI 任务](app/ui-task.md)** — FreeRTOS 任务包装的 UI 主循环
- **[服务管理助手](app/svc-mgr-helper.md)** — WiFi/BT 共享开关切换抽象

### 入门教程
- **[从零开始创建 App](tutorials/your-first-app.md)** — 面向初学者的完整教程，手把手教你创建第一个 `user_item` App

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
