#include "Sensors.h"

#include <math.h>

#define PT1000_R0 1000.0f
#define PT1000_A  3.9083e-3f
#define PT1000_B  -5.775e-7f
#define PT1000_C  -4.183e-12f

float Pt1000_TempToOhm(float temp)
{
    if (temp >= 0.0f)
    {
        return PT1000_R0 * (1.0f + PT1000_A * temp + PT1000_B * temp * temp);
    }
    else
    {
        return PT1000_R0 * (1.0f + PT1000_A * temp + PT1000_B * temp * temp + 
               PT1000_C * (temp - 100.0f) * temp * temp * temp);
    }
}

float Pt1000_OhmToTemp(float ohm)
{
    if (ohm >= PT1000_R0)
    {
        return (-PT1000_A + sqrtf(PT1000_A * PT1000_A - 4.0f * PT1000_B * (1.0f - ohm / PT1000_R0))) / (2.0f * PT1000_B);
    }
    else
    {
        return (ohm - PT1000_R0) / (PT1000_R0 * PT1000_A);
    }
}
