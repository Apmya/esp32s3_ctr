#ifndef MYNVS_H
#define MYNVS_H

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

/* =====================================================================
 * MYNVS —— 底层存储层 (纯 KV 工具箱)
 *
 * 架构定位:  Application/业务  →  *_config 配置层  →  MYNVS  →  Flash
 *
 * 本层零业务知识: 不定义任何 namespace/key 字符串, 不知道 ssid/密码
 * 的存在。键名由上层业务配置模块 (如 WIFI/wifi_config.c) 私有持有。
 *
 * ★ 业务模块禁止直接 include 本头文件,
 *   持久化需求一律经各自的 *_config 模块转发。
 * ===================================================================== */

/**
 * @brief 初始化 NVS 分区 (含损坏自动擦除重建)
 */
esp_err_t mynvs_init(void);


/**
 * @brief 保存字符串
 */
esp_err_t mynvs_save_string(const char *ns, const char *key, const char *value);


/**
 * @brief 读取字符串
 *
 * @param value   接收缓冲区
 * @param max_len 缓冲区大小 (含结尾 '\0')
 */
esp_err_t mynvs_load_string(const char *ns, const char *key, char *value, size_t max_len);


/**
 * @brief 保存 uint32
 */
esp_err_t mynvs_save_u32(const char *ns, const char *key, uint32_t value);


/**
 * @brief 读取 uint32
 */
esp_err_t mynvs_load_u32(const char *ns, const char *key, uint32_t *value);


/**
 * @brief 保存二进制块 (结构体/校准数据等)
 */
esp_err_t mynvs_save_blob(const char *ns, const char *key, const void *data, size_t len);


/**
 * @brief 读取二进制块
 *
 * @param out_len 实际长度输出, 不需要可传 NULL
 */
esp_err_t mynvs_load_blob(const char *ns, const char *key, void *buf, size_t max_len, size_t *out_len);


/**
 * @brief 删除指定 key
 */
esp_err_t mynvs_delete_key(const char *ns, const char *key);


/**
 * @brief 清空指定 namespace
 */
esp_err_t mynvs_clear_namespace(const char *ns);

#endif
