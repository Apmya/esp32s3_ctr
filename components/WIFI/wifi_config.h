#ifndef WIFI_CONFIG_H
#define WIFI_CONFIG_H

#include <stdbool.h>
#include "esp_err.h"

/* =====================================================================
 * WIFI 配置层 —— WiFi 凭据数据的唯一主人
 *
 * 架构分层:  业务(MYBLE/AP_WIFI/mywifi/main)  →  wifi_config  →  MYNVS
 *
 * 所有权规则:
 *   - g_cred 缓存与 NVS 键名 (namespace/key) 全部私有于本模块;
 *   - 外部只能通过本头文件 API 访问, 拿到的永远是拷贝快照;
 *   - save  = 仅持久化 + 更新内存缓存 (AP配网页使用, 重连由调用方自行调度);
 *   - apply = save + 触发 apply 钩子 (BLE配网页使用, 钩子由 mywifi 注册,
 *             内部仅向重启工作任务发通知, 调用方 µs 级返回)。
 *
 * 锁纪律: 临界区仅覆盖 RAM 拷贝 (µs 级), flash 写入全部在锁外,
 *         因此本锁与按键/UI 任务不存在任何可传导的阻塞路径。
 * ===================================================================== */

#define WIFI_SSID_MAX_LEN   64
#define WIFI_PWD_MAX_LEN    64

/* 凭据同代快照: ssid + password 一次性拷贝, 不会出现新旧混搭 */
typedef struct
{
    char ssid[WIFI_SSID_MAX_LEN];
    char password[WIFI_PWD_MAX_LEN];
} wifi_cred_t;

/* apply 钩子类型 (mywifi 提供: 内部为"通知重启任务", 非阻塞) */
typedef void (*wifi_config_apply_cb_t)(void);


/**
 * @brief 上电从 NVS 加载凭据到内存缓存 (须在 mynvs_init 之后、任务启动前调用)
 */
esp_err_t wifi_config_load(void);


/**
 * @brief 保存凭据 (NVS 落盘 + 更新缓存), 不触发重连
 */
esp_err_t wifi_config_save(const char *ssid, const char *password);


/**
 * @brief 保存凭据并触发 apply 钩子 (BLE 配网收齐 SSID+密码后调用)
 */
esp_err_t wifi_config_apply(const char *ssid, const char *password);


/**
 * @brief 清除凭据 (NVS namespace + 内存缓存)
 */
esp_err_t wifi_config_clear(void);


/**
 * @brief 是否持有有效 WiFi 配置 (按键/主循环判断是否开 AP 配网用)
 */
bool wifi_config_has_config(void);


/**
 * @brief 取凭据原子快照 (out 由调用方提供; 无配置时 ssid 为空串)
 */
void wifi_config_get_credential(wifi_cred_t *out);


/**
 * @brief 注册/注销 apply 钩子 (传 NULL 注销; 由 mywifi 在 wifista_init 调用)
 */
void wifi_config_set_apply_hook(wifi_config_apply_cb_t hook);

#endif
