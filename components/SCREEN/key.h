#ifndef __KEY_H
#define __KEY_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/* =====================================================================
 * KEY —— 按键输入层 (KEY1~5, board.h 已定义引脚)
 *
 * 链路: GPIO ISR → 队列(无内存分配) → 按键任务消抖 → 用户回调
 *
 * ★ 响应性保证 (网络故障隔离):
 *   - 回调在按键任务上下文执行, 该任务只碰 队列+valve 级 µs 锁;
 *   - 按键路径与 WiFi/BLE/MQTT 的锁集合零交集,
 *     网络断开/弱网阻塞不可能传导到按键事件。
 *
 * ★ 电气假设: 按键低电平有效 (按下=0), 内部上拉; 不符请告知改配置。
 * ★ A1~A9 输出不属本模块 —— 按约定由你在 VALUE 中实现。
 * ===================================================================== */

typedef enum
{
    KEY_ID_1 = 0,
    KEY_ID_2,
    KEY_ID_3,
    KEY_ID_4,
    KEY_ID_5,
    KEY_ID_MAX
} key_id_t;

typedef enum
{
    KEY_EVT_PRESS   = 0,   /* 按下沿 (消抖后) */
    KEY_EVT_RELEASE = 1,   /* 释放沿 (消抖后) */
} key_evt_t;

/* 事件回调: 运行于按键任务上下文, 可安全做 µs~ms 级轻操作;
 * 禁止在回调里做网络调用/长阻塞 (重活请再投给自己的队列) */
typedef void (*key_event_cb_t)(key_id_t id, key_evt_t evt);

/**
 * @brief 按键初始化: GPIO输入+双沿中断+消抖任务
 */
esp_err_t key_init(void);

/**
 * @brief 注册事件回调 (单订阅者; NULL 注销)
 */
esp_err_t key_register_callback(key_event_cb_t cb);

#endif
