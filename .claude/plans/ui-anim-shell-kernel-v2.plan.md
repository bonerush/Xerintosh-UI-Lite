# Plan: UI 动画引擎复用 + 任务管理器布局 + Shell 扩充 + 内核优化分析

**Source**: 用户综合需求（4 模块协同优化）
**Complexity**: Large
**Selected Milestone**: Phase 0–4 渐进式交付，每 Phase 可独立验证
**Target Board**: M5Stick-C (ESP32-PICO)

---

## Summary

本次计划涵盖 4 个独立模块，按投入产出比排序：

1. **Phase 0 — 提取公共行列表动画工具 `ui_anim_row`**（可复用基础件，后续所有行列表 App 共享）
2. **Phase 1 — 任务管理器：横屏 3 行布局 + 集成入场/切换动画**
3. **Phase 2 — 串口监视器：入场滑入动画 + 按钮选中平滑过渡**
4. **Phase 3 — Shell 命令扩充 22 条**（调试诊断 / 参数配置 / 系统控制 / 实时监测 / 运行时 / 维护）
5. **Phase 4 — 内核优化分析文档**（7 模块 × 诊断 + 方案 + 代码 + 收益）

设计哲学：**最小侵入、最大复用**。Phase 0 提取的 `ui_anim_row` 让 Phase 1/2 以及未来所有行列表 App 直接复用 `xerintosh_animation()` 引擎，不重复造轮子。

---

## 动画引擎复用分析

### 菜单引擎核心抽象

菜单框架提供三层动画原语：

| 原语 | 结构 | 函数 | 语义 |
|------|------|------|------|
| 缓动核心 | `float pos` → `float trg` | `xerintosh_animation()` | `current += (target - current) / (100 - speed)` |
| 行列表 | `y_list_item` / `y_list_item_trg` | `xerintosh_refresh_list_item_position()` | N 个浮动行向目标滑移 |
| 选择器 | `y/w/h_selector` / `*_trg` | `xerintosh_refresh_selector_position()` | 高亮框跟踪选中项 |
| 相机 | `y_camera` / `y_camera_trg` | `xerintosh_refresh_camera_position()` | 视图平滑滚动 |

### 任务管理器 → 菜单映射（几乎 1:1）

| 菜单概念 | 任务管理器等价物 | 类型 |
|----------|-----------------|------|
| `y_list_item[i]` → `y_list_item_trg[i]` | `rows[i].y` → `rows[i].y_trg` | `float` |
| `y_selector` → `y_selector_trg` | `highlight.y` → `highlight.y_trg` | `float` |
| `w_selector` → `w_selector_trg` | `highlight.w` → `highlight.w_trg` | `float` |
| `y_camera` → `y_camera_trg` | `scroll_offset` → `scroll_offset_trg` | `float`（替代 int scroll） |
| `xerintosh_init_list()` 入场 | 初始 `row.y = SCREEN_HEIGHT`，缓动到终点 | 同模式 |

### 串口监视器 → 简化映射

| 菜单概念 | 串口监视器等价物 |
|----------|-----------------|
| 入场偏移 | `sm_entry_offset`：`SCREEN_HEIGHT → 0` |
| 选择器高亮 | `sm_highlight_x`：两按钮间滑移 |

---

## Patterns to Mirror

| Category | Source | Pattern |
|----------|--------|---------|
| 动画缓动 | `ui_core.c:45` `xerintosh_animation()` | `current += (target - current) / (100 - speed)`，snap at ≤1.0 |
| 动画速度 | `ui_item.h:32-40` | `ANIM_SPEED_SELECTOR` / `ANIM_SPEED_EXIT` 等分级常量 |
| 行列表刷新 | `ui_core.c:138-143` `xerintosh_refresh_list_item_position()` | 循环调用 `xerintosh_animation()` |
| 选择器刷新 | `ui_core.c:149-159` `xerintosh_refresh_selector_position()` | 根据 selected 计算 trg，缓动 y/w/h |
| C 模块前缀 | `taskmgr_*`, `sm_*` | 模块前缀 + snake_case |
| 命令注册 | `kern_shell_cmds.c:607-633` | `shell_cmd_t` 静态数组 + handler |
| 错误处理 | `kern_shell_cmds.c` | `sh_println(tty, "msg")` + return |
| 文件大小 | CLAUDE.md | <400 lines/file, <50 lines/function |

---

## Files to Change

### Phase 0: 公共行列表动画工具

| File | Action | Why |
|------|--------|-----|
| `src/ui/ui_anim_row.h` | **CREATE** | 行列表动画结构体与 API 声明 |
| `src/ui/ui_anim_row.c` | **CREATE** | 动画更新实现（封装 `xerintosh_animation()` 循环） |

