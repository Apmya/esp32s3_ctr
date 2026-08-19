#include "myntc.h"
#include "driver/adc.h"
#include "esp_log.h"
#include <math.h>

static const char *TAG = "MYNTC";

/**
 * @brief 读取NTC分压点电压(mV), 多次采样取平均以抑制噪声
 *        (硬件已有电压跟随器+RC滤波, 软件再取平均进一步平滑)
 */
static float ntc_read_voltage_mv(void)
{
    uint32_t sum = 0;
    int raw;

    for (int i = 0; i < NTC_ADC_SAMPLES; i++)
    {
        raw = adc1_get_raw(NTC_ADC_CHANNEL);
        if (raw < 0)            /* ADC读取失败(如未初始化) */
        {
            ESP_LOGW(TAG, "ADC读取失败: %d", raw);
            return 0.0f;
        }
        sum += (uint32_t)raw;
    }
    /* 12位ADC: raw/4095 * VCC */
    return (float)sum / NTC_ADC_SAMPLES / 4095.0f * NTC_VCC_MV;
}

/**
 * @brief 计算当前NTC阻值(Ω)
 *        分压公式: V = VCC * R_ntc / (R_fixed + R_ntc)
 *        → R_ntc = R_fixed * V / (VCC - V)
 */
float ntc_read_resistance(void)
{
    float v_mv = ntc_read_voltage_mv();

    /* 电压越界: NTC开路(V≈VCC)或短路(V≈0), 避免除零/ln(0) */
    if (v_mv < 1.0f || v_mv > (NTC_VCC_MV - 1.0f))
    {
        ESP_LOGW(TAG, "NTC采样异常: V=%.1fmV (NTC开路或短路?)", v_mv);
        return 0.0f;
    }
    return NTC_R_FIXED * v_mv / (NTC_VCC_MV - v_mv);
}

/**
 * @brief 读取NTC温度(°C)
 *        B值公式: 1/T = 1/T0 + ln(R/R0)/B,  T0 = 298.15K
 */
float ntc_read_temperature(void)
{
    float r = ntc_read_resistance();
    if (r <= 0.0f)
    {
        return 0.0f;
    }
    float t_k = 1.0f / (1.0f / 298.15f + logf(r / NTC_R25) / NTC_B_VALUE);
    return t_k - 273.15f;
}

void ntc_init(void)
{
    adc1_config_width(ADC_WIDTH_BIT_12);                           /* 12位分辨率 */
    adc1_config_channel_atten(NTC_ADC_CHANNEL, ADC_ATTEN_DB_11);   /* 0~3.1V量程 */
    ESP_LOGI(TAG, "NTC初始化完成: GPIO%d, 10kΩ/B3950, 每次采样%d次取平均",
             NTC_ADC_GPIO, NTC_ADC_SAMPLES);
}
