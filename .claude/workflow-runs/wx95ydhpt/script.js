export const meta = {
  name: 'm5stick-to-espidf-migration-implement',
  description: '实施 M5Stick-P1 Arduino → xerintosh esp-idf 迁移并实时提交',
  phases: [
    { title: 'Skeleton', detail: '拷贝源码、更新构建系统、生成源文件列表、首次提交' },
    { title: 'HAL-Basic', detail: '迁移 HAL system、input、power key、power off' },
    { title: 'Core-Platform', detail: '迁移 storage、serial、debug_serial、WiFi、token usage' },
    { title: 'GPIO-ADC', detail: '迁移 GPIOFS、oscilloscope、flasher GPIO/UART' },
    { title: 'Display', detail: '迁移显示 HAL（M5GFX → LovyanGFX 兼容层）' },
    { title: 'Main-Cleanup', detail: '重写 main.cpp、清理 Arduino 头文件、最终验证提交' }
  ]
}

const source = args.sourceProject
const target = args.targetProject

phase('Skeleton')
log(`开始迁移骨架：${source} → ${target}`)

const skeleton = await agent(`将 ${source} 的源码树迁移到 ${target}，完成第一阶段骨架工作：

1. 删除 ${target}/src/main.c。
2. 将 ${source}/src/ 下所有 .c/.cpp/.h/.hpp 文件拷贝到 ${target}/src/，保持目录结构。
3. 将 ${source}/fonts/ 目录拷贝到 ${target}/src/fonts/（如果不存在则创建）。
4. 创建 ${target}/src/CMakeLists.txt：使用 FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*) 收集所有源文件（包括 .c/.cpp/.h/.hpp），然后调用 idf_component_register(SRCS ${app_sources})。
5. 更新 ${target}/platformio.ini：
   - [env:m5stick-c] 保留 platform=espressif32, board=m5stick-c
   - framework = espidf
   - 删除 lib_deps 中的 m5stack/M5Unified、M5GFX、bblanchon/ArduinoJson
   - 保留 native 环境不变
   - build_flags 改为：-std=c++17 -Os -fno-exceptions -fno-rtti -D CONFIG_PREEMPT_ENABLED -D CONFIG_SMP_ENABLED -D CONFIG_MPU_ENABLED
   - 可选：monitor_speed = 115200, monitor_rts = 0, monitor_dtr = 0
6. 创建 ${target}/sdkconfig.defaults，包含：
   CONFIG_FREERTOS_UNICORE=n
   CONFIG_ESP32_DEFAULT_CPU_FREQ_240=y
   CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192
   CONFIG_ESP_CONSOLE_UART_DEFAULT=y
   CONFIG_ESP_ERR_TO_NAME_LOOKUP=y
7. 创建 ${target}/components 目录（空目录即可，为后续 LovyanGFX 做准备）。
8. 使用 Bash 运行 git add 和 git commit，提交信息："feat(migration): copy M5Stick-P1 source skeleton and switch to esp-idf"

返回：已创建/修改的文件列表、提交哈希、遇到的错误。`, {
  label: 'migration-skeleton',
  phase: 'Skeleton',
  schema: {
    type: 'object',
    properties: {
      files: { type: 'array', items: { type: 'string' } },
      commitHash: { type: 'string' },
      errors: { type: 'array', items: { type: 'string' } }
    },
    required: ['files','commitHash','errors']
  }
})

if (skeleton.errors.length > 0) {
  log('骨架阶段出现错误，停止迁移')
  return { skeleton, stopped: true }
}

log(`骨架完成，提交 ${skeleton.commitHash}`)

phase('HAL-Basic')
log('开始迁移基础 HAL：system、input、power key、power off')

