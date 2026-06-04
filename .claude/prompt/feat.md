# Feat 工作流说明文档

本文档用于跟踪新功能的开发、集成与审查流程。所有特性需求以列表形式记录在下方 [FEAT 列表] 区域，Code Agent 与人工审查者根据此清单协同工作。本流程加入了 Git Worktree 机制(你可以参考多个skills使用worktree的方法和思想)，允许多个 Agent 在隔离环境中并行开发，最终由集成 Agent 统一合并后再进入人工审查。

## 状态定义

| 状态标记 | 含义 | 通常由谁设置 |
|----------|------|------------|
| `FEAT` | 新提出或重新打开的特性需求，尚未指派给 Agent | 人工 / Agent |
| `FEAT_AGENT` | Agent 正在独立 worktree 中开发此特性（占用状态） | Agent |
| `FEAT_AGENT_FINISH` | Agent 完成独立开发，等待集成（特性分支已推送） | Agent |
| `FEAT_MERGED` | 集成 Agent 已完成多个特性的合并与初步验证，等待人工最终审查 | 集成 Agent |
| `VERIFIED` | 人工审查通过，特性合并至主分支并关闭 | 人工 |
| `REJECTED` | 人工审查驳回（集成结果）或单个特性被驳回，需要 Agent 重新修改 | 人工 |

## 角色与权限

### 开发 Agent （普通 Agent）
- 每个特性通常由一名开发 Agent 在独立 worktree 中完成。
- **允许的状态转换**：
    - （新建） -> `FEAT` （用户明确提出新功能需求时记录）
    - `FEAT` -> `FEAT_AGENT` （领取任务，创建专属 worktree）
    - `FEAT_AGENT` -> `FEAT_AGENT_FINISH` （推送特性分支，提交成果）
    - `REJECTED` -> `FEAT_AGENT` （根据驳回意见修复后重新开始）
    - `VERIFIED` -> `FEAT` （已验证的功能需要重新修改或增强，重新打开）

### 集成 Agent
- 负责在多个特性开发完成后，**创建集成 worktree**，合并所有特性分支，解决冲突并执行初步测试。
- 通常由一名经验丰富的 Agent 或人工指定担任。
- **允许的状态转换**：
    - 多个 `FEAT_AGENT_FINISH` -> 将它们标记为 `FEAT_MERGED` （开始集成，合并到集成分支）
    - `FEAT_MERGED` -> `FEAT_AGENT_FINISH` （若集成失败回退至某个特性，需要该特性重新修改）
    - `FEAT_MERGED` -> `VERIFIED` （**无权限**，仅人工可执行）

### 人工审查者
- **允许的状态转换**：
    - （新建） -> `FEAT` （提出新功能需求）
    - `FEAT_MERGED` -> `VERIFIED` （确认集成结果符合预期，合并至主分支）
    - `FEAT_MERGED` -> `REJECTED` （驳回集成结果，附带全局修改意见）
    - `FEAT_AGENT_FINISH` -> `REJECTED` （直接驳回单个特性，无需等待集成）
    - 在特殊情况下，人工也可直接对 `FEAT_AGENT_FINISH` 进行预审并提前驳回。

## 通用流程（含 Git Worktree 协作）

```mermaid
stateDiagram-v2
    [*] --> FEAT : 创建/重新打开
    FEAT --> FEAT_AGENT : Agent领取(创建worktree)
    FEAT_AGENT --> FEAT_AGENT_FINISH : Agent推送特性分支
    FEAT_AGENT_FINISH --> FEAT_MERGED : 集成Agent合并多个完成特性
    FEAT_MERGED --> VERIFIED : 人工审查通过
    FEAT_MERGED --> REJECTED : 人工驳回集成结果
    REJECTED --> FEAT_AGENT : Agent根据反馈重新修改
    VERIFIED --> [*] : 关闭
    VERIFIED --> FEAT : 需要修改时重新打开
```

## 技能与工具索引

本工作流涉及以下技能（Skills）和工具（Tools）。各 Agent 在执行对应步骤时，**必须**按引用调用，不得跳过或替代。

