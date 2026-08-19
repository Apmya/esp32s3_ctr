#ifndef __MYNVS_H
#define __MYNVS_H

#define SSID_MAX_LEN 64
#define PWD_MAX_LEN  64

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"  

// 全局WiFi缓存，myble外部引用
extern char g_wifi_ssid[SSID_MAX_LEN];
extern char g_wifi_passwd[PWD_MAX_LEN];

esp_err_t wifi_nvs_load(void);
esp_err_t wifi_nvs_save(const char* ssid, const char* pwd);
void wifi_nvs_clear(void);


#endif