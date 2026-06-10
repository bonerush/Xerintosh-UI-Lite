# 烧录器 (USB Wired Bridge Flasher)

## 功能概述

M5Stick-C 作为 USB↔UART 有线桥接器，将 PC 端的 USB 串口数据透传到目标板（Arduino/AVR）的 UART。

进入烧录器 App 后，自动激活桥接模式，PC 端运行 avrdude 即可烧录目标板。

## 引脚映射

在 **设置 -> 烧录器引脚** 中配置可用引脚的角色：

| 引脚 | 默认角色 | 可映射角色 | 限制 |
|------|---------|-----------|------|
| G0   | BOOT/DTR | TX / BOOT/DTR / 未分配 | 输出 |
| G26  | TX      | TX / BOOT/DTR / 未分配 | 输出 |
| G36  | RX      | RX / 未分配 | **仅输入** |

- BOOT/DTR 复用：DTR 信号自动回退到 BOOT 引脚
- 每个角色在同一时间只能分配给一个引脚
- 修改后会自动保存到 NVS
- 角色冲突时自动清除旧引脚的角色

## 使用流程

1. **连接目标板**：将 M5Stick-C 的 G0/G26/G36 引脚连接到目标 Arduino/AVR 板的对应引脚
   - G0 → DTR/RESET
   - G26 → RX (目标板 TX)
   - G36 → TX (目标板 RX)
2. **进入烧录器 App**：在主菜单选择 **烧录器**
3. **桥接激活**：App 启动后自动进入桥接模式，屏幕显示全屏进度条，文字 "BRIDGE..."
4. **运行 avrdude**：在 PC 端运行 avrdude 烧录命令
5. **自动复位**：首次 USB 数据到达时自动触发 DTR 脉冲（G0 LOW 50ms）复位目标板进入 bootloader
6. **观察进度**：全屏进度条实时显示 STK500 烧录进度
   - BRIDGE...：桥接就绪，等待 PC 端开始烧录
   - FLASHING...：正在烧录（跑马灯动画）
   - SUCCESS!：烧录完成（绿色）
7. **手动复位**：长按 **BtnA** 可随时手动触发 DTR 复位
8. **退出**：长按 **BtnB** 退出 App

## 进度条设计

全屏进度条：
- 左侧白色填充区域 = 已完成进度
- 右侧黑色空心矩形 = 剩余进度
- 居中反色文字：进度条内部显示黑色，外部显示白色/绿色

进度计算方式：
- 解析 USB→UART 方向数据中的 STK500 PROG_PAGE 命令统计已写入页数
- 解析 STK500 LOAD_ADDRESS 命令获取最大写入地址
- 进度 = 已写入字节数 / 预估总大小 × 100

## RX 噪音过滤

UART→USB 方向的数据仅在最近 2 秒内有 USB→UART 转发时才回传给 PC，
避免 Serial1 RX 悬空噪声被误认为目标板响应。

进度计算只解析 USB→UART 方向的 STK500 命令，不受 UART RX 噪音影响。

## 透传协议

- **波特率**：115200
- **透传方向**：USB (PC) ↔ UART (目标板)
- **协议**：STK500v1（由 avrdude 在 PC 端实现，M5Stick 仅做桥接）
- **DTR 时序**：G0 LOW 50ms → HIGH，等待 500ms 后开始透传

## 文件结构

| 文件 | 职责 |
|------|------|
| `src/app/flasher/flasher.h` | 公共 App API（init/loop/exit） |
| `src/app/flasher/flasher_app.cpp` | user_item 生命周期、USB↔UART 透传、STK500 进度解析 |
| `src/app/flasher/flasher_ui.cpp/h` | 全屏进度条 UI 渲染（反色文字、跑马灯） |
| `src/app/flasher/flasher_gpio.cpp/h` | GPIO 引脚映射管理、UART 配置、DTR 时序 |

## 测试覆盖

- `test/test_native/test_flasher.cpp`：GPIO 映射、App 生命周期
- `test/test_native/test_flasher_ui.cpp`：跑马灯动画、进度限制、渲染冒烟测试
