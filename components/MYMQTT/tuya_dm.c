#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "math.h"
#include <string.h>
#include <sys/time.h>
#include "esp_log.h"

#include "myvalve.h"
#include "tuya_dm.h"
#include "led_ws2812.h"
#include "tuya_mqtt.h"
#include "esp_timer.h"

#define TAG  "tuya_dm"

/* 开关ON后延迟上报电参数状态的时间 */
#define STATUS_REPORT_DELAY_US  (4 * 1000 * 1000)  /* 4s */
/* 周期上报间隔: 每 5s 上报一次当前状态(仅数值 DP, 避免限流) */
#define PERIODIC_REPORT_MS      (5 * 1000)

/* ── 本地状态 ── */
static ws2812_strip_handle_t ws2812_led = NULL;
static TaskHandle_t valve_meas_hdl  = NULL;
static int  valve_state   = 0;       /* 0=关 1=开, 对应涂鸦 switch */
static char dev_status[128] = "normal";
static bool s_manual_switch = false;                    /* DP106 测量开关状态 */
static esp_timer_handle_t s_status_timer = NULL;        /* 开关ON 4s后上报定时器 */
static esp_timer_handle_t s_periodic_timer = NULL;      /* 5s 周期上报定时器 */

/* 数值截断到 [lo, hi], 防止超出物模型量程被云端拒收 */
static long clamp_i(long v, long lo, long hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* 周期上报: 仅数值 DP, 不含字符串/测量开关 */
static cJSON *tuya_property_upload_periodic(void);

SemaphoreHandle_t cur_str_mutex = NULL;
ina219_valve_curr_t cur_str;
static bool s_measure_valid = false;   /* 最近一次测量是否有效(VBUS≥5V, 采样电阻/供电正常) */

/* 传感器本地缓存 */
static float l_vbus, l_cur, l_power, l_temp;

/* ── 时间戳 ── */
static int64_t now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

/* ── 开关ON 4s后: 上报一次当前电流/电压/功率/温度状态 ──
 * esp_timer 回调中仅做轻量操作: 快照数据(内部加锁) + MQTT publish(异步, 不阻塞) */
static void status_report_timer_cb(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "开关已开启4s, 上报当前电参数状态");
    cJSON *js = tuya_property_upload();
    if (js) {
        char *payload = cJSON_PrintUnformatted(js);
        tuya_post_property_data(payload);
        cJSON_free(payload);
        cJSON_Delete(js);
    }
}

/* ── 5s 周期上报: 只发数值 DP(不含字符串, 字符串仅在事件时上报) ── */
static void periodic_report_timer_cb(void *arg)
{
    (void)arg;
    if (!tuya_mqtt_is_connected) return;   /* 未连接不重复入队 */

    cJSON *js = tuya_property_upload_periodic();
    if (js) {
        char *payload = cJSON_PrintUnformatted(js);
        tuya_post_property_data(payload);
        cJSON_free(payload);
        cJSON_Delete(js);
    }
}

/* ── 字符串转 hex（用于 Raw 透传） ── */
static void str_to_hex(const char *src, char *dst, size_t dst_len)
{
    size_t len = strlen(src);
    if (len * 2 + 1 > dst_len) len = (dst_len - 1) / 2;
    for (size_t i = 0; i < len; i++) {
        sprintf(dst + i * 2, "%02x", (unsigned char)src[i]);
    }
    dst[len * 2] = '\0';
}

/* ── 本地 LED 状态指示 ── */
static void led_indicate(int r, int g, int b)
{
    if (ws2812_led) ws2812_write(ws2812_led, 0, r, g, b);
}

/* ════════════════════════════════════════════════════════════
 * 阀门电流测量任务
 * 由动作 measure 触发, 完成后:
 *   ① 应答 action/execute_response (measure_data = hex)
 *   ② 将数据写到 device_state_report 属性并上报
 * ════════════════════════════════════════════════════════════ */
static char s_pending_msgId[64];   /* tuya_action_handle 写入, 测量任务读取 */

