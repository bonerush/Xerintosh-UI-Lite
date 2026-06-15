# 重构跟踪：app-oscilloscope（第六轮 · 2026-06-15）

本轮重构聚焦：**延续第五轮，修复遗留 P1 性能问题 + P2 代码质量**
- ADC 忙等阻塞调度 → 拆分采样批次（D12）
- 布局缓存优化 (D28)、HOLD 模式 dirty rect (D8)
- 文件/函数拆分 (D21/D22)、参数镜像消除 (D23)
- 采样率上限真实性 (D18)

## 阶段状态

| 阶段 | 名称 | 状态 | 负责 Agent | 产物文件 |
|------|------|------|------------|----------|
| 0 | 基线建立 | **DONE** | coder | `00-baseline-app-oscilloscope.md`（复用第5轮） |
| 1 | 遗留诊断 | **DONE** | — | `01-diagnosis-app-oscilloscope.md`（复用第5轮） |
| 2.4 | App 层重构（续） | **DONE** | coder | `02-refactor/app-oscilloscope.md` |
| 3 | 集成验证 | **DONE** | coder | `03-integration-app-oscilloscope.md` |
| 4 | 归档 | **DONE** | coder | `04-archive-app-oscilloscope.md` |

## 本轮范围（遗留 P1 + 精选 P2）

- [x] **采样调度优化（D12）**：拆分 ADC 采样为小批次，yield 给 Xeros 调度器
- [x] **采样率上限修正（D18）**：移除不可达 50/100kHz 选项，上限 20kHz
- [x] **HOLD 模式脏矩形（D8）**：HOLD 状态跳过冗余重绘
- [x] **布局缓存（D28）**：scope_compute_layout 只在变化时重算
- [x] **函数/文件拆分（D21/D22）**：oscilloscope_app.c 410→369 行（-10%），新增 oscilloscope_params.c
- [x] **参数镜像消除（D23）**：param_decrease/increase 合并为 scope_param_adjust（-28 行）
- [ ] **状态体分离（D24）**：scope_state_t 拆分 engine/view

| 阶段 | 名称 | 状态 | 负责 Agent | 产物文件 |
|------|------|------|------------|----------|
| 0 | 基线建立 | **DONE** | coder | `00-baseline-app-oscilloscope.md` |
| 1 | App/示波器诊断 | **DONE** | explore×3 | `01-diagnosis-app-oscilloscope.md` |
| 2.4 | App 层重构 | **DONE** | coder | `02-refactor/app-oscilloscope.md` |
| 3 | 集成验证 | **DONE** | coder | `03-integration-app-oscilloscope.md` |
| 4 | 归档 | **DONE** | coder | `04-archive-app-oscilloscope.md` |

## 本轮范围

- [x] **示波器引擎优化**：线性采集、AC 耦合重新居中、频率滞回、AC 窗口自适应
- [x] **示波器 UI 优化**：像素映射修正、分隔线硬件加速、编辑高亮统一
- [x] **输入响应性**：编辑键映射翻转、B 长按先退编辑、事件缓存清理
- [x] **代码结构**：移除环形缓冲、sample_write_pos 清理、滤波器语义修正

## 验证结果

| 验证项 | 状态 |
|--------|------|
| `pio run -e m5stick-c` | ✅ SUCCESS |
| `pio test -e native` | ✅ 427 pass, 1 skipped |

---

# 重构跟踪：docs-architecture-diagrams（2026-06-15 第四轮）

本轮重构聚焦：**文档体系优化 — 添加 Mermaid 架构图 + 原子化修正文档准确性**。

## 阶段状态

| 阶段 | 名称 | 状态 | 负责 Agent | 产物文件 |
|------|------|------|------------|----------|
| 0 | 基线建立 | **DONE** | coder | `00-baseline-docs-v4.md` |
| 1 | 文档诊断 | **DONE** | explore×2 | `01-diagnosis-docs-v4.md` |
| 2.4 | 文档体系重构 | **DONE** | coder | 本报告 |
| 3 | 集成验证 | **DONE** | coder | 构建 + native 测试通过 |
| 4 | 归档 | **DONE** | coder | `04-archive-docs-v4.md` |

## 本轮范围

- [x] **7 个 Mermaid 架构图**：系统总览、启动流程、内核结构、VFS 桥接、UI 渲染管线、user_item 生命周期、典型 App 功能
- [x] **文档准确性修正**：3 HIGH + 5 MEDIUM + 5 LOW 共 13 项修正
- [x] **新增 `ui_dirty.md`** 模块文档
- [x] **索引更新**：`doc/ui/index.md`、`doc/index.md` 补充遗漏条目

## 核心改动

