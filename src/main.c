#include "DataBase.h"
#include "ymodem.h"
#include "iPawn.h"
#include "ucg.h"
#include "hardware.h"
#include "UI.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "stm32f4xx.h"
#include <stdio.h>
#include <string.h>

extern QueueHandle_t xUartQueue;
extern TaskHandle_t xPawnTaskHandle;

void vLedFlashTask(void *pvParameters)
{
    (void)pvParameters;
    while (1)
    {
        //GPIOC->ODR ^= (1UL << 13);   // Инверсия PC13
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

extern int16_t ucg_com_stm32_spi_cb(ucg_t *ucg, int16_t msg, uint16_t arg, uint8_t *data);

/* Массив с именем файла, который заполняется внутри ymodem.c */
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

    // Уведомляем vPawnTask, если есть данные (или даже если нет, но тогда она должна обработать 0)
    if (xPawnTaskHandle != NULL)
    {
        xTaskNotifyGive(xPawnTaskHandle);
    }

    vTaskDelete(NULL);
}


void vPawnTask(void *pvParameters)
{
    PawnTask();
}

TaskHandle_t xEncoderButtonTaskHandle;

void vEncButtonTask(void *pvParameters)
{
    while (1) {
        // Задача заблокирована и ждет прерывания от EXTI1
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // Программный антидребезг: ждем 50 мс
        vTaskDelay(pdMS_TO_TICKS(50));
        
        // Проверяем, зажат ли пин PB1 до сих пор (ноль — кнопка нажата)
        if (!(GPIOB->IDR & GPIO_IDR_IDR_1)) {
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

        // ФИЛЬТР ЭНКОДЕРА: 
        // Большинство энкодеров выдают 2 или 4 шага на 1 физический щелчок.
        // Если у вас перескакивает, поменяйте значение '2' ниже на '4'.
        const int16_t STEPS_PER_CLICK = 2; 

        if (delta >= STEPS_PER_CLICK) {
            // Крутанули ВПРАВО/ВНИЗ: делаем строго ОДИН шаг навигации
            vTaskSuspendAll(); // Защищаем операцию от прерывания другими тасками
            UI_ProcessNavigate(1);
            xTaskResumeAll();
            
            // Сдвигаем базовый счетчик ровно на отработанный шаг
            last_tim_cnt += STEPS_PER_CLICK;
        } 
        else if (delta <= -STEPS_PER_CLICK) {
            // Крутанули ВЛЕВО/ВВЕРХ: делаем строго ОДИН шаг навигации
            vTaskSuspendAll();
            UI_ProcessNavigate(-1);
            xTaskResumeAll();
            
            last_tim_cnt -= STEPS_PER_CLICK;
        }

        vTaskDelay(pdMS_TO_TICKS(10)); // Опрос каждые 10 мс для мгновенной отзывчивости
    }
}

int main(void)
{
    xUartQueue = xQueueCreate(512, sizeof(uint8_t));

    hardware_init();
    DB_Init();
    UI_Init();

    xTaskCreate(vLedFlashTask, "LED", 256, NULL, 1, NULL);

    xTaskCreate(vReceiveTask, "Receive", 2024, NULL, 2, NULL);

    xTaskCreate(vPawnTask, "PawnVM", 2048, NULL, 1, &xPawnTaskHandle);
    
    xTaskCreate(vEncButtonTask, "EncBtn", 128, NULL, 3, &xEncoderButtonTaskHandle);
    xTaskCreate(vEncoderPollTask, "EncPoll", 128, NULL, 2, NULL);
    
    xTaskCreate(vGuiTask, "GuiTask", 2048, NULL, 2, NULL);

    vTaskStartScheduler();

    while (1);

    return 0;
}
