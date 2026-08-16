#include "UI.h"
#include "DataBase.h"
#include <stdio.h>

// Реальные переменные для параметров меню (в будущем они могут уйти в DB.c)
static int32_t param_brightness = 50;
static int32_t param_volume     = 20;
static int32_t param_contrast   = 80;

extern int16_t ucg_com_stm32_spi_cb(ucg_t *ucg, int16_t msg, uint16_t arg, uint8_t *data);

// Объявляем меню заранее, чтобы они могли ссылаться друг на друга
static Menu_t main_menu;
static Menu_t settings_menu;

#define IDX_0 0
#define IDX_1 1
#define IDX_2 2
#define IDX_3 3

// --- 1. ПУНКТЫ И СТРУКТУРА МЕНЮ НАСТРОЕК (ВЛОЖЕННОЕ) ---
static MenuItem_t settings_items[MENU_SIZE] = {
    { .name = "< Back",       .type = ITEM_BACK,      .is_enabled = true },
    { .name = "Main switch",  .type = ITEM_PARAM_INT, .is_enabled = true,  .load.int_param = { IDX_3, 0, 1, 1 } }
};

static Menu_t settings_menu = {
    .title = "SETTINGS:",
    .items = settings_items,
    .size = MENU_SIZE
};

// --- 2. ПУНКТЫ И СТРУКТУРА ГЛАВНОГО МЕНЮ (КОРНЕВОГО) ---
static MenuItem_t main_menu_items[MENU_SIZE] = {
    { .name = "Open Settings", .type = ITEM_SUBMENU,   .is_enabled = true,  .load.next_menu = &settings_menu },
    { .name = "Channel 0",   .type = ITEM_PARAM_INT, .is_enabled = true,  .load.int_param = { IDX_0, 0, 255, 1 } },
    { .name = "Channel 1",     .type = ITEM_PARAM_INT, .is_enabled = true,  .load.int_param = { IDX_1, 0, 255, 1 } },
    { .name = "Target Ohm",   .type = ITEM_PARAM_INT, .is_enabled = true,  .load.int_param = { IDX_2, 50, 500, 1 } }
};

static Menu_t main_menu = {
    .title = "MAIN MENU:",
    .items = main_menu_items,
    .size = MENU_SIZE
};

UiState_t ui = {
    .mode = UI_MODE_NAVIGATE,
    .current_menu = &main_menu,
    .cursor = 0,
    .force_refresh = true,
    .temp_value = 0,
    .scroll_offset = 0   // новое поле
};

ucg_t ucg;

// Глобальная статическая переменная для хранения полной записи во время редактирования
static DB_Value_t current_edit_value;

// Параметры отображения списка
#define UI_START_Y   38
#define UI_STEP_Y    14
static uint8_t visible_rows = 0; // будет вычислено в UI_Init

// --- СТЕК ИСТОРИИ И СОСТОЯНИЕ ---
static MenuHistory_t history[MAX_MENU_DEPTH];
static uint8_t history_depth = 0;

// Цветовые константы для BGR-матрицы дисплея
#define COLOR_WHITE   255, 255, 255
#define COLOR_BLACK   0, 0, 0
#define COLOR_RED     255, 0, 0   // В BGR первый аргумент (R) зажжет КРАСНЫЙ
#define COLOR_GREY    180, 180, 180
#define COLOR_BG_LINE 220, 220, 220 // Серый цвет для выделенной строки

// Прототип функции обновления прокрутки
static void UI_UpdateScroll(void);

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
        UI_UpdateScroll();  // обновляем прокрутку после восстановления курсора
        ui.force_refresh = true;
    }
}

// Функция обновления смещения прокрутки на основе текущего курсора
static void UI_UpdateScroll(void) {
    if (visible_rows == 0) return; // если ещё не инициализировано
    uint8_t max_offset = (ui.current_menu->size > visible_rows) ? (ui.current_menu->size - visible_rows) : 0;
    uint8_t new_offset = ui.scroll_offset;
    if (ui.cursor < new_offset) {
        new_offset = ui.cursor;
    } else if (ui.cursor >= new_offset + visible_rows) {
        new_offset = ui.cursor - visible_rows + 1;
    }
    if (new_offset > max_offset) new_offset = max_offset;
    if (new_offset != ui.scroll_offset) {
        ui.scroll_offset = new_offset;
        ui.force_refresh = true; // при изменении окна делаем полную перерисовку
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
            if (ui.current_menu->items[check_pos].is_enabled && ui.current_menu->items[check_pos].name != NULL) {
                ui.cursor = check_pos;
                UI_UpdateScroll();
                return;
            }
        }
    } 
    else if (ui.mode == UI_MODE_EDIT) {
        MenuItem_t *item = &ui.current_menu->items[ui.cursor];
        
        // Изменяем временное значение (не пишем в БД!)
        ui.temp_value += direction * item->load.int_param.step;
        
        // Проверка границ
        if (ui.temp_value < item->load.int_param.min) ui.temp_value = item->load.int_param.min;
        if (ui.temp_value > item->load.int_param.max) ui.temp_value = item->load.int_param.max;
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
                ui.cursor = 0;
                UI_UpdateScroll();  // обновляем прокрутку после смены меню
                ui.force_refresh = true;
                break;
            case ITEM_PARAM_INT: {
                // Читаем полную запись из БД
                DB_Value_t value;
                if (DB_Select(item->load.int_param.db_index, &value)) {
                    current_edit_value = value;           // сохраняем все флаги
                    ui.temp_value = value.raw_data;        // берём текущее значение
                } else {
                    // Если записи нет — создаём структуру по умолчанию
                    current_edit_value = (DB_Value_t){
                        .is_readable = true,
                        .save_to_flash = true,
                        .raw_data = item->load.int_param.min,
                        .type = 0x0
                    };
                    ui.temp_value = current_edit_value.raw_data;
                }
                ui.mode = UI_MODE_EDIT;
                break;
            }
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
        // Выходим из режима редактирования, сохраняем temp_value в БД
        current_edit_value.raw_data = ui.temp_value;   // обновляем только значение
        if (DB_Insert(item->load.int_param.db_index, current_edit_value)) {
            // Успешно сохранено
        } else {
            // Ошибка записи
        }
        ui.mode = UI_MODE_NAVIGATE;
    }
}

