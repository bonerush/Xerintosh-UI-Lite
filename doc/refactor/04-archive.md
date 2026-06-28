# 重构归档报告 (2026-06-28)

> **Parent:** [重构状态总览](README.md)

## 重构概览

| 项目 | 内容 |
|---|---|
| 分支 | `refactor/2026-06-28-fullstack` |
| 起始 commit | `5d4cc00` |
| 结束 commit | `c00d097` |
| 提交数 | 5 |
| 范围 | 内核 / HAL / UI / App / 文档 |

## 各阶段报告

| 阶段 | 报告 | 状态 |
|---|---|---|
| 0 基线建立 | [00-baseline.md](00-baseline.md) | DONE |
| 1 扫描诊断 | [01-diagnosis.md](01-diagnosis.md) | DONE |
| 2 分层重构 | [02-refactor/kernel.md](02-refactor/kernel.md) | DONE |
| | [02-refactor/hal.md](02-refactor/hal.md) | DONE |
| | [02-refactor/ui.md](02-refactor/ui.md) | DONE |
| | [02-refactor/app.md](02-refactor/app.md) | DONE |
| | [02-refactor/docs.md](02-refactor/docs.md) | DONE |
| 3 集成验证 | [03-integration.md](03-integration.md) | DONE |
| 4 归档 | [04-archive.md](04-archive.md) | DONE |

## 关键变更摘要

### 内核层

- 调度器 idle 优化：`ets_delay_us(200)`，tickless 阈值降至 1ms
- 移除 `main.cpp` 中死代码 FreeRTOS includes，标记 `kern_port_freertos.c` 为废弃
- **修复 tickless idle 时间漂移**：`g_sched_ticks` 在 tickless 结束后按实际流逝时间补偿，解决长时间空闲后 UI 卡死

### HAL 层

- 新增 `hal_display_flush_region()` 脏区域局部刷新 API（底层暂回退全屏推送，待 LovyanGFX 升级）

### UI 层

- VRR 可变刷新率：100Hz(10ms) 动画 / 60Hz(16ms) 静态 / 125Hz(8ms) 长按提示
- 脏矩形条件刷新：脏区 <50% 局部推送，>=50% 全屏推送
- `kern_sleep_ms()` 替代 `kern_sleep_ms(5) + kern_yield()`，消除冗余 yield

### 基础设施

- 基线建立：验证规则写入 `CLAUDE.md` 和 `.claude/rules/verification.md`
- 默认上传目标改为 `m5stick-c-native`（Xeros 原生调度器）
- 平台配置 `platformio.ini` 加入 `default_envs = m5stick-c-native`

## 提交记录

```
7622179 chore: establish refactor baseline, verification rules, and default upload target
277db32 perf(ui,sched): implement VRR 60-100Hz frame control and scheduler idle optimization
f4cbf6a refactor(kernel): remove dead FreeRTOS includes and deprecate port layer
484c564 perf(ui,hal): add dirty-region flush API and conditional SPI push
c00d097 fix(kernel): advance sched ticks by elapsed time after tickless idle
```

## 验证结果

| 目标 | 状态 | RAM | Flash |
|------|------|-----|-------|
| `native` (test) | 601 cases, 599 passed, 2 skipped | — | — |
| `m5stick-c` | SUCCESS | 20.2% (66048 / 327680) | 73.2% (1124393 / 1536000) |
| `m5stick-c-native` | SUCCESS | 20.2% (66200 / 327680) | 74.0% (1136333 / 1536000) |
| 实机启动 | 通过 | — | — |

## 已知问题与后续工作

- 局部 SPI 推送优化需 LovyanGFX 库升级支持区域重载 API
- `kern_port_freertos.c` 可在 m5stick-c-native 完全稳定后彻底删除
- 长期稳定性需 >30 分钟空闲测试确认 tickless 修复无退化

## 分支收尾

分支 `refactor/2026-06-28-fullstack` 已完成所有 5 个阶段。建议：
1. 合并回 `main` 或发起 PR
2. 确认长期稳定性后删除分支

---

> **历史轮次**：[2026-06-27 全栈重构](04-archive.md)（可通过 `git show 2026-06-27-refactor:doc/refactor/04-archive.md` 查看）
