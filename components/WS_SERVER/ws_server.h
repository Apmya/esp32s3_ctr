#ifndef __WS_SERVER_H
#define __WS_SERVER_H
#include "esp_err.h"
#include "stdint.h"

/* =====================================================================
 * WS_SERVER —— 配网网页的 HTTP/WebSocket 传输管道
 * 纯传输层: 不含任何业务/存储逻辑, 收到什么原样转给注册的回调
 * ===================================================================== */

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
