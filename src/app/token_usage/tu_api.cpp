#include "tu_api.h"
#include <string.h>
#include <stdlib.h>

#ifndef NATIVE_TEST

#include <esp_log.h>
#include <esp_http_client.h>
#include <cJSON.h>

#include "hal/hal_system.h"

bool tu_api_fetch_deepseek(const char *api_key, tu_deepseek_balance_t *out) {
    if (!api_key || api_key[0] == '\0' || !out) return false;

    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", api_key);

    esp_http_client_config_t config = {};
    config.url = "https://api.deepseek.com/user/balance";
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 5000;
    config.cert_pem = NULL;  /* 使用默认 CA 证书 */

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return false;

    esp_http_client_set_header(client, "Authorization", auth_header);

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return false;
    }

    int code = esp_http_client_get_status_code(client);
    vTaskDelay(pdMS_TO_TICKS(1));  /* yield 给调度器，防止看门狗超时 */
    if (code != 200) {
        esp_http_client_cleanup(client);
        return false;
    }

    int content_length = esp_http_client_get_content_length(client);
    if (content_length <= 0) {
        esp_http_client_cleanup(client);
        return false;
    }

    char *payload = (char *)malloc(content_length + 1);
    if (!payload) {
        esp_http_client_cleanup(client);
        return false;
    }

    int read_len = esp_http_client_read_response(client, payload, content_length);
    esp_http_client_cleanup(client);
    vTaskDelay(pdMS_TO_TICKS(1));

    if (read_len <= 0) {
        free(payload);
        return false;
    }
    payload[read_len] = '\0';

    cJSON *doc = cJSON_Parse(payload);
    free(payload);
    if (!doc) return false;

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