### Phase 1: 任务管理器

| File | Action | Why |
|------|--------|-----|
| `src/app/taskmgr/taskmgr.h` | UPDATE | `taskmgr_state_t` 新增 `xerintosh_anim_row_list_t anim_list` |
| `src/app/taskmgr/taskmgr_app.c` | UPDATE | 横屏 3 行间距调整 + 动画初始化/更新/刷新 |
| `src/app/taskmgr/taskmgr_ui.c` | UPDATE | `draw_list()` 使用动画行坐标 + 动画高亮框 |

### Phase 2: 串口监视器

| File | Action | Why |
|------|--------|-----|
| `src/app/serial_monitor/sm_app.h` | UPDATE | 新增 `sm_entry_offset`, `sm_highlight_alpha` 动画变量 |
| `src/app/serial_monitor/sm_app.cpp` | UPDATE | `init` 设 entry_offset = SCREEN_HEIGHT；`loop` 更新动画 |
| `src/app/serial_monitor/sm_ui.c` | UPDATE | `draw_info_bar` / `draw_terminal` 叠加入场偏移；按钮高亮平滑过渡 |

### Phase 3: Shell 扩充

| File | Action | Why |
|------|--------|-----|
| `src/kernel/kern_shell_cmds.c` | UPDATE | 新增 ~22 条命令 handler |
| `src/kernel/kern_shell_cmds.h` | UPDATE | 新增 scope 数据结构声明 |
| `src/kernel/kern_shell_cmds_internal.h` | UPDATE | scope_var_t + 脚本引擎基础类型 |
| `src/kernel/kern_shell.c` | UPDATE | shell 主循环中插入 scope tick |
| `src/kernel/kern_sysfs.c` | UPDATE | 新增 `/sys/mode`, `/sys/ctrl`, `/sys/debug` 属性 |
| `src/kernel/kern_sysfs.h` | UPDATE | 新增属性枚举 |

### Phase 4: 内核优化分析

| File | Action | Why |
|------|--------|-----|
| `doc/kernel-optimization-analysis.md` | **CREATE** | 7 模块 × 诊断 + 方案 + 代码 + 收益 |

---

## Tasks

---

### Phase 0: 提取公共行列表动画工具 `ui_anim_row`

#### Task 0.1: 定义数据结构 (`ui_anim_row.h`)

- **Action**: 定义 `xerintosh_anim_row_t`（单行动画状态）和 `xerintosh_anim_row_list_t`（列表动画上下文）
- **Design**:
  ```c
  #define ANIM_ROW_MAX 10

  typedef struct {
      float y;        // 当前动画 Y
      float y_trg;    // 目标 Y
      float w;        // 当前动画宽度（高亮框用）
      float w_trg;    // 目标宽度
  } xerintosh_anim_row_t;

  typedef struct {
      xerintosh_anim_row_t rows[ANIM_ROW_MAX];
      xerintosh_anim_row_t highlight;
      float   scroll_offset;
      float   scroll_offset_trg;
      int     visible_count;
      int16_t row_height;
      int16_t list_top;
  } xerintosh_anim_row_list_t;
  ```
- **Validate**: 编译通过（头文件语法检查）

#### Task 0.2: 实现动画更新 (`ui_anim_row.c`)

