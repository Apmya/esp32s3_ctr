#ifndef __TUYA_MQTT_H
#define __TUYA_MQTT_H
#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "tuya_token.h"

/* MQTT 连接状态: 数据私有于 tuya_mqtt.c, 只读 API */
bool tuya_mqtt_is_connected(void);

esp_err_t tuya_start(void);
/* 停止并销毁 MQTT 客户端（WiFi 断开时调用，避免重复实例互踢） */
void tuya_stop(void);

/* 启动 MQTT 生命周期环(prio5, core1): WiFi联网就绪→tuya_start, 断连→tuya_stop
 * 内部自建任务, 须在 wifista 初始化之后调用一次 */
void tuya_lifecycle_start(void);

esp_err_t tuya_post_property_data(const char *data);

/* 动作执行应答: topic = thing/action/execute_response */
esp_err_t tuya_post_action_response(const char *msgId,
                                     const char *actionCode,
                                     const char *measure_data_hex);

#endif /* __TUYA_MQTT_H */
