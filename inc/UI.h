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
typedef enum {
    UI_MODE_NAVIGATE,     // Листаем меню (серый фон строки)
    UI_MODE_EDIT,         // Редактируем параметр (белое окошко справа)
    UI_MODE_CUSTOM        // Кастомное приложение (например, менеджер файлов LittleFS)
} UiMode_t;

// Типы элементов в списке
typedef enum {
    ITEM_BACK,            // Кнопка "< Back" (всегда первая в меню)
    ITEM_SUBMENU,         // Переход во вложенную папку меню
    ITEM_PARAM_INT,       // Целочисленный параметр (изменение в белом окошке)
    ITEM_CUSTOM_PAGE,     // Переход на кастомный экран (LittleFS и т.д.)
    ITEM_LABEL
} MenuItemType_t;

// Структура элемента меню
typedef struct MenuItem {
    const char *name;     // Имя слева
    MenuItemType_t type;  // Тип
    bool is_enabled;      // Флаг доступности (true - активен, false - серый)
    
    union {
        struct Menu *next_menu; // Для ITEM_SUBMENU
        struct {
            uint16_t db_index;
            int32_t min;
            int32_t max;
            int32_t step;
        } int_param;            // Для ITEM_PARAM_INT
        void (*custom_init_cb)(void); // Колбэк для запуска кастомного экрана (например, LittleFS)
    } load;
} MenuItem_t;

// Структура самого меню
typedef struct Menu {
    const char *title;    // Заголовок на экране
    MenuItem_t *items;    // Массив пунктов
    uint8_t size;         // Количество пунктов
} Menu_t;

// Структура для стека истории (чтобы работал "Back")
typedef struct {
    Menu_t *menu;         // Какое меню было открыто
    uint8_t cursor;       // На какой строчке стоял курсор
} MenuHistory_t;

// Глобальное состояние интерфейса
typedef struct {
    UiMode_t mode;
    Menu_t *current_menu;
    uint8_t cursor;
    bool force_refresh;
    int32_t temp_value;
    uint8_t scroll_offset;
} UiState_t;

extern UiState_t ui;

void UI_Init(void);
void UI_ProcessNavigate(int8_t direction);
void UI_ProcessAction(void);

void vGuiTask(void *pvParameters);

void UI_ProcessNavigate(int8_t direction); // Принимает 1 (вниз) или -1 (вверх)
void UI_ProcessAction(void);               // Вызывается при клике на кнопку

#endif // UI_H
