#include "driver/gpio.h"
#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include <string.h>
#include "esp_wifi.h"

#include "myble.h"
#include "mywifi.h"
#include "mynvs.h"
#include "ap_wifi.h"

#define TAG "NimBLE"
#define DEVICE_NAME "ESP32S3-NimBLE"
static bool ble_adv_active = false;
static uint16_t rx_value_handler;
static uint16_t tx_value_handler;

// 状态标记
bool ble_connected_flag = false;


/**
 * @brief GATT指令回调
 */
static int gatt_event_handler(uint16_t conn_handle, uint16_t attr_handle,struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if(ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR)
    {
        if(attr_handle == rx_value_handler){
            uint16_t data_len = ctxt->om->om_len;
            uint8_t *data_buf = ctxt->om->om_data;
            if (data_len == 0)
            {
                ESP_LOGW(TAG, "未收到消息");
                return 0;
            }

            uint8_t cmd = data_buf[0];
            uint16_t str_len = data_len - 1;
            char recv_buf[128] = {0};
            if (str_len > 0)
            {
                if(str_len > sizeof(recv_buf) - 1)      
                    str_len = sizeof(recv_buf) - 1;
                memcpy(recv_buf, &data_buf[1], str_len);
            }

            switch (cmd)
            {
                case 0x30:  // 0
                    // WiFi名称
                    memset(g_wifi_ssid, 0, SSID_MAX_LEN);
                    strncpy(g_wifi_ssid, recv_buf, SSID_MAX_LEN - 1);
                    ESP_LOGI(TAG, "Get WiFi SSID: [%s]", g_wifi_ssid);
                    break;
                case 0x31:  //  1
                    // WiFi密码
                    // 判断是否只有单个指令字符 "1",无附加密码
                    if(str_len == 0)
                    {
                        wifista_restart();
                    }
                    else
                    {
                        memset(g_wifi_passwd, 0, PWD_MAX_LEN);
                        strncpy(g_wifi_passwd, recv_buf, PWD_MAX_LEN - 1);
                        ESP_LOGI(TAG, "获取 WiFi PWD: [%s]", g_wifi_passwd);
                        wifi_nvs_save(g_wifi_ssid, g_wifi_passwd);

                        ble_gap_adv_stop();
                        ble_adv_active = false;
                        wifista_restart();
                    }
                    break;
                case 0x32:  //  2
                    wifi_nvs_clear();
                    esp_wifi_disconnect();
                    esp_wifi_stop();
                    break;

                default:
                    ESP_LOGW(TAG, "未定义码: 0x%02X", cmd);
                    break;
            }
        }
    }
    else if(ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR)
    {
        if(attr_handle == tx_value_handler)
        {
            char ble_str[32], wifi_str[32];

            // 拼接蓝牙状态
            if(ble_connected_flag)
                snprintf(ble_str, sizeof(ble_str), "BLE:Connected ");
            else
                snprintf(ble_str, sizeof(ble_str), "BLE:Disconnect ");

            os_mbuf_append(ctxt->om, ble_str, strlen(ble_str));
            
            // 拼接WiFi状态
            if(wifi_is_connected)
                snprintf(wifi_str, sizeof(wifi_str), "WiFi:Online");
            else
                snprintf(wifi_str, sizeof(wifi_str), "WiFi:Offline");

            // 发送给APP
            os_mbuf_append(ctxt->om, wifi_str, strlen(wifi_str));


            // const char *value = "HELLO";
            // os_mbuf_append(ctxt->om,value,strlen(value));    
        }
    }
    return 0 ;
}


/**
 * @brief GAP指令回调
 */
static int gap_event_handler(struct ble_gap_event *event, void *arg)
{
    if(event->type == BLE_GAP_EVENT_CONNECT)
    {
        if(event->connect.status == 0)
        {
            ESP_LOGI(TAG,"设备已连接");
            ble_adv_active = false;
            ble_connected_flag = true;
        }
        else
        {
            ESP_LOGI(TAG,"连接失败,重新广播");
            if(!ble_adv_active)
            {
                vTaskDelay(pdMS_TO_TICKS(300));
                start_advertising();
                ble_connected_flag = false;
            }    
        }
    }
    else if(event->type == BLE_GAP_EVENT_DISCONNECT)
    {
        ESP_LOGI(TAG,"设备断开连接");
        if(!ble_adv_active)
        {
            vTaskDelay(pdMS_TO_TICKS(300));
            start_advertising();
            ble_connected_flag = false; 
        }
    }
    return 0;
}


static const struct ble_gatt_svc_def gatt_svcs[] = 
{
    {
        .type =  BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid =  BLE_UUID16_DECLARE(0x00FF),
        .characteristics = (struct ble_gatt_chr_def[])
        {
            //ESP32接受手机数据
            {
                .uuid = BLE_UUID16_DECLARE(0xFF01),
                .access_cb = gatt_event_handler,
                .flags = BLE_GATT_CHR_F_WRITE,
                .val_handle = &rx_value_handler,
                .arg = NULL,              
            },
            //ESP32向手机发送数据
            {
                .uuid = BLE_UUID16_DECLARE(0xFF02),
                .access_cb = gatt_event_handler,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &tx_value_handler,
                .arg = NULL,  
            },
            { 0 }
        }
    },
    { 0 }
};


/**
 * @brief 开启广播
 */
void start_advertising(void)
{
    struct ble_hs_adv_fields fields = {0};
    fields.name = (uint8_t *)DEVICE_NAME;
    fields.name_len = strlen(DEVICE_NAME);
    fields.name_is_complete = 1;
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl =  BLE_HS_ADV_TX_PWR_LVL_AUTO;


    int rc = ble_gap_adv_set_fields(&fields);
    if(rc != 0)
    {
        ESP_LOGE(TAG,"广播内容设置失败");
        return;
    }

    struct ble_gap_adv_params adv_params = {
        .conn_mode = BLE_GAP_CONN_MODE_UND,
        .disc_mode = BLE_GAP_DISC_MODE_GEN,
        .itvl_min =  BLE_GAP_ADV_ITVL_MS(200),
        .itvl_max =  BLE_GAP_ADV_ITVL_MS(500),
    };

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL,BLE_HS_FOREVER, &adv_params,&gap_event_handler,NULL);
    if(rc != 0)
    {
        ESP_LOGE(TAG,"广播启动失败");
    }
    else
    {
        ESP_LOGI(TAG,"广播已启动");
        ble_adv_active = true;
    }
}

/**
 * @brief 初始化后启动广播
 */
static void on_sync(void)
{
    ESP_LOGI(TAG,"设备初始化完毕");
    start_advertising();

}

void host_task( void * arg)
{
    nimble_port_run();
}


/**
 * @brief 蓝牙初始化
 */
void ble_init(void)
{
    nimble_port_init();
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set(DEVICE_NAME);
    ble_gatts_count_cfg(gatt_svcs);
    ble_gatts_add_svcs(gatt_svcs);

    ble_hs_cfg.sync_cb = on_sync;

    nimble_port_freertos_init(host_task);
    ESP_LOGI(TAG,"蓝牙初始化成功");
}