#ifndef __LCD_H
#define __LCD_H

#include <stdbool.h>
#include "esp_err.h"

/* =====================================================================
 * LCD —— 屏幕显示层 (骨架, 驱动代码由你按 API 填充)
 *
 * 硬件依据 board.h: LCD_PIN_DC/MOSI/CS/CLK/MISO/RESET/LED (SPI)
 * 接线与驱动后续在此文件体系内实现, 本头文件先定接口, 保证
 * 业务侧(按键/网络状态展示)可以先行编译集成。
 *
 * ★ 所有权: 显示内容与刷新节奏由 lcd.c 内部管理;
 *   外部只通过 lcd_update_status() 投喂状态快照, 拿不到任何内部数据。
 * ===================================================================== */

/* 状态快照: 各业务模块取数后组装, lcd 不反向依赖任何业务模块 */
typedef struct
{
    bool wifi_ok;      /* 来源: mywifi    */
    bool mqtt_ok;      /* 来源: tuya_mqtt */
    bool ble_ok;       /* 来源: myble     */
    bool valve_open;   /* 来源: myvalve   */
    float vbus_v;      /* 来源: ina219    */
    float cur_a;       /* 来源: ina219    */
    float power_w;     /* 来源: ina219    */
    float temp_c;      /* 来源: myntc     */
} lcd_status_t;

typedef enum
{
    LCD_PAGE_HOME = 0,     /* 主页: 阀门状态+电参数 */
    LCD_PAGE_NET,          /* 网络页: WiFi/MQTT/BLE */
    LCD_PAGE_MEASURE,      /* 测量页: IP/IH/TP */
    LCD_PAGE_MAX
} lcd_page_t;

/**
 * @brief SPI+面板初始化, 创建刷新任务 (TODO: 驱动实现)
 */
esp_err_t lcd_init(void);

/**
 * @brief 投喂状态快照 (非阻塞: 内部只拷贝进私有缓冲, 由刷新任务异步画屏)
 */
esp_err_t lcd_update_status(const lcd_status_t *st);

/**
 * @brief 切换显示页面 (按键模块后续调用)
 */
esp_err_t lcd_page_switch(lcd_page_t page);

#endif
