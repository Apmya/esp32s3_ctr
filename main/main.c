/* =====================================================================
 * MAIN —— 应用装配/调度根 (Composition Root)
 *
 * 职责边界 (刻意保持"薄"):
 *   1. 按依赖顺序初始化各层 (存储 → 系统基座 → 配置 → 外设 → 业务服务)
 *   2. 启动各业务模块的任务属主 (模块自建任务, main 只决定先后顺序)
 *   3. 全工程唯一的"任务总览表"(见 app_main 底部注释)
 *
 * 禁止事项:
 *   - 不在本文件定义任何共享数据 (所有权在属主模块)
 *   - 不在本文件写业务任务循环 (采集/配网/生命周期均已迁出)
 *   - mynvs.h 仅允许在本文件(装配根)为初始化目的被 include,
 *     业务模块的持久化一律经 *_config 层
 *
 * 任务总览 (优先级 | 绑核 | 属主 —— 调整 prio 请到属主模块的 _start 处, 并同步本表):
 *   ┌────────────────┬──────┬─────┬───────┬──────────────────────────┐
 *   │ 任务            │ prio │ 核  │ 栈    │ 属主                      │
 *   ├────────────────┼──────┼─────┼───────┼──────────────────────────┤
 *   │ key            │  10  │  1  │ 3072  │ SCREEN/key.c              │
 *   │ valve_meas     │   6  │  1  │ 6144  │ MYMQTT/tuya_dm.c          │
 *   │ wifi_restart   │   5  │  1  │ 6144  │ WIFI/mywifi.c             │
 *   │ dm_report      │   5  │  1  │ 8192  │ MYMQTT/tuya_dm.c          │
 *   │ tuya_lifecycle │   5  │  1  │ 8192  │ MYMQTT/tuya_mqtt.c        │
 *   │ httpd/ws       │   5  │  -  │ 4096  │ WS_SERVER (esp_http_server)│
 *   │ mqtt_task      │   5  │  -  │ 6144  │ esp-mqtt (tuya_start内建) │
 *   │ NimBLE host    │  ─   │  -  │  ─    │ bt 协议栈 (ble_init内建)  │
 *   │ ap_cfg_watch   │   4  │  1  │ 8192  │ AP_WIFI/ap_wifi.c         │
 *   │ apcfg(evbit)   │   3  │  1  │ 8192  │ AP_WIFI/ap_wifi.c         │
 *   │ telemetry      │   3  │  1  │ 8192  │ SENSOR/telemetry.c        │
 *   │ esp_timer      │ 高   │  -  │  ─    │ IDF守护(回调已零阻塞)      │
 *   └────────────────┴──────┴─────┴───────┴──────────────────────────┘
 *   core0 主要留给 esp_wifi/bt 协议栈(22/23); 应用任务全部钉在 core1。
 *   按键(10) > 测量(6) > 网络业务(5) > 配网(4/3) > 采集(3):
 *   网络故障无法通过"优先级反转"影响按键响应。
 * ===================================================================== */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_err.h"

#include "mynvs.h"        /* 装配根: 仅用于底层存储初始化 */
#include "wifi_config.h"

#include "iic.h"
#include "myntc.h"
#include "telemetry.h"
#include "myvalve.h"

#include "ap_wifi.h"
#include "tuya_mqtt.h"
#include "tuya_dm.h"
#include "myble.h"
#include "key.h"

#define TAG "MAIN_APP"

void app_main(void)
{
    /* ── 阶段1: 底层存储层 ─────────────────────────────── */
    ESP_ERROR_CHECK(mynvs_init());          /* 取代原手写 nvs_flash_init 重复块 */

    /* ── 阶段2: 系统基座 ───────────────────────────────── */
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* ── 阶段3: 配置装载 (NVS就绪后、业务任务启动前) ───── */
    ESP_ERROR_CHECK(wifi_config_load());
    {
        wifi_cred_t cred;
        wifi_config_get_credential(&cred);
        ESP_LOGI(TAG, "加载WiFi配置: SSID=%s", cred.ssid);
    }

    /* ── 阶段4: 驱动与外设初始化 ───────────────────────── */
    iic_dev_start();                        /* I2C总线 + INA219 */
    ntc_init();                             /* NTC: 引脚/通道见 board.h */
    valve_init();                           /* 电磁阀 GPIO9 */
    ESP_ERROR_CHECK(key_init());            /* KEY1~5 输入+消抖任务 */
    /* TODO(你): lcd_init(); 屏幕驱动完成后加入本阶段 */

    /* ── 阶段5: 业务服务装配 (顺序 = 依赖顺序) ─────────── */
    ap_wifi_init();            /* wifista+AP+WS, 注册 wifi_config apply 钩子 */
    tuya_dm_init();            /* 物模型: ws2812/测量任务/上报任务/定时器 */
    telemetry_start();         /* 传感器采集环 (2s) */
    ap_wifi_watch_start();     /* 配网策略环: 断网开AP, 连网关AP */
    tuya_lifecycle_start();    /* MQTT生命周期环: 联网起MQTT, 断连销毁 */

    /* BLE 最后开: 手机连上即可写特征值配网, 此时全部依赖已就绪;
     * (原 ble_task 的 init+空转延时循环已删 — 属无效任务) */
    ESP_ERROR_CHECK(ble_init());

    ESP_LOGI(TAG, "系统装配完成");

    /* app_main 返回后其任务栈会被系统回收, 常驻工作全部在上面的模块任务中 */
}
