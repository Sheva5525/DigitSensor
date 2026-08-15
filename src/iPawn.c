#include "iPawn.h"
#include "amx.h"
#include "DataBase.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "stm32f4xx.h"
#include <string.h>

// Выделяем рабочий буфер в RAM под исполняемый скрипт (код + стек/куча скрипта)
#define PAWN_MEMORY_SIZE_BYTES  (1 * 1024)
__attribute__((aligned(4))) uint8_t amx_run_memory[PAWN_MEMORY_SIZE_BYTES];

__attribute__((aligned(4))) uint8_t pawn_data_space[2048];

TaskHandle_t xPawnTaskHandle = NULL;

static cell AMXAPI native_SetLedState(AMX *amx, const cell *params)
{
    (void)amx;

    cell *arg_ptr = (cell *)params[1];
    int state = (int)(*arg_ptr); 
    
    if (state == 1) {
        GPIOC->BSRR = (1UL << (13 + 16)); // Зажечь LED
    } else {
        GPIOC->BSRR = (1UL << 13);        // Погасить LED
    }
    return 0;
}

static cell AMXAPI native_Delay(AMX *amx, const cell *params)
{
    (void)amx;

    cell *arg_ptr = (cell *)params[1];
    uint32_t ms = (uint32_t)(*arg_ptr); 
    
    if (ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(ms));
    }

    return 0;
}

static const AMX_NATIVE_INFO stm32_natives[] =
{
    { "SetLedState", native_SetLedState },
    { "Delay", native_Delay },
    { NULL, NULL }
};

void PawnTask()
{
    AMX amx;
    int init_error = -1;
    int exec_error = -1;
    cell ret_value = 0;

    while (1) 
    {
        // 1. Ожидаем уведомления от задачи приема файлов по XModem
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // Проверяем лимиты буфера
        if (g_receivedFile.length > PAWN_MEMORY_SIZE_BYTES || g_receivedFile.length == 0)
        {
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