void vGuiTask(void *pvParameters) 
{
    uint8_t last_cursor = 255;
    UiMode_t last_mode = UI_MODE_NAVIGATE;
    uint8_t last_scroll_offset = 255; // чтобы первый раз сработало обновление
    int32_t last_param_values[MENU_SIZE]; 
    
    // Буфер в статической памяти для защиты стека FreeRTOS
    static char val_str[16]; 
    
    // Инициализируем кэш параметров стартовыми значениями
    for(int k = 0; k < MENU_SIZE; k++) last_param_values[k] = -999999;
    
    vTaskDelay(pdMS_TO_TICKS(100));

    while (1) {
        if (ui.mode == UI_MODE_CUSTOM) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        // Локальный флаг, чтобы не потерять событие принудительной очистки экрана
        bool is_forced = false;

        if (ui.force_refresh) {
            is_forced = true;
            ui.force_refresh = false; // Сбрасываем сразу, событие сохранено в is_forced

            // 1. Ручная заливка фона в БЕЛЫЙ
            ucg_SetColor(&ucg, 0, COLOR_WHITE); 
            ucg_DrawBox(&ucg, 0, 0, ucg_GetWidth(&ucg), ucg_GetHeight(&ucg));

            // 2. Рисуем КРАСНУЮ рамку
            ucg_SetColor(&ucg, 0, COLOR_RED); 
            ucg_DrawFrame(&ucg, 4, 4, ucg_GetWidth(&ucg) - 8, ucg_GetHeight(&ucg) - 8);

            // 3. Пишем заголовок ЧЁРНЫМ
            ucg_SetColor(&ucg, 0, COLOR_BLACK); 
            ucg_DrawString(&ucg, 16, 20, 0, ui.current_menu->title);

            last_cursor = 255; 
            last_mode = UI_MODE_NAVIGATE;
            last_scroll_offset = 255; // форсируем перерисовку всех строк
            for(int k = 0; k < MENU_SIZE; k++) last_param_values[k] = -999999;
        }

        // Защищаем проверку изменений от вмешательства энкодера
        vTaskSuspendAll();
        uint8_t current_ui_cursor = ui.cursor;
        UiMode_t current_ui_mode = ui.mode;
        uint8_t current_scroll_offset = ui.scroll_offset;
        xTaskResumeAll();

        // Экран требует отрисовки если: сменился курсор, сменился режим, сменился скролл или была команда force_refresh
        bool scroll_changed = (current_scroll_offset != last_scroll_offset);
        bool need_redraw = (current_ui_cursor != last_cursor) || 
                           (current_ui_mode != last_mode) || 
                           scroll_changed ||
                           is_forced;
        
        // Массив измененных параметров для точечного обновления экрана
        bool param_changed[MENU_SIZE] = { false };

        // ВСЕГДА опрашиваем базу данных для обновления кэша параметров
        for (uint8_t i = 0; i < ui.current_menu->size; i++) {
            if (ui.current_menu->items[i].type == ITEM_PARAM_INT) {
                if (current_ui_mode == UI_MODE_EDIT && i == current_ui_cursor) {
                    if (ui.temp_value != last_param_values[i]) {
                        need_redraw = true;
                        param_changed[i] = true;
                        last_param_values[i] = ui.temp_value;
                    }
                } else {
                    DB_Value_t value;
                    if (DB_Select(ui.current_menu->items[i].load.int_param.db_index, &value)) {
                        if (value.raw_data != last_param_values[i]) {
                            need_redraw = true;
                            param_changed[i] = true;
                            last_param_values[i] = value.raw_data;
                        }
                    } else {
                        // Если не удалось прочитать, считаем значение минимальным
                        if (last_param_values[i] != -999999) {
                            need_redraw = true;
                            param_changed[i] = true;
                            last_param_values[i] = ui.current_menu->items[i].load.int_param.min;
                        }
                    }
                }
            }
        }

        if (need_redraw) {
            // Определяем видимый диапазон
            uint8_t visible_start = current_scroll_offset;
            uint8_t visible_end = current_scroll_offset + visible_rows;
            if (visible_end > ui.current_menu->size) {
                visible_end = ui.current_menu->size;
            }

            // Флаг первой отрисовки после force_refresh
            bool is_first_render = (last_cursor == 255) || scroll_changed; // если скролл изменился, считаем все строки изменёнными

            for (uint8_t i = visible_start; i < visible_end; i++) {
                // Строка должна обновиться, если:
                // - это первый рендер (после force_refresh или смены скролла)
                // - строка является текущим курсором
                // - строка была предыдущим курсором
                // - значение параметра этой строки изменилось
                bool row_changed = is_first_render ||
                                   (i == current_ui_cursor) || 
                                   (i == last_cursor) || 
                                   param_changed[i];

                if (row_changed) {
                        vTaskSuspendAll();
                        MenuItem_t item = ui.current_menu->items[i];
                        xTaskResumeAll();
                        
                        if (item.name == NULL) {
                            // Очищаем строку на всякий случай, чтобы не осталось артефактов
                            uint16_t row_y = UI_START_Y + (i - visible_start) * UI_STEP_Y;
                            ucg_SetColor(&ucg, 0, COLOR_WHITE);
                            ucg_DrawBox(&ucg, 12, row_y - 9, ucg_GetWidth(&ucg) - 24, 13);
                            continue;
                        }

                        uint16_t row_y = UI_START_Y + (i - visible_start) * UI_STEP_Y;

                        // --- ОЧИСТКА / СТИРАНИЕ СТРОКИ ---
                        if (i == current_ui_cursor) {
                            ucg_SetColor(&ucg, 0, COLOR_BG_LINE);
                        } else {
                            ucg_SetColor(&ucg, 0, COLOR_WHITE);
                        }
                        ucg_DrawBox(&ucg, 12, row_y - 9, ucg_GetWidth(&ucg) - 24, 13);

                        // --- ОТРИСОВКА НАЗВАНИЯ ---
                        if (!item.is_enabled) {
                            ucg_SetColor(&ucg, 0, COLOR_GREY);
                        } else {
                            ucg_SetColor(&ucg, 0, COLOR_BLACK);
                        }
                        ucg_DrawString(&ucg, 16, row_y, 0, item.name);
                    // --- ОТРИСОВКА ЗНАЧЕНИЯ ---
                    if (item.type == ITEM_PARAM_INT)
                    {
                        int32_t display_value;
                        if (i == current_ui_cursor && current_ui_mode == UI_MODE_EDIT) {
                            display_value = ui.temp_value;
                        } else {
                            DB_Value_t value;
                            if (DB_Select(ui.current_menu->items[i].load.int_param.db_index, &value)) {
                                display_value = value.raw_data;
                            } else {
                                display_value = ui.current_menu->items[i].load.int_param.min;
                            }
                        }

                        sprintf(val_str, "%4d", display_value);

                        if (i == current_ui_cursor && current_ui_mode == UI_MODE_EDIT) {
                            ucg_SetColor(&ucg, 0, COLOR_WHITE);
                            ucg_DrawBox(&ucg, ucg_GetWidth(&ucg) - 44, row_y - 9, 28, 13);
                        }

                        if (!item.is_enabled) ucg_SetColor(&ucg, 0, COLOR_GREY);
                        else ucg_SetColor(&ucg, 0, COLOR_BLACK);
                        
                        ucg_DrawString(&ucg, ucg_GetWidth(&ucg) - 42, row_y, 0, val_str);
                    }
                }
            }

            // Если элементов меньше, чем видимых строк, очищаем оставшиеся строки
            if (visible_end < current_scroll_offset + visible_rows) {
                for (uint8_t i = visible_end; i < current_scroll_offset + visible_rows; i++) {
                    uint16_t row_y = UI_START_Y + (i - current_scroll_offset) * UI_STEP_Y;
                    ucg_SetColor(&ucg, 0, COLOR_WHITE);
                    ucg_DrawBox(&ucg, 12, row_y - 9, ucg_GetWidth(&ucg) - 24, 13);
                }
            }

            // Фиксируем состояние для следующего кадра
            vTaskSuspendAll();
            last_cursor = current_ui_cursor;
            last_mode = current_ui_mode;
            last_scroll_offset = current_scroll_offset;
            xTaskResumeAll();
        }

        vTaskDelay(pdMS_TO_TICKS(20)); // Частота опроса графики
    }
}

void UI_Init()
{
    ucg_Init(&ucg, ucg_dev_st7735_18x128x160, ucg_ext_st7735_18, ucg_com_stm32_spi_cb);
    ucg_SetFontMode(&ucg, UCG_FONT_MODE_TRANSPARENT);
    ucg_SetFont(&ucg, ucg_font_6x10);
    
    // Вычисляем количество видимых строк
    visible_rows = (ucg_GetHeight(&ucg) - UI_START_Y) / UI_STEP_Y;
    if (visible_rows > MENU_SIZE) visible_rows = MENU_SIZE;
}