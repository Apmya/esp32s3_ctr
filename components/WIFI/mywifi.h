#ifndef __MYWIFI_H
#define __MYWIFI_H

#include "freertos/semphr.h"
#include "esp_wifi.h"
#include "esp_err.h"

extern bool wifi_is_connected;

typedef void (*p_wifi_scan_cb)(int num ,wifi_ap_record_t *ap_records);

//sta启动，连接wifi
void wifista_init(void);
//重连wifi
void wifista_restart(void);
//启动STA+AP
esp_err_t wifiap_sta_start(void);
//扫描周围wifi
esp_err_t wifiap_scan(p_wifi_scan_cb f);


#endif