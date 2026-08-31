#include "EncoderControl.h"
#include "UI.h"
#include "FreeRTOS.h"
#include "task.h"
#include "stm32f4xx.h"

void vEncoderPoll()
{
    int16_t last_tim_cnt = 0;
    int16_t current_tim_cnt = 0;
    int16_t delta = 0;

    TIM4->CNT = 0;
    last_tim_cnt = 0;

    while (1)
    {
        current_tim_cnt = (int16_t)TIM4->CNT;
        delta = current_tim_cnt - last_tim_cnt;

        const int16_t STEPS_PER_CLICK = 2; 

        if (delta >= STEPS_PER_CLICK)
        {
            vTaskSuspendAll();
            UI_ProcessNavigate(1);
            xTaskResumeAll();
            
            last_tim_cnt += STEPS_PER_CLICK;
        } 
        else if (delta <= -STEPS_PER_CLICK)
        {
            vTaskSuspendAll();
            UI_ProcessNavigate(-1);
            xTaskResumeAll();
            
            last_tim_cnt -= STEPS_PER_CLICK;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void vEncoderButton()
{
    while (1)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        vTaskDelay(pdMS_TO_TICKS(50));

        if (!(GPIOB->IDR & GPIO_IDR_IDR_1))
        {
            UI_ProcessAction(); 
        }
    }
}