const halBasic = await parallel([
  () => agent(`迁移 ${target}/src/hal/hal_system.cpp：

硬件环境（#else 路径）把 Arduino millis()/delay() 替换为 esp-idf API：
- #include <Arduino.h> 替换为 #include <esp_timer.h> 和 #include <freertos/FreeRTOS.h> / <freertos/task.h>
- hal_get_ticks() 返回 (uint32_t)(esp_timer_get_time() / 1000)
- hal_delay_ms() 调用 vTaskDelay(pdMS_TO_TICKS(ms))
- 保持 native 路径不变

改完后用 Bash 运行 git add 和 git commit，消息："refactor(hal): system clock uses esp_timer and vTaskDelay"
返回修改摘要。`, {
    label: 'hal-system',
    phase: 'HAL-Basic'
  }),
  () => agent(`迁移 ${target}/src/hal/hal_input.cpp：

硬件环境用 esp-idf GPIO 驱动替代 M5Unified 按钮 API：
- 移除 #include <M5Unified.h>
- 添加 #include <driver/gpio.h> 和 #include <esp_timer.h>
- 定义 BUTTON_A_GPIO 36, BUTTON_B_GPIO 37
- hal_input_init()：调用 gpio_set_direction 和 gpio_set_pull_mode（GPIO_PULLUP_ONLY）初始化两个按键为输入
- hal_input_update()：空操作（GPIO 电平在 get_event 直接读取）
- hal_input_get_event()：直接 gpio_get_level 读取当前电平，用内部状态机检测按下/释放边沿（保留现有 hal_input_dc_process / hal_input_simple_process）
- 启动保护：开机 300ms 内忽略事件
- hal_input_is_pressed()：gpio_get_level == 0 视为按下（按键低电平有效）
- 用 hal_get_ticks() 替代 millis()
- 保持 native 测试路径不变

改完后提交："refactor(hal): input uses esp-idf GPIO for buttons A/B"
返回修改摘要。`, {
    label: 'hal-input',
    phase: 'HAL-Basic'
  }),
  () => agent(`迁移 ${target}/src/hal/hal_power_key.cpp：

硬件环境用 esp-idf I2C 驱动替代 M5.Power.getKeyState()：
- 移除 #include <M5Unified.h>
- 添加 #include <driver/i2c_master.h>（或 legacy driver/i2c.h，任选其一）和 #include <esp_timer.h>
- 实现 axp192_read_pek()：向 AXP192 I2C 地址 0x34 读取寄存器 0x46（IRQ status 3），返回 uint8_t
- 如果 I2C 总线未初始化，可在 hal_power_key_init() 中初始化 I2C master（SDA GPIO 0, SCL GPIO 26，或参考 M5Stick-C 原理图）
- hal_power_key_get_event() 中把 M5.Power.getKeyState() 替换为 axp192_read_pek()
- 用 hal_get_ticks() 替代 millis()
- 保持 native 测试路径不变

改完后提交："refactor(hal): power key reads AXP192 register via esp-idf I2C"
返回修改摘要。`, {
    label: 'hal-power-key',
    phase: 'HAL-Basic'
  }),
  () => agent(`迁移 ${target}/src/hal/hal_power_off.cpp：

硬件环境移除 M5Unified 依赖，改为 esp-idf GPIO + AXP192 I2C：
- 移除 #include <M5Unified.h>
- 实现 hal_power_off_hw()：
  1. 先关闭 LCD 背光（gpio_set_level 控制背光 GPIO，低电平关闭；或保留 hal_display_set_brightness(0)）
  2. 通过 I2C 向 AXP192 写寄存器触发断电（参考 AXP192 数据手册：POWER_OFF 寄存器 0x32 bit 7）
- 保持 native 路径为空操作

改完后提交："refactor(hal): power off uses AXP192 I2C instead of M5Unified"
返回修改摘要。`, {
    label: 'hal-power-off',
    phase: 'HAL-Basic'
  })
])

log(`基础 HAL 迁移完成，提交数 ${halBasic.filter(Boolean).length}`)

phase('Core-Platform')
log('开始迁移核心平台模块')

