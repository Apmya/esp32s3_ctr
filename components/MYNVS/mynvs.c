#include "mynvs.h"

#include <string.h>

#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"

static const char *TAG = "MYNVS";


/* 内部工具: 统一 open, 失败打日志 */
static esp_err_t ns_open(const char *ns, nvs_open_mode_t mode, nvs_handle_t *h)
{
    if (ns == NULL || h == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = nvs_open(ns, mode, h);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "打开NVS[%s]失败: %s", ns, esp_err_to_name(ret));
    }

    return ret;
}


/**
 * @brief 初始化 NVS
 */
esp_err_t mynvs_init(void)
{
    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_LOGW(TAG, "NVS分区需要擦除");

        ret = nvs_flash_erase();

        if (ret != ESP_OK)
        {
            return ret;
        }

        ret = nvs_flash_init();
    }

    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "NVS初始化成功");
    }

    return ret;
}


esp_err_t mynvs_save_string(const char *ns, const char *key, const char *value)
{
    if (ns == NULL || key == NULL || value == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;

    esp_err_t ret = ns_open(ns, NVS_READWRITE, &handle);

    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = nvs_set_str(handle, key, value);

    if (ret == ESP_OK)
    {
        ret = nvs_commit(handle);
    }

    nvs_close(handle);

    return ret;
}


esp_err_t mynvs_load_string(const char *ns, const char *key, char *value, size_t max_len)
{
    if (ns == NULL || key == NULL || value == NULL || max_len == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;

    esp_err_t ret = ns_open(ns, NVS_READONLY, &handle);

    if (ret != ESP_OK)
    {
        return ret;
    }

    size_t length = max_len;

    ret = nvs_get_str(handle, key, value, &length);

    nvs_close(handle);

    return ret;
}


esp_err_t mynvs_save_u32(const char *ns, const char *key, uint32_t value)
{
    if (ns == NULL || key == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;

    esp_err_t ret = ns_open(ns, NVS_READWRITE, &handle);

    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = nvs_set_u32(handle, key, value);

    if (ret == ESP_OK)
    {
        ret = nvs_commit(handle);
    }

    nvs_close(handle);

    return ret;
}


esp_err_t mynvs_load_u32(const char *ns, const char *key, uint32_t *value)
{
    if (ns == NULL || key == NULL || value == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;

    esp_err_t ret = ns_open(ns, NVS_READONLY, &handle);

    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = nvs_get_u32(handle, key, value);

    nvs_close(handle);

    return ret;
}


esp_err_t mynvs_save_blob(const char *ns, const char *key, const void *data, size_t len)
{
    if (ns == NULL || key == NULL || data == NULL || len == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;

    esp_err_t ret = ns_open(ns, NVS_READWRITE, &handle);

    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = nvs_set_blob(handle, key, data, len);

    if (ret == ESP_OK)
    {
        ret = nvs_commit(handle);
    }

    nvs_close(handle);

    return ret;
}


esp_err_t mynvs_load_blob(const char *ns, const char *key, void *buf, size_t max_len, size_t *out_len)
{
    if (ns == NULL || key == NULL || buf == NULL || max_len == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;

    esp_err_t ret = ns_open(ns, NVS_READONLY, &handle);

    if (ret != ESP_OK)
    {
        return ret;
    }

    size_t length = max_len;

    ret = nvs_get_blob(handle, key, buf, &length);

    if (ret == ESP_OK && out_len != NULL)
    {
        *out_len = length;
    }

    nvs_close(handle);

    return ret;
}


esp_err_t mynvs_delete_key(const char *ns, const char *key)
{
    if (ns == NULL || key == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;

    esp_err_t ret = ns_open(ns, NVS_READWRITE, &handle);

    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = nvs_erase_key(handle, key);

    if (ret == ESP_OK)
    {
        ret = nvs_commit(handle);
    }

    nvs_close(handle);

    return ret;
}


esp_err_t mynvs_clear_namespace(const char *ns)
{
    if (ns == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;

    esp_err_t ret = ns_open(ns, NVS_READWRITE, &handle);

    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = nvs_erase_all(handle);

    if (ret == ESP_OK)
    {
        ret = nvs_commit(handle);
    }

    nvs_close(handle);

    return ret;
}
