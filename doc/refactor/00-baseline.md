# 重构基线报告

## 分支与 Commit

- 分支：`refactor/2026-06-14-kernel-first`
- 起始 commit：`a4d8bab2703908ff0c4934daf97c1bced671eb40`
- 工作目录：`/Users/yukisala/Documents/PlatformIO/Projects/M5Stick-P1/.worktrees/refactor-2026-06-14-kernel-first`

## 构建基线

### 硬件构建

- 命令：`pio run -e m5stick-c`
- 结果：✅ PASS
- 耗时：26.35 秒
- 内存占用：
  - RAM：22.3%（73,216 bytes / 327,680 bytes）
  - Flash：88.0%（1,845,389 bytes / 2,097,152 bytes）
- 新增警告：构建日志中出现 `cc1: warning: command line option '-fno-rtti' is valid for C++/ObjC++ but not for C`，来自 `M5GFX` 等第三方库的 C 文件编译，非项目源码新增警告。

### Native 测试

- 命令：`pio test -e native`
- 结果：✅ PASS
- 测试套件：3 个
- 测试用例：371 个全部通过
- 总耗时：8.602 秒

## 代码规模

> **说明**：`cloc` 未安装，因此使用 `find` + `wc` 进行补充统计；语言级别的代码行/注释行拆分待后续安装 `cloc` 后补充。

| 范围 | 文件数 | 总行数 |
|------|--------|--------|
| `src/` | 165 | 23,773 |
| `doc/` | 89 | 21,051 |

## 已知问题（来自 `src/` 与 `doc/` 扫描）

扫描关键词：`TODO`、`FIXME`、`XXX`、`HACK`、`已知陷阱`。

1. `src/kernel/kern_init.c:121` 存在 TODO：`/* TODO: 硬件 LED 闪烁 */`。
2. `src/kernel/kern_port_native.c:198` 存在 TODO：`/* TODO: 使用 esp_timer 或简单的忙等待 */`。
3. `doc/kernel/kern-init.md:120` 引用了源码中的同一 TODO（`/* TODO: 硬件 LED 闪烁 */`），文档与代码同步。

> 注：`tools/ui-layout-designer/exporter.js:198` 存在 `// TODO: Render the layout elements above using HAL APIs`，但不在 `src/` 或 `doc/` 范围内，故未计入本轮基线。

## 本次重构范围

本轮重构以 **内核层** 为首要目标，视阶段 1 诊断结果决定是否扩展至其他层级。

- [x] 内核层
- [ ] UI 核心层
- [ ] HAL 层
- [ ] App 层
- [ ] 文档体系