static void valve_measure_task(void *arg)
{
    (void)arg;
    ina219_valve_curr_t m;

    while (1) {
        /* 等待动作触发, 消费通知 */
        xTaskNotifyWait(0, 0xFFFFFFFF, NULL, portMAX_DELAY);

        memset(&m, 0, sizeof(m));
        led_indicate(0, 0, 255);  /* 蓝色=测量中 */

        /* 测量开始 → 结果标记为无效, 避免上报上一次的旧数据 */
        xSemaphoreTake(cur_str_mutex, portMAX_DELAY);
        s_measure_valid = false;
        xSemaphoreGive(cur_str_mutex);

        esp_err_t ret = ina219_measure_valve_current(&m,
                                                      valve_open, valve_close);
        if (ret == ESP_OK) {
            /* 有效性门控: VBUS<5V 说明采样电阻/24V供电未接入,
             * INA219 测量值全是噪声, 不上报 IP/IH/TP/Period/Duty */
            float vbus_gate = ina219_get_bus_voltage();
            bool valid = (vbus_gate >= 5.0f);
            if (!valid) {
                ESP_LOGW(TAG, "测量结果无效(VBUS=%.1fV<5V, 采样电阻或供电未接入?), IP/IH/TP/Period/Duty 不上报",
                         (double)vbus_gate);
            }

            xSemaphoreTake(cur_str_mutex, portMAX_DELAY);
            memcpy(&cur_str, &m, sizeof(ina219_valve_curr_t));
            s_measure_valid = valid;
            xSemaphoreGive(cur_str_mutex);

            /* period/duty: 正常时上报实测数据; 测不出(err!=0)时 Period 上报错误码, Duty 上报 0 */
            int64_t rp = (m.ripple_err == RIPPLE_ERR_OK) ? m.period : (int64_t)m.ripple_err;
            float   rd = (m.ripple_err == RIPPLE_ERR_OK) ? m.duty   : 0.0f;

            char measure_json[256];
            snprintf(measure_json, sizeof(measure_json),
                     "{\"IP\":%.3f,\"IH\":%.3f,\"Tp\":%.2f,\"period\":%.2f,\"duty\":%d}",
                     round(m.Ip * 1000.0f) / 1000.0f,
                     round(m.Ih * 1000.0f) / 1000.0f,
                     round((float)m.Tp / 1000.0f * 100.0f) / 100.0f,
                     round((float)rp / 1000.0f * 100.0f) / 100.0f,
                     (int)round(rd * 100.0f));

            /* ① 应答动作 (兼容 thing/action/execute 触发) */
            char hex_out[512];
            str_to_hex(measure_json, hex_out, sizeof(hex_out));

            if (strlen(s_pending_msgId) > 0) {
                tuya_post_action_response(s_pending_msgId, "measure", hex_out);
            }

            /* ② 测量完成 → 测量开关复位, 写入 device_state_report,
             *    并通过完整属性上报把测量数据与 manual_switch=false 一并回读云端 */
            s_manual_switch = false;
            snprintf(dev_status, sizeof(dev_status), "%s", measure_json);

            cJSON *js = tuya_property_upload();
            if (js) {
                char *payload = cJSON_PrintUnformatted(js);
                tuya_post_property_data(payload);
                cJSON_free(payload);
                cJSON_Delete(js);
            }
        } else {
            ESP_LOGW(TAG, "阀门测量失败: 0x%x", ret);
            /* 测量失败同样复位测量开关, 避免云端状态卡死 */
            s_manual_switch = false;
            if (strlen(s_pending_msgId) > 0) {
                tuya_post_action_response(s_pending_msgId, "measure", "error");
            }
        }

        /* 恢复 LED */
        led_indicate(valve_state ? 0 : 128, valve_state ? 128 : 0, 0);
    }
}

/* ════════════════════════════════════════════════════════════
 * 初始化
 * ════════════════════════════════════════════════════════════ */
void tuya_dm_init(void)
{
    ws2812_init(GPIO_NUM_48, 1, &ws2812_led);
    led_indicate(128, 0, 0);  /* 红灯=阀门关 */

    xTaskCreatePinnedToCore(valve_measure_task, "valve_meas",
                             6144, NULL, 6, &valve_meas_hdl, 1);
    cur_str_mutex = xSemaphoreCreateMutex();

    /* 开关ON 4s后上报电参数状态: 一次性定时器, 由 switch 下发启动/取消 */
    esp_timer_create_args_t targs = {
        .callback = status_report_timer_cb,
        .name     = "status_4s_timer",
    };
    if (esp_timer_create(&targs, &s_status_timer) != ESP_OK) {
        ESP_LOGE(TAG, "4s上报定时器创建失败, 开关4s上报功能不可用");
        s_status_timer = NULL;
    }

    /* 每 5s 周期上报当前状态(仅数值 DP) */
    esp_timer_create_args_t ptargs = {
        .callback = periodic_report_timer_cb,
        .name     = "periodic_5s_timer",
    };
    if (esp_timer_create(&ptargs, &s_periodic_timer) != ESP_OK) {
        ESP_LOGE(TAG, "周期上报定时器创建失败, 5s周期上报不可用");
        s_periodic_timer = NULL;
    } else {
        esp_timer_start_periodic(s_periodic_timer,
                                 PERIODIC_REPORT_MS * 1000);
    }

    ESP_LOGI(TAG, "Tuya 数据模型初始化完成");
}

