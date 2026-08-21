#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include <stdio.h>
#include <string.h>
#include "esp_err.h"

#include "ap_wifi.h"    
#include "myble.h"      
#include "tuya_mqtt.h"
#include "tuya_dm.h"  
#include "iic.h"
#include "ina219.h"
#include "myntc.h"
#include "myvalve.h"
#include "mynvs.h"
#include "led_ws2812.h"

#define TAG "MAIN_APP"

#define TASK_PRIOR_APWIFI  4
#define TASK_PRIOR_WIFI    5
#define TASK_PRIOR_IIC     3  
#define TASK_PRIOR_BLE     2  

#define DATA_UPLOAD_PERIOD 2000 
//=================================================================

SemaphoreHandle_t data_mutex;
float vbus = 0;
float cur = 0;
float power = 0;
float temp = 0;

/**
 * @brief ble配速
 */
static void ble_task(void* arg)
{
    ble_init();  
    while(1)
    {
        if (wifi_is_connected)
        {
            vTaskDelay(pdMS_TO_TICKS(3000));
        }
        else 
        {
            vTaskDelay(pdMS_TO_TICKS(100)); 
        }
    }

}

/**
 * @brief AP网页配网任务：无WiFi时启动热点网页
 */
static void ap_cfg_task(void* arg)
{
    bool is_ap_active = false; // 记录AP服务是否已启动

    while(1)
    {
        if ((strlen(g_wifi_ssid) == 0 || !wifi_is_connected) && !is_ap_active)
        {
            ESP_LOGW(TAG, "WiFi未连接,启动AP网页配网...");
            ap_wifi_apcfg(); 
            is_ap_active = true; 
        }
        else if (wifi_is_connected && is_ap_active)
        {
            ESP_LOGI(TAG, "WiFi已连接,关闭AP配网服务...");
            ap_wifi_stop();
            is_ap_active = false; 
        }
        
        vTaskDelay(pdMS_TO_TICKS(3000)); 
    }
}

/**
 * @brief Tuya MQTT连接管理任务
 *
 * wifi_is_connected 在 WIFI_EVENT_STA_CONNECTED 时置位（L2 已连），
 * 但此时 IP 可能尚未分配，需等待 IP_EVENT_STA_GOT_IP 后才能发起 TCP 连接。
 */
static void wifi_tuya_task(void* arg)
{
    bool is_tuya_started = false;
    while(1)
    {
        if (wifi_is_connected && !is_tuya_started)
        {
            /* 等 IP 就绪 — 若连接已断开则放弃本轮 */
            for (int i = 0; i < 50 && wifi_is_connected; i++) {
                esp_netif_ip_info_t ip;
                esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
                if (sta && esp_netif_get_ip_info(sta, &ip) == ESP_OK) {
                    if (ip.ip.addr != 0) break;   /* IP 已分配 */
                }
                vTaskDelay(pdMS_TO_TICKS(100));
            }

            if (!wifi_is_connected) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }

            ESP_LOGI(TAG, "WiFi已联网,启动Tuya MQTT...");
            tuya_start();
            is_tuya_started = true;
        }

        /* WiFi 断开 → 销毁 MQTT 客户端，防止旧实例自动重连时与下次新建的实例互踢 */
        if (!wifi_is_connected && is_tuya_started)
        {
            tuya_stop();
            is_tuya_started = false;
        }

        /* MQTT 断连由 esp_mqtt 内置自动重连处理，不再手动重启：
         * 手动重启会创建多个相同 clientId 的客户端，被云端互踢导致反复断连 */

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/**
 * @brief IIC采集任务：电压/电流/功率/温度
 * 读取在锁外(INA219 内部自带互斥), 仅数据拷贝短锁, 避免 I2C 超时卡住 MQTT 回调
 */
static void iic_collect_task(void* arg)
{
    while(1)
    {
        float local_vbus  = ina219_get_bus_voltage();
        float local_cur   = ina219_get_current();
        float local_power = ina219_get_power();
        float local_temp  = ntc_read_temperature();

        /* 只锁数据拷贝，瞬间完成 */
        xSemaphoreTake(data_mutex, portMAX_DELAY);
        vbus  = local_vbus + 0.3f;
        cur   = local_cur;
        power = local_power;
        temp  = local_temp;
        xSemaphoreGive(data_mutex);
        vTaskDelay(pdMS_TO_TICKS(DATA_UPLOAD_PERIOD));
    }
}

void app_main(void)
{
    // 1. NVS 初始化
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS初始化完成");
    
    wifi_nvs_load();
    ESP_LOGI(TAG, "NVS加载WiFi配置: SSID=%s", g_wifi_ssid);

    // 2. 系统基础初始化
    esp_event_loop_create_default();
    data_mutex = xSemaphoreCreateMutex();
    if(data_mutex == NULL){
        ESP_LOGE(TAG,"mutex create fail");
        vTaskDelete(NULL);
    }

    // 3. 外设与协议栈初始化
    
    ap_wifi_init();
    tuya_dm_init();
    iic_dev_start();
    ntc_init();          /* NTC 温度: ADC1_CH7(GPIO8), 10kΩ/B3950 */
    valve_init();
    vTaskDelay(pdMS_TO_TICKS(1000));

    // 4. 创建 FreeRTOS 任务
    xTaskCreatePinnedToCore(ble_task,         "ble_task", 6144, NULL, TASK_PRIOR_BLE, NULL,1); 
    xTaskCreatePinnedToCore(ap_cfg_task,      "ap_cfg_task", 8192, NULL, TASK_PRIOR_APWIFI, NULL,1); 
    xTaskCreatePinnedToCore(wifi_tuya_task,   "wifi_tuya",   8192, NULL, TASK_PRIOR_WIFI,   NULL,1);
    xTaskCreatePinnedToCore(iic_collect_task, "iic_collect", 8192, NULL, TASK_PRIOR_IIC,    NULL,1); 
    ESP_LOGI(TAG, "系统初始化完成,进入主循环");

    while(1) 
    {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
