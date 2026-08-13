#ifndef DATABASE_H
#define DATABASE_H

#include <stdint.h>
#include <stdbool.h>
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#define DB_MAX_ROWS 50    // Размер N вашего массива

typedef enum {
    TYPE_RAW_INT = 0,
    TYPE_TEMPERATURE,
    TYPE_VOLTAGE,
    TYPE_COUNTER,
    TYPE_CONFIG
} DB_ValueType_t;

// Ваша структура значения (размер ровно 8 байт из-за выравнивания компилятора)
typedef struct {
    bool is_readable;     
    bool save_to_flash;   
    int32_t raw_data;     
    DB_ValueType_t type;  
} DB_Value_t;

typedef struct {
    uint8_t key;          
    DB_Value_t value;     
    bool is_active;       
} DB_Row_t;

// API функции
bool DB_Init(void);
bool DB_Insert(uint8_t key, DB_Value_t val);
bool DB_Select(uint8_t key, DB_Value_t *out_val);
bool DB_Delete(uint8_t key);
void DB_LoadFromFlash(void); // Загрузка сохраненных ключей при старте

#endif // DATABASE_H