const corePlatform = await parallel([
  () => agent(`迁移 ${target}/src/app/storage/storage.cpp：

硬件环境把 Arduino Preferences 替换为 esp-idf NVS：
- 移除 #include <Preferences.h>
- 添加 #include <nvs_flash.h> 和 #include <nvs.h>
- 保持 NVS_NAMESPACE = "Xerintosh" 和所有 key 名称不变
- 把 Preferences 对象替换为 nvs_handle_t：
  - storage_init() 调用 nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle)，并确保计数 key 存在
  - read_count/write_count 使用 nvs_get_u8 / nvs_set_u8
  - 凭据读写使用 nvs_get_str / nvs_set_str
  - get/set short 使用 nvs_get_i16 / nvs_set_i16
  - get/set bool/uchar 使用 nvs_get_u8 / nvs_set_u8
- 保持 native 测试桩不变

改完后提交："refactor(storage): replace Arduino Preferences with esp-idf NVS"
返回修改摘要。`, {
    label: 'storage',
    phase: 'Core-Platform'
  }),
  () => agent(`迁移串口相关文件：${target}/src/kernel/debug_serial.cpp、${target}/src/kernel/devices/dev_ttyS0.cpp、${target}/src/app/serial_input/serial_input.cpp、${target}/src/app/serial_monitor/sm_app.cpp。

统一用 esp-idf UART 驱动替换 Arduino Serial：
- 移除 #include <Arduino.h>
- 添加 #include <driver/uart.h>
- 默认 UART 端口为 UART_NUM_0
- 实现或调用 uart_init_default()：uart_driver_install(UART_NUM_0, 512, 512, 0, NULL, 0)，uart_param_config 为 115200 8N1
- debug_serial.cpp：debug_printf / debug_vprintf 调用 uart_write_bytes(UART_NUM_0, buf, len)
- dev_ttyS0.cpp：Serial.available() → uart_get_buffered_data_len；Serial.read() → uart_read_bytes；Serial.write() → uart_write_bytes
- serial_input.cpp：Serial.print/println/flush/available/read 全部替换为 uart_write_bytes / uart_get_buffered_data_len / uart_read_bytes；millis() → hal_get_ticks()
- sm_app.cpp：Serial.available/read 替换为 uart_get_buffered_data_len / uart_read_bytes
- 保持 native 路径不变

改完后提交："refactor(uart): replace Arduino Serial with esp-idf uart driver"
返回修改摘要。`, {
    label: 'serial',
    phase: 'Core-Platform'
  }),
  () => agent(`迁移 ${target}/src/app/wifi/wifi_manager.cpp：

移除 Arduino WiFi 封装，全部使用 esp-idf WiFi API：
- 移除 #include <WiFi.h> 和 #include <Arduino.h>
- 添加 #include <esp_wifi.h>、<esp_event.h>、<esp_log.h>（已存在）
- ESP.getFreeHeap() → esp_get_free_heap_size()
- ESP.getMaxAllocHeap() → heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT)
- Serial.printf/flush 改为 uart_printf 辅助函数（或保留为 debug log，改用 ESP_LOG/通过 uart 输出）
- WiFi.persistent(false) → nvs 中清除或不调用（ESP-IDF 默认写入 NVS；如要禁用可调用 esp_wifi_set_storage(WIFI_STORAGE_RAM)）
- WiFi.mode(WIFI_STA) → esp_wifi_set_mode(WIFI_MODE_STA)
- WiFi.mode(WIFI_OFF) → esp_wifi_set_mode(WIFI_MODE_NULL)
- WiFi.disconnect() → esp_wifi_disconnect()
- WiFi.disconnect(true) → esp_wifi_disconnect() 后 esp_wifi_stop() / esp_wifi_deinit()
- WiFi.begin(ssid, pass) → esp_wifi_set_config(WIFI_IF_STA, &wifi_config) 后 esp_wifi_connect()
- WiFi.scanComplete() → esp_wifi_scan_get_ap_num(&num) 然后 esp_wifi_scan_get_ap_records(&num, records)
- WiFi.SSID(index) → 从本地缓存的 wifi_ap_record_t 数组读取 ssid
- WiFi.RSSI(index) → 同上读取 rssi
- WiFi.status() → esp_wifi_sta_get_ap_info(&ap_info) 或维护连接状态（可用 IP_EVENT_STA_GOT_IP / WIFI_EVENT_STA_DISCONNECTED）
- String 类型替换为 char 数组和 strncpy
- millis() → hal_get_ticks()
- delay(1) → vTaskDelay(pdMS_TO_TICKS(1))

改完后提交："refactor(wifi): use esp_wifi API instead of Arduino WiFi"
返回修改摘要。`, {
    label: 'wifi',
    phase: 'Core-Platform'
  }),
  () => agent(`迁移 ${target}/src/app/token_usage/tu_api.cpp：

硬件环境用 esp-idf HTTP + cJSON 替代 Arduino HTTPClient + ArduinoJson：
- 移除 #include <HTTPClient.h>、<ArduinoJson.h>、<Arduino.h>
- 添加 #include <esp_http_client.h>、<cJSON.h>、<string.h>
- 构造 Authorization 头：用 snprintf 拼接到固定缓冲区 "Authorization: Bearer <api_key>"
- 使用 esp_http_client_init / set_url / set_method(HTTP_METHOD_GET) / set_header / perform / cleanup
- 读取响应体：用 esp_http_client_read 循环读到缓冲区
- 用 cJSON_Parse 解析 JSON，然后 cJSON_GetObjectItem 提取字段
- delay(1) → vTaskDelay(pdMS_TO_TICKS(1))
- 保持 native 测试桩不变

改完后提交："refactor(token_usage): replace HTTPClient/ArduinoJson with esp_http_client/cJSON"
返回修改摘要。`, {
    label: 'token-usage',
    phase: 'Core-Platform'
  })
])

