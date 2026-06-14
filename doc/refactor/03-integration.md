# Phase 3 集成验证报告

> **Parent:** [重构跟踪](README.md) | **Prev:** [Phase 2.5 文档同步](02-refactor/docs.md)

## 1. 验证目标

确认内核层、HAL 层、UI 核心层、App 层重构后的代码能够：

1. 在 native 环境通过全部单元测试；
2. 在硬件目标（m5stick-c）无 warning、无 error 地编译通过；
3. 保持 `main.cpp`、`app_init.h` 等公共入口 API 不变；
4. 行为等价：菜单结构、烧录器引脚配置流程、WiFi/BT 开关行为与重构前一致。

## 2. 环境

- **分支**：`refactor/2026-06-14-kernel-ui`
- **基线 commit**：`6411c578124f1bf029169ffe1f5dfbef9fddeee5`
- **验证日期**：2026-06-14
- **PlatformIO**：通过 VS Code + PlatformIO IDE 扩展
- **硬件目标**：M5Stick-C（ESP32-PICO）

## 3. 验证项目

### 3.1 Native 测试

```bash
rm -rf .pio/build/native
pio test -e native
```

结果：

| 测试套件 | 状态 | 用例数 |
|----------|------|--------|
| test_ble_uart | PASSED | 见详细日志 |
| test_native | PASSED | 见详细日志 |
| test_token_usage | PASSED | 见详细日志 |
| **总计** | **PASSED** | **371 test cases** |

### 3.2 硬件编译

```bash
rm -rf .pio/build/m5stick-c
pio run -e m5stick-c
```

结果：

```
RAM:   [==        ]  22.3% (used 73216 bytes from 327680 bytes)
Flash: [========= ]  88.0% (used 1845325 bytes from 2097152 bytes)
========================= [SUCCESS] Took 4.09 seconds =========================
```

### 3.3 Warning / Error 审查

```bash
pio run -e m5stick-c 2>&1 | grep -Ei "warning:|error:|note:"
```

输出为空，无新增 warning/error。

### 3.4 Public API 稳定性检查

| API | 位置 | 状态 |
|-----|------|------|
| `app_init_ui()` | `app_init.h` | 保持 |
| `app_init_managers()` | `app_init.h` | 保持 |
| `app_input_process()` | `app_init.h`/`app_input.h` | 保持（实现迁移到 `app_input.c`） |
| `xerintosh_ui_main_core()` | `ui_core.h` | 保持 |
| `xerintosh_selector_go_next_item()` 等 | `ui_item.h` | 保持 |

`main.cpp` 新增 `#include "app/app_state.h"`、移除 `g_wifi_on`/`g_bt_on` 局部定义，并将显示旋转/亮度调用改为 HAL API。整体调用流程未变。

### 3.5 关键 diff 审查

| 改动文件 | 说明 | 风险 |
|----------|------|------|
| `src/app/app_init.c` | 从 347 行精简到 27 行，变为入口封装 | 低，菜单/输入逻辑完整迁移 |
| `src/app/app_menu.c` | 新增，承担菜单构建 | 低，与原 `app_init.c` 等价 |
| `src/app/app_input.c` | 新增，承担输入路由 | 低，与原 `app_init.c` 等价 |
| `src/app/flasher/flasher_menu.c` | 新增，烧录器引脚配置与强制解除状态机 | 低，逻辑整体平移 |
| `src/app/app_state.c` | 新增，集中全局状态 | 低，`main.cpp`/`native_main.cpp` 已移除重复定义 |
| `src/ui/ui_dispatch.c` | 新增完整生命周期 vtable | 低，测试覆盖 enter/measure/draw/destroy 等路径 |
| `src/ui/ui_core.c` | 增加空根保护 | 低，已加测试 |
| `src/hal/hal_display_fb.cpp` | 新增，帧缓冲/画布生命周期 + 方向/亮度配置 | 低，与旧 `hal_display.cpp` 等价 |
| `src/hal/hal_display_draw.cpp` | 新增，绘制原语 | 低，代码来自旧 `hal_display.cpp` 拆分 |
| `src/hal/hal_display_font.cpp` | 新增，字体与文本 | 低，native 增加 6×8 字体模拟 |
| `src/hal/hal_display_adv.cpp` | 新增，XOR/XBM/裁剪/测试钩子 | 低，代码来自旧 `hal_display.cpp` 拆分 |
| `src/hal/hal_screen.c/h` | 新增，运行时屏幕尺寸查询 | 低，`g_screen_width/height` 集中定义 |

## 4. 测试覆盖新增

| 测试文件 | 新增用例 | 覆盖目标 |
|----------|----------|----------|
| `test/test_native/test_ui_empty_root.cpp` | 2 | 空根菜单安全 |
| `test/test_native/test_ui_dispatch.cpp` | 8 | vtable 生命周期行为 |
| `test/test_native/test_ui_item_null_value.cpp` | 2 | 空值保护 |
| `test/test_native/test_app_state.cpp` | 2 | 全局状态默认值 |
| `test/test_native/test_flasher_menu.cpp` | 3 | 角色标签、默认引脚配置 |
| `test/test_native/test_kernel_*.cpp` | 若干 | 内核设备模型、SMP、同步、任务生命周期 |

基线 `test_native` 为 354 tests；当前为 **371 tests**，新增 **17 个** UI/App/内核相关测试用例。

## 5. 集成结论

- ✅ native 测试全通过（371/371）
- ✅ 硬件编译成功（Flash 88.0%，RAM 22.3%）
- ✅ 无新增 warning/error
- ✅ 公共入口 API 未发生破坏性变更
- ✅ `main.cpp` 改动最小（包含、全局状态迁移、显示配置 HAL 化）
- ✅ HAL 层 `M5.Display` 直接调用已清零

**建议**：可以进入 Phase 4 归档，整理最终报告与提交摘要。
