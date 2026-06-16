# 重构基线报告：app-transition-device-optimizations（第八轮）

## 分支与 Commit
- 分支：`refactor/2026-06-16-app-transition-device-optimizations`
- 起始 commit：`9410b8dcca54259c3f1b75dbb7cc97d72c344ba5`

## 构建基线
- `pio run -e m5stick-c`：✅ PASS（28.1% RAM，88.9% Flash）
- `pio test -e native`：✅ PASS（427 test cases：1 skipped，426 succeeded）

## 代码规模
| 类别 | 文件数 |
|------|--------|
| src/ C/C++/H | 181 |
| doc/ Markdown | 93 |

> `cloc` 未安装，未统计行数；后续可用 `wc -l` 补记。

## 已知问题（来自文档/代码扫描）
1. `src/kernel/kern_init.c:121`：`/* TODO: 硬件 LED 闪烁 */`
2. `src/kernel/kern_port_native.c:198`：`/* TODO: 使用 esp_timer 或简单的忙等待 */`
3. `doc/kernel/kern-init.md:120`：引用了源码中的同一 TODO，文档与代码同步。

## 本次重构范围
- [ ] 内核层：GPIO 设备优化 + 其余设备优化
- [ ] UI 核心层：为 App 进入/退出提供共享过渡动画基础设施
- [ ] App 层：为所有 `user_item` App 添加进入/退出过渡动画
- [ ] App 层：审计并修复非标准/绕过内核的系统 API 调用
- [ ] 文档体系：同步新增/变化 API 文档
