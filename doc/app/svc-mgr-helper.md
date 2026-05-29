# 服务管理助手（Service Manager Helper）

> **Parent:** [App 层索引](index.md) | **Related:** [WiFi 管理器](../../src/app/wifi/wifi_manager.cpp), [蓝牙管理器](../../src/app/bluetooth/bt_manager.cpp)

## 概述

`svc_mgr_helper` 是一个轻量级的共享工具模块，为 WiFi 和蓝牙管理器提供通用的**开关切换抽象**。当用户通过 UI 开关启用或禁用某项服务时，`svc_mgr_handle_switch_toggle()` 统一处理状态判断并调用对应的 enable/disable 函数，避免在两个管理器中重复编写相同的 if-else 逻辑。

---

## 核心 API

*📄 Source: [svc_mgr_helper.c](../../src/app/svc_mgr_helper.c#L10-L21)*

```c
void svc_mgr_handle_switch_toggle(bool *flag_ptr,
                                   svc_mgr_action_fn enable,
                                   svc_mgr_action_fn disable,
                                   void *ud);
```

### 参数

| 参数 | 类型 | 说明 |
|------|------|------|
| `flag_ptr` | `bool *` | 开关状态的布尔值指针 |
| `enable` | `svc_mgr_action_fn` | 当 `*flag_ptr == true` 时调用的启用函数 |
| `disable` | `svc_mgr_action_fn` | 当 `*flag_ptr == false` 时调用的禁用函数 |
| `ud` | `void *` | 用户数据（当前未使用） |

### 类型定义

*📄 Source: [svc_mgr_helper.h](../../src/app/svc_mgr_helper.h)*

```c
typedef void (*svc_mgr_action_fn)(void);
```

---

### 中文伪代码拆解

```
函数 服务管理器_处理开关切换(开关指针, 启用函数, 禁用函数, 用户数据) {
    if (*开关指针 == true) {
        调用启用函数()
    } else {
        调用禁用函数()
    }
}
```

**核心思想**：将 "根据布尔值二选一调用函数" 这个常见模式提取为可复用工具，使 WiFi 和蓝牙管理器的开关回调保持一行调用。

---

## 使用示例

*📄 Source: [wifi_manager.cpp](../../src/app/wifi/wifi_manager.cpp)*

```c
void wifi_mgr_on_switch_toggle(void *ud) {
    svc_mgr_handle_switch_toggle(&g_wifi_on,
                                  wifi_mgr_enable,
                                  wifi_mgr_disable,
                                  ud);
}
```

*📄 Source: [bt_manager.cpp](../../src/app/bluetooth/bt_manager.cpp)*

```c
void bt_mgr_on_switch_toggle(void *ud) {
    svc_mgr_handle_switch_toggle(&g_bt_on,
                                  bt_mgr_enable,
                                  bt_mgr_disable,
                                  ud);
}
```

---

## 为什么需要这个模块？

在重构前，WiFi 和蓝牙管理器各自包含以下重复代码：

```c
// 重构前（重复模式）
if (g_wifi_on) wifi_mgr_enable();
else           wifi_mgr_disable();

if (g_bt_on)   bt_mgr_enable();
else           bt_mgr_disable();
```

提取为 `svc_mgr_helper` 后：
- **消除重复**：相同的 if-else 逻辑只写一次
- **一致性**：所有服务的开关行为遵循同一模式
- **可扩展**：新增服务（如 GPS、NFC）时可直接复用

---

## 设计约束

1. **函数签名限制**：`enable` 和 `disable` 必须是 `void (*)(void)` 类型，不接受参数。这是因为 UI 开关回调通过 `xerintosh_new_switch_item()` 注册时，只允许一个 `void *ud` 参数，而 `svc_mgr_handle_switch_toggle` 本身已经占用了这个参数通道。

2. **同步执行**：`svc_mgr_handle_switch_toggle()` 是同步阻塞调用。如果 `enable()` 内部有长时间操作（如 WiFi 连接），会阻塞当前任务直到完成。这是嵌入式场景下的常见取舍。

---

> **See Also:** [App 层索引](index.md) | [应用初始化](app-init.md)
