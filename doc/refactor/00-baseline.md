# 重构基线报告（2026-06-28 轮次）

## 分支与 Commit

- **分支**：`refactor/2026-06-28-fullstack`
- **起始 commit**：`5d4cc0000d3d07ce912b97255453aa0602f716d9`

## 构建基线

| 目标 | 命令 | 结果 |
|------|------|------|
| 硬件目标构建 | `pio run -e m5stick-c` | ✅ PASS |
| Native 测试 | `pio test -e native` | ✅ PASS（601 用例，599 通过，2 跳过） |
| ESP32 native 调度构建 | `pio run -e m5stick-c-native` | 待阶段 2 kernel 后验证 |

### 硬件构建内存占用

- **RAM**：20.2%（66,048 / 327,680 bytes）
- **Flash**：73.2%（1,124,041 / 1,536,000 bytes）

## 代码规模（cloc）

| 语言 | 文件数 | 代码行 | 注释行 |
|------|--------|--------|--------|
| C | 85 | 13,773 | 2,702 |
| C++ | 24 | 3,661 | 1,080 |
| C/C++ Header | 103 | 2,848 | 3,325 |
| Markdown | 41 | 8,336 | — |
| 其他 | 23 | 2,759 | — |
| **总计** | **276** | **31,377** | **7,628** |

## 已知问题

### TODO/FIXME
| ID | 文件 | 内容 | 优先级 |
|----|------|------|--------|
| T1 | `hal_power_off.cpp:14` | display HAL 迁移完成后调用显示休眠 | P2 |
| T2 | `kern_init.c:128` | 硬件 LED 闪烁 | P2 |
| T3 | `kern_port_native.c:197` | 使用 esp_timer 或忙等待 | P2 |
| T4 | `dev_ttyS0.cpp:20` | 迁移到 app/flasher/flasher.h | P1 |

### FreeRTOS 残留（本轮重构重点）
| ID | 文件 | 类型 | 说明 |
|----|------|------|------|
| F1 | `kern_port_freertos.c` | 完整文件（390行） | XEROS_NATIVE_SCHED 下不使用 |
| F2 | `main.cpp:24-25` | #include | `<freertos/FreeRTOS.h>` + `<freertos/task.h>` |
| F3 | `kern_sched.c:588-589` | 条件编译内 #include | `<freertos/FreeRTOS.h>` + `<freertos/task.h>` |
| F4 | `kern_task_stack.c:113` | 条件编译内 #include | `<freertos/FreeRTOS.h>` |
| F5 | `esp32/core_start.c:5-11` | #include + 调用 | `xTaskCreatePinnedToCore` |
| F6 | 多处 | 注释提及 FreeRTOS | ~40+ 处 |

## 本轮重构范围

- [ ] **内核层**：调度器算法优化 + FreeRTOS 残留死代码清理
- [ ] **HAL 层**：显示驱动职责厘清
- [ ] **UI 核心层**：帧率优化（目标 100Hz-60Hz VRR）
- [ ] **App 层**：状态管理一致性
- [ ] **文档体系**：同步更新

## 本次特殊目标

1. 默认上传指令改为 `pio run -e m5stick-c-native --target upload`
2. 验证规则写入 `.claude/rules/verification.md`：先电脑验证 → 再实机验证
   - 均已实现 ✅

---

> **Parent:** [README.md](README.md)
