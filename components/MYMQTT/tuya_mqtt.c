#include "mqtt_client.h"
#include "esp_log.h"
#include "esp_crt_bundle.h"
#include "esp_sntp.h"
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "tuya_mqtt.h"
#include "tuya_dm.h"
#include "tuya_token.h"

#define TAG  "tuya_mqtt"

static esp_mqtt_client_handle_t mqtt_handle = NULL;
static bool sntp_inited = false;
bool tuya_mqtt_is_connected = false;

/* ── 前向声明 ── */
static void tuya_subscribe(void);
static void tuya_property_set_response(const char *msgId, int code);

/* ── 毫秒时间戳 ── */
static int64_t ts_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

/* ════════════════════════════════════════════════════════════
 * SNTP 初始化
 * ════════════════════════════════════════════════════════════ */
static void sntp_init_once(void)
{
    if (sntp_inited) return;
    sntp_inited = true;
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "ntp.aliyun.com");
    esp_sntp_setservername(1, "pool.ntp.org");
    esp_sntp_init();
    ESP_LOGI(TAG, "SNTP 已启动");
}

/* ════════════════════════════════════════════════════════════
 * MQTT 事件回调
 * ════════════════════════════════════════════════════════════ */
static void tuya_mqtt_event_handler(void *handler_args,
                                     esp_event_base_t base,
                                     int32_t event_id,
                                     void *event_data)
{
    esp_mqtt_event_handle_t e = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {

    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT 已连接");
        tuya_mqtt_is_connected = true;
        tuya_subscribe();

        /* 上电自动上报一次属性 */
        {
            cJSON *js = tuya_property_upload();
            if (js) {
                char *p = cJSON_PrintUnformatted(js);
                tuya_post_property_data(p);
                cJSON_free(p);
                cJSON_Delete(js);
            }
        }
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT 已断开");
        tuya_mqtt_is_connected = false;
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "已订阅 msg_id=%d", e->msg_id);
        break;

    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "← TOPIC: %.*s",  e->topic_len, e->topic);
        ESP_LOGI(TAG, "← DATA:  %.*s",  e->data_len,  e->data);

        /* ── 属性下发: thing/property/set ── */
        if (strstr(e->topic, "property/set")) {
            cJSON *root = cJSON_Parse(e->data);
            if (!root) { ESP_LOGW(TAG, "set JSON 解析失败"); break; }

            cJSON *data_obj = cJSON_GetObjectItem(root, "data");
            cJSON *msgId_js = cJSON_GetObjectItem(root, "msgId");
            const char *msgId = msgId_js ? cJSON_GetStringValue(msgId_js) : "";

            if (data_obj) {
                tuya_property_handle(data_obj);
            }

            tuya_property_set_response(msgId, 0);
            cJSON_Delete(root);
        }

        /* ── 动作执行: thing/action/execute ── */
        if (strstr(e->topic, "action/execute")) {
            cJSON *root = cJSON_Parse(e->data);
            if (!root) { ESP_LOGW(TAG, "action JSON 解析失败"); break; }

            cJSON *data_obj  = cJSON_GetObjectItem(root, "data");
            cJSON *msgId_js  = cJSON_GetObjectItem(root, "msgId");
            const char *msgId = msgId_js ? cJSON_GetStringValue(msgId_js) : "";

            if (data_obj) {
                cJSON *code   = cJSON_GetObjectItem(data_obj, "actionCode");
                cJSON *params = cJSON_GetObjectItem(data_obj, "inputParams");

                if (code && strcmp(cJSON_GetStringValue(code), "measure") == 0) {
                    tuya_action_handle(msgId, params);
                } else {
                    ESP_LOGW(TAG, "未知动作: %s",
                             code ? cJSON_GetStringValue(code) : "NULL");
                }
            }
            cJSON_Delete(root);
        }
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT 错误");
        if (e->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            ESP_LOGE(TAG, "TLS: %s",
                     strerror(e->error_handle->esp_tls_last_esp_err));
        }
        if (e->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
            ESP_LOGE(TAG, "拒绝码: %d", e->error_handle->connect_return_code);
        }
        break;

    default:
        break;
    }
}

/* ════════════════════════════════════════════════════════════
 * 启动 MQTT
 * ════════════════════════════════════════════════════════════ */
