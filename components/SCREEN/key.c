#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_check.h"

#include "key.h"
#include "board.h"

#define TAG "KEY"

#define KEY_NUM             5
#define KEY_QUEUE_LEN       16
#define KEY_DEBOUNCE_MS     25      /* 消抖等待 */
#define KEY_REPEAT_MS       250     /* 同键最小事件间隔 */
#define KEY_TASK_STACK      3072
#define KEY_TASK_PRIO       10      /* 应用任务中的最高优先级: 按键响应优先 */

/* 按键 GPIO 表 (低电平有效, 内部上拉) */
static const gpio_num_t s_key_gpio[KEY_NUM] = {
    KEY1_PIN, KEY2_PIN, KEY3_PIN, KEY4_PIN, KEY5_PIN,
};

typedef struct
{
    uint8_t id;
    uint8_t pressed;    /* ISR 采到的电平: 1=按下 0=释放 */
} key_raw_evt_t;

static QueueHandle_t   s_key_queue = NULL;
static key_event_cb_t  s_key_cb    = NULL;
static TaskHandle_t    s_key_task  = NULL;


/* ISR: 只读电平+发队列, 不打印不分配 — 任何网络/存储逻辑不可达 */
static void IRAM_ATTR key_isr_handler(void *arg)
{
    int idx = (int)(intptr_t)arg;

    /* 按下=低电平 (内部上拉) */
    key_raw_evt_t ev = {
        .id      = (uint8_t)idx,
        .pressed = (gpio_get_level(s_key_gpio[idx]) == 0) ? 1 : 0,
    };

    BaseType_t hi_wake = pdFALSE;
    xQueueSendFromISR(s_key_queue, &ev, &hi_wake);

    if (hi_wake)
    {
        portYIELD_FROM_ISR();
    }
}


/* 按键任务: 收原始边沿 → 消抖确认 → 节流 → 回调 */
static void key_task(void *arg)
{
    (void)arg;
    key_raw_evt_t ev;
    uint32_t last_ms[KEY_NUM] = {0};

    while (1)
    {
        if (xQueueReceive(s_key_queue, &ev, portMAX_DELAY) != pdTRUE)
        {
            continue;
        }

        /* 消抖: 等电平稳定后复核 */
        vTaskDelay(pdMS_TO_TICKS(KEY_DEBOUNCE_MS));

        int level = gpio_get_level(s_key_gpio[ev.id]);
        if (level != (ev.pressed ? 0 : 1))
        {
            continue;   /* 抖动, 丢弃 */
        }

        uint32_t now = (uint32_t)xTaskGetTickCount() * portTICK_PERIOD_MS;
        if (now - last_ms[ev.id] < KEY_REPEAT_MS)
        {
            continue;   /* 重复边沿, 节流 */
        }
        last_ms[ev.id] = now;

        key_event_cb_t cb = s_key_cb;
        if (cb)
        {
            cb((key_id_t)ev.id, ev.pressed ? KEY_EVT_PRESS : KEY_EVT_RELEASE);
        }
    }
}


esp_err_t key_register_callback(key_event_cb_t cb)
{
    s_key_cb = cb;
    return ESP_OK;
}


esp_err_t key_init(void)
{
    s_key_queue = xQueueCreate(KEY_QUEUE_LEN, sizeof(key_raw_evt_t));
    if (s_key_queue == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    uint64_t pin_mask = 0;
    for (int i = 0; i < KEY_NUM; i++)
    {
        pin_mask |= (1ULL << s_key_gpio[i]);
    }

    gpio_config_t io = {
        .pin_bit_mask = pin_mask,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_ANYEDGE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io), TAG, "按键GPIO配置失败");

    esp_err_t ret = gpio_install_isr_service(0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE)
    {
        return ret;
    }

    for (int i = 0; i < KEY_NUM; i++)
    {
        /* arg 传键索引, ISR 内只处理触发的这个键 */
        ESP_RETURN_ON_ERROR(gpio_isr_handler_add(s_key_gpio[i], key_isr_handler,
                                                 (void *)(intptr_t)i),
                            TAG, "按键中断注册失败");
    }

    BaseType_t ok = xTaskCreatePinnedToCore(key_task, "key",
                                            KEY_TASK_STACK, NULL,
                                            KEY_TASK_PRIO, &s_key_task, 1);
    if (ok != pdPASS)
    {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "按键初始化完成: KEY1~5, 低电平有效, 消抖%dms", KEY_DEBOUNCE_MS);
    return ESP_OK;
}
