# App 层重构报告

> **Parent:** [doc/refactor/README.md](../README.md) | **Prev:** [UI 核心层重构报告](ui.md)

## 目标

显式化 App 层菜单与设置模块的接口契约，将过长的 `app_menu.c` 拆分为职责单一的文件，集中设置项等级与硬件值的转换逻辑，并补齐电源键弹窗的 native 测试覆盖，使 App 层更易于扩展、测试与维护。

## 变更摘要

### 1. 补齐电源键弹窗测试（A4）

`power_key_popup` 已独立为 `src/app/shutdown/power_key_popup.c` 模块，`app_input.c` 仅调用其 API。本次补充 native 测试，验证初始化后双键状态与可见性标志被正确清除。

*📄 Source: [power_key_popup.h](../../../src/app/shutdown/power_key_popup.h)*
*📄 Source: [power_key_popup.c](../../../src/app/shutdown/power_key_popup.c)*
*📄 Source: [test_power_key_popup.cpp](../../../test/test_native/test_power_key_popup.cpp)*

### 2. 显式化 user_item 生命周期契约（A2）

新增 `src/app/user_item_contract.h`，显式声明所有内置 `user_item` App 必须遵循的生命周期回调类型：

- `user_item_init_fn_t`：进入 App 时调用一次
- `user_item_loop_fn_t`：每帧循环调用
- `user_item_exit_fn_t`：退出 App 时调用一次
- `user_item_contract_t`：将显示名称与三个回调绑定，便于菜单注册与编译期签名检查

*📄 Source: [user_item_contract.h](../../../src/app/user_item_contract.h)*
*📄 Source: [test_user_item_contract.cpp](../../../test/test_native/test_user_item_contract.cpp)*

### 3. 集中设置项 level↔hw 转换（A3）

在 `settings.h` / `settings.c` 中新增统一转换入口：

- `settings_level_to_hw(settings_kind_t kind, int16_t level)`：将等级转换为硬件/内部值
- `settings_hw_to_level(settings_kind_t kind, int32_t hw)`：将硬件/内部值反向映射为等级

支持的设置类型：亮度、动画速度、弹簧硬度、弹簧阻尼、波特率。`main.cpp` 中亮度与动画速度的回调改为通过该统一入口计算，避免调用点分散维护映射逻辑。

*📄 Source: [settings.h](../../../src/app/settings/settings.h)*
*📄 Source: [settings.c](../../../src/app/settings/settings.c#L254-L314)*
*📄 Source: [test_settings_conversion.cpp](../../../test/test_native/test_settings_conversion.cpp)*

### 4. 拆分 `app_menu.c`（A1）

将原本混合框架与条目注册的 `app_menu.c` 拆分为：

- `app_menu_core.c` / `app_menu_core.h`：菜单框架，负责 `app_menu_build()` 与安全挂载辅助 `app_menu_push_checked()`
- `app_menu_entries.c` / `app_menu_entries.h`：条目注册，负责设置子菜单、波特率子菜单与所有 `user_item` App 入口

拆分后保留原始行为：

- 根菜单下先挂载 6 个 `user_item` App，再挂载“设置”子菜单
- 设置子菜单保持原有 10 个子项：WiFi、亮度、动画效果、动画速度、动画风格、弹动硬度、反弹力度、横屏/竖屏、烧录器引脚、波特率
- `user_item` App 保持拆分前的图标选择（任务管理器/关于使用 `user_icon`，其余使用 `default_icon`）

*📄 Source: [app_menu_core.h](../../../src/app/app_menu_core.h)*
*📄 Source: [app_menu_core.c](../../../src/app/app_menu_core.c)*
*📄 Source: [app_menu_entries.h](../../../src/app/app_menu_entries.h)*
*📄 Source: [app_menu_entries.c](../../../src/app/app_menu_entries.c)*
*📄 Source: [test_app_menu_structure.cpp](../../../test/test_native/test_app_menu_structure.cpp)*

## 新增 / 修改测试

| 测试文件 | 测试名 | 覆盖点 |
|---|---|---|
| `test/test_native/test_power_key_popup.cpp` | `PowerKeyPopupTest.InitClearsDualActive` | 弹窗初始化后双键状态与可见性清零 |
| `test/test_native/test_user_item_contract.cpp` | `UserItemContractTest.AllBuiltinAppsMatchContract` | 6 个内置 App 均暴露非空 lifecycle 回调 |
| `test/test_native/test_settings_conversion.cpp` | `SettingsConversionTest.BrightnessLevelToHw` | 亮度等级 → PWM 值，且与全局解耦 |
| `test/test_native/test_settings_conversion.cpp` | `SettingsConversionTest.AnimSpeedLevelToHw` | 动画速度等级 → 内部值，且与全局解耦 |
| `test/test_native/test_settings_conversion.cpp` | `SettingsConversionTest.BrightnessLevelToHwClampsOutOfRange` | 亮度越界值被 clamp 到 [1,10] |
| `test/test_native/test_settings_conversion.cpp` | `SettingsConversionTest.AnimSpeedLevelToHwClampsOutOfRange` | 动画速度越界值被 clamp 到 [1,10] |
| `test/test_native/test_settings_conversion.cpp` | `SettingsConversionTest.BaudLevelToHw` | 波特率等级 → 实际波特率 |
| `test/test_native/test_settings_conversion.cpp` | `SettingsConversionTest.SpringStiffnessLevelToHw` | 弹簧硬度等级 → 硬件整数值 |
| `test/test_native/test_settings_conversion.cpp` | `SettingsConversionTest.HwToBrightnessLevel` | 亮度硬件值反向映射为等级 |
| `test/test_native/test_app_menu_structure.cpp` | `AppMenuStructureTest.RootHasSettingsAndBuiltinApps` | 拆分后菜单树结构不变 |

## 验证结果

- `pio test -e native`：**通过**（574 cases，2 skipped，572 succeeded）
- `pio run -e m5stick-c`：**通过**，无新增编译警告
- `pio run -e m5stick-c-native`：**通过**，无新增编译警告

## 风险与后续注意

- `settings_level_to_hw()` 的亮度/动画速度分支已由“读取全局”修正为“使用传入 level 参数”，调用方需确保传入当前等级而非依赖函数内部状态。
- `app_menu_entries.c` 仍直接引用各 `user_item` App 头文件；新增 App 时需在 `s_user_item_apps[]` 中注册，并在 `s_user_item_icons[]` 中指定图标。
- `power_key_popup` 的状态机仍由 `app_input.c` 驱动，后续若扩展弹窗行为应保持该模块边界。

## 相关提交

```
8d59d0a test(app): add power_key_popup init state tests (A4)
60d3919 feat(app): formalize user_item lifecycle contract (A2)
ab35159 style(app): add standard Doxygen file header to user_item_contract.h
c51da27 refactor(app): centralize settings level↔hw conversion (A3)
fc094e5 fix(app): make settings_level_to_hw honor level parameter for brightness/anim_speed
bb3b266 refactor(app): split app_menu.c into core and entries (A1)
```

---

> **See Also:** [UI 核心层重构报告](ui.md) | [下一阶段：文档体系](docs.md)
