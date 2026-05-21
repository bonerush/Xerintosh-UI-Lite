# Xerintosh UI for M5Stick-C 知识地图

## 架构总览

本项目是将 `reference/oled-ui-Xerintosh-lite`（128×64 OLED 菜单框架）移植到 **M5Stick-C**（80×160 TFT，ESP32-PICO）的固件项目。

采用 **分层架构**：

```
Project Root
├── HAL 层（硬件抽象）
│   ├── [显示驱动](hal/display.md)      ← TFT 双缓冲 / Native 内存帧缓冲
│   ├── [输入系统](hal/input.md)        ← 按键消抖 + 短按/长按/双击状态机
│   └── [系统时钟](hal/system.md)       ← tick 与延时
├── UI 核心层（纯 C）
│   ├── [绘制驱动适配](ui/draw-driver.md)  ← oled_* 宏映射到 HAL API
│   ├── [项目系统](ui/item.md)          ← 列表项、开关、滑条、按钮、用户页
│   ├── [核心引擎](ui/core.md)          ← 动画插值、相机、选择器、主循环
│   └── [绘制管线](ui/drawer.md)        ← 列表外观、选择器高亮、弹窗、信息栏
├── App 层
│   ├── [设置管理](app/settings.md)     ← 亮度/动画/方向配置与存储
│   ├── [应用初始化](app/app-init.md)   ← 菜单构建、管理器初始化、输入处理
│   └── [编码风格规范](coding-style.md) ← C OOP 命名、封装、继承规范
└── 入口
    ├── src/main.cpp                   ← M5Stick-C 实际入口
    └── src/native_main.cpp            ← GoogleTest 桌面测试入口
```

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

## 文档树

### HAL 层
- **[显示驱动](hal/display.md)** — TFT/Native 双实现、绘制原语、XOR 矩形
- **[输入系统](hal/input.md)** — 按键状态机、消抖、事件派发
- **[系统时钟](hal/system.md)** — `millis()` 封装、`std::chrono` 桌面回退

### UI 核心层
- **[绘制驱动适配](ui/draw-driver.md)** — 从 `oled_*` 到 `hal_*` 的宏桥接层
- **[项目系统](ui/item.md)** — 列表项类型层次、构造与挂载、选择器与相机
- **[核心引擎](ui/core.md)** — 动画系统、相机滚动、主循环调度
- **[绘制管线](ui/drawer.md)** — 列表外观绘制、选择器渲染、弹窗与信息栏

## 快速链接

- [主入口点](../src/main.cpp)
- [测试入口点](../src/native_main.cpp)
- [PlatformIO 配置](../platformio.ini)
