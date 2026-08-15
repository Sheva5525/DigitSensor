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

void Encoder_Init(void) 
{
    // 1. Включаем тактирование порта GPIOB, таймера TIM4 и системного конфигуратора SYSCFG
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
    RCC->APB1ENR |= RCC_APB1ENR_TIM4EN;
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;

    // 2. Настройка PB6 (S1) и PB7 (S2) под альтернативную функцию TIM4
    GPIOB->MODER &= ~((3UL << (6 * 2)) | (3UL << (7 * 2)));
    GPIOB->MODER |=  ((2UL << (6 * 2)) | (2UL << (7 * 2))); // Режим AF

    // Настройка альтернативной функции AF2 (TIM4) для PB6 и PB7
    GPIOB->AFR[0] &= ~((0xFUL << (6 * 4)) | (0xFUL << (7 * 4)));
    GPIOB->AFR[0] |=  ((2UL << (6 * 4)) | (2UL << (7 * 4))); // AF2

    // Включаем подтяжку (Pull-up) к 3.3В для стабильной работы открытых коллекторов энкодера
    GPIOB->PUPDR &= ~((3UL << (6 * 2)) | (3UL << (7 * 2)));
    GPIOB->PUPDR |=  ((1UL << (6 * 2)) | (1UL << (7 * 2))); // Pull-up

    // 3. Настройка Кнопки KEY на пин PB1 (Вход + Pull-up)
    GPIOB->MODER &= ~(3UL << (1 * 2)); // Input mode
    GPIOB->PUPDR &= ~(3UL << (1 * 2));
    GPIOB->PUPDR |=  (1UL << (1 * 2)); // Pull-up

    // 4. Настройка прерывания EXTI1 для порта GPIOB (Пин PB1)
    SYSCFG->EXTICR[0] &= ~SYSCFG_EXTICR1_EXTI1;
    SYSCFG->EXTICR[0] |=  SYSCFG_EXTICR1_EXTI1_PB; // Назначаем линию EXTI1 на порт B
    
    EXTI->IMR  |= EXTI_IMR_IM1;   // Разрешаем прерывание EXTI1
    EXTI->FTSR |= EXTI_FTSR_TR1;  // Триггер по спаду (Falling edge - нажатие кнопки)

    // Приоритет прерывания 5 (такой же, как у вашего UART1, безопасный для FreeRTOS)
    NVIC_SetPriority(EXTI1_IRQn, 5);
    NVIC_EnableIRQ(EXTI1_IRQn);

    // 5. Конфигурация TIM4 в режим Encoder 1 (Счет только по каналу TI1)
    TIM4->CCMR1 &= ~(TIM_CCMR1_CC1S | TIM_CCMR1_CC2S);
    TIM4->CCMR1 |=  (TIM_CCMR1_CC1S_0 | TIM_CCMR1_CC2S_0); // CC1->TI1, CC2->TI2

    // Оставляем глубокие цифровые фильтры
    TIM4->CCMR1 &= ~((0xFUL << TIM_CCMR1_IC1F_Pos) | (0xFUL << TIM_CCMR1_IC2F_Pos));
    TIM4->CCMR1 |=  (0x0DUL << TIM_CCMR1_IC1F_Pos) | (0x0DUL << TIM_CCMR1_IC2F_Pos);

    TIM4->CR1 &= ~TIM_CR1_CKD;
    TIM4->CR1 |= TIM_CR1_CKD_1; // Делитель DTS = 4

    // --- ВОТ ЭТОТ БЛОК МЕНЯЕМ ---
    TIM4->SMCR &= ~TIM_SMCR_SMS; 
    TIM4->SMCR |= TIM_SMCR_SMS_0; // Записываем 0x01: Encoder mode 1 (счет по TI1, TI2 задает направление)
    // ----------------------------

    TIM4->ARR   = 0xFFFF; 
    TIM4->CNT   = 0;
    TIM4->CR1  |= TIM_CR1_CEN; // Включаем таймер
}

extern TaskHandle_t xEncoderButtonTaskHandle;

void EXTI1_IRQHandler(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    // Проверяем, что прерывание случилось именно по линии EXTI1
    if (EXTI->PR & EXTI_PR_PR1) {
        EXTI->PR = EXTI_PR_PR1; // Сбрасываем флаг прерывания записью единицы

        if (xEncoderButtonTaskHandle != NULL) {
            // Разбудить задачу обработки кнопки
            vTaskNotifyGiveFromISR(xEncoderButtonTaskHandle, &xHigherPriorityTaskWoken);
        }
    }

    // Переключаем контекст, если разбуженная задача имеет более высокий приоритет
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
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
    Encoder_Init();
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
