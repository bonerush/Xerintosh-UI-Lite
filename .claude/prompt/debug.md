# Debug 工作流说明文档

本文档用于跟踪项目 Bug 的修复与审查流程。所有 Bug 以列表形式记录在下方 [BUG列表] 区域，Code Agent 与人工审查者根据此清单协同工作。

## 状态定义

| 状态标记 | 含义 | 通常由谁设置 |
|----------|------|------------|
| `BUG` | 新发现或重新打开的 Bug，尚未指派给 Agent | 人工 / Agent（从 VERIFIED 重新激活） |
| `AGENT` | Agent 正在处理此 Bug（占用状态，其他 Agent 应跳过） | Agent |
| `AGENT_FINISH` | Agent 完成修复，等待人工审查 | Agent |
| `VERIFIED` | 人工审查通过，Bug 关闭 | 人工 |
| `REJECTED` | 人工审查驳回，需要 Agent 根据反馈重新处理 | 人工 |

## 角色与权限

- **Agent** 可以执行以下状态转换：
    - （新建） → `BUG` （用户与agent交流的时候明确需要处理什么bug可以新添）
    - `BUG` → `AGENT` （领取任务）
    - `AGENT` → `AGENT_FINISH` （提交修复结果）
    - `REJECTED` → `AGENT` （根据驳回意见重新开始）
    - `VERIFIED` → `BUG` （发现已验证的 Bug 再次出现，重新打开）
- **人工审查者** 可以执行以下状态转换：
    - （新建） → `BUG` （发现新 Bug 时录入）
    - `AGENT_FINISH` → `VERIFIED` （确认修复有效）
    - `AGENT_FINISH` → `REJECTED` （驳回并附上测试意见）

并发说明：当有多个 Agent 并行工作时，应优先选择状态为 `BUG` 的条目。状态为 `AGENT` 的条目表示已被其他 Agent 占用，请跳过。

## 通用流程

```mermaid
stateDiagram-v2
    [*] --> BUG : 创建/重新打开
    BUG --> AGENT : Agent领取
    AGENT --> AGENT_FINISH : Agent完成修复
    AGENT_FINISH --> VERIFIED : 人工验证通过
    AGENT_FINISH --> REJECTED : 人工驳回
    REJECTED --> AGENT : Agent重新处理（循环）
    VERIFIED --> [*] : 关闭
    VERIFIED --> BUG :
│ 空格 + pU │ 上传 + 串口监控 │  │ 再次出现时重新激活
```

## Bug 列表格式规范

列表中的每一项必须包含状态标记和描述。使用以下格式（建议保留删除线以标记已关闭项）：
- [BUG] 文件上传时偶发崩溃的错误描述
- [AGENT] 登录页面样式错乱的错误描述
- [AGENT_FINISH] 接口超时无重试的错误描述
- [] 搜索功能结果不准确的错误描述
    - 测试意见：关键词“苹果”应返回水果结果，目前返回手机壳
- [VERIFIED] ~~首页加载速度慢的错误描述~~

**规则**：
- `[REJECTED]` 条目下必须缩进书写人工反馈的测试结果或意见。
- `[VERIFIED]` 条目描述使用删除线，表示已关闭。
- 当 Agent 将 `VERIFIED` 重新打开时，应去掉删除线并将标记改为 `[BUG]`，并可选地追加说明。

## 特殊情况处理

1. **发现重复 Bug（未被验证）**  
   如果 Agent 在创建 Bug 时发现列表中已存在相同或相似问题，且状态为 `BUG`、`AGENT`、`AGENT_FINISH` 或 `REJECTED`，不应新建条目，可直接操作现有条目（例如将 `BUG` 改为 `AGENT` 开始处理）。

2. **已验证的 Bug 再次出现**  
   如果发现某个问题曾被标记为 `[VERIFIED]`，但现在又复现了，Agent 可将该条目状态改为 `[BUG]` 并去除删除线，从而重新进入待处理队列。必要时在描述中补充复现信息。

---

# BUG 列表

