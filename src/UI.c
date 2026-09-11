#include "UI.h"
#include "DataBase.h"
#include <stdio.h>

extern int16_t ucg_com_stm32_spi_cb(ucg_t *ucg, int16_t msg, uint16_t arg, uint8_t *data);

static Menu_t main_menu;
static Menu_t settings_menu;

static MenuItem_t debug_menu_info[MENU_SIZE] =
{
    { .name = "< Back",       .type = ITEM_BACK,      .is_enabled = true },
    { .name = "Output 1", .type = ITEM_LABEL},
    { .name = "Channel 0",   .type = ITEM_PARAM_INT, .is_enabled = false,  .load.int_param = { .db_index = OUT_1_CH_0 } },
    { .name = "Channel 1",     .type = ITEM_PARAM_INT, .is_enabled = false,  .load.int_param = { .db_index = OUT_1_CH_1 } },
    { .name = "Target Ohm",   .type = ITEM_PARAM_INT, .is_enabled = false,  .load.int_param = { .db_index = OUT_1_OHM } },
    { .name = "Output 2", .type = ITEM_LABEL},
    { .name = "Channel 0",   .type = ITEM_PARAM_INT, .is_enabled = false,  .load.int_param = { .db_index = OUT_2_CH_0 } },
    { .name = "Channel 1",     .type = ITEM_PARAM_INT, .is_enabled = false,  .load.int_param = { .db_index = OUT_2_CH_1 } },
    { .name = "Target Ohm",   .type = ITEM_PARAM_INT, .is_enabled = false,  .load.int_param = { .db_index = OUT_2_OHM } }
};

static Menu_t debug_menu =
{
    .title = "DEBUG INFO:",
    .items = debug_menu_info,
    .size = MENU_SIZE
};

static MenuItem_t settings_items[MENU_SIZE] =
{
    { .name = "< Back",              .type = ITEM_BACK,        .is_enabled = true  },
    { .name = "Open debug info",     .type = ITEM_SUBMENU,     .is_enabled = true,  .load.next_menu = &debug_menu },
    { .name = "Main switch",         .type = ITEM_PARAM_INT,   .is_enabled = true,  .load.int_param = { .db_index = MAIN_SWITCH } },
    { .name = "Valve type",          .type = ITEM_PARAM_INT,   .is_enabled = true,  .load.int_param = { .db_index = VALVE_TYPE } },
    { .name = "Valve speed",         .type = ITEM_PARAM_FLOAT,   .is_enabled = true,  .load.int_param = { .db_index = VALVE_SPEED } },
    { .name = "Valve walk",          .type = ITEM_PARAM_FLOAT,   .is_enabled = true,  .load.int_param = { .db_index = VALVE_WALK } },
    { .name = "Valve open time",     .type = ITEM_PARAM_INT,   .is_enabled = false, .load.int_param = { .db_index = VALVE_OPEN_TIME } }
};

static Menu_t settings_menu =
{
    .title = "SETTINGS:",
    .items = settings_items,
    .size = MENU_SIZE
};

static MenuItem_t main_menu_items[MENU_SIZE] =
{
    { .name = "Open Settings",  .type = ITEM_SUBMENU,     .is_enabled = true,  .load.next_menu = &settings_menu },
    { .name = "Valve %",        .type = ITEM_PARAM_FLOAT, .is_enabled = false, .load.int_param = { .db_index = VALVE_PERCENT } },
    { .name = "Temp. output 1", .type = ITEM_PARAM_FLOAT, .is_enabled = false, .load.int_param = { .db_index = OUT_1_TEMP } },
    { .name = "Temp. output 2", .type = ITEM_PARAM_FLOAT, .is_enabled = false, .load.int_param = { .db_index = OUT_2_TEMP } },
    { .name = "TAU",            .type = ITEM_PARAM_INT,   .is_enabled = true, .load.int_param = { .db_index = CFG_TAU_P } },
    { .name = "T_PERT_PER",     .type = ITEM_PARAM_INT,   .is_enabled = true, .load.int_param = { .db_index = CFG_T_PERT_PER } },
    { .name = "T_RET_DELAY",    .type = ITEM_PARAM_INT,   .is_enabled = true, .load.int_param = { .db_index = CFG_T_RET_DELAY } },
    { .name = "K_p",            .type = ITEM_PARAM_FLOAT, .is_enabled = true, .load.int_param = { .db_index = CFG_K_P } },        // 30 -> "3.0"
    { .name = "Y0",             .type = ITEM_PARAM_FLOAT, .is_enabled = true, .load.int_param = { .db_index = CFG_Y0 } },         // 250 -> "25.0"
    { .name = "THETA_p",        .type = ITEM_PARAM_FLOAT, .is_enabled = true, .load.int_param = { .db_index = CFG_THETA_P } },   // 50 -> "5.0"
    { .name = "COEF_RET",       .type = ITEM_PARAM_FLOAT, .is_enabled = true, .load.int_param = { .db_index = CFG_COEF_RET } },  // 8 -> "0.8"
    { .name = "T_PERT_AMP",     .type = ITEM_PARAM_FLOAT, .is_enabled = true, .load.int_param = { .db_index = CFG_T_PERT_AMP } }
};

