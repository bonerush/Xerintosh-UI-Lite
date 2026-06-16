# 烧录器 (USB Wired Bridge Flasher)

> **Parent:** [App 层索引](index.md) | **Related:** [串口监视器](serial-monitor.md), [设置](settings.md)
>
> 本文档介绍烧录器 App 的引脚映射、桥接逻辑和使用方法。

## 功能概述

M5Stick-C 作为 USB↔UART 有线桥接器，将 PC 端的 USB 串口数据透传到目标板（Arduino/AVR/ESP32）的 UART。

进入烧录器 App 后，自动激活桥接模式，PC 端运行 avrdude / esptool 即可烧录目标板。

## 引脚映射

*📄 Source: [flasher_gpio.h](../../src/app/flasher/flasher_gpio.h#L23-L38)*

```c
typedef enum {
    FLASHER_SIG_NONE = 0,
    FLASHER_SIG_TX   = 1,
    FLASHER_SIG_RX   = 2,
    FLASHER_SIG_BOOT = 5,  /**< BOOT/DTR 复用引脚 */
    FLASHER_SIG_COUNT = 6
} flasher_signal_t;
```

*📄 Source: [flasher_gpio.cpp](../../src/app/flasher/flasher_gpio.cpp#L15-L19)*

```c
flasher_pin_mapping_t g_flasher_pins[FLASHER_AVAILABLE_PINS] = {
    {0,  FLASHER_SIG_BOOT, true},
    {26, FLASHER_SIG_TX,   true},
    {36, FLASHER_SIG_RX,   false}
};
```

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

1. **连接目标板**：将 M5Stick-C 的 G0/G26/G36 引脚连接到目标板的对应引脚
   - G0 (BOOT/DTR) → 目标板 RESET/DTR
   - G26 (TX) → 目标板 RX
   - G36 (RX) → 目标板 TX
2. **进入烧录器 App**：在主菜单选择 **烧录器**
3. **桥接激活**：App 启动后自动进入桥接模式，屏幕显示全屏进度条，文字 "BRIDGE..."
4. **运行烧录工具**（avrdude / esptool / stm32flash，详见下方各平台说明）
5. **自动复位**：首次 USB 数据到达时自动触发 DTR 脉冲（G0 LOW 50ms，等 80ms 让目标 bootloader 就绪），DTR 期间 PC 数据暂存缓冲、就绪后一次性转发
6. **观察进度**：全屏进度条实时显示烧录进度
   - BRIDGE...：桥接就绪，等待 PC 端开始烧录
   - FLASHING...：正在烧录（跑马灯动画）
   - SUCCESS!：烧录完成（绿色）
7. **手动复位**：长按 **BtnA** 可随时手动触发 DTR 复位
8. **退出**：长按 **BtnB** 退出 App

### ⚠️ ESP32 目标板特别注意

**问题**：M5Stick-C 自身是 ESP32-PICO，PC 端 esptool 打开串口时自动翻转
DTR/RTS，通过自动下载电路（CP2104: DTR→EN, RTS→GPIO0）将 **M5Stick-C 自身**
复位进入下载模式，而非目标板。

**解决办法：禁用 esptool 自动复位**，由烧录器负责目标板的 DTR 时序：

```bash
# 命令行
esptool.py --before no_reset --after no_reset \
  --chip esp32 -p /dev/cu.usbserial-xxx write_flash 0x1000 firmware.bin
```

**PlatformIO 项目**在目标板的 `platformio.ini` 中添加：

```ini
upload_flags =
    --before=no_reset
    --after=no_reset
```

**操作流程：**
1. M5Stick-C 进入烧录器桥接模式（显示 "BRIDGE..."）
2. 长按 **BtnA** 手动触发 DTR → 目标板 ESP32 进入下载模式
3. 立即在 PC 端运行 esptool（带 `--before no_reset`）
4. 观察进度条 → FLASHING... → SUCCESS!

## 进度条设计

全屏进度条：
- 左侧白色填充区域 = 已完成进度
- 右侧黑色空心矩形 = 剩余进度
- 居中反色文字：进度条内部显示黑色，外部显示白色/绿色

进度计算方式：
- 解析 USB→UART 方向数据中的烧录命令统计进度：
- STK500（avrdude）：解析 `STK_LOAD_ADDR_CMD(0x55)` 和 `STK_PROG_PAGE_CMD(0x64)` 统计已写入页数
  - 解析 LOAD_ADDRESS 命令获取最大写入地址
  - 进度 = 已写入字节数 / 预估总大小 × 100
- ESP32 SLIP（esptool）：解析 `FLASH_BEGIN(0x02)` 获取总块数
  - 解析 `FLASH_DATA(0x03)` 统计已发送块数
  - 进度 = 已发送块数 / 总块数 × 100
- 协议自动识别：双解析器同时运行，谁先匹配就以谁为准

## RX 噪音过滤

*📄 Source: [flasher_app.cpp](../../src/app/flasher/flasher_app.cpp#L490-L501)*

```c
/* ── UART (目标板) → USB (PC) ── */
{
    uint8_t uart_buf[64];
    int uart_len = flasher_uart_read(uart_buf, sizeof(uart_buf));
    if (uart_len > 0) {
        if (hal_get_ticks() - s_pt_last_tx_ms < 2000) {
            Serial.write(uart_buf, uart_len);
            Serial.flush();
        }
        s_pt_rx_bytes += (uint32_t)uart_len;
    }
}
```

UART→USB 方向的数据仅在最近 **2 秒** 内有 USB→UART 转发时才回传给 PC，避免 Serial1 RX 悬空噪声被误认为目标板响应。

进度计算只解析 USB→UART 方向的 STK500/SLIP 命令，不受 UART RX 噪音影响。

## 透传协议

*📄 Source: [flasher_app.cpp](../../src/app/flasher/flasher_app.cpp#L354-L369, L405-L457)*

- **波特率**：115200（`flasher_init_pins(115200U)`）
- **透传方向**：USB (PC) ↔ UART (目标板)
- **协议**：STK500v1 / ESP32 SLIP（由 avrdude/esptool 在 PC 端实现，M5Stick 仅做桥接）
- **DTR 时序**：首次 USB 数据到达 → 设置 `PT_PHASE_DTR_WAIT` → 1ms 后 G0 LOW 50ms → HIGH → 等待 80ms bootloader 初始化（期间 USB 数据暂存）→ 进入 IDLE 透传（先转发暂存数据）

## 文件结构

| 文件 | 职责 |
|------|------|
| `src/app/flasher/flasher.h` | 公共 App API（init/loop/exit） |
| `src/app/flasher/flasher_app.cpp` | user_item 生命周期、USB↔UART 透传、STK500/ESP32 SLIP 协议自动识别与进度解析 |
| `src/app/flasher/flasher_ui.cpp/h` | 全屏进度条 UI 渲染（反色文字、跑马灯） |
| `src/app/flasher/flasher_gpio.cpp/h` | GPIO 引脚映射管理、UART 配置、DTR 时序 |

## 测试覆盖

- `test/test_native/test_flasher.cpp`：GPIO 映射、App 生命周期
- `test/test_native/test_flasher_ui.cpp`：跑马灯动画、进度限制、渲染冒烟测试
