# 集成验证报告 (2026-06-28)

> **Parent:** [重构状态总览](README.md)

## 验证范围

本轮集成验证覆盖阶段 2（kernel / HAL / UI / App / docs）全部分层重构完成后的回归检查，以及阶段间发现的 tickless 稳定性修复。

## 提交记录

```
7622179 chore: establish refactor baseline, verification rules, and default upload target
277db32 perf(ui,sched): implement VRR 60-100Hz frame control and scheduler idle optimization
f4cbf6a refactor(kernel): remove dead FreeRTOS includes and deprecate port layer
484c564 perf(ui,hal): add dirty-region flush API and conditional SPI push
c00d097 fix(kernel): advance sched ticks by elapsed time after tickless idle
```

## 验证结果

### 1. 全量 native 测试

```bash
pio test -e native
```

- **状态**：通过
- **结果**：601 test cases，2 skipped，599 succeeded
- **跳过项**：2 个 ESP32 上下文大小相关测试（native 环境预期跳过）

### 2. 硬件目标构建

```bash
pio run -e m5stick-c
```

- **状态**：SUCCESS
- **RAM**：20.2%（66048 / 327680 bytes）
- **Flash**：73.2%（1124393 / 1536000 bytes）
- **新增警告**：无

### 3. ESP32 native 调度构建

```bash
pio run -e m5stick-c-native
```

- **状态**：SUCCESS
- **RAM**：20.2%（66200 / 327680 bytes）
- **Flash**：74.0%（1136333 / 1536000 bytes）
- **新增警告**：无

### 4. 实机验证

- **烧录命令**：`pio run -e m5stick-c-native --target upload`
- **串口设备**：`/dev/cu.usbserial-4D52671EFA` (M5Stick-C, ESP32-PICO-D4, MAC `94:b9:7e:93:15:34`)
- **启动验证**：
  - UART、NVS、Display、UI、Xeros kernel、SMP 双核调度、WiFi manager、shell 均正常初始化
  - UI 帧循环正常：`ui_task_main started (VRR 60-100Hz)` → frames 1-5 全部完成
  - 内核 shell 可用，`help` 命令正常响应
  - 内存健康：free_heap=126896 启动后
- **tickless 稳定性修复**：长时间空闲后 UI 卡死的根因是 tickless idle 期间 `g_sched_ticks` 未推进，导致睡眠任务唤醒时间漂移。修复后 `g_sched_ticks` 在 tickless 结束后按实际流逝时间补偿。

### 5. 本次新增修复

- **tickless 时间漂移** (`c00d097`): `kern_port_esp32_native.c` 的 `native_idle()` 在 tickless 等待结束后不再只依赖 `kern_sched_tick()` 的 +1，而是通过 `esp_timer_get_time()` 计算实际流逝毫秒数并补偿到 `g_sched_ticks`。解决了空闲时 tick 计数器落后真实时间导致睡眠任务延迟唤醒（O(n²) 级联延迟）从而 UI 卡死的问题。

## 结论

阶段 3 集成验证通过。全量 native 测试（601 cases）、硬件构建（m5stick-c + m5stick-c-native）、实机烧录启动验证、tickless 稳定性修复均已完成。所有提交可通过 `git revert` 单独回滚。

---

> **See Also:** [阶段 2 重构报告](02-refactor/) | [阶段 4 归档报告](04-archive.md)
