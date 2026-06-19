# 重构基线报告（第十一轮 · shell-wifi-kernel · 2026-06-19）

## 分支与 Commit

- 分支：`refactor/2026-06-19-shell-wifi-kernel`
- 起始 commit：`922cfb8805e57ac87285a0286bcdad498fb0bc3a`
- 基线来源：`main`（第十轮已合并）

## 构建基线

| 检查项 | 结果 | 说明 |
|--------|------|------|
| `pio run -e m5stick-c` | ✅ PASS | RAM 27.0% (88424/327680), Flash 89.3% (1872705/2097152) |
| `pio test -e native` | ✅ 224/225 | test_native ERRORED（已知 SIGTRAP teardown，无新增失败） |

## 代码规模

| 目录 | 文件数 | 代码/文档行数 |
|------|--------|---------------|
| `src/` | 183 | 27,874 |
| `doc/` | 123 | 24,632 |
| `test/` | 51  | 8,144  |

## 已知问题（来自 TODO/FIXME 扫描）

| ID | 文件 | 位置 | 内容 | 优先级 |
|----|------|------|------|--------|
| T1 | `src/kernel/kern_init.c` | 121 | `/* TODO: 硬件 LED 闪烁 */` | P2 |
| T2 | `src/kernel/kern_port_native.c` | 198 | `/* TODO: 使用 esp_timer 或简单的忙等待 */` | P2 |
| T3 | `src/kernel/devices/dev_ttyS0.cpp` | 20 | `/* TODO(phase 2.4): 迁移到 app/flasher/flasher.h 声明 */` | P1 |

## 本轮重构范围

- [x] 内核层：重点审查与 shell/WiFi 交互的 VFS、设备驱动、资源释放路径
- [x] HAL 层：审查显示/输入与 shell/WiFi 日志输出的关联
- [x] UI 核心层：审查 popup、选择器在 WiFi 状态变化时的表现
- [x] App 层：重点优化 shell 相关命令、WiFi 状态机、蓝牙/WiFi 互斥
- [x] 文档体系：原子化更新每个 public API 变化

## 上一轮未解决问题（本轮继承）

来自 `04-archive.md`（第十轮）：

| ID | 问题 | 优先级 | 备注 |
|----|------|--------|------|
| D2 | io 命令与 HAL 背光路径独立 | P1 | 需统一背光控制接口 |
| D3 | param save/load 存根 | P2 | 实现或移除 |
| D6 | sm_ui.c 死代码 | P2 | `strchr(\n)` 永远返回 NULL |
| D7 | 日志行长度 64 字符限制 | P2 | 可配置化 |
| D12 | bt_uart_service_deinit TOCTOU | P1 | delay(100) 不可靠 |
| D13 | BT/WiFi 双向互斥 | P2 | 当前仅单向保护 |
