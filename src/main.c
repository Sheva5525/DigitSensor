#include "DataBase.h"
#include "ymodem.h"
#include "iPawn.h"
#include "ucg.h"
#include "hardware.h"
#include "UI.h"
#include "AD8402_driver.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "timers.h"
#include "stm32f4xx.h"
#include <stdio.h>
#include <string.h>

extern QueueHandle_t xUartQueue;
extern TaskHandle_t xPawnTaskHandle;

void vDbSyncTimerCallback(TimerHandle_t xTimer)
{
    (void)xTimer;
    DB_Sync();
}

extern uint8_t aFileName[FILE_NAME_LENGTH]; 

void vReceiveTask(void *pvParameters)
{
    //vTaskDelay(pdMS_TO_TICKS(1000));

    // Переменная для записи точного размера файла от YMODEM
    uint32_t ymodem_file_size = 0;
    //xQueueReset(xUartQueue);
    // Запускаем прием по YMODEM (таймауты зашиты внутри него)
//    COM_StatusTypeDef ymodem_status = Ymodem_Receive(&ymodem_file_size);

//    if (ymodem_status == COM_OK && ymodem_file_size > 0)
//    {
//        // Успех! Записываем точный размер файла (без мусора округления блоков)
//        g_receivedFile.length = ymodem_file_size;
//        
//        /* 
//           БОНУС: Теперь в массиве (char*)aFileName у вас лежит реальное имя файла 
//           (например, "setup.bin"). Вы можете использовать его, если нужно.
//        */

//        // Принят новый файл — сохраняем его в Flash через ваш DB менеджер
//        if (!DB_StoreFile(g_receivedFile.data, g_receivedFile.length))
//        {
//            // Ошибка сохранения, но данные в ОЗУ есть
//        }
//    }
//    else
//    {
//        // Приём по YMODEM не удался — пробуем прочитать старый файл из Flash
//        if (!DB_ReadFile(g_receivedFile.data, sizeof(g_receivedFile.data), &g_receivedFile.length))
//        {
//            // Файла тоже нет — длина останется 0
//            g_receivedFile.length = 0;
//        }
//    }

    if (!DB_ReadFile(g_receivedFile.data, sizeof(g_receivedFile.data), &g_receivedFile.length))
    {
        g_receivedFile.length = 0;
    }

    // Уведомляем vPawnTask, если есть данные
    if (xPawnTaskHandle != NULL)
    {
        xTaskNotifyGive(xPawnTaskHandle);
    }

    vTaskDelete(NULL);
}

static bool is_pawn_suspended = false;

void vPawnTask(void *pvParameters)
{
    PawnTask();
}

TaskHandle_t xEncoderButtonTaskHandle;

void vEncButtonTask(void *pvParameters)
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

volatile int32_t my_encoder_counter = 0;

void vEncoderPollTask(void *pvParameters)
{
    int16_t last_tim_cnt = 0;
    int16_t current_tim_cnt = 0;
    int16_t delta = 0;

    TIM4->CNT = 0;
    last_tim_cnt = 0;

    while (1) {
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
        else if (delta <= -STEPS_PER_CLICK) {
            vTaskSuspendAll();
            UI_ProcessNavigate(-1);
            xTaskResumeAll();
            
            last_tim_cnt -= STEPS_PER_CLICK;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void vResistorControlTask(void *pvParameters)
{
    (void)pvParameters;
    uint32_t step1 = 0xFFFFFFFF, step2 = 0xFFFFFFFF; 
    DB_Value_t value1 = {0}, value2 = {0};
    DB_Value_t main_switch = {0};

    for (;;)
    {
        DB_Select(0, &main_switch);

        if (main_switch.raw_data == 0)
        {
            if (step1 != 0 || step2 != 0) {
                AD8402_Write(0, 0);
                AD8402_Write(1, 0);
                step1 = 0; step2 = 0;
            }

            if (!is_pawn_suspended && xPawnTaskHandle != NULL)
            {
                vTaskSuspend(xPawnTaskHandle);
                is_pawn_suspended = true;
            }

            vTaskDelay(pdMS_TO_TICKS(100));
        }
        else
        {
            if (is_pawn_suspended && xPawnTaskHandle != NULL)
            {
                vTaskResume(xPawnTaskHandle);
                is_pawn_suspended = false;
            }

            DB_Select(1, &value1);
            DB_Select(2, &value2);

            if (value1.raw_data != step1)
            {
                AD8402_Write(0, value1.raw_data);
                step1 = value1.raw_data;
            }
            if (value2.raw_data != step2)
            {
                AD8402_Write(1, value2.raw_data);
                step2 = value2.raw_data;
            }

            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}

int main(void)
{
    xUartQueue = xQueueCreate(512, sizeof(uint8_t));

    hardware_init();
    DB_Init();
    UI_Init();
    
    DB_Insert(0, (DB_Value_t){ .is_readable = true, .save_to_flash = true, .raw_data = 0, .type = 0x0 });
    DB_Insert(1, (DB_Value_t){ .is_readable = true, .save_to_flash = true, .raw_data = 1, .type = 0x0 });
    DB_Insert(2, (DB_Value_t){ .is_readable = true, .save_to_flash = true, .raw_data = 2, .type = 0x0 });

    DB_LoadFromFlash();

    TimerHandle_t xDbTimer = xTimerCreate("DbSyncTimer", 
                                          pdMS_TO_TICKS(5000), 
                                          pdTRUE, 
                                          (void*)0, 
                                          vDbSyncTimerCallback);
    
    if (xDbTimer != NULL)
    {
        xTimerStart(xDbTimer, 0);
    }

    xTaskCreate(vResistorControlTask, "Resistors", 256, NULL, 1, NULL);
    xTaskCreate(vReceiveTask, "Receive", 2024, NULL, 2, NULL);
    xTaskCreate(vPawnTask, "PawnVM", 2048, NULL, 1, &xPawnTaskHandle);
    xTaskCreate(vEncButtonTask, "EncBtn", 128, NULL, 3, &xEncoderButtonTaskHandle);
    xTaskCreate(vEncoderPollTask, "EncPoll", 128, NULL, 2, NULL);
    xTaskCreate(vGuiTask, "GuiTask", 2048, NULL, 2, NULL);

    vTaskStartScheduler();

    while (1);

    return 0;
}
