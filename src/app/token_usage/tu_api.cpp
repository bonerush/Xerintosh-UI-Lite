#include "tu_api.h"
#include <string.h>
#include <stdlib.h>

#ifndef NATIVE_TEST

#include <esp_log.h>
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <cJSON.h>

#include "hal/hal_system.h"
#include "kernel/kern_task.h"

#define TAG "tu_api"

bool tu_api_fetch_deepseek(const char *api_key, tu_deepseek_balance_t *out) {
    if (!api_key || api_key[0] == '\0' || !out) return false;

    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", api_key);

    esp_http_client_config_t config = {};
    config.url = "https://api.deepseek.com/user/balance";
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 5000;
    config.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "esp_http_client_init failed");
        return false;
    }

    esp_http_client_set_header(client, "Authorization", auth_header);

    /* 使用 open/fetch_headers/read 模式替代 perform，
       确保响应体可以被完整读取（perform 在某些 ESP-IDF 版本中会消费掉 body） */
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP open failed: %d (%s)", err, esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return false;
    }

    int content_length = esp_http_client_fetch_headers(client);
    int code = esp_http_client_get_status_code(client);
    kern_sleep_ms(1);
    ESP_LOGI(TAG, "HTTP status: %d, Content-Length: %d", code, content_length);
    if (code != 200) {
        ESP_LOGW(TAG, "Non-200 response: %d", code);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    if (content_length <= 0) {
        content_length = 2048; /* 分块传输回退 */
    }

    char *payload = (char *)malloc(content_length + 1);
    if (!payload) {
        ESP_LOGE(TAG, "malloc(%d) failed", content_length + 1);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    int total_read = 0;
    while (total_read < content_length) {
        int ret = esp_http_client_read(client, payload + total_read,
                                       content_length - total_read);
        if (ret <= 0) break;
        total_read += ret;
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    kern_sleep_ms(1);

    if (total_read <= 0) {
        ESP_LOGE(TAG, "read response failed: total_read=%d", total_read);
        free(payload);
        return false;
    }
    payload[total_read] = '\0';
    ESP_LOGI(TAG, "Response (%d bytes): %s", total_read, payload);

    cJSON *doc = cJSON_Parse(payload);
    free(payload);
    if (!doc) {
        ESP_LOGE(TAG, "JSON parse failed");
        return false;
    }

    cJSON *is_available = cJSON_GetObjectItem(doc, "is_available");
    out->is_available = cJSON_IsBool(is_available) ? cJSON_IsTrue(is_available) : false;

    cJSON *balance_infos = cJSON_GetObjectItem(doc, "balance_infos");
    if (cJSON_IsArray(balance_infos) && cJSON_GetArraySize(balance_infos) > 0) {
        cJSON *info = cJSON_GetArrayItem(balance_infos, 0);
        if (cJSON_IsObject(info)) {
            cJSON *total = cJSON_GetObjectItem(info, "total_balance");
            cJSON *granted = cJSON_GetObjectItem(info, "granted_balance");
            cJSON *topped = cJSON_GetObjectItem(info, "topped_up_balance");

            out->total_balance = cJSON_IsString(total) ? (float)atof(total->valuestring) : 0.0f;
            out->granted_balance = cJSON_IsString(granted) ? (float)atof(granted->valuestring) : 0.0f;
            out->topped_up_balance = cJSON_IsString(topped) ? (float)atof(topped->valuestring) : 0.0f;
        }
    }

    ESP_LOGI(TAG, "Balance: %.2f CNY, available: %d",
             (double)out->total_balance, out->is_available);

    cJSON_Delete(doc);
    return true;
}

#else
/* Native stubs */
bool tu_api_fetch_deepseek(const char *api_key, tu_deepseek_balance_t *out) {
    if (!api_key || !out) return false;
    out->total_balance = 4.95f;
    out->granted_balance = 0.0f;
    out->topped_up_balance = 4.95f;
    out->is_available = true;
    return true;
}
#endif

void tu_data_init(tu_data_t *data) {
    if (!data) return;
    memset(data, 0, sizeof(tu_data_t));
}
