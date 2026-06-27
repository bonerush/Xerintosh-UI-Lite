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

- **状态**：未执行
- **原因**：本轮集成验证在软件层面完成；实机烧录与菜单导航、WiFi 菜单、电源键弹窗等人工验证需用户配合后续进行。
- **建议动作**：
  1. `pio run -e m5stick-c-native --target upload`
  2. `python tools/xeros_debug.py --reset --wait-boot --cmd ps`
  3. 人工验证菜单导航、设置项切换、WiFi 菜单、电源键弹窗

## 结论

阶段 3 软件层面集成验证通过，可进入阶段 4 文档归档与分支收尾。硬件冒烟测试标记为待执行。

## 相关提交

```
1d97d76 docs(refactor): mirror doc structure to src and fix broken api-templates link (D1-D3)
<当前提交>  docs(refactor): add integration verification report and mark stage 3 complete
```

---

> **See Also:** [阶段 2 文档体系重构报告](02-refactor/docs.md) | [阶段 4 归档报告](04-archive.md)
