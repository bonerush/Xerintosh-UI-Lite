# UI Popup 重构延后记录

**问题 ID**：H-P2-04  
**文件**：`src/ui/ui_item_popup.c:132-270`  
**问题**：`xerintosh_push_pop_up()` 约 140 行，含自动换行、缓存、状态机，使用 `goto`。

## 延后原因

- 涉及多语言换行、弹窗缓存、状态机与动画目标值同步，拆分改动面大。
- 当前功能稳定，冒然拆分可能引入弹窗显示异常。
- 需要配套 `test_ui_popup.cpp` 做行为回归，本轮未准备该测试基础设施。

## 当前职责清单

`xerintosh_push_pop_up()` 当前承担以下职责：

1. 计算目标弹窗宽度（基于文本长度、图标、屏幕宽度）。
2. 自动换行：遍历文本，按字符宽度累加，超过阈值时换行。
3. 计算换行后的总高度（使用 `goto calc_height` 重新遍历）。
4. 更新全局 popup 状态（`g_popup`、`g_popup_icon`、`g_popup_frame`）。
5. 启动/重置弹窗显示计时器。
6. 缓存当前文本避免重复计算。

## 建议拆分方案

拆分为以下静态函数：

- `static uint16_t popup_compute_width(const char* text, xerintosh_icon_t icon)`
  - 输入：文本、图标
  - 输出：弹窗目标宽度
- `static uint8_t popup_wrap_text(const char* src, char* dst, uint16_t dst_size, uint16_t max_width)`
  - 输入：原始文本、目标缓冲区、缓冲区大小、最大行宽
  - 输出：换行后的行数
- `static void popup_update_state(const char* text, xerintosh_icon_t icon, uint8_t lines, uint16_t width, uint16_t height)`
  - 更新全局 popup 状态与动画目标值
- `static bool popup_text_changed(const char* text)`
  - 判断文本是否变化，决定是否重置计时器

## 移除 goto 方案

当前 `goto calc_height` 用于先尝试不换行计算宽度，若宽度超过阈值则重新换行并计算高度。可改为：

```c
bool need_wrap = (text_width > max_text_width);
uint8_t lines = need_wrap ? popup_wrap_text(text, wrapped, ..., max_text_width) : 1;
uint16_t final_height = popup_compute_height(lines);
```

用 `bool need_wrap` 状态变量 + 提前返回替代 `goto`。

## 依赖测试

需新建 `test/test_native/test_ui_popup.cpp`：

- `PopupWrap_SingleLine`：短文本不换行，行数为 1。
- `PopupWrap_TwoLines`：中等长度文本换为 2 行。
- `PopupWrap_ThreeLines`：长文本换为 3 行。
- `PopupResetTimer_OnTextChange`：文本变化时计时器重置。
- `PopupKeepTimer_OnSameText`：相同文本不重置计时器。
- `PopupClamp_LongText`：超长文本截断或按最大行数处理。

## 参考实现顺序

1. 先写 `test_ui_popup.cpp` 覆盖上述场景。
2. 将 `xerintosh_push_pop_up()` 拆分为上述静态函数。
3. 移除 `goto`。
4. 运行 `pio test -e native` 和 `pio run -e m5stick-c`。
5. 在硬件上验证短/长文本、图标弹窗的显示和动画。
