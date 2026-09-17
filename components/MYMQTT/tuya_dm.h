#ifndef __TUYA_DM_H
#define __TUYA_DM_H
#include "cJSON.h"
#include "ina219.h"

/* 传感器聚合数据(电压/电流/功率/温度)的所有权已迁至 SENSOR/telemetry.h,
 * 本头文件不再导出任何共享数据 */

void tuya_dm_init(void);
void tuya_property_handle(cJSON *data_obj);
cJSON *tuya_property_upload(void);

/* 动作处理：measure → 触发阀门电流测量 */
void tuya_action_handle(const char *msgId, cJSON *inputParams);

#endif /* __TUYA_DM_H */