log(`核心平台模块迁移完成，提交数 ${corePlatform.filter(Boolean).length}`)

phase('GPIO-ADC')
log('开始迁移 GPIO、ADC、Flasher')

const gpioAdc = await parallel([
  () => agent(`迁移 ${target}/src/kernel/kern_gpiofs.c：

硬件环境用 esp-idf GPIO 驱动替代 Arduino 函数：
- 移除 #include <Arduino.h>
- 添加 #include <driver/gpio.h>
- gpio_read()：digitalRead(pin) → gpio_get_level(pin)
- gpio_set_output()：pinMode(pin, OUTPUT) → gpio_set_direction(pin, GPIO_MODE_OUTPUT)；digitalWrite → gpio_set_level
- 在合适位置（如 gpiofs 初始化时）对每个引脚调用 gpio_set_direction 配置输入/输出
- HIGH/LOW 替换为 1/0
- 保持 native 路径不变

改完后提交："refactor(gpiofs): use esp-idf gpio driver instead of Arduino pin functions"
返回修改摘要。`, {
    label: 'gpiofs',
    phase: 'GPIO-ADC'
  }),
  () => agent(`迁移 ${target}/src/app/oscilloscope/oscilloscope_app.c：

硬件环境用 esp-idf ADC 驱动替代 Arduino analogRead：
- 移除 #include <Arduino.h>
- 添加 #include <esp_adc/adc_oneshot.h>、<esp_adc/adc_cali.h>（可选）、<driver/gpio.h>
- 定义 SCOPE_ADC_UNIT = ADC_UNIT_1，SCOPE_ADC_CHANNEL = ADC_CHANNEL_0（GPIO36）
- 在 #else 路径实现 scope_adc_init()：adc_oneshot_new_unit / adc_oneshot_config_channel / adc_cali_create_scheme_line
- scope_sample_one()：analogRead(SCOPE_PIN) → adc_oneshot_read(...)
- pinMode(SCOPE_PIN, INPUT) 和 analogSetPinAttenuation(SCOPE_PIN, ADC_11db) 替换为 adc 初始化配置， attenuation = ADC_ATTEN_DB_11
- delayMicroseconds(us) → esp_rom_delay_us(us)
- 保持 native 路径的 weak 桩不变

改完后提交："refactor(oscilloscope): use esp-idf adc_oneshot instead of Arduino analogRead"
返回修改摘要。`, {
    label: 'oscilloscope',
    phase: 'GPIO-ADC'
  }),
  () => agent(`迁移 ${target}/src/app/flasher/flasher_gpio.cpp：

硬件环境用 esp-idf GPIO + UART 替代 Arduino：
- 移除 #include <Arduino.h>
- 添加 #include <driver/gpio.h>、<driver/uart.h>
- s_flasher_uart 从 HardwareSerial* 改为 uart_port_t（UART_NUM_1）
- flasher_init_pins()：uart_driver_install(UART_NUM_1, 512, 512, 0, NULL, 0)，uart_param_config(115200 8N1)，uart_set_pin(tx=GPIO 26, rx=GPIO 36)
- pinMode/digitalWrite boot pin → gpio_set_direction + gpio_set_level
- flasher_uart_write() → uart_write_bytes(UART_NUM_1, ...)
- flasher_uart_read() → uart_read_bytes(UART_NUM_1, ...)
- delay(100) → vTaskDelay(pdMS_TO_TICKS(100))
- 保持 native 路径不变

改完后提交："refactor(flasher): use esp-idf GPIO/UART instead of Arduino Serial1"
返回修改摘要。`, {
    label: 'flasher-gpio',
    phase: 'GPIO-ADC'
  })
])

