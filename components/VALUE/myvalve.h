#ifndef MYVALVE_H
#define MYVALVE_H

#include <stdbool.h>
#include "driver/ledc.h"
#include "board.h"

/* 引脚 VALVE_PIN 唯一来源在 board.h, 本头文件不再定义引脚 */
#define VALVE_LEDC_CHANNEL      LEDC_CHANNEL_0
#define VALVE_LEDC_TIMER        LEDC_TIMER_0
#define VALVE_PWM_FREQ_HZ       500
#define VALVE_PWM_RESOLUTION    LEDC_TIMER_10_BIT

#define VALVE_PWM_RES_MAX     1023U
#define VALVE_DUTY_PULLIN     1023U
#define VALVE_DUTY_HOLD       100U
#define VALVE_PULLIN_TIME_MS    500U

#define LEDC_MODE LEDC_LOW_SPEED_MODE

void valve_init(void);
void valve_open(void);
void valve_close(void);
void valve_toggle(void);
bool valve_get_state(void);

#endif