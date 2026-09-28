#include <driver/gpio.h>
#include "esp_log.h"
#include <esp_err.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "my74hc165.h"
#include "myvalve.h"
#include "myboard.h"

static const char *TAG = "MY74HC165";

static SemaphoreHandle_t s_my74hc165_mutex = NULL;

static uint32_t my74hc165_data = 0;

uint32_t my74hc165_read(void)
{
    xSemaphoreTake(s_my74hc165_mutex, portMAX_DELAY);
    my74hc165_data = 0;

    uint8_t u[3] = {0};

    // 拉低锁存引脚，开始读取数据
    gpio_set_level(IO_LATCH_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(10));  // 延时1ms，确保锁存稳定

    // 拉高锁存引脚，锁存数据
    gpio_set_level(IO_LATCH_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(10));  // 延时1ms，确保数据稳定
    
    for(int arr = 0; arr < 3; arr++)
    {
        for (int bit = 0; bit < 8; bit++) {
            // 拉高时钟引脚，准备读取数据
            gpio_set_level(IO_CLK_PIN, 1);
    
            // 读取数据引脚的状态
            int bit_value = gpio_get_level(IO_DATA_PIN);
            u[arr] |= ((uint8_t)bit_value << bit);

            // 拉低时钟引脚，完成一次读取
            gpio_set_level(IO_CLK_PIN, 0);
        }
    }    
    my74hc165_data =((uint32_t)u[0] << 16) |
                    ((uint32_t)u[1] << 8)  |
                    ((uint32_t)u[2]);
    xSemaphoreGive(s_my74hc165_mutex);

    /*
    my74hc165_data
23   y6x6y12x12y5x5y11x11  16 15                         8 7                          0
┌────────────────────────────┬────────────────────────────┬────────────────────────────┐
│           u[0]             │           u[1]             │           u[2]             │
└────────────────────────────┴────────────────────────────┴────────────────────────────┘
    */
    return my74hc165_data;
}


void my74hc165_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << IO_CLK_PIN) |
                        (1ULL << IO_LATCH_PIN) |
                        (1ULL << IO_DATA_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    s_my74hc165_mutex = xSemaphoreCreateMutex();
    if (s_my74hc165_mutex == NULL) {
        ESP_LOGE(TAG, "创建MY74HC165互斥锁失败");
        return;
    }

}

uint8_t xdata_get(uint8_t valve_index,uint32_t my74hc165_data)
{
    return IsValid_ValveIndex(valve_index)? ((my74hc165_data >> (2U * valve_index)) & 0x01U): 0;
}

uint8_t ydata_get(uint8_t valve_index, uint32_t my74hc165_data)
{
    return IsValid_ValveIndex(valve_index)? ((my74hc165_data >> (2U * valve_index + 1)) & 0x01U): 0;
}