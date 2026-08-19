#include "mynvs.h"
#include "nvs_flash.h"
#include <string.h>
#include "esp_log.h"


static const char *TAG = "NVS_WIFI";
#define WIFI_NVS_NAMESPACE "wifi_config"
#define NVS_KEY_SSID       "ssid_store"
#define NVS_KEY_PWD        "pwd_store"

char g_wifi_ssid[SSID_MAX_LEN] = {0};
char g_wifi_passwd[PWD_MAX_LEN] = {0};


/**
 * @brief 上电读取保存的WiFi
 */
esp_err_t wifi_nvs_load(void)
{
    nvs_handle_t nvs_hdl;
    esp_err_t ret = nvs_open(WIFI_NVS_NAMESPACE, NVS_READONLY, &nvs_hdl);
    if(ret != ESP_OK)
    {
        memset(g_wifi_ssid, 0, SSID_MAX_LEN);
        memset(g_wifi_passwd, 0, PWD_MAX_LEN);
        return ret;
    }

    size_t str_len = SSID_MAX_LEN;
    ret = nvs_get_str(nvs_hdl, NVS_KEY_SSID, g_wifi_ssid, &str_len);
    if(ret != ESP_OK)
    {
        memset(g_wifi_ssid, 0, SSID_MAX_LEN);
    }

    str_len = PWD_MAX_LEN;
    ret = nvs_get_str(nvs_hdl, NVS_KEY_PWD, g_wifi_passwd, &str_len);
    if(ret != ESP_OK)
    {
        memset(g_wifi_passwd, 0, PWD_MAX_LEN);
    }

    nvs_close(nvs_hdl);

    if(strlen(g_wifi_ssid) > 0)
    {
        ESP_LOGI(TAG, "上电加载WiFi SSID:%s", g_wifi_ssid);
    }
    
    return ESP_OK;
}

/**
 * @brief 保存WiFi账号密码到Flash
 */
esp_err_t wifi_nvs_save(const char* ssid, const char* pwd)
{
    if(ssid == NULL || pwd == NULL)
    {
        ESP_LOGE(TAG, "保存WiFi参数为空");
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_hdl;
    esp_err_t ret = nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &nvs_hdl);
    if(ret != ESP_OK)
    {
        ESP_LOGE(TAG, "NVS打开失败");
        return ret;
    }

    nvs_set_str(nvs_hdl, NVS_KEY_SSID, ssid);
    nvs_set_str(nvs_hdl, NVS_KEY_PWD, pwd);
    ret = nvs_commit(nvs_hdl);
    nvs_close(nvs_hdl);

    if(ret == ESP_OK)
    {
        ESP_LOGI(TAG, "WiFi已保存至Flash");
        strncpy(g_wifi_ssid, ssid, SSID_MAX_LEN - 1);
        g_wifi_ssid[SSID_MAX_LEN - 1] = '\0';

        strncpy(g_wifi_passwd, pwd, PWD_MAX_LEN - 1);
        g_wifi_passwd[PWD_MAX_LEN - 1] = '\0';
    }
    return ret;
}


/**
 * @brief 清空存储的flash
 */
void wifi_nvs_clear(void)
{
    nvs_handle_t nvs_hdl;
    if(nvs_open(WIFI_NVS_NAMESPACE, NVS_READWRITE, &nvs_hdl) == ESP_OK)
    {
        nvs_erase_all(nvs_hdl);
        nvs_commit(nvs_hdl);
        nvs_close(nvs_hdl);
    }
    memset(g_wifi_ssid, 0, SSID_MAX_LEN);
    memset(g_wifi_passwd, 0, PWD_MAX_LEN);
    ESP_LOGI(TAG, "已清空保存的WiFi信息");
}