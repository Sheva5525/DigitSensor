#include "DataBase.h"
#include "ymodem.h"
#include "iPawn.h"
#include "ucg.h"
#include "hardware.h"
#include "UI.h"
#include "Sensors.h"
#include "DigitResistor.h"
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

static bool g_ymodem_mode = false;

extern uint8_t aFileName[FILE_NAME_LENGTH]; 

void vReceiveTask(void *pvParameters)
{
    // Если включен режим загрузки по YMODEM
    if (g_ymodem_mode) {
        uint32_t ymodem_file_size = 0;
        COM_StatusTypeDef ymodem_status = Ymodem_Receive(&ymodem_file_size);

        if (ymodem_status == COM_OK && ymodem_file_size > 0) {
            // Успех! Записываем точный размер файла
            g_receivedFile.length = ymodem_file_size;

            // Сохраняем принятый файл в Flash
            if (!DB_StoreFile(g_receivedFile.data, g_receivedFile.length)) {
                // Ошибка сохранения (данные остаются в ОЗУ)
            }
        } else {
            // Приём не удался — пробуем прочитать старый файл из Flash
            if (!DB_ReadFile(g_receivedFile.data, sizeof(g_receivedFile.data), &g_receivedFile.length)) {
                g_receivedFile.length = 0;
            }
        }
    } else {
        // Обычный режим — просто читаем сохранённый файл из Flash
        if (!DB_ReadFile(g_receivedFile.data, sizeof(g_receivedFile.data), &g_receivedFile.length)) {
            g_receivedFile.length = 0;
        }
    }

    // Уведомляем Pawn-задачу, если она существует
    if (xPawnTaskHandle != NULL) {
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
    DB_Value_t value1, value2, target_ohm, main_switch;
    
    DigitalRes hard = { .ratedRes = 1170, .resolution = 255, .current_resolution = 0 };
    DigitalRes soft = { .ratedRes = 1170, .resolution = 255, .current_resolution = 0 };
    
    uint32_t best_step1 = 0;
    uint32_t best_step2 = 0;

    // Переменные для отслеживания последних применённых значений
    int32_t last_applied_target = -1;
    int32_t last_applied_value1 = -1;
    int32_t last_applied_value2 = -1;

    for (;;)
    {
        // Читаем главный выключатель (индекс 3)
        if (!DB_Select(3, &main_switch)) {
            main_switch.raw_data = 0;
        }

        // Определяем, разрешено ли ручное редактирование (main_switch == 0)
        bool edit_enabled = (main_switch.raw_data == 0);

        // Обновляем флаг is_enabled для параметров 0,1,2 в БД
        for (uint8_t key = 0; key <= 2; key++) {
            DB_Value_t tmp;
            if (DB_Select(key, &tmp)) {
                if (tmp.is_enabled != edit_enabled) {
                    tmp.is_enabled = edit_enabled;
                    DB_Insert(key, tmp);
                }
            }
        }

        if (main_switch.raw_data == 0)  // Ручной режим
        {
            // Блокируем Pawn, если ещё не заблокирован
            if (!is_pawn_suspended && xPawnTaskHandle != NULL) {
                vTaskSuspend(xPawnTaskHandle);
                is_pawn_suspended = true;
            }

            // 1. Обрабатываем Target Ohm (индекс 2)
            if (DB_Select(2, &target_ohm)) {
                if (target_ohm.raw_data != last_applied_target) {
                    // Вычисляем оптимальные шаги для обоих каналов
                    FindOptimalSteps(&hard, &soft, target_ohm.raw_data, &best_step1, &best_step2);

                    // Отправляем на AD8402
                    AD8402_Write(0, best_step1);
                    AD8402_Write(1, best_step2);
                    step1 = best_step1;
                    step2 = best_step2;

                    // Обновляем кэш применённых значений
                    last_applied_target = target_ohm.raw_data;
                    last_applied_value1 = best_step1;
                    last_applied_value2 = best_step2;

                    // Записываем новые шаги в БД, чтобы UI показывал актуальные значения
                    DB_Value_t new_val1;
                    if (DB_Select(0, &new_val1)) {
                        new_val1.raw_data = best_step1;
                    } else {
                        new_val1 = (DB_Value_t){
                            .is_readable = true,
                            .save_to_flash = true,
                            .type = 0x0,
                            .min = 0,
                            .max = 255,
                            .step = 1,
                            .is_enabled = true
                        };
                        new_val1.raw_data = best_step1;
                    }
                    DB_Insert(0, new_val1);

                    DB_Value_t new_val2;
                    if (DB_Select(1, &new_val2)) {
                        new_val2.raw_data = best_step2;
                    } else {
                        new_val2 = (DB_Value_t){
                            .is_readable = true,
                            .save_to_flash = true,
                            .type = 0x0,
                            .min = 0,
                            .max = 255,
                            .step = 1,
                            .is_enabled = true
                        };
                        new_val2.raw_data = best_step2;
                    }
                    DB_Insert(1, new_val2);
                }
            }

            // 2. Обрабатываем прямое изменение Channel 0 (индекс 0)
            if (DB_Select(0, &value1)) {
                if (value1.raw_data != last_applied_value1) {
                    AD8402_Write(0, value1.raw_data);
                    step1 = value1.raw_data;
                    last_applied_value1 = value1.raw_data;

                    // Если target_ohm был применён ранее, сбрасываем его флаг,
                    // чтобы не было конфликтов (канал изменён вручную)
                    last_applied_target = -1;
                }
            }

            // 3. Обрабатываем прямое изменение Channel 1 (индекс 1)
            if (DB_Select(1, &value2)) {
                if (value2.raw_data != last_applied_value2) {
                    AD8402_Write(1, value2.raw_data);
                    step2 = value2.raw_data;
                    last_applied_value2 = value2.raw_data;

                    last_applied_target = -1;
                }
            }

            vTaskDelay(pdMS_TO_TICKS(100));
        }
        else  // Автоматический режим (Pawn управляет)
        {
            // Разблокируем Pawn, если он был заблокирован
            if (is_pawn_suspended && xPawnTaskHandle != NULL) {
                vTaskResume(xPawnTaskHandle);
                is_pawn_suspended = false;
            }

            // В этом режиме мы не трогаем потенциометры, ими управляет Pawn.
            // Сбрасываем кэш применённых значений, чтобы при следующем переключении
            // в ручной режим все текущие значения из БД были применены.
            last_applied_target = -1;
            last_applied_value1 = -1;
            last_applied_value2 = -1;

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
    
    DB_Insert(0, (DB_Value_t){ .is_readable = true, .save_to_flash = true, .raw_data = 0,  .type = 0x0, .min = 0,   .max = 255, .step = 1, .is_enabled = true });
    DB_Insert(1, (DB_Value_t){ .is_readable = true, .save_to_flash = true, .raw_data = 0,  .type = 0x0, .min = 0,   .max = 255, .step = 1, .is_enabled = true });
    DB_Insert(2, (DB_Value_t){ .is_readable = true, .save_to_flash = true, .raw_data = 50, .type = 0x0, .min = 50,  .max = 500, .step = 1, .is_enabled = true });
    DB_Insert(3, (DB_Value_t){ .is_readable = true, .save_to_flash = true, .raw_data = 1,  .type = 0x0, .min = 0,   .max = 1,   .step = 1, .is_enabled = true });
    
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
    
    if (!(GPIOB->IDR & GPIO_IDR_IDR_1)) {
        g_ymodem_mode = true;
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
