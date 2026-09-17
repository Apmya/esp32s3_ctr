#include "lcd.h"
#include "esp_log.h"

/* =====================================================================
 * LCD 骨架实现 —— 接口已定, 驱动逻辑留给你填充
 * 除 lcd_init 内的 TODO 外, 其余函数为可编译占位, 不产生副作用
 * ===================================================================== */

#define TAG "LCD"

esp_err_t lcd_init(void)
{
    /* TODO(你): SPI 主机初始化 (board.h LCD_PIN_*), 面板复位时序,
     *           创建私有刷新任务 + 状态快照缓冲 */
    ESP_LOGW(TAG, "lcd 驱动未实现 (骨架占位)");
    return ESP_OK;
}

esp_err_t lcd_update_status(const lcd_status_t *st)
{
    (void)st;
    /* TODO(你): 锁内 memcpy 进私有快照缓冲, 立即返回 (µs级持锁) */
    return ESP_OK;
}

esp_err_t lcd_page_switch(lcd_page_t page)
{
    (void)page;
    /* TODO(你): 页面状态机切换 */
    return ESP_OK;
}
