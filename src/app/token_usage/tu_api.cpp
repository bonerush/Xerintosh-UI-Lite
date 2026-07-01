#include "tu_api.h"
#include <string.h>
#include <stdlib.h>

#ifndef NATIVE_TEST

#include <esp_log.h>
#include <esp_http_client.h>
#include <cJSON.h>

#include "hal/hal_system.h"
#include "kernel/kern_task.h"
#include "kernel/kern_kmalloc.h"

#define TAG "tu_api"

/* ═══ 日志统一宏 ═══
 * 所有 HTTP 通信日志使用 I 级别，
 * 确保在不开启 DEBUG 级别时也能看到完整调用链 */
#define TU_LOGD(...) ESP_LOGI(TAG, __VA_ARGS__)
#define TU_LOGE(...) ESP_LOGE(TAG, __VA_ARGS__)
#define TU_LOGW(...) ESP_LOGW(TAG, __VA_ARGS__)

/* ═══ DigiCert Global Root G2 固定根 CA 证书 ═══
 *  用于替代 esp_crt_bundle_attach（压缩证书包），因为
 *  PSA 加密层在部分 ESP32 芯片上无法验证该证书链的签名。
 *
 *  证书链：
 *    api.deepseek.com (RSA 2048/SHA-256)
 *      └─ TrustAsia DV TLS RSA CA 2025 (RSA 4096/SHA-256)
 *           └─ DigiCert Global Root G2 (自签名) ← 本 PEM
 */
static const char *digicert_root_g2_pem =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIDjjCCAnagAwIBAgIQAzrx5qcRqaC7KGSxHQn65TANBgkqhkiG9w0BAQsFADBh\n"
    "MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3\n"
    "d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH\n"
    "MjAeFw0xMzA4MDExMjAwMDBaFw0zODAxMTUxMjAwMDBaMGExCzAJBgNVBAYTAlVT\n"
    "MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j\n"
    "b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IEcyMIIBIjANBgkqhkiG\n"
    "9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuzfNNNx7a8myaJCtSnX/RrohCgiN9RlUyfuI\n"
    "2/Ou8jqJkTx65qsGGmvPrC3oXgkkRLpimn7Wo6h+4FR1IAWsULecYxpsMNzaHxmx\n"
    "1x7e/dfgy5SDN67sH0NO3Xss0r0upS/kqbitOtSZpLYl6ZtrAGCSYP9PIUkY92eQ\n"
    "q2EGnI/yuum06ZIya7XzV+hdG82MHauVBJVJ8zUtluNJbd134/tJS7SsVQepj5Wz\n"
    "tCO7TG1F8PapspUwtP1MVYwnSlcUfIKdzXOS0xZKBgyMUNGPHgm+F6HmIcr9g+UQ\n"
    "vIOlCsRnKPZzFBQ9RnbDhxSJITRNrw9FDKZJobq7nMWxM4MphQIDAQABo0IwQDAP\n"
    "BgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBhjAdBgNVHQ4EFgQUTiJUIBiV\n"
    "5uNu5g/6+rkS7QYXjzkwDQYJKoZIhvcNAQELBQADggEBAGBnKJRvDkhj6zHd6mcY\n"
    "1Yl9PMWLSn/pvtsrF9+wX3N3KjITOYFnQoQj8kVnNeyIv/iPsGEMNKSuIEyExtv4\n"
    "NeF22d+mQrvHRAiGfzZ0JFrabA0UWTW98kndth/Jsw1HKj2ZL7tcu7XUIOGZX1NG\n"
    "Fdtom/DzMNU+MeKNhJ7jitralj41E6Vf8PlwUHBHQRFXGU7Aj64GxJUTFy8bJZ91\n"
    "8rGOmaFvE7FBcf6IKshPECBV1/MUReXgRPTqh5Uykw7+U0b6LJ3/iyK5S9kJRaTe\n"
    "pLiaWN0bfVKfjllDiIGknibVb63dDcY3fe0Dkhvld1927jyNxF1WW6LZZm6zNTfl\n"
    "MrY=\n"
    "-----END CERTIFICATE-----\n";

