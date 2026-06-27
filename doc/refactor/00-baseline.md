# 重构基线报告

## 分支与 Commit

- **分支**：`refactor/2026-06-27-fullstack`
- **起始 commit**：`7ac97ef`
- **基线修复 commit**：`4aaf6f2`
  - `src/kernel/kern_sched_rr.c`：idle 仅在 READY/RUNNING 时返回。
  - `test/test_native/test_ctx_switch.cpp`：非 Xtensa 平台跳过 ABI 大小断言。

## 隔离工作区

- **路径**：`/Users/yukisala/subject/Xerintosh/.worktrees/refactor-2026-06-27-fullstack`
- **验证**：`GIT_DIR != GIT_COMMON`，已隔离。

## 构建基线

| 目标 | 命令 | 结果 |
|------|------|------|
| 硬件目标构建 | `pio run -e m5stick-c` | 通过 |
| Native 测试 | `pio test -e native` | 通过（524 succeeded，2 skipped） |
| ESP32 native 调度构建 | `pio run -e m5stick-c-native` | 待阶段 2 kernel 后验证 |

### 硬件构建内存占用

- **RAM**：19.5%（63,816 / 327,680 bytes）
- **Flash**：73.2%（1,124,117 / 1,536,000 bytes）

## 代码规模（近似行数）

| 区域 | 行数 |
|------|------|
| `src/` | ~30,531 |
| `doc/` | ~5,973 |
| `test/` | ~8,812 |

> 注：`cloc` 未安装，行数由 `find + wc -l` 统计。

## 已知 TODO/FIXME

| 文件 | 行 | 内容 |
|------|-----|------|
| `src/hal/hal_power_off.cpp:14` | 待 display HAL 迁移完成后调用显示休眠 |
| `src/kernel/kern_init.c:121` | 硬件 LED 闪烁 |
| `src/kernel/kern_port_native.c:197` | 使用 esp_timer 或简单的忙等待 |
| `src/kernel/devices/dev_ttyS0.cpp:20` | 迁移到 app/flasher/flasher.h 声明或改用 dev_ttyS0_set_bridge_active |

## 本次重构范围

- [ ] 内核层
- [ ] HAL 层
- [ ] UI 核心层
- [ ] App 层
- [ ] 文档体系

---

> **Parent:** [README.md](README.md)
