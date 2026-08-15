#include "DataBase.h"
#include "iPawn.h"
#include "xmodem.h"
#include "hardware.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "stm32f4xx.h"
#include <string.h>

extern TaskHandle_t xPawnTaskHandle;

void vLedFlashTask(void *pvParameters)
{
    (void)pvParameters;
    while (1) {
        //GPIOC->ODR ^= (1UL << 13);   // Инверсия PC13
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void vReceiveTask(void *pvParameters)
{
    vTaskDelay(pdMS_TO_TICKS(1000));

    g_receivedFile.length = SimpleReceiveFile(g_receivedFile.data,
                                              sizeof(g_receivedFile.data),
                                              100);

    if (g_receivedFile.length == 0)
    {
        // Приём не удался — пробуем прочитать файл из Flash
        if (!DB_ReadFile(g_receivedFile.data, sizeof(g_receivedFile.data), &g_receivedFile.length))
        {
            // Файла тоже нет — длина останется 0
            g_receivedFile.length = 0;
        }
    }
    else
    {
        // Принят новый файл — сохраняем его в Flash
        if (!DB_StoreFile(g_receivedFile.data, g_receivedFile.length))
        {
            // Ошибка сохранения, но данные в ОЗУ есть — можно продолжить
            // или обработать ошибку
        }
    }

    // Уведомляем vPawnTask, если есть данные (или даже если нет, но тогда она должна обработать 0)
    if (xPawnTaskHandle != NULL) {
        xTaskNotifyGive(xPawnTaskHandle);
    }

    vTaskDelete(NULL);
}

void vPawnTask(void *pvParameters)
{
    PawnTask();
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
    
    // Приоритет задачи приема увеличен до 2, чтобы данные XModem не терялись
    xTaskCreate(vReceiveTask, "Receive", 1024, NULL, 2, NULL);
    
    // Исправлено: передаем функцию vPawnTask и сохраняем её хэндл xPawnTaskHandle
    xTaskCreate(vPawnTask, "PawnVM", 2048, NULL, 1, &xPawnTaskHandle);

    vTaskStartScheduler();

    while (1);
    
    return 0;
}
