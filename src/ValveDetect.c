#include "ValveDetect.h"
#include "FreeRTOS.h"
#include "task.h"
#include "stm32f4xx.h"

TaskHandle_t xNetworkDetectorTaskHandle = NULL;

volatile uint32_t ch1_pulses = 0; // Для PA3 (Открытие)
volatile uint32_t ch2_pulses = 0; // Для PB10 (Закрытие)

volatile uint32_t last_capture_ch3 = 0; 
volatile uint32_t last_capture_ch4 = 0;

#define NET_PERIOD_MIN_US  15000UL // Минимальный период сетевой волны (15 мс)

void TIM2_IRQHandler(void)
{
    // Проверяем Канал 3 (PB10 - Закрытие)
    if (TIM2->SR & TIM_SR_CC3IF) 
    {
        TIM2->SR = ~TIM_SR_CC3IF; // Сброс флага
        uint32_t current_capture = TIM2->CCR3;
        
        if ((current_capture - last_capture_ch3) >= NET_PERIOD_MIN_US)
        {
            ch2_pulses++; 
            last_capture_ch3 = current_capture; 
        }
    }
    
    // Проверяем Канал 4 (PA3 - Открытие)
    if (TIM2->SR & TIM_SR_CC4IF) 
    {
        TIM2->SR = ~TIM_SR_CC4IF; // Сброс флага
        uint32_t current_capture = TIM2->CCR4;
        
        if ((current_capture - last_capture_ch4) >= NET_PERIOD_MIN_US)
        {
            ch1_pulses++;
            last_capture_ch4 = current_capture;
        }
    }
}

#define VALVE_FULL_TIME_MS  100000UL // 100 секунд на полное открытие
#define TIMEOUT_220V_MS     30UL

int32_t valve_time_ms = 0;           // Текущее положение (0 ... VALVE_FULL_TIME_MS)
float valve_percent = 0.0f;

void vValveDetect()
{
    uint32_t ch1_timeout = 0;
    uint32_t ch2_timeout = 0;

    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(10)); // Шаг таска

        uint32_t local_ch1_pulses = 0;
        uint32_t local_ch2_pulses = 0;

        // Атомарно забираем накопленные импульсы
        taskENTER_CRITICAL();
        if (ch1_pulses > 0) {
            local_ch1_pulses = ch1_pulses;
            ch1_pulses = 0;
        }
        if (ch2_pulses > 0) {
            local_ch2_pulses = ch2_pulses;
            ch2_pulses = 0;
        }
        taskEXIT_CRITICAL();

        // --- Логика Канала 1 (PA3 - Открытие) ---
        if (local_ch1_pulses > 0) {
            ch1_timeout = 0;
            // Каждый импульс добавляет ровно 20 мс работы сети
            valve_time_ms += (local_ch1_pulses * 20); 
        } else {
            ch1_timeout += 10;
        }

        // --- Логика Канала 2 (PB10 - Закрытие) ---
        if (local_ch2_pulses > 0) {
            ch2_timeout = 0;
            // Каждый импульс отнимает ровно 20 мс
            valve_time_ms -= (local_ch2_pulses * 20); 
        } else {
            ch2_timeout += 10;
        }

        // Ограничиваем физические рамки хода клапана (0...100 сек)
        if (valve_time_ms > (int32_t)VALVE_FULL_TIME_MS) valve_time_ms = VALVE_FULL_TIME_MS;
        if (valve_time_ms < 0) valve_time_ms = 0;

        // Считаем проценты для вывода на экран или логики
        valve_percent = ((float)valve_time_ms / VALVE_FULL_TIME_MS) * 100.0f;
    }
}