static Menu_t main_menu =
{
    .title = "MAIN MENU:",
    .items = main_menu_items,
    .size = MENU_SIZE
};

UiState_t ui =
{
    .mode = UI_MODE_NAVIGATE,
    .current_menu = &main_menu,
    .cursor = 0,
    .force_refresh = true,
    .temp_value = 0,
    .scroll_offset = 0
};

ucg_t ucg;

// Глобальная переменная для хранения полной записи во время редактирования
static DB_Value_t current_edit_value;

// Параметры отображения списка
#define UI_START_Y   38
#define UI_STEP_Y    14
static uint8_t visible_rows = 0; // будет вычислено в UI_Init

// СТек истории и состояния
static MenuHistory_t history[MAX_MENU_DEPTH];
static uint8_t history_depth = 0;

// Цветовые константы для BGR-матрицы дисплея
#define COLOR_WHITE   255, 255, 255
#define COLOR_BLACK   0, 0, 0
#define COLOR_RED     255, 0, 0
#define COLOR_GREY    70, 70, 70
#define COLOR_BG_LINE 220, 220, 220 // Серый цвет для выделенной строки

static void UI_UpdateScroll(void);

// Функция добавления текущего экрана в историю перед переходом вглубь
static void UI_PushHistory(void)
{
    if (history_depth < MAX_MENU_DEPTH)
    {
        history[history_depth].menu = ui.current_menu;
        history[history_depth].cursor = ui.cursor;
        history_depth++;
    }
}

// Функция возврата назад по кнопке < Back
static void UI_PopHistory(void)
{
    if (history_depth > 0)
    {
        history_depth--;
        ui.current_menu = history[history_depth].menu;
        ui.cursor = history[history_depth].cursor;
        UI_UpdateScroll();
        ui.force_refresh = true;
    }
}

// Функция обновления смещения прокрутки на основе текущего курсора
static void UI_UpdateScroll(void)
{
    if (visible_rows == 0) return;

    uint8_t max_offset = (ui.current_menu->size > visible_rows) ? (ui.current_menu->size - visible_rows) : 0;
    uint8_t new_offset = ui.scroll_offset;

    if (ui.cursor < new_offset)
    {
        new_offset = ui.cursor;
    } else if (ui.cursor >= new_offset + visible_rows)
    {
        new_offset = ui.cursor - visible_rows + 1;
    }

    if (new_offset > max_offset) new_offset = max_offset;

    if (new_offset != ui.scroll_offset)
    {
        ui.scroll_offset = new_offset;
        ui.force_refresh = true;
    }
}

void UI_ProcessNavigate(int8_t direction)
{
    if (ui.mode == UI_MODE_NAVIGATE)
    {
        int8_t check_pos = ui.cursor;
        uint8_t size = ui.current_menu->size;
        
        for (uint8_t i = 0; i < size; i++)
        {
            check_pos += direction;
            if (check_pos >= size) check_pos = 0;
            if (check_pos < 0) check_pos = size - 1;

            MenuItem_t *candidate = &ui.current_menu->items[check_pos];
            if (candidate->name == NULL) continue;
            if (candidate->type == ITEM_LABEL) continue;   // только их пропускаем

            // Всегда перемещаемся на найденный элемент (даже если он неактивен)
            ui.cursor = check_pos;
            UI_UpdateScroll();
            return;
        }
    } 
    else if (ui.mode == UI_MODE_EDIT)
    {
        ui.temp_value += direction * current_edit_value.step;
        if (ui.temp_value < current_edit_value.min) ui.temp_value = current_edit_value.min;
        if (ui.temp_value > current_edit_value.max) ui.temp_value = current_edit_value.max;
    }
}