- **Action**: 实现 3 个核心函数

  **`xerintosh_anim_row_list_init()`** — 初始化所有行为起始偏移（入场动画用）：
  ```c
  void xerintosh_anim_row_list_init(xerintosh_anim_row_list_t *list,
                                     int visible_count, int16_t row_height,
                                     int16_t list_top)
  {
      list->visible_count = visible_count;
      list->row_height = row_height;
      list->list_top = list_top;
      for (int i = 0; i < visible_count; i++) {
          list->rows[i].y = (float)SCREEN_HEIGHT;   // 入场起点
          list->rows[i].y_trg = list_top + i * row_height;
      }
      list->highlight.y = SCREEN_HEIGHT;
      list->highlight.y_trg = list_top;
      list->highlight.w = SCREEN_WIDTH;
      list->highlight.w_trg = SCREEN_WIDTH;
      list->scroll_offset = 0.0f;
      list->scroll_offset_trg = 0.0f;
  }
  ```

  **`xerintosh_anim_row_list_update()`** — 每帧调用，返回 true 表示已稳定：
  ```c
  bool xerintosh_anim_row_list_update(xerintosh_anim_row_list_t *list, float speed)
  {
      bool all_settled = true;
      for (int i = 0; i < list->visible_count; i++) {
          xerintosh_animation(&list->rows[i].y, list->rows[i].y_trg, speed);
          if (fabsf(list->rows[i].y - list->rows[i].y_trg) > 0.5f)
              all_settled = false;
      }
      xerintosh_animation(&list->highlight.y, list->highlight.y_trg, speed);
      xerintosh_animation(&list->highlight.w, list->highlight.w_trg, speed);
      xerintosh_animation(&list->scroll_offset, list->scroll_offset_trg, speed);
      return all_settled;
  }
  ```

  **`xerintosh_anim_row_list_refresh()`** — 当 selected/scroll 变化时重新计算所有 trg：
  ```c
  void xerintosh_anim_row_list_refresh(xerintosh_anim_row_list_t *list,
                                        int selected, int scroll,
                                        int16_t screen_width, int item_count)
  {
      for (int i = 0; i < list->visible_count; i++) {
          int idx = i + scroll;
          if (idx < item_count) {
              list->rows[i].y_trg = list->list_top + i * list->row_height;
          } else {
              list->rows[i].y_trg = list->list_top + (i + 1) * list->row_height; // 越界项移出视野
          }
      }
      // 高亮框跟踪选中行
      int sel_visible = selected - scroll;
      if (sel_visible >= 0 && sel_visible < list->visible_count) {
          list->highlight.y_trg = list->rows[sel_visible].y_trg;
          list->highlight.w_trg = (float)screen_width;
      }
      list->scroll_offset_trg = (float)scroll;
  }
  ```

- **Mirror**: `ui_core.c:138-159` 的行列表和选择器刷新模式
- **Validate**: `pio run -e native` 编译通过

---

### Phase 1: 任务管理器改造

#### Task 1.1: 横屏布局显示 3 行

- **Root Cause**: `taskmgr_visible_lines()` 中 `header_h = HEADER_Y + fh + 6`（20px）和 `footer_h = fh + 6`（18px），在横屏 `SCREEN_HEIGHT=80` 下 `avail = 80 - 20 - 18 = 42` → `42 / 16 = 2` 行
- **Fix**: 缩减 header/footer 间距：
  ```c
  // taskmgr_app.c:taskmgr_visible_lines()
  // 旧:
  //   int16_t header_h = HEADER_Y + fh + 6;
  //   int16_t footer_h = fh + 6;
  // 新:
      int16_t header_h = HEADER_Y + fh + 2;   // 20→16 (-4px)
      int16_t footer_h = fh + 2;              // 18→14 (-4px)
  // 结果: avail = 80 - 16 - 14 = 50 → 50 / 16 = 3 行 ✓
  ```
- **同步**: `taskmgr_ui.c:86` 的 `list_top = HEADER_Y + fh + 3`（原 +6 → +3）
- **Validate**: 横屏 80px 高度下 `visible` 计算为 3；竖屏 160px 下不受影响（仍 ≥7 行）

#### Task 1.2: 集成 `ui_anim_row` 动画

- **Action**: `taskmgr_state_t` 新增 `xerintosh_anim_row_list_t anim_list` 字段

  **`taskmgr_init()` 改动**:
  ```c
  void taskmgr_init(void) {
      g_tm.selected = 0;
      g_tm.scroll = 0;
      g_tm.confirming = false;
      // + 初始化动画
      int visible = taskmgr_visible_lines();
      int16_t fh = hal_get_font_height();
      int16_t list_top = HEADER_Y + fh + 3;
      xerintosh_anim_row_list_init(&g_tm.anim_list, visible, fh + 4, list_top);
  }
  ```

  **`taskmgr_loop()` 改动** — 在输入处理后、绘制前插入：
  ```c
  // 当 selected/scroll 变化时刷新目标
  static int prev_selected = -1, prev_scroll = -1;
  if (prev_selected != g_tm.selected || prev_scroll != g_tm.scroll) {
      xerintosh_anim_row_list_refresh(&g_tm.anim_list,
          g_tm.selected, g_tm.scroll, SCREEN_WIDTH, g_tm.count);
      prev_selected = g_tm.selected;
      prev_scroll = g_tm.scroll;
  }
  // 每帧更新动画
  xerintosh_anim_row_list_update(&g_tm.anim_list, ANIM_SPEED_SELECTOR);
  ```

  **`taskmgr_draw()` → `draw_list()` 改动** — 用动画坐标取代整数行计算：
  ```c
  static void draw_list(void) {
      // ...
      for (int i = 0; i < visible && (i + scroll) < count; i++) {
          int idx = i + scroll;
          kern_task_t *t = taskmgr_get_task(idx);
          if (t == NULL) continue;

          int16_t row_y = (int16_t)g_tm.anim_list.rows[i].y;  // 动画 Y
          int16_t text_y = row_y + fh;

          if (idx == selected) {
              // 使用动画高亮框
              int16_t hy = (int16_t)g_tm.anim_list.highlight.y;
              int16_t hw = (int16_t)g_tm.anim_list.highlight.w;
              hal_draw_fill_rect(0, hy, hw, (int16_t)(fh + 4), COLOR_FG);
              hal_draw_string(LEFT_MARGIN + 1, text_y, line, COLOR_BG);
          } else {
              hal_draw_string(LEFT_MARGIN + 1, text_y, line, COLOR_FG);
          }
      }
  }
  ```

