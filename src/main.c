#include "stm32f4xx.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "DataBase.h"
#include "xmodem.h"   // Здесь должны быть объявлены SimpleReceiveFile, SendCharacter, ACK, NAK
#include <string.h>

// ================== Очередь UART ==================
QueueHandle_t xUartQueue;

// ================== Буфер для принятого файла ==================
#define MAX_FILE_SIZE   16384   // 16 КБ
uint8_t receivedData[MAX_FILE_SIZE];
uint32_t receivedLength = 0;

// ================== Задача мигания светодиодом ==================
void vLedFlashTask(void *pvParameters)
{
    (void)pvParameters;
    while (1) {
        GPIOC->ODR ^= (1UL << 13);   // Инверсия PC13
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// ================== Задача приёма файла ==================
void vReceiveTask(void *pvParameters)
{
    (void)pvParameters;

    // Даём время на инициализацию периферии и файловой системы
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Пытаемся прочитать сохранённый файл
    if (DB_ReadFile(receivedData, MAX_FILE_SIZE, &receivedLength)) {
        // Файл успешно прочитан: мигаем 5 раз быстро
        for (int i = 0; i < 5; i++) {
            GPIOC->ODR ^= (1UL << 13);
            vTaskDelay(pdMS_TO_TICKS(150));
        }
    } else {
        // Файл не найден или ошибка чтения: мигаем 2 раза медленно
        for (int i = 0; i < 2; i++) {
            GPIOC->ODR ^= (1UL << 13);
            vTaskDelay(pdMS_TO_TICKS(800));
        }
    }

}

// ================== Инициализация тактирования 100 МГц ==================
void SystemClock_Config_100MHz(void)
{
    // Включаем внешний кварц (HSE)
    RCC->CR |= RCC_CR_HSEON;
    while (!(RCC->CR & RCC_CR_HSERDY));

    // Настройка Flash Latency для работы на частоте 100 МГц
    FLASH->ACR = FLASH_ACR_ICEN | FLASH_ACR_DCEN | FLASH_ACR_LATENCY_3WS;

    // Конфигурация PLL (предполагается кварц 25 МГц)
    RCC->PLLCFGR = (25UL << RCC_PLLCFGR_PLLM_Pos) |
                   (400UL << RCC_PLLCFGR_PLLN_Pos) |
                   (1UL << RCC_PLLCFGR_PLLP_Pos) |
                   RCC_PLLCFGR_PLLSRC_HSE;

    // Включаем PLL
    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY));

    // ИСПРАВЛЕНО: Устанавливаем безопасные делители для шин периферии
    // AHB = 100 МГц (DIV1), APB1 = 50 МГц (DIV2), APB2 = 50 МГц (DIV2)
    RCC->CFGR |= RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV2 | RCC_CFGR_PPRE2_DIV2;

    // Переключаем системное тактирование на PLL
    RCC->CFGR |= RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL);
}

