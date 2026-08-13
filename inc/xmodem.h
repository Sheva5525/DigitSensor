#ifndef XMODEM_H
#define XMODEM_H

#include <stdint.h>
#include <stdbool.h>
#include "FreeRTOS.h"
#include "queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Протокольные байты */
#define ACK  0x06
#define NAK  0x15

/* Типы */
typedef uint8_t  uchar;
typedef uint16_t ushort;
typedef uint32_t ulong;

/* Глобальная очередь UART (создаётся в main.c) */
extern QueueHandle_t xUartQueue;

/* Низкоуровневая отправка символа (блокирующая) */
void SendCharacter(uchar uc);

/* Функция приёма файла по простому протоколу:
   [4 байта длины файла (little-endian)] [данные] -> ACK/NAK
   Возвращает фактическую длину принятых данных или 0 при ошибке. */
uint32_t SimpleReceiveFile(uint8_t *buffer, uint32_t max_len, uint32_t timeout_ms);

/* Задержка (если понадобится) */
void Delay(void);

#ifdef __cplusplus
}
#endif

#endif /* XMODEM_H */