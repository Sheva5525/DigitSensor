#include "stm32f4xx.h"
#include "FreeRTOS.h"
#include "task.h"
#include "W25Q16JVSNIQ_driver.h"
#include "uart1.h"
#include "DataBase.h"  // Подключаем нашу БД
#include <stdio.h>     // Для sprintf (чтобы форматировать вывод в UART)

void hardware_init(void);
void SystemClock_Config_100MHz(void);

// Вспомогательная функция для красивого вывода всей БД в терминал PuTTY
void DB_DumpToUART(void) {
    char buf[128];
    UART1_SendBuf((uint8_t*)"\r\n--- CURRENT RAM DATABASE DUMP ---\r\n", 37);
    UART1_SendBuf((uint8_t*)"Slot | Key | Value    | Type | Read | Flash\r\n", 45);
    UART1_SendBuf((uint8_t*)"--------------------------------------------\r\n", 46);

    // Вручную пройдемся по массиву для отладки (в реальном приложении лучше делать через API)
    // Предположим, что массив _db_storage доступен или мы временно тестируем здесь.
    // Чтобы не нарушать инкапсуляцию, мы просто по очереди вызовем DB_Select для ключей 0..5
    for (uint8_t k = 0; k <= 5; k++) {
        DB_Value_t val;
        if (DB_Select(k, &val)) {
            int len = sprintf(buf, "     |  %d  | %-8d |  %d   |  OK  |  %s\r\n", 
                              k, val.raw_data, val.type, val.save_to_flash ? "YES" : "NO");
            UART1_SendBuf((uint8_t*)buf, len);
        } else {
            // Если не читается, проверим, может ключ просто заблокирован для чтения (is_readable == false)
            // Для этого теста выведем, что слот пуст или скрыт
            int len = sprintf(buf, "     |  %d  | [Empty or Read-Protected]\r\n", k);
            UART1_SendBuf((uint8_t*)buf, len);
        }
    }
    UART1_SendBuf((uint8_t*)"--------------------------------------------\r\n\r\n", 48);
}

void vLedFlashTask(void *pvParameters) {
    (void)pvParameters;
    
    // 1. Сначала инициализируем пустую структуру БД в ОЗУ и мьютекс
    DB_Init();

    // 2. Считываем данные из чипа W25Q16 и восстанавливаем таблицу
    DB_LoadFromFlash();

    // Теперь база готова. Все сохраненные до ресета ключи уже находятся в ОЗУ!
    
    while (1) {
        GPIOC->ODR ^= (1UL << 13);
        vTaskDelay(pdMS_TO_TICKS(500)); 
    }
}


int main(void) {
    hardware_init(); // Тут настраивается SPI, UART и GPIO
    
    // Создаем задачу тестирования БД
    xTaskCreate(vLedFlashTask, "DB_Test_Task", 256, NULL, 1, NULL);
    
    vTaskStartScheduler();
    while (1);
    return 0;
}

// Конфигурация тактовой частоты на 100 МГц от HSE (25 МГц)
void SystemClock_Config_100MHz(void) {
    RCC->CR |= RCC_CR_HSEON;
    while (!(RCC->CR & RCC_CR_HSERDY));

    FLASH->ACR = FLASH_ACR_ICEN | FLASH_ACR_DCEN | FLASH_ACR_LATENCY_3WS;

    RCC->PLLCFGR = (25UL << RCC_PLLCFGR_PLLM_Pos) | 
                   (400UL << RCC_PLLCFGR_PLLN_Pos) | 
                   (1UL << RCC_PLLCFGR_PLLP_Pos) |   
                   RCC_PLLCFGR_PLLSRC_HSE;

    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY));

    RCC->CFGR |= RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV2 | RCC_CFGR_PPRE2_DIV1;

    RCC->CFGR |= RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL);
}