void hardware_init(void)
{
    SystemClock_Config_100MHz();

    // Включаем тактирование портов GPIOA, GPIOC и модуля USART1
    RCC->AHB1ENR |= (RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOCEN);
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;

    // Настройка светодиода на PC13 (Выход)
    GPIOC->MODER &= ~(3UL << (13 * 2));
    GPIOC->MODER |=  (1UL << (13 * 2));
    GPIOC->BSRR   =  (1UL << 13);   // Выключить LED (высокий уровень для платы Black Pill)

    // Настройка выводов UART1: PA9 (TX), PA10 (RX) в альтернативный режим
    GPIOA->MODER &= ~((3UL << (9 * 2)) | (3UL << (10 * 2)));
    GPIOA->MODER |=  ((2UL << (9 * 2)) | (2UL << (10 * 2)));  // Alternate Function
    GPIOA->OSPEEDR |= ((3UL << (9 * 2)) | (3UL << (10 * 2))); // High speed

    // Включаем Pull-up резистор на линию приема RX (PA10)
    GPIOA->PUPDR &= ~((3UL << (9 * 2)) | (3UL << (10 * 2)));
    GPIOA->PUPDR |=  (1UL << (10 * 2));

    // Привязываем AF7 (USART1) к пинам PA9 и PA10
    GPIOA->AFR[1] &= ~((0xFUL << ((9 - 8) * 4)) | (0xFUL << ((10 - 8) * 4)));
    GPIOA->AFR[1] |=  ((7UL << ((9 - 8) * 4)) | (7UL << ((10 - 8) * 4)));

    // Сброс конфигурации USART1
    USART1->CR1 = 0;
    
    // ИСПРАВЛЕНО: Расчет регистра BRR исходя из реальной частоты APB2 = 50 МГц
    uint32_t baudrate = 115200;
    uint32_t pclk2 = 50000000;   // APB2 строго 50 МГц

    uint32_t div_mantissa = pclk2 / (16 * baudrate);
    uint32_t div_fraction = ((pclk2 % (16 * baudrate)) * 16 + (8 * baudrate)) / (16 * baudrate);
    USART1->BRR = (div_mantissa << 4) | (div_fraction & 0x0F);

    // Разрешаем работу передатчика (TE), приемника (RE) и прерывания по приему (RXNEIE)
    USART1->CR1 |= (USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE | USART_CR1_UE);
    
    // Настройка NVIC для прерываний USART1
    NVIC_SetPriority(USART1_IRQn, 5); // Безопасный приоритет для FreeRTOS (>= configMAX_SYSCALL_INTERRUPT_PRIORITY)   
    NVIC_EnableIRQ(USART1_IRQn);
    
    // ===== Инициализация SPI1 для W25Q16 =====
    // Тактирование GPIOA уже включено выше, но на всякий случай:
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    // Включаем тактирование SPI1
    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;

    // --- Настройка пина CS (PA4) как выход ---
    GPIOA->MODER   &= ~(3UL << (4 * 2));
    GPIOA->MODER   |=  (1UL << (4 * 2));      // Output mode
    GPIOA->OTYPER  &= ~(1UL << 4);            // Push-pull
    GPIOA->OSPEEDR |=  (3UL << (4 * 2));      // High speed
    GPIOA->PUPDR   &= ~(3UL << (4 * 2));      // No pull
    // Поднять CS (неактивный уровень)
    GPIOA->BSRR = (1UL << 4);

    // --- Настройка пинов SCK (PA5), MISO (PA6), MOSI (PA7) в альтернативную функцию SPI1 (AF5) ---
    // Режим: Alternate Function
    GPIOA->MODER   &= ~((3UL << (5*2)) | (3UL << (6*2)) | (3UL << (7*2)));
    GPIOA->MODER   |=  ((2UL << (5*2)) | (2UL << (6*2)) | (2UL << (7*2)));

    // Скорость – высокая
    GPIOA->OSPEEDR |=  ((3UL << (5*2)) | (3UL << (6*2)) | (3UL << (7*2)));

    // Альтернативная функция AF5 для пинов 5,6,7 (регистр AFR[0], так как пины < 8)
    GPIOA->AFR[0] &= ~((0xFUL << (5*4)) | (0xFUL << (6*4)) | (0xFUL << (7*4)));
    GPIOA->AFR[0] |=  ((5UL << (5*4)) | (5UL << (6*4)) | (5UL << (7*4)));

    // --- Конфигурация SPI1 (мастер, программный NSS) ---
    SPI1->CR1 = 0;                       // сброс
    SPI1->CR1 |= SPI_CR1_MSTR;           // Master mode
    SPI1->CR1 |= SPI_CR1_SSM;            // Software slave management
    SPI1->CR1 |= SPI_CR1_SSI;            // Internal slave select = 1
    SPI1->CR1 |= SPI_CR1_BR_0;           // Baud rate = fPCLK/4 = 12.5 МГц (при APB2=50 МГц)
    SPI1->CR1 |= SPI_CR1_SPE;            // Включить SPI
}

// ================== Обработчик прерывания USART1 ==================
void USART1_IRQHandler(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    uint32_t sr = USART1->SR; // Читаем регистр статуса один раз

    // Проверяем, пришли ли данные в приемный регистр (RXNE)
    if (sr & USART_SR_RXNE) {
        uint8_t byte = (uint8_t)USART1->DR;
        // Передаем байт в очередь FreeRTOS
        xQueueSendFromISR(xUartQueue, &byte, &xHigherPriorityTaskWoken);
    }

    // ИСПРАВЛЕНО: Сброс флагов аппаратных ошибок (Overrun, Noise, Frame Error)
    // Без этого сброса при любой помехе в линии прерывание зависнет в бесконечном вызове
    if (sr & (USART_SR_ORE | USART_SR_NE | USART_SR_FE)) {
        volatile uint32_t dummy = USART1->DR; // Чтение регистра данных очищает флаги ошибок
        (void)dummy;
    }

    // Форсируем переключение контекста, если задача приема имеет высокий приоритет
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

// ================== Точка входа ==================
int main(void)
{
    // ИСПРАВЛЕНО: Сначала создаём очередь, чтобы избежать падения в HardFault 
    // при случайном срабатывании прерывания UART до старта планировщика
    xUartQueue = xQueueCreate(512, sizeof(uint8_t));
    if (xUartQueue == NULL) {
        while (1); // Ошибка выделения памяти под очередь
    }

    // Теперь безопасно инициализировать тактирование и периферию
    hardware_init();
    DB_Init();
    // Создаём задачи операционной системы
    xTaskCreate(vLedFlashTask, "LED", 256, NULL, 1, NULL);
    xTaskCreate(vReceiveTask, "Receive", 1024, NULL, 1, NULL);

    // Запуск планировщика FreeRTOS
    vTaskStartScheduler();

    // Сюда программа дойти не должна
    while (1);
    return 0;
}