- **动画效果**:
  - 入场：行从屏幕底部（`SCREEN_HEIGHT`）滑入到最终位置，约 300ms
  - 切换：高亮框从旧行平滑滑到新行，约 200ms
  - 滚动：`scroll_offset` 平滑过渡，消除整数跳动感

- **Validate**: `pio test -e native` + 硬件目视验证

---

### Phase 2: 串口监视器动画

#### Task 2.1: 入场滑入动画

- **Action**: 
  ```c
  // sm_app.h 新增
  extern float sm_entry_offset;

  // sm_app.cpp: serial_monitor_init()
  sm_entry_offset = (float)SCREEN_HEIGHT;

  // sm_app.cpp: serial_monitor_loop() — 在绘制前
  xerintosh_animation(&sm_entry_offset, 0.0f, ANIM_SPEED_EXIT);
  ```
- **`serial_monitor_draw()` 改动**: 所有 Y 坐标叠加 `(int16_t)sm_entry_offset`
  - `draw_info_bar()`: `bar_y` 改为 `1 + (int16_t)sm_entry_offset`
  - `draw_terminal()`: `term_y` 改为 `bar_h + 5 + (int16_t)sm_entry_offset`
  - `sm_entry_offset < 1.0f` 时跳过叠加（节省浮点→整数转换）

- **Validate**: 进入串口监视器时信息栏+终端从底部滑入

#### Task 2.2: 按钮选中平滑过渡

- **当前行为**: 选中按钮以 500ms 周期 `sm_blink_on` 闪烁
- **目标行为**: 选中按钮亮度平滑淡入淡出（替代闪烁）

- **Action**:
  ```c
  // sm_app.h 新增
  extern float sm_btn_alpha_0;  // RUN/STOP 按钮高亮强度 (0.0~1.0)
  extern float sm_btn_alpha_1;  // NORM/DBG 按钮高亮强度

  // sm_app.cpp: serial_monitor_loop()
  float trg0 = (sm_selected == 0) ? 1.0f : 0.0f;
  float trg1 = (sm_selected == 1) ? 1.0f : 0.0f;
  xerintosh_animation(&sm_btn_alpha_0, trg0, ANIM_SPEED_SELECTOR_H);
  xerintosh_animation(&sm_btn_alpha_1, trg1, ANIM_SPEED_SELECTOR_H);
  ```
- **`draw_button()` 改动**: 
  ```c
  // 用 alpha 插值计算颜色（替代 bool selected + blink）
  float a = (id == 0) ? sm_btn_alpha_0 : sm_btn_alpha_1;
  // a=1.0 → 白底黑字（选中）；a=0.0 → 黑底白字（未选中）
  // 中间态：灰度插值
  uint16_t bg = COLOR_FG;  // 始终白底
  uint16_t fg = COLOR_BG;  // 始终黑字
  // alpha 仅影响边框亮度（可选）
  hal_draw_fill_rect(x, y, w, h, bg);
  hal_draw_rect(x, y, w, h, COLOR_FG);
  hal_draw_string(tx, ty, label, fg);
  ```

  实际采用简化方案：用 alpha 控制是否反色，阈值 0.5：
  ```c
  bool is_selected = ((id == 0) ? sm_btn_alpha_0 : sm_btn_alpha_1) > 0.5f;
  ```

- **Validate**: 按钮切换时平滑过渡而非闪烁

---

### Phase 3: Shell 命令扩充

#### Task 3.1: 调试诊断命令（4 条）

| 命令 | 功能 | 实现 |
|------|------|------|
| `top` | 实时任务 CPU/栈监控 | 循环 `ps` 输出 + `kern_sleep_ms(2000)`；任意按键退出 |
| `mem` | 内存统计 | 直接复用 `cmd_free` handler（别名注册） |
| `log [level]` | 日志级别查看/设置 | 无参数 → `cat /sys/kernel/log_level`；有参数 → `echo <n> > /sys/kernel/log_level` |
| `debug <on/off> [module]` | 模块调试开关 | 新增 `/sys/debug/<module>` sysfs 节点（位掩码内部存储） |

