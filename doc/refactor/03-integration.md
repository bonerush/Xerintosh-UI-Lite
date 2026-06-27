# 集成验证报告

> **Parent:** [doc/refactor/README.md](README.md)

## 验证范围

本轮集成验证覆盖阶段 2（kernel / HAL / UI / App / docs）全部分层重构完成后的回归检查，确保无跨层回归、无新增编译警告、文档链接有效。

## 验证结果

### 1. 全量 native 测试

```bash
pio test -e native
```

- **状态**：通过
- **结果**：574 test cases，2 skipped，572 succeeded
- **说明**：跳过 2 个测试与 ESP32 上下文大小相关，在 native 环境下预期跳过。

### 2. 硬件目标构建

```bash
pio run -e m5stick-c
```

- **状态**：SUCCESS
- **RAM**：19.5%（63832 / 327680 bytes）
- **Flash**：73.2%（1124673 / 1536000 bytes）
- **新增警告**：无

### 3. ESP32 native 调度构建

```bash
pio run -e m5stick-c-native
```

- **状态**：SUCCESS
- **RAM**：19.5%（63984 / 327680 bytes）
- **Flash**：73.9%（1135017 / 1536000 bytes）
- **新增警告**：无

### 4. 文档链接检查

使用脚本扫描 `doc/` 下所有 Markdown 文件中的内部相对链接，结果如下：

- **本轮新增链接**：全部有效（`doc/ui/index.md`、`doc/app/index.md`、`doc/tutorials/api-templates.md` 及其相互引用）。
- **预存在断链**（未由本轮重构引入）：
  - `doc/freertos-remaining-references.md` → `../.claude/plans/tingly-mixing-sundae.md`
  - `doc/index.md` → `reference/index.md`
  - `doc/debug-xeros-native.md` → `../index.md`、`../implementation-plan.md`、`../freertos-remaining-references.md`
  - `doc/architecture/context-switch.md` → `../../src/kernel/kern_ctx_esp32.h`
  - `doc/kernel/port.md` → `../../../test/test_native/test_kernel_sched.cpp#L486-L497`
- **待阶段 4 创建后生效**：`doc/refactor/README.md` 与 `doc/refactor/02-refactor/docs.md` 中的 `03-integration.md`、`04-archive.md` 引用。

### 5. 硬件冒烟测试

- **状态**：已执行，并修复一处 UI 回归后重新烧录
- **烧录命令**：`pio run -e m5stick-c-native --target upload`
- **复位/日志命令**：`python tools/xeros_debug.py --reset --wait-boot --cmd ps /dev/cu.usbserial-4D52671EFA`
- **结果**：
  - 烧录成功（ESP32-PICO-D4，MAC `94:b9:7e:93:15:34`）
  - 设备复位后正常启动，检测到 `[  BOOT] M5Stick-P1 kernel starting...`
  - UART、NVS、Display、UI、Xeros kernel、SMP 双核调度、WiFi manager、shell 均正常初始化
  - `ps` 命令输出正常：idle、shell、ui、main-loop、wifi-mgr 任务状态符合预期
  - UI 帧循环正常：`ui frame 1/2/3/4/5 begin/flush done/yielding/resumed` 连续输出
- **修复的 UI 回归**：
  - 问题：选中超出初始屏幕范围的菜单项时，选择器内不显示文字、无反色高亮
  - 根因：`src/ui/ui_draw_list.c` 拆分后 `item_text_is_visible()` 把文字基线可见性误判为文字底部可见性，导致底部 4px 内的文字被跳过
  - 修复：恢复为判断文字基线（`y_center`）是否位于 `(LIST_INFO_BAR_HEIGHT, HAL_SCREEN_HEIGHT)`
  - 回归测试：`test/test_native/test_ui_draw_list_item.cpp` 新增 `DrawsTextForItemNearBottomEdgeAfterCameraScroll`
  - 修复后重新烧录：native 测试 573 succeeded、硬件构建无新增警告、`m5stick-c-native` 构建与烧录均成功
- **未验证项**：菜单导航、设置项切换、WiFi 菜单、电源键弹窗等人工交互项仍需现场确认。

## 结论

阶段 3 集成验证通过，包括全量 native 测试、硬件构建、ESP32 native 调度构建、文档链接检查、实机烧录启动验证与上述 UI 回归修复。人工交互项建议后续现场补充确认。

## 相关提交

```
1d97d76 docs(refactor): mirror doc structure to src and fix broken api-templates link (D1-D3)
<当前提交>  docs(refactor): add integration verification report and mark stage 3 complete
```

---

> **See Also:** [阶段 2 文档体系重构报告](02-refactor/docs.md) | [阶段 4 归档报告](04-archive.md)
