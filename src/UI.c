#include "UI.h"
#include <stdio.h>
// Реальные переменные для параметров меню (в будущем они могут уйти в DB.c)
static int32_t param_brightness = 50;
static int32_t param_volume     = 20;
static int32_t param_contrast   = 80;
extern int16_t ucg_com_stm32_spi_cb(ucg_t *ucg, int16_t msg, uint16_t arg, uint8_t *data);
// Объявляем меню заранее, чтобы они могли ссылаться друг на друга
static Menu_t main_menu;
static Menu_t settings_menu;

// --- 1. ПУНКТЫ И СТРУКТУРА МЕНЮ НАСТРОЕК (ВЛОЖЕННОЕ) ---
static MenuItem_t settings_items[MENU_SIZE] = {
    { .name = "< Back",       .type = ITEM_BACK,      .is_enabled = true },
    { .name = "Brightness",   .type = ITEM_PARAM_INT, .is_enabled = true,  .load.int_param = { &param_brightness, 0, 100, 5 } },
    { .name = "Contrast",     .type = ITEM_PARAM_INT, .is_enabled = true,  .load.int_param = { &param_contrast, 10, 100, 2 } },
    { .name = "Reset All",    .type = ITEM_PARAM_INT, .is_enabled = false } // Заблокирован
};

static Menu_t settings_menu = {
    .title = "SETTINGS:",
    .items = settings_items,
    .size = MENU_SIZE
};

// --- 2. ПУНКТЫ И СТРУКТУРА ГЛАВНОГО МЕНЮ (КОРНЕВОГО) ---
static MenuItem_t main_menu_items[MENU_SIZE] = {
    { .name = "Open Settings", .type = ITEM_SUBMENU,   .is_enabled = true,  .load.next_menu = &settings_menu },
    { .name = "Volume Ctrl",   .type = ITEM_PARAM_INT, .is_enabled = true,  .load.int_param = { &param_volume, 0, 30, 1 } },
    { .name = "LittleFS Files",.type = ITEM_CUSTOM_PAGE,.is_enabled = false, .load.custom_init_cb = NULL }, // Пока выключен
    { .name = "Device Info",   .type = ITEM_PARAM_INT, .is_enabled = false } // Заблокирован
};

static Menu_t main_menu = {
    .title = "MAIN MENU:",
    .items = main_menu_items,
    .size = MENU_SIZE
};
UiState_t ui = {
    .mode = UI_MODE_NAVIGATE,
    .current_menu = &main_menu, // Ссылка железно зафиксирована
    .cursor = 0,
    .force_refresh = true
};

ucg_t ucg;


// --- СТЕК ИСТОРИИ И СОСТОЯНИЕ ---
UiState_t ui;
static MenuHistory_t history[MAX_MENU_DEPTH];
static uint8_t history_depth = 0;


// Цветовые константы для BGR-матрицы дисплея
#define COLOR_WHITE   255, 255, 255
#define COLOR_BLACK   0, 0, 0
#define COLOR_RED     255, 0, 0   // В BGR первый аргумент (R) зажжет КРАСНЫЙ
#define COLOR_GREY    180, 180, 180
#define COLOR_BG_LINE 220, 220, 220 // Серый цвет для выделенной строки

// Функция добавления текущего экрана в историю перед переходом вглубь
static void UI_PushHistory(void) {
    if (history_depth < MAX_MENU_DEPTH) {
        history[history_depth].menu = ui.current_menu;
        history[history_depth].cursor = ui.cursor;
        history_depth++;
    }
}

// Функция возврата назад по кнопке < Back
static void UI_PopHistory(void) {
    if (history_depth > 0) {
        history_depth--;
        ui.current_menu = history[history_depth].menu;
        ui.cursor = history[history_depth].cursor;
        ui.force_refresh = true;
    }
}

// Обработка вращения энкодера
void UI_ProcessNavigate(int8_t direction) {
    if (ui.mode == UI_MODE_NAVIGATE) {
        int8_t check_pos = ui.cursor;
        uint8_t size = ui.current_menu->size;
        
        for (uint8_t i = 0; i < size; i++) {
            check_pos += direction;
            if (check_pos >= size) check_pos = 0;
            if (check_pos < 0) check_pos = size - 1;
            
            // Пропускаем неактивные (серые) пункты меню
            if (ui.current_menu->items[check_pos].is_enabled) {
                ui.cursor = check_pos;
                return;
            }
        }
    } 
    else if (ui.mode == UI_MODE_EDIT) {
        // Режим изменения параметра в белом окошке
        MenuItem_t *item = &ui.current_menu->items[ui.cursor];
        int32_t val = *(item->load.int_param.val_ptr);
        val += direction * item->load.int_param.step;
        
        if (val < item->load.int_param.min) val = item->load.int_param.min;
        if (val > item->load.int_param.max) val = item->load.int_param.max;
        
        *(item->load.int_param.val_ptr) = val;
    }
}

// Обработка нажатия на кнопку энкодера
void UI_ProcessAction(void) {
    MenuItem_t *item = &ui.current_menu->items[ui.cursor];
    
    if (ui.mode == UI_MODE_NAVIGATE) {
        switch (item->type) {
            case ITEM_BACK:
                UI_PopHistory();
                break;
            case ITEM_SUBMENU:
                UI_PushHistory();
                ui.current_menu = item->load.next_menu;
                ui.cursor = 0; // Начинаем с верхнего пункта (< Back)
                ui.force_refresh = true;
                break;
            case ITEM_PARAM_INT:
                ui.mode = UI_MODE_EDIT; // Переходим в режим редактирования
                break;
            case ITEM_CUSTOM_PAGE:
                if (item->load.custom_init_cb) {
                    UI_PushHistory();
                    ui.mode = UI_MODE_CUSTOM;
                    item->load.custom_init_cb(); // Запуск LittleFS менеджера
                }
                break;
        }
    } 
    else if (ui.mode == UI_MODE_EDIT) {
        ui.mode = UI_MODE_NAVIGATE; // Фиксируем значение, выходим в навигацию
    }
}

