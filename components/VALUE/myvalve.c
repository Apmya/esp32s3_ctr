#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"

#include "myvalve.h"
#include "my74hc165.h"
#include "myboard.h"

#define TAG "VALVE"

static bool relay_state[RELAY_MAX_NUM] = {false};  /* 继电器状态: true=打开, false=关闭 */
static bool valve_state[VALVE_MAX_NUM] = {false};  /* 电磁阀状态: true=打开, false=关闭 */

static SemaphoreHandle_t s_valve_mutex = NULL;

static const gpio_num_t addr_pins1[4] = {A1_PIN, A2_PIN, A3_PIN, A4_PIN};  /* 地址引脚数组 *///1-8
static const gpio_num_t addr_pins2[4] = {A6_PIN, A7_PIN, A8_PIN, A9_PIN}; //9-12

esp_err_t IsValid_ValveIndex(uint8_t valve_index)
{
    if(valve_index >= VALVE_MAX_NUM )
    {
        ESP_LOGE(TAG, "无效的电磁阀索引: %d", valve_index);
    }
    return ESP_ERR_INVALID_ARG;
}

void relay_close_all(void){
    xSemaphoreTake(s_valve_mutex, portMAX_DELAY);
    gpio_set_level(EN_PIN, 1);  // 关闭所有继电器
    gpio_set_level(A5_PIN, 1);
    xSemaphoreGive(s_valve_mutex);
}  

/**
 * @brief 选中指定的输出通道 (0-15)
 * @param line 要选中的通道号
 */
static uint8_t select_line1(uint8_t line) {
    // 按位拆分 line 的值，写入对应的地址引脚
    for (int i = 0; i < 4; i++) {
        gpio_set_level(addr_pins1[i], (line >> i) & 0x01);
    }
    return line;
}

static uint8_t select_line2(uint8_t line) {
    // 按位拆分 line 的值，写入对应的地址引脚
    for (int i = 0; i < 4; i++) {
        gpio_set_level(addr_pins2[i], (line >> i) & 0x01);
    }
    return line;
}

/*relay_index ~ 0-48*/
static esp_err_t relay_on(uint8_t relay_index){
    if(relay_index >= RELAY_MAX_NUM || relay_index < 0)
    {
        ESP_LOGE(TAG, "无效的继电器索引: %d", relay_index);
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t line = relay_index % 16;  // 计算继电器在译码器中的输出通道 (0-15)
    uint8_t relay_num = relay_index /16;  // 计算继电器在译码器中的编号 (0-2)

    xSemaphoreTake(s_valve_mutex, portMAX_DELAY);
    if(relay_num == 0)
    {
        select_line1(line);  // 选择第一个译码器的输出通道
        gpio_set_level(EN_PIN, 0);  
        gpio_set_level(A5_PIN, 0);  
    }
    else if(relay_num == 1)
    {
        select_line1(line);  // 选择第二个译码器的输出通道
        gpio_set_level(EN_PIN, 0);  
        gpio_set_level(A5_PIN, 1);  
    }
    else if(relay_num == 2)
    {
        select_line2(line);  // 选择第三个译码器的输出通道
        gpio_set_level(EN_PIN, 1);  
        gpio_set_level(A5_PIN, 0);  
    }

    relay_state[relay_index] = true;  // 更新继电器状态为打开
    ESP_LOGI(TAG, "继电器 %d 打开", relay_index);
    xSemaphoreGive(s_valve_mutex);
    return ESP_OK;
}


void valve_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask =  (1ULL << A1_PIN) |
                                (1ULL << A2_PIN) |
                                (1ULL << A3_PIN) |
                                (1ULL << A4_PIN) |
                                (1ULL << A5_PIN) |
                                (1ULL << A6_PIN) |
                                (1ULL << A7_PIN) |
                                (1ULL << A8_PIN) |
                                (1ULL << A9_PIN) |
                                (1ULL << EN_PIN),
        /*通过A5和EN控制3个译码器的输入*/
        /*
        |————————————————————————|
        |  A5  |  EN  | 译码器输出|
        |  0   |   0  |    U8    |
        |  0   |   1  |    U9    |
        |  1   |   0  |    U12   |
        |  1   |   1  | ALL DOWN |
        |————————————————————————|
        */
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    relay_close_all();

    s_valve_mutex = xSemaphoreCreateMutex();
    if (s_valve_mutex == NULL)
    {
        ESP_LOGE(TAG, "创建电磁阀互斥锁失败");
        return;
    }

    xSemaphoreTake(s_valve_mutex, portMAX_DELAY);
    for (int i = 0; i < VALVE_MAX_NUM; i++)
    {
        valve_state[i] = false;
    }
    for (int i = 0; i < RELAY_MAX_NUM; i++)
    {
        relay_state[i] = false;
    }
    xSemaphoreGive(s_valve_mutex);

    ESP_LOGI(TAG, "电磁阀回路初始化完成");
}

