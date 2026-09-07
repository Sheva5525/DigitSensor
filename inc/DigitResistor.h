#ifndef DIGITAL_RESISTOR_H
#define DIGITAL_RESISTOR_H

#include <math.h>
#include <stdint.h>

#define POT_STEPS_COUNT 256

typedef struct
{
    uint32_t ratedRes;          // Номинальное сопротивление потенциометра
    uint32_t channel0_step;     // Текущий шаг канала 0 (0 ... 255)
    uint32_t channel1_step;     // Текущий шаг канала 1 (0 ... 255)
    const float* calibrate;     // Указатель на массив калибровочных значений из 256 элементов
} DualDigitalRes;

float DigitalRes_GetChannelResistance(const DualDigitalRes* pot, uint32_t step);

float ParallelOhm(const DualDigitalRes* pot);

void FindOptimalSteps( const DualDigitalRes* pot
                     , float target_ohm
                     , uint32_t* best_ch0_step
                     , uint32_t* best_ch1_step);

#endif // DIGITAL_RESISTOR_H
