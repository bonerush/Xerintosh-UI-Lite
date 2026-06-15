# 重构基线报告（2026-06-15 kernel-ui-performance）

## 分支与 Commit

- 分支：`refactor/2026-06-15-kernel-ui`
- 起始 commit：`07b9e3192a97102b4da69c6464f72c7b6639c4ac`
- 工作目录：`/Users/yukisala/Documents/PlatformIO/Projects/M5Stick-P1-refactor-kernel-ui`
- 基线来源：上一轮 `refactor/2026-06-14-kernel-first` 已完成并合并到 main

## 构建基线

### 硬件构建

- 命令：`pio run -e m5stick-c`
- 结果：✅ PASS
- 耗时：27.36 秒
- 内存占用：
  - RAM：22.3%（73,104 bytes / 327,680 bytes）
  - Flash：88.1%（1,846,669 bytes / 2,097,152 bytes）
- 新增警告：无（`-fno-rtti` 警告来自 M5GFX 等第三方库，非项目源码）

### Native 测试

- 命令：`pio test -e native`
- 结果：✅ PASS
- 测试套件：3 个（test_ble_uart, test_native, test_token_usage）
- 测试用例：415 个（414 通过，1 跳过）
- 总耗时：10.294 秒

## 代码规模

| 范围 | 文件数 | 总行数 |
|------|--------|--------|
| `src/` | 166 | 24,295 |
| `doc/` | ~89 | ~21,000 |

## 已知问题（来自 `src/` 与 `doc/` 扫描）

1. `src/kernel/kern_init.c:121`：`/* TODO: 硬件 LED 闪烁 */`
2. `src/kernel/kern_port_native.c:198`：`/* TODO: 使用 esp_timer 或简单的忙等待 */`
3. `doc/kernel/kern-init.md:120`：引用了源码中的同一 TODO，文档与代码同步。

## 本次重构范围

本轮聚焦 **内核性能优化** 和 **UI 流畅度提升**：

- [x] 内核层 — 内存占用、CPU 性能、实时性
- [x] UI 核心层 — 动画优雅、帧率提高、响应速度
- [ ] HAL 层 — 按需修复
- [ ] App 层 — 按需修复
- [ ] 文档体系
