#include "MainLogic.h"
#include "DigitResistor.h"
#include "AD8402_driver.h"
#include "database.h"
#include "FreeRTOS.h"
#include "task.h"

#define MAX_DELAY_STEPS  200    // Максимальный размер кольцевого буфера задержки
#define DT               0.1f

extern float calibrate[POT_STEPS_COUNT];

// Вспомогательная функция для ручного режима (теперь без передачи указателя на макрос)
static void HandleManualPot( DualDigitalRes* pot
                           , uint8_t ohm_key, uint8_t ch0_key, uint8_t ch1_key
                           , int32_t* last_target, int32_t* last_ch0, int32_t* last_ch1
                           , uint8_t pot_index ) // Передаем индекс 1 или 2 вместо функции
{
    DB_Value_t target, ch0, ch1;
    if (!DB_Select(ohm_key, &target)) target.raw_data = 50;
    if (!DB_Select(ch0_key,  &ch0))    ch0.raw_data = 0;
    if (!DB_Select(ch1_key,  &ch1))    ch1.raw_data = 0;

    // Сценарий А: Изменилась целевая уставка в Омах -> Считаем шаги
    if (target.raw_data != *last_target)
    {
        uint32_t step0, step1;
        FindOptimalSteps(pot, (float)target.raw_data, &step0, &step1);
        
        *last_ch0 = step0;
        *last_ch1 = step1;
        *last_target = target.raw_data;

        DB_Value_t new0 = ch0; new0.raw_data = step0; DB_Insert(ch0_key, new0);
        DB_Value_t new1 = ch1; new1.raw_data = step1; DB_Insert(ch1_key, new1);
        
        if (pot_index == 1) {
            POT1_WRITE(0, step0);
            POT1_WRITE(1, step1);
        } else {
            POT2_WRITE(0, step0);
            POT2_WRITE(1, step1);
        }
    }
    // Сценарий Б: Изменились шаги вручную -> Считаем Омы напрямую
    else if (ch0.raw_data != *last_ch0 || ch1.raw_data != *last_ch1)
    {
        pot->channel0_step = ch0.raw_data;
        pot->channel1_step = ch1.raw_data;

        float current_ohm = ParallelOhm(pot);
        uint32_t rounded_ohm = (uint32_t)(current_ohm + 0.5f);

        *last_ch0 = ch0.raw_data;
        *last_ch1 = ch1.raw_data;
        *last_target = rounded_ohm;

        DB_Value_t new_target = target; 
        new_target.raw_data = rounded_ohm; 
        DB_Insert(ohm_key, new_target);

        if (pot_index == 1) {
            POT1_WRITE(0, ch0.raw_data);
            POT1_WRITE(1, ch1.raw_data);
        } else {
            POT2_WRITE(0, ch0.raw_data);
            POT2_WRITE(1, ch1.raw_data);
        }
    }
}

// ====================================================================
// ОБРАБОТЧИКИ РЕЖИМОВ
// ====================================================================

static void ProcessManualMode( DualDigitalRes* pot1, DualDigitalRes* pot2
                             , int32_t* lt1, int32_t* lc1_0, int32_t* lc1_1
                             , int32_t* lt2, int32_t* lc2_0, int32_t* lc2_1 )
{
    // Вместо макросов передаем просто идентификатор потенциометра: 1 или 2
    HandleManualPot(pot1, OUT_1_OHM, OUT_1_CH_0, OUT_1_CH_1, lt1, lc1_0, lc1_1, 1);
    HandleManualPot(pot2, OUT_2_OHM, OUT_2_CH_0, OUT_2_CH_1, lt2, lc2_0, lc2_1, 2);
}

uint32_t Convert_Temperature_To_Ohms(float temperature, uint8_t pot_number) {
    float full_ohms = 1000.0f + (3.8505f * temperature);

    float pot_ohms = full_ohms - 1000.0f;

    if (pot_ohms < 50.0f)  pot_ohms = 50.0f;
    if (pot_ohms > 500.0f) pot_ohms = 500.0f;

    // 4. Округляем до ближайшего целого Ома
    return (uint32_t)(pot_ohms + 0.5f);
}

