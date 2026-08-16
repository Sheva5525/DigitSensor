#ifndef UI_H
#define UI_H

#include "ucg.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdbool.h>
#include <stdint.h>

#define MAX_MENU_DEPTH 6  // Максимальная глубина вложенности меню
#define MENU_SIZE      28

// Режимы работы интерфейса и энкодера
typedef enum
{
    UI_MODE_NAVIGATE,     // Пролистывание меню. (серый фон строки)
    UI_MODE_EDIT,         // Редаактирвоание параметра (белое окно справа)
    UI_MODE_CUSTOM        // Кастомное меню. На будущее
} UiMode_t;

// Типы элементов в списке
typedef enum
{
    ITEM_BACK,            // Кнопка "< Back". Всегда первая в меню
    ITEM_SUBMENU,         // Переход во вложенную папку меню
    ITEM_PARAM_INT,       // Целочисленный параметр
    ITEM_CUSTOM_PAGE,     // Переход в кастомное меню
    ITEM_LABEL
} MenuItemType_t;

// Структура элемента меню
typedef struct MenuItem
{
    const char *name;     // Имя слева
    MenuItemType_t type;  // Тип
    bool is_enabled;      // Флаг доступностиЖ true - активен, false - серый, неактивный
    
    union
    {
        struct Menu *next_menu; // ITEM_SUBMENU
        struct
        {
            uint16_t db_index;
            int32_t min;
            int32_t max;
            int32_t step;
        } int_param;            // ITEM_PARAM_INT
        void (*custom_init_cb)(void); // Колбэк для запуска кастомного меню
    } load;
} MenuItem_t;

// Шаблонная структура меню
typedef struct Menu
{
    const char *title;    // Заголовок на экране
    MenuItem_t *items;    // Массив пунктов
    uint8_t size;         // Количество пунктов
} Menu_t;

// Структура для стека истории для работы перехода назад
typedef struct
{
    Menu_t *menu;         // Какое меню было открыто
    uint8_t cursor;       // На какой строчке стоял курсор
} MenuHistory_t;

// Глобальное состояние интерфейса
typedef struct
{
    UiMode_t mode;
    Menu_t *current_menu;
    uint8_t cursor;
    bool force_refresh;
    int32_t temp_value;
    uint8_t scroll_offset;
} UiState_t;

void UI_Init(void);
void UI_ProcessNavigate(int8_t direction);
void UI_ProcessAction(void);

void vGuiTask(void *pvParameters);

void UI_ProcessNavigate(int8_t direction); // Принимает 1 (вниз) или -1 (вверх)
void UI_ProcessAction(void);               // Вызывается при клике на кнопку

#endif // UI_H
