#include "stm32f4xx.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "xmodem.h"   
#include <string.h>

QueueHandle_t xUartQueue;

#define MAX_FILE_SIZE   16384   
uint8_t receivedData[MAX_FILE_SIZE];
uint32_t receivedLength = 0;

// ПРОТОТИП НАШЕЙ ФУНКЦИИ ИЗ ДРАЙВЕРА (Добавлено)
void save_to_littlefs(const uint8_t *data, uint32_t length);


void vLedFlashTask(void *pvParameters)
{
    (void)pvParameters;
    while (1) {
        GPIOC->ODR ^= (1UL << 13);           
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void vReceiveTask(void *pvParameters)
{
    (void)pvParameters;
    vTaskDelay(pdMS_TO_TICKS(1000));  

    for (int i = 0; i < 4; i++) {
        GPIOC->ODR ^= (1UL << 13);
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    receivedLength = SimpleReceiveFile(receivedData, MAX_FILE_SIZE, 5000);

    if (receivedLength > 0) {
        
        // --- НАША ВСТАВКА: Данные уходят во внешнюю SPI Flash W25Q16 ---
        save_to_littlefs(receivedData, receivedLength);
        // --------------------------------------------------------------

        for (int i = 0; i < 10; i++) {
            GPIOC->ODR ^= (1UL << 13);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    } else {
        for (int i = 0; i < 6; i++) {
            GPIOC->ODR ^= (1UL << 13);
            vTaskDelay(pdMS_TO_TICKS(300));
        }
    }

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void SystemClock_Config_100MHz(void)
{
    RCC->CR |= RCC_CR_HSEON;
    while (!(RCC->CR & RCC_CR_HSERDY));

    FLASH->ACR = FLASH_ACR_ICEN | FLASH_ACR_DCEN | FLASH_ACR_LATENCY_3WS;

    RCC->PLLCFGR = (25UL << RCC_PLLCFGR_PLLM_Pos) |
                   (400UL << RCC_PLLCFGR_PLLN_Pos) |
                   (1UL << RCC_PLLCFGR_PLLP_Pos) |
                   RCC_PLLCFGR_PLLSRC_HSE;

    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY));

    RCC->CFGR |= RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV2 | RCC_CFGR_PPRE2_DIV2;

    RCC->CFGR |= RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL);
}

void hardware_init(void)
{
    SystemClock_Config_100MHz();

    RCC->AHB1ENR |= (RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOCEN);
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;

    GPIOC->MODER &= ~(3UL << (13 * 2));
    GPIOC->MODER |=  (1UL << (13 * 2));
    GPIOC->BSRR   =  (1UL << 13);   
    
    GPIOA->MODER &= ~((3UL << (9 * 2)) | (3UL << (10 * 2)));
    GPIOA->MODER |=  ((2UL << (9 * 2)) | (2UL << (10 * 2)));      
    GPIOA->OSPEEDR |= ((3UL << (9 * 2)) | (3UL << (10 * 2))); 
    
    GPIOA->PUPDR &= ~((3UL << (9 * 2)) | (3UL << (10 * 2)));
    GPIOA->PUPDR |=  (1UL << (10 * 2));

    GPIOA->AFR[1] &= ~((0xFUL << ((9 - 8) * 4)) | (0xFUL << ((10 - 8) * 4)));
    GPIOA->AFR[1] |=  ((7UL << ((9 - 8) * 4)) | (7UL << ((10 - 8) * 4)));

    USART1->CR1 = 0;
    
    uint32_t baudrate = 115200;
    uint32_t pclk2 = 50000000;   
    uint32_t div_mantissa = pclk2 / (16 * baudrate);
    uint32_t div_fraction = ((pclk2 % (16 * baudrate)) * 16 + (8 * baudrate)) / (16 * baudrate);
    USART1->BRR = (div_mantissa << 4) | (div_fraction & 0x0F);

    USART1->CR1 |= (USART_CR1_TE | RCC_CFGR_PPRE1_DIV2 ? USART_CR1_RE | USART_CR1_RXNEIE | USART_CR1_UE : USART_CR1_RE | USART_CR1_RXNEIE | USART_CR1_UE); // Возвращено строго к вашему оригиналу
    USART1->CR1 = (USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE | USART_CR1_UE);

    NVIC_SetPriority(USART1_IRQn, 5);     
    NVIC_EnableIRQ(USART1_IRQn);
}

void USART1_IRQHandler(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    uint32_t sr = USART1->SR; 
    if (sr & USART_SR_RXNE) {
        uint8_t byte = (uint8_t)USART1->DR;
        xQueueSendFromISR(xUartQueue, &byte, &xHigherPriorityTaskWoken);
    }

    if (sr & (USART_SR_ORE | USART_SR_NE | USART_SR_FE)) {
        volatile uint32_t dummy = USART1->DR;         
        (void)dummy;
    }

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

int main(void)
{
    xUartQueue = xQueueCreate(512, sizeof(uint8_t));
    if (xUartQueue == NULL) {
        while (1);     
    }

    hardware_init();

    xTaskCreate(vLedFlashTask, "LED", 256, NULL, 1, NULL);
    
    // Оставляем ваш родной стек 1024 слова! Всё заведётся без переполнений.
    xTaskCreate(vReceiveTask, "Receive", 1024, NULL, 1, NULL);

    vTaskStartScheduler();

    while (1);
    return 0;
}