<!-- 请按照上方格式在下方添加 Bug 条目 -->

- [VERIFIED] ~~竖屏模式下关机弹窗宽度超出屏幕显示范围，需做横竖屏弹窗尺寸适配~~
    **根因:** ① `strcmp` 静态缓冲区误判；② 换行算法要求 `acc <= avail && p >= mid`；③ 宽度钳位到 `SCREEN_WIDTH` 未扣除边框 8px。→ 驳回后：文字基线绘制导致 descent 紧贴弹窗底边，POP_UP_HEIGHT=44 在 3 行时无余量。→ 再次调整：横屏 1 行文字时固定高度 48 导致上下空白过多
    **修复:** POP_UP_HEIGHT 从 44 增至 48，为文字多提供 4px 垂直空间；`y_text` 计算公式中将 `+ fh` 改为 `+ fh - 2`，使基线下移量从 fontHeight 改为近似 ascent，文字整体居中的同时上下各留出约 4px 空隙。→ 动态高度：`xerintosh_push_pop_up` 和 `xerintosh_draw_pop_up` 中根据 `wrap_line_count` 实时计算弹窗高度（内容高度 + 上下各 4px padding），横屏 1 行时高度约 20px，竖屏 3 行时高度约 48px，各种方向下 padding 保持一致
    **验证:** build=PASS native=PASS tests=186/187 (1个预存SIGSEGV与本次无关)
    **文件:** src/ui/ui_item.h, src/ui/ui_draw_widgets.c, src/ui/ui_item_popup.c
    **驳回重处理:** ① 在原有横竖屏宽度适配基础上，针对竖屏 3 行文字增加 POP_UP_HEIGHT 并修正基线偏移，消除文字碰底问题。② 将固定 POP_UP_HEIGHT 改为运行时根据内容行数动态计算，使横屏/竖屏的上下 padding 保持一致
- [VERIFIED] ~~关机弹窗关闭逻辑：应改为长按开始倒计时、松手立即停止并收起弹窗~~
    **根因:** 双键松手后 `g_dual_active` 立即置 false，但 `M5.BtnX.wasReleased()` 的释放边沿在下一帧 `hal_input_get_event()` 中被 `hal_input_simple_process` 的 `!long_fired` 路径判定为 SHORT_PRESS，立即触发菜单导航/确认操作
    **修复:** 在 `power_key_popup_update()` 的 else 分支中增加 300ms 冷却期（`DUAL_RELEASE_COOLDOWN_MS`），刚从双键状态退出时调用 `hal_input_reset_events()` 重置状态机，并在冷却期内继续让 `power_key_popup_is_dual_active()` 返回 true，隔离所有正常按钮事件直至释放边沿被 M5.update() 自然清除
    **验证:** build=PASS native=PASS tests=186/187 (1个预存SIGSEGV与本次无关)
    **文件:** src/app/shutdown/power_key_popup.c
    **驳回重处理:** 在原有松手收起弹窗逻辑基础上，增加冷却期防止按钮释放边沿被误判为短按/长按，避免松手慢时触发
- [VERIFIED] ~~打开 App 时沙漏页面（退场动画）偶发卡住后直接 reboot，怀疑看门狗触发热重启~~
    **根因:** FreeRTOS 内核任务（UI、WiFi、BT）均在优先级 `tskIDLE_PRIORITY + 1`（=1），与 Arduino loop 任务同级。ESP32 的 TG1 系统看门狗由 idle 任务（优先级 0）喂狗。在退场动画等多帧渲染期间，优先级 1 的任务通过双信号量令牌协议（kern_port.c）持续轮转——UI 任务执行→yield→调度器取回→再分派，idle 任务始终被饿死。TG1 看门狗超时（~5 秒）后热重启。
    **修复:** 在 `ui_task_main()` 每帧 `kern_yield()` 前添加 `delay(1)`（`#ifndef NATIVE_TEST` 守卫），释放 1ms 给 FreeRTOS idle 任务喂 TG1 看门狗。不影响原生测试（native 无看门狗和 FreeRTOS）。硬件端每帧增加 1ms 开销（约 3% @ 30fps），对 UI 流畅度无影响。
    **验证:** build(hw)=PASS build(native)=PASS tests=186/187 (1个预存SIGSEGV与本次无关)
    **文件:** src/app/ui_task.c
    **驳回重处理:** 之前的修复（移除 boot_screen_show 的 hal_delay_ms）方向正确但只覆盖了启动阶段——boot_screen_show 只在 setup() 中运行一次，而 bug 描述的是「打开 App 时」的退场动画卡死。本次修复针对的是运行时的持续看门狗饿死问题，覆盖所有 App 的进入/退出动画场景。