| 技能 / 工具 | 用途 | 适用阶段 | 执行者 |
|---|---|---|---|
| `using-git-worktrees` | Worktree 隔离开发最佳实践（检测、创建、清理） | Step 0 ~ Step 1 | 开发 Agent / 集成 Agent |
| `EnterWorktree` / `ExitWorktree` | 原生 worktree 进入 / 退出工具 | Step 1a | 开发 Agent / 集成 Agent |
| `writing-plans` | 复杂特性的实现规划与架构设计 | `FEAT` -> `FEAT_AGENT` | 开发 Agent（领取任务前） |
| `subagent-driven-development` | 使用多子代理按计划执行开发（每任务独立子 Agent + 两阶段审查） | Step 2 | 开发 Agent |
| `test-driven-development` | 测试驱动开发（先写测试，再实现） | Step 2 | 开发 Agent |
| `verification-before-completion` | 完成前的验证流程（检查清单、回归测试） | `FEAT_AGENT` -> `FEAT_AGENT_FINISH` | 开发 Agent |
| `requesting-code-review` | 请求代码审查（生成 PR、描述、测试计划） | `FEAT_AGENT_FINISH` | 开发 Agent |
| `receiving-code-review` | 接收审查反馈并迭代修复 | `REJECTED` -> `FEAT_AGENT` | 开发 Agent |
| `dispatching-parallel-agents` | 多 Agent 并行开发调度与管理 | 多 `FEAT` 并行时 | 集成 Agent / 人工 |

### 技能调用时机详解

- **`using-git-worktrees`**：任何 Agent 在操作 worktree 之前**必须先调用此技能**，遵循其 Step 0 检测 -> Step 1a 原生工具 -> Step 1b 手动回退的流程。
- **`writing-plans`** + **`subagent-driven-development`**：当特性涉及 3+ 文件修改、架构变更或复杂逻辑时，开发 Agent 在领取任务后必须先调用 `writing-plans` 生成实现计划，获得用户确认后再按 `subagent-driven-development` 执行。
- **`test-driven-development`**：新特性开发必须遵循 TDD 流程：RED（写测试）-> GREEN（实现通过）-> REFACTOR（重构）-> COVERAGE（验证 >=80%）。
- **`verification-before-completion`**：在将 `FEAT_AGENT` 改为 `FEAT_AGENT_FINISH` 之前，必须调用此技能完成验证检查清单，确保基线测试通过、无回归。
- **`dispatching-parallel-agents`**：当需要同时开发多个独立特性时，由集成 Agent 或人工调用此技能，并行派发开发 Agent，每个 Agent 在独立 worktree 中工作。

## Git Worktree 工作流说明

本工作流遵循 **using-git-worktrees** skill 的最佳实践，核心原则是：**先检测现有隔离环境，优先使用原生工具，再回退到手动 git worktree。**

### Step 0: 检测现有隔离环境（防嵌套）

任何 Agent 在创建 worktree 之前，**必须**先运行以下检测：

```bash
GIT_DIR=$(cd "$(git rev-parse --git-dir)" 2>/dev/null && pwd -P)
GIT_COMMON=$(cd "$(git rev-parse --git-common-dir)" 2>/dev/null && pwd -P)
```

- **若 `GIT_DIR != GIT_COMMON`（且不在子模块中）**：说明已处于隔离 worktree 中。**禁止**再创建嵌套 worktree，直接使用当前环境。
- **若 `GIT_DIR == GIT_COMMON`**：说明处于正常仓库，继续下一步。

> **子模块保护**：若 `git rev-parse --show-superproject-working-tree` 返回路径，说明你在子模块中，应按正常仓库处理。

### Step 1: 创建隔离工作区（优先原生工具）

#### 1a. 原生 Worktree 工具（优先）

当前环境提供原生 worktree 工具：`EnterWorktree` 和 `ExitWorktree`。Agent **必须优先使用**这些工具：

- 开发 Agent 领取任务后，调用 `EnterWorktree` 创建隔离环境并自动切换目录。
- 完成后调用 `ExitWorktree` 退出并清理。

> **严禁**：在有原生工具的情况下仍手动执行 `git worktree add`。

#### 1b. Git Worktree 手动回退（仅当无原生工具时）

