#ifndef W25Q16JVSNIQ_DRIVER_H
#define W25Q16JVSNIQ_DRIVER_H

#include "stm32f4xx.h"

#define W25Q16_PAGE_SIZE        256
#define W25Q16_SECTOR_SIZE      4096
#define W25Q16_BLOCK_SIZE       65536
#define W25Q16_TOTAL_PAGES      8192
#define W25Q16_TOTAL_SECTORS    512

#define CMD_WRITE_ENABLE        0x06
#define CMD_WRITE_DISABLE       0x04
#define CMD_READ_STATUS_REG1    0x05
#define CMD_WRITE_STATUS_REG1   0x01
#define CMD_READ_DATA           0x03
#define CMD_FAST_READ           0x0B
#define CMD_PAGE_PROGRAM        0x02
#define CMD_SECTOR_ERASE_4KB    0x20
#define CMD_BLOCK_ERASE_32KB    0x52
#define CMD_BLOCK_ERASE_64KB    0xD8
#define CMD_CHIP_ERASE          0xC7
#define CMD_JEDEC_ID            0x9F

#define W25Q16_CS_LOW()         (GPIOA->BSRR = (1UL << (4 + 16)))
#define W25Q16_CS_HIGH()        (GPIOA->BSRR = (1UL << 4))

void W25Q16_Init(void);
uint8_t W25Q16_SPI_Transfer(uint8_t data);
uint32_t W25Q16_Read_JEDEC_ID(void);
void W25Q16_Wait_Busy(void);
void W25Q16_Write_Enable(void);
void W25Q16_Erase_Sector(uint32_t address);
void W25Q16_Write_Page(uint32_t address, uint8_t *pData, uint16_t size);
void W25Q16_Read_Data(uint32_t address, uint8_t *pBuffer, uint32_t size);

#endif // W25Q16JVSNIQ_DRIVER_H