log(`GPIO/ADC/Flasher 迁移完成，提交数 ${gpioAdc.filter(Boolean).length}`)

phase('Display')
log('开始迁移显示 HAL（高风险阶段）')

const display = await agent(`迁移 ${target}/src/hal/ 下的显示相关文件：hal_display_fb.cpp、hal_display_draw.cpp、hal_display_font.cpp、hal_display_adv.cpp。

策略：由于显示 API 与 M5GFX 深度耦合，这里不直接引入 LovyanGFX，而是先在 esp-idf 路径下实现一个基于 esp_lcd 的 ST7735S 驱动 + 软件绘制回退，尽量复用 native 路径的 hal_display_draw.cpp 软件实现。

具体步骤：
1. 保留 native 路径（#ifdef NATIVE_TEST）不变。
2. 在 ${target}/components 目录下创建 xerintosh_display 组件，或直接在 src 中新增 xerintosh_display/st7735s_driver.c/.h 和 lcd_hal.c。
3. hal_display_fb.cpp #else 路径：
   - 移除 M5Unified/M5GFX 头文件
   - 添加自定义 lcd_hal.h
   - g_canvas 不再使用 M5Canvas*，改为指向内部帧缓冲的指针（RGB565 或 RGB332）
   - hal_display_init()：调用 st7735s_init() 初始化 SPI + LCD；分配 framebuffer（80x160 RGB565 或 RGB332）
   - hal_display_set_rotation/set_brightness/get_rotation 保留状态变量
   - hal_display_clear_color() 填充 framebuffer
   - hal_display_flush() 把 framebuffer 通过 st7735s_flush() 发送到屏幕
4. hal_display_draw.cpp #else 路径：
   - 如果已有 native 软件实现，可让 #else 路径直接调用与 native 相同的绘制函数（绘制到 framebuffer）
   - 或保留 g_canvas->drawPixel 等 API 调用，但用内部 framebuffer 包装
5. hal_display_font.cpp #else 路径：
   - 先用简化字体：使用 native 路径的 6x8 位图字体（bitmap font）来渲染 ASCII
   - hal_draw_string 先只支持 ASCII；中文字体暂时用 '?' 占位或保留 cn_font_subset 数据但暂不渲染
   - hal_get_string_width / hal_get_font_height 按字体表计算
6. hal_display_adv.cpp #else 路径：
   - XOR 矩形、XBM 位图用 framebuffer 软件实现（与 native 路径一致）
   - 裁剪矩形暂时空操作或简单实现

目标：先让编译通过，后续可再用 LovyanGFX 替换提升性能。

改完后提交："refactor(display): replace M5GFX with esp-idf ST7735S framebuffer driver"
返回修改摘要和可能遗留的 TODO。`, {
  label: 'display',
  phase: 'Display',
  schema: {
    type: 'object',
    properties: {
      summary: { type: 'string' },
      todos: { type: 'array', items: { type: 'string' } },
      commitHash: { type: 'string' }
    },
    required: ['summary','todos','commitHash']
  }
})

log(`显示 HAL 迁移完成：${display.commitHash}`)

phase('Main-Cleanup')
log('重写主入口并清理剩余 Arduino 依赖')

