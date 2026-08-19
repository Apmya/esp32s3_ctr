#ifndef __TUYA_DM_H
#define __TUYA_DM_H
#include "cJSON.h"
#include "ina219.h"

/* 传感器全局变量（main.c 定义） */
extern float vbus, cur, power, temp;
extern SemaphoreHandle_t data_mutex;
extern SemaphoreHandle_t cur_str_mutex;

void tuya_dm_init(void);
void tuya_property_handle(cJSON *data_obj);
cJSON *tuya_property_upload(void);

/* 动作处理：measure → 触发阀门电流测量 */
void tuya_action_handle(const char *msgId, cJSON *inputParams);

#endif /* __TUYA_DM_H */
