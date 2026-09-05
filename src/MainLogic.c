#include "MainLogic.h"
#include "DigitResistor.h"
#include "DataBase.h"
#include "AD8402_driver.h"
#include "FreeRTOS.h"

void ResistorControl()
{
    int32_t last_applied_target1 = -1;
    int32_t last_applied_ch1_0 = -1;
    int32_t last_applied_ch1_1 = -1;
    int32_t last_applied_target2 = -1;
    int32_t last_applied_ch2_0 = -1;
    int32_t last_applied_ch2_1 = -1;

    POT1_WRITE(0, 255);
    POT1_WRITE(1, 255);
    POT2_WRITE(0, 255);
    POT2_WRITE(1, 255);

    DigitalRes hard = { .ratedRes = 1170, .resolution = 255, .current_resolution = 0 };
    DigitalRes soft = { .ratedRes = 1170, .resolution = 255, .current_resolution = 0 };

    for (;;)
    {
        // 1. Читаем главный выключатель
        DB_Value_t main_switch;
        if (!DB_Select(MAIN_SWITCH, &main_switch))
            main_switch.raw_data = 0;

        bool enable = (main_switch.raw_data == 0);   // 0 – включено, разрешено редактирование

        // 2. Синхронизация is_enabled ТОЛЬКО для целевых сопротивлений (OHM)
        uint8_t ohm_keys[] = { OUT_1_OHM, OUT_2_OHM };
        for (uint8_t i = 0; i < sizeof(ohm_keys)/sizeof(ohm_keys[0]); i++)
        {
            DB_Value_t val;
            if (!DB_Select(ohm_keys[i], &val))
            {
                val = (DB_Value_t){
                    .is_readable = true,
                    .save_to_flash = true,
                    .raw_data = 50,
                    .type = 0x0,
                    .min = 50,
                    .max = 500,
                    .step = 1,
                    .is_enabled = enable
                };
                DB_Insert(ohm_keys[i], val);
            }
            else
            {
                if (val.is_enabled != enable)
                {
                    val.is_enabled = enable;
                    DB_Insert(ohm_keys[i], val);
                }
            }
        }

        // 3. Для каналов – всегда is_enabled = false (неактивны)
        uint8_t channel_keys[] = { OUT_1_CH_0, OUT_1_CH_1, OUT_2_CH_0, OUT_2_CH_1 };
        for (uint8_t i = 0; i < sizeof(channel_keys)/sizeof(channel_keys[0]); i++)
        {
            DB_Value_t val;
            if (!DB_Select(channel_keys[i], &val))
            {
                val = (DB_Value_t){
                    .is_readable = true,
                    .save_to_flash = true,
                    .raw_data = 0,
                    .type = 0x0,
                    .min = 0,
                    .max = 255,
                    .step = 1,
                    .is_enabled = false   // всегда false
                };
                DB_Insert(channel_keys[i], val);
            }
            else
            {
                // Принудительно держим false
                if (val.is_enabled != false)
                {
                    val.is_enabled = false;
                    DB_Insert(channel_keys[i], val);
                }
            }
        }

        // 4. Если выключено (MAIN_SWITCH == 1) – пропускаем управление
        if (!enable)
        {
            last_applied_target1 = last_applied_target2 = -1;
            last_applied_ch1_0 = last_applied_ch1_1 = -1;
            last_applied_ch2_0 = last_applied_ch2_1 = -1;
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        // ====== Включено (MAIN_SWITCH == 0): управление активно ======

        // ---- OUT_1 ----
        DB_Value_t target1, ch1_0, ch1_1;
        DB_Select(OUT_1_OHM, &target1) ? : (target1.raw_data = 50);
        DB_Select(OUT_1_CH_0, &ch1_0) ? : (ch1_0.raw_data = 0);
        DB_Select(OUT_1_CH_1, &ch1_1) ? : (ch1_1.raw_data = 0);

        if (target1.raw_data != last_applied_target1)
        {
            uint32_t step1, step2;
            FindOptimalSteps(&hard, &soft, target1.raw_data, &step1, &step2);
            DB_Value_t new1 = ch1_0; new1.raw_data = step1; DB_Insert(OUT_1_CH_0, new1);
            DB_Value_t new2 = ch1_1; new2.raw_data = step2; DB_Insert(OUT_1_CH_1, new2);
            POT1_WRITE(0, step1);
            POT1_WRITE(1, step2);
            last_applied_target1 = target1.raw_data;
            last_applied_ch1_0 = step1;
            last_applied_ch1_1 = step2;
        }
        if (ch1_0.raw_data != last_applied_ch1_0)
        {
            POT1_WRITE(0, ch1_0.raw_data);
            last_applied_ch1_0 = ch1_0.raw_data;
            last_applied_target1 = -1;
        }
        if (ch1_1.raw_data != last_applied_ch1_1)
        {
            POT1_WRITE(1, ch1_1.raw_data);
            last_applied_ch1_1 = ch1_1.raw_data;
            last_applied_target1 = -1;
        }

        // ---- OUT_2 ----
        DB_Value_t target2, ch2_0, ch2_1;
        DB_Select(OUT_2_OHM, &target2) ? : (target2.raw_data = 50);
        DB_Select(OUT_2_CH_0, &ch2_0) ? : (ch2_0.raw_data = 0);
        DB_Select(OUT_2_CH_1, &ch2_1) ? : (ch2_1.raw_data = 0);

        if (target2.raw_data != last_applied_target2)
        {
            uint32_t step1, step2;
            FindOptimalSteps(&hard, &soft, target2.raw_data, &step1, &step2);
            DB_Value_t new1 = ch2_0; new1.raw_data = step1; DB_Insert(OUT_2_CH_0, new1);
            DB_Value_t new2 = ch2_1; new2.raw_data = step2; DB_Insert(OUT_2_CH_1, new2);
            POT2_WRITE(0, step1);
            POT2_WRITE(1, step2);
            last_applied_target2 = target2.raw_data;
            last_applied_ch2_0 = step1;
            last_applied_ch2_1 = step2;
        }
        if (ch2_0.raw_data != last_applied_ch2_0)
        {
            POT2_WRITE(0, ch2_0.raw_data);
            last_applied_ch2_0 = ch2_0.raw_data;
            last_applied_target2 = -1;
        }
        if (ch2_1.raw_data != last_applied_ch2_1)
        {
            POT2_WRITE(1, ch2_1.raw_data);
            last_applied_ch2_1 = ch2_1.raw_data;
            last_applied_target2 = -1;
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}