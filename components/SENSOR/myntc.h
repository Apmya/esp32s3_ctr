#ifndef __MYNTC_H
#define __MYNTC_H

#include <stdint.h>
#include "driver/adc.h"
#include "board.h"     /* NTC_PIN / NTC_ADC_CHANNEL 唯一来源 */

/* ═══════════════ NTC 硬件电路参数 (按实际硬件修改) ═══════════════
 * 电路: VCC ── 10.5kΩ固定电阻 ──┬── NTC(10kΩ@25°C, B=3950) ── GND
 *                            └── 电压跟随器 ── RC滤波 ── ADC(引脚见 board.h NTC_PIN)
 * 注意: ADC 引脚/通道 已收编至 board.h, 本头文件不再定义 */

#define NTC_R_FIXED         10500.0f        /* 分压固定电阻 (Ω) */
#define NTC_R25             10000.0f        /* NTC 25°C 标称阻值 (Ω) */
#define NTC_B_VALUE         3950.0f         /* NTC B 值 (25/85) */
#define NTC_VCC_MV          3300.0f         /* 分压供电电压 (mV) */

#define NTC_ADC_SAMPLES     32              /* 单次读取采样次数(取平均, 与硬件RC滤波配合) */

void ntc_init(void);
float ntc_read_temperature(void);   /* 返回摄氏温度(°C), 采样异常时返回 0 */
float ntc_read_resistance(void);    /* 调试用: 当前NTC阻值(Ω), 异常时返回 0 */

#endif