// Обработка нажатия на кнопку энкодера
void UI_ProcessAction(void)
{
    MenuItem_t *item = &ui.current_menu->items[ui.cursor];
    
    if (ui.mode == UI_MODE_NAVIGATE)
    {
        switch (item->type)
        {
            case ITEM_BACK:
                UI_PopHistory();
                break;
            case ITEM_SUBMENU:
                UI_PushHistory();
                ui.current_menu = item->load.next_menu;
                ui.cursor = 0;
                UI_UpdateScroll();
                ui.force_refresh = true;
                break;
            case ITEM_PARAM_FLOAT:   // === ADDED FOR FLOAT ===
            case ITEM_PARAM_INT: {
                DB_Value_t value;
                // Для FLOAT и INT используем db_index из int_param (совпадает по смещению)
                if (DB_Select(item->load.int_param.db_index, &value))
                {
                    if (!value.is_enabled) {
                        // Неактивный параметр – игнорируем нажатие
                        break;
                    }
                    current_edit_value = value;
                    ui.temp_value = value.raw_data;
                }
                else
                {
                    // fallback, если чтение не удалось
                    current_edit_value = (DB_Value_t){ .is_readable = true, .save_to_flash = true,
                                                       .raw_data = 0, .type = 0x0, .min = 0,
                                                       .max = 0, .step = 1 };
                    ui.temp_value = current_edit_value.raw_data;
                }
                ui.mode = UI_MODE_EDIT;
                break;
            }
            case ITEM_CUSTOM_PAGE:
                if (item->load.custom_init_cb)
                {
                    UI_PushHistory();
                    ui.mode = UI_MODE_CUSTOM;
                    item->load.custom_init_cb();
                }
                break;
            default:
                break;
        }
    } 
    else if (ui.mode == UI_MODE_EDIT)
    {
        current_edit_value.raw_data = ui.temp_value;
        if (DB_Insert(item->load.int_param.db_index, current_edit_value))
        {
        }
        else
        {
        }
        ui.mode = UI_MODE_NAVIGATE;
    }
}

