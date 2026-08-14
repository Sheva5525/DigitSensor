#include "DataBase.h"
#include "W25Q16JVSNIQ_driver.h"

static DB_Row_t _db_storage[DB_MAX_ROWS];
static SemaphoreHandle_t _db_mutex = NULL;

#define DB_TIMEOUT_TICKS  pdMS_TO_TICKS(20) 

DB_File_t g_receivedFile = {0};

bool DB_Init(void)
{
    if (_db_mutex == NULL)
    {
        _db_mutex = xSemaphoreCreateMutex();
        if (_db_mutex == NULL) return false;
    }

    for (uint32_t i = 0; i < DB_MAX_ROWS; i++)
    {
        _db_storage[i].is_active = false;
    }

    return true;
}

// Положить значение в БД. Если его нет - сделается новое.
bool DB_Insert(uint8_t key, DB_Value_t val)
{
    if (_db_mutex == NULL)
    {
        return false;
    }
    
    if (xSemaphoreTake(_db_mutex, DB_TIMEOUT_TICKS) != pdTRUE)
    {
        return false;
    }

    int32_t target_index = -1;

    // Ищем, есть ли уже такой ключ, или ищем пустую строку
    for (uint32_t i = 0; i < DB_MAX_ROWS; i++)
    {
        if (_db_storage[i].is_active && _db_storage[i].key == key)
        {
            target_index = (int32_t)i;
            break;
        }
        if (!_db_storage[i].is_active && target_index == -1)
        {
            target_index = (int32_t)i;
        }
    }

    if (target_index != -1)
    {
        uint32_t idx = (uint32_t)target_index;
        _db_storage[idx].key = key;
        _db_storage[idx].value = val;
        _db_storage[idx].is_active = true;

        // Запись во флеш
        if (val.save_to_flash)
        {
            uint32_t flash_addr = VARS_START_ADDR + (key * VARS_SLOT_SIZE);
            W25Q16_Write_Page(flash_addr, (uint8_t*)&val, sizeof(DB_Value_t));
        }

        xSemaphoreGive(_db_mutex);
        return true;
    }

    xSemaphoreGive(_db_mutex);
    return false; 
}

// Взять значение из БД по ключу
bool DB_Select(uint8_t key, DB_Value_t *out_val)
{
    if (_db_mutex == NULL || out_val == NULL)
    {
        return false;
    }
    
    if (xSemaphoreTake(_db_mutex, DB_TIMEOUT_TICKS) != pdTRUE)
    {
        return false;
    }

    for (uint32_t i = 0; i < DB_MAX_ROWS; i++)
    {
        if (_db_storage[i].is_active && _db_storage[i].key == key)
        {
            if (!_db_storage[i].value.is_readable)
            {
                xSemaphoreGive(_db_mutex);
                return false; 
            }
            
            *out_val = _db_storage[i].value;
            xSemaphoreGive(_db_mutex);
            
            return true;
        }
    }
    xSemaphoreGive(_db_mutex);
    return false;
}

// Восстановление сохраненных параметров из Flash
void DB_LoadFromFlash(void)
{
    DB_Value_t loaded_val;
    
    // Проходим по списку критически важных конфигурационных ключей
    // Допустим, вы выделили ключи с 0 по 10 под энергонезависимые настройки
    for (uint8_t key = 0; key < 10; key++)
    {
        uint32_t flash_addr = VARS_START_ADDR + (key * VARS_SLOT_SIZE);
        
        // Считывание из флеш-памяти напрямую в буфер
        W25Q16_Read_VarSlot(key, (uint8_t*)&loaded_val, sizeof(DB_Value_t));
        
        // Проверка валидности данных
        if (loaded_val.save_to_flash == true && loaded_val.raw_data != -1)
        {
            DB_Insert(key, loaded_val);
        }
    }
}

bool DB_StoreFile(const uint8_t *data, uint32_t length)
{
    if (data == NULL || length == 0 || length > FILE_MAX_SIZE)
    {
        return false;
    }

    if (_db_mutex == NULL)
    {
        return false;
    }
    
    if (xSemaphoreTake(_db_mutex, DB_TIMEOUT_TICKS) != pdTRUE)
    {
        return false;
    }

    uint32_t total_size = FILE_HEADER_SIZE + length;
    uint32_t num_sectors = (total_size + FLASH_SECTOR_SIZE - 1) / FLASH_SECTOR_SIZE;

    for (uint32_t i = 0; i < num_sectors; i++)
    {
        W25Q16_Erase_Sector(FILE_START_ADDR + i * FLASH_SECTOR_SIZE);
    }

    uint32_t len = length;
    W25Q16_Write_Page(FILE_START_ADDR, (uint8_t*)&len, sizeof(len));

    uint32_t offset = 0;
    while (offset < length)
    {
        uint32_t chunk = (length - offset > 256) ? 256 : (length - offset);
        uint32_t addr = FILE_DATA_OFFSET + offset;

        // Если chunk < 256, дополняем буфер 0xFF, чтобы не повредить соседние данные
        uint8_t page_buf[256];
        if (chunk < 256)
        {
            memset(page_buf, 0xFF, sizeof(page_buf));
            memcpy(page_buf, data + offset, chunk);
            W25Q16_Write_Page(addr, page_buf, 256);
        }
        else
        {
            W25Q16_Write_Page(addr, (uint8_t*)(data + offset), chunk);
        }

        offset += chunk;
    }

    xSemaphoreGive(_db_mutex);
    return true;
}

// Чтение файла из Flash
bool DB_ReadFile(uint8_t *buffer, uint32_t max_length, uint32_t *out_length)
{
    if (buffer == NULL || out_length == NULL)
    {
        return false;
    }

    if (_db_mutex == NULL)
    {
        return false;
    }
    
    if (xSemaphoreTake(_db_mutex, DB_TIMEOUT_TICKS) != pdTRUE)
    {
        return false;
    }

    // Читаем заголовок
    uint32_t stored_len = 0;
    W25Q16_Read_Data(FILE_START_ADDR, (uint8_t*)&stored_len, sizeof(stored_len));

    // Проверка валидности: если флеш стерта, там будет 0xFFFFFFFF
    if (stored_len == 0xFFFFFFFF || stored_len == 0) {
        xSemaphoreGive(_db_mutex);
        return false;
    }

    // Проверяем, что буфер достаточен
    if (stored_len > max_length)
    {
        xSemaphoreGive(_db_mutex);
        return false;
    }

    W25Q16_Read_Data(FILE_DATA_OFFSET, buffer, stored_len);
    *out_length = stored_len;

    xSemaphoreGive(_db_mutex);
    return true;
}
