#include "tu_api.h"
#include <string.h>

#ifndef NATIVE_TEST
#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

bool tu_api_fetch_deepseek(const char *api_key, tu_deepseek_balance_t *out) {
    if (!api_key || api_key[0] == '\0' || !out) return false;

    HTTPClient http;
    http.begin("https://api.deepseek.com/user/balance");
    http.addHeader("Authorization", String("Bearer ") + api_key);

    int code = http.GET();
    if (code != 200) {
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();

    JsonDocument doc;
    if (deserializeJson(doc, payload) != DeserializationOk) return false;

    out->is_available = doc["is_available"] | false;
    out->total_balance = doc["balance_infos"][0]["total_balance"] | 0.0f;
    out->granted_balance = doc["balance_infos"][0]["granted_balance"] | 0.0f;
    out->topped_up_balance = doc["balance_infos"][0]["topped_up_balance"] | 0.0f;

    return true;
}

bool tu_api_fetch_kimi(const char *api_key, tu_kimi_usage_t *out) {
    if (!api_key || api_key[0] == '\0' || !out) return false;

    HTTPClient http;
    http.begin("https://api.moonshot.cn/v1/usage");
    http.addHeader("Authorization", String("Bearer ") + api_key);

    int code = http.GET();
    if (code != 200) {
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();

    JsonDocument doc;
    if (deserializeJson(doc, payload) != DeserializationOk) return false;

    /* 解析今日用量 */
    JsonArray data = doc["data"];
    if (data.size() > 0) {
        out->daily_tokens = data[0]["total_tokens"] | 0.0f;
        out->rate_limit = data[0]["rate_limit"] | 0.0f;
        out->is_limited = out->rate_limit > 0 && out->daily_tokens >= out->rate_limit;
    }

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

bool tu_api_fetch_kimi(const char *api_key, tu_kimi_usage_t *out) {
    if (!api_key || !out) return false;
    out->daily_tokens = 12345.0f;
    out->rate_limit = 100000.0f;
    out->is_limited = false;
    return true;
}
#endif

void tu_data_init(tu_data_t *data) {
    if (!data) return;
    memset(data, 0, sizeof(tu_data_t));
}
