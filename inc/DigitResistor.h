#ifndef DIGITAL_RESISTOR_H
#define DIGITAL_RESISTOR_H

#include <math.h>
#include <stdint.h>

typedef struct
{
    uint32_t ratedRes;
    uint32_t resolution;
    uint32_t current_resolution;
} DigitalRes;

float ParallelOhm(const DigitalRes* hard, const DigitalRes* soft);

void FindOptimalSteps( const DigitalRes* pot1
                     , const DigitalRes* pot2
                     , float target_ohm
                     , uint32_t* best_res1
                     , uint32_t* best_res2);

float DigitalRes_GetResistance(const DigitalRes* obj);

#endif // DIGITAL_RESISTOR_H
