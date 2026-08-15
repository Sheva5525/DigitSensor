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
extern ucg_t ucg;

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
            // ДЕЙСТВИЕ: Кнопка энкодера успешно нажата!
            // Например: отправка события или смена состояния меню
        }
    }
}
volatile int32_t my_encoder_counter = 0;

void vEncoderPollTask(void *pvParameters)
{
    int16_t last_tim_cnt = 0;
    int16_t current_tim_cnt = 0;
    int16_t delta = 0;
    int16_t accumulator = 0;

    TIM4->CNT = 0;

    while (1) {
        current_tim_cnt = (int16_t)TIM4->CNT;
        delta = current_tim_cnt - last_tim_cnt;

        if (delta != 0) {
            accumulator += delta;
            last_tim_cnt = current_tim_cnt;

            if (accumulator >= 2)
            {
                my_encoder_counter++; 
                accumulator %= 2; 
            }
            else if (accumulator <= -2)
            {
                my_encoder_counter--;
                accumulator %= 2;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void int_to_str(int32_t num, char *str) {
    int i = 0;
    int is_negative = 0;

    if (num == 0) {
        str[i++] = '0';
        str[i] = '\0';
        return;
    }

    if (num < 0) {
        is_negative = 1;
        num = -num;
    }

    while (num > 0) {
        str[i++] = (num % 10) + '0';
        num /= 10;
    }

    if (is_negative) {
        str[i++] = '-';
    }

    str[i] = '\0';

    // Разворачиваем строку
    int j;
    char temp;
    for (j = 0; j < i / 2; j++) {
        temp = str[j];
        str[j] = str[i - j - 1];
        str[i - j - 1] = temp;
    }
}

void vGuiTask(void *pvParameters) 
{
    // Назначаем ИНДЕКС 1 как цвет фона системы (Глубокий синий)
    ucg_SetColor(&ucg, 1, 0, 0, 150); 
    
    // Очищаем ВЕСЬ экран цветом из Индекса 1 (Синим) — это самый правильный и быстрый способ
    ucg_ClearScreen(&ucg);

    // Назначаем ИНДЕКС 0 как основной цвет рисования (Ярко-жёлтый)
    ucg_SetColor(&ucg, 0, 255, 255, 0); 
    
    // Рисуем рамку жёлтым цветом (используется idx=0)
    ucg_DrawFrame(&ucg, 4, 4, ucg_GetWidth(&ucg) - 8, ucg_GetHeight(&ucg) - 8);

    // Выводим заголовок жёлтым цветом (используется idx=0)
    ucg_DrawString(&ucg, 15, 35, 0, "ENCODER:");

    int32_t last_displayed_value = 999999; 
    char enc_str[16]; 

    while (1) {
        int32_t current_value = my_encoder_counter;

        if (current_value != last_displayed_value) {
            
            // 1. Чтобы стереть старые цифры, мы временно переключаем ИНДЕКС 0 в цвет фона (Синий)
            ucg_SetColor(&ucg, 0, 0, 0, 150); 
            ucg_DrawBox(&ucg, 15, 45, 95, 22); 

            // 2. Переводим значение в строку
            int_to_str(current_value, enc_str);

            // 3. Возвращаем ИНДЕКС 0 в БЕЛЫЙ цвет для отрисовки новых цифр счетчика
            ucg_SetColor(&ucg, 0, 255, 255, 255); 
            ucg_DrawString(&ucg, 15, 63, 0, enc_str);

            last_displayed_value = current_value;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
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
    
    xTaskCreate(vGuiTask, "GuiTask", 512, NULL, 2, NULL);

    vTaskStartScheduler();

    while (1);

    return 0;
}
