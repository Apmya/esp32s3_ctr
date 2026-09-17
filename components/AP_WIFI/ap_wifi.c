#include "esp_spiffs.h"
#include <sys/stat.h>
#include "cJSON.h"
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "ap_wifi.h"
#include "mywifi.h"
#include "ws_server.h"
#include "wifi_config.h"

#define TAG     "ap_wifi"

#define SPIFF_MOUNT     "/spiffs"
#define HTML_PATH       "/spiffs/apcfg.html"

static char* html_code = NULL;
static EventGroupHandle_t apcfg_ev;
#define APCFG_BIT   (BIT0)

static char* init_web_page_buffer(void)
{
    esp_vfs_spiffs_conf_t conf = 
    {
        .base_path = SPIFF_MOUNT,
        .format_if_mount_failed = false,
        .max_files = 3,
        .partition_label = NULL,
    };
    esp_vfs_spiffs_register(&conf);
    struct stat st;
    if(stat(HTML_PATH,&st))
    {
        return NULL;
    }
    char* buf = (char*)malloc(st.st_size + 1);
    memset(buf , 0 ,st.st_size + 1);
    FILE *fp = fopen(HTML_PATH,"r");
    if(fp)
    {
        if(fread(buf,st.st_size,1,fp) == 0)
        {
            free(buf);
            buf = NULL;
        }
        fclose(fp);
    }else
    {
        free(buf);
        buf = NULL;
    }
    return buf;
}

static void ap_wifi_task(void* param)
{
    EventBits_t ev;
    while(1)
    {
        ev = xEventGroupWaitBits(apcfg_ev,APCFG_BIT,pdTRUE,pdFALSE,pdMS_TO_TICKS(10*1000));
        if(ev & APCFG_BIT)
        {
            /* ap_wifi_stop(): 关WS服务器→关AP→切STA→重连 */
            ap_wifi_stop();
        }
    }
}

void ap_wifi_init()
{
    wifista_init();
    html_code = init_web_page_buffer();
    apcfg_ev = xEventGroupCreate();
    xTaskCreatePinnedToCore(ap_wifi_task,"apcfg",8192,NULL,3,NULL,1);
    ESP_LOGI(TAG,"wifista+ap初始化成功");
}

/* ── 配网策略环 (原 main.c ap_cfg_task 迁入): 断网开AP / 连网关AP ──
 * 只读 wifi_config / mywifi 的 API, 不持有任何共享数据 */
static void ap_cfg_watch_task(void *arg)
{
    (void)arg;
    bool is_ap_active = false; // 记录AP服务是否已启动

    while(1)
    {
        if ((!wifi_config_has_config() || !wifi_is_connected()) && !is_ap_active)
        {
            ESP_LOGW(TAG, "WiFi未连接,启动AP网页配网...");
            ap_wifi_apcfg(); 
            is_ap_active = true; 
        }
        else if (wifi_is_connected() && is_ap_active)
        {
            ESP_LOGI(TAG, "WiFi已连接,关闭AP配网服务...");
            ap_wifi_stop();
            is_ap_active = false; 
        }
        
        vTaskDelay(pdMS_TO_TICKS(3000)); 
    }
}

void ap_wifi_watch_start(void)
{
    xTaskCreatePinnedToCore(ap_cfg_watch_task,"ap_cfg_watch",8192,NULL,4,NULL,1);
    ESP_LOGI(TAG,"配网策略环已启动");
}


/* 扫描结果处理: 仅本文件 wifiap_scan 回调使用 */
static void wifi_scan_handle(int num ,wifi_ap_record_t *ap_records)
{
    cJSON* root = cJSON_CreateObject();
    cJSON* wifilist_js = cJSON_AddArrayToObject(root , "wifi_list");
    for(int i = 0;i < num; i++)
    {
        cJSON* wifi_js = cJSON_CreateObject();
        cJSON_AddStringToObject(wifi_js,"ssid",(char*)ap_records[i].ssid);
        cJSON_AddNumberToObject(wifi_js,"rssi",ap_records[i].rssi);
        if(ap_records[i].authmode == WIFI_AUTH_OPEN)
            cJSON_AddBoolToObject(wifi_js,"encrypted",0);
        else 
            cJSON_AddBoolToObject(wifi_js,"encrypted",1);
        cJSON_AddItemToArray(wifilist_js, wifi_js);
    }
    /* 紧凑JSON: 帧更小、单包发出, 降低弱链路下丢帧概率 */
    char* data = cJSON_PrintUnformatted(root);
    esp_err_t snd = web_ws_send((uint8_t*)data, strlen(data));
    if (snd != ESP_OK)
    {
        ESP_LOGE(TAG, "扫描结果回发失败(手机链路此刻已断?): %s", esp_err_to_name(snd));
    }
    else
    {
        ESP_LOGI(TAG, "扫描结果已回发 %d 字节", (int)strlen(data));
    }
    cJSON_free(data);
    cJSON_Delete(root);
}

static void ws_receive_handle(uint8_t *payload, int len)
{   
    cJSON* root = cJSON_Parse((char*)payload);
    if(root)
    {
        cJSON* scan_js = cJSON_GetObjectItem(root,"scan");
        cJSON* ssid_js = cJSON_GetObjectItem(root,"ssid");
        cJSON* password_js = cJSON_GetObjectItem(root,"password");
        if(scan_js)
        {
            char* scan_value = cJSON_GetStringValue(scan_js);
            if(strcmp(scan_value , "start") == 0)
            {
                //scan
                esp_err_t r = wifiap_scan(wifi_scan_handle);
                if (r != ESP_OK)
                {
                    ESP_LOGE(TAG, "扫描请求受理失败: %s", esp_err_to_name(r));
                }
            }
        }
        if(ssid_js && password_js)
        {
            char* ssid_value = cJSON_GetStringValue(ssid_js);
            char* password_value = cJSON_GetStringValue(password_js);
            /* AP通道只存配置(wifi_config_save): 关AP与重连由本模块
             * ap_wifi_task→ap_wifi_stop() 自己调度, 因此不走 apply 钩子,
             * 避免与 BLE 通道重复触发重连 */
            esp_err_t ret = wifi_config_save(ssid_value, password_value);

            
            if (ret == ESP_OK)
            {
                xEventGroupSetBits(apcfg_ev,APCFG_BIT);
            }
        }
    }
}

void ap_wifi_apcfg()
{
    wifiap_sta_start();
    ws_cfg_t ws_cfg = 
    {
        .html_code = html_code,
        .receive_fn = ws_receive_handle,
    };
    web_ws_start(&ws_cfg);
    ESP_LOGI(TAG, "开始AP网页配网服务");
}

void ap_wifi_stop(void)
{
    ESP_LOGI(TAG, "开始关闭AP配网服务");
    web_ws_stop();

    wifi_mode_t cur_mode;
    esp_wifi_get_mode(&cur_mode);
    if(cur_mode == WIFI_MODE_APSTA)
    {
        esp_wifi_stop();
        esp_wifi_set_mode(WIFI_MODE_STA);
        esp_wifi_start();
        // 重新发起STA连接 (凭据已由 wifi_config 写入驱动)
        esp_wifi_connect();
    }

    ESP_LOGI(TAG, "AP配网服务已关闭");
}