**`top` 实现要点**（shell 主循环中插入）：
```c
static void cmd_top(kern_fd_t tty, int argc, char *argv[], char *cwd, size_t cwd_size) {
    (void)argc; (void)argv; (void)cwd; (void)cwd_size;
    sh_println(tty, "TOP — press any key to exit");
    bool running = true;
    while (running) {
        cmd_ps(tty, 0, NULL, cwd, cwd_size);
        sh_println(tty, "---");
        // 非阻塞检查输入
        char c;
        if (kern_read(tty, &c, 1) > 0) running = false;
        if (running) kern_sleep_ms(2000);
    }
}
```

#### Task 3.2: 参数配置命令（5 条）

| 命令 | 功能 | 实现 |
|------|------|------|
| `param list` | 列出所有参数 | 遍历 sysfs `g_sysfs_attrs[]`，输出 `name = value (min ~ max)` |
| `param get <name>` | 读取参数值 | `fd = kern_open("/sys/<name>", ...)` → `kern_read()` |
| `param set <name> <value>` | 设置参数 | 解析 value → `kern_write()` → 利用 sysfs 已有的范围校验 |
| `param save` | 持久化到 Flash | 调用 `settings_save_all()`（需暴露为 syscall 或直接调用） |
| `param load` | 从 Flash 恢复 | 调用 `settings_load_all()` |

**`param list` 实现**：
```c
static void cmd_param(kern_fd_t tty, int argc, char *argv[], char *cwd, size_t cwd_size) {
    // 遍历 sysfs 内部属性表
    for (int i = 0; i < KERN_SYSFS_ATTR_MAX; i++) {
        const kern_sysfs_attr_t *attr = kern_sysfs_get_attr(i);
        if (attr == NULL || attr->name[0] == '\0') continue;
        char line[96];
        snprintf(line, sizeof(line), "%-20s = %-6d (%d ~ %d)",
                 attr->name, attr->current_value, attr->min, attr->max);
        sh_println(tty, line);
    }
}
```

#### Task 3.3: 系统控制命令（4 条）

| 命令 | 功能 | 实现 |
|------|------|------|
| `bootloader` | 进入 OTA 升级模式 | `esp_ota_set_boot_partition(esp_ota_get_next_update_partition(NULL))` + `esp_restart()` |
| `update` | 触发 OTA 升级 | 预留接口，当前返回 `"update: not yet implemented"` |
| `factory` | 恢复出厂设置 | **二次确认**：打印警告 + 要求输入 `"yes"` 才执行 → 清除 NVS → `esp_restart()` |
| `version` | 固件/硬件信息 | 输出 `XEROS_VERSION_STRING` + 编译时间 + 芯片型号 + Flash 大小 |

**`factory` 二次确认**：
```c
static void cmd_factory(kern_fd_t tty, int argc, char *argv[], char *cwd, size_t cwd_size) {
    sh_println(tty, "WARNING: This will erase ALL settings and reboot.");
    sh_print(tty, "Type 'yes' to confirm: ");
    char confirm[8];
    ssize_t n = kern_read(tty, confirm, sizeof(confirm) - 1);
    if (n <= 0) return;
    confirm[n] = '\0';
    // 去除尾部 \r\n
    for (ssize_t i = n - 1; i >= 0 && (confirm[i] == '\r' || confirm[i] == '\n'); i--)
        confirm[i] = '\0';
    if (strcmp(confirm, "yes") != 0) {
        sh_println(tty, "Aborted.");
        return;
    }
    sh_println(tty, "Erasing NVS and rebooting...");
    nvs_flash_erase();
    esp_restart();
}
```

#### Task 3.4: 实时数据监测命令（3 条）

| 命令 | 功能 | 实现 |
|------|------|------|
| `scope add <path>` | 注册观测变量 | 将 `/proc/` 或 `/sys/` 路径加入 `scope_vars[]` 数组 |
| `scope start [ms]` | 开始周期打印 | 设置 `scope_period` + `scope_running = true` |
| `scope stop` | 停止 | `scope_running = false` |

**数据结构**:
```c
#define SCOPE_MAX_VARS 8
typedef struct {
    char   path[KERN_PATH_MAX];
    bool   active;
} scope_var_t;

static scope_var_t g_scope_vars[SCOPE_MAX_VARS];
static int     g_scope_count = 0;
static bool    g_scope_running = false;
static int     g_scope_period_ms = 1000;
static uint64_t g_scope_last_tick = 0;
```

