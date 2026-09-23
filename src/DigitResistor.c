#include "DigitResistor.h"
#include <float.h>

#include <float.h>
#include <stddef.h>

float calibrate_out1[256];
float calibrate_out2[256];

float DigitalRes_GetChannelResistance(const DualDigitalRes* pot, uint32_t step)
{
    if (pot == NULL || pot->calibrate == NULL || step >= POT_STEPS_COUNT) 
    {
        return 0.0f; 
    }
    return pot->calibrate[step];
}

float ParallelOhm(const DualDigitalRes* pot)
{
    if (pot == NULL) return 0.0f;

    float r1 = DigitalRes_GetChannelResistance(pot, pot->channel0_step);
    float r2 = DigitalRes_GetChannelResistance(pot, pot->channel1_step);
    
    if ((r1 + r2) == 0.0f) return 0.0f;
    
    return (r1 * r2) / (r1 + r2);
}

static uint32_t FindClosestStep(const float* calibrate, float target_r)
{
    if (target_r <= calibrate[0]) return 0;
    if (target_r >= calibrate[POT_STEPS_COUNT - 1]) return POT_STEPS_COUNT - 1;

    uint32_t low = 0;
    uint32_t high = POT_STEPS_COUNT - 1;

    while (high - low > 1)
    {
        uint32_t mid = low + (high - low) / 2;
        if (calibrate[mid] < target_r)
        {
            low = mid;
        }
        else
        {
            high = mid;
        }
    }

    if (fabsf(calibrate[low] - target_r) < fabsf(calibrate[high] - target_r))
    {
        return low;
    }
    return high;
}

void FindOptimalSteps( const DualDigitalRes* pot
                     , float target_ohm
                     , uint32_t* best_ch0_step
                     , uint32_t* best_ch1_step)
{
    *best_ch0_step = 0;
    *best_ch1_step = 0;

    if (pot == NULL || pot->calibrate == NULL) return;

    if (target_ohm < 50.0f)  target_ohm = 50.0f;
    if (target_ohm > 500.0f) target_ohm = 500.0f;

    float min_error = FLT_MAX;
    DualDigitalRes temp_pot = *pot;

    for (uint32_t ch0 = 0; ch0 < POT_STEPS_COUNT; ++ch0)
    {
        float r1 = pot->calibrate[ch0];

        if (r1 <= target_ohm) continue;

        float required_r2 = (r1 * target_ohm) / (r1 - target_ohm);

        uint32_t ch1 = FindClosestStep(pot->calibrate, required_r2);

        temp_pot.channel0_step = ch0;
        temp_pot.channel1_step = ch1;
        
        float current_ohm = ParallelOhm(&temp_pot);
        float error = fabsf(current_ohm - target_ohm);

        if (error < min_error)
        {
            min_error = error;
            *best_ch0_step = ch0;
            *best_ch1_step = ch1;
        }
    }
}
