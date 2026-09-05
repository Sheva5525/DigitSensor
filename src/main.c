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
extern TaskHandle_t xPawnTaskHandle;
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

void vPawnTask(void *pvParameters)
{
    while (1);
    PawnTask();
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
    
    if (!(GPIOB->IDR & GPIO_IDR_IDR_1))
    {
        g_ymodem_mode = true;
    }
   

    xTaskCreate(vResistorControlTask, "Resistors", 256, NULL, 1, NULL);
    xTaskCreate(vReceiveTask, "Receive", 2024, NULL, 2, NULL);
    xTaskCreate(vPawnTask, "PawnVM", 2048, NULL, 1, &xPawnTaskHandle);
    xTaskCreate(vEncButtonTask, "EncBtn", 128, NULL, 3, &xEncoderButtonTaskHandle);
    xTaskCreate(vEncoderPollTask, "EncPoll", 128, NULL, 2, NULL);
    xTaskCreate(vGuiTask, "GuiTask", 2048, NULL, 2, NULL);
    xTaskCreate(vNetworkDetectorTask, "NetDetect", 256,  NULL, 2, &xNetworkDetectorTaskHandle);

    vTaskStartScheduler();

    while (1);

    return 0;
}
