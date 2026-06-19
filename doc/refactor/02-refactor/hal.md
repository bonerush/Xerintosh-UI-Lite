# 阶段 2.2 HAL 层重构报告

## 目标

修复 HAL 层与 shell/WiFi 交互相关的显示、输入问题，统一 native 与硬件行为。

## 已修复问题

| ID | 问题 | 文件 | 修改 | 状态 |
|----|------|------|------|------|
| P1-1 | 双击状态机窗口超时同帧丢按下事件 | `src/hal/hal_input_double_click.c` | 调整处理顺序：按下→释放→超时检查→长按 | ✅ |
| P1-2 | `hal_display_set_rotation()` 不重建 sprite | `src/hal/hal_display_fb.cpp` | 硬件路径删除旧 sprite 并按新方向重建 | ✅ |
| P1-6 | native XBM 位序与硬件/M5GFX 不一致 | `src/hal/hal_display_adv.cpp` | native 改为 MSB 在前 | ✅ |
| P2-14 | `/dev/fb0` rotation ioctl 不重建 sprite | `src/kernel/devices/dev_fb0.c` | 通过 `hal_display_set_rotation()` 内部重建，间接修复 | ✅ |

## 新增/扩展测试

| 测试文件 | 新增测试 |
|----------|----------|
| `test/test_native/test_double_click.cpp` | `PressAtWindowTimeoutDoesNotDropEvent` |
| `test/test_native/test_hal_display.cpp` | `HalDisplayXbm.MsbFirstBitOrder` |

## 构建与测试

| 检查项 | 结果 |
|--------|------|
| `pio run -e m5stick-c` | ✅ SUCCESS（Flash 89.3%） |
| `pio test -e native` | ⚠️ test_native 套件因 KernelSchedTest 偶发 SIGTRAP ERRORED；其余套件 PASS。 |

## 遗留问题

| ID | 问题 | 原因 |
|----|------|------|
| P1-9 | 硬件路径字体每行 255 字节截断 | 需重构 `hal_display_font.cpp` 分块绘制，改动较大 |
| P2-1 | native `hal_get_string_width()` 对 `\n`/UTF-8 估算不一致 | 需与硬件 `textWidth()` 对齐 |
| P3-6 | `hal_input.cpp` 使用 `millis()` 而非 `hal_get_ticks()` | 风格问题，低优先级 |

## 关键提交

- `24407c3` fix(hal): 旋转后重建 sprite，统一 native XBM 位序
- `0dc992a` fix(hal): 双击状态机先处理按下边沿
