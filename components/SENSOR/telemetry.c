#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"

#include "ina219.h"
#include "myntc.h"
#include "telemetry.h"

#define TAG "TELEM"

#define TELEMETRY_PERIOD_MS   2000   /* 采集周期 */
#define VBUS_CAL_OFFSET_V     0.3f   /* 母线电压校准偏置(实测): 生产侧唯一标定入口 */

/* ── 主人数据 ── */
static telemetry_t g_tel;
static SemaphoreHandle_t g_tel_lock = NULL;


static void tel_lock(void)
{
    if (g_tel_lock)
    {
        xSemaphoreTake(g_tel_lock, portMAX_DELAY);
    }
}


static void tel_unlock(void)
{
    if (g_tel_lock)
    {
        xSemaphoreGive(g_tel_lock);
    }
}


void telemetry_update(float vbus_v, float cur_a, float power_w, float temp_c)
{
    /* 锁由第一个调用者(采集任务, 单写者)创建, 无竞态;
     * 创建前的读快照拿到全0初值, 行为安全 */
    if (g_tel_lock == NULL)
    {
        g_tel_lock = xSemaphoreCreateMutex();
    }

    tel_lock();
    g_tel.vbus_v  = vbus_v;
    g_tel.cur_a   = cur_a;
    g_tel.power_w = power_w;
    g_tel.temp_c  = temp_c;
    tel_unlock();
}


void telemetry_snapshot(telemetry_t *out)
{
    if (out == NULL)
    {
        return;
    }

    tel_lock();
    memcpy(out, &g_tel, sizeof(*out));
    tel_unlock();
}


/* ── 采集任务: 传感器域的生产者 (原 main.c iic_collect_task 迁入) ──
 * 读取在锁外(INA219 内部自带互斥+100ms I2C超时), 仅注入时短锁,
 * 外设挂死/模块离线只影响本任务自身, 与按键/网络任务无锁交集 */
static void telemetry_collect_task(void *arg)
{
    (void)arg;

    while (1)
    {
        float local_vbus  = ina219_get_bus_voltage();
        float local_cur   = ina219_get_current();
        float local_power = ina219_get_power();
        float local_temp  = ntc_read_temperature();

        telemetry_update(local_vbus + VBUS_CAL_OFFSET_V,
                         local_cur, local_power, local_temp);

        vTaskDelay(pdMS_TO_TICKS(TELEMETRY_PERIOD_MS));
    }
}


void telemetry_start(void)
{
    static bool s_started = false;
    if (s_started)
    {
        return;
    }
    s_started = true;

    xTaskCreatePinnedToCore(telemetry_collect_task, "telemetry",
                            8192, NULL, 3, NULL, 1);
    ESP_LOGI(TAG, "采集任务已启动 (周期%dms)", TELEMETRY_PERIOD_MS);
}
