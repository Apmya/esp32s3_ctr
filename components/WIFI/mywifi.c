#include <string.h>
#include <stdio.h>
#include "esp_err.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lwip/ip4_addr.h"

#include "mywifi.h"
#include "wifi_config.h"

#define TAG "MYWIFI"

/* 重连工作任务: 所有慢序列(disconnect/stop/start+延时)都在本任务里执行,
 * 调用方(BLE host/httpd等)只发通知, µs级返回, 绝不阻塞网络协议栈任务 */
#define RESTART_TASK_STACK  6144
#define RESTART_TASK_PRIO   5

static const char* ap_ssid_name = "ESP32S3-AP";
static const char* ap_password = "12345678";

static esp_netif_t* esp_netif_ap = NULL;

static SemaphoreHandle_t scan_sem = NULL;

/* 自动重连: 指数退避定时器 */
static esp_timer_handle_t s_reconnect_timer = NULL;
static uint32_t s_reconnect_attempt = 0;

/* 重连工作任务句柄 */
static TaskHandle_t s_restart_task = NULL;

/* STA连接状态: 数据唯一主人在本文件, 外部经 wifi_is_connected() 查询 */
static bool s_wifi_connected = false;

bool wifi_is_connected(void)
{
    return s_wifi_connected;
}

/* 重连回调: 事件回调中不能阻塞延时, 由定时器延迟后发起重连 */
static void wifi_reconnect_timer_cb(void *arg)
{
    (void)arg;
    if (!s_wifi_connected && wifi_config_has_config())
    {
        esp_wifi_connect();
    }
}

/**
 * @brief wifista事件回调
 */
