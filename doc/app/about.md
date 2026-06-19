# 关于页面（About）

> **Parent:** [App 层索引](index.md) | **Related:** [开机画面](boot.md), [UI 公共服务](ui-service.md)

## 概述

`about` 是一个 `user_item` 全屏 App，显示系统版本、平台、代号与开发者信息。页面左侧展示复用自 `boot_screen` 的 Macintosh 风格 logo 与 "Xerintosh" 标题，右侧以纵线分隔的信息区列出详细版本数据。

## 关键概念

### 页面布局

*📄 Source: [about.c](../../src/app/about/about.c#L32-L81)*

```c
static void about_draw(void)
{
    int16_t fh = hal_get_font_height();

    int16_t logo_w = 44 * ABOUT_LOGO_SCALE / 100;
    int16_t logo_h = 60 * ABOUT_LOGO_SCALE / 100;

    hal_set_font(NULL);
    int16_t title_w = hal_get_string_width("Xerintosh");

    int16_t content_w = (logo_w > title_w) ? logo_w : title_w;
    int16_t logo_x, title_x;
    if (logo_w > title_w) {
        logo_x  = ABOUT_LEFT_MARGIN;
        title_x = ABOUT_LEFT_MARGIN + (logo_w - title_w) / 2;
    } else {
        title_x = ABOUT_LEFT_MARGIN;
        logo_x  = ABOUT_LEFT_MARGIN + (title_w - logo_w) / 2;
    }

    int16_t group_h = logo_h + fh + ABOUT_TITLE_GAP;
    int16_t y0 = HAL_CENTER_Y(group_h);

    boot_screen_draw_logo(logo_x, y0, ABOUT_LOGO_SCALE);
    hal_draw_string(title_x, y0 + group_h, "Xerintosh", COLOR_FG);

    /* 分隔线（纵贯屏幕） */
    hal_draw_v_line(ABOUT_LEFT_MARGIN + content_w + ABOUT_SEP_GAP,
                    y0, SCREEN_HEIGHT - 2 * y0, COLOR_FG);

    /* 信息区 */
    hal_set_font(hal_get_cn_font());
    int16_t info_x = ABOUT_LEFT_MARGIN + content_w + ABOUT_INFO_GAP;
    char buf[32];

    snprintf(buf, sizeof(buf), "Version:%s", XEROS_VERSION_STRING);
    hal_draw_string(info_x, y0 + fh,              buf,              COLOR_FG);
    hal_draw_string(info_x, y0 + fh + fh,         XEROS_CODENAME,   COLOR_FG);
    hal_draw_string(info_x, y0 + fh + 2 * fh,     XEROS_PLATFORM,   COLOR_FG);

    snprintf(buf, sizeof(buf), "By:%s", XEROS_DEVELOPER);
    hal_draw_string(info_x, y0 + fh + 4 * fh,     buf,              COLOR_FG);
}
```

绘制流程：
1. 根据 logo 与标题的宽度取较大者作为内容宽度
2. 将 logo 与标题作为整体垂直居中，较窄者水平居中于较宽者
3. 绘制纵贯屏幕的垂直分隔线
4. 在分隔线右侧列出 `XEROS_VERSION_STRING`、`XEROS_CODENAME`、`XEROS_PLATFORM`、`XEROS_DEVELOPER`

### 生命周期

*📄 Source: [about.c](../../src/app/about/about.c#L85-L109)*

```c
void about_init(void *ud)
{
    (void)ud;
#ifndef NATIVE_TEST
    ui_service_user_item_init();
#endif
}

void about_loop(void *ud)
{
    (void)ud;
    hal_event_t event_b = hal_input_get_event(HAL_BTN_B);

    if (ui_service_user_item_loop(event_b)) return;

    about_draw();
}

void about_exit(void *ud)
{
    (void)ud;
#ifndef NATIVE_TEST
    ui_service_user_item_exit();
#endif
}
```

关于页面遵循标准 `user_item` 生命周期：
- `about_init()` 调用 `ui_service_user_item_init()` 重置按键事件
- `about_loop()` 检测 BtnB 退出事件，未触发退出时调用 `about_draw()`
- `about_exit()` 调用 `ui_service_user_item_exit()` 清理按键状态

## 版本信息来源

*📄 Source: [kern_version.h](../../src/kernel/kern_version.h)*

版本字符串由 Xeros 内核版本模块集中管理，`about.c` 仅负责渲染展示。

---

> **See Also:** [开机画面](boot.md) | [UI 公共服务](ui-service.md) | [内核版本](../kernel/kern-version.md)