/* ════════════════════════════════════════════════════════════
 * 属性下发处理
 * data_obj = 云端下发 JSON 中 "data" 的值
 * 格式: {"switch": true}  或  {"switch": false, "device_state_report": "xxx"}
 * ════════════════════════════════════════════════════════════ */
void tuya_property_handle(cJSON *data_obj)
{
    if (!data_obj) return;

    cJSON *item = data_obj->child;
    while (item) {
        const char *key = item->string;
        if (!key) { item = item->next; continue; }

        if (strcmp(key, "switch") == 0) {
            if (cJSON_IsTrue(item)) {
                valve_state = 1;
                valve_open();
                led_indicate(0, 128, 0);  /* 绿灯=阀门开 */
                snprintf(dev_status, sizeof(dev_status), "valve_open");
                /* 按下开关4s后上报一次电流/电压/功率/温度状态 */
                if (s_status_timer) {
                    esp_timer_restart(s_status_timer, STATUS_REPORT_DELAY_US);
                }
            } else {
                valve_state = 0;
                valve_close();
                led_indicate(128, 0, 0);  /* 红灯=阀门关 */
                snprintf(dev_status, sizeof(dev_status), "valve_closed");
                /* 关阀 → 取消待执行的4s上报 */
                if (s_status_timer) {
                    esp_timer_stop(s_status_timer);
                }
            }
            ESP_LOGI(TAG, "switch → %s", valve_state ? "ON" : "OFF");
        }
        else if (strcmp(key, "manual_switch") == 0) {
            /* DP106 测量开关: 置 true → 自动开关电磁阀并测量 IP/IH/TP/PERIOD/DUTY */
            if (cJSON_IsTrue(item)) {
                s_manual_switch = true;
                /* 属性触发无 action 应答 → 清空遗留 msgId, 防止误应答旧动作 */
                s_pending_msgId[0] = '\0';
                ESP_LOGI(TAG, "收到测量开关(manual_switch=true), 触发自动测量");
                if (valve_meas_hdl) {
                    xTaskNotifyGive(valve_meas_hdl);
                }
            }
        }
        else if (strcmp(key, "device_state_report") == 0) {
            /* 云端可下发重置状态 */
            const char *s = cJSON_GetStringValue(item);
            if (s) snprintf(dev_status, sizeof(dev_status), "%s", s);
        }
        else {
            ESP_LOGW(TAG, "未知下发属性: %s", key);
        }
        item = item->next;
    }
}

/* ════════════════════════════════════════════════════════════
 * 动作下发处理
 * msgId       消息 ID（用于应答）
 * inputParams 输入参数 {"measure_but": true}
 * ════════════════════════════════════════════════════════════ */
void tuya_action_handle(const char *msgId, cJSON *inputParams)
{
    if (!inputParams) return;

    cJSON *btn = cJSON_GetObjectItem(inputParams, "measure_but");
    if (!btn || !cJSON_IsTrue(btn)) return;

    ESP_LOGI(TAG, "收到测量动作, msgId=%s", msgId);

    /* 保存 msgId 供测量任务应答使用 */
    snprintf(s_pending_msgId, sizeof(s_pending_msgId), "%s", msgId ? msgId : "");

    if (valve_meas_hdl) {
        xTaskNotifyGive(valve_meas_hdl);
    }
}

/* ════════════════════════════════════════════════════════════
 * 构建属性上报 JSON (TyLINK 格式, DP 定义见 tuya.md)
 * 每项为 {"value":..,"time":..}; 数值 DP 按物模型倍数换算并截断到量程
 * ════════════════════════════════════════════════════════════ */