| # | 项目 | 文件 | 效果 |
|---|------|------|------|
| 图1 | 系统总体架构 Mermaid | `doc/index.md` | 替换 ASCII ASCII art，六层交互可视化 |
| 图2 | 系统启动流程 Mermaid | `doc/index.md` | 全新时序图，从 power on 到 60fps 循环 |
| 图3 | Xeros 内核结构 Mermaid | `doc/kernel/index.md` | 替换 ASCII box art，分组可视化 |
| 图4 | VFS 桥接 Mermaid | `doc/kernel/kern-device-model.md` | 替换 ASCII，open→read→close 完整路径 |
| 图5 | UI 渲染管线 Mermaid | `doc/index.md` | 替换文字列表，dirty 优化分支可视化 |
| 图6 | user_item 生命周期 Mermaid | `doc/developer-guide.md` | 状态机图，init→loop→exit 清晰表达 |
| 图7 | 典型 App 功能 Mermaid | `doc/developer-guide.md` | 全新功能图，菜单→App→服务交互 |
| 修正 | API 表移除 3 个 static 函数 | `doc/ui/core.md` | 消除 HIGH 级误导 |
| 修正 | 补充 `dirty` 字段 | `doc/ui/context.md` | 结构体字段对齐源码 |
| 修正 | 枚举源引用修正 | `doc/ui/item.md` | ui_item_core.h → ui_types.h |
| 修正 | BT 描述修正 + 去重 | `doc/app/index.md` | NimBLE→Classic BT，移除重复行 |
| 修正 | 补充 `kern_sched` 模块 | `doc/kernel/index.md` | 模块表完整性 |
| 新增 | `ui_dirty.md` | `doc/ui/ui-dirty.md` | 脏矩形模块首次文档化 |
| 补充 | flasher/shutdown 到架构树 | `doc/index.md` | 知识地图完整性 |

## 验证结果

| 验证项 | 状态 |
|--------|------|
| `pio run -e m5stick-c` | ✅ SUCCESS |
| `pio test -e native` | ✅ 414/415 pass, 1 skipped |
| Mermaid 图语法正确 | ✅ 7/7 |
| 文档修正 | ✅ 13/13 |

## 快速链接

- 基线报告：`00-baseline-docs-v4.md`
- 诊断报告：`01-diagnosis-docs-v4.md`
- 新增模块文档：`src/ui/ui_dirty.h` → `doc/ui/ui-dirty.md`

---

# 重构跟踪：ui-dirty-rect（2026-06-15 第三轮）

本轮重构聚焦：**UI 渲染管线的局部刷新与脏矩形处理统一接口**。

## 阶段状态

| 阶段 | 名称 | 状态 | 负责 Agent | 产物文件 |
|------|------|------|------------|----------|
| 0 | 基线建立与冻结 | **DONE** | coder | (继承上轮基线) |
| 1 | UI 渲染管线诊断 | **DONE** | coder | `01-diagnosis-ui-render.md`（内联分析） |
| 2.3 | UI 核心层重构 | **DONE** | coder | `02-refactor/ui-dirty.md` |
| 3 | 集成验证 | **DONE** | coder | 构建 + native 测试通过 |
| 4 | 文档同步与归档 | **DONE** | coder | `developer-guide.md` 已更新 |

## 本轮范围

- [x] **UI 核心层**（`src/ui/`）— 脏矩形 API 统一化：新建 `ui_dirty.h/c`，标准化 `xerintosh_invalidate()` 接口
- [x] **App 层对齐**（`src/app/`）— `ui_task.c` 迁移至新 API
- [x] **文档更新**（`doc/`）— `developer-guide.md` 新增脏矩形 API 参考表

## 核心改动

| # | 项目 | 文件 | 效果 |
|---|------|------|------|
| 新建 | 脏矩形管理模块 | `src/ui/ui_dirty.h/c` | 统一入口，三元 API |
| 替换 | 22 处直接写入 | 7 个文件 | 全部走 `xerintosh_invalidate()` |
| 替换 | 2 处直接读取 | `ui_core.c` + `ui_task.c` | 走 `xerintosh_is_dirty()` |
| 文档 | API 参考表 | `developer-guide.md` | 开发者可清晰知晓何时/如何标记脏 |

## 新 API 速查

| 函数 | 对象 | 说明 |
|------|------|------|
| `xerintosh_invalidate()` | **App 开发者** | 标记 UI 需要重绘 |
| `xerintosh_is_dirty()` | 框架内部 | 查询脏状态 |
| `xerintosh_clear_dirty()` | 框架内部 | 清除脏标志 |
| `xerintosh_mark_dirty()` | 向后兼容 | `xerintosh_invalidate()` 别名 |

## 验证结果

| 验证项 | 状态 |
|--------|------|
| `pio run -e m5stick-c` | ✅ SUCCESS |
| `pio test -e native` | ✅ 414/415 pass, 1 skipped |
| 编译警告 | ✅ 无新增 |
| RAM | 25.5%（无变化） |

## 快速链接

- 重构报告：`02-refactor/ui-dirty.md`
- 新模块源码：`src/ui/ui_dirty.h`、`src/ui/ui_dirty.c`
- 开发者文档：`doc/developer-guide.md` §8.7
- 上轮内核重构：`02-refactor/kernel-v2.md`
