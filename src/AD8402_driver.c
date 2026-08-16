#include "AD8402_driver.h"
#include "stm32f4xx.h"

void AD8402_Write(uint8_t channel, uint8_t value)
{

    uint32_t cr1_backup = SPI1->CR1;

    SPI1->CR1 = (cr1_backup & ~SPI_CR1_BR_Msk) | (SPI_CR1_BR_2 | SPI_CR1_BR_0);

    while (SPI1->SR & SPI_SR_BSY);

    (void)SPI1->DR;

    GPIOB->BSRR = GPIO_BSRR_BR0;

    while (!(SPI1->SR & SPI_SR_TXE)); 

    *(__IO uint8_t *)&SPI1->DR = (channel & 0x01); 

    while (!(SPI1->SR & SPI_SR_RXNE));
    (void)SPI1->DR; // Обнуление буфера

    while (!(SPI1->SR & SPI_SR_TXE)); 
    *(__IO uint8_t *)&SPI1->DR = value;
    
    while (!(SPI1->SR & SPI_SR_RXNE));
    (void)SPI1->DR; // Обнуление буфера

    while (SPI1->SR & SPI_SR_BSY);

    GPIOB->BSRR = GPIO_BSRR_BS0;

    SPI1->CR1 &= ~SPI_CR1_SPE; 
    SPI1->CR1 = cr1_backup; 

    (void)SPI1->DR;

}
