# 阶段 2.3 HAL 层重构微观实施计划

## 1. 本轮 HAL 层目标

### 处理的问题
- **H-P1-01**：`src/hal/hal_display_fb.cpp:86-93`，`setColorDepth(8)` 与 `createSprite()` 顺序依赖人工记忆，无封装/断言保护。
- **H-P2-01**：`src/hal/hal_system.cpp:39-41`，Native 环境下 `hal_delay_ms()` 为空操作，与硬件环境语义不一致。

### 明确延后的问题
- **H-P1-02**：`src/hal/hal_input.cpp:137-144` 与 `src/hal/hal_input_double_click.c` 的输入事件模型统一。

> 延后理由：H-P1-02 涉及简单状态机与双击状态机共享 `btn_state.dc` 的事件消费边界，需要硬件验证短按/长按/双击的竞争行为；改动面较大，放到后续专门的输入子系统轮次处理更可控。

---

## 2. 子任务列表

### T1：H-P1-01 — 封装 M5Canvas 色深 + 创建精灵顺序

- **目标问题**：`setColorDepth(8)` 与 `createSprite()` 顺序依赖人工记忆，颠倒会导致 alpha=0 黑屏。
- **变更文件**：
  - `src/hal/hal_display_fb.cpp`
  - `doc/hal/display.md`
  - `test/test_native/test_hal_display.cpp`（仅追加编译/链接保护测试）
- **步骤**：
  1. 在 `src/hal/hal_display_fb.cpp` 硬件分支（`#else` 之后）新增静态 helper：
     ```cpp
     static void hal_display_create_sprite(M5Canvas* canvas,
                                           int16_t w, int16_t h,
                                           uint8_t depth)
     {
         canvas->setColorDepth(depth);
         canvas->createSprite(w, h);
     }
     ```
  2. 将 `hal_display_init()` 中的：
     ```cpp
     g_canvas->setColorDepth(8);
     ...
     g_canvas->createSprite(g_screen_width, g_screen_height);
     ```
     替换为：
     ```cpp
     hal_display_create_sprite(g_canvas,
                               g_screen_width,
                               g_screen_height,
                               8);
     ```
  3. 保留 `g_canvas` 延迟创建逻辑不变。
  4. 更新 `doc/hal/display.md` 中“关键顺序”段落，说明已封装为 helper，上层无需再记忆顺序。
  5. 在 `test_hal_display.cpp` 追加一个编译/链接保护测试，确保 `hal_display_init()` 在 native 与硬件路径下均可正常链接。
  6. 执行：
     ```bash
     pio test -e native
     pio run -e m5stick-c
     ```
  7. `git commit`。
- **回滚策略**：删除 `hal_display_create_sprite` helper，恢复 `hal_display_init()` 中原有的两行顺序调用。
- **验收标准**：
  - `m5stick-c` 编译通过且无新增 warning。
  - `pio test -e native` 全部通过。
  - 新增/修改的 native 显示测试通过。

---

### T2：H-P2-01 — Native `hal_delay_ms()` 实现真实延时

- **目标问题**：Native 环境下 `hal_delay_ms()` 为空操作，导致依赖延时的代码语义与硬件不一致。
- **变更文件**：
  - `src/hal/hal_system.cpp`
  - `doc/hal/system.md`
  - `test/test_native/test_hal_system.cpp`（新建）
- **步骤**：
  1. 在 `src/hal/hal_system.cpp` 的 `NATIVE_TEST` 分支顶部增加 `#include <thread>`。
  2. 将：
     ```cpp
     void hal_delay_ms(uint32_t ms) {
         (void)ms;
     }
     ```
     替换为：
     ```cpp
     void hal_delay_ms(uint32_t ms) {
         std::this_thread::sleep_for(std::chrono::milliseconds(ms));
     }
     ```
  3. 新建 `test/test_native/test_hal_system.cpp`：
     - `DelayAtLeast50ms`：使用 `std::chrono::steady_clock` 测量 `hal_delay_ms(50)` 的实际耗时，断言 `elapsed >= 45ms`。
     - `DelayZeroDoesNotCrash`：调用 `hal_delay_ms(0)` 不崩溃、不阻塞异常。
  4. 更新 `doc/hal/system.md`：
     - 修改“Native 测试实现”中的 `hal_delay_ms()` 代码片段；
     - 删除“Native 测试中空实现”的表述；
     - 在伪代码中改为“调用线程睡眠”。
  5. 执行：
     ```bash
     pio test -e native
     pio run -e m5stick-c
     ```
  6. `git commit`。
- **回滚策略**：恢复 `hal_delay_ms(uint32_t ms) { (void)ms; }` 空实现；删除 `<thread>` 引入。
- **验收标准**：
  - 新增 `test_hal_system.cpp` 全部通过。
  - `pio test -e native` 全部通过。
  - `m5stick-c` 编译通过。

---

## 3. 依赖关系图

```
T1 (H-P1-01 显示色深封装) ─── 可并行 ─── T2 (H-P2-01 Native 延时实现)
```

- **无串行依赖**：T1 与 T2 修改的文件互不重叠，可由两个 coder agent 并行执行。
- **延后项 H-P1-02**：不进入本轮范围，与 T1/T2 无依赖关系。

---

## 4. 风险与回退点

| 子任务 | 风险等级 | 主要风险 | 回退方式 |
|--------|----------|----------|----------|
| T1 | **低** | helper 封装行为需与原始两行完全一致。 | 删除 helper，恢复原始两行代码。 |
| T2 | **低~中** | Native 真实延时会让长延时测试变慢。 | 恢复空实现 `void hal_delay_ms(uint32_t ms) { (void)ms; }`。 |

**统一回退点**：每个子任务独立 `git commit`，任意时刻可 `git revert HEAD` 回到上一轮绿色基线。

---

## 5. 测试策略

### 新增/修改的测试文件
- `test/test_native/test_hal_display.cpp`：追加 `hal_display_init()` 链接/编译保护测试。
- `test/test_native/test_hal_system.cpp`：新建，覆盖 `hal_delay_ms()` 的延时语义与零值安全。

### 边界条件覆盖
- **显示**：native 路径 `hal_display_init()` 不崩溃；硬件路径 helper 正确设置 8-bit 色深。
- **延时**：`ms=0`、`ms=50` 均不崩溃；实际耗时与请求值偏差在合理范围（±10ms）。

### 验证命令（每个子任务必须执行）
```bash
pio test -e native
pio run -e m5stick-c
```

---

## 6. 本轮处理与延后问题 ID 汇总

- **本轮处理**：H-P1-01、H-P2-01
- **延后处理**：H-P1-02

---

## 7. 关键实施文件

- `src/hal/hal_display_fb.cpp`
- `src/hal/hal_system.cpp`
- `doc/hal/display.md`
- `doc/hal/system.md`
- `test/test_native/test_hal_system.cpp`
