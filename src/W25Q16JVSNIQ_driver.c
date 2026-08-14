#include "W25Q16JVSNIQ_driver.h"

uint8_t W25Q16_SPI_Transfer(uint8_t data)
{
    while (!(SPI1->SR & SPI_SR_TXE));  
    SPI1->DR = data;                   
    while (!(SPI1->SR & SPI_SR_RXNE)); 
    return (uint8_t)(SPI1->DR);                   
}

uint32_t W25Q16_Read_JEDEC_ID(void)
{
    uint8_t b1, b2, b3;
    W25Q16_CS_LOW();
    W25Q16_SPI_Transfer(CMD_JEDEC_ID);
    b1 = W25Q16_SPI_Transfer(0x00);
    b2 = W25Q16_SPI_Transfer(0x00);
    b3 = W25Q16_SPI_Transfer(0x00);
    W25Q16_CS_HIGH();
    return ((uint32_t)b1 << 16) | ((uint32_t)b2 << 8) | b3;
}

void W25Q16_Wait_Busy(void)
{
    uint8_t status = 0;
    do
    {
        W25Q16_CS_LOW();
        W25Q16_SPI_Transfer(CMD_READ_STATUS_REG1);
        status = W25Q16_SPI_Transfer(0x00);
        W25Q16_CS_HIGH();
    } while (status & 0x01);
}

void W25Q16_Write_Enable(void)
{
    W25Q16_CS_LOW();
    W25Q16_SPI_Transfer(CMD_WRITE_ENABLE);
    W25Q16_CS_HIGH();
}

void W25Q16_Erase_Sector(uint32_t address)
{
    W25Q16_Write_Enable();
    W25Q16_Wait_Busy();
    W25Q16_CS_LOW();
    W25Q16_SPI_Transfer(CMD_SECTOR_ERASE_4KB);
    W25Q16_SPI_Transfer((address >> 16) & 0xFF);
    W25Q16_SPI_Transfer((address >> 8) & 0xFF);
    W25Q16_SPI_Transfer(address & 0xFF);
    W25Q16_CS_HIGH();
    W25Q16_Wait_Busy();
}

void W25Q16_Write_Page(uint32_t address, uint8_t *pData, uint16_t size)
{
    if (size > 256) size = 256;
    W25Q16_Write_Enable();
    W25Q16_Wait_Busy();
    W25Q16_CS_LOW();
    W25Q16_SPI_Transfer(CMD_PAGE_PROGRAM);
    W25Q16_SPI_Transfer((address >> 16) & 0xFF);
    W25Q16_SPI_Transfer((address >> 8) & 0xFF);
    W25Q16_SPI_Transfer(address & 0xFF);
    for (uint16_t i = 0; i < size; i++)
    {
        W25Q16_SPI_Transfer(pData[i]);
    }
    W25Q16_CS_HIGH();
    W25Q16_Wait_Busy();
}

void W25Q16_Read_Data(uint32_t address, uint8_t *pBuffer, uint32_t size)
{
    W25Q16_Wait_Busy();
    W25Q16_CS_LOW();
    W25Q16_SPI_Transfer(CMD_READ_DATA);
    W25Q16_SPI_Transfer((address >> 16) & 0xFF);
    W25Q16_SPI_Transfer((address >> 8) & 0xFF);
    W25Q16_SPI_Transfer(address & 0xFF);
    for (uint32_t i = 0; i < size; i++)
    {
        pBuffer[i] = W25Q16_SPI_Transfer(0x00);
    }
    W25Q16_CS_HIGH();
}

void W25Q16_Write_VarSlot(uint8_t slot_num, uint8_t *pData, uint16_t size)
{
    if (slot_num >= VARS_MAX_SLOTS) return;
    if (size > VARS_SLOT_SIZE) size = VARS_SLOT_SIZE;

    uint32_t sector_addr = VARS_START_ADDR + (slot_num * VARS_SLOT_SIZE);
    W25Q16_Erase_Sector(sector_addr);

    uint16_t bytes_written = 0;
    while (bytes_written < size)
    {
        uint16_t chunk_size = size - bytes_written;
        if (chunk_size > W25Q16_PAGE_SIZE)
        {
            chunk_size = W25Q16_PAGE_SIZE;
        }
        W25Q16_Write_Page(sector_addr + bytes_written, &pData[bytes_written], chunk_size);
        bytes_written += chunk_size;
    }
}

void W25Q16_Read_VarSlot(uint8_t slot_num, uint8_t *pBuffer, uint16_t size)
{
    if (slot_num >= VARS_MAX_SLOTS) return;
    if (size > VARS_SLOT_SIZE) size = VARS_SLOT_SIZE;

    uint32_t sector_addr = VARS_START_ADDR + (slot_num * VARS_SLOT_SIZE);
    W25Q16_Read_Data(sector_addr, pBuffer, size);
}