const mainCleanup = await parallel([
  () => agent(`重写 ${target}/src/main.cpp：

把 Arduino setup()/loop() 改为 esp-idf app_main()：
- 移除 #include <M5Unified.h> / <M5GFX.h>
- 保留必要的 app/storage/storage.h、settings/settings.h、app_init.h、app_state.h 等
- app_main() 执行顺序：
  1. 初始化 UART（调用 uart_init_default，或保持 debug_serial_init）
  2. 初始化 NVS：nvs_flash_init()
  3. 初始化 I2C（用于 AXP192）
  4. 初始化 SPI/LCD：hal_display_init()
  5. storage_init(), settings_load_from_storage()
  6. hal_system_init(), hal_input_init(), hal_power_key_init()
  7. boot_screen_show()
  8. app_init_ui(), app_init_managers(), xerintosh_init_core()
  9. 初始化内核子系统（kern_init, kern_vfs_init 等，原 deferred_kernel_init 内容）
 10. 创建 ui_task 和 wifi_mgr_task（xTaskCreatePinnedToCore）
 11. 进入主循环：调用 dev_ttyS0_poll, serial_monitor_update, wifi_mgr_process_requests, kern_port_preempt_consume, kern_sched_tick，并周期性栈画像
- 所有 Serial.println 改为 debug_printf 或 ESP_LOGI
- delay() → vTaskDelay(pdMS_TO_TICKS(ms))
- 删除 setup()/loop() 函数

改完后提交："feat(main): rewrite setup/loop as esp-idf app_main with FreeRTOS tasks"
返回修改摘要。`, {
    label: 'main',
    phase: 'Main-Cleanup'
  }),
  () => agent(`清理 ${target}/src/ 中剩余的 Arduino 头文件和 API 引用：

1. 扫描所有 src 文件，移除任何剩余的 #include <Arduino.h>。
2. 对于 kern_shell_cmds.c：
   - 移除 #include <Arduino.h>
   - 确认 cmd_date 已使用 esp_timer_get_time()
3. 对于 flasher_app.cpp：
   - 移除 #include <Arduino.h>
   - Serial.printf/flush/available/read/write 改为 uart_* 函数（可参考 dev_ttyS0 中的 UART0 输出）
   - delay(?) → vTaskDelay
4. 对于 app/flasher/flasher_menu.c 和 ui/ui_item_core.h：仅把注释中的 M5GFX 改为 "显示 HAL"，不修改代码逻辑。
5. 确保没有遗漏的 WiFi、String、Serial、M5、Preferences、HTTPClient 引用。

改完后提交："chore(cleanup): remove remaining Arduino includes and APIs"
返回修改摘要和未解决的引用列表。`, {
    label: 'cleanup',
    phase: 'Main-Cleanup'
  })
])

log(`主入口与清理完成，提交数 ${mainCleanup.filter(Boolean).length}`)

phase('Verify')
log('运行验证：native 测试与 esp-idf 构建')

const verify = await agent(`在 ${target} 中运行验证步骤：

1. 运行 native 测试：pio test -e native
   - 如果失败，记录前 5 个失败测试名称和错误摘要。
2. 运行 esp-idf 构建：pio run -e m5stick-c
   - 记录构建是否成功、前 10 个错误（如果有）。
3. 使用 grep 检查 src 中是否仍残留 Arduino/M5Unified/M5GFX/Preferences/HTTPClient/WiFi.h 等头文件或 API。
4. 运行 git status，确认是否有未提交修改；如果有，全部 add 并提交，消息 "chore(migration): final fixes after verification"。

返回：native 测试结果、esp-idf 构建结果、残留引用列表、最终提交哈希。`, {
  label: 'verify',
  phase: 'Verify',
  schema: {
    type: 'object',
    properties: {
      nativeTestResult: { type: 'string' },
      buildResult: { type: 'string' },
      remainingRefs: { type: 'array', items: { type: 'string' } },
      finalCommitHash: { type: 'string' }
    },
    required: ['nativeTestResult','buildResult','remainingRefs','finalCommitHash']
  }
})

return {
  skeleton,
  halBasic,
  corePlatform,
  gpioAdc,
  display,
  mainCleanup,
  verify
}