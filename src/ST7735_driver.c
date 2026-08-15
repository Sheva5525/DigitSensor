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

// Используем проверенные макросы быстрого управления пинами через BSRR
#define TEST_CS_H()   GPIOB->BSRR = (1UL << 12)  // PB12 в 1
#define TEST_CS_L()   GPIOB->BSRR = (1UL << 28)  // PB12 в 0
#define TEST_DC_H()   GPIOA->BSRR = (1UL << 2)   // PA2 в 1 (Данные)
#define TEST_DC_L()   GPIOA->BSRR = (1UL << 18)  // PA2 в 0 (Команда)
#define TEST_RST_H()  GPIOA->BSRR = (1UL << 1)   // PA1 в 1
#define TEST_RST_L()  GPIOA->BSRR = (1UL << 17)  // PA1 в 0

int16_t ucg_com_stm32_spi_cb(ucg_t *ucg, int16_t msg, uint16_t arg, uint8_t *data) 
{
  switch(msg)
  {
    case UCG_COM_MSG_POWER_UP:
      // Железо уже настроено в SPI2_Init()
      TEST_CS_H();
      break;

    case UCG_COM_MSG_POWER_DOWN:
      break;

    case UCG_COM_MSG_DELAY:
      if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
          uint32_t ticks = pdMS_TO_TICKS(arg / 1000);
          vTaskDelay(ticks > 0 ? ticks : 1);
      } else {
          for (volatile uint32_t i = 0; i < (arg * 12); i++);
      }
      break;

    case UCG_COM_MSG_CHANGE_RESET_LINE:
      if (arg != 0) {
          TEST_RST_H(); // Отпустить сброс
      } else {
          TEST_RST_L(); // Аппаратный сброс (Reset активен)
      }
      break;

    case UCG_COM_MSG_CHANGE_CS_LINE:
      // Внимание: в шаблонах некоторых версий u2g/ucg arg == 0 означает СБРОС (активировать)
      // Но в Ucglib безопаснее проверять прямо: arg == 0 активирует экран (CS Low)
      if (arg == 0) {
          TEST_CS_L(); 
      } else {
          while (SPI2->SR & SPI_SR_BSY); 
          TEST_CS_H(); 
      }
      break;

    case UCG_COM_MSG_CHANGE_CD_LINE:
      while (SPI2->SR & SPI_SR_BSY); // Ждем окончания физической отправки
      if (arg != 0) {
          TEST_DC_H(); // Режим Данных (Data)
      } else {
          TEST_DC_L(); // Режим Команды (Command)
      }
      break;

case UCG_COM_MSG_SEND_BYTE:
      while (!(SPI2->SR & SPI_SR_TXE));
      *(volatile uint8_t *)&SPI2->DR = (uint8_t)arg;
      break;

    case UCG_COM_MSG_REPEAT_1_BYTE:
      // Передается ОДИН байт, который лежит в data[0]
      for (uint32_t i = 0; i < arg; i++) {
          while (!(SPI2->SR & SPI_SR_TXE));
          *(volatile uint8_t *)&SPI2->DR = data[0]; 
      }
      while (SPI2->SR & SPI_SR_BSY); 
      break;

    case UCG_COM_MSG_REPEAT_2_BYTES:
      // Передается ДВА байта (data[0] и data[1]), повторяем arg раз
      for (uint32_t i = 0; i < arg; i++) {
          while (!(SPI2->SR & SPI_SR_TXE));
          *(volatile uint8_t *)&SPI2->DR = data[0];
          while (!(SPI2->SR & SPI_SR_TXE));
          *(volatile uint8_t *)&SPI2->DR = data[1];
      }
      while (SPI2->SR & SPI_SR_BSY); 
      break;

    case UCG_COM_MSG_REPEAT_3_BYTES:
      for (uint32_t i = 0; i < arg; i++) {
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
      while(arg > 0) {
          while (!(SPI2->SR & SPI_SR_TXE));
          *(volatile uint8_t *)&SPI2->DR = *data;
          data++;
          arg--;
      }
      while (SPI2->SR & SPI_SR_BSY); 
      break;

    case UCG_COM_MSG_SEND_CD_DATA_SEQUENCE:
      // data[0] — это флаг управления линией DC. data[1] — сам байт.
      while(arg > 0) {
          if ( data[0] != 0 ) {
              while (SPI2->SR & SPI_SR_BSY); // Ждем завершения перед дерганием DC
              if ( data[0] == 1 ) {
                  TEST_DC_L(); // 1 в Ucglib — это Команда (CD/DC Low)
              } else if ( data[0] == 2 ) {
                  TEST_DC_H(); // 2 в Ucglib — это Данные (CD/DC High)
              }
          }
          data++; // Переходим к байту данных
          
          while (!(SPI2->SR & SPI_SR_TXE));
          *(volatile uint8_t *)&SPI2->DR = data[0];
          data++; // Переходим к следующей паре флаг/данные
          arg--;
      }
      while (SPI2->SR & SPI_SR_BSY); 
      break;
  }
  return 1;
}
