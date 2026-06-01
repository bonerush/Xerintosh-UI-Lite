# Debug 工作流说明文档

本文档用于跟踪项目 Bug 的修复与审查流程。所有 Bug 以列表形式记录在下方 [BUG列表] 区域，Code Agent 与人工审查者根据此清单协同工作。

## 状态定义

| 状态标记 | 含义 | 通常由谁设置 |
|----------|------|------------|
| `BUG` | 新发现或重新打开的 Bug，尚未指派给 Agent | 人工 / Agent（从 VERIFIED 重新激活） |
| `AGENT` | Agent 正在处理此 Bug（占用状态，其他 Agent 应跳过） | Agent |
| `AGENT_FINISH` | Agent 完成修复，等待人工审查 | Agent |
| `VERIFIED` | 人工审查通过，Bug 关闭 | 人工 |
| `REJECTED` | 人工审查驳回，需要 Agent 根据反馈重新处理 | 人工 |

## 角色与权限

- **Agent** 可以执行以下状态转换：
    - （新建） → `BUG` （用户与agent交流的时候明确需要处理什么bug可以新添）
    - `BUG` → `AGENT` （领取任务）
    - `AGENT` → `AGENT_FINISH` （提交修复结果）
    - `REJECTED` → `AGENT` （根据驳回意见重新开始）
    - `VERIFIED` → `BUG` （发现已验证的 Bug 再次出现，重新打开）
- **人工审查者** 可以执行以下状态转换：
    - （新建） → `BUG` （发现新 Bug 时录入）
    - `AGENT_FINISH` → `VERIFIED` （确认修复有效）
    - `AGENT_FINISH` → `REJECTED` （驳回并附上测试意见）

并发说明：当有多个 Agent 并行工作时，应优先选择状态为 `BUG` 的条目。状态为 `AGENT` 的条目表示已被其他 Agent 占用，请跳过。

## 通用流程

```mermaid
stateDiagram-v2
    [*] --> BUG : 创建/重新打开
    BUG --> AGENT : Agent领取
    AGENT --> AGENT_FINISH : Agent完成修复
    AGENT_FINISH --> VERIFIED : 人工验证通过
    AGENT_FINISH --> REJECTED : 人工驳回
    REJECTED --> AGENT : Agent重新处理（循环）
    VERIFIED --> [*] : 关闭
    VERIFIED --> BUG :
│ 空格 + pU │ 上传 + 串口监控 │  │ 再次出现时重新激活
```

## Bug 列表格式规范

列表中的每一项必须包含状态标记和描述。使用以下格式（建议保留删除线以标记已关闭项）：
- [BUG] 文件上传时偶发崩溃的错误描述
- [AGENT] 登录页面样式错乱的错误描述
- [AGENT_FINISH] 接口超时无重试的错误描述
- [] 搜索功能结果不准确的错误描述
    - 测试意见：关键词“苹果”应返回水果结果，目前返回手机壳
- [VERIFIED] ~~首页加载速度慢的错误描述~~

**规则**：
- `[REJECTED]` 条目下必须缩进书写人工反馈的测试结果或意见。
- `[VERIFIED]` 条目描述使用删除线，表示已关闭。
- 当 Agent 将 `VERIFIED` 重新打开时，应去掉删除线并将标记改为 `[BUG]`，并可选地追加说明。

## 特殊情况处理

1. **发现重复 Bug（未被验证）**  
   如果 Agent 在创建 Bug 时发现列表中已存在相同或相似问题，且状态为 `BUG`、`AGENT`、`AGENT_FINISH` 或 `REJECTED`，不应新建条目，可直接操作现有条目（例如将 `BUG` 改为 `AGENT` 开始处理）。

2. **已验证的 Bug 再次出现**  
   如果发现某个问题曾被标记为 `[VERIFIED]`，但现在又复现了，Agent 可将该条目状态改为 `[BUG]` 并去除删除线，从而重新进入待处理队列。必要时在描述中补充复现信息。

---

# BUG 列表

<!-- 请按照上方格式在下方添加 Bug 条目 -->

- [VERIFIED] ~~竖屏模式下关机弹窗宽度超出屏幕显示范围，需做横竖屏弹窗尺寸适配~~
    **根因:** ① `strcmp` 静态缓冲区误判；② 换行算法要求 `acc <= avail && p >= mid`；③ 宽度钳位到 `SCREEN_WIDTH` 未扣除边框 8px。→ 驳回后：文字基线绘制导致 descent 紧贴弹窗底边，POP_UP_HEIGHT=44 在 3 行时无余量。→ 再次调整：横屏 1 行文字时固定高度 48 导致上下空白过多
    **修复:** POP_UP_HEIGHT 从 44 增至 48，为文字多提供 4px 垂直空间；`y_text` 计算公式中将 `+ fh` 改为 `+ fh - 2`，使基线下移量从 fontHeight 改为近似 ascent，文字整体居中的同时上下各留出约 4px 空隙。→ 动态高度：`xerintosh_push_pop_up` 和 `xerintosh_draw_pop_up` 中根据 `wrap_line_count` 实时计算弹窗高度（内容高度 + 上下各 4px padding），横屏 1 行时高度约 20px，竖屏 3 行时高度约 48px，各种方向下 padding 保持一致
    **验证:** build=PASS native=PASS tests=186/187 (1个预存SIGSEGV与本次无关)
    **文件:** src/ui/ui_item.h, src/ui/ui_draw_widgets.c, src/ui/ui_item_popup.c
    **驳回重处理:** ① 在原有横竖屏宽度适配基础上，针对竖屏 3 行文字增加 POP_UP_HEIGHT 并修正基线偏移，消除文字碰底问题。② 将固定 POP_UP_HEIGHT 改为运行时根据内容行数动态计算，使横屏/竖屏的上下 padding 保持一致
- [VERIFIED] ~~关机弹窗关闭逻辑：应改为长按开始倒计时、松手立即停止并收起弹窗~~
    **根因:** 双键松手后 `g_dual_active` 立即置 false，但 `M5.BtnX.wasReleased()` 的释放边沿在下一帧 `hal_input_get_event()` 中被 `hal_input_simple_process` 的 `!long_fired` 路径判定为 SHORT_PRESS，立即触发菜单导航/确认操作
    **修复:** 在 `power_key_popup_update()` 的 else 分支中增加 300ms 冷却期（`DUAL_RELEASE_COOLDOWN_MS`），刚从双键状态退出时调用 `hal_input_reset_events()` 重置状态机，并在冷却期内继续让 `power_key_popup_is_dual_active()` 返回 true，隔离所有正常按钮事件直至释放边沿被 M5.update() 自然清除
    **验证:** build=PASS native=PASS tests=186/187 (1个预存SIGSEGV与本次无关)
    **文件:** src/app/shutdown/power_key_popup.c
    **驳回重处理:** 在原有松手收起弹窗逻辑基础上，增加冷却期防止按钮释放边沿被误判为短按/长按，避免松手慢时触发
- [BUG] 对于蓝牙设备的扫描,大部分设备都是类似硬件地址那么一长串,但是确扫不到真正可用的设备,例如我想把我的开发板连接到我的电脑,我的电脑就没有办法扫描到开发板.