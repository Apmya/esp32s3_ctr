#ifndef __AP_WIFI_H
#define __AP_WIFI_H

#include "mywifi.h"
#include "ws_server.h"

void ap_wifi_init(void);

void ap_wifi_apcfg(void); 

void ap_wifi_stop(void);

/* 启动配网策略环(prio4, core1): 无配置/未连网→自动开AP网页配网, 连上→自动关
 * 须在 ap_wifi_init 之后调用一次 */
void ap_wifi_watch_start(void);

#endif
