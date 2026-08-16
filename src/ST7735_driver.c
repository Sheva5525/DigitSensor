#include "ST7735_driver.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "stm32f4xx.h"

// Макросы управления пинами для наглядности (BSRR регистр)
#define CS_H()   GPIOB->BSRR = (1UL << 12)
#define CS_L()   GPIOB->BSRR = (1UL << 28)
#define DC_H()   GPIOA->BSRR = (1UL << 2)  // Данные
#define DC_L()   GPIOA->BSRR = (1UL << 18) // Команда
#define RST_H()  GPIOA->BSRR = (1UL << 1)
#define RST_L()  GPIOA->BSRR = (1UL << 17)

int16_t ucg_com_stm32_spi_cb(ucg_t *ucg, int16_t msg, uint16_t arg, uint8_t *data)
{
  switch(msg)
  {
    case UCG_COM_MSG_POWER_UP:
      CS_H();
      break;
    case UCG_COM_MSG_POWER_DOWN:
      break;
    case UCG_COM_MSG_DELAY:
      if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
      {
          uint32_t ticks = pdMS_TO_TICKS(arg / 1000);
          vTaskDelay(ticks > 0 ? ticks : 1);
      }
      else
      {
          for (volatile uint32_t i = 0; i < (arg * 12); i++);
      }
      break;
    case UCG_COM_MSG_CHANGE_RESET_LINE:
      if (arg != 0)
      {
          RST_H(); // Отпустить сброс
      } else {
          RST_L(); // Аппаратный сброс
      }
      break;
    case UCG_COM_MSG_CHANGE_CS_LINE:
      if (arg == 0)
      {
          CS_L(); 
      }
      else
      {
          while (SPI2->SR & SPI_SR_BSY); 
          CS_H(); 
      }
      break;
    case UCG_COM_MSG_CHANGE_CD_LINE:
      while (SPI2->SR & SPI_SR_BSY);
      if (arg != 0)
      {
          DC_H();
      }
      else
      {
          DC_L();
      }
      break;
    case UCG_COM_MSG_SEND_BYTE:
          while (!(SPI2->SR & SPI_SR_TXE));
          *(volatile uint8_t *)&SPI2->DR = (uint8_t)arg;
          break;
    case UCG_COM_MSG_REPEAT_1_BYTE:
      for (uint32_t i = 0; i < arg; i++)
      {
          while (!(SPI2->SR & SPI_SR_TXE));
          *(volatile uint8_t *)&SPI2->DR = data[0]; 
      }
      while (SPI2->SR & SPI_SR_BSY); 
      break;
    case UCG_COM_MSG_REPEAT_2_BYTES:
      for (uint32_t i = 0; i < arg; i++)
      {
          while (!(SPI2->SR & SPI_SR_TXE));
          *(volatile uint8_t *)&SPI2->DR = data[0];
          while (!(SPI2->SR & SPI_SR_TXE));
          *(volatile uint8_t *)&SPI2->DR = data[1];
      }
      while (SPI2->SR & SPI_SR_BSY); 
      break;
    case UCG_COM_MSG_REPEAT_3_BYTES:
      for (uint32_t i = 0; i < arg; i++)
      {
          while (!(SPI2->SR & SPI_SR_TXE));
          *(volatile uint8_t *)&SPI2->DR = data[0];
          while (!(SPI2->SR & SPI_SR_TXE));
          *(volatile uint8_t *)&SPI2->DR = data[1];
          while (!(SPI2->SR & SPI_SR_TXE));
          *(volatile uint8_t *)&SPI2->DR = data[2];
      }
      while (SPI2->SR & SPI_SR_BSY); 
      break;
    case UCG_COM_MSG_SEND_STR:
      while(arg > 0)
      {
          while (!(SPI2->SR & SPI_SR_TXE));
          *(volatile uint8_t *)&SPI2->DR = *data;
          data++;
          arg--;
      }
      while (SPI2->SR & SPI_SR_BSY); 
      break;
    case UCG_COM_MSG_SEND_CD_DATA_SEQUENCE:
      while(arg > 0)
      {
          if ( data[0] != 0 )
          {
              while (SPI2->SR & SPI_SR_BSY);
              if ( data[0] == 1 )
              {
                  DC_L();
              } else if ( data[0] == 2 )
              {
                  DC_H();
              }
          }
          data++;
          while (!(SPI2->SR & SPI_SR_TXE));
          *(volatile uint8_t *)&SPI2->DR = data[0];
          data++;
          arg--;
      }
      while (SPI2->SR & SPI_SR_BSY); 
      break;
  }
  return 1;
}
