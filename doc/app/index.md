# App 层索引

> **Parent:** [知识地图](../index.md)

## 概述

App 层是用户应用代码所在。每个 App 在独立子目录中，通过 `app_init.c` 挂载到 UI 菜单树。

## 模块列表

| 模块 | 文档 | 源码 | 说明 |
|------|------|------|------|
| 应用初始化 | [app-init.md](app-init.md) | `src/app/app_init.c/h` | 入口封装，委托菜单构建、输入处理与管理器初始化 |
| 菜单构建 | [app-menu.md](app-menu.md) | `src/app/app_menu.c/h` | Xerintosh UI 菜单树构造 |
| 输入处理 | [app-input.md](app-input.md) | `src/app/app_input.c/h` | 按键输入路由与状态机调度 |
| 全局状态 | [app-state.md](app-state.md) | `src/app/app_state.c/h` | 跨模块全局状态（`g_wifi_on`、`g_bt_on`） |
| 统一内存视图 | [app-mem.md](app-mem.md) | `src/app/app_mem.c/h` | 包装内核内存统计，提供保留水位感知的安全分配判断 |
| UI 公共服务 | [ui-service.md](ui-service.md) | `src/app/ui_service.c/h` | `user_item` 生命周期公共辅助 + 横屏切换 helper |
| 设置管理 | [settings.md](settings.md) | `src/app/settings/settings.c/h` | 亮度/动画/方向/波特率配置与存储 |
| 任务管理器 | [taskmgr.md](taskmgr.md) | `src/app/taskmgr/` | 任务查看与终止（动画行列表 + 横屏3行布局） |
| 串口监视器 | [serial-monitor.md](serial-monitor.md) | `src/app/serial_monitor/` | 串口监视器 App（SER/BLE 双数据源 + 入场滑入动画 + 按钮平滑过渡） |
| 烧录器 | [flasher.md](flasher.md) | `src/app/flasher/` | USB↔UART 有线桥接器（STK500/ESP32 协议自动识别 + GPIO 引脚映射 + 全屏进度条） |
| 开机画面 | [boot.md](boot.md) | `src/app/boot/` | Macintosh 128K 风格开机动画 |
| 关机屏幕 | [shutdown.md](shutdown.md) | `src/app/shutdown/` | 关机画面 + 电源键长按弹窗 |
| 示波器 | [oscilloscope.md](oscilloscope.md) | `src/app/oscilloscope/` | G36 单通道示波器 App |
| Token Usage | [token-usage.md](token-usage.md) | `src/app/token_usage/` | Token 用量统计 |
| 关于页面 | [about.md](about.md) | `src/app/about/` | 版本/Logo/开发者信息 |
| 存储 | [storage.md](storage.md) | `src/app/storage.cpp/h` | NVS 持久化存储封装 |
| WiFi 管理 | [wifi.md](wifi.md) | `src/app/wifi/wifi_manager.cpp/h` | WiFi 状态机（扫描/连接/密码输入） |
| WiFi 菜单 | — | `src/app/wifi/wifi_menu.c/h` | 网络列表菜单构建（已保存/可用 AP 动态重建） |
| 蓝牙管理 | [bluetooth.md](bluetooth.md) | `src/app/bluetooth/bt_manager.cpp/h` | 蓝牙管理器（Classic BT SPP） |
| 蓝牙 UART | [bluetooth.md](bluetooth.md) | `src/app/bluetooth/bt_uart_service.cpp/h` | BT UART 串口服务（SPP） |
| 烧录器协议 | — | `src/app/flasher/flasher_proto.c/h` | STK500/ESP32 SLIP 协议解析引擎（提取自 flasher_app.cpp） |
| 串口输入 | [serial-input.md](serial-input.md) | `src/app/serial_input/serial_input.cpp/h` | 串口 CLI 输入（WiFi/蓝牙密码） |
| UI 任务 | [ui-task.md](ui-task.md) | `src/app/ui_task.c` | Xerintosh UI 内核任务包装（输入→渲染→yield） |
| 服务管理助手 | [svc-mgr-helper.md](svc-mgr-helper.md) | `src/app/svc_mgr_helper.c/h` | 系统服务懒加载助手（BT enable/disable） |

---

> **See Also:** [HAL 层](../hal/index.md) | [UI 核心层](../ui/index.md) | [内核层](../kernel/index.md)
