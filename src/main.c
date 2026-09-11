#include "StoreFiles.h"
#include "iPawn.h"
#include "MainLogic.h"
#include "UI.h"
#include "EncoderControl.h"
#include "ValveDetect.h"
#include "DataBase.h"
#include "hardware.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "timers.h"
#include "stm32f4xx.h"
#include <stdio.h>
#include <string.h>

extern QueueHandle_t xUartQueue;
extern TaskHandle_t xEncoderButtonTaskHandle;
extern TaskHandle_t xNetworkDetectorTaskHandle;
extern bool g_ymodem_mode;

void vDbSyncTimerCallback(TimerHandle_t xTimer)
{
    DB_Sync();
}

void vReceiveTask(void *pvParameters)
{
    vReceive();
}

void vEncButtonTask(void *pvParameters)
{
    vEncoderButton();
}

void vEncoderPollTask(void *pvParameters)
{
    vEncoderPoll();
}

void vResistorControlTask(void *pvParameters)
{
    ResistorControl();
}

void vNetworkDetectorTask(void *pvParameters)
{
    vValveDetect();
}

int main(void)
{
    xUartQueue = xQueueCreate(512, sizeof(uint8_t));

    hardware_init();
    DB_Init();
    UI_Init();
    
    DB_Insert(OUT_1_CH_0, (DB_Value_t){ .is_readable = true, .save_to_flash = true, .raw_data = 0,  .type = 0x0, .min = 0,   .max = 255, .step = 1, .is_enabled = false });
    DB_Insert(OUT_1_CH_1, (DB_Value_t){ .is_readable = true, .save_to_flash = true, .raw_data = 0,  .type = 0x0, .min = 0,   .max = 255, .step = 1, .is_enabled = false });
    DB_Insert(OUT_1_OHM, (DB_Value_t){ .is_readable = true, .save_to_flash = true, .raw_data = 50, .type = 0x0, .min = 50,  .max = 500, .step = 1, .is_enabled = false });
    DB_Insert(OUT_2_CH_0, (DB_Value_t){ .is_readable = true, .save_to_flash = true, .raw_data = 0,  .type = 0x0, .min = 0,   .max = 255, .step = 1, .is_enabled = false });
    DB_Insert(OUT_2_CH_1, (DB_Value_t){ .is_readable = true, .save_to_flash = true, .raw_data = 0,  .type = 0x0, .min = 0,   .max = 255, .step = 1, .is_enabled = false });
    DB_Insert(OUT_2_OHM, (DB_Value_t){ .is_readable = true, .save_to_flash = true, .raw_data = 50, .type = 0x0, .min = 50,  .max = 500, .step = 1, .is_enabled = false });
    
    DB_Insert(VALVE_PERCENT,   (DB_Value_t){ .is_readable = true, .save_to_flash = true, .raw_data = 0,  .type = 0x0, .min = 0, .max = 1000, .step = 1, .is_enabled = false });
    DB_Insert(VALVE_SPEED,     (DB_Value_t){ .is_readable = true, .save_to_flash = true, .raw_data = 200,  .type = 0x0, .min = 1, .max = 1000, .step = 1, .is_enabled = true });
    DB_Insert(VALVE_WALK,      (DB_Value_t){ .is_readable = true, .save_to_flash = true, .raw_data = 50,  .type = 0x0, .min = 1, .max = 1000, .step = 1, .is_enabled = true });
    
    DB_Insert(MAIN_SWITCH,     (DB_Value_t){ .is_readable = true, .save_to_flash = true, .raw_data = 1,   .type = 0x0, .min = 0, .max = 2,    .step = 1, .is_enabled = true });

    DB_Insert(OUT_1_1KOHM,     (DB_Value_t){ .is_readable = true, .save_to_flash = true, .raw_data = 989,   .type = 0x0, .min = 9900, .max = 1100,    .step = 1, .is_enabled = false });
    DB_Insert(OUT_2_1KOHM,     (DB_Value_t){ .is_readable = true, .save_to_flash = true, .raw_data = 986,   .type = 0x0, .min = 9900, .max = 1100,    .step = 1, .is_enabled = false }); // yellow

    DB_Insert(VALVE_TYPE,      (DB_Value_t){ .is_readable = true, .save_to_flash = true, .raw_data = 1,   .type = 0x0, .min = 0, .max = 1,    .step = 1, .is_enabled = true });
    DB_Insert(VALVE_OPEN_TIME, (DB_Value_t){ .is_readable = true, .save_to_flash = true, .raw_data = 100, .type = 0x0, .min = 1, .max = 1000, .step = 1, .is_enabled = true });

    DB_Insert(CFG_TAU_P,       (DB_Value_t){ .is_readable = true, .save_to_flash = true, .raw_data = 60,  .type = 0x0, .min = 1, .max = 500,  .step = 1, .is_enabled = true });  // 20 сек
    DB_Insert(CFG_T_RET_DELAY, (DB_Value_t){ .is_readable = true, .save_to_flash = true, .raw_data = 10,  .type = 0x0, .min = 1, .max = 300,  .step = 1, .is_enabled = true });  // 10 сек
    DB_Insert(CFG_T_PERT_PER,  (DB_Value_t){ .is_readable = true, .save_to_flash = true, .raw_data = 60,  .type = 0x0, .min = 1, .max = 3600, .step = 1, .is_enabled = true });  // 60 сек

    // 1:10 fixed point
    DB_Insert(CFG_K_P,         (DB_Value_t){ .is_readable = true, .save_to_flash = true, .raw_data = 1000,  .type = 0x0, .min = 0, .max = 4000, .step = 1, .is_enabled = true });  // 30 -> 3.0
    DB_Insert(CFG_Y0,          (DB_Value_t){ .is_readable = true, .save_to_flash = true, .raw_data = 250, .type = 0x0, .min = 0, .max = 1000, .step = 1, .is_enabled = true });  // 250 -> 25.0°C (было 0, исправили на комнатную)
    DB_Insert(CFG_THETA_P,     (DB_Value_t){ .is_readable = true, .save_to_flash = true, .raw_data = 200,  .type = 0x0, .min = 0, .max = 200,  .step = 1, .is_enabled = true });  // 50 -> 5.0 сек
    DB_Insert(CFG_COEF_RET,    (DB_Value_t){ .is_readable = true, .save_to_flash = true, .raw_data = 7,   .type = 0x0, .min = 5, .max = 15,   .step = 1, .is_enabled = true });  // 8 -> 0.8 (диапазон 0.5 .. 1.5)
    DB_Insert(CFG_T_PERT_AMP,  (DB_Value_t){ .is_readable = true, .save_to_flash = true, .raw_data = 25,  .type = 0x0, .min = 0, .max = 100,  .step = 1, .is_enabled = true });  // 25 -> 2.5°C (Ом)
    DB_Insert(OUT_1_TEMP,      (DB_Value_t){ .is_readable = true, .save_to_flash = true, .raw_data = 0,  .type = 0x0, .min = 0, .max = 5000,  .step = 1, .is_enabled = false });
    DB_Insert(OUT_2_TEMP,      (DB_Value_t){ .is_readable = true, .save_to_flash = true, .raw_data = 0,  .type = 0x0, .min = 0, .max = 5000,  .step = 1, .is_enabled = false });
    


    TimerHandle_t xDbTimer = xTimerCreate("DbSyncTimer", 
                                          pdMS_TO_TICKS(5000), 
                                          pdTRUE, 
                                          (void*)0, 
                                          vDbSyncTimerCallback);
    
    if (xDbTimer != NULL)
    {
        xTimerStart(xDbTimer, 0);
    }
    
    if (!(GPIOB->IDR & GPIO_IDR_IDR_1))
    {
        g_ymodem_mode = true;
    }
    
    xTaskCreate(vResistorControlTask, "MainLogic", 256, NULL, 3, NULL);
    xTaskCreate(vReceiveTask, "Receive", 2024, NULL, 5, NULL);
    xTaskCreate(vEncButtonTask, "EncBtn", 128, NULL, 1, &xEncoderButtonTaskHandle);
    xTaskCreate(vEncoderPollTask, "EncPoll", 128, NULL, 1, NULL);
    xTaskCreate(vGuiTask, "GuiTask", 2048, NULL, 2, NULL);
    xTaskCreate(vNetworkDetectorTask, "ValveDetect", 256,  NULL, 1, &xNetworkDetectorTaskHandle);

    vTaskStartScheduler();

    while (1);

    return 0;
}
