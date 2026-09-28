#pragma once

#ifndef __BOARD_H__
#define __BOARD_H__

/* =========================================================
 * LCD SPI
 * ========================================================= */

#define LCD_PIN_DC          GPIO_NUM_1
#define LCD_PIN_MOSI        GPIO_NUM_2
#define LCD_PIN_CS          GPIO_NUM_43
#define LCD_PIN_CLK         GPIO_NUM_42
#define LCD_PIN_MISO        GPIO_NUM_40
#define LCD_PIN_RESET       GPIO_NUM_44
#define LCD_PIN_LED         GPIO_NUM_41


/* =========================================================
 * Touch
 * ========================================================= */

#define TOUCH_IRQ_PIN       GPIO_NUM_35
#define TOUCH_DO_PIN        GPIO_NUM_36
#define TOUCH_DIN_PIN       GPIO_NUM_37
#define TOUCH_CS_PIN        GPIO_NUM_38
#define TOUCH_CLK_PIN       GPIO_NUM_39

/* =========================================================
 * I2C
 * ========================================================= */

#define I2C_PIN_SCL         GPIO_NUM_4
#define I2C_PIN_SDA         GPIO_NUM_5


/* =========================================================
 * NTC
 * ========================================================= */

#define NTC_PIN             GPIO_NUM_6              /* NTC 分压 ADC 输入 */
#define NTC_ADC_CHANNEL     ADC1_CHANNEL_5          /* ESP32-S3: GPIO6 = ADC1_CH5 */


/* =========================================================
 * ADC  74hc154
 * ========================================================= */

#define A1_PIN          GPIO_NUM_18
#define A2_PIN          GPIO_NUM_17
#define A3_PIN          GPIO_NUM_16
#define A4_PIN          GPIO_NUM_15

#define A5_PIN          GPIO_NUM_7

#define A6_PIN          GPIO_NUM_8
#define A7_PIN          GPIO_NUM_3
#define A8_PIN          GPIO_NUM_46
#define A9_PIN          GPIO_NUM_9

#define EN_PIN          GPIO_NUM_0

/* =========================================================
 * WS2812 状态灯 (RMT 数据线)
 * ========================================================= */

#define WS2812_PIN          GPIO_NUM_48


/* =========================================================
 * KEY
 * ========================================================= */

#define KEY1_PIN            GPIO_NUM_10
#define KEY2_PIN            GPIO_NUM_11
#define KEY3_PIN            GPIO_NUM_12
#define KEY4_PIN            GPIO_NUM_13
#define KEY5_PIN            GPIO_NUM_14


/* =========================================================
 * 74HC165 输入扩展芯片 
 * ========================================================= */

#define IO_DATA_PIN         GPIO_NUM_19
#define IO_CLK_PIN          GPIO_NUM_20
#define IO_LATCH_PIN        GPIO_NUM_21


/* =========================================================
 * RGB LED
 * ========================================================= */

#define LED_R_PIN           GPIO_NUM_45  //avoid using
#define LED_B_PIN           GPIO_NUM_47
#define LED_G_PIN           GPIO_NUM_48


#endif /* __BOARD_H__ */