/*
valve_index: 0-11
half false--relay1/2
half true--relay3/4
*/
esp_err_t valve_half_on(uint8_t valve_index, bool half)
{
    uint8_t relay_index1;
    uint8_t relay_index2;

    if(!half)
    {
        ESP_LOGI(TAG, "开启电磁阀 %d 的上半部分", valve_index);
        relay_index1 = valve_index * 4;      // 第一个继电器索引
        relay_index2 = valve_index * 4 + 1;  // 第二个继电器索引
    }
    else
    {
        ESP_LOGI(TAG, "开启电磁阀 %d 的下半部分", valve_index);
        relay_index1 = valve_index * 4 + 2;  // 第一个继电器索引
        relay_index2 = valve_index * 4 + 3;  // 第二个继电器索引
    }

    relay_on(relay_index1);  // 打开第一个继电器
    vTaskDelay(pdMS_TO_TICKS(100));  // 延时100ms，等待继电器动作完成

    // 检查第一个继电器是否成功打开
    if (relay_state[relay_index1] == false) {
        ESP_LOGE(TAG, "继电器 %d 打开失败", relay_index1);
        relay_close_all();  // 关闭所有继电器
        return ESP_FAIL;
    }

    relay_on(relay_index2);  // 打开第二个继电器
    vTaskDelay(pdMS_TO_TICKS(100));  // 延时100ms，等待继电器动作完成

    // 检查第二个继电器是否成功打开
    if (relay_state[relay_index2] == false) {
        ESP_LOGE(TAG, "继电器 %d 打开失败", relay_index2);
        relay_close_all();  // 关闭所有继电器
        return ESP_FAIL;
    }

    return ESP_OK;  // 两个继电器都成功打开
}

esp_err_t valve_on(uint8_t valve_index)
{
    /*
    1.开启一半继电器
    2.判断my74hc165的返回值
    3.如果返回值为0，说明继电器成功打开，再开启另一半继电器
    4.再判断另一个my74hc165的返回值，如果为0，说明继电器成功打开，返回ESP_OK
    5.如果返回值不为0，说明继电器没有成功打开，关闭所有继电器，返回ESP_FAIL
    */

    if(IsValid_ValveIndex(valve_index))
    {
        uint32_t my74hc165_data = my74hc165_read();  

        valve_half_on(valve_index,false);
        if(xdata_get(valve_index,my74hc165_data))
        {
            vTaskDelay(pdMS_TO_TICKS(50));
            valve_half_on(valve_index,true);
        }

        if(!ydata_get(valve_index,my74hc165_data))
        {
           return ESP_FAIL;
        }
    }
    return ESP_OK;
}

bool valve_get_state(uint8_t valve_index)
{
    bool state = false;
    if(IsValid_ValveIndex(valve_index))
    {
        xSemaphoreTake(s_valve_mutex, portMAX_DELAY);
        state = valve_state[valve_index];
        xSemaphoreGive(s_valve_mutex);
    }
    return state;
} 