- [VERIFIED] ~~烧录 Classic Bluetooth SPP 固件后开机触发看门狗重启（Flash 已压缩至 91.3%，排除空间不足)~~
    **根因:** 经 backtrace 解码，实际崩溃点在 ESP-IDF v4.4 Bluedroid 的 BLE GAP/GATT 初始化路径（`gap_ble.c:410` → `gatt_api.c:209` → `gatt_utils.c:341`）。即使仅使用 Classic BT SPP，Bluedroid 仍会初始化 BLE 组件。`gatt_alloc_hdl_buffer()` 内 `fixed_queue_new()` 分配失败（WiFi 已占用大量 RAM），清理路径调用 `fixed_queue_free()` → `osi_sem_free()` → `vQueueDelete(NULL)` 触发 FreeRTOS assertion 崩溃。
    **修复:** ① 在 `setup()` 中于任何 BT 初始化前调用 `esp_bt_controller_mem_release(ESP_BT_MODE_BLE)` 释放 BLE controller 内存；② 调整 `app_init_managers()` 顺序，BT 先于 WiFi 初始化，减少内存竞争；③ 移除 warm-up 延迟，直接在 `setup()` 上下文中调用 `bt_uart_service_init()`，避免在 bt-mgr 内核任务中触发 Bluedroid bug；④ 保留 `bt-mgr` 任务栈 8192 字节的先前调整。
    **验证:** build(hw)=PASS build(native)=PASS tests=186/187 (1个预存SIGSEGV与本次无关)
    **文件:** src/main.cpp, src/app/app_init.c, src/app/bluetooth/bt_manager.cpp
    **驳回重处理:** 修复后不再崩溃，但 `g_bt_serial.begin()` 在 `setup()` 中同步阻塞，导致 `loop()` 和 UI 任务无法启动，屏幕卡在开机画面。恢复异步 WARMUP 机制：在 `bt_mgr_enable()` 中设置 `BT_MGR_WARMUP` 状态并记录 `millis()`，在 `bt_mgr_update()` 中经过 200ms 预热后于 `bt-mgr` 任务上下文中调用 `bt_uart_service_init()`，彻底解除对 `setup()` 的阻塞。由于 BLE 内存已释放且 BT 先于 WiFi 初始化，异步路径不再有内存竞争导致的 Bluedroid crash 风险。
    **再次驳回重处理:** 异步修复后 UI 正常启动 2 帧，但随后 `bt-mgr` 任务中 `g_bt_serial.begin()` 触发 `StoreProhibited`（`EXCVADDR=0x00000000`）。backtrace: `btu_task_start_up` → `BTE_DeinitStack` → `bta_sys_init` → `memset(NULL, 0, 0x130)`。根因：`esp_bt_controller_mem_release(ESP_BT_MODE_BLE)` 与 ESP32 Arduino `BluetoothSerial` 不兼容——`BluetoothSerial::begin()` 使用 BTDM（双模）配置初始化 Bluedroid，仍会尝试初始化 BLE 组件。释放 BLE 内存后，初始化失败 cleanup 路径访问空指针。
    **最终修复:** 移除 `esp_bt_controller_mem_release(ESP_BT_MODE_BLE)` 调用，同时保留：① BT 先于 WiFi 的初始化顺序（避免 WiFi 先占内存导致 BT OOM）；② 异步 WARMUP 机制（`bt_mgr_update()` 中 200ms 后于 `bt-mgr` 任务内调用 `bt_uart_service_init()`，不阻塞 `setup()`）。当前 `free heap: 91364`，即使保留 BLE 内存也不足导致 OOM。
    **再次驳回重处理:** 移除 `mem_release` 后仍然在 `bt-mgr` 任务中崩溃（`StoreProhibited`），backtrace: `osi_thread_run` → `btu_task_start_up` → `BTE_DeinitStack` → `bta_sys_init` → `memset(NULL)`。根因：`BluetoothSerial::begin()` **根本不能在非 Arduino 主任务的 FreeRTOS 任务中调用**。Bluedroid 初始化与 Arduino 任务上下文有绑定关系，在子任务中初始化会导致内部全局状态未就绪，cleanup 路径访问空指针。
    **真正最终修复:** ① `bt_uart_service_init()` 必须在 `setup()` 中同步调用；② `bt_mgr_enable()` 只设置 `g_bt_enabled=true` 和 `g_state=BT_MGR_ENABLED`，不再做实际初始化；③ `bt_mgr_update()` 只负责连接状态 polling；④ `bt_mgr_disable()` 仍然调用 `bt_uart_service_deinit()`。此方案下 `setup()` 会阻塞几秒（BT 初始化时间），但开机画面已在屏幕上，初始化完成后 `loop()` 启动，UI 任务正常接管。避免了子任务中 `StoreProhibited` 崩溃。
