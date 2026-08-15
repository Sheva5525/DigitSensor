#include "xmodem.h"
#include "stm32f4xx.h"
#include <string.h>

QueueHandle_t xUartQueue;

/* ------------------------------------------------------------------
   Отправка символа через USART1
------------------------------------------------------------------ */
void SendCharacter(uchar uc)
{
    while (!(USART1->SR & USART_SR_TXE));
    USART1->DR = uc;
}

/* ------------------------------------------------------------------
   Простейший приём файла:
   1) ждём 4 байта длины файла (little-endian)
   2) ждём сами данные (ровно столько, сколько указано)
   3) отправляем ACK при успехе или NAK при ошибке/таймауте
------------------------------------------------------------------ */

uint32_t SimpleReceiveFile(uint8_t *buffer, uint32_t max_len, uint32_t timeout_ms)
{
    uint8_t len_bytes[4];
    uint32_t file_len = 0;

    xQueueReset(xUartQueue);

    SendCharacter('S');

    // 3. Приём длины файла (4 байта)
    for (int i = 0; i < 4; i++)
    {
        if (xQueueReceive(xUartQueue, &len_bytes[i], pdMS_TO_TICKS(timeout_ms)) != pdPASS)
        {
            SendCharacter(NAK); // Выход по таймауту
            return 0;
        }
    }

    // Собираем длину (Little-Endian)
    file_len = (uint32_t)len_bytes[0] |
               ((uint32_t)len_bytes[1] << 8) |
               ((uint32_t)len_bytes[2] << 16) |
               ((uint32_t)len_bytes[3] << 24);

    // Проверяем лимит буфера
    if (file_len == 0 || file_len > max_len)
    {
        SendCharacter(NAK); // Длина некорректна
        return 0;
    }

    // 4. Приём самих данных файла
    for (uint32_t i = 0; i < file_len; i++)
    {
        if (xQueueReceive(xUartQueue, &buffer[i], pdMS_TO_TICKS(timeout_ms)) != pdPASS)
        {
            SendCharacter(NAK); // Обрыв связи во время передачи данных
            return 0;
        }
    }

    // 5. УСПЕХ
    SendCharacter(ACK);
    return file_len;
}
