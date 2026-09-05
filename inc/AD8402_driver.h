#ifndef AD8402_DRIVER_H
#define AD8402_DRIVER_H

#include "stm32f4xx.h"
#include <stdint.h>

#define POT1_WRITE(channel, value)  AD8402_Write(GPIO_BSRR_BR4, GPIO_BSRR_BS4, channel, value)

#define POT2_WRITE(channel, value)  AD8402_Write(GPIO_BSRR_BR2, GPIO_BSRR_BS2, channel, value)

void AD8402_Write(uint32_t pin_reset_mask, uint32_t pin_set_mask, uint8_t channel, uint8_t value);

#endif // AD8402_DRIVER_H