若原生工具不可用，才允许手动创建 worktree。

**目录选择优先级**（由高到低）：
1. 项目已有的 `.worktrees/` 目录（隐藏目录优先）
2. 项目已有的 `worktrees/` 目录
3. 全局历史路径：`~/.config/superpowers/worktrees/<项目名>/`
4. 默认：项目根目录下的 `.worktrees/`

**安全检查（仅项目本地目录）**：

创建前必须验证目录已被 `.gitignore` 忽略：

```bash
git check-ignore -q .worktrees 2>/dev/null || git check-ignore -q worktrees 2>/dev/null
```

- **若未忽略**：先将目录加入 `.gitignore` 并提交，再继续创建 worktree。
- **原因**：防止 worktree 内容被意外提交到仓库。

**创建命令示例**：

```bash
project=$(basename "$(git rev-parse --show-toplevel)")
branch="feature/<特性名>"
path=".worktrees/$branch"
git worktree add "$path" -b "$branch"
cd "$path"
```

> **沙盒回退**：若 `git worktree add` 因权限错误失败，说明沙盒阻止了 worktree 创建。此时应在当前目录直接工作，并告知用户。

### Step 2: 独立开发

Agent 在隔离 worktree 内完成编码，**完成后必须执行自测**：

1. **项目设置自动检测**：根据项目类型运行对应命令
   - PlatformIO: `pio run -e m5stick-c`
   - Node.js: `npm install`
   - Rust: `cargo build`
   - Python: `pip install -r requirements.txt` 或 `poetry install`
2. **基线验证**：运行项目对应的测试套件
   - PlatformIO Native: `pio test -e native`
   - Node.js: `npm test`
   - Rust: `cargo test`
3. **推送特性分支**：`git push -u origin feature/<特性名>`
4. **更新条目状态**：将 `FEAT_AGENT` 改为 `FEAT_AGENT_FINISH`

> **若基线测试失败**：Agent 必须在提交成果前报告失败原因，并询问是否继续。禁止在测试未通过的情况下标记为 `FEAT_AGENT_FINISH`。

### Step 3: 集成触发

当一组关联特性均为 `FEAT_AGENT_FINISH` 时，集成 Agent 执行合并：

1. **创建集成 worktree**（遵循 Step 0-1 的隔离规范）
2. 基于主分支拉取最新代码
3. 按依赖顺序合并所有目标特性分支，解决冲突
4. **集成后冒烟测试**：运行完整的构建和测试流程，确认无重大缺陷
5. **标记状态**：将所有涉及条目改为 `FEAT_MERGED`，并附上：
   - 合并后的分支名（如 `integration/v2.3`）
   - 测试通过概况
   - 已知冲突及解决方案简述

### Step 4: 人工审查

审查者在集成 worktree 或基于集成分支的环境中验证功能：

- 通过 -> `VERIFIED`
- 驳回 -> `REJECTED`，必须在反馈中指明：
  - 是**整体架构问题**（集成 Agent 处理）
  - 还是**某个具体特性的缺陷**（对应开发 Agent 处理）

### Step 5: 发布与清理

`VERIFIED` 后：

1. 集成 Agent 将集成分支合并至主分支
2. 删除所有相关 worktree（开发 worktree + 集成 worktree）
3. 条目使用删除线标记关闭

> **Worktree 清理检查清单**：
> - [ ] 特性分支已合并到主分支
> - [ ] 开发 worktree 已删除（`git worktree remove <path>`）
> - [ ] 集成 worktree 已删除
> - [ ] 远程特性分支已删除（可选）

## Feat 列表格式规范

- [FEAT] 用户个人资料页面的需求描述
- [FEAT_AGENT] 订单导出CSV功能的需求描述
- [FEAT_AGENT_FINISH] 消息推送通知功能的需求描述
- [FEAT_MERGED] 搜索过滤器与排序联动整合（涉及 #12、#15 特性）
    - 集成概要：已合并至 `integration/search_v2`，轻量测试通过，等待人工全量审查
- [REJECTED] 夜间模式切换功能的需求描述
    - 审查意见：颜色对比度不足，需提供高对比度选项
- [VERIFIED] ~~管理员仪表盘图表功能的需求描述~~

