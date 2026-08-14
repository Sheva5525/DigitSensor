#include "stm32f4xx.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "DataBase.h"
#include "xmodem.h"
#include "hardware.h"
#include <string.h>

// Подключаем заголовочный файл Pawn AMX
#include "amx.h"

// Хэндл задачи для отправки уведомлений
TaskHandle_t xPawnTaskHandle = NULL;

// Выделяем рабочий буфер в RAM под исполняемый скрипт (код + стек/куча скрипта)
#define PAWN_MEMORY_SIZE_BYTES  (1 * 1024) 
__attribute__((aligned(4))) uint8_t amx_run_memory[PAWN_MEMORY_SIZE_BYTES];

void vLedFlashTask(void *pvParameters)
{
    (void)pvParameters;
    while (1) {
        //GPIOC->ODR ^= (1UL << 13);   // Инверсия PC13
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void vReceiveTask(void *pvParameters) {
    vTaskDelay(pdMS_TO_TICKS(1000));

    g_receivedFile.length = SimpleReceiveFile(g_receivedFile.data,
                                              sizeof(g_receivedFile.data),
                                              5000);

    if (g_receivedFile.length == 0) {
        // Приём не удался — пробуем прочитать файл из Flash
        if (!DB_ReadFile(g_receivedFile.data, sizeof(g_receivedFile.data), &g_receivedFile.length)) {
            // Файла тоже нет — длина останется 0
            g_receivedFile.length = 0;
        }
    } else {
        // Принят новый файл — сохраняем его в Flash
        if (!DB_StoreFile(g_receivedFile.data, g_receivedFile.length)) {
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

cell AMXAPI native_SetLedState(AMX *amx, const cell *params)
{
    (void)amx;

    // Разыменовываем указатель, чтобы получить 0 или 1
    cell *arg_ptr = (cell *)params[1];
    int state = (int)(*arg_ptr); 
    
    if (state == 1) {
        GPIOC->BSRR = (1UL << (13 + 16)); // Зажечь LED
    } else {
        GPIOC->BSRR = (1UL << 13);        // Погасить LED
    }
    return 0;
}

cell AMXAPI native_Delay(AMX *amx, const cell *params)
{
    (void)amx;
    
    // Разыменовываем указатель, чтобы получить честные 500 мс
    cell *arg_ptr = (cell *)params[1];
    uint32_t ms = (uint32_t)(*arg_ptr); 
    
    if (ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(ms));
    }

    return 0;
}

// Таблица сопоставления текстовых имен из скрипта с функциями Си
const AMX_NATIVE_INFO stm32_natives[] = {
    { "SetLedState", native_SetLedState },
    { "Delay", native_Delay },
    { NULL,          NULL } // Маркер конца таблицы
};

__attribute__((aligned(4))) uint8_t pawn_data_space[2048];

// Напоминание: Убедитесь, что в верхнюю часть main.c или amx.h добавлены строки:
// #undef  BYTE_ORDER
// #define BYTE_ORDER  2
// А функция check_endian() в amx.c возвращает 0.

void vPawnTask(void *pvParameters)
{
    (void)pvParameters;
    AMX amx;
    int init_error = -1;
    int exec_error = -1;
    cell ret_value = 0;

    while (1) 
    {
        // 1. Ожидаем уведомления от задачи приема файлов по XModem
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // Проверяем лимиты буфера
        if (g_receivedFile.length > PAWN_MEMORY_SIZE_BYTES || g_receivedFile.length == 0) {
            continue; 
        }

        // 2. Копируем файл бинарника из БД в выровненную RAM-память VM "как есть" (с 0-го байта)
        memset(amx_run_memory, 0, PAWN_MEMORY_SIZE_BYTES);
        memcpy(amx_run_memory, g_receivedFile.data, g_receivedFile.length);

        // Сброс контекста структуры AMX перед инициализацией
        memset(&amx, 0, sizeof(amx));
        amx.callback = amx_Callback; 
        
        // Настройка указателей на изолированные области памяти кода и данных (RAM-секцию)
        amx.base = amx_run_memory;
        amx.data = pawn_data_space;
        memset(pawn_data_space, 0, sizeof(pawn_data_space));

        // 3. Инициализируем виртуальную машину
        // Благодаря дефайнам, amx_Init сама развернет Big Endian заголовок в Little Endian
        init_error = amx_Init(&amx, amx_run_memory);
        
        if (init_error == AMX_ERR_NONE) 
        {
            // РЕГИСТРАЦИЯ: Передаем ВМ нашу таблицу с Си-функцией SetLedState
            amx_Register(&amx, stm32_natives, -1);

            // 4. Запускаем функцию main() из откомпилированного скрипта
            exec_error = amx_Exec(&amx, &ret_value, AMX_EXEC_MAIN);
            
            if (exec_error == AMX_ERR_NONE)
            {
                // Проверяем возвращаемое значение нашего скрипта (return 42;)
                if (ret_value == 42)
                {
                    // ПОЛНЫЙ УСПЕХ: Поставьте точку останова сюда для финальной проверки!
                    __NOP(); 
                }
            }
            
            // Освобождаем внутренние ресурсы рантайма Pawn после завершения скрипта
            amx_Cleanup(&amx);
        }
        else
        {
            // Если init_error вернет ошибку, сохраняем ее код для анализа в Watch
            int broken_init = init_error;
            (void)broken_init; 
        }
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
    
    // Приоритет задачи приема увеличен до 2, чтобы данные XModem не терялись
    xTaskCreate(vReceiveTask, "Receive", 1024, NULL, 2, NULL);
    
    // Исправлено: передаем функцию vPawnTask и сохраняем её хэндл xPawnTaskHandle
    xTaskCreate(vPawnTask, "PawnVM", 2048, NULL, 1, &xPawnTaskHandle);

    vTaskStartScheduler();

    while (1);
    
    return 0;
}
