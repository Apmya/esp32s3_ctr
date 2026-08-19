#include "iic.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "ina219.h"
#include "esp_log.h"

#define TAG     "IIC"

void iic_init(void)
{
    i2c_config_t i2c_structure = {
        .clk_flags = 0,
        .master.clk_speed = 100000,
        .mode = I2C_MODE_MASTER,
        .scl_io_num = GPIO_NUM_4,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,   /* 打开内部上拉: 模块断开时总线不悬空 */
        .sda_io_num = GPIO_NUM_5,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_0, &i2c_structure));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0,0,0));
    ESP_LOGI("IIC", "I2C初始化成功");
}

void i2c_scan_bus(void)
{
    ESP_LOGI("I2C_SCAN", "====== 开始扫描I2C总线 ======");
    int found = 0;
    
    for (uint8_t addr = 0x08; addr <= 0x77; addr++)  // 7位地址范围
    {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
        i2c_master_stop(cmd);
        
        esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(100));
        i2c_cmd_link_delete(cmd);
        
        if (ret == ESP_OK)
        {
            ESP_LOGI("I2C_SCAN", "  找到设备: 0x%02X", addr);
            found++;
        }
        else if (ret != ESP_ERR_TIMEOUT)
        {
            // 非超时的其他错误（一般就是NACK=ESP_FAIL，正常现象）
        }
    }
    
    if (found == 0) {
        ESP_LOGW("I2C_SCAN", "总线上没有任何设备应答！");
    } else {
        ESP_LOGI("I2C_SCAN", "共找到 %d 个I2C设备", found);
    }
    ESP_LOGI("I2C_SCAN", "====== 扫描结束 ======");
}

void iic_dev_start(void)
{
    iic_init();
    i2c_scan_bus();
    INA219_init();
    ESP_LOGI(TAG,"IIC设备初始化成功");
}
