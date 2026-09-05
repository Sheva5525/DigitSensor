#include "AD8402_driver.h"
#include "stm32f4xx.h"

void AD8402_Write(uint32_t pin_reset_mask, uint32_t pin_set_mask, uint8_t channel, uint8_t value)
{
    // Ожидаем окончания предыдущей передачи, если SPI занят
    while (SPI1->SR & SPI_SR_BSY);
    
    // Очищаем буфер приемника от возможного старого мусора
    (void)SPI1->DR;

    // Активируем нужный чип (опускаем выбранный CS в 0)
    GPIOB->BSRR = pin_reset_mask;

    // --- 1. Отправка байта адреса канала ---
    while (!(SPI1->SR & SPI_SR_TXE)); 
    // В AD8402 адрес канала задается битами A1 и A0 (0 или 1)
    *(__IO uint8_t *)&SPI1->DR = (channel & 0x03); 

    // Ждем, пока байт уйдет в кремний, и очищаем буфер приема
    while (!(SPI1->SR & SPI_SR_RXNE));
    (void)SPI1->DR; 

    // --- 2. Отправка байта значения (0..255) ---
    while (!(SPI1->SR & SPI_SR_TXE)); 
    *(__IO uint8_t *)&SPI1->DR = value;
    
    // Ждем завершения приема мусорного байта
    while (!(SPI1->SR & SPI_SR_RXNE));
    (void)(__IO uint8_t)SPI1->DR; 

    // Ждем, пока SPI полностью доплюет последние биты по воздуху
    while (SPI1->SR & SPI_SR_BSY);

    // Деактивируем чип (поднимаем выбранный CS в 1)
    GPIOB->BSRR = pin_set_mask;
}