**Shell 主循环注入**（`kern_shell.c:shell_task_main()`）:
```c
// 在 kern_read(tty, ...) 阻塞之前插入
if (g_scope_running && g_scope_count > 0) {
    uint64_t now = esp_timer_get_time();
    if (now - g_scope_last_tick >= g_scope_period_ms * 1000ULL) {
        g_scope_last_tick = now;
        // CSV 格式输出
        char line[256]; int pos = 0;
        for (int i = 0; i < g_scope_count; i++) {
            if (!g_scope_vars[i].active) continue;
            kern_fd_t fd = kern_open(g_scope_vars[i].path, KERN_O_RDONLY);
            if (fd >= 0) {
                char val[32]; ssize_t n = kern_read(fd, val, sizeof(val)-1);
                if (n > 0) { val[n] = '\0'; pos += snprintf(line+pos, sizeof(line)-pos, "%s%s", i>0?",":"", val); }
                kern_close(fd);
            }
        }
        sh_println(tty, line);
    }
}
```

#### Task 3.5: 运行时控制命令（3 条）

| 命令 | 功能 | 实现 |
|------|------|------|
| `mode [get/set <mode>]` | 运行模式 | 新增 `/sys/mode` sysfs 节点（manual/auto/calibrate/estop） |
| `ctrl [start/stop/reset]` | 控制算法启停 | 新增 `/sys/ctrl` sysfs 节点 |
| `io <get/set> <pin> [value]` | GPIO 调试 | 利用已有 `/sys/gpio/<pin>` 节点 |

**sysfs 新增属性**（`kern_sysfs.c`）:
```c
{ KERN_SYSFS_MODE,  "/sys/mode",  0, 0, 0, 3 },   // 0=manual 1=auto 2=calibrate 3=estop
{ KERN_SYSFS_CTRL,  "/sys/ctrl",  0, 0, 0, 2 },   // 0=stop 1=start 2=reset
```

#### Task 3.6: 辅助维护命令（3 条）

| 命令 | 功能 | 实现 |
|------|------|------|
| `info` | 设备汇总信息 | 组合输出：`uname` + `free` + `df` + `date` |
| `ping <host>` | 网络连通性测试 | 仅 WiFi 连接时可用；预留接口，当前返回 `"ping: not connected"` |
| `help [cmd]` | 详细帮助 | 扩展 `help`：有参数时显示单条命令的详细说明 |

**`help [cmd]` 扩展**:
```c
static void cmd_help(kern_fd_t tty, int argc, char *argv[], char *cwd, size_t cwd_size) {
    if (argc > 1) {
        const shell_cmd_t *cmd = shell_lookup_cmd(argv[1]);
        if (cmd == NULL) {
            sh_println(tty, "help: no help for that command");
            return;
        }
        char line[128];
        snprintf(line, sizeof(line), "%s — %s\r\nUsage: varies by command", cmd->name, cmd->help);
        sh_println(tty, line);
        return;
    }
    // 原有逻辑：列出所有命令
    ...
}
```

#### Task 3.7: 注册所有新命令

- **Action**: 在 `g_builtin_cmds[]` 中添加所有新命令条目
- **可裁剪设计**:
  ```c
  #ifndef KERN_SHELL_MINIMAL
      { "top",       cmd_top,       "real-time task monitor" },
      { "debug",     cmd_debug,     "module debug switch" },
      { "scope",     cmd_scope,     "real-time data scope" },
      { "factory",   cmd_factory,   "factory reset (DANGER!)" },
  #endif
  ```
- **Validate**: `pio test -e native` + 串口逐条测试

---

### Phase 4: 内核优化分析文档

产出 `doc/kernel-optimization-analysis.md`，按 7 模块分点。

#### 4.1 调度器优化

**问题**: 纯 Round-Robin 无优先级，单个失控任务可饿死 UI。

**方案**:
- TCB 增加 `uint32_t exec_ticks` 和 `uint32_t last_run`，在 `/proc/tasks` 中暴露 CPU 占用率
- 就绪队列改为两级：`high_prio`（UI/Shell，优先级 128+）和 `low_prio`（后台，<128），2:1 调度比例
- Idle 任务中调用 `esp_light_sleep_start()` 利用 FreeRTOS tickless 省电

```c
// TCB 扩展
typedef struct kern_task {
    // ... 现有字段
    uint32_t exec_ticks;     // 累计运行 tick 数
    uint32_t last_run;       // 上次调度时间戳
} kern_task_t;

// 两级调度
static kern_task_t *pick_next_ready(void) {
    static int balance = 0;
    // 2:1 high:low
    if (balance < 2 && has_high_prio_ready())
        return pick_high_prio();
    balance = (balance + 1) % 3;
    return round_robin_next();
}
```

