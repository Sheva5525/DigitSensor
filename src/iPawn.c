#include "iPawn.h"
#include "Sensors.h"
#include "DigitResistor.h"
#include "AD8402_driver.h"
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

    int state = (int)params[1];
    
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

    uint32_t ms = (uint32_t)params[1];
    
    if (ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(ms));
    }

    return 0;
}

DigitalRes hard = { .ratedRes = 1170, .resolution = 255, .current_resolution = 0 };
DigitalRes soft = { .ratedRes = 1170, .resolution = 255, .current_resolution = 0 };

static cell AMXAPI native_SetOhm(AMX *amx, const cell *params)
{
    (void)amx;

    // Статические переменные для хранения последних применённых значений
    static int32_t  last_target_ohm = -1;
    static uint32_t last_step1 = 0xFFFFFFFF;
    static uint32_t last_step2 = 0xFFFFFFFF;

    uint32_t target_ohm = (uint32_t)params[1];

    // Если сопротивление не изменилось, ничего не делаем
    if ((int32_t)target_ohm == last_target_ohm)
        return 0;

    // Цифровые резисторы (параметры фиксированы)
    DigitalRes hard = { .ratedRes = 1170, .resolution = 255, .current_resolution = 0 };
    DigitalRes soft = { .ratedRes = 1170, .resolution = 255, .current_resolution = 0 };

    uint32_t best_step1 = 0;
    uint32_t best_step2 = 0;

    // Вычисляем оптимальные шаги
    FindOptimalSteps(&hard, &soft, target_ohm, &best_step1, &best_step2);

    // Применяем шаги к AD8402
    AD8402_Write(0, best_step1);
    AD8402_Write(1, best_step2);

    // Обновляем кэш последних применённых значений
    last_target_ohm = target_ohm;
    last_step1 = best_step1;
    last_step2 = best_step2;

    // Обновляем базу данных
    DB_Value_t val_target, val0, val1;
    bool has_target = DB_Select(2, &val_target);
    bool has0 = DB_Select(0, &val0);
    bool has1 = DB_Select(1, &val1);

    // Если записи не было, создаём с параметрами по умолчанию
    if (!has_target) {
        val_target = (DB_Value_t){
            .is_readable = true,
            .save_to_flash = true,
            .type = 0x0,
            .min = 50,
            .max = 500,
            .step = 1,
            .is_enabled = false
        };
    }
    val_target.raw_data = target_ohm;
    DB_Insert(2, val_target);

    if (!has0) {
        val0 = (DB_Value_t){
            .is_readable = true,
            .save_to_flash = true,
            .type = 0x0,
            .min = 0,
            .max = 255,
            .step = 1,
            .is_enabled = false
        };
    }
    val0.raw_data = best_step1;
    DB_Insert(0, val0);

    if (!has1) {
        val1 = (DB_Value_t){
            .is_readable = true,
            .save_to_flash = true,
            .type = 0x0,
            .min = 0,
            .max = 255,
            .step = 1,
            .is_enabled = false
        };
    }
    val1.raw_data = best_step2;
    DB_Insert(1, val1);

    return 0;
}

static const AMX_NATIVE_INFO stm32_natives[] =
{
    { "SetLedState", native_SetLedState },
    { "Delay", native_Delay },
    { "SetOhm", native_SetOhm },
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
