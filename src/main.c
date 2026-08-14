#include "stm32f4xx.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "DataBase.h"
#include "xmodem.h"
#include "hardware.h"
#include <string.h>

void vLedFlashTask(void *pvParameters)
{
    (void)pvParameters;
    while (1) {
        GPIOC->ODR ^= (1UL << 13);   // Инверсия PC13
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void vReceiveTask(void *pvParameters)
{
    (void)pvParameters;
    // Даем время на стабилизацию UART и запуск системы
    vTaskDelay(pdMS_TO_TICKS(1000));  

    // Мигнём 2 раза, показывая готовность к приему
    for (int i = 0; i < 4; i++) {
        GPIOC->ODR ^= (1UL << 13);
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    // Вызываем ваш кастомный приём: сначала длина (4 байта), затем данные
    // Таймаут ожидания каждого байта увеличен до 5000 мс
    g_receivedFile.length = SimpleReceiveFile(g_receivedFile.data,
                                              sizeof(g_receivedFile.data),
                                              5000);

    if (g_receivedFile.length > 0) {
        // Сохранение в Flash
        if (DB_StoreFile(g_receivedFile.data, g_receivedFile.length)) {
            // Успех и приёма, и сохранения: 10 быстрых миганий
            for (int i = 0; i < 10; i++) {
                GPIOC->ODR ^= (1UL << 13);
                vTaskDelay(pdMS_TO_TICKS(100));
            }
        } else {
            // Приём успешен, но сохранение не удалось: 2 длинных мигания
            for (int i = 0; i < 2; i++) {
                GPIOC->ODR ^= (1UL << 13);
                vTaskDelay(pdMS_TO_TICKS(800));
            }
        }
    } else {
        // ОШИБКА: 3 медленных мигания
        for (int i = 0; i < 6; i++) {
            GPIOC->ODR ^= (1UL << 13);
            vTaskDelay(pdMS_TO_TICKS(300));
        }
    }

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

int main(void)
{
    xUartQueue = xQueueCreate(512, sizeof(uint8_t));
    if (xUartQueue == NULL)
    {
        while (1);
    }

    hardware_init();
    DB_Init();

    xTaskCreate(vLedFlashTask, "LED", 256, NULL, 1, NULL);
    xTaskCreate(vReceiveTask, "Receive", 1024, NULL, 1, NULL);

    vTaskStartScheduler();

    while (1);
    
    return 0;
}
