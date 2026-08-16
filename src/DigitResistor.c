#include "DigitResistor.h"
#include <float.h>

float DigitalRes_GetResistance(const DigitalRes* obj)
{
    return ((float)obj->ratedRes / obj->resolution) * obj->current_resolution;
}

float ParallelOhm(const DigitalRes* hard, const DigitalRes* soft)
{
    float r1 = DigitalRes_GetResistance(hard);
    float r2 = DigitalRes_GetResistance(soft);
    
    if ((r1 + r2) == 0.0f) return 0.0f;
    
    return (r1 * r2) / (r1 + r2);
}

void FindOptimalSteps( const DigitalRes* pot1
                     , const DigitalRes* pot2
                     , float target_ohm
                     , uint32_t* best_res1
                     , uint32_t* best_res2)
{
    float step_cost1 = (float)pot1->ratedRes / pot1->resolution;
    float step_cost2 = (float)pot2->ratedRes / pot2->resolution;
    
    float min_error = FLT_MAX;
    *best_res1 = 0;
    *best_res2 = 0;

    for (uint32_t res1 = 1; res1 <= pot1->resolution; ++res1)
    {
        float r1 = step_cost1 * res1;

        if (r1 <= target_ohm)
        {
            continue; 
        }

        float r2_needed = (r1 * target_ohm) / (r1 - target_ohm);
        float exact_res2 = r2_needed / step_cost2;
        
        uint32_t res2 = 0;
        if (exact_res2 >= (float)pot2->resolution) 
        {
            res2 = pot2->resolution;
        }
        else
        {
            res2 = (uint32_t)roundf(exact_res2);
        }

        if (res2 == 0)
        {
            continue;
        }

        float r2_actual = step_cost2 * res2;
        float current_ohm = (r1 * r2_actual) / (r1 + r2_actual);
        float error = fabsf(current_ohm - target_ohm);

        if (error < min_error)
        {
            min_error = error;
            *best_res1 = res1;
            *best_res2 = res2;
        }
    }
}
