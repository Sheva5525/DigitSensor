#include <core>

#pragma dynamic 256

native SetOhm(ohm_value);
native SetLedState(led_state);
native Delay(delay_ms);

main()
{

    Delay(200);

    Delay(200);

    new target_ohm = 50;
    new direction = 1;
    new keep_running = 1;
    new led_f = 0;

    while (keep_running)
    {
        if (led_f == 0)
        {
            led_f = 1;
        }
        else
        {
            led_f = 0;
        }

        SetLedState(led_f);
        SetOhm(target_ohm);

        target_ohm += direction;

        if (target_ohm >= 500)
        {
            direction = -10;
        }
        else if (target_ohm <= 50)
        {
            direction = 10;
        }

        Delay(2000);
    }
}

