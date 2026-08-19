#include <string.h>
#include <stdio.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lwip/ip4_addr.h"

#include "mywifi.h"
#include "mynvs.h"

#define TAG "MYWIFI"


static const char* ap_ssid_name = "ESP32S3-AP";
static const char* ap_password = "12345678";

static esp_netif_t* esp_netif_ap = NULL;

static SemaphoreHandle_t scan_sem = NULL;

/* 自动重连: 指数退避定时器 */
static esp_timer_handle_t s_reconnect_timer = NULL;
static uint32_t s_reconnect_attempt = 0;

bool wifi_is_connected = false;

/* 重连回调: 事件回调中不能阻塞延时, 由定时器延迟后发起重连 */
static void wifi_reconnect_timer_cb(void *arg)
{
    (void)arg;
    if (!wifi_is_connected) {
        esp_wifi_connect();
    }
}

/**
 * @brief wifista事件回调
 */
void wifista_event_handler(void* event_handler_arg,esp_event_base_t event_base,int32_t event_id,void* event_data)
{
    if(event_base == WIFI_EVENT)
    {   
        switch(event_id)
        {
            case WIFI_EVENT_STA_START:
                wifi_mode_t mode;
                esp_wifi_get_mode(&mode);
                if(mode == WIFI_MODE_STA)
                    esp_wifi_connect();          
                break;
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG,"WiFi Connected");
                wifi_is_connected = true;
                s_reconnect_attempt = 0;   /* 连接成功, 退避计数清零 */
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGI(TAG,"WiFi DisConnected");
                wifi_is_connected = false;
                /* 自动重连: 指数退避 2s→4s→8s→16s→30s(封顶) */
                if (s_reconnect_timer) {
                    uint32_t shift = (s_reconnect_attempt > 4) ? 4 : s_reconnect_attempt;
                    uint32_t delay_ms = 2000U << shift;
                    if (delay_ms > 30000) {
                        delay_ms = 30000;
                    }
                    s_reconnect_attempt++;
                    esp_timer_start_once(s_reconnect_timer, (uint64_t)delay_ms * 1000);
                    ESP_LOGI(TAG, "%dms 后自动重连", (int)delay_ms);
                }
                break;
            case WIFI_EVENT_AP_STACONNECTED:
                ESP_LOGI(TAG,"STA Device Connected");
                break;
            case WIFI_EVENT_AP_STADISCONNECTED:
                ESP_LOGI(TAG,"STA Device DisConnected");
                break;
        }
    }
    else if(event_base == IP_EVENT)
    {
        switch(event_id)
        {
            case IP_EVENT_STA_GOT_IP:
                esp_netif_ip_info_t *event = (esp_netif_ip_info_t *)event_data;
                ESP_LOGI(TAG,"输出ip");
                ESP_LOGI(TAG,"ip =  %d.%d.%d.%d", esp_ip4_addr1_16(&event->ip), esp_ip4_addr2_16(&event->ip)
                                                , esp_ip4_addr3_16(&event->ip), esp_ip4_addr4_16(&event->ip));
                break;
        }
    }
}
/**
 * @brief wifi初始化,并打开STA
 */
void wifista_init()
{
    esp_netif_init();
    //在主函数中调用esp_event_loop_create_default函数
    esp_netif_create_default_wifi_sta();
    esp_netif_ap = esp_netif_create_default_wifi_ap();
 
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,&wifista_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,&wifista_event_handler, NULL);

    scan_sem = xSemaphoreCreateBinary();
    xSemaphoreGive(scan_sem);

    /* 自动重连定时器(一次性, 由断开事件启动) */
    esp_timer_create_args_t reconnect_args = {
        .callback = wifi_reconnect_timer_cb,
        .name     = "wifi_reconnect",
    };
    if (esp_timer_create(&reconnect_args, &s_reconnect_timer) != ESP_OK) {
        ESP_LOGW(TAG, "重连定时器创建失败, 断开后将无法自动重连");
        s_reconnect_timer = NULL;
    }
 

    //STA下连接WIFI
    wifi_config_t wifista_config = {0};
    // 从NVS全局缓存读取SSID、密码
    strncpy((char *)wifista_config.sta.ssid, g_wifi_ssid, sizeof(wifista_config.sta.ssid)-1);
    strncpy((char *)wifista_config.sta.password, g_wifi_passwd, sizeof(wifista_config.sta.password)-1);
    wifista_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    esp_wifi_set_mode(WIFI_MODE_STA);
    if(strlen(g_wifi_ssid) == 0)
        ESP_LOGW(TAG, "NVS无保存WiFi");
    esp_wifi_set_config(WIFI_IF_STA, &wifista_config);
    esp_wifi_start();
}

