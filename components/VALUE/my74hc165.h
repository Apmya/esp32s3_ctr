#ifndef __74HC165_H
#define __74HC165_H

void my74hc165_init(void);
uint32_t my74hc165_read(void);
uint8_t xdata_get(uint8_t valve_index, uint32_t my74hc165_data);
uint8_t ydata_get(uint8_t valve_index, uint32_t my74hc165_data);

#endif