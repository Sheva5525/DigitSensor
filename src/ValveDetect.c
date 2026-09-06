#include "ValveDetect.h"
#include "DataBase.h"
#include "FreeRTOS.h"
#include "task.h"
#include "stm32f4xx.h"
#include <stdbool.h>

#define NET_PERIOD_MIN_US  15000UL // Минимальный период сетевой волны (15 мс)
#define TIMEOUT_220V_MS     30UL

TaskHandle_t xNetworkDetectorTaskHandle = NULL;

volatile uint32_t ch1_pulses = 0; // Для PA3 (Открытие)
volatile uint32_t ch2_pulses = 0; // Для PB10 (Закрытие)

volatile uint32_t last_capture_ch3 = 0; 
volatile uint32_t last_capture_ch4 = 0;

int32_t valve_time_ms = 0;           // Текущее положение (0 ... VALVE_FULL_TIME_MS)
float valve_percent = 0.0f;

void TIM2_IRQHandler(void)
{
    if (TIM2->SR & TIM_SR_CC3IF) 
    {
        TIM2->SR = ~TIM_SR_CC3IF;
        uint32_t current_capture = TIM2->CCR3;
        
        if ((current_capture - last_capture_ch3) >= NET_PERIOD_MIN_US)
        {
            ch2_pulses++; 
            last_capture_ch3 = current_capture; 
        }
    }

    if (TIM2->SR & TIM_SR_CC4IF) 
    {
        TIM2->SR = ~TIM_SR_CC4IF;
        uint32_t current_capture = TIM2->CCR4;
        
        if ((current_capture - last_capture_ch4) >= NET_PERIOD_MIN_US)
        {
            ch1_pulses++;
            last_capture_ch4 = current_capture;
        }
    }
}

uint16_t ADC1_Read_PB0(void)
{
    ADC1->CR2 |= ADC_CR2_SWSTART;

    while (!(ADC1->SR & ADC_SR_EOC));

    return (uint16_t)(ADC1->DR);
}

void vValveDetect()
{
    uint32_t ch1_timeout = 0;
    uint32_t ch2_timeout = 0;
    bool mode_0_10v_enabled = 1;
    DB_Value_t valve_t, max_sec_open;

    if (!DB_Select(VALVE_OPEN_TIME, &max_sec_open))
        max_sec_open.raw_data = 200;
    
    uint32_t max_time_ms = max_sec_open.raw_data * 1000UL;

    DB_Value_t last_percent;
    if (DB_Select(VALVE_PERCENT, &last_percent)) 
    {
        if (last_percent.raw_data > 100) last_percent.raw_data = 100;
        
        valve_percent = (float)last_percent.raw_data;
        valve_time_ms = (int32_t)((valve_percent / 100.0f) * max_time_ms);
    } 
    else 
    {
        valve_percent = 0.0f;
        valve_time_ms = 0;
    }

    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(10));
        
        if (!DB_Select(VALVE_TYPE, &valve_t))
        {
            valve_t.raw_data = 0;
        }

        if (!DB_Select(VALVE_OPEN_TIME, &max_sec_open))
        {
            max_sec_open.raw_data = 200;
        }
        max_time_ms = max_sec_open.raw_data * 1000UL;

        bool type_valve = (valve_t.raw_data == 0);

        if (type_valve) 
        {
            uint16_t adc_value = ADC1_Read_PB0();
            valve_percent = ((float)adc_value / 4095.0f) * 100.0f;

            valve_time_ms = (int32_t)((valve_percent / 100.0f) * max_time_ms);
        }
        else 
        {
            uint32_t local_ch1_pulses = 0;
            uint32_t local_ch2_pulses = 0;

            taskENTER_CRITICAL();

            if (ch1_pulses > 0)
            {
                local_ch1_pulses = ch1_pulses;
                ch1_pulses = 0;
            }
            if (ch2_pulses > 0)
            {
                local_ch2_pulses = ch2_pulses;
                ch2_pulses = 0;
            }
            taskEXIT_CRITICAL();

            // --- Логика Канала 1 (PA3 - Открытие) ---
            if (local_ch1_pulses > 0)
            {
                ch1_timeout = 0;
                valve_time_ms += (local_ch1_pulses * 20); // каждый импульс = 20 мс
            }
            else
            {
                ch1_timeout += 10;
            }

            if (local_ch2_pulses > 0)
            {
                ch2_timeout = 0;
                valve_time_ms -= (local_ch2_pulses * 20);
            }
            else
            {
                ch2_timeout += 10;
            }

            if (valve_time_ms > (int32_t)max_time_ms)
            {
                valve_time_ms = max_time_ms;
            }
            if (valve_time_ms < 0)
            {
                valve_time_ms = 0;
            }

            valve_percent = ((float)valve_time_ms / max_time_ms) * 100.0f;
        }

        DB_Insert(VALVE_PERCENT, (DB_Value_t){
            .is_readable = true,
            .save_to_flash = true,
            .raw_data = (uint8_t)valve_percent,  // целое число процентов
            .type = 0x0,
            .min = 0,
            .max = 100,
            .step = 1,
            .is_enabled = false
        });
    }
}
