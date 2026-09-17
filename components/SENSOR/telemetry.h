#ifndef __TELEMETRY_H
#define __TELEMETRY_H

#include "esp_err.h"

/* =====================================================================
 * TELEMETRY —— 传感器聚合数据 (电压/电流/功率/温度) 的唯一主人
 *
 * 原状: main.c 裸定义 vbus/cur/power/temp + data_mutex, 多处 extern 共享。
 * 现况: 数据私有于 telemetry.c, 外部只走两个 API:
 *   - 采集者(iic_collect_task)  → telemetry_update()   单写者注入
 *   - 消费者(tuya_dm 上报/将来lcd) → telemetry_snapshot() 原子快照
 *
 * 锁纪律: 临界区仅一次 4-float 结构体拷贝, µs 级, 与按键路径无关。
 * ===================================================================== */

typedef struct
{
    float vbus_v;    /* 母线电压 V   */
    float cur_a;     /* 电流 A       */
    float power_w;   /* 功率 W       */
    float temp_c;    /* NTC 温度 °C  */
} telemetry_t;

/**
 * @brief 采集任务注入最新一组数据 (单写者)
 */
void telemetry_update(float vbus_v, float cur_a, float power_w, float temp_c);

/**
 * @brief 读取同代原子快照 (out 由调用方提供; 尚无数据时为全 0)
 */
void telemetry_snapshot(telemetry_t *out);

/**
 * @brief 启动采集任务 (2s 周期读 INA219/NTC 并注入):
 *        任务参数: prio 3, core1, 栈 8192 —— 须在 iic_dev_start/ntc_init 之后调用
 */
void telemetry_start(void);

#endif
