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
#include "mynvs.h"

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


void wifi_scan_handle(int num ,wifi_ap_record_t *ap_records)
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
    char* data = cJSON_Print(root);
    ESP_LOGI(TAG,"WS send:%s", data);
    web_ws_send((uint8_t*)data, strlen(data));
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
                wifiap_scan(wifi_scan_handle);
            }
        }
        if(ssid_js && password_js)
        {
            char* ssid_value = cJSON_GetStringValue(ssid_js);
            char* password_value = cJSON_GetStringValue(password_js);
            esp_err_t ret = wifi_nvs_save(ssid_value, password_value);

            
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
    ESP_LOGI(TAG, "开始AP配网服务");
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
        // 重新发起STA连接
        esp_wifi_connect();
    }

    ESP_LOGI(TAG, "AP配网服务已关闭");
}