**规则**：
- `[REJECTED]` 条目下必须缩进书写人工审查的具体反馈或修改要求。
- `[VERIFIED]` 条目描述使用删除线表示关闭。
- `[FEAT_MERGED]` 条目建议附上集成概要（合并范围、测试情况等），便于人工审查者掌握上下文。
- 当 Agent 将 `VERIFIED` 重新打开时，应去掉删除线并将标记改为 `[FEAT]`，并可选说明重新打开的原因。

## 特殊情况处理

1. **冲突无法自动解决**  
   集成 Agent 在合并时若遇到难以自动解决的冲突，可将相关 `FEAT_AGENT_FINISH` 条目中的某一个或几个改为 `REJECTED`，指明冲突细节，由对应开发 Agent 协调解决。

2. **单个特性在集成前被人工驳回**  
   人工审查者有权在特性处于 `FEAT_AGENT_FINISH` 阶段直接驳回（例如代码审查中发现严重问题），此时状态变为 `REJECTED`，集成 Agent 不应合并该条目。

3. **已验证特性的再次修改**  
   若已关闭特性需要增强或修复，直接将其重新打开为 `FEAT`，进入新一轮开发流程。

4. **Worktree 管理**  
   Agent 应定期清理无用 worktree，避免积累过多环境。集成 Agent 在合并完成后应及时移除集成 worktree。

5. **PlatformIO Worktree 编译与上传**  
   本项目使用 PlatformIO 构建系统，在 worktree 隔离环境中需注意以下事项：

   - **编译缓存共享**：`platformio.ini` 中已配置 `build_cache_dir = ${platformio.core_dir}/cache/build`，所有 worktree 共享编译缓存。首次编译会下载依赖并缓存对象文件，后续 worktree 编译相同未改动的源文件时直接使用缓存，大幅缩短编译时间。
   - **worktree 中禁止上传**：`pio run -e m5stick-c --target upload` 需要独占串口设备，多个 worktree 同时上传会导致冲突。开发 Agent 在 worktree 中**只能执行编译**（`pio run -e m5stick-c`），**严禁执行上传**。
   - **上传由人工在主仓库执行**：硬件烧录应在主仓库（`main` 分支）执行，或人工在确认代码无误后手动上传。
   - **优先使用 native 测试验证**：`pio test -e native` 是桌面模拟环境，不依赖硬件，适合在 worktree 中快速验证逻辑正确性。这是 worktree 自测的首选方式。
   - **构建目录独立**：每个 worktree 的 `.pio/build/` 目录仍独立（约 210MB+），但编译缓存共享后，增量编译的对象文件不会重复。worktree 删除时 `.pio/` 目录一并清理。
   - **libdeps 保持独立**：`.pio/libdeps/` 不共享，避免不同分支使用不同版本的库导致冲突。PlatformIO 会在首次编译时自动下载所需依赖。

---

# FEAT 列表

<!-- 请按照上方格式在下方添加特性条目 -->

- [VERIFIED] ~~**UI 框架深度重构** — 对 `src/ui/` 全部核心模块进行系统性重构：~~
   1. **可维护性优化**：简化复杂的 API 逻辑（消除向后兼容宏、统一内存管理、标准化错误处理）
   2. **性能优化**：减少每帧重复计算（字体宽度缓存、脏区域跟踪、静态元素跳过）
   3. **逻辑整理**：分离主循环职责（lifecycle vs rendering）、引入类型派发表替代内联 switch
   4. **死代码清理**：移除 `xerintosh_ui_driver_init()` 等薄包装、消除重复的位置计算逻辑
   5. **可复用逻辑提取**：将 `is_item_visible()`、动画 easing、类型派发等模式抽取为公共工具
- [VERIFIED] ~~我想把当前的蓝牙框架彻底重构，我想使用classic BLE的协议而不是当前的方案。我需要你做的是把当前的蓝牙框架彻底卸载换成支持classic BLE的协议，并且对当前ui系统中含有蓝牙的部分也修改成适配该协议的方案。~~
    - 分支: `feature/bt-classic-spp`
    - 概要: 已将 NimBLE-Arduino (BLE GATT/NUS) 完全替换为 ESP32 Arduino 内置 BluetoothSerial (Classic Bluetooth SPP)。bt_manager 状态机大幅简化（移除扫描/广告/配对码流程），bt_uart_service API 保持不变，UI 文本从 "BLE" 统一改为 "BT/蓝牙"。硬件编译通过（Flash 99.0%, RAM 21.4%），native 测试通过（186/187，末尾 SIGSEGV 为既有内核 IPC 问题）。