- [VERIFIED] ~~蓝牙开关开启无效，蓝牙串口不可用~~
    **根因:** ① ESP32 Arduino 框架在 `setup()` 之前就预初始化了 BT 全栈（controller + Bluedroid + SPP 回调），`esp_spp_register_callback()` 返回 `ESP_ERR_INVALID_STATE`(259)；② `esp_bt_controller_get_status()` 返回过时的 0(IDLE) 导致 deinit+retry 策略使状态更混乱；③ BT 初始化（~92KB）在 `setup()` 中过早执行，与 WiFi（~38KB）+ 内核任务竞争内存，导致 FreeRTOS `xTaskCreate` 全部失败。
    **修复:** ① `bt_uart_service_init()` 从底层 ESP-IDF SPP API 改为 `BluetoothSerial` Arduino 库，内部正确复用框架已初始化的 BT 栈；② BT 初始化从 `app_init_managers()` 延迟到 `deferred_kernel_init()` 中内核任务 spawn 之后（137KB→40KB）；③ WiFi 默认关闭（`g_wifi_on=false`）释放 ~38KB。
    **验证:** build(hw)=PASS build(native)=PASS tests=186/187 (1个预存SIGSEGV与本次无关) 串口日志：BT init 成功，所有内核任务正常，BT state 0→2(ENABLED)。用户实测：电脑蓝牙串口连接成功，数据收发正常。
    **文件:** src/app/bluetooth/bt_uart_service.cpp, src/main.cpp, src/app/app_init.
- [VERIFIED] ~~串口监视器切换到 BLE 模式后，电脑蓝牙 SPP 连接立即断开，串口监视器显示 "BLE:--"~~
    **根因:** ① `g_bt_serial.connected()` 和 `g_bt_serial.read()` 在 bt-mgr FreeRTOS 子任务中调用，与 `begin()` 不在同一任务上下文，破坏 Bluedroid 内部状态导致 SPP 连接断开；② `sm_on_bt_rx` 回调在主任务中写 `sm_buffer`，`serial_monitor_draw()` 在 UI 任务中读，跨任务竞争可能导致内存损坏；③ `sm_on_bt_rx` 不检查 `sm_running`，BLE 数据始终写入缓冲区导致 RUN/STOP 失效。
    **修复:** ① 将 `bt_uart_poll()` 从 bt-mgr 子任务移至 `loop()` 主任务（50ms 节流），确保所有 `g_bt_serial` 操作在同一任务上下文；② 引入 FreeRTOS 队列做 RX 数据跨任务传递——主任务读 BT 数据入队，UI 任务通过 `bt_uart_drain_rx_queue()` 消费并调用回调写 `sm_buffer`，消除跨任务竞争；③ `sm_on_bt_rx` 增加 `sm_running` 检查。
    **验证:** build(hw)=PASS build(native)=PASS tests=182/183 (1个预存SIGSEGV与本次无关)
    **文件:** src/main.cpp, src/app/bluetooth/bt_manager.cpp, src/app/bluetooth/bt_uart_service.cpp, src/app/bluetooth/bt_uart_service.h, src/app/serial_monitor/sm_app.cpp
