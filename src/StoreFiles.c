#include "StoreFiles.h"
#include "ymodem.h"
#include "DataBase.h"

bool g_ymodem_mode = false;

void vReceive()
{
    if (g_ymodem_mode)
    {
        uint32_t ymodem_file_size = 0;
        COM_StatusTypeDef ymodem_status = Ymodem_Receive(&ymodem_file_size);

        if (ymodem_status == COM_OK && ymodem_file_size > 0)
        {
            g_receivedFile.length = ymodem_file_size;

            if (!DB_StoreFile(g_receivedFile.data, g_receivedFile.length))
            {
                // В будущем написать assert
            }
        }
        else
        {
            if (!DB_ReadFile(g_receivedFile.data, sizeof(g_receivedFile.data), &g_receivedFile.length))
            {
                g_receivedFile.length = 0;
            }
        }
    }
    else
    {
        if (!DB_ReadFile(g_receivedFile.data, sizeof(g_receivedFile.data), &g_receivedFile.length))
        {
            g_receivedFile.length = 0;
        }
    }

    vTaskDelete(NULL);
}