void vGuiTask(void *pvParameters) 
{
    uint8_t last_cursor = 255;
    UiMode_t last_mode = UI_MODE_NAVIGATE;
    uint8_t last_scroll_offset = 255;
    int32_t last_param_values[MENU_SIZE]; 
    static char val_str[16]; 
    
    for(int k = 0; k < MENU_SIZE; k++) last_param_values[k] = -999999;
    
    vTaskDelay(pdMS_TO_TICKS(100));

    while (1)
    {
        if (ui.mode == UI_MODE_CUSTOM)
        {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        bool is_forced = false;

        if (ui.force_refresh)
        {
            is_forced = true;
            ui.force_refresh = false;

            ucg_SetColor(&ucg, 0, COLOR_WHITE); 
            ucg_DrawBox(&ucg, 0, 0, ucg_GetWidth(&ucg), ucg_GetHeight(&ucg));

            ucg_SetColor(&ucg, 0, COLOR_RED); 
            ucg_DrawFrame(&ucg, 4, 4, ucg_GetWidth(&ucg) - 8, ucg_GetHeight(&ucg) - 8);

            ucg_SetColor(&ucg, 0, COLOR_BLACK); 
            ucg_DrawString(&ucg, 16, 20, 0, ui.current_menu->title);

            last_cursor = 255; 
            last_mode = UI_MODE_NAVIGATE;
            last_scroll_offset = 255;
            for(int k = 0; k < MENU_SIZE; k++) last_param_values[k] = -999999;
        }

        vTaskSuspendAll();
        uint8_t current_ui_cursor = ui.cursor;
        UiMode_t current_ui_mode = ui.mode;
        uint8_t current_scroll_offset = ui.scroll_offset;
        xTaskResumeAll();

        bool scroll_changed = (current_scroll_offset != last_scroll_offset);
        bool need_redraw = (current_ui_cursor != last_cursor) || 
                           (current_ui_mode != last_mode) || 
                           scroll_changed ||
                           is_forced;
        
        bool param_changed[MENU_SIZE] = { false };

        // Проверка изменения значений для INT и FLOAT
        for (uint8_t i = 0; i < ui.current_menu->size; i++)
        {
            uint8_t item_type = ui.current_menu->items[i].type;
            if (item_type == ITEM_PARAM_INT || item_type == ITEM_PARAM_FLOAT) // === ADDED FLOAT ===
            {
                if (current_ui_mode == UI_MODE_EDIT && i == current_ui_cursor)
                {
                    if (ui.temp_value != last_param_values[i])
                    {
                        need_redraw = true;
                        param_changed[i] = true;
                        last_param_values[i] = ui.temp_value;
                    }
                }
                else
                {
                    DB_Value_t value;
                    if (DB_Select(ui.current_menu->items[i].load.int_param.db_index, &value))
                    {
                        if (value.raw_data != last_param_values[i])
                        {
                            need_redraw = true;
                            param_changed[i] = true;
                            last_param_values[i] = value.raw_data;
                        }
                    }
                    else
                    {
                        if (last_param_values[i] != -999999)
                        {
                            need_redraw = true;
                            param_changed[i] = true;
                            last_param_values[i] = ui.current_menu->items[i].load.int_param.min;
                        }
                    }
                }
            }
        }

        if (need_redraw)
        {
            uint8_t visible_start = current_scroll_offset;
            uint8_t visible_end = current_scroll_offset + visible_rows;
            if (visible_end > ui.current_menu->size)
            {
                visible_end = ui.current_menu->size;
            }

            bool is_first_render = (last_cursor == 255) || scroll_changed;

            for (uint8_t i = visible_start; i < visible_end; i++)
            {
                bool row_changed = is_first_render ||
                                   (i == current_ui_cursor) || 
                                   (i == last_cursor) || 
                                   param_changed[i];

                if (row_changed)
                {
                    vTaskSuspendAll();
                    MenuItem_t item = ui.current_menu->items[i];
                    xTaskResumeAll();

                    if (item.name == NULL)
                    {
                        uint16_t row_y = UI_START_Y + (i - visible_start) * UI_STEP_Y;
                        ucg_SetColor(&ucg, 0, COLOR_WHITE);
                        ucg_DrawBox(&ucg, 12, row_y - 9, ucg_GetWidth(&ucg) - 24, 13);
                        continue;
                    }

                    uint16_t row_y = UI_START_Y + (i - visible_start) * UI_STEP_Y;

                    if (item.type == ITEM_LABEL)
                    {
                        ucg_SetColor(&ucg, 0, COLOR_WHITE);
                        ucg_DrawBox(&ucg, 12, row_y - 9, ucg_GetWidth(&ucg) - 24, 13);
                        ucg_SetColor(&ucg, 0, COLOR_GREY);
                        ucg_DrawString(&ucg, 16, row_y, 0, item.name);
                        continue;
                    }

                    bool item_enabled = item.is_enabled;
                    // Для INT и FLOAT проверяем is_enabled из БД (дополнительно)
                    if (item.type == ITEM_PARAM_INT || item.type == ITEM_PARAM_FLOAT) // === ADDED FLOAT ===
                    {
                        DB_Value_t db_val;
                        if (DB_Select(item.load.int_param.db_index, &db_val))
                        {
                            item_enabled = db_val.is_enabled;
                        }
                        else
                        {
                            item_enabled = false;
                        }
                    }

                    if (i == current_ui_cursor && item_enabled)
                    {
                        ucg_SetColor(&ucg, 0, COLOR_BG_LINE);
                    }
                    else
                    {
                        ucg_SetColor(&ucg, 0, COLOR_WHITE);
                    }
                    ucg_DrawBox(&ucg, 12, row_y - 9, ucg_GetWidth(&ucg) - 24, 13);

                    if (!item_enabled)
                    {
                        ucg_SetColor(&ucg, 0, COLOR_GREY);
                    }
                    else
                    {
                        ucg_SetColor(&ucg, 0, COLOR_BLACK);
                    }
                    ucg_DrawString(&ucg, 16, row_y, 0, item.name);

                    if (item.type == ITEM_PARAM_INT)
                    {
                        int32_t display_value;
                        if (i == current_ui_cursor && current_ui_mode == UI_MODE_EDIT)
                            display_value = ui.temp_value;
                        else
                        {
                            DB_Value_t value;
                            if (DB_Select(item.load.int_param.db_index, &value))
                                display_value = value.raw_data;
                            else
                                display_value = item.load.int_param.min;
                        }
                        sprintf(val_str, "%4d", display_value);

                        // Подсветка поля редактирования
                        if (i == current_ui_cursor && current_ui_mode == UI_MODE_EDIT && item_enabled)
                        {
                            ucg_SetColor(&ucg, 0, COLOR_WHITE);
                            ucg_DrawBox(&ucg, ucg_GetWidth(&ucg) - 44, row_y - 9, 32, 13); // ширина 32 для 4 символов
                        }

                        if (!item_enabled) ucg_SetColor(&ucg, 0, COLOR_GREY);
                        else ucg_SetColor(&ucg, 0, COLOR_BLACK);
                        ucg_DrawString(&ucg, ucg_GetWidth(&ucg) - 42, row_y, 0, val_str);
                    }
                    else if (item.type == ITEM_PARAM_FLOAT)
                    {
                        int32_t display_raw;
                        if (i == current_ui_cursor && current_ui_mode == UI_MODE_EDIT)
                        {
                            display_raw = ui.temp_value;
                        }
                        else
                        {
                            DB_Value_t value;
                            if (DB_Select(item.load.int_param.db_index, &value))
                            {
                                display_raw = value.raw_data;
                            }
                            else
                            {
                                display_raw = item.load.int_param.min;
                            }
                        }

                        // Преобразование raw (целое * 10) в строку с одной десятичной
                        int int_part = display_raw / 10;
                        int frac_part = display_raw % 10;
                        if (frac_part < 0) frac_part = -frac_part;
                        sprintf(val_str, "%4d.%d", int_part, frac_part);

                        // Подсветка поля редактирования (только в режиме EDIT)
                        if (i == current_ui_cursor && current_ui_mode == UI_MODE_EDIT && item_enabled)
                        {
                            ucg_SetColor(&ucg, 0, COLOR_WHITE);
                            ucg_DrawBox(&ucg, ucg_GetWidth(&ucg) - 56, row_y - 9, 48, 13); // Сдвиг левее
                        }

                        // Цвет текста
                        if (!item_enabled) ucg_SetColor(&ucg, 0, COLOR_GREY);
                        else ucg_SetColor(&ucg, 0, COLOR_BLACK);
                        
                        ucg_DrawString(&ucg, ucg_GetWidth(&ucg) - 54, row_y, 0, val_str); // Сдвиг левее
                    }
                }
            }

            if (visible_end < current_scroll_offset + visible_rows)
            {
                for (uint8_t i = visible_end; i < current_scroll_offset + visible_rows; i++)
                {
                    uint16_t row_y = UI_START_Y + (i - current_scroll_offset) * UI_STEP_Y;
                    ucg_SetColor(&ucg, 0, COLOR_WHITE);
                    ucg_DrawBox(&ucg, 12, row_y - 9, ucg_GetWidth(&ucg) - 24, 13);
                }
            }

            vTaskSuspendAll();
            last_cursor = current_ui_cursor;
            last_mode = current_ui_mode;
            last_scroll_offset = current_scroll_offset;
            xTaskResumeAll();
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void UI_Init()
{
    ucg_Init(&ucg, ucg_dev_st7735_18x128x160, ucg_ext_st7735_18, ucg_com_stm32_spi_cb);
    ucg_SetRotate90(&ucg);
    ucg_SetFontMode(&ucg, UCG_FONT_MODE_TRANSPARENT);
    ucg_SetFont(&ucg, ucg_font_6x10);

    visible_rows = (ucg_GetHeight(&ucg) - UI_START_Y) / UI_STEP_Y;
    if (visible_rows > MENU_SIZE) visible_rows = MENU_SIZE;
}