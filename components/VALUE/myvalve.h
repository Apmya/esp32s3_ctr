#ifndef MYVALVE_H
#define MYVALVE_H

#include <stdbool.h>

#define RELAY_CLOSE_ALL_NUM 48
#define RELAY_MAX_NUM 48
#define VALVE_MAX_NUM 12

void valve_init(void);
esp_err_t valve_on(uint8_t valve_index);
bool valve_get_state(uint8_t valve_index);
esp_err_t IsValid_ValveIndex(uint8_t valve_index);
void relay_close_all(void);


#endif