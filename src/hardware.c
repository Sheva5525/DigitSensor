#include "hardware.h"
#include "stm32f4xx.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

// =====================================================================
//  SystemClock_Config
//  Настраивает тактирование: HSE -> PLL -> SYSCLK 100 МГц
//  AHB = 100 МГц, APB1 = 50 МГц, APB2 = 50 МГц
// =====================================================================
void SystemClock_Config(void)
{
    // Включаем внешний кварц HSE
    RCC->CR |= RCC_CR_HSEON;
    while (!(RCC->CR & RCC_CR_HSERDY));

    // Flash Latency = 3 WS (для 100 МГц)
    FLASH->ACR = FLASH_ACR_ICEN | FLASH_ACR_DCEN | FLASH_ACR_LATENCY_3WS;

    // Настройка PLL: источник HSE (25 МГц), M=25, N=400, P=1 -> SYSCLK = 100 МГц
    RCC->PLLCFGR = (25UL << RCC_PLLCFGR_PLLM_Pos) |
                   (400UL << RCC_PLLCFGR_PLLN_Pos) |
                   (1UL << RCC_PLLCFGR_PLLP_Pos) |
                   RCC_PLLCFGR_PLLSRC_HSE;

    // Включаем PLL и ждём готовности
    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY));

    // Делители шин: AHB = DIV1, APB1 = DIV2 (50 МГц), APB2 = DIV2 (50 МГц)
    RCC->CFGR |= RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV2 | RCC_CFGR_PPRE2_DIV2;

    // Переключаем системный тактовый генератор на PLL
    RCC->CFGR |= RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL);
}

// =====================================================================
//  LED_Init
//  Инициализация светодиода на выводе PC13 (выход, выключен)
// =====================================================================
void LED_Init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN;
    
    GPIOC->MODER &= ~(3UL << (13 * 2));
    GPIOC->MODER |=  (1UL << (13 * 2));

    GPIOC->BSRR = (1UL << 13);
}

// =====================================================================
//  UART1_Init
//  Инициализация USART1: пины PA9 (TX), PA10 (RX), настройка UART, NVIC
//  Baudrate = 115200, прерывание по приёму
// =====================================================================
void UART1_Init(void)
{
    // Включаем тактирование GPIOA и USART1
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;

    // Настройка пинов PA9 (TX), PA10 (RX) как альтернативная функция AF7
    GPIOA->MODER   &= ~((3UL << (9 * 2)) | (3UL << (10 * 2)));
    GPIOA->MODER   |=  ((2UL << (9 * 2)) | (2UL << (10 * 2)));   // AF mode
    GPIOA->OSPEEDR |=  ((3UL << (9 * 2)) | (3UL << (10 * 2)));   // High speed

    // Подтяжка вверх на RX (PA10)
    GPIOA->PUPDR &= ~((3UL << (9 * 2)) | (3UL << (10 * 2)));
    GPIOA->PUPDR |=  (1UL << (10 * 2));

    // Альтернативная функция AF7 для PA9, PA10
    GPIOA->AFR[1] &= ~((0xFUL << ((9 - 8) * 4)) | (0xFUL << ((10 - 8) * 4)));
    GPIOA->AFR[1] |=  ((7UL << ((9 - 8) * 4)) | (7UL << ((10 - 8) * 4)));

    // Сброс USART1
    USART1->CR1 = 0;

    // Расчёт делителя для 115200 бод при APB2 = 50 МГц
    uint32_t baudrate = 115200;
    uint32_t pclk2 = 50000000;
    uint32_t div_mantissa = pclk2 / (16 * baudrate);
    uint32_t div_fraction = ((pclk2 % (16 * baudrate)) * 16 + (8 * baudrate)) / (16 * baudrate);
    USART1->BRR = (div_mantissa << 4) | (div_fraction & 0x0F);

    // Включаем передатчик, приёмник, прерывание по RXNE и сам USART
    USART1->CR1 |= (USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE | USART_CR1_UE);

    // Настройка прерывания USART1 в NVIC
    NVIC_SetPriority(USART1_IRQn, 5);   // Приоритет безопасный для FreeRTOS
    NVIC_EnableIRQ(USART1_IRQn);
}

// =====================================================================
//  SPI1_Init
//  Инициализация SPI1 для работы с Flash W25Q16:
//  PA4  -> CS (программный)
//  PA5  -> SCK
//  PA6  -> MISO
//  PA7  -> MOSI
//  Режим: мастер, программный NSS, частота SCK = APB2/4 = 12.5 МГц
// =====================================================================
void SPI1_Init(void)
{
    // Включаем тактирование GPIOA и SPI1
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;

    // --- CS (PA4) как обычный выход ---
    GPIOA->MODER   &= ~(3UL << (4 * 2));
    GPIOA->MODER   |=  (1UL << (4 * 2));   // Output
    GPIOA->OTYPER  &= ~(1UL << 4);         // Push-pull
    GPIOA->OSPEEDR |=  (3UL << (4 * 2));   // High speed
    GPIOA->PUPDR   &= ~(3UL << (4 * 2));   // Без подтяжек
    GPIOA->BSRR = (1UL << 4);              // CS = 1 (неактивен)

    // --- SCK (PA5), MISO (PA6), MOSI (PA7) как альтернативная функция AF5 ---
    GPIOA->MODER   &= ~((3UL << (5*2)) | (3UL << (6*2)) | (3UL << (7*2)));
    GPIOA->MODER   |=  ((2UL << (5*2)) | (2UL << (6*2)) | (2UL << (7*2))); // AF mode

    GPIOA->OSPEEDR |=  ((3UL << (5*2)) | (3UL << (6*2)) | (3UL << (7*2))); // High speed

    // AF5 для пинов 5,6,7 (регистр AFR[0] отвечает за пины 0..7)
    GPIOA->AFR[0] &= ~((0xFUL << (5*4)) | (0xFUL << (6*4)) | (0xFUL << (7*4)));
    GPIOA->AFR[0] |=  ((5UL << (5*4)) | (5UL << (6*4)) | (5UL << (7*4)));

    // --- Конфигурация SPI1 ---
    SPI1->CR1 = 0;                    // Сброс
    SPI1->CR1 |= SPI_CR1_MSTR;        // Мастер
    SPI1->CR1 |= SPI_CR1_SSM;         // Программное управление NSS
    SPI1->CR1 |= SPI_CR1_SSI;         // Внутренний NSS = 1
    SPI1->CR1 |= SPI_CR1_BR_0;        // Делитель = 4 (APB2 50 МГц -> SCK 12.5 МГц)
    SPI1->CR1 |= SPI_CR1_SPE;         // Включить SPI
}

void Button_Init(void) {
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    // PA0 как вход с подтяжкой вверх
    GPIOA->MODER &= ~(3UL << (0 * 2));   // Input
    GPIOA->PUPDR &= ~(3UL << (0 * 2));
    GPIOA->PUPDR |=  (1UL << (0 * 2));   // Pull-up
}

// =====================================================================
//  System_Init
//  Вызывает все инициализации периферии в правильном порядке:
//  сначала тактирование, затем светодиод, UART, SPI
// =====================================================================
void hardware_init(void)
{
    SystemClock_Config();
    LED_Init();
    UART1_Init();
    Button_Init();
    SPI1_Init();
}

extern QueueHandle_t xUartQueue;
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