/* 构建 data 对象: include_status=1 时附带 manual_switch + device_state_report */
static cJSON *build_property_data(int include_status)
{
    int64_t ts = now_ms();

    /* 快照传感器数据 */
    xSemaphoreTake(data_mutex, portMAX_DELAY);
    l_vbus  = vbus;
    l_cur   = cur;
    l_power = power;
    l_temp  = temp;
    xSemaphoreGive(data_mutex);

    /* 快照最近一次测量结果 (IP/IH/TP/Period/Duty) */
    ina219_valve_curr_t m;
    bool has_measure;
    if (cur_str_mutex) {
        xSemaphoreTake(cur_str_mutex, portMAX_DELAY);
        m = cur_str;
        has_measure = s_measure_valid;
        xSemaphoreGive(cur_str_mutex);
    } else {
        memset(&m, 0, sizeof(m));
        has_measure = false;
    }
    /* 未测量过或测量无效 → Period/Duty 上报 0 */
    int64_t rp = 0;
    float   rd = 0;
    if (has_measure) {
        /* 纹波测不出(err!=0)时 Period 直接上报错误码, Duty 上报 0 */
        rp = (m.ripple_err == RIPPLE_ERR_OK) ? m.period : (int64_t)m.ripple_err;
        rd = (m.ripple_err == RIPPLE_ERR_OK) ? m.duty   : 0.0f;
    }

    cJSON *data = cJSON_CreateObject();

    /* 辅助：添加 {value, time} 属性 */
    #define ADD_VT_BOOL(_d, _k, _v, _t) do {  \
        cJSON *o = cJSON_CreateObject();       \
        cJSON_AddBoolToObject(o, "value", _v); \
        cJSON_AddNumberToObject(o, "time", _t);\
        cJSON_AddItemToObject(_d, _k, o);      \
    } while(0)

    #define ADD_VT_NUM(_d, _k, _v, _t) do {    \
        cJSON *o = cJSON_CreateObject();       \
        cJSON_AddNumberToObject(o, "value", _v);\
        cJSON_AddNumberToObject(o, "time", _t);\
        cJSON_AddItemToObject(_d, _k, o);      \
    } while(0)

    #define ADD_VT_STR(_d, _k, _v, _t) do {    \
        cJSON *o = cJSON_CreateObject();       \
        cJSON_AddStringToObject(o, "value", _v);\
        cJSON_AddNumberToObject(o, "time", _t);\
        cJSON_AddItemToObject(_d, _k, o);      \
    } while(0)

    /* DP101 */ ADD_VT_BOOL(data, "switch",              valve_state, ts);
    /* DP102 */ ADD_VT_NUM (data, "voltage_current",     (int)round(l_vbus  * 100.0),  ts);
    /* DP103 */ ADD_VT_NUM (data, "cur_current",         clamp_i((long)round(l_cur * 1000.0f), 0, 99999), ts);
    /* DP104 */ ADD_VT_NUM (data, "power_current",       (int)round(l_power * 100.0),  ts);
    /* DP105 */ ADD_VT_NUM (data, "temp_outdoor",        (int)round(l_temp  * 100.0),  ts);

    /* 测量结果数值 DP (tuya.md 物模型, 倍数换算 + 量程截断) */
    /* DP108 */ ADD_VT_NUM (data, "IP",      clamp_i((long)round(m.Ip * 1000.0f), 0, 1000000),     ts);
    /* DP109 */ ADD_VT_NUM (data, "IH",      clamp_i((long)round(m.Ih * 1000.0f), 0, 100000),     ts);
    /* DP110 */ ADD_VT_NUM (data, "TP",      clamp_i((long)round((float)m.Tp / 10.0f), 0, 10000000), ts);
    /* DP111 */ ADD_VT_NUM (data, "Period",  clamp_i((long)rp, 0, 10000),                          ts);
    /* DP112 */ ADD_VT_NUM (data, "Duty",    clamp_i((long)round(rd * 100.0f), 0, 100),            ts);

    if (include_status) {
        /* DP106 */ ADD_VT_BOOL(data, "manual_switch",       s_manual_switch, ts);
        /* DP107 */ ADD_VT_STR (data, "device_state_report", dev_status, ts);
    }

    #undef ADD_VT_BOOL
    #undef ADD_VT_NUM
    #undef ADD_VT_STR

    return data;
}

cJSON *tuya_property_upload(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "msgId", "report");
    cJSON_AddNumberToObject(root, "time", now_ms());
    cJSON *data = build_property_data(1);
    cJSON_AddItemToObject(root, "data", data);
    return root;
}

/* 周期上报(5s): 只发数值 DP, 不含字符串/测量开关 */
static cJSON *tuya_property_upload_periodic(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "msgId", "report");
    cJSON_AddNumberToObject(root, "time", now_ms());
    cJSON *data = build_property_data(0);
    cJSON_AddItemToObject(root, "data", data);
    return root;
}