esp_err_t tuya_start(void)
{
    sntp_init_once();

    for (int i = 0; i < 50; i++) {
        if (time(NULL) > 1000000000) break;
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    char client_id[192], username[192], password[65];
    tuya_mqtt_credential_generate(client_id, username, password, sizeof(client_id));

    ESP_LOGI(TAG, "Broker:  %s:%d", TUYA_MQTT_BROKER, TUYA_MQTT_PORT);

    esp_mqtt_client_config_t cfg = {0};
    cfg.broker.address.uri               = TUYA_MQTT_BROKER;
    cfg.broker.address.port              = TUYA_MQTT_PORT;
    cfg.credentials.client_id            = client_id;
    cfg.credentials.username             = username;
    cfg.credentials.authentication.password = password;
    /* 对齐官方 Java 示例: MQTT 3.1.1 / keepalive 60s; clean session 与自动重连为默认开启 */
    cfg.session.protocol_ver             = MQTT_PROTOCOL_V_3_1_1;
    cfg.session.keepalive                = 60;
    cfg.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;

    mqtt_handle = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(mqtt_handle, ESP_EVENT_ANY_ID,
                                    tuya_mqtt_event_handler, NULL);
    ESP_LOGI(TAG, "MQTT 客户端启动...");
    return esp_mqtt_client_start(mqtt_handle);
}

/* ════════════════════════════════════════════════════════════
 * 停止并销毁 MQTT 客户端（WiFi 断开时调用）
 * ════════════════════════════════════════════════════════════ */
void tuya_stop(void)
{
    if (mqtt_handle) {
        esp_mqtt_client_stop(mqtt_handle);
        esp_mqtt_client_destroy(mqtt_handle);
        mqtt_handle = NULL;
    }
    tuya_mqtt_is_connected = false;
}

/* ════════════════════════════════════════════════════════════
 * 属性上报
 * Topic: tylink/{deviceId}/thing/property/report
 * ════════════════════════════════════════════════════════════ */
esp_err_t tuya_post_property_data(const char *data)
{
    if (!mqtt_handle) return ESP_ERR_INVALID_STATE;

    char topic[128];
    snprintf(topic, sizeof(topic),
             "tylink/%s/thing/property/report", TUYA_DEVICE_ID);

    ESP_LOGI(TAG, "上报: %s", data);
    return esp_mqtt_client_publish(mqtt_handle, topic, data, strlen(data), 1, 0);
}

/* ════════════════════════════════════════════════════════════
 * 动作执行应答
 * Topic: tylink/{deviceId}/thing/action/execute_response
 * ════════════════════════════════════════════════════════════ */
esp_err_t tuya_post_action_response(const char *msgId,
                                     const char *actionCode,
                                     const char *measure_data_hex)
{
    if (!mqtt_handle) return ESP_ERR_INVALID_STATE;

    char topic[128];
    snprintf(topic, sizeof(topic),
             "tylink/%s/thing/action/execute_response", TUYA_DEVICE_ID);

    cJSON *rsp = cJSON_CreateObject();
    cJSON_AddStringToObject(rsp, "msgId", msgId);
    cJSON_AddNumberToObject(rsp, "time", ts_ms());
    cJSON_AddNumberToObject(rsp, "code", 0);
    cJSON *data = cJSON_AddObjectToObject(rsp, "data");
    cJSON_AddStringToObject(data, "actionCode", actionCode);
    cJSON *out = cJSON_AddObjectToObject(data, "outputParams");
    cJSON_AddStringToObject(out, "measure_data", measure_data_hex);

    char *p = cJSON_PrintUnformatted(rsp);
    esp_err_t ret = ESP_OK;
    if (p) {
        ESP_LOGI(TAG, "动作应答: %s", p);
        ret = esp_mqtt_client_publish(mqtt_handle, topic, p, strlen(p), 1, 0);
        cJSON_free(p);
    }
    cJSON_Delete(rsp);
    return ret;
}

/* ════════════════════════════════════════════════════════════
 * 订阅下行 Topic
 * ════════════════════════════════════════════════════════════ */
static void tuya_subscribe(void)
{
    char topic[128];

    /* 属性上报应答 */
    snprintf(topic, sizeof(topic),
             "tylink/%s/thing/property/report_response", TUYA_DEVICE_ID);
    esp_mqtt_client_subscribe_single(mqtt_handle, topic, 1);

    /* 属性下发 */
    snprintf(topic, sizeof(topic),
             "tylink/%s/thing/property/set", TUYA_DEVICE_ID);
    esp_mqtt_client_subscribe_single(mqtt_handle, topic, 1);

    /* 动作执行 */
    snprintf(topic, sizeof(topic),
             "tylink/%s/thing/action/execute", TUYA_DEVICE_ID);
    esp_mqtt_client_subscribe_single(mqtt_handle, topic, 1);
}

/* ════════════════════════════════════════════════════════════
 * 属性设置应答
 * 官方格式 (device_mode.md):
 *   {"msgId":"xxx","time":...,"code":0}  ← 无 data 字段
 * Topic: tylink/{deviceId}/thing/property/set_response
 * ════════════════════════════════════════════════════════════ */
static void tuya_property_set_response(const char *msgId, int code)
{
    char topic[128];
    snprintf(topic, sizeof(topic),
             "tylink/%s/thing/property/set_response", TUYA_DEVICE_ID);

    cJSON *rsp = cJSON_CreateObject();
    cJSON_AddStringToObject(rsp, "msgId", msgId);
    cJSON_AddNumberToObject(rsp, "time", ts_ms());
    cJSON_AddNumberToObject(rsp, "code", code);

    char *p = cJSON_PrintUnformatted(rsp);
    if (p) {
        esp_mqtt_client_publish(mqtt_handle, topic, p, strlen(p), 1, 0);
        cJSON_free(p);
    }
    cJSON_Delete(rsp);
}