**预期收益**: UI 延迟从最坏 O(N×tick) 降到 O(1)；idle 省电约 20mA（light sleep）。

#### 4.2 动态栈管理优化

**问题**: `realloc` 产生碎片和不确定延迟。

**方案**: 两级固定栈池替代动态分配。

```c
#define POOL_SMALL_SIZE  1024
#define POOL_LARGE_SIZE  4096
#define POOL_SMALL_COUNT 4
#define POOL_LARGE_COUNT 2

typedef struct {
    uint8_t  buf[POOL_SMALL_SIZE];
    bool     in_use;
    kern_pid_t owner;
} small_slot_t;

typedef struct {
    uint8_t  buf[POOL_LARGE_SIZE];
    bool     in_use;
    kern_pid_t owner;
} large_slot_t;

static small_slot_t g_small_pool[POOL_SMALL_COUNT];
static large_slot_t g_large_pool[POOL_LARGE_COUNT];
```

栈溢出增强报告:
```c
void kern_stack_overflow_panic(kern_task_t *task, uint32_t canary) {
    KERN_LOG_PANIC("STACK OVERFLOW: task='%s'(pid=%d), "
                   "used=%zu/%zu, canary=0x%08X (expected 0x%08X)",
                   task->name, task->pid,
                   kern_task_stack_usage(task), task->stack_size,
                   canary, KERN_STACK_CANARY);
}
```

**预期收益**: 消除 `realloc` 碎片风险；分配/释放 O(1)。

#### 4.3 VFS 与文件系统优化

**路径解析缓存**:
```c
static kern_dentry_t *g_last_dentry = NULL;
static char g_last_path[KERN_PATH_MAX];

kern_dentry_t *kern_path_resolve(const char *path) {
    if (g_last_dentry && strcmp(path, g_last_path) == 0)
        return g_last_dentry;  // 缓存命中
    // ... 完整路径解析 ...
    strncpy(g_last_path, path, KERN_PATH_MAX);
    g_last_dentry = result;
    return result;
}
```

**动态内容单次生成**（`/proc/meminfo`）:
```c
static ssize_t procfs_meminfo_generate(char *buf, size_t len) {
    return snprintf(buf, len,
        "MemTotal: %" PRIu32 "\n"
        "MemFree:  %" PRIu32 "\n"
        "MemUsed:  %" PRIu32 "\n"
        "MinFree:  %" PRIu32 "\n",
        total, free, used, min_free);
}
```

**预期收益**: 路径解析命中率 >90%（shell 场景）；单次 snprintf 减少函数调用。

#### 4.4 IPC 优化

**MQ 哈希表**（O(1) 查找）:
```c
#define MQ_HASH_SIZE 8

typedef struct {
    char   name[32];
    mq_queue_t *queue;
    bool   in_use;
} mq_hash_entry_t;

static mq_hash_entry_t g_mq_hash[MQ_HASH_SIZE];

static uint8_t mq_hash(const char *name) {
    return (uint8_t)(name[0] + name[1] * 31) % MQ_HASH_SIZE;
}
```

**预期收益**: MQ 查找从 O(N) → O(1)；pipe FIFO 唤醒已天然满足（`kern_sched_tick()` 按到达顺序唤醒）。

#### 4.5 Shell 与系统调用

**统一参数校验**（sysfs write）:
```c
static ssize_t sysfs_write(kern_file_t *f, const char *buf, size_t len) {
    kern_sysfs_attr_t *attr = (kern_sysfs_attr_t *)f->inode->private_data;
    long val = parse_long(buf, len);
    if (val < attr->min || val > attr->max)
        return KERN_EINVAL;  // 统一错误码
    attr->current_value = (int32_t)val;
    // 触发硬件绑定回调
    for (int i = 0; i < attr->callback_count; i++)
        attr->callbacks[i](attr, attr->callbacks_user_data[i]);
    return len;
}
```

**脚本引擎最小实现**:
```c
// 支持: echo, sleep, if-goto, assert, wait
typedef struct {
    char   name[32];
    char  *lines[64];
    int    line_count;
    int    pc;          // 程序计数器
    bool   running;
} script_t;

// script run /calibration.scr
// 逐行解析: if-goto <cond> <label> | sleep <ms> | assert <expr>
```

**预期收益**: 参数校验统一返回 `EINVAL`；脚本引擎使自动化标定无需 PC 连接。

#### 4.6 内核可观测性

