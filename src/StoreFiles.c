#include "StoreFiles.h"
#include "ymodem.h"
#include "DataBase.h"
#include "DigitResistor.h"
#include <string.h>

bool g_ymodem_mode = false;
extern float calibrate_out1[256];
extern float calibrate_out2[256];

typedef struct {
    float   out1[256];
    float   out2[256];
    int32_t ohm1;
    int32_t ohm2;
} CalibBin_t;

bool ApplyCalibration(const uint8_t *data, uint32_t length)
{
    if (data == NULL) return false;
    if (length != sizeof(CalibBin_t)) return false;

    const CalibBin_t *c = (const CalibBin_t *)data;

    if (c->ohm1 < 100 || c->ohm1 > 5000) return false;
    if (c->ohm2 < 100 || c->ohm2 > 5000) return false;

    memcpy(calibrate_out1, c->out1, sizeof(calibrate_out1));
    memcpy(calibrate_out2, c->out2, sizeof(calibrate_out2));

    DB_Value_t v;
    if (DB_Select(OUT_1_1KOHM, &v)) { v.raw_data = c->ohm1; DB_Insert(OUT_1_1KOHM, v); }
    if (DB_Select(OUT_2_1KOHM, &v)) { v.raw_data = c->ohm2; DB_Insert(OUT_2_1KOHM, v); }

    DB_DynamicLimits();
    return true;
}

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
            ApplyCalibration(g_receivedFile.data, g_receivedFile.length);
        }
        else
        {
            if (!DB_ReadFile(g_receivedFile.data, sizeof(g_receivedFile.data), &g_receivedFile.length))
            {
                g_receivedFile.length = 0;
            }
            else
            {
                ApplyCalibration(g_receivedFile.data, g_receivedFile.length);
            }
        }
    }
    else
    {
        if (!DB_ReadFile(g_receivedFile.data, sizeof(g_receivedFile.data), &g_receivedFile.length))
        {
            g_receivedFile.length = 0;
        }
        else
        {
            ApplyCalibration(g_receivedFile.data, g_receivedFile.length);
        }
    }

    vTaskDelete(NULL);
}