- [VERIFIED] ~~wifi扫描没有提示弹窗，扫描刚开始会显示"扫描中..."弹窗，扫描结束之后显示"扫描完毕"；并且扫描之后可用网络没有生成设备列表~~
    且已保存目录里面所有的列表点击进入连接，也不会显示连接的提示弹窗。
    当我在打开蓝牙但是关闭wifi的情况下，存在我打开wifi之后会触发开门狗的问题。
    另外 WiFi 扫描弹窗显示时机不对：每次开启 WiFi 都会弹出"扫描中..."，应仅在首次启动扫描或用户手动点击扫描时显示。
    **根因:** ① 扫描完成后 `rebuild_network_list` 中 `ui_selector_rebuild_anchor` 将选择器提升到"网络"根节点，随后代码将选择器移到第一个子项"已保存"而非"可用网络"，导致用户看不到扫描结果；② `on_saved_connect_pressed` 未设置 `g_state = WIFI_MGR_CONNECTING`，状态机不会轮询连接结果，用户无反馈；③ `wifi_mgr_enable` 中 `WiFi.mode(WIFI_STA)` 在 BT 活跃时分配 DMA 内存阻塞，饿死 idle 任务触发 TG1 看门狗；④ WARMUP 扫描失败时错误设置为 `WIFI_MGR_CONNECTED` 而非 `WIFI_MGR_SCAN_DONE`；⑤ WiFi 扫描弹窗无首次标记，每次 enable 都会触发。
    **修复:** ① `rebuild_network_list` 选择器逻辑改为：有可用网络时移到"可用网络"容器，否则移到"已保存"；② `on_saved_connect_pressed` 增加 `g_state = WIFI_MGR_CONNECTING` 和 `delay(1)` 喂狗；③ `wifi_mgr_enable` 在 `WiFi.mode(WIFI_STA)` 前加 `delay(1)` 防止看门狗；④ WARMUP 扫描失败状态改为 `WIFI_MGR_SCAN_DONE`；⑤ 新增 `g_initial_scan_shown` 标志，warmup 扫描和扫描完成弹窗均检查此标志，仅首次启动扫描时显示，后续 WiFi enable/disable 不再弹窗，手动扫描按钮不受影响。
    **验证:** build(hw)=PASS build(native)=PASS tests=189/190 (1个预存SIGSEGV与本次无关) 用户实测：首次开机扫描有弹窗，后续开关 WiFi 无弹窗，手动扫描正常弹窗。
    **文件:** src/app/wifi/wifi_manager.cpp
