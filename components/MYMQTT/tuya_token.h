#ifndef __TUYA_TOKEN_H
#define __TUYA_TOKEN_H
#include <stdint.h>
#include "esp_err.h"

/* ── Tuya 平台凭证 ── */
#define TUYA_PRODUCT_ID     "akne6fuu7uo6ki50"
#define TUYA_DEVICE_ID      "2610cf838a3c50c5b3gn0n"
#define TUYA_DEVICE_SECRET  "hP509E5hvCXLemM4"

/* ── MQTT 连接参数 ── */
#define TUYA_MQTT_BROKER    "mqtts://m1.tuyacn.com"
#define TUYA_MQTT_PORT      8883

/**
 * @brief 生成 Tuya MQTT 鉴权凭证（一机一密，见 key.md）
 *
 * 签名原文: deviceId={did},timestamp={ts},secureMode=1,accessType=1
 * 签名算法: HMAC-SHA256(DeviceSecret, 签名原文) → 64 字符小写 hex
 *
 * client_id = tuyalink_{did}（官方 TuyaLink MQTT 标准格式）
 * username   = {did}|signMethod=hmacSha256,timestamp={ts},secureMode=1,accessType=1
 * password   = 签名 hex 字符串（非空！）
 *
 * @param cid_out  client_id 输出缓冲区
 * @param user_out username 输出缓冲区
 * @param pwd_out  password 输出缓冲区（>= 65 字节）
 * @param buf_len  每个缓冲区大小
 */
esp_err_t tuya_mqtt_credential_generate(char *cid_out,
                                         char *user_out,
                                         char *pwd_out,
                                         size_t buf_len);

/**
 * @brief HMAC-SHA256 → 64 字符小写 hex（左补零）
 */
void tuya_hmac_sha256_hex(const unsigned char *key, size_t key_len,
                           const unsigned char *content, size_t content_len,
                           char *hex_out);

#endif /* __TUYA_TOKEN_H */