bool tu_api_fetch_deepseek(const char *api_key, tu_deepseek_balance_t *out) {
    if (!api_key || api_key[0] == '\0' || !out) {
        TU_LOGE("[INIT] 参数无效: api_key=%p key_empty=%d out=%p",
                 (void*)api_key, api_key ? (api_key[0] == '\0') : -1, (void*)out);
        return false;
    }

    /* ── 步骤 1：构建认证头部 ── */
    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", api_key);
    TU_LOGD("[1/7] 认证头已构建 (len=%zu, key_len=%zu)",
             strlen(auth_header), strlen(api_key));

    /* ── 步骤 2：配置 HTTP 客户端 ── */
    TU_LOGD("[2/7] 配置 esp_http_client: url=https://api.deepseek.com/user/balance, "
             "method=GET, timeout=%dms", 5000);

    esp_http_client_config_t config = {};
    config.url = "https://api.deepseek.com/user/balance";
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 5000;
    config.cert_pem = digicert_root_g2_pem;  /* 使用固定根 CA 证书（替代 esp_crt_bundle_attach） */
    TU_LOGD("[2/7] TLS 验证: 使用固定 CA 证书 (DigiCert Global Root G2, PEM=%zu bytes)",
             strlen(digicert_root_g2_pem));
    TU_LOGD("[2/7] TLS 缓冲区: IN=%d OUT=%d (sdkconfig, DYNAMIC_BUFFER=y)",
             CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN, CONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN);

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        TU_LOGE("[2/7] esp_http_client_init 失败！可能原因为: 内存不足或 TLS 配置错误");
        return false;
    }
    TU_LOGD("[2/7] esp_http_client_init = OK (handle=%p)", (void*)client);

    /* ── 步骤 3：设置 Authorization 头部 ── */
    esp_err_t hdr_err = esp_http_client_set_header(client, "Authorization", auth_header);
    TU_LOGD("[3/7] 设置 Authorization 头: %s", hdr_err == ESP_OK ? "OK" : "FAILED");

    /* ── 步骤 4：打开连接（DNS 解析 + TCP 连接 + TLS 握手） ──
     *  这是最关键的边界。内部时序：
     *  1) lwip DNS 解析 api.deepseek.com
     *  2) TCP 连接 remote IP:443
     *  3) TLS 1.2/1.3 握手 + 证书验证
     *  4) 发送 HTTP GET 请求
     *  整个步骤受 config.timeout_ms (5s) 约束 */

    /* ── 内存快照：TLS 分配前 ── */
    {
        kern_kmem_stat_t mem;
        kern_kmem_get_stats(&mem);
        TU_LOGD("[4/7] [内存] BEFORE esp_http_client_open: "
                 "free=%lu  largest_block=%lu  min_free=%lu  frag=%lu%%",
                 (unsigned long)mem.free_bytes,
                 (unsigned long)mem.largest_free_block,
                 (unsigned long)mem.min_free_bytes,
                 (unsigned long)mem.fragmentation_percent);
    }

    TU_LOGD("[4/7] esp_http_client_open() → 开始 DNS/TCP/TLS...");

    uint32_t t_open_start = hal_get_ticks();
    esp_err_t err = esp_http_client_open(client, 0);
    uint32_t t_open_elapsed = hal_get_ticks() - t_open_start;
    kern_sleep_ms(1);

    /* ── 内存快照：TLS 分配后 ── */
    {
        kern_kmem_stat_t mem;
        kern_kmem_get_stats(&mem);
        TU_LOGD("[4/7] [内存] AFTER esp_http_client_open: "
                 "free=%lu  largest_block=%lu  min_free=%lu  frag=%lu%%",
                 (unsigned long)mem.free_bytes,
                 (unsigned long)mem.largest_free_block,
                 (unsigned long)mem.min_free_bytes,
                 (unsigned long)mem.fragmentation_percent);
    }

    if (err != ESP_OK) {
        TU_LOGE("[4/7] ⚠️ HTTP open 失败！耗时=%lums, err=%d(%s)",
                 (unsigned long)t_open_elapsed, err, esp_err_to_name(err));

        /* ── 错误码详细诊断 ── */
        switch (err) {
        case ESP_ERR_HTTP_CONNECT:
            TU_LOGE("[4/7] 诊断: TCP 连接被拒或超时");
            TU_LOGE("[4/7]   → 检查: ① 网关可达 ② 无防火墙 ③ 443 端口开放");
            /* ESP_ERR_HTTP_CONNECT 也可能在 TLS 握手失败时返回（非真正 TCP 问题）
             *  如果耗时 <500ms，则 TCP 实际已连接，失败发生在 TLS 层 */
            if (t_open_elapsed < 500) {
                TU_LOGE("[4/7] ⚠️ 注意：耗时仅 %lums，TCP 很可能已连接成功",
                         (unsigned long)t_open_elapsed);
                TU_LOGE("[4/7]   → 请检查上方 esp-tls-mbedtls 的日志");
            }
            break;
        case ESP_ERR_HTTP_CONNECTING:
            TU_LOGE("[4/7] 诊断: 连接未完成（可能 DNS 解析失败或 TCP SYNC 无响应）");
            TU_LOGE("[4/7]   → 请检查: ① DNS 能否解析 api.deepseek.com；"
                     "② 网络能访问外网");
            break;
        case ESP_ERR_HTTP_READ_TIMEOUT:
            TU_LOGE("[4/7] 诊断: 5s 超时 — DNS/TCP/TLS 超过时限");
            TU_LOGE("[4/7]   → 请检查: ① DNS 解析是否正常；"
                     "② 网络能访问外网；③ 无防火墙拦截 443 端口");
            break;
        case ESP_ERR_HTTP_INVALID_TRANSPORT:
            TU_LOGE("[4/7] 诊断: 无效传输层（URL 格式或协议错误）");
            break;
        case ESP_ERR_HTTP_WRITE_DATA:
            TU_LOGE("[4/7] 诊断: HTTP 请求发送失败（TCP 连接可能中断）");
            break;
        case ESP_ERR_HTTP_EAGAIN:
            TU_LOGE("[4/7] 诊断: 资源暂时不可用（EAGAIN），可能网络缓冲区满");
            break;
        case ESP_ERR_HTTP_CONNECTION_CLOSED:
            TU_LOGE("[4/7] 诊断: 服务端提前关闭连接");
            break;
        default:
            TU_LOGE("[4/7] 诊断: 未知错误码=%d，查阅 ESP-IDF esp_http_client.h", err);
            break;
        }

        /* ── TLS 握手失败诊断 ──
         *  使用固定 CA 证书 (DigiCert Global Root G2) 进行验证。
         *  如果仍然失败，可能原因：
         *    a) DeepSeek 更换了证书链（不同根 CA）
         *    b) 固定证书内容损坏或格式错误
         *    c) 服务端 TLS 配置变更 */
        TU_LOGE("[4/7] [TLS] ⚠️ 证书验证失败（使用固定 CA: DigiCert Global Root G2）");
        TU_LOGE("[4/7] [TLS]   请检查: ① DeepSeek 证书链是否变更  "
                 "(openssl s_client -connect api.deepseek.com:443)");
        TU_LOGE("[4/7] [TLS]   ② 查看上方 esp-tls-mbedtls 的错误信息");

        esp_http_client_cleanup(client);
        return false;
    }

    /* ── 步骤 5：读取响应头部 ── */
    TU_LOGD("[5/7] esp_http_client_fetch_headers()...");
    uint32_t t_hdr_start = hal_get_ticks();
    int content_length = esp_http_client_fetch_headers(client);
    uint32_t t_hdr_elapsed = hal_get_ticks() - t_hdr_start;
    kern_sleep_ms(1);

    int code = esp_http_client_get_status_code(client);
    TU_LOGD("[5/7] 头部读取完成: status=%d, Content-Length=%d, 耗时=%lums",
             code, content_length, (unsigned long)t_hdr_elapsed);

    if (code != 200) {
        TU_LOGW("[5/7] ⚠️ 非 200 响应: status=%d", code);
        if (code == 401) {
            TU_LOGE("[5/7]   → HTTP 401 Unauthorized: API key 无效或已过期，请检查 'dskey'");
        } else if (code == 403) {
            TU_LOGE("[5/7]   → HTTP 403 Forbidden: API key 无权限");
        } else if (code == 429) {
            TU_LOGE("[5/7]   → HTTP 429 Rate Limited: 请求过于频繁");
        } else if (code >= 500) {
            TU_LOGE("[5/7]   → HTTP %d: DeepSeek 服务端错误", code);
        }
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    /* ── 步骤 6：读取响应体 ── */
    if (content_length <= 0) {
        content_length = 2048; /* 分块传输回退 */
        TU_LOGD("[6/7] Content-Length 不可用，回退到 %d 字节缓冲区", content_length);
    }

    char *payload = (char *)malloc(content_length + 1);
    if (!payload) {
        TU_LOGE("[6/7] malloc(%d) 失败（内存不足）", content_length + 1);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }
    TU_LOGD("[6/7] 缓冲区已分配: %d bytes", content_length + 1);

    uint32_t t_read_start = hal_get_ticks();
    int total_read = 0;
    int chunk_count = 0;
    while (total_read < content_length) {
        int ret = esp_http_client_read(client, payload + total_read,
                                       content_length - total_read);
        if (ret < 0) {
            TU_LOGE("[6/7] 读取失败: chunk=%d, ret=%d, total_read=%d",
                     chunk_count, ret, total_read);
            break;
        }
        if (ret == 0) break;  /* 连接关闭 */
        total_read += ret;
        chunk_count++;
        TU_LOGD("[6/7]   chunk #%d: read %d bytes (累积 %d/%d)",
                 chunk_count, ret, total_read, content_length);
    }
    uint32_t t_read_elapsed = hal_get_ticks() - t_read_start;
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    kern_sleep_ms(1);

    if (total_read <= 0) {
        TU_LOGE("[6/7] 响应体读取失败: total_read=%d, chunks=%d, 耗时=%lums",
                 total_read, chunk_count, (unsigned long)t_read_elapsed);
        free(payload);
        return false;
    }
    payload[total_read] = '\0';
    TU_LOGD("[6/7] 响应体读取完成: total=%d bytes, chunks=%d, 耗时=%lums",
             total_read, chunk_count, (unsigned long)t_read_elapsed);
    TU_LOGD("[6/7] 原始响应: [%s]", payload);

    /* ── 步骤 7：解析 JSON ── */
    TU_LOGD("[7/7] cJSON_Parse()...");
    cJSON *doc = cJSON_Parse(payload);
    if (!doc) {
        const char *err_ptr = cJSON_GetErrorPtr();
        TU_LOGE("[7/7] JSON 解析失败! 错误位置(约): %s", err_ptr ? err_ptr : "unknown");
        TU_LOGE("[7/7]   raw=[%.160s]", payload);  /* 只显示前 160 字符避免刷屏 */
        free(payload);
        return false;
    }
    free(payload);

    /* ── 提取字段 ── */
    cJSON *is_available = cJSON_GetObjectItem(doc, "is_available");
    out->is_available = cJSON_IsBool(is_available) ? cJSON_IsTrue(is_available) : false;

    cJSON *balance_infos = cJSON_GetObjectItem(doc, "balance_infos");
    int binfo_count = cJSON_GetArraySize(balance_infos);
    TU_LOGD("[7/7] balance_infos: %s (%d items)",
             cJSON_IsArray(balance_infos) ? "array" : "NOT array", binfo_count);

    if (cJSON_IsArray(balance_infos) && binfo_count > 0) {
        cJSON *info = cJSON_GetArrayItem(balance_infos, 0);
        if (cJSON_IsObject(info)) {
            cJSON *total = cJSON_GetObjectItem(info, "total_balance");
            cJSON *granted = cJSON_GetObjectItem(info, "granted_balance");
            cJSON *topped = cJSON_GetObjectItem(info, "topped_up_balance");

            out->total_balance = cJSON_IsString(total) ? (float)atof(total->valuestring) : 0.0f;
            out->granted_balance = cJSON_IsString(granted) ? (float)atof(granted->valuestring) : 0.0f;
            out->topped_up_balance = cJSON_IsString(topped) ? (float)atof(topped->valuestring) : 0.0f;

            TU_LOGD("[7/7] total_balance=%s, granted_balance=%s, topped_up_balance=%s",
                     cJSON_IsString(total) ? total->valuestring : "(null)",
                     cJSON_IsString(granted) ? granted->valuestring : "(null)",
                     cJSON_IsString(topped) ? topped->valuestring : "(null)");
        } else {
            TU_LOGW("[7/7] balance_infos[0] 不是对象");
        }
    } else {
        TU_LOGW("[7/7] balance_infos 缺失或为空，尝试直接读取顶层字段...");
        /* 回退：部分 API 版本可能直接返回顶层字段 */
        cJSON *total_balance = cJSON_GetObjectItem(doc, "total_balance");
        if (cJSON_IsNumber(total_balance)) {
            out->total_balance = (float)cJSON_GetNumberValue(total_balance);
            TU_LOGD("[7/7] 顶层 total_balance=%.2f", (double)out->total_balance);
        }
    }

    TU_LOGD("[7/7] ✅ 完整结果: balance=%.2f CNY, available=%d",
             (double)out->total_balance, out->is_available);
    TU_LOGD("[7/7] ✅ TLS 验证通过 (cert_pem=DigiCert Global Root G2)");

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