**TCB exec_ticks 暴露**（`/proc/tasks`）:
```c
// procfs_tasks_generate() 增加
pos += snprintf(buf + pos, len - pos,
    "%-4d %-8s %-12s %zu/%-4zu %3u%%\n",
    task->pid, state_str, task->name,
    usage, task->stack_size,
    (unsigned)(task->exec_ticks * 100 / total_ticks));
```

**栈金丝雀扫描**:
```c
void kern_stack_canary_scan_all(void) {
    kern_task_t *t = g_task_list_head;
    while (t) {
        if (t->state != KERN_TASK_ZOMBIE) {
            uint32_t *canary = (uint32_t *)(t->stack_base + t->stack_size - 4);
            if (*canary != KERN_STACK_CANARY) {
                KERN_LOG_PANIC("canary corrupted in '%s'(pid=%d)", t->name, t->pid);
            }
            size_t usage = kern_task_stack_usage(t);
            if (usage > t->stack_size * 80 / 100) {
                KERN_LOG_WARN("stack >80%% in '%s'(pid=%d): %zu/%zu",
                              t->name, t->pid, usage, t->stack_size);
            }
        }
        t = t->next;
    }
}
// 在 kern_sched_tick() 的 idle 分支调用
```

**预期收益**: 栈溢出提前预警（80% 阈值）；CPU 占用率可视化。

#### 4.7 编译与内存优化

**PROGMEM 常量存储**:
```c
// kern_shell_cmds.c
static const char HELP_LS[] PROGMEM = "list directory";
static const char HELP_CD[] PROGMEM = "change directory";
// ...
static const shell_cmd_t g_builtin_cmds[] = {
    { "ls", cmd_ls, HELP_LS },
    { "cd", cmd_cd, HELP_CD },
    // ...
};
// 读取时: strcpy_P(buf, cmd->help);
```

**热路径内联**:
```c
// kern_task.c
static inline __attribute__((always_inline))
kern_task_t *pick_next_ready(void) { ... }

// 整个调度器文件
#pragma GCC optimize("O3")
// #include "kern_task.c" 实现
```

**预期收益**: 帮助文本省 ~1.2KB SRAM（移入 Flash）；调度器热路径省 2-3 函数调用。

---

## Validation

```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"

# 每阶段编译检查
pio run -e m5stick-c
pio run -e native

# 全量 native 测试
pio test -e native

# 硬件烧录验证
pio run -e m5stick-c --target upload
pio device monitor -e m5stick-c
```

### 每阶段验收标准

| Phase | 验收条件 |
|-------|---------|
| 0 | `ui_anim_row.h/c` 编译通过；native 测试无回归 |
| 1 | 横屏任务管理器显示 3 行；入场行从底部滑入；选中切换高亮平滑过渡 |
| 2 | 串口监视器入场时信息栏+终端从底部滑入；按钮选中平滑淡入（非闪烁） |
| 3 | `help` 列出全部 ~42 条命令；`param list/get/set` 可读写参数；`scope add/start/stop` 可周期性输出 CSV；`factory` 二次确认 |
| 4 | `doc/kernel-optimization-analysis.md` 产出，7 模块全覆盖 |

---

## Risks

| Risk | Likelihood | Mitigation |
|------|-----------|------------|
| `ui_anim_row` 浮点运算在 ESP32 上增加帧开销 | 低 | 每帧仅 N 次 `xerintosh_animation()`（~10 浮点操作/次），ESP32 双核 240MHz 可忽略 |
| 横屏 footer 间距缩小导致文字截断 | 低 | fh=12 时 footer 总高 14px（2px margin + 12px 字），baseline 对齐，不越界 |
| Shell 新命令增加 ROM 占用 | 中 | `scope`/`debug`/`factory` 等用 `#ifndef KERN_SHELL_MINIMAL` 守卫；`help` 文本用 PROGMEM |
| 入场动画与退场 hourglass 动画重叠 | 低 | 入场动画在 `g_xerintosh_exit_animation_finished == true` 后才开始（已在框架中保证） |
| sysfs 新增 `/sys/mode` 等与 UI 菜单设置冲突 | 低 | Settings App 已绑定 sysfs，自然同步 |

---

## Acceptance

- [ ] Phase 0: `ui_anim_row.h/c` 创建并通过编译
- [ ] Phase 1: 横屏 3 行 + 行滑入动画 + 高亮切换动画
- [ ] Phase 2: 串口监视器入场滑入 + 按钮平滑过渡
- [ ] Phase 3: 22 条新 Shell 命令可用，`help` 完整
- [ ] Phase 4: 内核优化分析文档产出
- [ ] `pio test -e native` 全部通过
- [ ] `pio run -e m5stick-c` 编译通过
- [ ] 所有模式对齐，无重复造轮子