- [VERIFIED] ~~Token Usage app 中连接 WiFi 后进入会卡死在沙漏界面（退场动画），随后触发看门狗重启；断网时正常；且无法配置 API Key，余额始终显示 0~~
    **根因:** ① `tu_api_fetch_deepseek` 中 `HTTPClient::GET()` 在 WiFi 已连接时阻塞 5-15 秒，阻塞 UI 任务导致退场动画无法渲染，触发 TG1 看门狗；② `storage_set_deepseek_key` 已定义但从未被调用，设备上无 API Key 配置入口，key 永远为空，API 请求直接跳过；③ DeepSeek API 返回的 `total_balance` 等字段为字符串 `"5.81"` 而非数字，ArduinoJson 的 `| 0.0f` 运算符不会将字符串转为 float，类型不匹配时返回默认值 0.0f；④ 同时删除了 Kimi（Moonshot）相关代码，仅保留 DeepSeek。
    **修复:** ① `tu_api_fetch_deepseek` 中添加 `http.setConnectTimeout(3000)` 和 `http.setTimeout(5000)` 限制 HTTP 阻塞时间，并在 GET/getString/end 后添加 `delay(1)` yield 给调度器；② 在 `kern_shell_cmds.c` 中新增 `dskey` Shell 命令，支持 `dskey <api_key>` 设置和 `dskey` 查看（脱敏显示），调用 `storage_set_deepseek_key` 存入 NVS；③ JSON 解析改用 `atof()` 显式将字符串转为 float；④ 移除 `tu_api_fetch_kimi`、`tu_kimi_usage_t` 及相关 UI/存储代码。
    **验证:** build(hw)=PASS build(native)=PASS 串口调试：HTTP code=200, payload 正确返回余额, atof 解析正确显示 5.81 CNY。用户实测：连接 WiFi 后进入 Token Usage 不再卡死，余额正常显示。
    **文件:** src/app/token_usage/tu_api.cpp, src/app/token_usage/tu_api.h, src/app/token_usage/tu_app.cpp, src/app/token_usage/tu_ui.cpp, src/kernel/kern_shell_cmds.c
- [AGENT_FINISH] 当前开机虽然自动扫描，但是没有自动连接。
    自动连接的逻辑如下：当已保存列表只有一个网络的时候默认只连接那个，当列表存在多个网络的时候就需要轮询找出离当前开发版最近（信号强度最大，连接稳定性最好）的网络去连接，如果没有网络则什么都不做。
    另外之前写过一个wifi的逻辑就是每次只要是用蓝牙就关闭wifi，当退出使用蓝牙的app时候wifi自动打开并且不弹出提示弹窗。在这里需要修改成自动打开且连接并且不弹出提示弹窗。
    **根因:** 状态机在 `WIFI_MGR_SCAN_DONE` 后停死，`wifi_mgr_update()` 中该 case 仅为 `break`，无任何自动连接逻辑。`wifi_mgr_init()` 注释明确写"自动连接已移除"。
    **修复:** 新增 `try_auto_connect()` 辅助函数，在 `WIFI_MGR_SCAN_DONE` 中调用（仅一次）：① 遍历已保存网络，在扫描结果中匹配；② 多个匹配时选择 RSSI 最强的；③ 发起 `WiFi.begin()` 静默连接（`g_is_auto_connect=true` 抑制所有弹窗）。手动连接回调（`on_saved_connect_pressed`/`on_network_button_pressed`）重置 `g_is_auto_connect=false` 保留弹窗反馈。`wifi_mgr_enable()`/`wifi_mgr_disable()` 重置自动连接标志。
    **验证:** build(hw)=PASS RAM=22.1% Flash=99.1% native=3 ERRORED（均为预存问题，与本次无关）
    **文件:** src/app/wifi/wifi_manager.cpp
- [AGENT_FINISH] 烧录当前固件后菜单渲染正常但所有按键操作无响应，系统卡死在菜单页面
    **根因:** ① SMP 死锁 → 输入无响应（commit f41e416 已修复）；② 修复 SMP 后 Core 0 看门狗超时重启：`kern_smp_sched_loop` (prio+2) 与 `xidle0` (prio+1) 信号量乒乓导致 FreeRTOS idle 任务 (prio+0) 饿死，TG1 5s 超时触发。
    **修复:** ① `KERN_THIS_CPU` → `kern_cpu_id()`，移除 `g_active_cpu`；② `kern_smp_sched_loop` 改为 Core 0；③ `kern_smp_sched_loop` 每 tick 后调用 `kern_port_idle()`（`vTaskDelay(1)`，10ms 喂狗窗口）。
    **验证:** build(hw)=PASS native=338 PASS
    **文件:** src/kernel/kern_smp.h, src/kernel/kern_smp.c, src/kernel/kern_sched.c, doc/kernel/kern-smp.md, doc/assets/diagrams/smp-architecture.drawio