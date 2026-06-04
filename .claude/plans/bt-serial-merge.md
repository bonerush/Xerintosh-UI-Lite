# 蓝牙串口整合到串口监视器 — 实现计划

## 目标
将独立的 `ble_serial` App 删除，其功能整合到 `serial_monitor` App 中。用 **SER/BLE** 模式切换替代现有的 **NORM/DEBUG** 模式切换。

## 影响范围

| 类别 | 文件 | 操作 |
|------|------|------|
| 核心修改 | `sm_app.h` | 重写状态模型 |
| 核心修改 | `sm_app.cpp` | 重写生命周期 + 数据读取 |
| 核心修改 | `sm_ui.c` | 重写信息栏 + 终端前缀 |
| 菜单清理 | `app_init.c` | 删除蓝牙串口菜单项 |
| 测试删除 | `test/test_native/test_ble_serial.cpp` | 删除 |
| 源码删除 | `src/app/ble_serial/ble_serial.h` | 删除 |
| 源码删除 | `src/app/ble_serial/ble_serial.cpp` | 删除 |
| 文档更新 | `doc/app/serial-monitor.md` | 全面重写 |
| 文档更新 | `doc/index.md` | 删除 ble_serial 引用 |

## 实现顺序

```
Step 1: sm_app.h          ← 先定义新类型
Step 2: sm_app.cpp         ← 核心逻辑
Step 3: sm_ui.c            ← UI 适配
Step 4: app_init.c         ← 菜单清理
Step 5: 删除 ble_serial/   ← 清理死代码
Step 6: 测试               ← 更新测试
Step 7: 文档               ← 最后更新
```

## 关键设计决策

1. **数据源枚举**: `SM_SOURCE_SER` (有线串口) / `SM_SOURCE_BLE` (蓝牙串口)
2. **`serial_monitor_is_active()` 语义变更**: 仅 SER 模式运行时阻止 dev_ttyS0 消费 UART
3. **BLE 回调注册**: 在 `serial_monitor_init()` 中注册，`serial_monitor_exit()` 中注销
4. **切换源时清空缓冲区**: 避免两种数据源的数据混在一起
5. **并发安全**: 保持无锁模式，与原 `ble_serial.cpp` 做法一致

## 风险点

1. **`serial_monitor_is_active()` 语义变更** (高风险): 如果遗漏，BLE 模式下 Shell 将无法正常工作
2. **BLE 回调注册时机** (中风险): 用户从未进入串口监视器 App 时，回调为 NULL，BT 数据丢弃（预期行为）
3. **Buffer 切换时清空** (低风险): 切换 SER↔BLE 时清空缓冲区
