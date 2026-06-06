# 烧录器 (USB Serial Flasher)

## 功能概述

M5Stick-C 作为 ESP32 离线烧录器，通过蓝牙串口接收固件数据，然后通过 GPIO+UART 烧录到目标 ESP32。

## 引脚映射

在 **设置 -> 烧录器引脚** 中配置可用引脚的角色：

| 引脚 | 默认角色 | 可映射角色 | 限制 |
|------|---------|-----------|------|
| G0   | BOOT    | TX/RX/DTR/RTS/BOOT/未分配 | 输出 |
| G26  | TX      | TX/RX/DTR/RTS/BOOT/未分配 | 输出 |
| G36  | RX      | RX/未分配 | **仅输入** |

- 每个角色在同一时间只能分配给一个引脚
- 修改后会自动保存到 NVS
- 角色冲突时自动清除旧引脚的角色

## 使用流程

1. **连接目标 ESP32**：将 M5Stick-C 的 G0/G26/G36 引脚按配置连接到目标 ESP32 的对应引脚
2. **进入烧录器 App**：在主菜单选择 **烧录器**
3. **蓝牙连接**：通过手机或电脑的蓝牙串口工具连接 M5Stick-C
4. **发送固件**：
   - 固件格式：4 字节小端序文件大小 + 原始固件数据
   - 通过蓝牙串口发送
5. **开始烧录**：长按 **BtnA**（M5 按钮）开始烧录
6. **观察进度**：全屏进度条实时显示烧录进度
   - LOADING...：正在烧录（跑马灯动画）
   - SUCCESS!：烧录成功（绿色）
   - FAILED：烧录失败（红色）
7. **退出**：长按 **BtnB** 退出 App

## 烧录协议

- **协议**：ESP32 ROM Bootloader SLIP 协议
- **波特率**：115200
- **数据块大小**：1KB (0x400 bytes)
- **默认烧录地址**：0x10000（用户应用分区起始地址）
- **时序**：
  1. 进入下载模式：BOOT=LOW → RTS=LOW → 延时 100ms → RTS=HIGH → 延时 100ms
  2. 发送 SYNC 命令建立通信
  3. 发送 FLASH_BEGIN 命令（含固件大小、块数、块大小、地址）
  4. 逐块发送 FLASH_DATA 命令（含校验和）
  5. 发送 FLASH_END 命令完成烧录
  6. 复位目标 ESP32 进入正常启动模式

## 文件结构

| 文件 | 职责 |
|------|------|
| `src/app/flasher/flasher.h` | 公共 App API（init/loop/exit） |
| `src/app/flasher/flasher_app.cpp` | user_item 生命周期、按键处理、BT 数据接收、协议状态机 |
| `src/app/flasher/flasher_ui.cpp/h` | 全屏进度条 UI 渲染（反色文字、跑马灯） |
| `src/app/flasher/flasher_protocol.cpp/h` | ESP32 ROM Bootloader SLIP 协议、命令构建、进度计算 |
| `src/app/flasher/flasher_gpio.cpp/h` | GPIO 引脚映射管理、UART 配置、下载模式时序 |

## 测试覆盖

- `test/test_native/test_flasher.cpp`：GPIO 映射、协议编解码、App 生命周期
- `test/test_native/test_flasher_ui.cpp`：跑马灯动画、进度限制、渲染冒烟测试
