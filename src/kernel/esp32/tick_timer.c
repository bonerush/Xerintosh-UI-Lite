/**
 * @file tick_timer.c
 * @brief ESP32 硬件定时器实现 — 基于 GPTimer 的抢占式调度 tick
 *
 * 使用 ESP32 GPTimer 实现周期性 tick 中断。
 *
 * ═══ ISR 安全原则 ═══
 * 本 ISR 仅执行最小化工作：原子设置抢占标志。
 * 所有调度逻辑在任务上下文中通过 tick_timer_consume() 触发。
 * 严禁在 ISR 中调用阻塞操作、malloc/free 或非 ISR-safe API。
 *
 * @note 此模块不依赖 FreeRTOS，可被原生调度器直接使用。
 */

#ifndef NATIVE_TEST

#include "tick_timer.h"

#include <driver/gptimer.h>
#include <esp_attr.h>
#include <esp_log.h>

static const char *TAG = "tick_timer";

/* ═══ 内部状态 ═══ */

static volatile bool g_tick_pending[2] = {false, false};  /* ISR → 任务上下文通知标志（per-CPU） */
static volatile bool g_timer_running = false;
static gptimer_handle_t g_gptimer = NULL;
static uint32_t g_period_us = 1000;             /* 初始化时保存的周期，用于 tickless 唤醒后恢复 */

/* ═══ ISR ═══ */

/**
 * @brief GPTimer 报警回调（ISR 上下文）
 *
 * 由硬件定时器自动触发。仅设置 tick 标志。
 *
 * @return false 表示不唤醒高优先级任务（调度逻辑在任务上下文处理）
 */
static bool IRAM_ATTR tick_timer_alarm_isr(gptimer_handle_t timer,
                                            const gptimer_alarm_event_data_t *edata,
                                            void *user_data)
{
    (void)timer;
    (void)edata;
    (void)user_data;

    g_tick_pending[0] = true;
    g_tick_pending[1] = true;
    return false;  /* 不唤醒高优先级任务 */
}

/* ═══ 公共 API ═══ */

int tick_timer_init(uint32_t period_us)
{
    if (g_timer_running) {
        ESP_LOGW(TAG, "timer already running, stop first");
        return -1;
    }

    g_tick_pending[0] = false;
    g_tick_pending[1] = false;

    g_period_us = period_us;

    /* 配置 GPTimer：1MHz 时钟（1 tick = 1us），向上计数 */
    gptimer_config_t config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000,  /* 1MHz */
    };

    esp_err_t err = gptimer_new_timer(&config, &g_gptimer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gptimer_new_timer failed: %s", esp_err_to_name(err));
        return -1;
    }

    /* 配置报警：自动重装，周期 = period_us */
    gptimer_alarm_config_t alarm_config = {
        .alarm_count = period_us,
        .reload_count = 0,
        .flags.auto_reload_on_alarm = true,
    };

    err = gptimer_set_alarm_action(g_gptimer, &alarm_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_alarm_action failed: %s", esp_err_to_name(err));
        gptimer_del_timer(g_gptimer);
        g_gptimer = NULL;
        return -1;
    }

    /* 注册 ISR 回调 */
    gptimer_event_callbacks_t cbs = {
        .on_alarm = tick_timer_alarm_isr,
    };

    err = gptimer_register_event_callbacks(g_gptimer, &cbs, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register_callbacks failed: %s", esp_err_to_name(err));
        gptimer_del_timer(g_gptimer);
        g_gptimer = NULL;
        return -1;
    }

    /* 使能定时器 */
    err = gptimer_enable(g_gptimer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gptimer_enable failed: %s", esp_err_to_name(err));
        gptimer_del_timer(g_gptimer);
        g_gptimer = NULL;
        return -1;
    }

    ESP_LOGI(TAG, "timer initialized: period=%u us", period_us);
    return 0;
}

int tick_timer_start(void)
{
    if (g_timer_running) return 0;
    if (g_gptimer == NULL) return -1;

    g_tick_pending[0] = false;
    g_tick_pending[1] = false;

    esp_err_t err = gptimer_start(g_gptimer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gptimer_start failed: %s", esp_err_to_name(err));
        return -1;
    }

    g_timer_running = true;
    ESP_LOGI(TAG, "timer started");
    return 0;
}

void tick_timer_stop(void)
{
    if (!g_timer_running || g_gptimer == NULL) return;

    gptimer_stop(g_gptimer);
    g_timer_running = false;
    g_tick_pending[0] = false;
    g_tick_pending[1] = false;
    ESP_LOGI(TAG, "timer stopped");
}

bool tick_timer_pending(uint8_t cpu_id)
{
    if (cpu_id >= 2) return false;
    return g_tick_pending[cpu_id];
}

bool tick_timer_consume(uint8_t cpu_id)
{
    if (cpu_id >= 2) return false;
    if (!g_tick_pending[cpu_id]) return false;
    g_tick_pending[cpu_id] = false;
    return true;
}

bool tick_timer_is_running(void)
{
    return g_timer_running;
}

void tick_timer_set_next_alarm(uint32_t us)
{
    if (!g_timer_running || g_gptimer == NULL) return;
    if (us == 0) us = g_period_us;

    uint64_t now = 0;
    gptimer_get_raw_count(g_gptimer, &now);

    gptimer_alarm_config_t alarm_config = {
        .alarm_count = now + us,
        .reload_count = 0,
        .flags.auto_reload_on_alarm = false,
    };

    esp_err_t err = gptimer_set_alarm_action(g_gptimer, &alarm_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_next_alarm failed: %s", esp_err_to_name(err));
    }
}

void tick_timer_restore_periodic(void)
{
    if (!g_timer_running || g_gptimer == NULL) return;

    /*
     * 先停止 GPTimer 再重新配置，避免以下竞态：
     *   tick_timer_restore_periodic() 调用 gptimer_set_alarm_action()
     *   释放内部自旋锁后中断恢复，GPTimer ISR 立即触发，
     *   此时 GPTimer 的 group 自旋锁处于边界状态，
     *   ISR 中 vPortExitCritical 读取 spinlock->owner 时
     *   可能触发 LoadStoreError（EXCVADDR=0x00000000）。
     *
     * 先停止计数 → 配置 → 重新启动，确保 ISR 在配置期间不会触发。
     */
    gptimer_stop(g_gptimer);

    gptimer_alarm_config_t alarm_config = {
        .alarm_count = g_period_us,
        .reload_count = 0,
        .flags.auto_reload_on_alarm = true,
    };

    esp_err_t err = gptimer_set_alarm_action(g_gptimer, &alarm_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "restore_periodic failed: %s", esp_err_to_name(err));
    }

    gptimer_start(g_gptimer);
}

#endif /* !NATIVE_TEST */