void hardware_init(void) {
    SystemClock_Config_100MHz();

    RCC->AHB1ENR |= (RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN);
    RCC->APB2ENR |= (RCC_APB2ENR_SPI1EN | RCC_APB2ENR_USART1EN);

    // Светодиод Black Pill (PC13)
    GPIOC->MODER &= ~(3UL << (13 * 2));
    GPIOC->MODER |=  (1UL << (13 * 2));
    GPIOC->BSRR   =  (1UL << 13);

    // Настройка CS флешки (PA4)
    GPIOA->MODER &= ~(3UL << (4 * 2));
    GPIOA->MODER |=  (1UL << (4 * 2));
    W25Q16_CS_HIGH();

    // Настройка линий SPI1 (PA5, PA6, PA7)
    GPIOA->MODER &= ~((3UL << (5 * 2)) | (3UL << (6 * 2)) | (3UL << (7 * 2)));
    GPIOA->MODER |=  ((2UL << (5 * 2)) | (2UL << (6 * 2)) | (2UL << (7 * 2)));
    GPIOA->OSPEEDR |= ((2UL << (5 * 2)) | (2UL << (6 * 2)) | (2UL << (7 * 2)));
    
    // Внутренняя подтяжка Pull-up для стабильной работы MISO (PA6)
    GPIOA->PUPDR &= ~((3UL << (5 * 2)) | (3UL << (6 * 2)) | (3UL << (7 * 2)));
    GPIOA->PUPDR |=  (1UL << (6 * 2)); 
    
    // Исправлено: Работаем с массивом AFR[0] для пинов < 8
    GPIOA->AFR[0] &= ~((0xFUL << (5 * 4)) | (0xFUL << (6 * 4)) | (0xFUL << (7 * 4)));
    GPIOA->AFR[0] |=  ((5UL << (5 * 4)) | (5UL << (6 * 4)) | (5UL << (7 * 4)));

    // Настройка линий UART1 (PA9, PA10)
    GPIOA->MODER &= ~((3UL << (9 * 2)) | (3UL << (10 * 2)));
    GPIOA->MODER |=  ((2UL << (9 * 2)) | (2UL << (10 * 2))); 
    GPIOA->OSPEEDR |= ((3UL << (9 * 2)) | (3UL << (10 * 2))); 
    
    // Внутренняя подтяжка Pull-up для стабильности линии RX (PA10)
    GPIOA->PUPDR &= ~((3UL << (9 * 2)) | (3UL << (10 * 2)));  
    GPIOA->PUPDR |=  (1UL << (10 * 2));

    // Исправлено: Работаем с массивом AFR[1] для пинов >= 8
    GPIOA->AFR[1] &= ~((0xFUL << ((9 - 8) * 4)) | (0xFUL << ((10 - 8) * 4)));
    GPIOA->AFR[1] |=  ((7UL << ((9 - 8) * 4)) | (7UL << ((10 - 8) * 4))); 

    // Аппаратная инициализация модуля SPI1 (Master mode)
    SPI1->CR1 = 0; 
    SPI1->CR1 |= (SPI_CR1_MSTR | SPI_CR1_SSI | SPI_CR1_SSM); 
    SPI1->CR1 |= (4UL << SPI_CR1_BR_Pos); // Делитель /32 для стабильности
    SPI1->CR1 |= SPI_CR1_SPE;             

    // Аппаратный сброс CS в исходное высокое состояние и короткая пауза
    W25Q16_CS_HIGH();
    for(volatile int i = 0; i < 50000; i++);

    // Настройка UART1 на 115200 при частоте шины 100 МГц
    USART1->CR1 = 0;
    uint32_t baudrate = 115200;
    uint32_t pclk2 = 100000000; 

    uint32_t div_mantissa = pclk2 / (16 * baudrate);
    uint32_t div_fraction = ((pclk2 % (16 * baudrate)) * 16 + (8 * baudrate)) / (16 * baudrate);
    USART1->BRR = (div_mantissa << 4) | (div_fraction & 0x0F);

    USART1->CR1 |= (USART_CR1_TE | USART_CR1_RE | USART_CR1_UE);
}
