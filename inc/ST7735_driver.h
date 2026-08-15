#ifndef ST7735_DRIVER_H
#define ST7735_DRIVER_H

#include "ucg.h"
#include <stdint.h>

int16_t ucg_com_stm32_spi_cb(ucg_t *ucg, int16_t msg, uint16_t arg, uint8_t *data);

#endif // ST7735_DRIVER_H