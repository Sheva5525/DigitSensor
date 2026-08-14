#ifndef DATABASE_H
#define DATABASE_H

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#define DB_MAX_ROWS 50    // Размер N вашего массива

#define FILE_START_ADDR     0x100000UL      // 1 МБ, кратно 4 КБ
#define FILE_HEADER_SIZE    256             // заголовок (256 байт)
#define FILE_DATA_OFFSET    (FILE_START_ADDR + FILE_HEADER_SIZE)
#define FILE_MAX_SIZE       16384UL         // максимальный размер данных (16 КБ)
#define FLASH_SECTOR_SIZE   4096UL          // размер сектора для стирания

#define MAX_FILE_SIZE   16384UL   // 16 КБ

typedef enum
{
    TYPE_RAW_INT = 0,
    TYPE_TEMPERATURE,
    TYPE_VOLTAGE,
    TYPE_COUNTER,
    TYPE_CONFIG
} DB_ValueType_t;


// Структура для хранения файла в ОЗУ
typedef struct
{
    uint8_t data[MAX_FILE_SIZE];
    uint32_t length;
} DB_File_t;

// Глобальный экземпляр файла
extern DB_File_t g_receivedFile;

typedef struct
{
    bool is_readable;     
    bool save_to_flash;   
    int32_t raw_data;     
    DB_ValueType_t type;  
} DB_Value_t;

typedef struct
{
    uint8_t key;          
    DB_Value_t value;     
    bool is_active;       
} DB_Row_t;

// API

bool DB_Init(void);

bool DB_Insert(uint8_t key, DB_Value_t val);

bool DB_Select(uint8_t key, DB_Value_t *out_val);

bool DB_Delete(uint8_t key);

void DB_LoadFromFlash(void); // Загрузка из флеша при старте

bool DB_StoreFile(const uint8_t *data, uint32_t length);

bool DB_ReadFile(uint8_t *buffer, uint32_t max_length, uint32_t *out_length);

#endif // DATABASE_H
