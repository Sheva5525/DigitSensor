#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#define configUSE_PREEMPTION                    1
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configCPU_CLOCK_HZ                      (16000000UL) // По умолчанию HSI = 16 МГц
#define configTICK_RATE_HZ                      ((TickType_t)1000) // Квант времени 1 мс
#define configMAX_PRIORITIES                    (5)
#define configMINIMAL_STACK_SIZE                ((unsigned short)130)
#define configTOTAL_HEAP_SIZE                   ((size_t)(10 * 1024)) // 10 КБ под кучу
#define configMAX_TASK_NAME_LEN                 (16)
#define configUSE_16_BIT_TICKS                  0

#define INCLUDE_vTaskDelay                      1

/* ---- НАСТРОЙКА ПРИОРТЕТОВ ПРЕРЫВАНИЙ ДЛЯ CORTEX-M4 ---- */
/* В STM32 под приоритеты выделено 4 бита конфигурации */
#define configPRIO_BITS                         4

/* Минимальный приоритет прерываний (для Системного Таймера FreeRTOS) */
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY   15

/* Максимальный приоритет, из которого можно вызывать функции FreeRTOS API */
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5

/* Сдвиг значений приоритетов для аппаратных регистров Cortex-M */
#define configKERNEL_INTERRUPT_PRIORITY 		( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )
#define configMAX_SYSCALL_INTERRUPT_PRIORITY 	( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )

/* Переопределение системных прерываний под FreeRTOS */
#define vPortSVCHandler                         SVC_Handler
#define xPortPendSVHandler                      PendSV_Handler
#define xPortSysTickHandler                     SysTick_Handler

#endif /* FREERTOS_CONFIG_H */
