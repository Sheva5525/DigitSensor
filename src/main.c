#include "stm32f4xx.h"
#include "FreeRTOS.h"
#include "task.h"
#include "W25Q16JVSNIQ_driver.h"

void hardware_init(void);

// Глобальные массивы, чтобы их было удобно вытянуть в окно Watch отладчика
uint8_t flash_save_data[16] = "PawnScript_v1.0"; // То, что хотим сохранить
uint8_t flash_load_data[16] = {0};               // Сюда считаем данные из памяти

void vLedBlinkTask(void *pvParameters) {
    (void)pvParameters;
    
    vTaskDelay(100 / portTICK_PERIOD_MS);

    // ЗДЕСЬ БОЛЬШЕ НЕТ СТИРАНИЯ И ЗАПИСИ! Мы ничего не отправляем во флешку.
    // Контроллер просто пытается прочитать то, что ТАМ УЖЕ БЫЛО сохранено ранее.
    W25Q16_Read_Data(0x000000, flash_load_data, 16);

    while (1) {
        GPIOC->ODR ^= (1UL << 13);
        vTaskDelay(500 / portTICK_PERIOD_MS); 
    }
}


int main(void) {
    hardware_init();

    xTaskCreate(
        vLedBlinkTask,
        "LedBlink",
        150, 
        NULL,
        1,
        NULL
    );

    vTaskStartScheduler();

    while (1);
    return 0;
}

void hardware_init(void) {
    RCC->AHB1ENR |= (RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN);
    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;

    GPIOC->MODER &= ~(3UL << (13 * 2));
    GPIOC->MODER |=  (1UL << (13 * 2));
    GPIOC->BSRR   =  (1UL << 13);

    GPIOA->MODER &= ~(3UL << (4 * 2));
    GPIOA->MODER |=  (1UL << (4 * 2));
    W25Q16_CS_HIGH();

    GPIOA->MODER &= ~((3UL << (5 * 2)) | (3UL << (6 * 2)) | (3UL << (7 * 2)));
    GPIOA->MODER |=  ((2UL << (5 * 2)) | (2UL << (6 * 2)) | (2UL << (7 * 2)));

    GPIOA->OSPEEDR &= ~((3UL << (5 * 2)) | (3UL << (6 * 2)) | (3UL << (7 * 2)));
    GPIOA->OSPEEDR |=  ((2UL << (5 * 2)) | (2UL << (6 * 2)) | (2UL << (7 * 2)));

    GPIOA->PUPDR &= ~((3UL << (5 * 2)) | (3UL << (6 * 2)) | (3UL << (7 * 2)));

    // Связываем физические ножки PA5, PA6, PA7 с внутренним ядром SPI1 (AF5)
    // Используем AFR[0], так как пины 5, 6 и 7 относятся к младшему регистру
    GPIOA->AFR[0] &= ~((0xFUL << (5 * 4)) | (0xFUL << (6 * 4)) | (0xFUL << (7 * 4)));
    GPIOA->AFR[0] |=  ((5UL << (5 * 4)) | (5UL << (6 * 4)) | (5UL << (7 * 4)));


    SPI1->CR1 = 0; 
    SPI1->CR1 |= SPI_CR1_MSTR;
    SPI1->CR1 |= SPI_CR1_SSI | SPI_CR1_SSM;
    SPI1->CR1 |= (7UL << SPI_CR1_BR_Pos); 
    
    SPI1->CR1 |= SPI_CR1_SPE;
}
