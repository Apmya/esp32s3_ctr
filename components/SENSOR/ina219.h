#ifndef __INA219_H_
#define __INA219_H_

#include <stdint.h>
#include "esp_err.h"

#define INA219_ADDR_7BIT    0x40
#define INA219_WR_ADDR      (INA219_ADDR_7BIT << 1 | 0) // 0x80 写
#define INA219_RD_ADDR      (INA219_ADDR_7BIT << 1 | 1) // 0x81 读

#define INA219_CONFIG   0x00    // 配置寄存器
#define INA219_SHUNT    0X01    // 分流电阻两端电压
#define INA219_BUS      0X02    // 总线电压(IN-->GND)
#define INA219_POWER    0X03    // 功率
#define INA219_CURRENT  0X04    // 回路电流
#define INA219_CALIBR   0X05    // 校准寄存器

// 硬件参数
#define R_SHUNT             0.05f
/* 电流 LSB=1mA, 与 INA219_init() 写入的 CAL=0x0333 及实际采样电阻标定一致(硬件实测验证)。
 * 勿按 R_SHUNT=0.05Ω 推算改成 0.000125A —— 那会使所有电流/功率读数偏小 8 倍
 * (如 IP 0.12A 会显示成 0.015A)。R_SHUNT 仅为参考值, 不代表实际采样电阻 */
#define CURRENT_LSB         0.001f

/* period/duty 纹波测量错误码(测不出时置位, 由 Period DP 上报) */
#define RIPPLE_ERR_OK           0   /* 实测成功 */
#define RIPPLE_ERR_AMP          1   /* 纹波幅度 <3%Ih, 判为无纹波 */
#define RIPPLE_ERR_EDGES        2   /* 上升沿 <3 个 */
#define RIPPLE_ERR_TOO_FAST     3   /* 周期 <2ms 或抖动大: 负载变化快于 INA219 采样能力(12bit=532us/次) */
#define RIPPLE_ERR_RECORD       4   /* 末尾密集记录 <8 点 */
#define RIPPLE_ERR_NONE         5   /* 未进入纹波分析(无峰值/提前结束) */

typedef struct
{
    float Ip;          /* 峰值电流 (A) */
    float Ih;          /* 维持电流 (A) */
    int64_t Tp;        /* IP 维持时间 (us) */
    int64_t period;    /* 纹波周期 (us), 测不出为 0 */
    float duty;        /* 纹波占空比, 测不出为 0 */
    uint8_t ripple_err;/* period/duty 错误码 (RIPPLE_ERR_*), 0=成功 */
} ina219_valve_curr_t;

void INA219_send_cmd(uint8_t cmd_data, uint8_t data_high, uint8_t data_low);
int16_t INA219_read_data(uint8_t cmd_data);

void INA219_init(void);

float ina219_get_shunt_voltage(void);    // 0x01 采样电阻压差(V)
float ina219_get_valve_voltage(void);    // 分压后电磁阀线圈电压(V)
float ina219_get_bus_voltage(void);      // 0x02 24V母线电压(V)
float ina219_get_current(void);          // 0x04 回路电流(A)
float ina219_get_power(void);            // 0x03 实时功率(W)

esp_err_t ina219_measure_valve_current(ina219_valve_curr_t *res,void (*valve_open_func)(void),void (*valve_close_func)(void));

#endif