- [VERIFIED] ~~当前蓝牙这个功能已经调试完毕，串口也能正常的传输数据和连接设备。在root菜单里面有蓝牙串口这个程序，我在仔细评估之后认为该app与串口监视器的这个app存在功能上的重合。因此我想把蓝牙串口这个app删除并且把功能整合进入串口监视器这个程序中。下面是大致的施行顺序。~~
   1. 首先对于NORM/DEBUG模式，我在几天的重度使用发现我基本上不会用到DEBUG模式，请删除掉相关的代码默认只使用norm模式，并且把原先UI界面的NORM/DEBUG按钮改成SER/BLE（有线串口/蓝牙串口）模式，并且原先的操控逻辑操控SER/BLE按钮的时候会切换对应的模式。对于串口的显示逻辑则有线串口/蓝牙串口为一致的。
   2. 修改调试完毕之后删除掉之前的死代码，保持项目的清洁。
   3. 最后更新相关的文档。
      - 分支: `feature/bt-serial-merge`
      - 概要: 已将独立的 ble_serial App 删除，功能整合到 serial_monitor App 中。用 SER/BLE 模式切换替代原有的 NORM/DEBUG 模式切换。删除 NORM/DEBUG 相关代码、ble_serial 源文件和测试文件。Native 测试 186/187 通过（末尾 SIGSEGV 为既有内核 IPC 问题），硬件编译成功（RAM 21.2%, Flash 91.2%）。
- [VERIFIED] ~~我想做一个app，是把一些我的token用量实施显示到当前的开发板上~~
   - Deepseek：https://platform.deepseek.com/usage
      - 需要显示我的余额，本月消费是多少
      - API暂时给你：[REDACTED-DEEPSEEK]
   用户后续也可以通过串口来更改api-key，就像输入wifi密码那样。
   - 计划生成完毕默认使用subagent-driven
- [FEAT_AGENT] 工作流：分析当前内核的缺陷和待优化的地方 --> 更具分析报告生成分模块的执行计划。因为本次FEAT是大的变更，所以请你在先在主工作区创立一个全新的分支，之后再去创立worktree，并且最后合并在这个创立的分支上。
   - 对于内核的核心逻辑进行优化，尤其是当前的协作式内核我觉得已经不能满足对于该系统的开销，我想优化成抢占式内核。并且支持SMP（多核调度）。
   - 内存保护是缺失的，你可以利用MPU和系统调用来保护内存。一个任务崩溃，MPU 会触发 MemManage 异常，内核可以捕获它并优雅地重启该任务，而不会让整个系统宕机。
   - 实现资源追踪和安全终止，在内核的任务控制块 (TCB) 中，增加一个资源列表，记录本任务持有的所有互斥锁、信号量、动态内存指针。当任务退出或被强制终止时，内核遍历这个列表，自动释放所有锁并回收内存。这就需要一个更规范的内存分配器，并和内核紧密集成。
   - 将调度算法从内核核心逻辑中解耦出来。定义一个“调度器接口”（比如 scheduler_select_next() 函数指针）。你可以实现多个调度类：固定优先级类、时间片轮转类、EDF 类。任务创建时可以选择它属于哪个调度类，内核按优先级顺序查询这些类，选出下一个任务。这就像 Linux 的调度类（CFS, RT, DEADLINE）的极简版。
   - 构建驱动开发规范体系，定义统一的设备模型：struct device，包含名称、open/read/write/ioctl 等统一的函数指针。创建一个虚拟文件系统（DevFS）或者简单的设备注册表，让应用程序可以通过名字打开设备，获得句柄。将中断处理程序统一管理，底层硬件中断在驱动中处理为数据，然后通过你之前用过的“消息队列”发送给等待该设备的任务。
