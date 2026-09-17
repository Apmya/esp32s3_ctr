#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"

#include "mynvs.h"
#include "wifi_config.h"

#define TAG "WIFI_CFG"


/* ── NVS 键名: 本模块私有, 全工程唯一定义处 ──
 * namespace 值与重构前 (nvs_config.h) 保持一致 → 存量设备配网记录不丢失 */
#define WIFI_NVS_NAMESPACE  "wifi_config"
#define WIFI_NVS_KEY_SSID   "ssid"
#define WIFI_NVS_KEY_PWD    "password"


/* ── 主人数据: 对外永不暴露指针, 只暴露拷贝快照 ── */
static wifi_cred_t g_cred;
static SemaphoreHandle_t g_lock = NULL;
static wifi_config_apply_cb_t g_apply_hook = NULL;


static void cfg_lock(void)
{
    if (g_lock)
    {
        xSemaphoreTake(g_lock, portMAX_DELAY);
    }
}


static void cfg_unlock(void)
{
    if (g_lock)
    {
        xSemaphoreGive(g_lock);
    }
}


esp_err_t wifi_config_load(void)
{
    if (g_lock == NULL)
    {
        g_lock = xSemaphoreCreateMutex();
        configASSERT(g_lock != NULL);
    }

    /* flash 读取(可能慢) 全部在锁外, 攒进局部变量 */
    wifi_cred_t local;
    memset(&local, 0, sizeof(local));

    esp_err_t ret = mynvs_load_string(
        WIFI_NVS_NAMESPACE, WIFI_NVS_KEY_SSID,
        local.ssid, sizeof(local.ssid));

    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "读取SSID失败: %s", esp_err_to_name(ret));
        local.ssid[0] = '\0';
    }

    ret = mynvs_load_string(
        WIFI_NVS_NAMESPACE, WIFI_NVS_KEY_PWD,
        local.password, sizeof(local.password));

    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "读取WiFi密码失败: %s", esp_err_to_name(ret));
        local.password[0] = '\0';
    }

    /* 只有 µs 级的 RAM 赋值在锁内 */
    cfg_lock();
    memcpy(&g_cred, &local, sizeof(g_cred));
    cfg_unlock();

    if (local.ssid[0] != '\0')
    {
        ESP_LOGI(TAG, "加载WiFi SSID: %s", local.ssid);
    }

    return ESP_OK;
}


esp_err_t wifi_config_save(const char *ssid, const char *password)
{
    if (ssid == NULL || password == NULL)
    {
        ESP_LOGE(TAG, "WiFi参数为空");
        return ESP_ERR_INVALID_ARG;
    }

    if (strlen(ssid) >= WIFI_SSID_MAX_LEN || strlen(password) >= WIFI_PWD_MAX_LEN)
    {
        ESP_LOGE(TAG, "WiFi参数超长");
        return ESP_ERR_INVALID_SIZE;
    }

    /* ① flash 落盘在锁外: 即使写 flash 慢, 也不会拖住任何 getter */
    esp_err_t ret = mynvs_save_string(WIFI_NVS_NAMESPACE, WIFI_NVS_KEY_SSID, ssid);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "保存SSID失败: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = mynvs_save_string(WIFI_NVS_NAMESPACE, WIFI_NVS_KEY_PWD, password);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "保存WiFi密码失败: %s", esp_err_to_name(ret));
        return ret;
    }

    /* ② 落盘成功后, 锁内仅做 RAM 缓存刷新 */
    cfg_lock();
    strncpy(g_cred.ssid, ssid, sizeof(g_cred.ssid) - 1);
    g_cred.ssid[sizeof(g_cred.ssid) - 1] = '\0';
    strncpy(g_cred.password, password, sizeof(g_cred.password) - 1);
    g_cred.password[sizeof(g_cred.password) - 1] = '\0';
    cfg_unlock();

    ESP_LOGI(TAG, "WiFi配置保存成功");

    return ESP_OK;
}


esp_err_t wifi_config_apply(const char *ssid, const char *password)
{
    esp_err_t ret = wifi_config_save(ssid, password);

    if (ret == ESP_OK && g_apply_hook)
    {
        g_apply_hook();   /* 钩子内部只发任务通知, µs 级返回 */
    }

    return ret;
}


esp_err_t wifi_config_clear(void)
{
    esp_err_t ret = mynvs_clear_namespace(WIFI_NVS_NAMESPACE);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "清除WiFi NVS失败: %s", esp_err_to_name(ret));
        return ret;
    }

    cfg_lock();
    memset(&g_cred, 0, sizeof(g_cred));
    cfg_unlock();

    ESP_LOGI(TAG, "WiFi配置已清除");

    return ESP_OK;
}


bool wifi_config_has_config(void)
{
    cfg_lock();
    bool has = (g_cred.ssid[0] != '\0');
    cfg_unlock();

    return has;
}


void wifi_config_get_credential(wifi_cred_t *out)
{
    if (out == NULL)
    {
        return;
    }

    cfg_lock();
    memcpy(out, &g_cred, sizeof(*out));
    cfg_unlock();
}


void wifi_config_set_apply_hook(wifi_config_apply_cb_t hook)
{
    g_apply_hook = hook;
}
