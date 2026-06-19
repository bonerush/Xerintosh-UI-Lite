# 服务管理助手（Service Manager Helper）

> **Parent:** [App 层索引](index.md) | **Related:** [蓝牙管理器](../../src/app/bluetooth/bt_manager.cpp), [串口监视器](serial-monitor.md)

## 概述

`svc_mgr_helper` 模块为各 `user_item` App 提供统一的系统服务生命周期管理，避免各 App 直接调用底层 manager 时出现重复代码和生命周期泄漏。phase 2.5 重构后，该模块首先提供蓝牙（BT）懒加载助手：进入 App 时按需启用蓝牙，退出时根据懒加载标志禁用蓝牙。

---

## 核心 API

*📄 Source: [svc_mgr_helper.h](../../src/app/svc_mgr_helper.h#L20-L35)*

```c
void svc_mgr_bt_request_enable(bool *lazy_inited);
void svc_mgr_bt_request_disable(bool *lazy_inited);
```

### svc_mgr_bt_request_enable()

*📄 Source: [svc_mgr_helper.c](../../src/app/svc_mgr_helper.c#L14-L24)*

请求启用蓝牙并记录懒加载状态：

```c
void svc_mgr_bt_request_enable(bool *lazy_inited)
{
    if (lazy_inited == NULL) return;

    if (!bt_mgr_is_enabled()) {
        bt_mgr_request_enable();
        *lazy_inited = true;
    }
}
```

| 输入 | 行为 |
|------|------|
| `lazy_inited == NULL` | 直接返回，不做任何操作 |
| 蓝牙已启用 | 不重复请求，`*lazy_inited` 保持原值 |
| 蓝牙未启用 | 调用 `bt_mgr_request_enable()`，并将 `*lazy_inited` 置为 `true` |

### svc_mgr_bt_request_disable()

*📄 Source: [svc_mgr_helper.c](../../src/app/svc_mgr_helper.c#L26-L36)*

请求禁用蓝牙并清除懒加载状态：

```c
void svc_mgr_bt_request_disable(bool *lazy_inited)
{
    if (lazy_inited == NULL) return;

    if (*lazy_inited && bt_mgr_is_enabled()) {
        bt_mgr_request_disable();
        *lazy_inited = false;
    }
}
```

| 输入 | 行为 |
|------|------|
| `lazy_inited == NULL` | 直接返回 |
| `*lazy_inited == false` | 说明不是由本 App 启用的蓝牙，不请求禁用 |
| `*lazy_inited == true` 且蓝牙仍启用 | 调用 `bt_mgr_request_disable()`，并将 `*lazy_inited` 置为 `false` |

---

## 设计说明

- **懒加载标志**：每个使用蓝牙的 `user_item` 需要维护自己的 `bool g_bt_lazy_inited` 静态变量，在 `init()` 中传入 `&g_bt_lazy_inited`
- **生命周期对称**：`request_enable()` 在 App 进入时调用，`request_disable()` 在 App 退出时调用，确保不会泄漏蓝牙功耗
- **不越权关闭**：只有被本 App 懒加载启用的蓝牙才会在退出时请求禁用，避免影响其他已经启用蓝牙的模块

---

## 历史

- phase 2.4 之前：`svc_mgr_helper.c/h` 中的 `svc_mgr_handle_switch_toggle()` 已被内联到各管理器
- phase 2.5：模块重新创建，职责改为 `user_item` 系统服务懒加载助手

---

> **See Also:** [App 层索引](index.md) | [应用初始化](app-init.md) | [串口监视器](serial-monitor.md)