static void wifista_event_handler(void* event_handler_arg,esp_event_base_t event_base,int32_t event_id,void* event_data)
{
    if(event_base == WIFI_EVENT)
    {   
        switch(event_id)
        {
            case WIFI_EVENT_STA_START:
                wifi_mode_t mode;
                esp_wifi_get_mode(&mode);
                if(mode == WIFI_MODE_STA && wifi_config_has_config())
                    esp_wifi_connect();          
                break;
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG,"WiFi Connected");
                s_wifi_connected = true;
                s_reconnect_attempt = 0;   /* 连接成功, 退避计数清零 */
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGI(TAG,"WiFi DisConnected");
                s_wifi_connected = false;
                /* 自动重连: 指数退避 2s→4s→8s→16s→30s(封顶), 无配置不空转 */
                if (s_reconnect_timer && wifi_config_has_config())
                {
                    uint32_t shift = (s_reconnect_attempt > 4) ? 4 : s_reconnect_attempt;
                    uint32_t delay_ms = 2000U << shift;
                    if (delay_ms > 30000)
                    {
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
 * @brief 从 wifi_config 取凭据快照并写入驱动 (凭据不再经全局变量传递)
 */
static void wifi_apply_credential_to_driver(void)
{
    wifi_cred_t cred;
    wifi_config_get_credential(&cred);

    wifi_config_t wifista_config = {0};
    strncpy((char *)wifista_config.sta.ssid, cred.ssid, sizeof(wifista_config.sta.ssid)-1);
    strncpy((char *)wifista_config.sta.password, cred.password, sizeof(wifista_config.sta.password)-1);
    wifista_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    esp_wifi_set_config(WIFI_IF_STA, &wifista_config);
}

/**
 * @brief 重连工作任务: 收到通知后取最新凭据, 在自己的上下文里执行慢序列
 */
static void wifi_restart_worker(void *arg)
{
    (void)arg;

    while (1)
    {
        /* 无阻塞等待; 期间多次 restart 请求自动合并, 每轮都取最新凭据 */
        xTaskNotifyWait(0, 0xFFFFFFFFu, NULL, portMAX_DELAY);

        wifi_cred_t cred;
        wifi_config_get_credential(&cred);

        if (cred.ssid[0] == '\0')
        {
            ESP_LOGW(TAG, "重连请求但无已存WiFi, 忽略");
            continue;
        }

        ESP_LOGI(TAG, "重新连接 SSID:%s", cred.ssid);

        wifi_apply_credential_to_driver();

        wifi_mode_t cur_mode;
        esp_wifi_get_mode(&cur_mode);

        if (cur_mode == WIFI_MODE_STA)
        {
            ESP_LOGI(TAG,"已是STA模式,仅重连不重启wifi驱动");
            esp_wifi_disconnect();
            vTaskDelay(pdMS_TO_TICKS(200));
        }
        else
        {
            /* 非纯STA(含APSTA配网中): 完整回落到 STA-only */
            ESP_LOGI(TAG,"当前模式%d, 完整重启WiFi为STA", (int)cur_mode);
            esp_wifi_disconnect();
            esp_wifi_stop();
            esp_wifi_set_mode(WIFI_MODE_STA);
            vTaskDelay(pdMS_TO_TICKS(200));
            esp_wifi_start();
            vTaskDelay(pdMS_TO_TICKS(100));
        }

        esp_err_t ret = esp_wifi_connect();
        if (ret == ESP_OK)
        {
            ESP_LOGI(TAG, "WiFi连接请求已发起，等待获取IP");
        }
        else
        {
            ESP_LOGE(TAG, "wifi connect fail, ret:%s", esp_err_to_name(ret));
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
    if (esp_timer_create(&reconnect_args, &s_reconnect_timer) != ESP_OK)
    {
        ESP_LOGW(TAG, "重连定时器创建失败, 断开后将无法自动重连");
        s_reconnect_timer = NULL;
    }

    /* 重连工作任务 */
    xTaskCreatePinnedToCore(wifi_restart_worker, "wifi_restart",
                            RESTART_TASK_STACK, NULL, RESTART_TASK_PRIO,
                            &s_restart_task, 1);

    /* 注册 apply 钩子: BLE配网通道 wifi_config_apply() → 本模块异步重连 */
    wifi_config_set_apply_hook(wifista_restart);

    //STA下连接WIFI
    esp_wifi_set_mode(WIFI_MODE_STA);

    if (!wifi_config_has_config())
        ESP_LOGW(TAG, "NVS无保存WiFi");
    wifi_apply_credential_to_driver();

    esp_wifi_start();
}

/**
 * @brief wifi重新连接 (非阻塞): 仅向重连工作任务发通知, 调用方立即返回
 */
void wifista_restart(void)
{
    if (s_restart_task == NULL)
    {
        ESP_LOGE(TAG, "重连任务未创建, 降级为直接下发配置");
        wifi_apply_credential_to_driver();
        return;
    }

    xTaskNotifyGive(s_restart_task);
}

/**
 * @brief 打开STA+AP (AP配网入口)
 *
 * ★ 修复AP"开而不可见": 依据 esp_wifi.h @attention 1
 *   ("set_config 只对已使能的接口生效, 否则失败/丢弃"), 旧顺序先写AP配置
 *   后切模式, 且返回值从未检查 —— 一旦静默失败, softAP 即以空SSID广播,
 *   手机列表自然找不到 ESP32S3-AP。现改为规范序列:
 *   stop → set_mode(使能AP) → set_config → start, 全程检查返回值并回读自检。
 */
esp_err_t wifiap_sta_start(void)
{
    wifi_mode_t mode;
    ESP_RETURN_ON_ERROR(esp_wifi_get_mode(&mode), TAG, "读取wifi模式失败");
    if (mode == WIFI_MODE_APSTA)
    {
        return ESP_OK;
    }

    wifi_config_t ap_cfg = {0};
    ap_cfg.ap.channel        = 5;
    ap_cfg.ap.max_connection = 2;
    ap_cfg.ap.authmode       = WIFI_AUTH_WPA2_PSK;
    strncpy((char *)ap_cfg.ap.ssid, ap_ssid_name, sizeof(ap_cfg.ap.ssid) - 1);
    ap_cfg.ap.ssid_len = strlen(ap_ssid_name);
    strncpy((char *)ap_cfg.ap.password, ap_password, sizeof(ap_cfg.ap.password) - 1);

    /* ① 停下: 让接口进入可重配状态 (此刻STA本就未连接, stop无副作用) */
    esp_err_t ret = esp_wifi_stop();
    if (ret != ESP_OK && ret != ESP_ERR_WIFI_NOT_STARTED)
    {
        ESP_LOGE(TAG, "wifi stop失败: %s", esp_err_to_name(ret));
        return ret;
    }

    /* ② 先使能 AP 接口 */
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_APSTA), TAG, "切换APSTA模式失败");

    /* ③ 接口已使能, 此时写配置才保证生效 */
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg), TAG, "写入AP配置失败");

    /* ④ 重启, softAP 以带名 beacon 广播 */
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start失败");

    esp_netif_ip_info_t ipInfo;
    IP4_ADDR(&ipInfo.ip, 192,168,100,1);
    IP4_ADDR(&ipInfo.gw, 192,168,100,1);
    IP4_ADDR(&ipInfo.netmask ,255,255,255,0);

    esp_netif_dhcps_stop(esp_netif_ap);
    ESP_RETURN_ON_ERROR(esp_netif_set_ip_info(esp_netif_ap, &ipInfo), TAG, "AP网关IP设置失败");
    ESP_RETURN_ON_ERROR(esp_netif_dhcps_start(esp_netif_ap), TAG, "AP DHCP启动失败");

    /* ⑤ 回读自检: 串口日志直接证明SSID生效 */
    wifi_config_t chk = {0};
    if (esp_wifi_get_config(WIFI_IF_AP, &chk) == ESP_OK)
    {
        ESP_LOGI(TAG, "AP已广播: SSID=[%s] ch=%d", (char *)chk.ap.ssid, chk.ap.channel);
    }

    return ESP_OK;
}

static void scan_task(void* param)
{
    p_wifi_scan_cb callback = (p_wifi_scan_cb)param;
    uint16_t ap_count = 0;
    uint16_t ap_num = 20;
    wifi_ap_record_t *ap_list = (wifi_ap_record_t*)malloc(sizeof(wifi_ap_record_t)*ap_num);

    /* OOM 保护: 原来不查NULL直接喂给scan_get_ap_records → 崩溃点 */
    if (ap_list == NULL)
    {
        ESP_LOGE(TAG, "扫描缓冲分配失败, 剩余堆%u", (unsigned)xPortGetFreeHeapSize());
        xSemaphoreGive(scan_sem);
        vTaskDelete(NULL);
        return;
    }

    esp_err_t ret = esp_wifi_scan_start(NULL,true);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "扫描启动失败: %s, 剩余堆%u",
                 esp_err_to_name(ret), (unsigned)xPortGetFreeHeapSize());
    }
    else
    {
        esp_wifi_scan_get_ap_num(&ap_count);
        esp_wifi_scan_get_ap_records(&ap_num,ap_list);
        ESP_LOGI(TAG,"总共有:%d, 实际有:%d, 剩余堆%u",ap_count, ap_num, (unsigned)xPortGetFreeHeapSize());
        vTaskDelay(pdMS_TO_TICKS(500));
        if(callback)    callback(ap_num,ap_list);
    }

    free(ap_list);
    xSemaphoreGive(scan_sem);
    vTaskDelete(NULL);
}

/**
 * @brief STA+AP打开后扫描网络
 */
esp_err_t wifiap_scan(p_wifi_scan_cb f)
{
    if(pdTRUE != xSemaphoreTake(scan_sem,0))
    {
        ESP_LOGW(TAG, "上一次扫描未结束/令牌未归还, 本次请求忽略");
        return ESP_ERR_INVALID_STATE;
    }

    esp_wifi_clear_ap_list();

    BaseType_t ok = xTaskCreatePinnedToCore(scan_task,"scan",8192,f,3,NULL,1);
    if (ok != pdPASS)
    {
        /* 关键修复: 创建失败必须归还令牌, 否则扫描功能被永久禁死(且此前静默) */
        xSemaphoreGive(scan_sem);
        ESP_LOGE(TAG, "扫描任务创建失败! 剩余堆%u 最小块%u",
                 (unsigned)xPortGetFreeHeapSize(),
                 (unsigned)xPortGetMinimumEverFreeHeapSize());
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