/**
 * @brief wifi重新初始化
 */
void wifista_restart(void)
{
    wifi_mode_t cur_mode;
    esp_wifi_get_mode(&cur_mode);

    wifi_config_t wifista_config = {0};
    strncpy((char *)wifista_config.sta.ssid, g_wifi_ssid, sizeof(wifista_config.sta.ssid)-1);
    strncpy((char *)wifista_config.sta.password, g_wifi_passwd, sizeof(wifista_config.sta.password)-1);
    wifista_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_LOGI(TAG, "重新连接 SSID:%s", g_wifi_ssid);
    esp_wifi_set_config(WIFI_IF_STA, &wifista_config);

    if (cur_mode != WIFI_MODE_STA && cur_mode != WIFI_MODE_APSTA)
    {
        ESP_LOGI(TAG,"当前非STA模式,完整重启WiFi");
        esp_wifi_disconnect();
        esp_wifi_stop();

        esp_wifi_set_mode(WIFI_MODE_STA);
        vTaskDelay(pdMS_TO_TICKS(200));
        esp_wifi_start();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    else
    {
        ESP_LOGI(TAG,"已是STA/APSTA模式,仅重连不重启wifi驱动");
        esp_wifi_disconnect();
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    esp_err_t ret= esp_wifi_connect();
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "WiFi连接请求已发起，等待获取IP");
    }
    else
    {
        ESP_LOGE(TAG, "wifi connect fail, ret:%s", esp_err_to_name(ret));
    }

}

/**
 * @brief 打开STA+AP
 */
esp_err_t wifiap_sta_start(void)
{
    
    wifi_config_t wifi_config = 
    {
        .ap = 
        {
            .channel = 5,
            .max_connection = 2,
            .authmode = WIFI_AUTH_WPA2_PSK,
        }
    };
    strncpy((char *)wifi_config.ap.ssid, ap_ssid_name, sizeof(wifi_config.ap.ssid)-1);
    wifi_config.ap.ssid_len = strlen(ap_ssid_name);
    strncpy((char *)wifi_config.ap.password, ap_password, sizeof(wifi_config.ap.password)-1);
    esp_wifi_set_config(WIFI_IF_AP,&wifi_config);
    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);
    if(mode == WIFI_MODE_APSTA)
    {
        return ESP_OK;
    }
    
    esp_wifi_set_mode(WIFI_MODE_APSTA);

    esp_netif_ip_info_t ipInfo;
    IP4_ADDR(&ipInfo.ip, 192,168,100,1);
    IP4_ADDR(&ipInfo.gw, 192,168,100,1);
    IP4_ADDR(&ipInfo.netmask ,255,255,255,0);

    esp_netif_dhcps_stop(esp_netif_ap);
    esp_netif_set_ip_info(esp_netif_ap, &ipInfo);
    esp_netif_dhcps_start(esp_netif_ap);

    return  ESP_OK;
}

static void scan_task(void* param)
{
    p_wifi_scan_cb callback = (p_wifi_scan_cb)param;
    uint16_t ap_count = 0;
    uint16_t ap_num = 20;
    wifi_ap_record_t *ap_list = (wifi_ap_record_t*)malloc(sizeof(wifi_ap_record_t)*ap_num);

    esp_wifi_scan_start(NULL,true);
    esp_wifi_scan_get_ap_num(&ap_count);
    esp_wifi_scan_get_ap_records(&ap_num,ap_list);
    ESP_LOGI(TAG,"总共有:%d, 实际有:%d",ap_count,ap_num);
    if(callback)
    callback(ap_num,ap_list);
    free(ap_list);
    xSemaphoreGive(scan_sem);
    vTaskDelete(NULL);
}

/**
 * @brief STA+AP打开后扫描网络
 */
esp_err_t wifiap_scan(p_wifi_scan_cb f)
{
    if(pdTRUE == xSemaphoreTake(scan_sem,0))
    {
        esp_wifi_clear_ap_list();
        return xTaskCreatePinnedToCore(scan_task,"scan",8192,f,3,NULL,1);
    }
    return ESP_OK;
}
