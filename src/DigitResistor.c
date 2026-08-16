#include "DigitResistor.h"
#include <float.h>

extern float calibrate[255];
 
float DigitalRes_GetResistance(const DigitalRes* obj)
{
    return calibrate[obj->current_resolution];
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

    for (uint32_t res1 = 0; res1 <= pot1->resolution; ++res1)
    {
        float r1 = calibrate[res1];

        if (r1 <= target_ohm)
        {
            continue; 
        }

        float r2_needed = (r1 * target_ohm) / (r1 - target_ohm);
        uint32_t closest_index = 0;
        float min_diff = fabsf(r2_needed - calibrate[0]);

        for (uint32_t i = 0; i < 255; i++) 
        {
            float current_diff = fabsf(r2_needed - calibrate[i]);
            if (current_diff < min_diff) 
            {
                min_diff = current_diff;
                closest_index = i;
            }
        }

        uint32_t res2 = 0;
        if (closest_index >= pot2->resolution) 
        {
            res2 = pot2->resolution;
        }
        else
        {
            res2 = closest_index;
        }

        if (res2 == 0)
        {
            continue;
        }

        float r2_actual = calibrate[res2];
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

float calibrate[255] =
{
    52.33f,
    58.50f,
    62.40f,
    67.20f,
    71.20f,
    76.00f,
    80.00f,
    84.80f,
    88.80f,
    93.70f,
    97.60f,
    102.40f,
    106.40f,
    111.30f,
    115.10f,
    119.90f,
    122.40f,
    128.60f,
    132.40f,
    137.20f,
    141.20f,
    146.00f,
    150.00f,
    154.80f,
    158.70f,
    163.60f,
    167.50f,
    172.30f,
    176.40f,
    181.20f,
    185.10f,
    189.80f,
    190.80f,
    197.00f,
    200.90f,
    205.70f,
    209.80f,
    214.60f,
    218.50f,
    223.80f,
    227.80f,
    232.80f,
    236.70f,
    241.50f,
    245.60f,
    250.40f,
    254.30f,
    259.00f,
    259.50f,
    265.70f,
    269.70f,
    274.50f,
    278.50f,
    283.40f,
    287.30f,
    292.20f,
    296.10f,
    301.00f,
    304.90f,
    309.90f,
    313.90f,
    318.70f,
    322.60f,
    327.40f,
    330.00f,
    336.20f,
    340.00f,
    344.90f,
    349.00f,
    353.80f,
    357.80f,
    362.60f,
    366.60f,
    371.50f,
    375.40f,
    380.20f,
    384.20f,
    389.20f,
    393.00f,
    397.80f,
    399.70f,
    405.90f,
    409.70f,
    414.60f,
    418.60f,
    423.50f,
    427.50f,
    432.30f,
    436.30f,
    441.20f,
    445.10f,
    449.90f,
    454.00f,
    458.80f,
    462.80f,
    467.50f,
    469.60f,
    475.80f,
    479.70f,
    484.50f,
    488.50f,
    493.30f,
    497.30f,
    502.20f,
    506.20f,
    511.10f,
    515.00f,
    519.80f,
    523.90f,
    528.70f,
    532.60f,
    537.40f,
    538.00f,
    544.30f,
    548.20f,
    553.00f,
    557.00f,
    561.90f,
    565.90f,
    570.70f,
    574.70f,
    579.60f,
    583.50f,
    588.30f,
    592.40f,
    597.20f,
    601.10f,
    605.90f,
    606.90f,
    613.10f,
    617.10f,
    621.90f,
    626.00f,
    630.80f,
    634.80f,
    639.60f,
    643.50f,
    648.50f,
    652.40f,
    657.30f,
    661.30f,
    666.20f,
    670.00f,
    674.90f,
    676.20f,
    682.40f,
    686.30f,
    691.10f,
    695.30f,
    700.10f,
    704.00f,
    708.90f,
    712.90f,
    717.70f,
    721.60f,
    726.50f,
    730.50f,
    735.40f,
    739.30f,
    744.00f,
    744.30f,
    750.50f,
    754.40f,
    759.30f,
    763.30f,
    768.10f,
    772.20f,
    777.00f,
    781.00f,
    785.90f,
    789.80f,
    794.60f,
    798.60f,
    803.50f,
    807.30f,
    812.20f,
    813.00f,
    819.20f,
    823.00f,
    827.80f,
    831.90f,
    836.70f,
    840.70f,
    845.50f,
    849.50f,
    854.50f,
    858.30f,
    863.20f,
    867.20f,
    872.10f,
    875.90f,
    880.70f,
    882.50f,
    888.80f,
    892.60f,
    897.50f,
    901.50f,
    906.40f,
    910.40f,
    915.20f,
    919.10f,
    924.10f,
    928.00f,
    932.90f,
    936.90f,
    941.70f,
    945.60f,
    950.40f,
    951.20f,
    957.40f,
    961.30f,
    966.20f,
    970.30f,
    975.10f,
    979.00f,
    983.90f,
    987.80f,
    992.80f,
    996.70f,
    1001.60f,
    1005.60f,
    1010.50f,
    1014.30f,
    1019.10f,
    1020.60f,
    1026.80f,
    1030.60f,
    1035.50f,
    1039.60f,
    1044.50f,
    1048.40f,
    1053.30f,
    1057.20f,
    1062.10f,
    1066.00f,
    1070.90f,
    1074.90f,
    1079.80f,
    1083.70f,
    1088.50f,
    1089.60f,
    1095.80f,
    1099.70f,
    1104.50f,
    1108.50f,
    1113.40f,
    1117.50f,
    1122.30f,
    1126.30f,
    1131.20f,
    1135.10f,
    1140.00f,
    1143.90f,
    1148.80f,
    1152.60f
};