#define M_TWO_PI          6.28318530f
#define THETA_FILTER_TAU  2.0f   // Время сглаживания изменения уставки задержки, сек

static void ProcessDiffEqMode(DualDigitalRes* pot1, DualDigitalRes* pot2, bool reset_state)
{
    static float Y_current = 0.0f;
    static float Y_ret_avg = 0.0f;
    static float U_buffer[MAX_DELAY_STEPS];
    static uint32_t buffer_index = 0;
    static bool is_initialized = false;
    
    // Состояния фильтров и фазы
    static float pert_phase = 0.0f; 
    static float current_theta_p = -1.0f; 

    // Оптимизация: кэшируем коэффициенты фильтров, чтобы не вызывать expf() каждый такт
    static float alpha_theta = 0.0f;
    static float alpha_ret = 0.0f;
    static float last_t_ret_delay = -1.0f;

    static int32_t last_applied_ohm1 = -1;
    static int32_t last_applied_ohm2 = -1;

    if (reset_state)
    {
        is_initialized = false;
        last_applied_ohm1 = -1;
        last_applied_ohm2 = -1;
        pert_phase = 0.0f;
        current_theta_p = -1.0f;
        last_t_ret_delay = -1.0f;
    }

    DB_Value_t db_tau, db_kp, db_y0, db_theta, db_valve, db_coef, db_ret_delay;
    DB_Value_t db_pert_amp, db_pert_per;
    
    float tau_p       = DB_Select(CFG_TAU_P,       &db_tau)       ? (float)db_tau.raw_data        : 20.0f;
    float t_ret_delay = DB_Select(CFG_T_RET_DELAY, &db_ret_delay) ? (float)db_ret_delay.raw_data   : 10.0f;
    float t_pert      = DB_Select(CFG_T_PERT_PER,  &db_pert_per)  ? (float)db_pert_per.raw_data    : 60.0f;

    float K_p         = DB_Select(CFG_K_P,         &db_kp)        ? (float)db_kp.raw_data / 10.0f  : 3.0f;
    float Y0          = DB_Select(CFG_Y0,          &db_y0)        ? (float)db_y0.raw_data / 10.0f  : 25.0f;
    
    // Внимание: масштабы шкал разные (секунды уставки делятся на 10, тау_п идет как есть)
    float target_theta= DB_Select(CFG_THETA_P,     &db_theta)     ? (float)db_theta.raw_data / 10.0f : 5.0f;
    
    float T_pert      = DB_Select(CFG_T_PERT_AMP,  &db_pert_amp)  ? (float)db_pert_amp.raw_data / 10.0f : 2.5f;
    float coef_ret    = DB_Select(CFG_COEF_RET,    &db_coef)      ? (float)db_coef.raw_data / 10.0f : 0.8f;
    float U_in        = DB_Select(VALVE_PERCENT,   &db_valve)     ? (float)db_valve.raw_data / 1000.0f : 0.01f;

    if (tau_p <= 0.0f) tau_p = 1.0f;
    if (t_ret_delay <= 0.0f) t_ret_delay = 1.0f;
    if (t_pert <= 0.0f) t_pert = 1.0f;

    if (!is_initialized)
    {
        Y_current = Y0;
        Y_ret_avg = Y0;
        buffer_index = 0;
        pert_phase = 0.0f;
        current_theta_p = target_theta;
        
        // Тяжелые вычисления выполняем только при инициализации
        alpha_theta = 1.0f - expf(-DT / THETA_FILTER_TAU);
        alpha_ret = 1.0f - expf(-DT / t_ret_delay);
        last_t_ret_delay = t_ret_delay;

        // ИСПРАВЛЕНО: Инициализируем буфер чистым нулем (холодная труба до старта)
        for (uint32_t i = 0; i < MAX_DELAY_STEPS; i++)
        {
            U_buffer[i] = 0.0f; 
        }
        is_initialized = true;
    }
    else if (t_ret_delay != last_t_ret_delay)
    {
        // Пересчитываем альфу фильтра обратки только если уставка изменилась в БД
        alpha_ret = 1.0f - expf(-DT / t_ret_delay);
        last_t_ret_delay = t_ret_delay;
    }

    // Плавное изменение текущего транспортного запаздывания
    current_theta_p = (alpha_theta * target_theta) + ((1.0f - alpha_theta) * current_theta_p);

    // 1. Безопасное ограничение уставки сверху ДО расчетов индексов (Защита буфера)
    float max_allowed_theta = (float)(MAX_DELAY_STEPS - 2) * DT;
    if (current_theta_p > max_allowed_theta) current_theta_p = max_allowed_theta;
    if (current_theta_p < 0.0f) current_theta_p = 0.0f;

    // 2. Запись текущего управления в буфер
    U_buffer[buffer_index] = U_in;

    // 3. ИСПРАВЛЕНО: Математически точный сдвиг для кольцевого буфера
    float delay_steps_float = current_theta_p / DT;
    uint32_t delay_steps = (uint32_t)delay_steps_float;
    float frac = delay_steps_float - (float)delay_steps;

    // Сдвигаем базовый индекс на -1 шаг назад в прошлое, 
    // чтобы компенсировать текущую запись и влияние интерполяции
    int32_t idx_curr = (int32_t)buffer_index - (int32_t)delay_steps - 1;
    if (idx_curr < 0) idx_curr += MAX_DELAY_STEPS;

    // Следующий элемент находится еще дальше в прошлом
    int32_t idx_next = idx_curr - 1;
    if (idx_next < 0) idx_next += MAX_DELAY_STEPS;

    // Линейная интерполяция между двумя точками прошлого
    float u_delayed = U_buffer[idx_curr] + frac * (U_buffer[idx_next] - U_buffer[idx_curr]);


    // Инкремент циклического указателя записи
    buffer_index++;
    if (buffer_index >= MAX_DELAY_STEPS) buffer_index = 0;

    // Инкремент фазы синусоиды (без фазовых прыжков)
    pert_phase += (M_TWO_PI * DT) / t_pert;
    if (pert_phase >= M_TWO_PI) pert_phase -= M_TWO_PI;
    float Y_pert = T_pert * sinf(pert_phase);

    // Расчет дифференциального уравнения
    float dydt = (-Y_current + (K_p * u_delayed) + Y0 + Y_pert) / tau_p;
    Y_current = Y_current + (dydt * DT);
    
    float Y_podacha_final = Y_current;

    // Экспоненциальное сглаживание для Обратки (использует оптимизированную alpha_ret)
    Y_ret_avg = (alpha_ret * Y_current) + ((1.0f - alpha_ret) * Y_ret_avg);
    float Y_return = Y0 + coef_ret * (Y_ret_avg - Y0);

    // Ограничение снизу
    if (Y_podacha_final < 1.0f) Y_podacha_final = 1.0f;
    if (Y_return < 1.0f)        Y_return = 1.0f;

    // Конвертация и работа с аппаратными потенциометрами
    uint32_t target_ohm1 = Convert_Temperature_To_Ohms(Y_podacha_final, 1);
    uint32_t target_ohm2 = Convert_Temperature_To_Ohms(Y_return, 2);
    
    DB_Value_t val;
    if (DB_Select(OUT_1_TEMP, &val)) { val.raw_data = (int32_t)(Y_podacha_final * 10.0f + 0.5f); DB_Insert(OUT_1_TEMP, val); }
    if (DB_Select(OUT_2_TEMP, &val)) { val.raw_data = (int32_t)(Y_return * 10.0f + 0.5f);        DB_Insert(OUT_2_TEMP, val); }

    if ((int32_t)target_ohm1 != last_applied_ohm1)
    {
        uint32_t ch0, ch1;
        FindOptimalSteps(pot1, (float)target_ohm1, &ch0, &ch1);
        POT1_WRITE(0, ch0); POT1_WRITE(1, ch1);
        last_applied_ohm1 = target_ohm1;
        if (DB_Select(OUT_1_CH_0, &val)) { val.raw_data = ch0; DB_Insert(OUT_1_CH_0, val); }
        if (DB_Select(OUT_1_CH_1, &val)) { val.raw_data = ch1; DB_Insert(OUT_1_CH_1, val); }
        if (DB_Select(OUT_1_OHM,  &val)) { val.raw_data = target_ohm1; DB_Insert(OUT_1_OHM, val); }
    }

    if ((int32_t)target_ohm2 != last_applied_ohm2)
    {
        uint32_t ch0, ch1;
        FindOptimalSteps(pot2, (float)target_ohm2, &ch0, &ch1);
        POT2_WRITE(0, ch0); POT2_WRITE(1, ch1);
        last_applied_ohm2 = target_ohm2;
        if (DB_Select(OUT_2_CH_0, &val)) { val.raw_data = ch0; DB_Insert(OUT_2_CH_0, val); }
        if (DB_Select(OUT_2_CH_1, &val)) { val.raw_data = ch1; DB_Insert(OUT_2_CH_1, val); }
        if (DB_Select(OUT_2_OHM,  &val)) { val.raw_data = target_ohm2; DB_Insert(OUT_2_OHM, val); }
    }
}