void vGuiTask(void *pvParameters) 
{
    uint8_t last_cursor = 255;
    UiMode_t last_mode = UI_MODE_NAVIGATE;
    int32_t last_param_values[MENU_SIZE]; // Буфер для отслеживания изменений параметров
    
    while (1) {
        if (ui.mode == UI_MODE_CUSTOM) {
            // Если мы в режиме LittleFS, UI_Task засыпает, управление у кастомного экрана файлов
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        if (ui.force_refresh) {
            // Ручная заливка всего экрана в БЕЛЫЙ
            ucg_SetColor(&ucg, 0, COLOR_WHITE); 
            ucg_DrawBox(&ucg, 0, 0, ucg_GetWidth(&ucg), ucg_GetHeight(&ucg));

            // КРАСНАЯ рамка
            ucg_SetColor(&ucg, 0, COLOR_RED); 
            ucg_DrawFrame(&ucg, 4, 4, ucg_GetWidth(&ucg) - 8, ucg_GetHeight(&ucg) - 8);

            // ЧЕРНЫЙ заголовок меню
            ucg_SetColor(&ucg, 0, COLOR_BLACK); 
            ucg_DrawString(&ucg, 15, 35, 0, ui.current_menu->title);

            ui.force_refresh = false;
            last_cursor = 255; // Сброс, чтобы принудительно перерисовать строки
            for(int k=0; k<MENU_SIZE; k++) last_param_values[k] = -999999;
        }

        // Проверяем, нужно ли обновиться (сдвинулся курсор, изменился режим или значение параметра)
        bool need_redraw = (ui.cursor != last_cursor) || (ui.mode != last_mode);
        
        if (!need_redraw) {
            for (uint8_t i = 0; i < ui.current_menu->size; i++) {
                if (ui.current_menu->items[i].type == ITEM_PARAM_INT) {
                    int32_t current_val = *(ui.current_menu->items[i].load.int_param.val_ptr);
                    if (current_val != last_param_values[i]) {
                        need_redraw = true;
                        last_param_values[i] = current_val;
                    }
                }
            }
        }

        if (need_redraw) {
            uint16_t start_y = 65;
            uint16_t step_y = 22;

            for (uint8_t i = 0; i < ui.current_menu->size; i++) {
                MenuItem_t *item = &ui.current_menu->items[i];
                uint16_t row_y = start_y + (i * step_y);

                // 1. ОЧИСТКА СТРОКИ (Белый бокс)
                ucg_SetColor(&ucg, 0, COLOR_WHITE);
                ucg_DrawBox(&ucg, 12, row_y - 14, ucg_GetWidth(&ucg) - 24, step_y - 2);

                // 2. ПОДЛОЖКА СТРОКИ (Если курсор стоит тут)
                if (i == ui.cursor) {
                    ucg_SetColor(&ucg, 0, COLOR_BG_LINE); // Чуть серый фон для всей строки
                    ucg_DrawBox(&ucg, 12, row_y - 14, ucg_GetWidth(&ucg) - 24, step_y - 2);
                }

                // 3. ОТРИСОВКА НАЗВАНИЯ (Слева)
                if (!item->is_enabled) {
                    ucg_SetColor(&ucg, 0, COLOR_GREY); // Серый цвет текста для неактивных
                } else {
                    ucg_SetColor(&ucg, 0, COLOR_BLACK); // Обычный черный
                }
                ucg_DrawString(&ucg, 16, row_y, 0, item->name);

                // 4. ОТРИСОВКА ЗНАЧЕНИЯ (Справа, если это параметр)
                if (item->type == ITEM_PARAM_INT) {
                    char val_str[16];
                    sprintf(val_str, "%4d", *(item->load.int_param.val_ptr));

                    // Если мы РЕДАКТИРУЕМ именно эту строку, делаем окошко значения БЕЛЫМ
                    if (i == ui.cursor && ui.mode == UI_MODE_EDIT) {
                        ucg_SetColor(&ucg, 0, COLOR_WHITE); // Белое окошко
                        ucg_DrawBox(&ucg, ucg_GetWidth(&ucg) - 55, row_y - 14, 40, step_y - 2);
                    }

                    // Выводим само число
                    if (!item->is_enabled) ucg_SetColor(&ucg, 0, COLOR_GREY);
                    else ucg_SetColor(&ucg, 0, COLOR_BLACK);
                    
                    ucg_DrawString(&ucg, ucg_GetWidth(&ucg) - 50, row_y, 0, val_str);
                }
            }
            last_cursor = ui.cursor;
            last_mode = ui.mode;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void UI_Init()
{
    ucg_Init(&ucg, ucg_dev_st7735_18x128x160, ucg_ext_st7735_18, ucg_com_stm32_spi_cb);
    ucg_SetFontMode(&ucg, UCG_FONT_MODE_TRANSPARENT);
        // Подключаем ваш шрифт
    ucg_SetFont(&ucg, ucg_font_6x10);
}
