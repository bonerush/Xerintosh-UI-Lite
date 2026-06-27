# UI 核心层重构报告

> **Parent:** [doc/refactor/README.md](../README.md) | **Prev:** [HAL 层重构报告](hal.md)

## 目标

消除 UI 核心层中的内联魔法数字、重复逻辑与全局状态直接访问，集中列表项绘制与选择器状态管理，提升 native 测试覆盖，为 App 层重构提供稳定的 UI 契约。

## 变更摘要

### 1. 空指针与空列表保护（U4）

在 `xerintosh_init_core()`、`xerintosh_draw_list()`、`xerintosh_draw_list_item()` 等入口增加对空父节点、空子列表的显式保护，避免空指针解引用导致 native 测试与硬件运行崩溃。

*📄 Source: [ui_core.c](../../../src/ui/ui_core.c)*
*📄 Source: [ui_draw_list.c](../../../src/ui/ui_draw_list.c#L50-L70)*

### 2. 统一 UI 布局常量（U1/U2/U10）

将分散在 `ui_core.c`、`ui_draw_list.c`、`ui_item_selector.c` 等文件中的魔法数字提取到 `ui_types.h`，统一命名与管理：

- 列表装饰线长度：`LIST_DECO_H_LINE1_LEN`、`LIST_DECO_H_LINE2_LEN`
- 滚动条几何：`SCROLLBAR_WIDTH`、`SCROLLBAR_TRACK_ENDCAP_H` 等
- 选择器尺寸：`SELECTOR_HEIGHT`、`SELECTOR_DASH_EXTEND`
- 长按提示条：`LONG_PRESS_HINT_BAR_W`、`LONG_PRESS_HINT_BAR_H` 等
- 文字滚动周期：`TEXT_SCROLL_CYCLE_MS`

*📄 Source: [ui_types.h](../../../src/ui/ui_types.h#L47-L79)*

### 3. 拆分 `xerintosh_draw_list_item` 绘制函数（U3）

将原本约 80 行的 `xerintosh_draw_list_item()` 拆分为职责单一的辅助函数：

- `item_text_is_visible()`：判断项文字是否可见
- `item_compute_avail_width()`：计算文字可用宽度
- `item_compute_scroll_x()`：计算滚动文字偏移
- `item_draw_text_scroll()`：绘制滚动文字
- `draw_single_item()`：绘制单个完整项
- `xerintosh_draw_list_item()`：保留顶层遍历与分段控制

*📄 Source: [ui_draw_list.c](../../../src/ui/ui_draw_list.c#L80-L220)*

### 4. 集中选择器速度复位逻辑（U7）

新增 `selector_reset_velocity()` 辅助函数，统一复位 `v_y_selector`、`v_w_selector`、`v_h_selector` 三个速度字段。替换原先分散在 6 个位置的重复赋值：

- `xerintosh_bind_item_to_selector()`
- `xerintosh_selector_go_next_item()` 边界跳转
- `xerintosh_selector_go_prev_item()` 边界跳转
- `xerintosh_selector_exit_current_item()`
- `ui_selector_rebuild_anchor()`
- `ui_selector_safety_move_out()`

*📄 Source: [ui_item_selector.c](../../../src/ui/ui_item_selector.c#L20-L35)*

### 5. 退场动画魔法数字常量化（U8）

将 `ui_draw_anim.c` 中硬编码的 8、13、22、3、5 等数字提取到 `ui_types.h`，命名为：

- `EXIT_ANIM_OVERDRAW_PX`
- `EXIT_ANIM_SNAP_PX`
- `EXIT_ANIM_HOURGLASS_W` / `EXIT_ANIM_HOURGLASS_H`
- `EXIT_ANIM_HOURGLASS_OFFSET_X` / `EXIT_ANIM_HOURGLASS_CENTER_OFFSET_Y`
- `EXIT_ANIM_SCANLINE_OVERDRAW` / `EXIT_ANIM_SCANLINE_TRAIL_PX`

*📄 Source: [ui_types.h](../../../src/ui/ui_types.h#L81-L89)*
*📄 Source: [ui_draw_anim.c](../../../src/ui/ui_draw_anim.c#L90-L140)*

### 6. 弹窗换行缓冲区 `sizeof` 保护（U9）

在 `ui_item_popup.c` 中新增 `popup_copy_line()` 辅助函数，统一封装 `memcpy` + `sizeof` 截断逻辑，替换 2 行/3 行换行分支中手写的重复截断代码。同时调整默认单行路径：先测量原始内容宽度，仅在确实能单行显示时才拷贝完整内容，避免超长内容先写入 48 字节换行缓冲区导致截断/断言。

*📄 Source: [ui_item_popup.c](../../../src/ui/ui_item_popup.c#L22-L35)*
*📄 Source: [ui_item_popup.c](../../../src/ui/ui_item_popup.c#L140-L210)*

### 7. 升级脏矩形到区域追踪（U5）

将 `xerintosh_context_t` 中的 `bool dirty` 升级为 `xerintosh_dirty_region_t dirty_region`，并扩展 `ui_dirty.h` / `ui_dirty.c` API：

- `xerintosh_invalidate()`：标记全屏为脏
- `xerintosh_invalidate_region(x, y, w, h)`：标记指定区域为脏，自动合并包围盒
- `xerintosh_is_dirty()`：查询是否存在脏区域
- `xerintosh_get_dirty_region()`：获取当前脏区域（只读）
- `xerintosh_clear_dirty()`：清除脏区域

当前渲染管线仍全屏重绘，新数据结构为后续局部刷新预留接口。`g_xerintosh_dirty` 兼容宏继续指向 `dirty_region.active`。

*📄 Source: [ui_dirty.h](../../../src/ui/ui_dirty.h#L27-L65)*
*📄 Source: [ui_dirty.c](../../../src/ui/ui_dirty.c)*
*📄 Source: [ui_context.h](../../../src/ui/ui_context.h#L38-L39)*

## 新增 / 修改测试

| 测试文件 | 测试名 | 覆盖点 |
|---|---|---|
| `test/test_native/test_ui_core_fixes.cpp` | `UiCoreFixesTest.InitCoreHandlesEmptyRoot` | 空根节点初始化保护 |
| `test/test_native/test_ui_core_fixes.cpp` | `UiCoreFixesTest.DrawListItemGuardsNullParent` | 空父节点绘制保护 |
| `test/test_native/test_ui_core_fixes.cpp` | `UiCoreFixesTest.DrawListItemGuardsEmptyChildren` | 空子列表绘制保护 |
| `test/test_native/test_ui_core_fixes.cpp` | `UiCoreFixesTest.SelectorVelocitiesResetAfterBind` | 选择器绑定后速度复位 |
| `test/test_native/test_ui_layout_constants.cpp` | `UiLayoutConstants.*` | 新增常量存在性与正值 |
| `test/test_native/test_ui_draw_list_item.cpp` | `UiDrawListItemTest.*` | 空根、单一项、自定义位图、长文字滚动 |
| `test/test_native/test_exit_animation.cpp` | `ExitAnimationConstants.*` | 退场动画常量定义 |
| `test/test_native/test_ui_popup_wrap.cpp` | `UiPopupWrapTest.LongContentWrapDoesNotOverflow` | 超长弹窗内容换行不溢出 |
| `test/test_native/test_ui_popup_wrap.cpp` | `UiPopupWrapTest.SingleLineFitsBuffer` | 短内容保持单行 |
| `test/test_native/test_ui_dirty_region.cpp` | `UiDirtyRegionTest.*` | 全屏/区域 invalidation、合并、清除 |

## 验证结果

- `pio test -e native`：**通过**（562 cases，2 skipped，560 succeeded）
- `pio run -e m5stick-c`：**通过**，无新增编译警告
- `pio run -e m5stick-c-native`：**通过**，无新增编译警告

## 风险与后续注意

- 脏矩形区域追踪已建立数据结构，但渲染管线尚未使用区域信息进行局部刷新；后续若实现局部刷新需同步更新 `xerintosh_ui_render_frame()`。
- `popup_copy_line()` 对超长内容采用安全截断，弹窗内容过长时可能丢失末尾字符；当前行为与硬件屏幕限制一致，后续可考虑增加滚动或省略号提示。
- 选择器速度复位集中后，所有复位点使用同一 helper，避免遗漏；新增边界条件测试防止回归。

## 相关提交

```
cd32d38 fix(ui): guard NULL parent and empty child list in draw/init paths (U4)
4f26af1 refactor(ui): extract layout magic numbers to ui_types.h constants (U1/U2/U10)
5a912f9 refactor(ui): split xerintosh_draw_list_item into focused helpers (U3)
4fa731a refactor(ui): centralize selector velocity reset into helper (U7)
14f2176 refactor(ui): name exit-animation magic numbers as constants (U8)
93e0447 refactor(ui): use sizeof consistently for popup wrap buffers (U9)
df4af28 refactor(ui): upgrade dirty flag to xerintosh_dirty_region_t (U5)
```

---

> **See Also:** [HAL 层重构报告](hal.md) | [下一阶段：App 层](app.md)
