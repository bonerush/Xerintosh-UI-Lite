# Xerintosh 项目指令

## 验证规则

**核心原则：先电脑验证，随后传到实机验证。**

所有代码修改后必须严格遵循两步验证流程，不得跳过任何一步。

### 第一步：电脑端验证 (Computer Verification)

```bash
# 1. Native 测试（必须全部通过）
pio test -e native

# 2. 硬件目标构建（必须无警告）
pio run -e m5stick-c

# 3. Xeros 原生调度构建（内核层修改后强制要求）
pio run -e m5stick-c-native
```

电脑端全部通过后，再进入第二步。

### 第二步：实机验证 (Device Verification)

```bash
# 烧录（默认使用 Xeros 原生调度器版本）
pio run --target upload

# 串口监控
pio device monitor -e m5stick-c-native
```

**冒烟测试清单：**
1. 开机画面正常显示
2. 菜单导航响应流畅（100Hz-60Hz VRR target）
3. 设置项切换即时生效
4. WiFi 扫描/连接正常
5. 内核 shell 命令正常响应
6. 长时间运行无崩溃（≥ 5 分钟稳定性测试）

### 失败处理

| 阶段 | 失败时动作 |
|------|-----------|
| 电脑端验证失败 | **禁止烧录**。修复后重新验证。 |
| 实机验证失败 | 记录崩溃信息，标记阶段为 `BLOCKED`。 |

## 默认上传目标

本项目默认使用 **Xeros 原生调度器** (`m5stick-c-native`) 环境。

```bash
# 上传（自动使用 m5stick-c-native）
pio run --target upload

# 明确指定
pio run -e m5stick-c-native --target upload
```

## 构建命令速查

```bash
# 全量验证（电脑端）
pio test -e native && pio run -e m5stick-c && pio run -e m5stick-c-native

# 烧录 + 监控
pio run --target upload && pio device monitor -e m5stick-c-native
```

## 重构规则

见 `.claude/skills/refactor-workflow-xeros/SKILL.md` 和 `doc/refactor/README.md`。

核心约束：
1. 一次只动一层（内核 → HAL → UI → App → 文档）
2. 先加测试再改代码
3. 每阶段结束必须可 `git revert`
4. 关注 ESP32-PICO 520KB SRAM 限制
