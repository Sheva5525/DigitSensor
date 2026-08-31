#include "ValveDetect.h"
#include "FreeRTOS.h"
#include "task.h"
#include "stm32f4xx.h"

TaskHandle_t xNetworkDetectorTaskHandle = NULL;

volatile uint8_t is_220v_alive = 0;
volatile uint32_t total_network_present_us = 0;
volatile uint32_t net_t_start = 0;
volatile uint32_t net_t_last = 0;
volatile uint8_t  new_data_flag = 0;
volatile uint8_t  network_active_isr = 0; 

void TIM2_IRQHandler(void)
{
    if (TIM2->SR & TIM_SR_CC1IF) 
    {
        uint32_t capture = TIM2->CCR1;

        if (!network_active_isr)
        {
            net_t_start = capture;
            net_t_last = capture; 
            network_active_isr = 1;
        }
        else
        {
            net_t_last = capture; 
        }

        new_data_flag = 1;
    }
}

void vValveDetect()
{
    uint32_t timeout_counter_ms = 0;

    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(10));

        taskENTER_CRITICAL();
        uint8_t  has_new = new_data_flag;
        uint32_t t_start = net_t_start;
        uint32_t t_last  = net_t_last;
        if (has_new) {
            new_data_flag = 0; 
        }
        taskEXIT_CRITICAL();

        if (has_new) 
        {
            timeout_counter_ms = 0;
            is_220v_alive = 1;

            total_network_present_us = t_last - t_start;

            if (total_network_present_us > 3000000000UL) // 30 ms
            {
                taskENTER_CRITICAL();
                net_t_start = t_last;
                taskEXIT_CRITICAL();
            }
        }
        else
        {
            if (is_220v_alive == 1) 
            {
                timeout_counter_ms += 10;

                if (timeout_counter_ms >= 30)
                {
                    is_220v_alive = 0;

                    taskENTER_CRITICAL();
                    network_active_isr = 0;

                    net_t_start = 0;
                    net_t_last = 0;
                    taskEXIT_CRITICAL();

                    total_network_present_us = t_last - t_start;
                    timeout_counter_ms = 0;
                }
            }
        }
    }
}
