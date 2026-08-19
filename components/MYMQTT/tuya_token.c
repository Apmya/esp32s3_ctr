#include "tuya_token.h"
#include "mbedtls/md.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>
#include <sys/time.h>

static const char *TAG = "tuya_token";

/* ── HMAC-SHA256 → 64 字符小写 hex ── */
void tuya_hmac_sha256_hex(const unsigned char *key, size_t key_len,
                           const unsigned char *content, size_t content_len,
                           char *hex_out)
{
    unsigned char raw[32] = {0};

    mbedtls_md_context_t ctx;
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);

    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, info, 1);
    mbedtls_md_hmac_starts(&ctx, key, key_len);
    mbedtls_md_hmac_update(&ctx, content, content_len);
    mbedtls_md_hmac_finish(&ctx, raw);
    mbedtls_md_free(&ctx);

    for (int i = 0; i < 32; i++) {
        sprintf(hex_out + i * 2, "%02x", raw[i]);
    }
    hex_out[64] = '\0';
}

/* ── 一机一密凭证生成（涂鸦官方标准 + 官方 Java 示例格式） ── */
esp_err_t tuya_mqtt_credential_generate(char *cid_out,
                                         char *user_out,
                                         char *pwd_out,
                                         size_t buf_len)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    uint32_t ts = (uint32_t)tv.tv_sec;
    if (ts < 1000000000) ts = 1700000000;

    /* ── 1. 签名原文：逗号分隔 + accessType=1 ── */
    char content[160];
    snprintf(content, sizeof(content),
             "deviceId=%s,timestamp=%lu,secureMode=1,accessType=1",
             TUYA_DEVICE_ID, (unsigned long)ts);

    /* ── 2. HMAC-SHA256 签名 → password ── */
    tuya_hmac_sha256_hex((const unsigned char *)TUYA_DEVICE_SECRET,
                          strlen(TUYA_DEVICE_SECRET),
                          (const unsigned char *)content,
                          strlen(content),
                          pwd_out);

    ESP_LOGI(TAG, "签名原文: %s", content);
    ESP_LOGI(TAG, "签名结果: %s", pwd_out);

    /* ── 3. client_id = tuyalink_{deviceId}（官方 TuyaLink MQTT 标准格式） ── */
    snprintf(cid_out, buf_len, "tuyalink_%s", TUYA_DEVICE_ID);

    /* ── 4. username（不含 password 字段） ── */
    snprintf(user_out, buf_len,
             "%s|signMethod=hmacSha256,timestamp=%lu,secureMode=1,accessType=1",
             TUYA_DEVICE_ID, (unsigned long)ts);

    ESP_LOGI(TAG, "ClientID: %s", cid_out);
    ESP_LOGI(TAG, "Username: %s", user_out);

    return ESP_OK;
}