static void ProcessCalibrationMode(uint32_t* step, uint32_t* ticks)
{
    (*ticks)++;
    if (*ticks >= 50) 
    {
        *ticks = 0;
        if (*step < 255) 
        {
            (*step)++;
        }
    }

    uint8_t channels[] = { OUT_1_CH_0, OUT_1_CH_1, OUT_2_CH_0, OUT_2_CH_1 };
    for (uint8_t i = 0; i < sizeof(channels); i++)
    {
        DB_Value_t ch_val;
        if (DB_Select(channels[i], &ch_val) && ch_val.raw_data != *step)
        {
            ch_val.raw_data = *step;
            DB_Insert(channels[i], ch_val);
        }
    }

    POT1_WRITE(0, *step); POT1_WRITE(1, *step);
    POT2_WRITE(0, *step); POT2_WRITE(1, *step);
}

// ====================================================================
// ОСНОВНОЙ ДИСПЕТЧЕР (STATE MACHINE)
// ====================================================================

void ResistorControl(void)
{
    int32_t lt1 = -1, lc1_0 = -1, lc1_1 = -1;
    int32_t lt2 = -1, lc2_0 = -1, lc2_1 = -1;

    uint32_t cal_step = 0;
    uint32_t cal_ticks = 0;
    uint32_t prev_mode = 0xFFFFFFFF; 

    DualDigitalRes pot1 = { .ratedRes = calibrate[255], .channel0_step = 255, .channel1_step = 255, .calibrate = calibrate };
    DualDigitalRes pot2 = { .ratedRes = calibrate[255], .channel0_step = 255, .channel1_step = 255, .calibrate = calibrate };

    POT1_WRITE(0, 255); POT1_WRITE(1, 255);
    POT2_WRITE(0, 255); POT2_WRITE(1, 255);

    for (;;)
    {
        DB_Value_t main_switch;
        if (!DB_Select(MAIN_SWITCH, &main_switch)) main_switch.raw_data = 0;
        uint32_t mode = main_switch.raw_data;
        
        bool mode_changed = (mode != prev_mode);

        if (mode != prev_mode)
        {
            bool manual_en = (mode == 0);
            uint8_t keys[] = { OUT_1_OHM, OUT_2_OHM, OUT_1_CH_0, OUT_1_CH_1, OUT_2_CH_0, OUT_2_CH_1 };
            for (uint8_t i = 0; i < sizeof(keys); i++)
            {
                DB_Value_t val;
                if (DB_Select(keys[i], &val) && val.is_enabled != manual_en)
                {
                    val.is_enabled = manual_en;
                    DB_Insert(keys[i], val);
                }
            }
            lt1 = lt2 = lc1_0 = lc1_1 = lc2_0 = lc2_1 = -1;
            cal_step = 0; cal_ticks = 0;
            prev_mode = mode;
        }

        switch (mode)
        {
            case 0:  ProcessManualMode(&pot1, &pot2, &lt1, &lc1_0, &lc1_1, &lt2, &lc2_0, &lc2_1); break;
            case 1: ProcessDiffEqMode(&pot1, &pot2, mode_changed); break;
            case 2:  ProcessCalibrationMode(&cal_step, &cal_ticks); break;
            default: break;
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
