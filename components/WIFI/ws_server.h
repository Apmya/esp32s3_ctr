#ifndef __WS_SERVER_H
#define __WS_SERVER_H
#include "esp_err.h"
#include "stdint.h"

typedef void(*ws_receive_cb)(uint8_t* payload , int len);
typedef struct 
{
    const char*html_code;
    ws_receive_cb receive_fn;
}ws_cfg_t;

//启动HTTP和Web服务器
esp_err_t web_ws_start(ws_cfg_t* cfg);

esp_err_t web_ws_stop(void);

esp_err_t web_ws_send(uint8_t* data , int len);

#endif