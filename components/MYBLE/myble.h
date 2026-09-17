#ifndef __MYBLE_H_
#define __MYBLE_H_

#include "esp_err.h"

void start_advertising(void);

/* BLE 初始化(内部自建 NimBLE host 任务): 在 app_main 装配序列中调用一次 */
esp_err_t ble_init(void);

#endif
