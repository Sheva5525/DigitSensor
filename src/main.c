#include "stm32f4xx.h"
#include "FreeRTOS.h"
#include "task.h"

// Функция задачи для мигания светодиодом
void vLedBlinkTask(void *pvParameters) {
    // Инициализация GPIO (код из прошлого шага)
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN;
    GPIOC->MODER &= ~(3UL << (13 * 2));
    GPIOC->MODER |=  (1UL << (13 * 2));

    while (1) {
        GPIOC->ODR ^= (1UL << 13); // Инвертируем светодиод
        
        // Задержка на 500 системных тиков (при configTICK_RATE_HZ = 1000 это ровно 500 мс)
        vTaskDelay(500 / portTICK_PERIOD_MS); 
    }
}

int main(void) {
    // Создаем задачу мигания светодиода
    xTaskCreate(
        vLedBlinkTask,       // Функция, выполняющая задачу
        "LedBlink",          // Текстовое имя задачи (для отладки)
        130,                 // Размер стека для задачи в словах (130 * 4 байта)
        NULL,                // Параметры, передаваемые в задачу
        1,                   // Приоритет задачи (1 - низкий, выше 0)
        NULL                 // Хэндл задачи (не нужен)
    );

    // Запускаем планировщик FreeRTOS
    // После этой строчки управление переходит к операционной системе
    vTaskStartScheduler();

    // Сюда микроконтроллер никогда не должен дойти
    while (1);
    return 0;
}
