#include "MainLogic.h"
#include "DigitResistor.h"
#include "DataBase.h"
#include "AD8402_driver.h"
#include "iPawn.h"
#include "FreeRTOS.h"

extern bool is_pawn_suspended;
extern TaskHandle_t xPawnTaskHandle;

void ResistorControl()
{
    uint32_t step1 = 0xFFFFFFFF, step2 = 0xFFFFFFFF; 
    DB_Value_t value1, value2, target_ohm, main_switch;
    
    DigitalRes hard = { .ratedRes = 1170, .resolution = 255, .current_resolution = 0 };
    DigitalRes soft = { .ratedRes = 1170, .resolution = 255, .current_resolution = 0 };
    
    uint32_t best_step1 = 0;
    uint32_t best_step2 = 0;

    int32_t last_applied_target = -1;
    int32_t last_applied_value1 = -1;
    int32_t last_applied_value2 = -1;
    
    POT1_WRITE(0, 255); // Записать максимум в первый канал потенциометра №1
    POT1_WRITE(1, 255); // Записать максимум в первый канал потенциометра №1
    POT2_WRITE(0, 255); // Записать половину во второй канал потенциометра №2
    POT2_WRITE(1, 255); // Записать половину во второй канал потенциометра №2

    for (;;)
    {
        if (!DB_Select(3, &main_switch))
        {
            main_switch.raw_data = 0;
        }

        bool edit_enabled = (main_switch.raw_data == 0);

        for (uint8_t key = 0; key <= 2; key++)
        {
            DB_Value_t tmp;
            if (DB_Select(key, &tmp))
            {
                if (tmp.is_enabled != edit_enabled)
                {
                    tmp.is_enabled = edit_enabled;
                    DB_Insert(key, tmp);
                }
            }
        }

        if (main_switch.raw_data == 0)
        {
            // Блокировка поткоа pawn
            if (!is_pawn_suspended && xPawnTaskHandle != NULL)
            {
                vTaskSuspend(xPawnTaskHandle);
                is_pawn_suspended = true;
            }

            if (DB_Select(2, &target_ohm))
            {
                if (target_ohm.raw_data != last_applied_target)
                {
                    FindOptimalSteps(&hard, &soft, target_ohm.raw_data, &best_step1, &best_step2);

//                    AD8402_Write(0, best_step1);
//                    AD8402_Write(1, best_step2);
                    step1 = best_step1;
                    step2 = best_step2;

                    last_applied_target = target_ohm.raw_data;
                    last_applied_value1 = best_step1;
                    last_applied_value2 = best_step2;

                    DB_Value_t new_val1;
                    if (DB_Select(0, &new_val1))
                    {
                        new_val1.raw_data = best_step1;
                    }
                    else
                    {
                        new_val1 = (DB_Value_t)
                        {
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
                    if (DB_Select(1, &new_val2))
                    {
                        new_val2.raw_data = best_step2;
                    }
                    else
                    {
                        new_val2 = (DB_Value_t)
                        {
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

            if (DB_Select(0, &value1))
            {
                if (value1.raw_data != last_applied_value1)
                {
                    //AD8402_Write(0, value1.raw_data);
                    step1 = value1.raw_data;
                    last_applied_value1 = value1.raw_data;

                    last_applied_target = -1;
                }
            }

            if (DB_Select(1, &value2))
            {
                if (value2.raw_data != last_applied_value2)
                {
//                    AD8402_Write(1, value2.raw_data);
                    step2 = value2.raw_data;
                    last_applied_value2 = value2.raw_data;

                    last_applied_target = -1;
                }
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

            last_applied_target = -1;
            last_applied_value1 = -1;
            last_applied_value2 = -1;

            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}
