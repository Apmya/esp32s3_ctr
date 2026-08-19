#include "myvalve.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"

#define TAG "VALVE"

static bool s_valve_state = false;
static SemaphoreHandle_t s_valve_mutex = NULL;

// 底层硬件输出：低电平开启，高电平关闭
static void valve_set_hw_level(bool enable)
{
    if(enable)
    {
        gpio_set_level(VALVE_GPIO_PIN, 0);  // 低电平 = 打开电磁阀
    }
    else
    {
        gpio_set_level(VALVE_GPIO_PIN, 1);  // 高电平 = 关闭电磁阀
    }
}

void valve_init(void)
{
    s_valve_mutex = xSemaphoreCreateMutex();

    gpio_config_t io_conf = {0};
    io_conf.pin_bit_mask = 1ULL << VALVE_GPIO_PIN;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_config(&io_conf);

    // 上电默认高电平：电磁阀关闭
    gpio_set_level(VALVE_GPIO_PIN, 1);

    xSemaphoreTake(s_valve_mutex, portMAX_DELAY);
    s_valve_state = false;
    xSemaphoreGive(s_valve_mutex);

    ESP_LOGI(TAG, "电磁阀GPIO初始化完成, GPIO%d, 低电平导通,高电平关闭", VALVE_GPIO_PIN);
}

void valve_open(void)
{
    xSemaphoreTake(s_valve_mutex, portMAX_DELAY);
    if (s_valve_state)
    {
        xSemaphoreGive(s_valve_mutex);
        return;
    }
    s_valve_state = true;
    valve_set_hw_level(true);
    xSemaphoreGive(s_valve_mutex);
    ESP_LOGI(TAG, "电磁阀 打开");
}

void valve_close(void)
{
    xSemaphoreTake(s_valve_mutex, portMAX_DELAY);
    if (!s_valve_state)
    {
        xSemaphoreGive(s_valve_mutex);
        return;
    }
    s_valve_state = false;
    valve_set_hw_level(false);
    xSemaphoreGive(s_valve_mutex);
    ESP_LOGI(TAG, "电磁阀 关闭");
}

  void valve_toggle(void)
  {
      xSemaphoreTake(s_valve_mutex, portMAX_DELAY);
      if(s_valve_state)
      {
          s_valve_state = false;
          valve_set_hw_level(false);  
      }
      else
      {
          s_valve_state = true;
          valve_set_hw_level(true);
      }
      xSemaphoreGive(s_valve_mutex);
      ESP_LOGI(TAG, "电磁阀 切换为 %s", s_valve_state ? "打开" : "关闭");
  }

bool valve_get_state(void)
{
    bool ret;
    xSemaphoreTake(s_valve_mutex, portMAX_DELAY);
    ret = s_valve_state;
    xSemaphoreGive(s_valve_mutex);
    return ret;
}