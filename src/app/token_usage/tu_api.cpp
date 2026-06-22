#include "tu_api.h"
#include <string.h>

#ifndef NATIVE_TEST
#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

bool tu_api_fetch_deepseek(const char *api_key, tu_deepseek_balance_t *out) {
    if (!api_key || api_key[0] == '\0' || !out) return false;

    HTTPClient http;
    http.setConnectTimeout(3000);
    http.setTimeout(5000);
    http.begin("https://api.deepseek.com/user/balance");
    http.addHeader("Authorization", String("Bearer ") + api_key);

    int code = http.GET();
    delay(1);  /* yield 给调度器，防止看门狗超时 */
    if (code != 200) {
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();
    delay(1);

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) return false;

    out->is_available = doc["is_available"] | false;
    /* DeepSeek 返回的 balance 值是字符串，需要显式转换 */
    out->total_balance = (float)atof(doc["balance_infos"][0]["total_balance"] | "0");
    out->granted_balance = (float)atof(doc["balance_infos"][0]["granted_balance"] | "0");
    out->topped_up_balance = (float)atof(doc["balance_infos"][0]["topped_up_balance"] | "0");

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
