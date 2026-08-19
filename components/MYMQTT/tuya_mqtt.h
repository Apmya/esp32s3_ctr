#ifndef __TUYA_MQTT_H
#define __TUYA_MQTT_H
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "tuya_token.h"

extern bool tuya_mqtt_is_connected;
extern SemaphoreHandle_t data_mutex;

esp_err_t tuya_start(void);
/* 停止并销毁 MQTT 客户端（WiFi 断开时调用，避免重复实例互踢） */
void tuya_stop(void);
esp_err_t tuya_post_property_data(const char *data);

/* 动作执行应答: topic = thing/action/execute_response */
esp_err_t tuya_post_action_response(const char *msgId,
                                     const char *actionCode,
                                     const char *measure_data_hex);

#endif /* __TUYA_MQTT_H */
